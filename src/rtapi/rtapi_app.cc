/* Copyright (C) 2006-2008 Jeff Epler <jepler@unpythonic.net>
 * Copyright (C) 2012-2014 Michael Haberler <license@mah.priv.at>
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

#include "config.h"

#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <string>
#include <map>
#include <algorithm>
#include <sys/resource.h>
#include <linux/capability.h>
#include <sys/io.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <malloc.h>
#include <assert.h>
#include <syslog_async.h>
#include <limits.h>
#include <sys/prctl.h>

#include <czmq.h>
#include <google/protobuf/text_format.h>
#include <protobuf/generated/message.pb.h>
using namespace google::protobuf;
typedef ::google::protobuf::RepeatedPtrField< ::std::string> pbstringarray_t;

#include <discovery.h>  // for UDP service discovery
#include<arpa/inet.h>
#include<sys/socket.h>

#include "rtapi.h"
#include "rtapi_global.h"
#include "rtapi_compat.h"
#include "hal.h"
#include "hal/hal_priv.h"
#include "rtapi/shmdrv/shmdrv.h"

using namespace std;

/* Pre-allocation size. Must be enough for the whole application life to avoid
 * pagefaults by new memory requested from the system. */
#define PRE_ALLOC_SIZE		(30 * 1024 * 1024)

template<class T> T DLSYM(void *handle, const string &name) {
    return (T)(dlsym(handle, name.c_str()));
}

template<class T> T DLSYM(void *handle, const char *name) {
    return (T)(dlsym(handle, name));
}

static std::map<string, void*> modules;
static struct rusage rusage;
static unsigned long minflt, majflt;
static int instance_id;
flavor_ptr flavor;
static int use_drivers = 0;
static int foreground;
static int debug;
static int signal_fd;
static bool interrupted;
int shmdrv_loaded;
long page_size;
static const char *progname;
//static const char *z_uri = "tcp://127.0.0.1:*";
static const char *z_uri = "tcp://127.0.0.1:10043";
static int z_port;
static pb::Container command, reply;
static int sd_port = SERVICE_DISCOVERY_PORT;
static int register_service_discovery(int sd_port);

// the following two variables, despite extern, are in fact private to rtapi_app
// in the sense that they are not visible in the RT space (the namespace 
// of dlopen'd modules); these are supposed to be 'ships in the night'
// relative to any symbols exported by rtapi_app.
//
// global_data is set in attach_global_segment() which was already 
// created by rtapi_msgd
// rtapi_switch is set once rtapi.so has been loaded by calling the 
// rtapi_get_handle() method in rtapi.so.
// Once set, rtapi methods in rtapi.so can be called normally through
// the rtapi_switch redirection (see rtapi.h).

// NB: do _not_ call any rtapi_* methods before these variables are set
// except for rtapi_msg* and friends (those do not go through the rtapi_switch).
rtapi_switch_t *rtapi_switch;
global_data_t *global_data; 

static int init_actions(int instance);
static void exit_actions(int instance);
static int harden_rt(void);
static void rtapi_app_msg_handler(msg_level_t level, const char *fmt, va_list ap);
static void stderr_rtapi_msg_handler(msg_level_t level, const char *fmt, va_list ap);

// raise/drop privilege support
void save_uid(void);
void do_setuid (void);
void undo_setuid (void);

static int do_one_item(char item_type_char, const string &param_name,
		       const string &param_value, void *vitem, int idx=0)
{
    char *endp;
    switch(item_type_char) {
    case 'l': {
	long *litem = *(long**) vitem;
	litem[idx] = strtol(param_value.c_str(), &endp, 0);
	if(*endp) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "`%s' invalid for parameter `%s'",
			    param_value.c_str(), param_name.c_str());
	    return -1;
	}
	return 0;
    }
    case 'i': {
	int *iitem = *(int**) vitem;
	iitem[idx] = strtol(param_value.c_str(), &endp, 0);
	if(*endp) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "`%s' invalid for parameter `%s'",
			    param_value.c_str(), param_name.c_str());
	    return -1;
	}
	return 0;
    }
    case 's': {
	char **sitem = *(char***) vitem;
	sitem[idx] = strdup(param_value.c_str());
	return 0;
    }
    default:
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: Invalid type character `%c'\n",
			param_name.c_str(), item_type_char);
	return -1;
    }
    return 0;
}

void remove_quotes(string &s)
{
    s.erase(remove_copy(s.begin(), s.end(), s.begin(), '"'), s.end());
}

static int do_comp_args(void *module, pbstringarray_t args)
{
    for(int i = 0; i < args.size(); i++) {
        string s(args.Get(i));
	remove_quotes(s);
        size_t idx = s.find('=');
        if(idx == string::npos) {
            rtapi_print_msg(RTAPI_MSG_ERR, "Invalid parameter `%s'\n",
			    s.c_str());
            return -1;
        }
        string param_name(s, 0, idx);
        string param_value(s, idx+1);
        void *item=DLSYM<void*>(module, "rtapi_info_address_" + param_name);
        if(!item) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "Unknown parameter `%s'\n", s.c_str());
            return -1;
        }
        char **item_type=DLSYM<char**>(module, "rtapi_info_type_" + param_name);
        if(!item_type || !*item_type) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "Unknown parameter `%s' (type information missing)\n",
			    s.c_str());
            return -1;
        }
        string item_type_string = *item_type;

        if(item_type_string.size() > 1) {
            int a, b;
            char item_type_char;
            int r = sscanf(item_type_string.c_str(), "%d-%d%c",
			   &a, &b, &item_type_char);
            if(r != 3) {
                rtapi_print_msg(RTAPI_MSG_ERR,
				"Unknown parameter `%s'"
				" (corrupt array type information): %s\n",
				s.c_str(), item_type_string.c_str());
                return -1;
            }
            size_t idx = 0;
            int i = 0;
            while(idx != string::npos) {
                if(i == b) {
                    rtapi_print_msg(RTAPI_MSG_ERR,
				    "%s: can only take %d arguments\n",
				    s.c_str(), b);
                    return -1;
                }
                size_t idx1 = param_value.find(",", idx);
                string substr(param_value, idx, idx1 - idx);
                int result = do_one_item(item_type_char, s, substr, item, i);
                if(result != 0) return result;
                i++;
                idx = idx1 == string::npos ? idx1 : idx1 + 1;
            }
        } else {
            char item_type_char = item_type_string[0];
            int result = do_one_item(item_type_char, s, param_value, item);
            if(result != 0) return result;
        }
    }
    return 0;
}

static int do_load_cmd(int instance, string name, pbstringarray_t args)
{
    void *w = modules[name];
    char module_name[PATH_MAX];
    void *module;

    if(w == NULL) {
	strncpy(module_name, (name + flavor->mod_ext).c_str(),
		PATH_MAX);

        module = modules[name] = dlopen(module_name, RTLD_GLOBAL |RTLD_NOW);
        if(!module) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: dlopen: %s\n", 
			    name.c_str(), dlerror());
            return -1;
        }
	// retrieve the address of rtapi_switch_struct
	// so rtapi functions can be called and members
	// access
	if (rtapi_switch == NULL) {

	    rtapi_get_handle_t rtapi_get_handle;
    	    dlerror();
	    rtapi_get_handle = (rtapi_get_handle_t) dlsym(module,
							  "rtapi_get_handle");
	    if (rtapi_get_handle != NULL) {
		rtapi_switch = rtapi_get_handle();
		assert(rtapi_switch != NULL);
	    }
	}
	/// XXX handle arguments
        int (*start)(void) = DLSYM<int(*)(void)>(module, "rtapi_app_main");
        if(!start) {
            rtapi_print_msg(RTAPI_MSG_ERR, "%s: dlsym: %s\n",
			    name.c_str(), dlerror());
            return -1;
        }
        int result;

        result = do_comp_args(module, args);
        if(result < 0) { dlclose(module); return -1; }

        if ((result=start()) < 0) {
            rtapi_print_msg(RTAPI_MSG_ERR, "rtapi_app_main(%s): %d %s\n", 
			    name.c_str(), result, strerror(-result));
	    modules.erase(modules.find(name));
	    return result;
        }
	rtapi_print_msg(RTAPI_MSG_DBG, "%s: loaded from %s\n",
			name.c_str(), module_name);
	return 0;
    }
    rtapi_print_msg(RTAPI_MSG_ERR, "%s: already loaded\n", name.c_str());
    return -1;
}

static int do_unload_cmd(int instance, string name)
{
    void *w = modules[name];
    if(w == NULL) {
        rtapi_print_msg(RTAPI_MSG_ERR, "unload: '%s' not loaded\n", 
			name.c_str());
	return -1;
    } else {
        int (*stop)(void) = DLSYM<int(*)(void)>(w, "rtapi_app_exit");
	if(stop) stop();
	modules.erase(modules.find(name));
        dlclose(w);
	rtapi_print_msg(RTAPI_MSG_DBG, " '%s' unloaded\n", 
			name.c_str());
    }
    return 0;
}

// shut down the stack in proper order
static void exit_actions(int instance)
{
    do_unload_cmd(instance, "hal_lib");
    do_unload_cmd(instance, "rtapi");
}

static int init_actions(int instance)
{
    int retval;

    retval =  do_load_cmd(instance, "rtapi", pbstringarray_t());
    if (retval)
	return retval;
    return do_load_cmd(instance, "hal_lib", pbstringarray_t());
}


static int attach_global_segment()
{
    int retval = 0;
    int globalkey = OS_KEY(GLOBAL_KEY, instance_id);
    int size = 0;
    int tries = 10; // 5 sec deadline for msgd/globaldata to come up

    shm_common_init();
    do {
	retval = shm_common_new(globalkey, &size,
				instance_id, (void **) &global_data, 0);
	if (retval < 0) {
	    tries--;
	    if (tries == 0) {
		syslog_async(LOG_ERR,
		       "rt:%d ERROR: cannot attach global segment key=0x%x %s\n",
		       instance_id, globalkey, strerror(-retval));
		return retval;
	    }
	    struct timespec ts = {0, 500 * 1000 * 1000}; //ms
	    nanosleep(&ts, NULL);
	}
    } while (retval < 0);

    if (size != sizeof(global_data_t)) {
	syslog_async(LOG_ERR,
	       "rt:%d global segment size mismatch: expect %zu got %d\n", 
	       instance_id, sizeof(global_data_t), size);
	return -EINVAL;
    }

    tries = 10;
    while  (global_data->magic !=  GLOBAL_READY) {
	tries--;
	if (tries == 0) {
	    syslog_async(LOG_ERR,
		   "rt:%d ERROR: global segment magic not changing to ready: magic=0x%x\n",
		   instance_id, global_data->magic);
	    return -EINVAL;
	}
	struct timespec ts = {0, 500 * 1000 * 1000}; //ms
	nanosleep(&ts, NULL);
    }
    return retval;
}

// handle commands from zmq socket
static int rtapi_request(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    zmsg_t *r = zmsg_recv(poller->socket);
    char *origin = zmsg_popstr (r);
    zframe_t *request_frame  = zmsg_pop (r);
    static bool force_exit = false;

    pb::Container pbreq, pbreply;

    if (!pbreq.ParseFromArray(zframe_data(request_frame),
			      zframe_size(request_frame))) {
	rtapi_print_msg(RTAPI_MSG_ERR, "cant decode request from %s (size %d)",
			origin ? origin : "NULL",
			zframe_size(request_frame));
	zmsg_destroy(&r);
	return 0;
    }
    if (debug) {
	string buffer;
	if (TextFormat::PrintToString(pbreq, &buffer)) {
	    fprintf(stderr, "request: %s\n",buffer.c_str());
	}
    }

    pbreply.set_type(pb::MT_RTAPI_APP_REPLY);

    switch (pbreq.type()) {
    case pb::MT_RTAPI_APP_PING:
	char buffer[100];
	snprintf(buffer, sizeof(buffer),
		 "pid=%d flavor=%s gcc=%s git=%s",
		 getpid(),flavor->name,  __VERSION__, GIT_VERSION);
	pbreply.set_note(buffer);
	pbreply.set_retcode(0);
	break;

    case pb::MT_RTAPI_APP_EXIT:
	assert(pbreq.has_instance());
	exit_actions(pbreq.instance());
	force_exit = true;
	pbreply.set_retcode(0);
	break;

    case pb::MT_RTAPI_APP_LOADRT:
	assert(pbreq.has_modname());
	assert(pbreq.has_instance());
	pbreply.set_retcode(do_load_cmd(pbreq.instance(),pbreq.modname(), pbreq.argv()));
	break;

    case pb::MT_RTAPI_APP_UNLOADRT:
	assert(pbreq.has_instance());
	assert(pbreq.has_modname());
	pbreply.set_retcode(do_unload_cmd(pbreq.instance(),pbreq.modname()));
	break;

    case pb::MT_RTAPI_APP_LOG_USR:
	assert(pbreq.has_loglevel());
	global_data->user_msg_level = pbreq.loglevel();
	rtapi_print_msg(RTAPI_MSG_DBG, "User msglevel set to %d\n",
			global_data->user_msg_level);
	pbreply.set_retcode(0);
	break;

    case pb::MT_RTAPI_APP_LOG_RT:
	assert(pbreq.has_loglevel());
	global_data->rt_msg_level = pbreq.loglevel();
	rtapi_set_msg_level(global_data->rt_msg_level);
	rtapi_print_msg(RTAPI_MSG_DBG, "RT msglevel set to %d\n",
			global_data->rt_msg_level);
	pbreply.set_retcode(0);
	break;

#if DEPRECATED
    case pb::MT_RTAPI_APP_NEWINST:
	pbreply.set_retcode(0);
	break;
#endif

    case pb::MT_RTAPI_APP_NEWTHREAD:
	pbreply.set_retcode(-1);
	break;

    case pb::MT_RTAPI_APP_DELTHREAD:
	pbreply.set_retcode(-1);
	break;

    default:
	rtapi_print_msg(RTAPI_MSG_ERR,
			"unkown command type %d)",
			(int) pbreq.type());
	zmsg_destroy(&r);
	return 0;
    }
    // TODO: extract + attach error message

    size_t reply_size = pbreply.ByteSize();
    zframe_t *reply = zframe_new (NULL, reply_size);
    if (!pbreply.SerializeWithCachedSizesToArray(zframe_data (reply))) {
	zframe_destroy(&reply);
	rtapi_print_msg(RTAPI_MSG_ERR,
			"cant serialize to %s (type %d size %d)",
			origin ? origin : "NULL",
			pbreply.type(),
			reply_size);

    } else {
	if (debug) {
	    string buffer;
	    if (TextFormat::PrintToString(pbreply, &buffer)) {
		fprintf(stderr, "reply: %s\n",buffer.c_str());
	    }
	}
	assert(zstr_sendm (poller->socket, origin) == 0);
	if (zframe_send (&reply, poller->socket, 0)) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "cant serialize to %s (type %d size %d)",
			    origin ? origin : "NULL",
			    pbreply.type(),
			    zframe_size(reply));
	}
    }
    zmsg_destroy(&r);
    if (force_exit) // terminate the zloop
	return -1;
    return 0;
}

static int service_discovery_request(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    char buffer[8192];
    pb::Container rx, tx;
    struct sockaddr_in remote_addr = {0};
    socklen_t addrlen = sizeof(remote_addr);

    size_t recvlen = recvfrom(poller->fd, buffer,
			      sizeof(buffer), 0,
			      (struct sockaddr *)&remote_addr, &addrlen);
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "service disovery: request size %d from %s",
		    recvlen, inet_ntoa(remote_addr.sin_addr));

    if (rx.ParseFromArray(buffer, recvlen)) {
	std::string text;
	if (rx.type() ==  pb::MT_SERVICE_PROBE) {
	    pb::ServiceAnnouncement *sa;

	    tx.set_type(pb::MT_SERVICE_ANNOUNCEMENT);
	    sa = tx.add_service_announcement();
	    sa->set_instance(instance_id);
	    sa->set_stype(pb::ST_RTAPI_COMMAND);
	    sa->set_version(1);
	    sa->set_cmd_port(z_port);

	    size_t txlen = tx.ByteSize();
	    assert(txlen < sizeof(buffer));
	    tx.SerializeWithCachedSizesToArray((uint8 *)buffer);

	    struct sockaddr_in destaddr = {0};
	    destaddr.sin_port = htons(sd_port);
	    destaddr.sin_family = AF_INET;
	    destaddr.sin_addr = remote_addr.sin_addr;

	    if (sendto(poller->fd, buffer, txlen, 0,
		       (struct sockaddr *)&destaddr, sizeof(destaddr)) < 0) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"sd: sendto(%s) failed: %s",
				inet_ntoa(remote_addr.sin_addr),
				strerror(errno));
	    }
	}
    } else {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"sd: can parse request from %s (size %d)",
			inet_ntoa(remote_addr.sin_addr),recvlen);
    }
    return 0;
}

static int s_handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"read(signal_fd): %s", strerror(errno));
	return 0;
    }
    switch (fdsi.ssi_signo) {
    case SIGINT:
    case SIGTERM:
	rtapi_print_msg(RTAPI_MSG_INFO,
			"signal %d - '%s' received, exiting",
			fdsi.ssi_signo, strsignal(fdsi.ssi_signo));

	exit_actions(instance_id);
	interrupted = true; // make mainloop exit
	return -1;
    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "unhandled signal %d - '%s' received\n",
			fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    }
    return 0;
}


static int mainloop(size_t  argc, char **argv)
{
    int retval;
    unsigned i;
    static char proctitle[20];

    if (!foreground) {
	pid_t pid = fork();
	if (pid > 0) { // parent
	    exit(0);
	}
	if (pid < 0) { // fork failed
	    perror("fork");
	    exit(1);
	}
    }
    // in child

    // set new process name
    snprintf(proctitle, sizeof(proctitle), "rtapi:%d",instance_id);
    size_t argv0_len = strlen(argv[0]);
    size_t procname_len = strlen(proctitle);
    size_t max_procname_len = (argv0_len > procname_len) ?
	(procname_len) : (argv0_len);

    strncpy(argv[0], proctitle, max_procname_len);
    memset(&argv[0][max_procname_len], '\0', argv0_len - max_procname_len);

    for (i = 1; i < argc; i++)
	memset(argv[i], '\0', strlen(argv[i]));

    // set this thread's name so it can be identified in ps/top as
    // rtapi:<instance>
    if (prctl(PR_SET_NAME, argv[0]) < 0) {
	syslog_async(LOG_ERR,	"rtapi_app: prctl(PR_SETNAME,%s) failed: %s\n",
		argv[0], strerror(errno));
    }

    // attach global segment which rtapi_msgd already prepared
    if ((retval = attach_global_segment()) != 0) {
	syslog_async(LOG_ERR, "%s: FATAL - failed to attach to global segment\n",
		argv[0]);
	write_exitcode(statuspipe[1], 1);
	exit(retval);
    }

    // make sure rtapi_msgd's pid is valid and msgd is running, 
    // in case we caught a leftover shmseg
    // otherwise log messages would vanish
    if ((global_data->rtapi_msgd_pid == 0) ||
	kill(global_data->rtapi_msgd_pid, 0) != 0) {
	syslog_async(LOG_ERR,"%s: rtapi_msgd pid invalid: %d, exiting\n",
		argv[0], global_data->rtapi_msgd_pid);
	write_exitcode(statuspipe[1], 2);
	exit(EXIT_FAILURE);
    }

    // from here on it is safe to use rtapi_print() and friends as 
    // the error ring is now set up and msgd is logging it
    rtapi_set_logtag("rtapi_app");
    rtapi_set_msg_level(global_data->rt_msg_level);

    // obtain handle on flavor descriptor as detected by rtapi_msgd
    flavor = flavor_byid(global_data->rtapi_thread_flavor);
    if (flavor == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"FATAL - invalid flavor id: %d\n",
			global_data->rtapi_thread_flavor);
	global_data->rtapi_app_pid = 0;
	exit(EXIT_FAILURE);
    }

    // make sure we're setuid root when we need to
    if (use_drivers || (flavor->flags & FLAVOR_DOES_IO)) {
	if (geteuid() != 0)
	    do_setuid();
	if (geteuid() != 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "rtapi_app:%d need to"
			    " 'sudo make setuid' to access I/O?\n",
			    instance_id);
	    global_data->rtapi_app_pid = 0;
	    exit(EXIT_FAILURE);
	}
	undo_setuid();
    }

    // assorted RT incantations - memory locking, prefaulting etc
    if ((retval = harden_rt())) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"rtapi_app:%d failed to setup "
			"realtime environment - 'sudo make setuid' missing?\n", 
			instance_id);
	global_data->rtapi_app_pid = 0;
	exit(retval);
    }

    // load rtapi and hal_lib
    if (init_actions(instance_id)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"init_actions() failed\n");
	global_data->rtapi_app_pid = 0;
	exit(1);
    }

    // zmq & service discovery incantations

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    zsys_handler_set(NULL);

    zctx_t *z_context = zctx_new ();
    void *z_command = zsocket_new (z_context, ZMQ_ROUTER);
    {
	char z_ident[30];
	snprintf(z_ident, sizeof(z_ident), "rtapi_app%d", getpid());
	zsocket_set_identity(z_command, z_ident);
	zsocket_set_linger(z_command, 1000); // wait for last reply to drain
    }
    if ((z_port = zsocket_bind(z_command, z_uri)) == -1) {
	rtapi_print_msg(RTAPI_MSG_ERR,  "cannot bind '%s' - %s\n",
			z_uri, strerror(errno));
	global_data->rtapi_app_pid = 0;
	exit(EXIT_FAILURE);
    } else {
	rtapi_print_msg(RTAPI_MSG_DBG,  "command socket bound to port %d\n",
			z_port);
    }

    zloop_t *z_loop = zloop_new();
    assert(z_loop);
    zloop_set_verbose (z_loop, debug);

    zmq_pollitem_t signal_poller = { 0, signal_fd, ZMQ_POLLIN };
    zloop_poller (z_loop, &signal_poller, s_handle_signal, NULL);

    zmq_pollitem_t command_poller = { z_command, 0, ZMQ_POLLIN };
    zloop_poller(z_loop, &command_poller, rtapi_request, NULL);

    int sd_socket = -1;
    if (sd_port) {
	sd_socket = register_service_discovery(sd_port);
	if (sd_socket > -1) {
	    zmq_pollitem_t sd_poller = { NULL, sd_socket, ZMQ_POLLIN, 0 };
	    zloop_poller(z_loop, &sd_poller, service_discovery_request, NULL);
	}
    }

    // report success
    rtapi_print_msg(RTAPI_MSG_INFO, "rtapi_app:%d ready flavor=%s gcc=%s git=%s",
		    instance_id, flavor->name,  __VERSION__, GIT_VERSION);

    // the RT stack is now set up and good for use
    global_data->rtapi_app_pid = getpid();

    // main loop
    do {
	retval = zloop_start(z_loop);
    } while  (!(retval || interrupted));

    rtapi_print_msg(RTAPI_MSG_INFO,
		    "exiting mainloop (%s)\n",
		    interrupted ? "interrupted": "by remote command");

    // exiting, so deregister our pid, which will make rtapi_msgd exit too
    global_data->rtapi_app_pid = 0;
    return 0;
}

static int configure_memory(void)
{
    unsigned int i, pagesize;
    char *buf;

    if (global_data->rtapi_thread_flavor != RTAPI_POSIX_ID) {
	// Realtime tweak requires privs
	/* Lock all memory. This includes all current allocations (BSS/data)
	 * and future allocations. */
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) {
	    rtapi_print_msg(RTAPI_MSG_WARN, 
			    "mlockall() failed: %d '%s'\n",
			    errno,strerror(errno));
	    return 1;
	}
    }

    /* Turn off malloc trimming.*/
    if (!mallopt(M_TRIM_THRESHOLD, -1)) {
	rtapi_print_msg(RTAPI_MSG_WARN,
			"mallopt(M_TRIM_THRESHOLD, -1) failed\n");
	return 1;
    }
    /* Turn off mmap usage. */
    if (!mallopt(M_MMAP_MAX, 0)) {
	rtapi_print_msg(RTAPI_MSG_WARN,
			"mallopt(M_MMAP_MAX, -1) failed\n");
	return 1;
    }
    buf = static_cast<char *>(malloc(PRE_ALLOC_SIZE));
    if (buf == NULL) {
	rtapi_print_msg(RTAPI_MSG_WARN, "malloc(PRE_ALLOC_SIZE) failed\n");
	return 1;
    }
    pagesize = sysconf(_SC_PAGESIZE);
    /* Touch each page in this piece of memory to get it mapped into RAM */
    for (i = 0; i < PRE_ALLOC_SIZE; i += pagesize) {
	/* Each write to this buffer will generate a pagefault.
	 * Once the pagefault is handled a page will be locked in
	 * memory and never given back to the system. */
	buf[i] = 0;
    }
    /* buffer will now be released. As Glibc is configured such that it
     * never gives back memory to the kernel, the memory allocated above is
     * locked for this process. All malloc() and new() calls come from
     * the memory pool reserved and locked above. Issuing free() and
     * delete() does NOT make this locking undone. So, with this locking
     * mechanism we can build C++ applications that will never run into
     * a major/minor pagefault, even with swapping enabled. */
    free(buf);

    return 0;
}

static void
exit_handler(void)
{
    struct rusage rusage;

    getrusage(RUSAGE_SELF, &rusage);
    if ((rusage.ru_majflt - majflt) > 0) {
	// RTAPI already shut down here
	rtapi_print_msg(RTAPI_MSG_WARN,
			"rtapi_app_main %d: %ld page faults, %ld page reclaims\n",
			getpid(), rusage.ru_majflt - majflt,
			rusage.ru_minflt - minflt);
    }
}

static void setup_signals(void)
{
    sigset_t mask;

    sigemptyset( &mask );
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGSEGV);
    sigaddset(&mask, SIGILL);
    sigaddset(&mask, SIGFPE);
    // sigaddset(&mask, SIGXCPU);

    assert(sigprocmask(SIG_BLOCK, &mask, NULL) != -1);
    signal_fd = signalfd(-1, &mask, 0);
    assert(signal_fd > -1);
}

static int harden_rt()
{
    // enable core dumps
    struct rlimit core_limit;
    core_limit.rlim_cur = RLIM_INFINITY;
    core_limit.rlim_max = RLIM_INFINITY;

    if (setrlimit(RLIMIT_CORE, &core_limit) < 0)
	rtapi_print_msg(RTAPI_MSG_WARN, 
			"setrlimit: %s - core dumps may be truncated or non-existant\n",
			strerror(errno));

    // even when setuid root
    if (prctl(PR_SET_DUMPABLE, 1) < 0)
	rtapi_print_msg(RTAPI_MSG_WARN, 
			"prctl(PR_SET_DUMPABLE) failed: no core dumps will be created - %d - %s\n",
			errno, strerror(errno));

    configure_memory();

    if (getrusage(RUSAGE_SELF, &rusage)) {
	rtapi_print_msg(RTAPI_MSG_WARN, 
			"getrusage(RUSAGE_SELF) failed: %d '%s'\n",
			errno,strerror(errno));
    } else {
	minflt = rusage.ru_minflt;
	majflt = rusage.ru_majflt;
	if (atexit(exit_handler)) {
	    rtapi_print_msg(RTAPI_MSG_WARN, 
			    "atexit() failed: %d '%s'\n",
			    errno,strerror(errno));
	}
    }

    if (!foreground)
	setsid(); // Detach from the parent session

    setup_signals();

    if (flavor->id == RTAPI_XENOMAI_ID) {
	int retval = user_in_xenomai_group();

	switch (retval) {
	case 1:
	    break;
	case 0:
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "this user is not member of group xenomai");
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "please 'sudo adduser <username>  xenomai',"
			    " logout and login again");
	    return -1;

	default:
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "cannot determine if this user is a member of group xenomai: %s",
			    strerror(-retval));
	    return -1;
	}
    }

#if defined(__x86_64__) || defined(__i386__)

    // this is a bit of a shotgun approach and should be made more selective
    // however, due to serial invocations of rtapi_app during setup it is not
    // guaranteed the process executing e.g. hal_parport's rtapi_app_main is
    // the same process which starts the RT threads, causing hal_parport
    // thread functions to fail on inb/outb
    if (use_drivers || (flavor->flags & FLAVOR_DOES_IO)) {
	do_setuid();
	if (iopl(3) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "cannot gain I/O privileges - "
			    "forgot 'sudo make setuid'?\n");
	    return -EPERM;
	}
	undo_setuid();
    }
#endif
    return 0;
}

// normally rtapi_app will log through the message ringbuffer in the
// global data segment. This isnt available initially, and during shutdown,
// so switch to direct syslog_async during these time windows so we dont
// loose log messages, even if they cant go through the ringbuffer
void rtapi_app_msg_handler(msg_level_t level, const char *fmt,
				va_list ap) {
    // during startup the global segment might not be
    // available yet, so use stderr until then
    if (global_data) {
	vs_ring_write(level, fmt, ap);
    } else {
	vsyslog_async(rtapi2syslog(level), fmt, ap);
    }
}

// use this handler if -F/--foreground was given
void stderr_rtapi_msg_handler(msg_level_t level, const char *fmt,
				  va_list ap) {
    vfprintf(stderr, fmt, ap);
}

static void usage(int argc, char **argv) 
{
    printf("Usage:  %s [options]\n", argv[0]);
}

static struct option long_options[] = {
    {"help",  no_argument,          0, 'h'},
    {"foreground",  no_argument,    0, 'F'},
    {"instance", required_argument, 0, 'I'},
    {"drivers",   required_argument, 0, 'D'},
    {"uri",   required_argument,    0, 'U'},
    {"debug",   no_argument,    0, 'd'},
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    int c;
    progname = argv[0];

    // drop privs unless needed
    save_uid();
    undo_setuid();

    rtapi_set_msg_handler(rtapi_app_msg_handler);
    openlog_async(argv[0], LOG_NDELAY, LOG_LOCAL1);
    setlogmask_async(LOG_UPTO(LOG_DEBUG));
    // max out async syslog buffers for slow system in debug mode
    tunelog_async(99,1000);

    while (1) {
	int option_index = 0;
	int curind = optind;
	c = getopt_long (argc, argv, "hH:m:I:f:r:U:NFd",
			 long_options, &option_index);
	if (c == -1)
	    break;

	switch (c)	{

	case 'd':
	    debug++;
	    break;

	case 'D':
	    use_drivers = 1;
	    break;

	case 'F':
	    foreground = 1;
	    rtapi_set_msg_handler(stderr_rtapi_msg_handler);
	    break;

	case 'I':
	    instance_id = atoi(optarg);
	    break;

	case 'f':
	    if ((flavor = flavor_byname(optarg)) == NULL) {
		fprintf(stderr, "no such flavor: '%s' -- valid flavors are:\n", 
			optarg);
		flavor_ptr f = flavors;
		while (f->name) {
		    fprintf(stderr, "\t%s\n", f->name);
		    f++;
		}
		exit(1);
	    }
	    break;

	case 'U':
	    z_uri = optarg;
	    break;

	case '?':
	    if (optopt)  fprintf(stderr, "bad short opt '%c'\n", optopt);
	    else  fprintf(stderr, "bad long opt \"%s\"\n", argv[curind]);
	    //usage(argc, argv);
	    exit(1);
	    break;

	default:
	    usage(argc, argv);
	    exit(0);
	}
    }

    // sanity
    // the actual checking for setuid happens in harden_rt() (if needed)
    if (getuid() == 0) {
	fprintf(stderr, "%s: FATAL - will not run as root\n", progname);
	exit(EXIT_FAILURE);
    }

    exit(mainloop(argc, argv));
}

static int register_service_discovery(int sd_port)
{
    int sd_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd_fd == -1) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"service discovery: socket() failed: %s",
			strerror(errno));
	return -1;
    }
    struct sockaddr_in sd_addr =  {0};
    sd_addr.sin_family = AF_INET;
    sd_addr.sin_port = htons(sd_port);
    sd_addr.sin_addr.s_addr = INADDR_ANY;
    int on = 1;
    //  since there might be several servers on a host,
    //  allow multiple owners to bind to socket; incoming
    //  messages will replicate to each owner
    if (setsockopt (sd_fd, SOL_SOCKET, SO_REUSEADDR,
		    (char *) &on, sizeof (on)) == -1) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"service discovery: setsockopt (SO_REUSEADDR) failed: %s",
			strerror(errno));
	return -1;
    }
#if defined (SO_REUSEPORT)
    // On some platforms we have to ask to reuse the port
    // as of linux 3.9 - ignore failure for earlier kernels
    setsockopt (sd_fd, SOL_SOCKET, SO_REUSEPORT,
		(char *) &on, sizeof (on));
#endif
    if (bind(sd_fd, (struct sockaddr*)&sd_addr, sizeof(sd_addr) ) == -1) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"service discovery: bind() failed: %s",
			strerror(errno));
	return -1;
    }
    return sd_fd;
}

// normally rtapi_app will log through the message ringbuffer in the
// global data segment. This isnt available initially, and during shutdown,
// so switch to direct syslog during these time windows so we dont
// loose log messages, even if they cant go through the ringbuffer
static void rtapi_app_msg_handler(msg_level_t level, const char *fmt,
				  va_list ap)
{
    // during startup the global segment might not be
    // available yet, so use stderr until then
    if (global_data) {
	vs_ring_write(level, fmt, ap);
    } else {
	vsyslog(rtapi2syslog(level), fmt, ap);
    }
}

// use this handler if -F/--foreground was given
static void stderr_rtapi_msg_handler(msg_level_t level,
				     const char *fmt, va_list ap)
{
    vfprintf(stderr, fmt, ap);
}


static uid_t euid, ruid;

void save_uid(void)
{
    ruid = getuid();
    euid = geteuid();
}

// Restore the effective UID to its original value.
void do_setuid (void)
{
    int status = seteuid(euid);
    if (status < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"FATAL: do_setuid(): cannot set uid to %d - %s\n",
			euid, strerror(errno));
	if (global_data)
	    global_data->rtapi_app_pid = 0;
	exit(1);
    }
}

// Set the effective UID to the real UID.
void undo_setuid (void)
{
    int status = seteuid (ruid);
    if (status < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"FATAL: undo_setuid(): - could not set uid to %d - %s\n",
			ruid, strerror(errno));
	if (global_data)
	    global_data->rtapi_app_pid = 0;
	exit(1);
    }
}
