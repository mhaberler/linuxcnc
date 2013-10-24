// serve hal groups

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signalfd.h>
#include <sys/un.h>
#include <sys/timerfd.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <alloca.h>
#include <limits.h>		/* for CHAR_BIT */
#include <signal.h>

#include <string>
#include <map>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_group.h>
#include <inifile.h>
#include <czmq.h>

#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/message.pb.h>
using namespace google::protobuf;

typedef enum {
    GROUP_REPORT_FULL = 1,
} group_flags_t;

typedef  std::map<const char*, hal_compiled_group_t *> groupmap_t;
typedef groupmap_t::iterator groupmap_iterator;

static const char *option_string = "m:hI:S:d";
static struct option long_options[] = {
    {"msglevel", required_argument, 0, 'm'},
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {0,0,0,0}
};

typedef struct htconf {
    const char *progname;
    const char *inifile;
    const char *section;
    const char *modname;
    const char *status;
    const char *command;
    int msglevel;
    int debug;
    int pid;
} htconf_t;

htconf_t conf = {
    "",
    NULL,
    "HALTALKER",
    "haltalker",
    "tcp://127.0.0.1:6650",
    "tcp://127.0.0.1:6651",
    -1,
    0,
    0,
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
} htself_t;

static int handle_command(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    return 0;
}

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
    return -1;
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
	self->update.Clear();
	// this enables a new subscriber to easily detect she's receiving
	// a full status snapshot, not just a change tracking update
	if (cgroup->user_flags & GROUP_REPORT_FULL)
	    self->update.set_type(pb::MT_HALUPDATE_FULL);
	else
	    self->update.set_type(pb::MT_HALUPDATE);
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
	break;
    }
    return 0;
}

static int handle_timer(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
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
    unsigned char *s = zframe_data(f_subscribe);

    if (s && *s) { // non-zero: subscribe event
	rtapi_print_msg(RTAPI_MSG_DBG,
			"%s: subscribe event '%s'\n",
			self->cfg->progname, s+1);
	// mark all groups as requiring a full report on next poll
	for (groupmap_iterator g = self->groups.begin();
	     g != self->groups.end(); g++) {
	    hal_compiled_group_t *cg = g->second;
	    cg->user_flags |= GROUP_REPORT_FULL;
	}
    }
    zframe_destroy(&f_subscribe);
    return 0;
}

static int mainloop( htself_t *self)
{
    int retval;
    sigset_t mask;
    int msec;

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
	perror("sigprocmask");
    self->signal_fd = signalfd(-1, &mask, 0);
    if (self->signal_fd == -1) {
	perror("signalfd");
	return -1;
    }
    zmq_pollitem_t command_poller = { self->z_command, 0, ZMQ_POLLIN };
    zmq_pollitem_t signal_poller =  { 0, self->signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t subscribe_poller =  { self->z_status, 0, ZMQ_POLLIN };

    self->z_loop = zloop_new();
    assert (self->z_loop);
    zloop_set_verbose (self->z_loop, self->cfg->debug);
    zloop_poller(self->z_loop, &command_poller, handle_command, self);
    zloop_poller(self->z_loop, &signal_poller, handle_signal, self);
    zloop_poller(self->z_loop, &subscribe_poller, handle_subscribe, self);

    for (groupmap_iterator g = self->groups.begin();
	 g != self->groups.end(); g++) {
	hal_compiled_group_t *cg = g->second;
	cg->user_data = self;
	msec =  hal_cgroup_timer(cg);
	if (msec > 0) {
	    zloop_timer(self->z_loop, msec, 0, handle_timer, cg);
	}
    }

    // handle signals && profiling properly
    do {
	retval = zloop_start(self->z_loop);
    } while  (!(retval || zctx_interrupted));

    // drop group refcount on exit
    for (groupmap_iterator g = self->groups.begin(); g != self->groups.end(); g++)
	hal_unref_group(g->first);
    return 0;
}

static int zmq_init(htself_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // fprintf(stderr, "czmq version: %d.%d.%d\n",
    // 	    CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH);

    self->z_context = zctx_new ();
    self->z_status = zsocket_new (self->z_context, ZMQ_XPUB);
    zsocket_set_linger (self->z_status, 0);
    zsocket_set_xpub_verbose (self->z_status, 1);
    int rc = zsocket_bind(self->z_status, self->cfg->status);
    assert (rc != 0);

    self->z_command = zsocket_new (self->z_context, ZMQ_ROUTER);

    char z_ident[30];
    snprintf(z_ident, sizeof(z_ident), "%s%d", self->cfg->modname,
	     self->cfg->pid);
    zsocket_set_identity(self->z_command, z_ident);
    zsocket_set_linger(self->z_command, 0);
    rc = zsocket_bind(self->z_command, self->cfg->command);
    assert (rc != 0);

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

    if (!conf->inifile) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: need an inifile - either --ini <inifile>"
			" or env INI_FILE_NAME=<inifile>\n",
			conf->progname);
	return -1;
    }
    if ((inifp = fopen(conf->inifile,"r")) == NULL) {
	fprintf(stderr, "%s: cant open inifile '%s'\n",
		conf->progname, conf->inifile);
	return -1;
    }
    if ((s = iniFind(inifp, "COMMAND", conf->section)))
	conf->command = strdup(s);
    if ((s = iniFind(inifp, "STATUS", conf->section)))
	conf->status = strdup(s);
    iniFindInt(inifp, "MSGLEVEL", conf->section, &conf->msglevel);
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
	   "-d or --debug\n"
	   "    Turn on event debugging messages.\n");
}

int main (int argc, char *argv[ ])
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
	case 'S':
	    conf.section = optarg;
	    break;
	case 'I':
	    conf.inifile = optarg;
	    break;
	case 'm':
	    conf.msglevel = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    conf.pid = getpid();
    if (conf.msglevel > -1)
	rtapi_set_msg_level(conf.msglevel);

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;
    self.serial = 0;

    if (!(setup_hal(&self) ||
	  zmq_init(&self)))
	mainloop(&self);

    if (self.comp_id)
	    hal_exit(self.comp_id);
    exit(0);
}
