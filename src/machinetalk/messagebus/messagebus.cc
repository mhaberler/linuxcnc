/*
 * Copyright (C) 2013-2014 Michael Haberler <license@mah.priv.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

// msgbusd acts as a switching fabric between RT and non-RT actors.
// see https://github.com/mhaberler/messagebus for an overview.

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <getopt.h>
#include <syslog.h>
#include <czmq.h>
#include <syslog_async.h>

#include <string>
#include <unordered_set>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_ring.h>
#include <setup_signals.h>
#include <mk-zeroconf.hh>
#include <mk-service.hh>
#include <inifile.h>
#include <inihelp.hh>

#include <machinetalk/generated/message.pb.h>
using namespace google::protobuf;

#include "messagebus.hh"
#include "rtproxy.hh"

#define MESSAGEBUS_VERSION 1 // protocol version

typedef std::unordered_set<std::string> actormap_t;
typedef actormap_t::iterator actormap_iterator;

static const char *option_string = "hI:S:dtp:";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", no_argument, 0, 'd'},
    {"textreply", no_argument, 0, 't'},
    {"rtproxy", required_argument, 0, 'p'},
    {0,0,0,0}
};


static const char *inifile;
static const char *section = "MSGBUS";

// inproc variant for comms with RT proxy threads (not announced)
const char *proxy_cmd_uri = "inproc://messagebus.cmd";
const char *proxy_response_uri = "inproc://messagebus.response";

const char *progname = "";

static int debug;
int comp_id;
int instance_id; // HAL instance

static int textreplies; // return error messages in strings instead of protobuf Containers
static int signal_fd;

#define NSVCS  2
enum {
    SVC_MBUS_CMD = 0,
    SVC_MBUS_RESPONSE,
};
typedef struct {
    mk_netopts_t netopts;
    mk_socket_t mksock[NSVCS];
    actormap_t *cmd_subscribers;
    actormap_t *response_subscribers;
    int comp_id;
    bool interrupted;
    FILE *inifp;
    bool trap_signals;
} msgbusd_self_t;


static int handle_xpub_in(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    msgbusd_self_t *self = (msgbusd_self_t *) arg;
    actormap_t *map;
    const char *rail;
    char *data, *topic;
    int retval;
    bool is_cmd = (poller->socket == self->mksock[SVC_MBUS_CMD].socket);

    if (is_cmd) {
	map = self->cmd_subscribers;
	rail = "cmd";
    } else {
	map = self->response_subscribers;
	rail = "response";
    }

    zmsg_t *msg = zmsg_recv(poller->socket);
    size_t nframes = zmsg_size( msg);

    if (nframes == 1) {
	// likely a subscribe/unsubscribe message
	// a proper message needs at least three parts: src, dest, contents
	    zframe_t *f = zmsg_pop(msg);
	data = (char *) zframe_data(f);
	assert(data);
	topic = data + 1;

	switch (*data) {
	case '\001':
	    map->insert(topic);
	    rtapi_print_msg(RTAPI_MSG_DBG, "%s: rail %s: %s subscribed\n",
			    progname, rail, topic);
	    break;

	case '\000':
	    map->erase(topic);
	    rtapi_print_msg(RTAPI_MSG_DBG, "%s: rail %s: %s unsubscribed\n",
			    progname, rail, topic);
	    break;

	default:
	    rtapi_print_msg(RTAPI_MSG_ERR, "%s: rail %s: invalid frame (tag=%d topic=%s)",
			    progname, rail, *data, topic);
	}
	zframe_destroy(&f);
	return 0;
    }
    if (nframes > 2) {
	// forward
	char *from  = zmsg_popstr(msg);
	char *to  = zmsg_popstr(msg);

	if (map->find(to) == map->end()) {
	    char errmsg[100];
	    snprintf(errmsg, sizeof(errmsg), "rail %s: no such destination: %s", rail, to);
	    rtapi_print_msg(RTAPI_MSG_ERR, "%s: %s\n", progname,errmsg);

	    if (is_cmd) {
		// command was directed to non-existent actor
		// we wont get a reply from a non-existent actor
		// so send error message on response rail instead:
		void *r = self->mksock[SVC_MBUS_RESPONSE].socket;
		retval = zstr_sendm(r, from);  // originator
		assert(retval == 0);
		retval = zstr_sendm(r, to);    // destination
		assert(retval == 0);

		if (textreplies) {
		    assert(zstr_send(r, errmsg) == 0);
		    assert(retval == 0);
		} else {
		    pb::Container c;
		    c.set_type(pb::MT_MESSAGEBUS_NO_DESTINATION);
		    c.set_name(to);
		    c.add_note(errmsg);
		    zframe_t *errorframe = zframe_new(NULL, c.ByteSize());
		    c.SerializeWithCachedSizesToArray(zframe_data(errorframe));
		    retval = zframe_send(&errorframe, r, 0);
		    assert(retval == 0);
		}
		zmsg_destroy(&msg);
	    } // else: response to non-existent actor is dropped
	} else {
	    // forward
	    if (debug)
		rtapi_print_msg(RTAPI_MSG_ERR, "forward: %s->%s:\n", from,to);

	    zstr_sendm(poller->socket, to);          // topic
	    zstr_sendm(poller->socket, from);        // destination
	    zmsg_send(&msg, poller->socket);
	}
	free(from);
	free(to);

    } else {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rail %s: short message (%zu frames)",
			progname, rail, nframes);
	zmsg_dump_to_stream(msg, stderr);
	zmsg_destroy(&msg);
    }
    return 0;
}


static int handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    msgbusd_self_t *self = (msgbusd_self_t *)arg;
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(poller->fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }
    rtapi_print_msg(RTAPI_MSG_ERR, "%s: signal %d - '%s' received\n",
			progname, fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    self->interrupted = true;
    return -1; // exit reactor with -1
}

static int mainloop(msgbusd_self_t *self)
{
    int retval;

    zmq_pollitem_t signal_poller =        { 0, signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t cmd_poller =           { self->mksock[SVC_MBUS_CMD].socket, 0, ZMQ_POLLIN };
    zmq_pollitem_t response_poller =      { self->mksock[SVC_MBUS_RESPONSE].socket, 0, ZMQ_POLLIN };

    zloop_t *l = self->netopts.z_loop;
    zloop_poller(l, &signal_poller,   handle_signal,  self);
    zloop_poller(l, &cmd_poller,      handle_xpub_in, self);
    zloop_poller(l, &response_poller, handle_xpub_in, self);

    do {
	retval = zloop_start(l);
    } while  (self->trap_signals && !(retval || self->interrupted));

    rtapi_print_msg(RTAPI_MSG_INFO,
		    "%s: exiting mainloop (%s)\n",
		    progname,
		    self->interrupted ? "interrupted": "reactor exited");

    return 0;
}

static int zmq_setup(msgbusd_self_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    zsys_handler_set(NULL);

    mk_netopts_t *np = &self->netopts;

    np->z_context = zctx_new ();
    assert(np->z_context);
    zctx_set_linger (np->z_context, 0);

    np->z_loop = zloop_new();
    assert (np->z_loop);
    zloop_set_verbose (np->z_loop, debug);

    np->rundir = RUNDIR;
    np->rtapi_instance = rtapi_instance;

    np->av_loop = avahi_czmq_poll_new(np->z_loop);
    assert(np->av_loop);

    mk_socket_t *ms = &self->mksock[SVC_MBUS_CMD];
    ms->dnssd_subtype = MSGBUSCMD_DNSSD_SUBTYPE;
    ms->tag = "mbcmd";
    ms->port = -1;
    ms->socket = zsocket_new (self->netopts.z_context, ZMQ_XPUB);
    assert(ms->socket);
    zsocket_set_linger(ms->socket, 0);
    zsocket_set_xpub_verbose(ms->socket, 1);
    if (mk_bindsocket(np, ms))
	return -1;
    assert(ms->port > -1);
    if (mk_announce(np, ms, "Messagebus command service", NULL))
	return -1;
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking Messagebus command on '%s'",
		    progname, ms->announced_uri);

    ms = &self->mksock[SVC_MBUS_RESPONSE];
    ms->dnssd_subtype = MSGBUSRESP_DNSSD_SUBTYPE;
    ms->tag = "mbresp";
    ms->port = -1;
    ms->socket = zsocket_new (self->netopts.z_context, ZMQ_XPUB);
    assert(ms->socket);
    zsocket_set_linger(ms->socket, 0);
    zsocket_set_xpub_verbose(ms->socket, 1);
    if (mk_bindsocket(np, ms))
	return -1;
    assert(ms->port > -1);
    if (mk_announce(np, ms, "Messagebus response service", NULL))
	return -1;
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking Messagebus response on '%s'",
		    progname, ms->announced_uri);

    usleep(200 *1000); // avoid slow joiner syndrome

    self->cmd_subscribers = new actormap_t();
    self->response_subscribers = new actormap_t();


    return 0;
}

static rtproxy_t echo, demo; //, too;


static int rtproxy_setup(msgbusd_self_t *self)
{
    echo.flags = ACTOR_ECHO|TRACE_TO_RT;
    echo.name = "echo";
    echo.pipe = zthread_fork (self->netopts.z_context, rtproxy_thread, &echo);
    assert (echo.pipe);

    demo.flags = ACTOR_RESPONDER|TRACE_FROM_RT|TRACE_TO_RT|DESERIALIZE_TO_RT|SERIALIZE_FROM_RT;
    demo.state = IDLE;
    demo.min_delay = 2;   // msec
    demo.max_delay = 200; // msec

    demo.name = "demo";
    demo.to_rt_name = "mptx.0.in";
    demo.from_rt_name = "mptx.0.out";
    demo.min_delay = 2;   // msec
    demo.max_delay = 200; // msec
    demo.pipe = zthread_fork (self->netopts.z_context, rtproxy_thread, &demo);
    assert (demo.pipe);

    // too.flags = ACTOR_RESPONDER|ACTOR_TRACE;
    // too.state = IDLE;
    // too.min_delay = 2;   // msec
    // too.max_delay = 200; // msec
    // too.name = "mptx";
    // too.to_rt_name = "mptx.0.in";
    // too.from_rt_name = "mptx.0.out";
    // too.pipe = zthread_fork (self->context, rtproxy_thread, &too);
    // assert (too.pipe);

    return 0;
}

static int hal_setup(msgbusd_self_t *self)
{
    if ((comp_id = hal_init(progname)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n",
			progname, progname, comp_id);
	return -1;
    }
    hal_ready(comp_id);

    {
	int zmajor, zminor, zpatch, pbmajor, pbminor, pbpatch;
	zmq_version (&zmajor, &zminor, &zpatch);
	pbmajor = GOOGLE_PROTOBUF_VERSION / 1000000;
	pbminor = (GOOGLE_PROTOBUF_VERSION / 1000) % 1000;
	pbpatch = GOOGLE_PROTOBUF_VERSION % 1000;
	rtapi_print_msg(RTAPI_MSG_DBG,
	       "%s: startup ØMQ=%d.%d.%d protobuf=%d.%d.%d",
	       progname, zmajor, zminor, zpatch, pbmajor, pbminor, pbpatch);
    }
    return 0;
}

static void sigaction_handler(int sig, siginfo_t *si, void *uctx)
{
    syslog_async(LOG_ERR,"signal %d - '%s' received, dumping core (current dir=%s)",
		    sig, strsignal(sig), get_current_dir_name());
    closelog_async(); // let syslog_async drain
    sleep(1);
    // reset handler for current signal to default
    signal(sig, SIG_DFL);
    // and re-raise so we get a proper core dump and stacktrace
    kill(getpid(), sig);
    sleep(1);
}

static int read_config(msgbusd_self_t *self, const char *inifile)
{
    FILE *inifp = self->netopts.mkinifp;

    iniFindInt(inifp, "DEBUG", section, &debug);
    iniFindInt(inifp, "TEXTREPLIES", section, &textreplies);
    iniFindInt(inifp, "MBUS_CMD_PORT",
	       "MACHINEKIT", &self->mksock[SVC_MBUS_CMD].port);
    iniFindInt(inifp, "MBUS_RESPONSE_PORT",
	       "MACHINEKIT", &self->mksock[SVC_MBUS_RESPONSE].port);
    return 0;
}

static int parse_proxy(const char *s)
{
    return 0;
}


static void usage(void) {
    printf("Usage:  messagebus [options]\n");
    printf("This is a userspace HAL program, typically loaded "
	   "using the halcmd \"loadusr\" command:\n"
	   "    loadusr messagebus [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment"
	   " variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'VFS11')\n"
	   "-d --debug\n"
	   "    increase debug level.\n"
	   "-t or --textreply\n"
	   "    send error messages on response-out as strings (default protobuf)\n"
	   "-d or --debug\n"
	   "    Turn on event debugging messages.\n");
}


int main (int argc, char *argv[])
{
    int opt, retval;
    int logopt = LOG_LOCAL1;
    msgbusd_self_t self = {};
    self.trap_signals = true;

    progname = argv[0];
    inifile = getenv("INI_FILE_NAME");

    while ((opt = getopt_long(argc, argv, option_string,
			      long_options, NULL)) != -1) {
	switch(opt) {
	case 'd':
	    debug++;
	    break;
	case 't':
	    textreplies++;
	    break;
	case 'S':
	    section = optarg;
	    break;
	case 'I':
	    inifile = optarg;
	    break;
	case 'p':
	    parse_proxy(optarg);
	    break;

	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    openlog_async(progname, logopt, LOG_LOCAL1);

    // ease debugging with gdb - disable all signal handling
    if (getenv("NOSIGHDLR") != NULL)
	self.trap_signals = false;

    // generic binding & announcement parameters
    // from $MACHINEKIT_INI
    self.netopts.rundir = RUNDIR;
    if (mk_getnetopts(&self.netopts))
	exit(1);

    if (read_config(&self, inifile))
	exit(1);

    retval = zmq_setup(&self);
    if (retval) exit(retval);

    retval = hal_setup(&self);
    if (retval) exit(retval);

    signal_fd = setup_signals(sigaction_handler, SIGINT, SIGQUIT, SIGKILL, SIGTERM, -1);
    if (signal_fd < 0)
	exit(1);

    retval = rtproxy_setup(&self);
    if (retval) exit(retval);

    mainloop(&self);

    // shutdown zmq context
    zctx_destroy (&self.netopts.z_context);

    if (comp_id)
	hal_exit(comp_id);
    exit(0);
}
