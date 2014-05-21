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
//
//   3. Implements remote remote set/get operations for pins and signals.
//
//   4. Reports a HAL instance through the DESCRIBE operation.
//
//   5. Announce services via zeroconf.
//
//   6. [notyet] optional may bridge to a remote HAL instance through a remote component.

#include "haltalk.hh"
#include <setup_signals.h>
#include <select_interface.h>


static const char *option_string = "hI:S:dt:u:r:T:c:pb:C:U:i:N:R:";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"paranoid", no_argument, 0, 'p'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {"gtimer", required_argument, 0, 't'},
    {"ctimer", required_argument, 0, 'T'},
    {"stpuri", required_argument, 0, 'u'},
    {"rcompuri", required_argument, 0, 'r'},
    {"cmduri", required_argument, 0, 'c'},
    {"bridge", required_argument, 0, 'b'},
    {"bridgecmduri", required_argument, 0, 'C'},
    {"bridgeupdateuri", required_argument, 0, 'U'},
    {"bridgeinstance", required_argument, 0, 'i'},
    {"interfaces", required_argument, 0, 'N'},
    {"svcuuid", required_argument, 0, 'R'},
    {0,0,0,0}
};

// configuration defaults
static htconf_t conf = {
    "",
    NULL,
    "HALTALK",
    "haltalk",
    "localhost",
    "127.0.0.1",
    "tcp://%s:*", // as per preferred interface, use ephemeral port
    "tcp://%s:*",
    "tcp://%s:*",
    NULL,
    NULL,
    NULL,
    NULL,
    -1,
    0,
    0,
    100,
    100,
    0,
    NULL,
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
			self->cfg->progname,
			fdsi.ssi_signo,
			strsignal(fdsi.ssi_signo));
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


    zloop_set_verbose (self->z_loop, self->cfg->debug > 1);

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

static void sigaction_handler(int sig, siginfo_t *si, void *uctx)
{
    syslog_async(LOG_ERR,"signal %d - '%s' received, dumping core (current dir=%s)",
		    sig, strsignal(sig), get_current_dir_name());
    closelog_async(); // let syslog_async drain
    sleep(1);
    signal(SIGABRT, SIG_DFL);
    abort();
    // not reached
}

static int
zmq_init(htself_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    self->signal_fd = setup_signals(sigaction_handler);
    assert(self->signal_fd > -1);

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    // must happen before zctx_new()
    zsys_handler_set(NULL);

    self->z_context = zctx_new ();
    assert(self->z_context);

    self->z_loop = zloop_new();
    assert (self->z_loop);

    self->z_group_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_group_status);
    zsocket_set_linger (self->z_group_status, 0);
    zsocket_set_xpub_verbose (self->z_group_status, 1);
    self->z_group_port = zsocket_bind(self->z_group_status, self->cfg->group_status);
    assert (self->z_group_port != 0);
    self->z_group_status_dsn = zsocket_last_endpoint (self->z_group_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALGroup on '%s'",
		    conf.progname, self->z_group_status_dsn);

    self->z_rcomp_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_rcomp_status);
    zsocket_set_linger (self->z_rcomp_status, 0);
    zsocket_set_xpub_verbose (self->z_rcomp_status, 1);
    self->z_rcomp_port = zsocket_bind(self->z_rcomp_status, self->cfg->rcomp_status);
    assert (self->z_rcomp_port != 0);
    self->z_rcomp_status_dsn = zsocket_last_endpoint (self->z_rcomp_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALRcomp on '%s'",
		    conf.progname, self->z_rcomp_status_dsn);


    self->z_command = zsocket_new (self->z_context, ZMQ_ROUTER);
    assert(self->z_command);
    zsocket_set_linger (self->z_command, 0);
    zsocket_set_identity (self->z_command, self->cfg->modname);

    self->z_command_port = zsocket_bind(self->z_command, self->cfg->command);
    assert (self->z_command_port != 0);
    self->z_command_dsn = zsocket_last_endpoint (self->z_command);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALComannd on '%s'",
		    conf.progname, self->z_command_dsn);

    // register Avahi poll adapter
    if (!(self->av_loop = avahi_czmq_poll_new(self->z_loop))) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: zeroconf: Failed to create avahi event loop object.",
			conf.progname);
	return -1;
    }

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
    uuid_unparse(self->process_uuid, buf);

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
    uuid_t uutmp;

    if (!conf->inifile) {
	// if no ini, must have a service UUID as arg or in environment
	if (conf->service_uuid == NULL) {
		rtapi_print_msg(RTAPI_MSG_ERR, "%s: no service UUID given (-R <uuid> or env MKUUID)\n",
				conf->progname);
		return -1;
	}
	if (uuid_parse(conf->service_uuid, uutmp)) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: service UUID: syntax error: '%s'",
			    conf->progname,conf->service_uuid);
	    return -1;
	}
	return 0; // use compiled-in defaults
    }

    if ((inifp = fopen(conf->inifile,"r")) == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: cant open inifile '%s'\n",
			conf->progname, conf->inifile);
	return -1;
    }

    // insist on service UUID
    if (conf->service_uuid == NULL) {
	if ((s = iniFind(inifp, "MKUUID", "GLOBAL"))) {
	    conf->service_uuid = strdup(s);
	}
    }
    if (conf->service_uuid == NULL)
	conf->service_uuid = getenv("MKUUID");

    if (conf->service_uuid == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: no service UUID on command line, environment "
			"or inifile (-R <uuid> or env MKUUID or [GLOBAL]MKUUID=)\n",
			conf->progname);
	return -1;
    }

    // validate uuid
    if (uuid_parse(conf->service_uuid, uutmp)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: service UUID syntax error: '%s'",
			conf->progname, conf->service_uuid);
	return -1;
    }

    if ((s = iniFind(inifp, "INTERFACES","GLOBAL"))) {

	char ifname[100], ip[100];

	// pick a preferred interface
	if (parse_interface_prefs(s,  ifname, ip, &conf->ifIndex) == 0) {
	    conf->interface = strdup(ifname);
	    conf->ipaddr = strdup(ip);
	    rtapi_print_msg(RTAPI_MSG_DBG, "%s %s: using preferred interface %s/%s\n",
			    conf->progname, conf->inifile,
			    conf->interface, conf->ipaddr);
	} else {
	    rtapi_print_msg(RTAPI_MSG_ERR, "%s %s: INTERFACES='%s'"
			    " - cant determine preferred interface, using %s/%s\n",
			    conf->progname, conf->inifile, s,
			    conf->interface, conf->ipaddr);
	}
    }

    // finalize the URI's
    char uri[100];
    snprintf(uri, sizeof(uri), conf->group_status, conf->ipaddr);
    conf->group_status = strdup(uri);

    snprintf(uri, sizeof(uri), conf->rcomp_status, conf->ipaddr);
    conf->rcomp_status = strdup(uri);

    snprintf(uri, sizeof(uri), conf->command, conf->ipaddr);
    conf->command = strdup(uri);

    // bridge: TBD

    if ((s = iniFind(inifp, "GROUP_STATUS_URI", conf->section)))
	conf->group_status = strdup(s);
    if ((s = iniFind(inifp, "RCOMP_STATUS_URI", conf->section)))
	conf->rcomp_status = strdup(s);
    if ((s = iniFind(inifp, "COMMAND_URI", conf->section)))
	conf->command = strdup(s);

    if ((s = iniFind(inifp, "BRIDGE_COMP", conf->section)))
	conf->bridgecomp = strdup(s);
    if ((s = iniFind(inifp, "BRIDGE_COMMAND_URI", conf->section)))
	conf->bridgecomp_cmduri = strdup(s);
    if ((s = iniFind(inifp, "BRIDGE_STATUS_URI", conf->section)))
	conf->bridgecomp_updateuri = strdup(s);
    iniFindInt(inifp, "BRIDGE_TARGET_INSTANCE", conf->section, &conf->bridge_target_instance);

    iniFindInt(inifp, "GROUPTIMER", conf->section, &conf->default_group_timer);
    iniFindInt(inifp, "RCOMPTIMER", conf->section, &conf->default_rcomp_timer);
    iniFindInt(inifp, "DEBUG", conf->section, &conf->debug);
    iniFindInt(inifp, "PARANOID", conf->section, &conf->paranoid);
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
	   "-p or --paranoid <msec>\n"
	   "    turn on extensive runtime checks (may be costly).\n"
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
	    conf.debug++;
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
	case 'p':
	    conf.paranoid = 1;
	    break;
	case 'b':
	    conf.bridgecomp = optarg;
	    break;
	case 'C':
	    conf.bridgecomp_cmduri = optarg;
	    break;
	case 'i':
	    conf.bridge_target_instance = atoi(optarg);
	    break;
	case 'U':
	    conf.bridgecomp_updateuri = optarg;
	    break;
	case 'N':
	    conf.interfaces = optarg;
	    break;
	case 'R':
	    conf.service_uuid = optarg;
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;
    self.pid = getpid();
    uuid_generate_time(self.process_uuid);

    retval = hal_setup(&self);
    if (retval) exit(retval);

    retval = zmq_init(&self);
    if (retval) exit(retval);

    retval = bridge_init(&self);
    if (retval) exit(retval);

    retval = ht_zeroconf_announce(&self);
    if (retval) exit(retval);

    mainloop(&self);

    ht_zeroconf_withdraw(&self);
    // probably should run zloop here until deregister complete

    // shutdown zmq context
    zctx_destroy(&self.z_context);

    hal_cleanup(&self);

    exit(0);
}
