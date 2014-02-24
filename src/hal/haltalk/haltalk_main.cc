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

// haltalk:
//   1. reports the status of HAL signals aggregated into
//      HAL groups via the Status Tracking protocol.
//
//   2. implements the HALRcomp protocol for remote HAL components.

#include "haltalk.hh"


static const char *option_string = "hI:S:dt:Du:r:T:c:";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {"sddebug", required_argument, 0, 'D'},
    {"gtimer", required_argument, 0, 't'},
    {"ctimer", required_argument, 0, 'T'},
    {"stpuri", required_argument, 0, 'u'},
    {"rcompuri", required_argument, 0, 'r'},
    {"cmduri", required_argument, 0, 'c'},
    {0,0,0,0}
};

// configuration defaults
static htconf_t conf = {
    "",
    NULL,
    "HALTALK",
    "haltalk",
    "tcp://127.0.0.1:*", // localhost, use ephemeral port
    "tcp://127.0.0.1:*",
    "tcp://127.0.0.1:*",
    0,
    0,
    100,
    100,
    0
};


static int
handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *)arg;
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(self->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }
    switch (fdsi.ssi_signo) {
    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signal %d - '%s' received\n",
			self->cfg->progname, fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    }
    self->interrupted = true;
    return -1; // exit reactor with -1
}

static int
mainloop( htself_t *self)
{
    int retval;

    zmq_pollitem_t signal_poller = { 0, self->signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t group_poller =  { self->z_group_status, 0, ZMQ_POLLIN };
    zmq_pollitem_t rcomp_poller =  { self->z_rcomp_status, 0, ZMQ_POLLIN };
    zmq_pollitem_t cmd_poller   =  { self->z_command,      0, ZMQ_POLLIN };

    self->z_loop = zloop_new();
    assert (self->z_loop);

    zloop_set_verbose (self->z_loop, self->cfg->debug);

    zloop_poller(self->z_loop, &signal_poller, handle_signal, self);
    zloop_poller(self->z_loop, &group_poller,  handle_group_input, self);
    zloop_poller(self->z_loop, &rcomp_poller,  handle_rcomp_input, self);
    zloop_poller(self->z_loop, &cmd_poller,    handle_command_input, self);

    do {
	retval = zloop_start(self->z_loop);
    } while  (!(retval || self->interrupted));

    rtapi_print_msg(RTAPI_MSG_INFO,
		    "%s: exiting mainloop (%s)\n",
		    self->cfg->progname,
		    self->interrupted ? "interrupted": "reactor exited");

    return 0;
}

static int
zmq_init(htself_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    sigset_t sigmask;
    int retval;

    sigemptyset(&sigmask);

    // block all signal delivery through default signal handlers
    // since we're using signalfd()
    sigfillset(&sigmask);
    retval = sigprocmask(SIG_SETMASK, &sigmask, NULL);
    assert(retval == 0);

    // explicitly enable signals we want delivered via signalfd()
    retval = sigemptyset(&sigmask); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGINT);  assert(retval == 0);
    retval = sigaddset(&sigmask, SIGQUIT); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGTERM); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGSEGV); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGFPE);  assert(retval == 0);

    if ((self->signal_fd = signalfd(-1, &sigmask, 0)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signaldfd() failed: %s\n",
			self->cfg->progname,  strerror(errno));
	return -1;
    }

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    // must happen before zctx_new()
    zsys_handler_set(NULL);

    self->z_context = zctx_new ();
    assert(self->z_context);

    self->z_group_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_group_status);
    zsocket_set_linger (self->z_group_status, 0);
    zsocket_set_xpub_verbose (self->z_group_status, 1);
    int rc = zsocket_bind(self->z_group_status, self->cfg->group_status);
    assert (rc != 0);
    self->z_group_status_dsn = zsocket_last_endpoint (self->z_group_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking STP on '%s'",
		    conf.progname, self->z_group_status_dsn);

    self->z_rcomp_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_rcomp_status);
    zsocket_set_linger (self->z_rcomp_status, 0);
    zsocket_set_xpub_verbose (self->z_rcomp_status, 1);
    rc = zsocket_bind(self->z_rcomp_status, self->cfg->rcomp_status);
    assert (rc != 0);
    self->z_rcomp_status_dsn = zsocket_last_endpoint (self->z_rcomp_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALRcomp on '%s'",
		    conf.progname, self->z_rcomp_status_dsn);


    self->z_command = zsocket_new (self->z_context, ZMQ_ROUTER);
    assert(self->z_command);
    zsocket_set_linger (self->z_command, 0);
    zsocket_set_identity (self->z_command, self->cfg->modname);

    rc = zsocket_bind(self->z_command, self->cfg->command);
    assert (rc != 0);
    self->z_command_dsn = zsocket_last_endpoint (self->z_command);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALComannd on '%s'",
		    conf.progname, self->z_command_dsn);

    usleep(200 *1000); // avoid slow joiner syndrome
    return 0;
}

static int
hal_setup(htself_t *self)
{
    int retval;

    if ((self->comp_id = hal_init(self->cfg->modname)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n",
			self->cfg->progname, self->cfg->modname, self->comp_id);
	return self->comp_id;
    }
    hal_ready(self->comp_id);

    int major, minor, patch;
    zmq_version (&major, &minor, &patch);

    char buf[40];
    uuid_unparse(self->instance_uuid, buf);

    rtapi_print_msg(RTAPI_MSG_DBG,
		    "%s: startup ØMQ=%d.%d.%d czmq=%d.%d.%d protobuf=%d.%d.%d uuid=%s\n",
		    self->cfg->progname, major, minor, patch,
		    CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH,
		    GOOGLE_PROTOBUF_VERSION / 1000000,
		    (GOOGLE_PROTOBUF_VERSION / 1000) % 1000,
		    GOOGLE_PROTOBUF_VERSION % 1000,
		    buf);

    retval = scan_groups(self);
    if (retval < 0) return retval;
    retval = scan_comps(self);
    if (retval < 0) return retval;
    return 0;
}

static int
hal_cleanup(htself_t *self)
{
    int retval;
    retval = release_comps(self);
    retval = release_groups(self);

    if (self->comp_id)
	hal_exit(self->comp_id);
    return retval;
}

static int
read_config(htconf_t *conf)
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
    if ((s = iniFind(inifp, "GROUP_STATUS_URI", conf->section)))
	conf->group_status = strdup(s);
    if ((s = iniFind(inifp, "RCOMP_STATUS_URI", conf->section)))
	conf->rcomp_status = strdup(s);
    if ((s = iniFind(inifp, "COMMAND_URI", conf->section)))
	conf->command = strdup(s);

    iniFindInt(inifp, "GROUPTIMER", conf->section, &conf->default_group_timer);
    iniFindInt(inifp, "RCOMPTIMER", conf->section, &conf->default_rcomp_timer);
    iniFindInt(inifp, "DEBUG", conf->section, &conf->debug);
    iniFindInt(inifp, "SDDEBUG", conf->section, &conf->sddebug);
    iniFindInt(inifp, "SDPORT", conf->section, &conf->sd_port);
    fclose(inifp);
    return 0;
}

static void
usage(void)
{
    printf("Usage:  haltalk [options]\n");
    printf("This is a userspace HAL program, typically loaded "
	   "using the halcmd \"loadusr\" command:\n"
	   "    loadusr haltalk [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment"
	   " variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'HALTALK')\n"
	   "-u or --uri <uri>\n"
	   "    zeroMQ URI for status reporting socket\n"
	   "-m or --rtapi-msg-level <level>\n"
	   "    set the RTAPI message level.\n"
	   "-t or --timer <msec>\n"
	   "    set the default group scan timer (100mS).\n"
	   "-d or --debug\n"
	   "    Turn on event debugging messages.\n");
}

int main (int argc, char *argv[])
{
    int opt, retval;

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
	case 'u':
	    conf.group_status = optarg;
	    break;
	case 'r':
	    conf.rcomp_status = optarg;
	    break;
	case 'c':
	    conf.command = optarg;
	    break;
	case 't':
	    conf.default_group_timer = atoi(optarg);
	    break;
	case 'T':
	    conf.default_rcomp_timer = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    conf.sd_port = SERVICE_DISCOVERY_PORT;

    // to_syslog("haltalk> ", &stdout); // redirect stdout to syslog
    // to_syslog("haltalk>> ", &stderr);  // redirect stderr to syslog

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;
    self.pid = getpid();
    uuid_generate_time(self.instance_uuid);

    retval = hal_setup(&self);
    if (retval) exit(retval);

    retval = zmq_init(&self);
    if (retval) exit(retval);

    retval = service_discovery_start(&self);
    if (retval) exit(retval);

    mainloop(&self);

    service_discovery_stop(&self);

    // shutdown zmq context
    zctx_destroy(&self.z_context);

    hal_cleanup(&self);

    exit(0);
}
