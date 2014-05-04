/*
 * minimalistic zeroconf interface
 *
 * Michael Haberler 2014
 * based on distcc code by Lennart Poettering,  Copyright (C) 2007
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <avahi-common/thread-watch.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/malloc.h>
#include <avahi-client/publish.h>

#include "config.h"
#include "mk-zeroconf.hh"
#include "syslog_async.h"

struct context {
    AvahiThreadedPoll *threaded_poll;
    AvahiClient *client;
    AvahiEntryGroup *group;
    const zservice_t *service;
    char *name;
};

static void publish_reply(AvahiEntryGroup *g,
                          AvahiEntryGroupState state,
                          void *userdata);

static void register_stuff(struct context *ctx)
{
    if (!ctx->group) {
        if (!(ctx->group = avahi_entry_group_new(ctx->client,
                                                 publish_reply,
                                                 ctx))) {
            syslog_async(LOG_ERR,
			 "zeroconf: Failed to create avahi entry group: %s\n",
			 avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }
    }
    if (avahi_entry_group_is_empty(ctx->group)) {
	// Register our service
        if (avahi_entry_group_add_service_strlst(
						 ctx->group,
						 AVAHI_IF_UNSPEC,
						 ctx->service->ipv6 ?  AVAHI_PROTO_INET:AVAHI_PROTO_UNSPEC,
						 (AvahiPublishFlags) 0,
						 ctx->name,
						 ctx->service->type,
						 NULL,
						 NULL,
						 ctx->service->port,
						 ctx->service->txt) < 0) {
            syslog_async(LOG_ERR,
			 "zeroconf: Failed to add service: %s\n",
			 avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }
        if (avahi_entry_group_commit(ctx->group) < 0) {
            syslog_async(LOG_ERR,
			 "zeroconf: Failed to commit entry group: %s\n",
			 avahi_strerror(avahi_client_errno(ctx->client)));
            goto fail;
        }
    }
    return;

 fail:
    // Stop the avahi client, if it's running.
    if (ctx->threaded_poll)
	avahi_threaded_poll_stop(ctx->threaded_poll);
    ctx->threaded_poll = NULL;
    avahi_free(ctx->name);
}

// Called when publishing of service data completes
static void publish_reply(AvahiEntryGroup *g,
                          AvahiEntryGroupState state,
			  void *userdata)
{
    struct context *ctx = (struct context *)userdata;
    switch (state) {
    case AVAHI_ENTRY_GROUP_COLLISION: {
	// Pick a new name for our service
	char *n = avahi_alternative_service_name(ctx->name);
	assert(n);
	avahi_free(ctx->name);
	ctx->name = n;
	syslog_async(LOG_INFO,
		     "zeroconf: collision - register alternative"
		     " service name: '%s'\n",
		     ctx->name);
	register_stuff(ctx);
	break;
    }
    case AVAHI_ENTRY_GROUP_FAILURE:
	syslog_async(LOG_ERR,
		     "zeroconf: Failed to register service '%s': %s (avahi running?)\n",
		     ctx->name,
		     avahi_strerror(avahi_client_errno(ctx->client)));
	if (ctx->threaded_poll)
	    avahi_threaded_poll_quit(ctx->threaded_poll);
	break;
    case AVAHI_ENTRY_GROUP_UNCOMMITED:
	break;
    case AVAHI_ENTRY_GROUP_REGISTERING:
	syslog_async(LOG_DEBUG, "zeroconf: registering: '%s'\n", ctx->name);
	break;
    case AVAHI_ENTRY_GROUP_ESTABLISHED:
	{
	    char *txt = avahi_string_list_to_string(ctx->service->txt);
	    syslog_async(LOG_INFO,
			 "zeroconf: registered '%s' %s %d TXT %s\n",
			 ctx->name, ctx->service->type, ctx->service->port, txt);
	    avahi_free(txt);
	}
    }
}

static void client_callback(AvahiClient *client,
                            AvahiClientState state,
                            void *userdata)
{
    struct context *ctx = (struct context *)userdata;
    ctx->client = client;

    switch (state) {
    case AVAHI_CLIENT_S_RUNNING:
	register_stuff(ctx);
	break;

    case AVAHI_CLIENT_S_COLLISION:
    case AVAHI_CLIENT_S_REGISTERING:
	if (ctx->group)
	    avahi_entry_group_reset(ctx->group);
	break;

    case AVAHI_CLIENT_FAILURE:
	if (avahi_client_errno(client) == AVAHI_ERR_DISCONNECTED) {
	    int error;
	    avahi_client_free(ctx->client);
	    ctx->client = NULL;
	    ctx->group = NULL;
	    // Reconnect to the server
	    if (!(ctx->client = avahi_client_new(
						 avahi_threaded_poll_get(ctx->threaded_poll),
						 AVAHI_CLIENT_NO_FAIL,
						 client_callback,
						 ctx,
						 &error))) {
		syslog_async(LOG_ERR,
			     "zeroconf: Failed to contact mDNS server: %s\n",
			     avahi_strerror(error));
		avahi_threaded_poll_quit(ctx->threaded_poll);
	    }
	} else {
	    syslog_async(LOG_ERR,"zeroconf: Client failure: %s\n",
			 avahi_strerror(avahi_client_errno(client)));
	    avahi_threaded_poll_quit(ctx->threaded_poll);
	}
	break;

    case AVAHI_CLIENT_CONNECTING:
	syslog_async(LOG_ERR, "zeroconf: connecting - mDNS server not yet available"
		     " (avahi-daemon not installed or not runnung?)\n");

	;
    }
}

// register a service in DNS-SD/mDNS
void *mk_zeroconf_register(const zservice_t *s)
{
    struct context *ctx = NULL;
    int error;

    ctx = (struct context *)malloc(sizeof(struct context));
    assert(ctx);
    ctx->client = NULL;
    ctx->group = NULL;
    ctx->threaded_poll = NULL;
    ctx->service = s;
    assert(ctx->service->name);
    ctx->name = strdup(ctx->service->name); // might be renamed
    assert(ctx->name);

    if (!(ctx->threaded_poll = avahi_threaded_poll_new())) {
        syslog_async(LOG_ERR,
		     "zeroconf: Failed to create avahi event loop object.\n");
        goto fail;
    }

    if (!(ctx->client = avahi_client_new(avahi_threaded_poll_get(ctx->threaded_poll),
                                         AVAHI_CLIENT_NO_FAIL,
                                         client_callback,
                                         ctx,
                                         &error))) {
        syslog_async(LOG_ERR,
		     "zeroconf: Failed to create avahi client object: %s\n",
		     avahi_strerror(avahi_client_errno(ctx->client)));
        goto fail;
    }

    // Create the mDNS event handler
    if ((ctx->threaded_poll == NULL) ||
	avahi_threaded_poll_start(ctx->threaded_poll) < 0) {
        syslog_async(LOG_ERR,"zeroconf: Failed to create avahi thread.\n");
	return NULL;
    }
    return ctx;

 fail:
    if (ctx)
        mk_zeroconf_unregister(ctx);
    return NULL;
}

// Unregister this server from DNS-SD/mDNS
int mk_zeroconf_unregister(void *u)
{
    struct context *ctx = (struct context *)u;
    syslog_async(LOG_INFO, "zeroconf: unregistering '%s'\n", ctx->name);

    if (ctx->threaded_poll)
        avahi_threaded_poll_stop(ctx->threaded_poll);

    if (ctx->client)
        avahi_client_free(ctx->client);

    if (ctx->threaded_poll)
        avahi_threaded_poll_free(ctx->threaded_poll);

    avahi_free(ctx->name);
    free(ctx);
    return 0;
}
