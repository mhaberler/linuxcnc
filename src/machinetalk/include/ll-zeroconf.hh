/*
 * minimalistic zeroconf publish interface
 * low-level Avahi interface, avahi poll loop integration
 */

#ifndef _LL_ZEROCONF_H
#define _LL_ZEROCONF_H

#include <inttypes.h>
#include <avahi-common/strlst.h>
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/malloc.h>
#include <avahi-common/error.h>
#include <avahi-common/alternative.h>
#include <avahi-common/timeval.h>

#include <avahi-client/client.h>
#include <avahi-client/lookup.h>
#include <avahi-client/publish.h>


#include "czmq.h"
#include "czmq-watch.h"
#include "mk-zeroconf-types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    AvahiProtocol proto; //  AVAHI_PROTO_INET6,  AVAHI_PROTO_INET, AVAHI_PROTO_UNSPEC
    int port;
    const char *type;
    AvahiStringList *subtypes;
    AvahiIfIndex interface; // usually AVAHI_IF_UNSPEC
    AvahiStringList *txt;   // must contain uuid=<instance uuid>
    zloop_t *loop;
} zservice_t;

typedef struct  {
    AvahiCzmqPoll *czmq_poll;
    AvahiClient *client;
    AvahiEntryGroup *group;
    zservice_t *service;
    char *name;
} register_context_t;

    register_context_t *ll_zeroconf_register(zservice_t *s, AvahiCzmqPoll *av_loop);
    int   ll_zeroconf_unregister(register_context_t *s);

typedef enum {
    SD_OK,  // service was found, single UUID matched ok if UUID was given
    SD_TIMEOUT,
    SD_RESOLVER_FAILURE,
    SD_BROWSER_FAILURE,
    SD_CLIENT_FAILURE,
    SD_OTHER,
    SD_UNSET
} zresult_t;

typedef struct {
    AvahiProtocol proto;     //  AVAHI_PROTO_INET6,  AVAHI_PROTO_INET, AVAHI_PROTO_UNSPEC
    AvahiIfIndex interface;  // usually AVAHI_IF_UNSPEC
    char *type;
    const char *match;
    char *domain;
    int timeout_ms;

    AvahiStringList *txt;
    AvahiAddress address;
    char *name;
    char *host_name;
    int port;
    AvahiLookupResultFlags flags;
    int result;
} zresolve_t;

typedef struct  {
    AvahiSimplePoll *simple_poll;
    AvahiClient *client;
    AvahiServiceResolver *resolver;
    AvahiServiceBrowser *browser;
    zresolve_t *resolve;
} resolve_context_t;

    resolve_context_t *ll_zeroconf_resolve(zresolve_t *s);
    int ll_zeroconf_resolve_free(resolve_context_t *p);





#ifdef __cplusplus
}
#endif

#endif
