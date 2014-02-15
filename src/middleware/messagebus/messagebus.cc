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
#include <czmq.h>

#include <string>
#include <unordered_set>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_ring.h>
#include <sdpublish.h>  // for UDP service discovery
#include <redirect_log.h>

#include <inifile.h>

#include <middleware/generated/message.pb.h>
using namespace google::protobuf;

#include "messagebus.hh"
#include "rtproxy.hh"

#define MESSAGEBUS_VERSION 1 // protocol version

typedef std::unordered_set<std::string> actormap_t;
typedef actormap_t::iterator actormap_iterator;

static const char *option_string = "hI:S:dr:R:c:C:tp:DP:";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", no_argument, 0, 'd'},
    {"sddebug", no_argument, 0, 'D'},
    {"sdport", required_argument, 0, 'P'},
    {"textreply", no_argument, 0, 't'},
    {"responsein", required_argument, 0, 'r'},
    {"responseout", required_argument, 0, 'R'},
    {"cmdin", required_argument, 0, 'c'},
    {"cmdout", required_argument, 0, 'C'},
    {"rtproxy", required_argument, 0, 'p'},
    {0,0,0,0}
};


static int sd_port = SERVICE_DISCOVERY_PORT;

const char *progname = "messagebus";
static const char *inifile;
static const char *section = "MSGBUS";
static const char *cmd_in_uri = "tcp://127.0.0.1:5570";
static const char *cmd_out_uri = "tcp://127.0.0.1:5571";
static const char *response_in_uri = "tcp://127.0.0.1:5572";
static const char *response_out_uri = "tcp://127.0.0.1:5573";

// inproc variant for comms with RT proxy threads
static const char *proxy_cmd_in_uri = "inproc://cmd-in";
static const char *proxy_cmd_out_uri = "inproc://cmd-out";
static const char *proxy_response_in_uri = "inproc://response-in";
static const char *proxy_response_out_uri = "inproc://response-out";

static int debug;
static int sddebug;
int comp_id;
// return error messages in strings instead of  protobuf Containers:
static int textreplies;
static int signal_fd;

typedef struct {
    void *response_in;
    void *response_out;
    void *cmd_in;
    void *cmd_out;
    actormap_t *cmd_subscribers;
    actormap_t *response_subscribers;
    int comp_id;
    zctx_t *context;
    zloop_t *loop;
    spub_t *sd_publisher;
} msgbusd_self_t;


static int handle_router_in(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    msgbusd_self_t *self = (msgbusd_self_t *) arg;
    actormap_t *map;
    void *forward;
    const char *rail;

    char *from  = zstr_recv(poller->socket);
    char *to    = zstr_recv(poller->socket);
    zframe_t *payload = zframe_recv(poller->socket);

    if (poller->socket == self->cmd_in) {
	map = self->cmd_subscribers;
	forward = self->cmd_out;
	rail = "cmd-in";
    } else {
	map = self->response_subscribers;
	forward = self->response_out;
	rail = "response-in";
    }

    if (map->find(to) == map->end()) {
	char errmsg[100];
	snprintf(errmsg, sizeof(errmsg), "rail %s: no such destination: %s", rail, to);
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: %s\n", progname,errmsg);

	// we wont get a reply from a non-existent actor
	// so send error message instead:

	assert(zstr_sendm(self->response_out, from) == 0);  // originator
	if (textreplies) {
	    assert(zstr_send(self->response_out, errmsg) == 0);
	} else {
	    pb::Container c;
	    c.set_type(pb::MT_MESSAGEBUS_NO_DESTINATION);
	    c.set_name(to);
	    c.set_note(errmsg);
	    zframe_t *errorframe = zframe_new(NULL, c.ByteSize());
	    assert(c.SerializeWithCachedSizesToArray(zframe_data(errorframe)));
	    assert(zframe_send(&errorframe, self->response_out, 0) == 0);
	}
	zframe_destroy(&payload);
    } else {
	// forward
	zstr_sendm(forward, to);          // topic
	zstr_sendm(forward, from);        // destination
	zframe_send(&payload, forward, 0);
    }
    free(from);
    free(to);
    return 0;
}

static int handle_xpub_in(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    msgbusd_self_t *self = (msgbusd_self_t *) arg;
    actormap_t *map;
    bool subscribe;
    char *data, *topic;
    const char *rail;

    zframe_t *f = zframe_recv(poller->socket);
    data = (char *) zframe_data(f);
    assert(data);
    subscribe = (*data == '\001');
    topic = data + 1;

    if (poller->socket == self->cmd_out) {
	map = self->cmd_subscribers;
	rail = "cmd-out";
    } else {
	map = self->response_subscribers;
	rail = "response-out";
    }
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: rail %s: %s %ssubscribed\n",
		    progname, rail, topic,
		    subscribe ? "" : "un");
    if (subscribe)
	map->insert(topic);
    else
	map->erase(topic);
    return 0;
}


static int handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(poller->fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }
    rtapi_print_msg(RTAPI_MSG_ERR, "%s: signal %d - '%s' received\n",
			progname, fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    return -1; // exit reactor with -1
}

static int signal_setup(void)
{
    sigset_t sigmask;

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    // must happen after zctx_new()
    zsys_handler_set(NULL);

    sigemptyset(&sigmask);

    // block all signal delivery through default signal handlers
    // since we're using signalfd()
    sigfillset(&sigmask);
    assert(sigprocmask(SIG_SETMASK, &sigmask, NULL) == 0);

    // explicitly enable signals we want delivered via signalfd()
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGINT);
    sigaddset(&sigmask, SIGQUIT);
    sigaddset(&sigmask, SIGTERM);
    sigaddset(&sigmask, SIGSEGV);
    sigaddset(&sigmask, SIGFPE);

    if ((signal_fd = signalfd(-1, &sigmask, 0)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signaldfd() failed: %s\n",
			progname,  strerror(errno));
	return -1;
    }
    return 0;
}


static int mainloop(msgbusd_self_t *self)
{


    self->loop = zloop_new();
    assert(self->loop);
    zloop_set_verbose (self->loop, debug);

    zmq_pollitem_t signal_poller =        { 0, signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t cmd_in_poller =        { self->cmd_in, 0, ZMQ_POLLIN };
    zmq_pollitem_t cmd_out_poller =       { self->cmd_out, 0, ZMQ_POLLIN };
    zmq_pollitem_t response_in_poller =   { self->response_in, 0, ZMQ_POLLIN };
    zmq_pollitem_t response_out_poller =  { self->response_out, 0, ZMQ_POLLIN };

    zloop_poller(self->loop, &signal_poller, handle_signal, self);
    zloop_poller(self->loop, &cmd_in_poller,  handle_router_in, self);
    zloop_poller(self->loop, &cmd_out_poller, handle_xpub_in, self);
    zloop_poller(self->loop, &response_in_poller, handle_router_in, self);
    zloop_poller(self->loop, &response_out_poller, handle_xpub_in, self);

    {
	int zmajor, zminor, zpatch, pbmajor, pbminor, pbpatch;
	zmq_version (&zmajor, &zminor, &zpatch);
	pbmajor = GOOGLE_PROTOBUF_VERSION / 1000000;
	pbminor = (GOOGLE_PROTOBUF_VERSION / 1000) % 1000;
	pbpatch = GOOGLE_PROTOBUF_VERSION % 1000;

	rtapi_print_msg(RTAPI_MSG_DBG, "%s: startup ØMQ=%d.%d.%d protobuf=%d.%d.%d",
			progname, zmajor, zminor, zpatch, pbmajor, pbminor, pbpatch);
    }
    zloop_start(self->loop);
    return 0;
}

static int zmq_setup(msgbusd_self_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    self->context = zctx_new ();
    assert(self->context);
    zctx_set_linger (self->context, 0);

    self->cmd_in = zsocket_new (self->context, ZMQ_ROUTER);
    zsocket_set_identity(self->cmd_in, "cmd-in");
    assert(zsocket_bind(self->cmd_in, cmd_in_uri));
    assert(zsocket_bind(self->cmd_in, proxy_cmd_in_uri) > -1);

    self->cmd_out = zsocket_new (self->context, ZMQ_XPUB);
    assert(self->cmd_out);
    zsocket_set_xpub_verbose (self->cmd_out, 1);
    assert(zsocket_bind(self->cmd_out, cmd_out_uri));
    assert(zsocket_bind(self->cmd_out, proxy_cmd_out_uri) > -1);

    self->response_in = zsocket_new (self->context, ZMQ_ROUTER);
    zsocket_set_identity(self->response_in, "response-in");
    assert(zsocket_bind(self->response_in, response_in_uri));
    assert(zsocket_bind(self->response_in, proxy_response_in_uri) > -1);

    self->response_out = zsocket_new (self->context, ZMQ_XPUB);
    assert(self->response_out);
    zsocket_set_xpub_verbose (self->response_out, 1);
    assert(zsocket_bind(self->response_out, response_out_uri));
    assert(zsocket_bind(self->response_out, proxy_response_out_uri) > -1);

    self->cmd_subscribers = new actormap_t();
    self->response_subscribers = new actormap_t();

    return 0;
}

static int sd_init(msgbusd_self_t *self, int port)
{
    int retval;

    if (!port)
	return 0;  // service discovery disabled

    // start the service announcement responder
    self->sd_publisher = sp_new(self->context, port,
				rtapi_instance);

    assert(self->sd_publisher != NULL);
    sp_log(self->sd_publisher, sddebug);
    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_MESSAGEBUS_COMMAND_IN, //type
		    MESSAGEBUS_VERSION, // version
		    NULL, // ip
		    0, // port
		    proxy_cmd_in_uri,
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "messagebus command input socket");  // descr
    assert(retval == 0);
    assert(sp_start(self->sd_publisher) == 0);
    return 0;
}

static rtproxy_t echo, demo, too;


static int rtproxy_setup(msgbusd_self_t *self)
{
    echo.flags = ACTOR_ECHO|ACTOR_TRACE;
    echo.name = "echo";
    echo.pipe = zthread_fork (self->context, rtproxy_thread, &echo);
    assert (echo.pipe);

    demo.flags = ACTOR_RESPONDER|ACTOR_TRACE;
    demo.state = IDLE;
    demo.min_delay = 2;   // msec
    demo.max_delay = 200; // msec

    demo.name = "demo";
    demo.to_rt_name = "pbring.0.in";
    demo.from_rt_name = "pbring.0.out";
    demo.pipe = zthread_fork (self->context, rtproxy_thread, &demo);
    assert (demo.pipe);

    too.flags = ACTOR_RESPONDER|ACTOR_TRACE;
    too.state = IDLE;
    too.min_delay = 2;   // msec
    too.max_delay = 200; // msec
    too.name = "too";
    too.to_rt_name = "pbring.1.in";
    too.from_rt_name = "pbring.1.out";
    too.pipe = zthread_fork (self->context, rtproxy_thread, &too);
    assert (too.pipe);

    return 0;
}

static int hal_setup(void)
{
    if ((comp_id = hal_init(progname)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n",
			progname, progname, comp_id);
	return -1;
    }
    hal_ready(comp_id);
    return 0;
}

static int read_config(void )
{
    const char *s;
    FILE *inifp;

    if (!inifile)
	return 0; // use compiled-in defaults

    if ((inifp = fopen(inifile,"r")) == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: cant open inifile '%s'\n",
			progname, inifile);
	return -1;
    }

    if ((s = iniFind(inifp, "CMD_IN", section)))
	cmd_in_uri = strdup(s);
    if ((s = iniFind(inifp, "CMD_OUT", section)))
	cmd_out_uri = strdup(s);
    if ((s = iniFind(inifp, "RESPONSE_IN", section)))
	response_in_uri = strdup(s);
    if ((s = iniFind(inifp, "RESPONSE_OUT", section)))
	response_out_uri = strdup(s);
    iniFindInt(inifp, "DEBUG", section, &debug);
    iniFindInt(inifp, "TEXTREPLIES", section, &textreplies);
    fclose(inifp);
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
    int opt;

    inifile = getenv("INI_FILE_NAME");

    while ((opt = getopt_long(argc, argv, option_string,
			      long_options, NULL)) != -1) {
	switch(opt) {
	case 'd':
	    debug++;
	    break;
	case 'D':
	    sddebug++;
	    break;
	case 't':
	    textreplies++;
	    break;
	case 'S':
	    section = optarg;
	    break;
	case 'P':
	    sd_port = atoi(optarg);
	    break;
	case 'I':
	    inifile = optarg;
	    break;
	case 'r':
	    response_in_uri = optarg;
	    break;
	case 'R':
	    response_out_uri = optarg;
	    break;
	case 'c':
	    cmd_in_uri = optarg;
	    break;
	case 'C':
	    cmd_out_uri = optarg;
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

    to_syslog("messagebus> ", &stdout); // redirect stdout to syslog
    to_syslog("messagebus>> ", &stderr);  // redirect stderr to syslog


    if (read_config())
	exit(1);

    msgbusd_self_t self = {0};
    if (!zmq_setup(&self) &&
	!hal_setup() &&
	!signal_setup() &&
	!sd_init(&self, sd_port) &&
	!rtproxy_setup(&self)) {

	mainloop(&self);
    }

    // stop  the service announcement responder
    sp_destroy(&self.sd_publisher);

    // shutdown zmq context
    zctx_destroy (&self.context);

    if (comp_id)
	hal_exit(comp_id);
    exit(0);
}
