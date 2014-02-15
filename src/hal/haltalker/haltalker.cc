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

// haltalker reports the status of HAL signals aggregated into
// HAL groups.
// The status and changes are reported via the Status Tracking protocol.

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <getopt.h>

#include <string>
#include <unordered_map>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_group.h>
#include <inifile.h>
#include <sdpublish.h>  // for UDP service discovery
#include <redirect_log.h>

#include <czmq.h>

#include <middleware/generated/message.pb.h>
using namespace google::protobuf;

#define STP_VERSION 1 // announced protocol version

#if JSON_TIMING
#include <middleware/json2pb/json2pb.h>
#include <jansson.h>
#endif

typedef enum {
    GROUP_REPORT_FULL = 1,
} group_flags_t;

typedef  std::unordered_map<std::string, hal_compiled_group_t *> groupmap_t;
typedef groupmap_t::iterator groupmap_iterator;

static const char *option_string = "hI:S:dt:D";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {"sddebug", required_argument, 0, 'D'},
    {"timer", required_argument, 0, 't'},
    {0,0,0,0}
};

typedef struct htconf {
    const char *progname;
    const char *inifile;
    const char *section;
    const char *modname;
    const char *status;
    //const char *command;
    int debug;
    int sddebug;
    int pid;
    int default_group_timer; // msec
    int sd_port;
} htconf_t;

htconf_t conf = {
    "",
    NULL,
    "HALTALKER",
    "haltalker",
    "tcp://127.0.0.1:6042",
    // "tcp://127.0.0.1:6651",
    0,
    0,
    100,
};

typedef struct htself {
    htconf_t *cfg;
    int comp_id;
    groupmap_t groups;
    zctx_t *z_context;
    void *z_command, *z_status;
    int signal_fd;
    zloop_t *z_loop;
    pb::Container update;
    int serial;
    spub_t *sd_publisher;
} htself_t;

// static int handle_command(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
// {
//     return 0;
// }

static int handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *)arg;
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(self->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }
    switch (fdsi.ssi_signo) {
    case SIGINT:
    case SIGQUIT:
	break;
    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signal %d - '%s' received\n",
			self->cfg->progname, fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    }
    return -1; // exit reactor with -1
}


int group_report_cb(int phase, hal_compiled_group_t *cgroup, int handle,
		    hal_sig_t *sig, void *cb_data)
{
    htself_t *self = (htself_t *) cb_data;
    hal_data_u *vp;
    pb::Signal *signal;
    zmsg_t *msg;

    switch (phase) {

    case REPORT_BEGIN:	// report initialisation
	// this enables a new subscriber to easily detect she's receiving
	// a full status snapshot, not just a change tracking update
	if (cgroup->user_flags & GROUP_REPORT_FULL)
	    self->update.set_type(pb::MT_STP_UPDATE_FULL);
	else
	    self->update.set_type(pb::MT_STP_UPDATE);
	self->update.set_serial(self->serial++);
	break;

    case REPORT_SIGNAL: // per-reported-signal action
	signal = self->update.add_signal();
	vp = (hal_data_u *) SHMPTR(sig->data_ptr);
	switch (sig->type) {
	default:
	    assert("invalid signal type" == NULL);
	case HAL_BIT:
	    signal->set_halbit(vp->b);
	    break;
	case HAL_FLOAT:
	    signal->set_halfloat(vp->f);
	    break;
	case HAL_S32:
	    signal->set_hals32(vp->s);
	    break;
	case HAL_U32:
	    signal->set_halu32(vp->u);
	    break;
	}
	if (cgroup->user_flags & GROUP_REPORT_FULL)
	    signal->set_name(sig->name);
	signal->set_type((pb::ValueType)sig->type);
	signal->set_handle(sig->data_ptr);
	break;

    case REPORT_END: // finalize & send
	msg = zmsg_new();
	zmsg_pushstr(msg, cgroup->group->name);
	zframe_t *update_frame = zframe_new(NULL, self->update.ByteSize());
	assert(self->update.SerializeWithCachedSizesToArray(zframe_data(update_frame)));
	zmsg_add(msg, update_frame);
	assert(zmsg_send (&msg, self->z_status) == 0);
	assert(msg == NULL);
	cgroup->user_flags &= ~GROUP_REPORT_FULL;

#if JSON_TIMING
	// timing test:
	try {
	    std::string json = pb2json(self->update);
	    zframe_t *z_jsonframe = zframe_new( json.c_str(), json.size());
	    //assert(zframe_send(&z_jsonframe, self->z_status, 0) == 0);
	    zframe_destroy(&z_jsonframe);
	} catch (std::exception &ex) {
	    rtapi_print_msg(RTAPI_MSG_ERR, "to_ws: pb2json exception: %s\n",
		     ex.what());
	    // std::string text;
	    // if (TextFormat::PrintToString(c, &text))
	    // 	fprintf(stderr, "container: %s\n", text.c_str());
	}
#endif // JSON_TIMING

	self->update.Clear();
	break;
    }
    return 0;
}

static int handle_timer(zloop_t *loop, int timer_id, void *arg)
{
    hal_compiled_group_t *cg = (hal_compiled_group_t *) arg;
    htself_t *self = (htself_t *) cg->user_data;

    if (hal_cgroup_match(cg) ||  (cg->user_flags & GROUP_REPORT_FULL)) {
	hal_cgroup_report(cg, group_report_cb, self,
			  (cg->user_flags & GROUP_REPORT_FULL));
    }
    return 0;
}

// monitor subscribe events:
//
// a new subscriber will cause the next update to be 'full', i.e. with current
// values and including signal names regardless of any change respective to the last scan
//
// this permits a new subscriber to establish the set of signal names immediately as
// well as retrieve all current values without constantly broadcasting all
// signal names
static int handle_subscribe(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *) arg;
    zframe_t *f_subscribe = zframe_recv(poller->socket);
    const char *s = (const char *) zframe_data(f_subscribe);

    if ((s == NULL) || ((*s != '\000') && (*s != '\001'))) {
	// some random message - ignore
	zframe_destroy(&f_subscribe);
	return 0;
    }
    // bool subscribe = (*s == '\001');
    const char *topic = s+1;

    if (s && *s) { // non-zero: subscribe event
	rtapi_print_msg(RTAPI_MSG_DBG, "%s: subscribe event topic='%s'",
			self->cfg->progname, topic);
	if (strlen(topic) == 0) {
	    // this was a subscribe("") - all topics
	    // mark all groups as requiring a full report on next poll
	    for (groupmap_iterator gi = self->groups.begin();
		 gi != self->groups.end(); gi++) {
		hal_compiled_group_t *cg = gi->second;
		// rtapi_print_msg(RTAPI_MSG_DBG,
		// 		"schedule full update for %s (\"\")\n", gi->first.c_str());
		cg->user_flags |= GROUP_REPORT_FULL;
	    }
	} else {
	    // a selective subscribe
	    groupmap_iterator gi = self->groups.find(topic);
	    if (gi != self->groups.end()) {
		hal_compiled_group_t *cg = gi->second;
		// rtapi_print_msg(RTAPI_MSG_DBG,
		// 		"schedule full update for %s (%s)\n",
		//              gi->first.c_str(), topic);
		cg->user_flags |= GROUP_REPORT_FULL;
	    } else {
		// non-existant topic, complain.
		std::string note = "no such topic: " + std::string(topic)
		    + ", valid topics are: ";
		for (groupmap_iterator g = self->groups.begin();
		     g != self->groups.end(); g++) {
		    note += g->first;
		    note += " ";
		}
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: subscribe error: %s\n",
				self->cfg->progname, note.c_str());

		self->update.set_type(pb::MT_STP_NOGROUP);
		self->update.set_note(note);
		zmsg_t *msg = zmsg_new();
		zmsg_pushstr(msg, topic);
		zframe_t *update_frame = zframe_new(NULL, self->update.ByteSize());
		assert(self->update.SerializeWithCachedSizesToArray(zframe_data(update_frame)));
		zmsg_add(msg, update_frame);
		assert(zmsg_send (&msg, self->z_status) == 0);
		assert(msg == NULL);
		self->update.Clear();
	    }
	}
    }
    zframe_destroy(&f_subscribe);
    return 0;
}

static int mainloop( htself_t *self)
{
    int retval;
    sigset_t sigmask;
    size_t msec;

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

    if ((self->signal_fd = signalfd(-1, &sigmask, 0)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signaldfd() failed: %s\n",
			self->cfg->progname,  strerror(errno));
	return -1;
    }

    // zmq_pollitem_t command_poller = { self->z_command, 0, ZMQ_POLLIN };
    zmq_pollitem_t signal_poller =  { 0, self->signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t subscribe_poller =  { self->z_status, 0, ZMQ_POLLIN };

    self->z_loop = zloop_new();
    assert (self->z_loop);

    zloop_set_verbose (self->z_loop, self->cfg->debug);
    //    zloop_poller(self->z_loop, &command_poller, handle_command, self);
    zloop_poller(self->z_loop, &signal_poller, handle_signal, self);
    zloop_poller(self->z_loop, &subscribe_poller, handle_subscribe, self);

    for (groupmap_iterator g = self->groups.begin();
	 g != self->groups.end(); g++) {
	hal_compiled_group_t *cg = g->second;
	cg->user_data = self;
	msec =  hal_cgroup_timer(cg);
	if (msec == 0)
	    msec = self->cfg->default_group_timer;
	zloop_timer(self->z_loop, msec, 0, handle_timer, (void *)cg);
    }

    do {
	retval = zloop_start(self->z_loop);
    } while  (!retval);

    // drop group refcount on exit
    for (groupmap_iterator g = self->groups.begin(); g != self->groups.end(); g++)
	hal_unref_group(g->first.c_str());
    return 0;
}

static int zmq_init(htself_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    self->z_context = zctx_new ();
    assert(self->z_context);


    self->z_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_status);
    zsocket_set_linger (self->z_status, 0);
    zsocket_set_xpub_verbose (self->z_status, 1);
    int rc = zsocket_bind(self->z_status, self->cfg->status);
    assert (rc != 0);

    // self->z_command = zsocket_new (self->z_context, ZMQ_ROUTER);
    // assert(self->z_command);

    // char z_ident[30];
    // snprintf(z_ident, sizeof(z_ident), "%s%d", self->cfg->modname,
    // 	     self->cfg->pid);
    // zsocket_set_identity(self->z_command, z_ident);
    // zsocket_set_linger(self->z_command, 0);
    // rc = zsocket_bind(self->z_command, self->cfg->command);
    // assert (rc != 0);

    return 0;
}

static int sd_init(htself_t *self)
{
    int retval;

    if (!self->cfg->sd_port)
	return 0;  // service discovery disabled

    // start the service announcement responder
    self->sd_publisher = sp_new(self->z_context, self->cfg->sd_port,
				rtapi_instance);

    assert(self->sd_publisher != NULL);
    sp_log(self->sd_publisher, self->cfg->sddebug);
    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_STP, //type
			    STP_VERSION, // version
			    NULL, // ip
			    0, // port
			    self->cfg->status, // uri
			    (int) pb::SA_ZMQ_PROTOBUF, // api
			    "HAL status tracking socket");  // descr
    assert(retval == 0);
    assert(sp_start(self->sd_publisher) == 0);
    return 0;
}

static int group_cb(hal_group_t *g, void *cb_data)
{
    htself_t *self = (htself_t *)cb_data;
    hal_compiled_group_t *cgroup;
    int retval;

    if ((retval = halpr_group_compile(g->name, &cgroup))) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"hal_group_compile(%s) failed: %d\n",
			g->name, retval);
	return -1;
    }
    self->groups[g->name] = cgroup;
    return 0;
}

static int setup_hal(htself_t *self)
{
    if ((self->comp_id = hal_init(self->cfg->modname)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n",
			self->cfg->progname, self->cfg->modname, self->comp_id);
	return -1;
    }
    hal_ready(self->comp_id);

    // rtapi logging now available
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: publishing STP on %s",
		    conf.progname, conf.status);
    int major, minor, patch;
    zmq_version (&major, &minor, &patch);
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: ØMQ version: %d.%d.%d",
		    conf.progname, major, minor, patch);
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: czmq version: %d.%d.%d",
		    conf.progname,
		    CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH);
    major = GOOGLE_PROTOBUF_VERSION / 1000000;
    minor = (GOOGLE_PROTOBUF_VERSION / 1000) % 1000;
    patch = GOOGLE_PROTOBUF_VERSION % 1000;
    rtapi_print_msg(RTAPI_MSG_DBG, "%s: protobuf version: %d.%d.%d",
		    conf.progname, major, minor, patch);
    {   // scoped lock
	int retval __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	// run through all groups, execute a callback for each group found.
	if ((retval = halpr_foreach_group(NULL, group_cb, self)) < 0)
	    return retval;
	rtapi_print_msg(RTAPI_MSG_DBG,"found %d group(s)\n", retval);
    }
    return 0;
}


static int read_config(htconf_t *conf)
{
    const char *s;
    FILE *inifp;

    if (!conf->inifile)
	return 0; // use compiled-in defaults

    if ((inifp = fopen(conf->inifile,"r")) == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: cant open inifile '%s'\n",
		conf->progname, conf->inifile);
	return -1;
    }
    // if ((s = iniFind(inifp, "COMMAND", conf->section)))
    // 	conf->command = strdup(s);
    if ((s = iniFind(inifp, "STATUS", conf->section)))
	conf->status = strdup(s);
    iniFindInt(inifp, "DEBUG", conf->section, &conf->debug);
    iniFindInt(inifp, "SDDEBUG", conf->section, &conf->sddebug);
    iniFindInt(inifp, "SDPORT", conf->section, &conf->sd_port);
    iniFindInt(inifp, "GROUPTIMER", conf->section, &conf->default_group_timer);
    fclose(inifp);
    return 0;
}

static void usage(void) {
    printf("Usage:  haltalker [options]\n");
    printf("This is a userspace HAL program, typically loaded "
	   "using the halcmd \"loadusr\" command:\n"
	   "    loadusr haltalker [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment"
	   " variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'VFS11')\n"
	   "-m or --rtapi-msg-level <level>\n"
	   "    set the RTAPI message level.\n"
	   "-t or --timer <msec>\n"
	   "    set the default group scan timer (100mS).\n"
	   "-d or --debug\n"
	   "    Turn on event debugging messages.\n");
}

int main (int argc, char *argv[])
{
    int opt;

    conf.progname = argv[0];
    conf.inifile = getenv("INI_FILE_NAME");
    while ((opt = getopt_long(argc, argv, option_string,
			      long_options, NULL)) != -1) {
	switch(opt) {
	case 'd':
	    conf.debug = 1;
	    break;
	case 'D':
	    conf.sddebug = 1;
	    break;
	case 'S':
	    conf.section = optarg;
	    break;
	case 'I':
	    conf.inifile = optarg;
	    break;
	case 't':
	    conf.default_group_timer = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    conf.pid = getpid();
    conf.sd_port = SERVICE_DISCOVERY_PORT;

    to_syslog("haltalker> ", &stdout); // redirect stdout to syslog
    to_syslog("haltalker>> ", &stderr);  // redirect stderr to syslog

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;
    self.serial = 0;

    if (!(setup_hal(&self) ||
	  zmq_init(&self) ||
	  sd_init(&self))) {
	mainloop(&self);
    }

    // stop  the service announcement responder
    sp_destroy(&self.sd_publisher);

    if (self.comp_id)
	hal_exit(self.comp_id);

    // shutdown zmq context
    zctx_destroy(&self.z_context);

    exit(0);
}
