/********************************************************************
 * Description:  the RTAPI message deamon
 *
 * polls the rtapi message ring in the global data segment and eventually logs them
 * this is the single place for RTAPI and any ULAPI processes where log messages
 * pass through, regardless of origin or thread style (kernel, rtapi_app, ULAPI)

 * eventually this will become a zeroMQ PUBLISH server making messages available
 * to any interested subscribers
 * the PUBLISH/SUBSCRIBE pattern will also fix the current situation where an error
 * message consumed by an entity is not seen by any other entities
 *
 *
 * Copyright (C) 2012, 2013  Michael Haberler <license AT mah DOT priv DOT at>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ********************************************************************/

#include "rtapi.h"
#include "rtapi_compat.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <errno.h>
#include <assert.h>
#include <syslog_async.h>
#include <uuid/uuid.h>
#include <string>
#include <vector>
#include <poll.h>
#include <assert.h>

using namespace std;

#include <rtapi.h>
#include <rtapi/shmdrv/shmdrv.h>
#include <ring.h>
#include <sdpublish.h>  // for UDP service discovery
#include <setup_signals.h>

#include <czmq.h>

#include <google/protobuf/text_format.h>
#include <machinetalk/generated/message.pb.h>
using namespace google::protobuf;

#include <json2pb.hh>
#include <jansson.h> // just for library version tag

#include <zwsproxy.h>
static void lwsl_emit_rtapilog(int level, const char *line);
static int json_policy(zwsproxy_t *self, zws_session_t *wss, zwscb_type type);

#ifndef SYSLOG_FACILITY
#define SYSLOG_FACILITY LOG_LOCAL1  // where all rtapi/ulapi logging goes
#endif
#define GRACE_PERIOD 2000 // ms to wait after rtapi_app exit detected

int rtapi_instance;
static int log_stderr;
static int foreground;
static int use_shmdrv;
static flavor_ptr flavor;
static int usr_msglevel = RTAPI_MSG_INFO ;
static int rt_msglevel = RTAPI_MSG_INFO ;
static int halsize = HAL_SIZE;
static const char *instance_name;
static int hal_thread_stack_size = HAL_STACKSIZE;
static int signal_fd;
static int sd_port = SERVICE_DISCOVERY_PORT;
static spub_t *sd_publisher;
static uuid_t uuid;

// messages tend to come bunched together, e.g during startup and shutdown
// poll faster if a message was read, and decay the poll timer up to msg_poll_max
// if no messages are pending
// this way we retrieve bunched messages fast without much overhead in idle times
static int msg_poll_max = 200; // maximum msgq checking interval, mS
static int msg_poll_min = 20;  // minimum msgq checking interval
static int msg_poll_inc = 10;  // increment interval if no message read up to msg_poll_max
static int msg_poll =     20;  // current delay; startup fast
static int polltimer_id;      // as returned by zloop_timer()
static int shutdowntimer_id;

// zeroMQ related
static zloop_t* loop;
static void *logpub;  // zeromq log publisher socket
static zctx_t *zctx;
static const char *logpub_uri = "tcp://127.0.0.1:5550";
static zwsproxy_t *zws;    // zeroMQ/websockets proxy instance
static int wsdebug = 0;    // websockets
static int sddebug = 0;    // service discover
static bool have_json_subs;

int shmdrv_loaded;
long page_size;
global_data_t *global_data;
static ringbuffer_t rtapi_msg_buffer;   // ring access strcuture for messages
static const char *progname;
static char proctitle[20];
static int exit_code  = 0;
static const char *origins[] = { "kernel", "rt", "user" };

static void usage(int argc, char **argv)
{
    printf("Usage:  %s [options]\n", argv[0]);
}

pid_t pid_of(const char *fmt, ...)
{
    char line[LINELEN];
    FILE *cmd;
    pid_t pid;
    va_list ap;

    strcpy(line, "pidof ");
    va_start(ap, fmt);
    vsnprintf(line + strlen(line), sizeof(line) - strlen(line), fmt, ap);
    va_end(ap);
    cmd = popen(line, "r");
    if (!fgets(line, sizeof(line), cmd))
	pid = -1;
    else
	pid = strtoul(line, NULL, 10);
    pclose(cmd);
    return pid;
}

static int create_global_segment()
{
    int retval = 0;

    int globalkey = OS_KEY(GLOBAL_KEY, rtapi_instance);
    int rtapikey = OS_KEY(RTAPI_KEY, rtapi_instance);
    int halkey = OS_KEY(HAL_KEY, rtapi_instance);

    int global_exists = shm_common_exists(globalkey);
    int hal_exists = shm_common_exists(halkey);
    int rtapi_exists = shm_common_exists(rtapikey);

    if (global_exists || rtapi_exists || hal_exists) {
	// hm, something is wrong here

	pid_t msgd_pid = pid_of("msgd:%d", rtapi_instance);

	if (rtapi_instance == kernel_instance_id()) {

	    // collision with a running kernel instance - not good.
	    int shmdrv_loaded = is_module_loaded("shmdrv");
	    int rtapi_loaded = is_module_loaded("rtapi");
	    int hal_loaded = is_module_loaded("hal_lib");

	    fprintf(stderr, "ERROR: found existing kernel "
		   "instance with the same instance id (%d)\n",
		   rtapi_instance);

	    fprintf(stderr,"kernel modules loaded: %s%s%s\n",
		   shmdrv_loaded ? "shmdrv " : "",
		   rtapi_loaded ? "rtapi " : "",
		   hal_loaded ? "hal_lib " : "");

	    if (msgd_pid > 0)
		fprintf(stderr,"the msgd process msgd:%d is "
		       "already running, pid: %d\n",
		       rtapi_instance, msgd_pid);
	    else
		fprintf(stderr,"msgd:%d not running!\n",
		       rtapi_instance);
	    return -EEXIST;
	}

	// running userthreads instance?
	pid_t app_pid = pid_of("rtapi:%d", rtapi_instance);

	if ((msgd_pid > -1) || (app_pid > -1)) {

	    fprintf(stderr, "ERROR: found existing user RT "
		   "instance with the same instance id (%d)\n",
		   rtapi_instance);
	    if (msgd_pid > 0)
		fprintf(stderr,"the msgd process msgd:%d is "
		       "already running, pid: %d\n",
		       rtapi_instance, msgd_pid);
	    else
		fprintf(stderr,"msgd:%d not running!\n",
		       rtapi_instance);

	    if (app_pid > 0)
		fprintf(stderr,"the RT process rtapi:%d is "
		       "already running, pid: %d\n",
		       rtapi_instance, app_pid);
	    else
		fprintf(stderr,"the RT process rtapi:%d not running!\n",
		       rtapi_instance);

	    // TBD: might check for other user HAL processes still
	    // around. This might work with fuser on the HAL segment
	    // but might be tricky wit shmdrv.

	    return -EEXIST;
	}

	// leftover shared memory segments were around, but no using
	// entities (user process or kernel modules).
	// Remove and keep going:
	if (shmdrv_loaded) {
	    // since neiter rtapi.ko nor hal_lib.ko is loaded
	    // cause a garbage collect in shmdrv
	    shmdrv_gc();
	} else {
	    // Posix shm case.
	    char segment_name[LINELEN];

	    if (hal_exists) {
		sprintf(segment_name, SHM_FMT, rtapi_instance, halkey);
		fprintf(stderr,"warning: removing unused HAL shm segment %s\n",
			segment_name);
		if (shm_unlink(segment_name))
		    perror(segment_name);
	    }
	    if (rtapi_exists) {
		sprintf(segment_name, SHM_FMT, rtapi_instance, rtapikey);
		fprintf(stderr,"warning: removing unused RTAPI"
			" shm segment %s\n",
			segment_name);
		if (shm_unlink(segment_name))
		    perror(segment_name);
	    }
	    if (global_exists) {
		sprintf(segment_name, SHM_FMT, rtapi_instance, globalkey);
		fprintf(stderr,"warning: removing unused global"
			" shm segment %s\n",
			segment_name);
		if (shm_unlink(segment_name))
		    perror(segment_name);
	    }
	}
    }

    // now try again:
    if (shm_common_exists(globalkey)) {
	fprintf(stderr,
		"MSGD:%d ERROR: found existing global segment key=0x%x\n",
		rtapi_instance, globalkey);

	return -EEXIST;
    }

    int size = sizeof(global_data_t);

    retval = shm_common_new(globalkey, &size,
			    rtapi_instance, (void **) &global_data, 1);
    if (retval < 0) {
	fprintf(stderr,
		"MSGD:%d ERROR: cannot create global segment key=0x%x %s\n",
	       rtapi_instance, globalkey, strerror(-retval));
    }
    if (size != sizeof(global_data_t)) {
	fprintf(stderr,
		"MSGD:%d ERROR: global segment size mismatch: expect %d got %d\n",
	       rtapi_instance, sizeof(global_data_t), size);
	return -EINVAL;
    }
    return retval;
}

void init_global_data(global_data_t * data, int flavor,
		      int instance_id, int hal_size,
		      int rt_level, int user_level,
		      const char *name, int stack_size)
{
    // force-lock - we're first, so thats a bit theoretical
    rtapi_mutex_try(&(data->mutex));
    // touch all memory exposed to RT
    memset(data, 0, sizeof(global_data_t));

    // lock the global data segment
    if (flavor != RTAPI_POSIX_ID) {
	if (mlock(data, sizeof(global_data_t))) {
	    syslog_async(LOG_ERR, "MSGD:%d mlock(global) failed: %d '%s'\n",
		   instance_id, errno,strerror(errno));
	}
    }
    // report progress
    data->magic = GLOBAL_INITIALIZING;
    /* set version code so other modules can check it */
    data->layout_version = GLOBAL_LAYOUT_VERSION;

    data->instance_id = instance_id;

    if ((name == NULL) || (strlen(name) == 0)) {
	snprintf(data->instance_name, sizeof(data->instance_name),
		 "inst%d",rtapi_instance);
    } else {
	strncpy(data->instance_name,name, sizeof(data->instance_name));
    }

    // separate message levels for RT and userland
    data->rt_msg_level = rt_level;
    data->user_msg_level = user_level;

    // counter for unique handles within an RTAPI instance
    // guaranteed not to collide with a any module ID, so start at
    // RTAPI_MAX_MODULES + 1 (relevant for khreads)
    // uthreads use arbitrary ints since those dont use fixed-size arrays
    data->next_handle = RTAPI_MAX_MODULES + 1;

    // tell the others what we determined as the proper flavor
    data->rtapi_thread_flavor = flavor;

    // HAL segment size
    data->hal_size = hal_size;

    // stack size passed to rtapi_task_new() in hal_create_thread()
    data->hal_thread_stack_size = stack_size;

    // init the error ring
    ringheader_init(&data->rtapi_messages, 0, SIZE_ALIGN(MESSAGE_RING_SIZE), 0);
    memset(&data->rtapi_messages.buf[0], 0, SIZE_ALIGN(MESSAGE_RING_SIZE));

    // attach to the message ringbuffer
    ringbuffer_init(&data->rtapi_messages, &rtapi_msg_buffer);
    rtapi_msg_buffer.header->refcount = 1; // rtapi not yet attached
    rtapi_msg_buffer.header->reader = getpid();
    data->rtapi_messages.use_wmutex = 1; // locking hint

    // demon pids
    data->rtapi_app_pid = -1; // not yet started
    data->rtapi_msgd_pid = 0;

    /* done, release the mutex */
    rtapi_mutex_give(&(data->mutex));
    return;
}

// determine if we can run this flavor on the current kernel
static int flavor_and_kernel_compatible(flavor_ptr f)
{
    int retval = 1;

    if (f->id == RTAPI_POSIX_ID)
	return 1; // no prerequisites

    if (kernel_is_xenomai()) {
	if (f->id == RTAPI_RT_PREEMPT_ID) {
	    fprintf(stderr,
		    "MSGD:%d Warning: starting %s RTAPI on a Xenomai kernel\n",
		    rtapi_instance, f->name);
	    return 1;
	}
	if ((f->id != RTAPI_XENOMAI_ID) &&
	    (f->id != RTAPI_XENOMAI_KERNEL_ID)) {
	    fprintf(stderr,
		    "MSGD:%d ERROR: trying to start %s RTAPI on a Xenomai kernel\n",
		    rtapi_instance, f->name);
	    return 0;
	}
    }

    if (kernel_is_rtai() &&
	(f->id != RTAPI_RTAI_KERNEL_ID)) {
	fprintf(stderr, "MSGD:%d ERROR: trying to start %s RTAPI on an RTAI kernel\n",
		    rtapi_instance, f->name);
	return 0;
    }

    if (kernel_is_rtpreempt() &&
	(f->id != RTAPI_RT_PREEMPT_ID)) {
	fprintf(stderr, "MSGD:%d ERROR: trying to start %s RTAPI on an RT PREEMPT kernel\n",
		rtapi_instance, f->name);
	return 0;
    }
    return retval;
}

static int s_handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    if (global_data) {
	global_data->magic = GLOBAL_EXITED;
	global_data->rtapi_msgd_pid = 0;
    }
    // no point in keeping rtapi_app running if msgd exits
    if (global_data->rtapi_app_pid > 0) {
	kill(global_data->rtapi_app_pid, SIGTERM);
	syslog_async(LOG_INFO,"sent SIGTERM to rtapi (pid %d)\n",
	       global_data->rtapi_app_pid);
    }

    struct signalfd_siginfo fdsi;
    ssize_t s = read(poller->fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }

    switch (fdsi.ssi_signo) {
    case SIGTERM:
    case SIGINT:
	syslog_async(LOG_INFO, "msgd:%d: %s - shutting down\n",
	       rtapi_instance, strsignal(fdsi.ssi_signo));

	// hint if error ring couldnt be served fast enough,
	// or there was contention
	// none observed so far - this might be interesting to hear about
	if (global_data && (global_data->error_ring_full ||
			    global_data->error_ring_locked))
	    syslog_async(LOG_INFO, "msgd:%d: message ring stats: full=%d locked=%d ",
		   rtapi_instance,
		   global_data->error_ring_full,
		   global_data->error_ring_locked);
	return -1; // exit reactor normally
	break;

    default: // pretty bad
	syslog_async(LOG_ERR,
	       "msgd:%d: signal %d - '%s' received, dumping core (current dir=%s)\n",
	       rtapi_instance, fdsi.ssi_signo, strsignal(fdsi.ssi_signo),
	       get_current_dir_name());
	closelog();
	sleep(1); // let syslog drain
	signal(SIGABRT, SIG_DFL);
	abort();
	break;
    }
}

static void
cleanup_actions(void)
{
    int retval;

    if (global_data) {
	if (global_data->rtapi_app_pid > 0) {
	    kill(global_data->rtapi_app_pid, SIGTERM);
	    syslog_async(LOG_INFO,"sent SIGTERM to rtapi (pid %d)\n",
		   global_data->rtapi_app_pid);
	}
	// in case some process catches a leftover shm segment
	global_data->magic = GLOBAL_EXITED;
	global_data->rtapi_msgd_pid = 0;
	if (rtapi_msg_buffer.header != NULL)
	    rtapi_msg_buffer.header->refcount--;
	retval = shm_common_detach(sizeof(global_data_t), global_data);
	if (retval < 0) {
	    syslog_async(LOG_ERR,"shm_common_detach(global) failed: %s\n",
		   strerror(-retval));
	} else {
	    shm_common_unlink(OS_KEY(GLOBAL_KEY, rtapi_instance));
	    syslog_async(LOG_DEBUG,"normal shutdown - global segment detached");
	}
	global_data = NULL;
    }
}

// react to subscribe/unsubscribe events
static int logpub_readable_cb(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    zframe_t *f = zframe_recv(poller->socket);
    const char *s = (const char *) zframe_data(f);
    if (strcmp(s+1, "json") == 0) {
	syslog_async(LOG_DEBUG,"%s publishing JSON log messages",
	       *s ? "start" : "stop");
	have_json_subs = (*s != 0);
    }
    zframe_destroy(&f);
    return 0;
}

static int
s_start_shutdown_cb(zloop_t *loop, int  timer_id, void *args)
{
    syslog_async(LOG_ERR, "msgd shutting down");
    return -1; // exit reactor
}

static int
message_poll_cb(zloop_t *loop, int  timer_id, void *args)
{
    rtapi_msgheader_t *msg;
    size_t msg_size;
    size_t payload_length;
    int retval;
    char *cp;
    pb::Container container;
    pb::LogMessage *logmsg;
    zframe_t *z_pbframe; // , *z_jsonframe;
    std::string json;
    int current_interval = msg_poll;


    while ((retval = record_read(&rtapi_msg_buffer,
				 (const void **) &msg, &msg_size)) == 0) {
	payload_length = msg_size - sizeof(rtapi_msgheader_t);

	switch (msg->encoding) {
	case MSG_ASCII:
	    // strip trailing newlines
	    while ((cp = strrchr(msg->buf,'\n')))
		*cp = '\0';
	    syslog_async(rtapi2syslog(msg->level), "%s:%d:%s %.*s",
		   msg->tag, msg->pid, origins[msg->origin],
		   (int) payload_length, msg->buf);


	    if (logpub) {
		// publish protobuf-encoded log message
		container.set_type(pb::MT_LOG_MESSAGE);

		struct timespec timestamp;
		clock_gettime(CLOCK_REALTIME, &timestamp);
		container.set_tv_sec(timestamp.tv_sec);
		container.set_tv_nsec(timestamp.tv_nsec);

		logmsg = container.mutable_log_message();
		logmsg->set_origin((pb::MsgOrigin)msg->origin);
		logmsg->set_pid(msg->pid);
		logmsg->set_level((pb::MsgLevel) msg->level);
		logmsg->set_tag(msg->tag);
		logmsg->set_text(msg->buf, strlen(msg->buf));

		z_pbframe = zframe_new(NULL, container.ByteSize());
		assert(z_pbframe != NULL);

		if (container.SerializeWithCachedSizesToArray(zframe_data(z_pbframe))) {
		    // channel name:
		    if (zstr_sendm(logpub, "pb2"))
			syslog_async(LOG_ERR,"zstr_sendm(%s,pb2): %s",
			       logpub_uri, strerror(errno));

		    // and the actual pb2-encoded message
		    // zframe_send() deallocates the frame after sending,
		    // and frees pb_buffer through zfree_cb()
		    if (zframe_send(&z_pbframe, logpub, 0))
			syslog_async(LOG_ERR,"zframe_send(%s): %s",
			       logpub_uri, strerror(errno));

		    // convert to JSON only if we have actual connections
		    // to the log websocket:
		    if (have_json_subs) {
			json = pb2json(container);
			zframe_t *z_jsonframe = zframe_new( json.c_str(), json.size());
			//syslog_async(LOG_DEBUG, "json pub: '%s'", json.c_str());
			if (zstr_sendm(logpub, "json"))
			    syslog_async(LOG_ERR,"zstr_sendm(%s,json): %s",
				   logpub_uri, strerror(errno));
			if (zframe_send(&z_jsonframe, logpub, 0))
			    syslog_async(LOG_ERR,"zframe_send(%s): %s",
				   logpub_uri, strerror(errno));
		    }
		} else {
		    syslog_async(LOG_ERR, "container serialization failed");
		}
	    }
	    break;
	case MSG_STASHF:
	    break;
	case MSG_PROTOBUF:
	    break;
	default: ;
	    // whine
	}
	record_shift(&rtapi_msg_buffer);
	msg_poll = msg_poll_min;
    }
    msg_poll += msg_poll_inc;
    if (msg_poll > msg_poll_max)
	msg_poll = msg_poll_max;

    if (current_interval != msg_poll) {
	zloop_timer_end(loop, polltimer_id);
	polltimer_id = zloop_timer (loop, current_interval, 0, message_poll_cb, NULL);
    }

    // check for rtapi_app exit only after all pending messages are logged:
    if ((global_data->rtapi_app_pid == 0) &&
	(shutdowntimer_id ==  0)) {
	// schedule a loop shutdown but keep reading messages for a while
	// so we dont loose messages
	syslog_async(LOG_ERR, "rtapi_app exit detected - scheduled shutdown");
	shutdowntimer_id = zloop_timer (loop, GRACE_PERIOD, 1,
					s_start_shutdown_cb, NULL);
    }
    return 0;
}

// handle signals delivered via sigaction - not all signals
// can be dealt with through signalfd(2)
// log, try to do something sane, and dump core
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


static struct option long_options[] = {
    { "help",  no_argument,          0, 'h'},
    { "extensions",	no_argument,	NULL, 'e' },
    { "stderr",  no_argument,        0, 's'},
    { "foreground",  no_argument,    0, 'F'},
    { "usrmsglevel", required_argument, 0, 'u'},
    { "rtmsglevel", required_argument, 0, 'r'},
    { "instance", required_argument, 0, 'I'},
    { "instance_name", required_argument, 0, 'i'},
    { "flavor",   required_argument, 0, 'f'},
    { "halsize",  required_argument, 0, 'H'},
    { "halstacksize",  required_argument, 0, 'R'},
    { "shmdrv",  no_argument,        0, 'S'},
    { "uri", required_argument,	 NULL, 'U' },
    { "wwwdir", required_argument,      NULL, 'w' },
    { "port",	required_argument,	NULL, 'p' },
    { "wsdebug",required_argument,	NULL, 'W' },
    { "sddebug",no_argument,	NULL, 'D' },
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    int c, i, retval;
    int option = LOG_NDELAY;
    pid_t pid, sid;
    size_t argv0_len, procname_len, max_procname_len;
    struct lws_context_creation_info info = {0};
    const char *www_dir = NULL;

    uuid_generate_time(uuid);

    info.port = 7681; // move to config.h DEFAULT_HTTP_PORT
    info.gid = -1;
    info.uid = -1;

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    progname = argv[0];
    shm_common_init();

    while (1) {
	int option_index = 0;
	int curind = optind;
	c = getopt_long (argc, argv, "hI:sFf:i:SU:w:p:W:eu:r:D",
			 long_options, &option_index);
	if (c == -1)
	    break;
	switch (c)	{
	case 'e':
	    info.extensions = libwebsocket_get_internal_extensions();
	    break;
	case 'D':
	    sddebug++;
	    break;
	case 'F':
	    foreground++;
	    break;
	case 'I':
	    rtapi_instance = atoi(optarg);
	    break;
	case 'R':
	    hal_thread_stack_size = atoi(optarg);
	    break;
	case 'i':
	    instance_name = optarg;
	    break;
	case 'f':
	    if ((flavor = flavor_byname(optarg)) == NULL) {
		fprintf(stderr, "no such flavor: '%s' -- valid flavors are:\n", optarg);
		flavor_ptr f = flavors;
		while (f->name) {
		    fprintf(stderr, "\t%s\n", f->name);
		    f++;
		}
		exit(1);
	    }
	    break;
	case 'u':
	    usr_msglevel = atoi(optarg);
	    break;
	case 'r':
	    rt_msglevel = atoi(optarg);
	    break;
	case 'H':
	    halsize = atoi(optarg);
	    break;
	case 'S':
	    use_shmdrv++;
	    break;
	case 's':
	    log_stderr++;
	    option |= LOG_PERROR;
	    break;
	case 'U':
	    logpub_uri = strdup(optarg);
	    break;
	case 'w':
	    www_dir = strdup(optarg);
	    break;
	case 'p':
	    info.port = atoi(optarg);
	    break;
	case 'W':
	    wsdebug = atoi(optarg);
	    break;
	case '?':
	    if (optopt)  fprintf(stderr, "bad short opt '%c'\n", optopt);
	    else  fprintf(stderr, "bad long opt \"%s\"\n", argv[curind]);
	    exit(1);
	    break;
	default:
	    usage(argc, argv);
	    exit(0);
	}
    }

    // sanity
    if (getuid() == 0) {
	fprintf(stderr, "%s: FATAL - will not run as root\n", progname);
	exit(EXIT_FAILURE);
    }
    if (geteuid() == 0) {
	fprintf(stderr, "%s: FATAL - will not run as setuid root\n", progname);
	exit(EXIT_FAILURE);
    }

    if (flavor == NULL)
	flavor = default_flavor();

    if (flavor == NULL) {
	fprintf(stderr, "%s: FATAL - failed to detect thread flavor\n", progname);
	exit(EXIT_FAILURE);
    }

    // can we actually run what's being suggested?
    if (!flavor_and_kernel_compatible(flavor)) {
	fprintf(stderr, "%s: FATAL - cant run the %s flavor on this kernel\n",
		progname, flavor->name);
	exit(EXIT_FAILURE);
    }

    // catch installation error: user not in xenomai group
    if (flavor->id == RTAPI_XENOMAI_ID) {
	int retval = user_in_xenomai_group();

	switch (retval) {
	case 1:  // yes
	    break;
	case 0:
	    fprintf(stderr, "this user is not member of group xenomai\n");
	    fprintf(stderr, "please 'sudo adduser <username>  xenomai',"
		    " logout and login again\n");
	    exit(EXIT_FAILURE);

	default:
	    fprintf(stderr, "cannot determine if this user "
		    "is a member of group xenomai: %s\n",
		    strerror(-retval));
	    exit(EXIT_FAILURE);
	}
    }

    // do we need the shmdrv module?
    if (((flavor->flags & FLAVOR_KERNEL_BUILD) ||
	 use_shmdrv) &&
	!shmdrv_available()) {
	if (run_module_helper("insert shmdrv debug=5")) {
	    fprintf(stderr, "%s: cant insert shmdrv module - needed by %s\n",
		    progname, use_shmdrv ? "--shmdrv" : flavor->name);
	    exit(EXIT_FAILURE);
	}

	shm_common_init();
	if (!shmdrv_available()) {
	    fprintf(stderr, "%s: BUG: shmdrv module not detected\n",
		    progname);
	    exit(EXIT_FAILURE);
	}
    }

    // the global segment every entity in HAL/RTAPI land attaches to
    if ((retval = create_global_segment()) != 1) // must be a new shm segment
	exit(retval);

    // good to go
    if (!foreground) {
        pid = fork();
        if (pid < 0) {
	    exit(EXIT_FAILURE);
        }
        if (pid > 0) {
	    exit(EXIT_SUCCESS);
        }
        umask(0);
        sid = setsid();
        if (sid < 0) {
	    exit(EXIT_FAILURE);
        }
#if 0
        if ((chdir("/")) < 0) {
	    exit(EXIT_FAILURE);
        }
#endif
    }

    snprintf(proctitle, sizeof(proctitle), "msgd:%d",rtapi_instance);

    openlog_async(proctitle, option , SYSLOG_FACILITY);
    // max out async syslog buffers for slow system in debug mode
    tunelog_async(99,1000);

    // set new process name
    argv0_len = strlen(argv[0]);
    procname_len = strlen(proctitle);
    max_procname_len = (argv0_len > procname_len) ? (procname_len) : (argv0_len);

    strncpy(argv[0], proctitle, max_procname_len);
    memset(&argv[0][max_procname_len], '\0', argv0_len - max_procname_len);

    for (i = 1; i < argc; i++)
	memset(argv[i], '\0', strlen(argv[i]));


    // this is the single place in all of linuxCNC where the global segment
    // gets initialized - no reinitialization from elsewhere
    init_global_data(global_data, flavor->id, rtapi_instance,
		     halsize, rt_msglevel, usr_msglevel,
		     instance_name,hal_thread_stack_size);

    syslog_async(LOG_INFO,
		 "startup instance=%s pid=%d flavor=%s "
		 "rtlevel=%d usrlevel=%d halsize=%d shm=%s gcc=%s version=%s",
		 global_data->instance_name, getpid(),
		 flavor->name,
		 global_data->rt_msg_level,
		 global_data->user_msg_level,
		 global_data->hal_size,
		 shmdrv_loaded ? "shmdrv" : "Posix",
		 __VERSION__,
		 GIT_VERSION);

    syslog_async(LOG_INFO,"configured: %s sha=%s", CONFIG_DATE, GIT_CONFIG_SHA);
    syslog_async(LOG_INFO,"built:      %s %s sha=%s",  __DATE__, __TIME__, GIT_BUILD_SHA);
    if (strcmp(GIT_CONFIG_SHA,GIT_BUILD_SHA))
	syslog_async(LOG_WARNING, "WARNING: git SHA's for configure and build do not match!");


   if ((global_data->rtapi_msgd_pid != 0) &&
	kill(global_data->rtapi_msgd_pid, 0) == 0) {
	syslog_async(LOG_ERR, "%s: another rtapi_msgd is already running (pid %d), exiting\n",
	       progname, global_data->rtapi_msgd_pid);
	exit(EXIT_FAILURE);
    }

    int fd = open("/dev/null", O_RDONLY);
    dup2(fd, STDIN_FILENO);
    close(fd);

    // // redirect stdout to syslog
    // // NB: this works for FILE *, not the underlying STDOUT_FILENO
    // to_syslog("> ", &stdout);

    // if (!log_stderr)
    // 	to_syslog(">> ", &stderr);  // redirect stderr to syslog

    signal_fd = setup_signals(sigaction_handler);
    assert(signal_fd > -1);

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    zsys_handler_set(NULL);

    // zeroMQ context
    zctx  = zctx_new();

    // start the service announcement responder
    sd_publisher = sp_new( zctx, sd_port, rtapi_instance, uuid);
    assert(sd_publisher);
    sp_log(sd_publisher, sddebug);

    if (logpub_uri) {
	int major, minor, patch;
	zmq_version (&major, &minor, &patch);
	syslog_async(LOG_DEBUG,
	       "ØMQ=%d.%d.%d czmq=%d.%d.%d protobuf=%d.%d.%d jansson=%s libwebsockets=%s\n",
	       major, minor, patch,
	       CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH,
	       GOOGLE_PROTOBUF_VERSION / 1000000,
	       (GOOGLE_PROTOBUF_VERSION / 1000) % 1000,
	       GOOGLE_PROTOBUF_VERSION % 1000,
	       JANSSON_VERSION, lws_get_library_version());

	// start the zeromq log publisher socket
	logpub = zsocket_new(zctx, ZMQ_XPUB);

	zsocket_set_xpub_verbose (logpub, 1);  // enable reception
	if (zsocket_bind(logpub, logpub_uri) > 0) {
	    syslog_async(LOG_DEBUG, "publishing ZMQ/protobuf log messages at %s\n",
		   logpub_uri);
	    retval = sp_add(sd_publisher,
			    (int) pb::ST_LOGGING, //type
			    1, // version
			    NULL, // ip
			    0, // port
			    logpub_uri, // uri
			    (int) pb::SA_ZMQ_PROTOBUF, // api
			    "zmq/protobuf log socket");  // descr
	    assert(retval == 0);

	    lws_set_log_level(wsdebug, lwsl_emit_rtapilog);

	    if ((zws = zwsproxy_new(zctx, www_dir, &info)) == NULL) {
		syslog_async(LOG_ERR, "failed to start websockets proxy\n");
		goto nows;
	    }
	    // add a user-defined relay policy
	    zwsproxy_add_policy(zws, "json", json_policy);

	    // start serving
	    zwsproxy_start(zws);

	    retval = sp_add(sd_publisher, (int) pb::ST_WEBSOCKET,
		1, NULL, info.port, "fixme",
		(int) pb::SA_WS_JSON,
		"ws/JSON log socket");
	    assert(retval == 0);

	} else {
	    syslog_async(LOG_INFO,"zsocket_bind(%s) failed: %s\n",
		   logpub_uri, strerror(errno));
	    logpub = NULL;
	}
    }
 nows:

    loop = zloop_new();
    assert (loop);

    zmq_pollitem_t signal_poller =  { 0, signal_fd,   ZMQ_POLLIN };
    zloop_poller (loop, &signal_poller, s_handle_signal, NULL);

    zmq_pollitem_t logpub_poller = { logpub, 0, ZMQ_POLLIN };
    zloop_poller (loop, &logpub_poller, logpub_readable_cb, NULL);

    polltimer_id = zloop_timer (loop, msg_poll, 0, message_poll_cb, NULL);

    global_data->rtapi_msgd_pid = getpid();
    global_data->magic = GLOBAL_READY;

    if ((retval = sp_start(sd_publisher))) {
	syslog_async(LOG_ERR, "failed to start service publisher: %d\n", retval);
    }

    do {
	retval = zloop_start(loop);
    } while (!(retval || zctx_interrupted));

    zwsproxy_exit(&zws);

    // stop  the service announcement responder
    sp_destroy(&sd_publisher);

    // shutdown zmq context
    zctx_destroy(&zctx);

    cleanup_actions();
    closelog();
    exit(exit_code);
}

static void lwsl_emit_rtapilog(int level, const char *line)
{
	int syslog_level = LOG_DEBUG;

	switch (level) {
	case LLL_ERR:
		syslog_level = LOG_ERR;
		break;
	case LLL_WARN:
		syslog_level = LOG_WARNING;
		break;
	case LLL_NOTICE:
		syslog_level = LOG_NOTICE;
		break;
	case LLL_INFO:
		syslog_level = LOG_INFO;
		break;
	}
	syslog_async(syslog_level, "%s", line);
}

#include <zwsproxy-private.h>

// relay policy to convert to/from JSON as needed
static int
json_policy(zwsproxy_t *self,
	    zws_session_t *wss,
	    zwscb_type type)
{
    lwsl_debug("%s op=%d\n",__func__,  type);
    zmsg_t *m;
    zframe_t *f;
    static pb::Container c; // fast; threadsafe???

    switch (type) {

    case ZWS_CONNECTING:
	// > 1 indicates: run the default policy ZWS_CONNECTING code
	return 1;
	break;

    case ZWS_ESTABLISHED:
	break;

    case ZWS_CLOSE:
	break;

    case ZWS_FROM_WS:

	switch (wss->socket_type) {
	case ZMQ_DEALER:
	    {
		try{
		    json2pb(c, (const char *) wss->buffer, wss->length);
		} catch (std::exception &ex) {
		    lwsl_err("from_ws: json2pb exception: %s on '%.*s'\n",
			     ex.what(),wss->length, wss->buffer);
		}
		zframe_t *z_pbframe = zframe_new(NULL, c.ByteSize());
		assert(z_pbframe != NULL);
		if (c.SerializeWithCachedSizesToArray(zframe_data(z_pbframe))) {
		    assert(zframe_send(&z_pbframe, wss->socket, 0) == 0);
		} else {
		    lwsl_err("from_ws: cant serialize: '%.*s'\n",
			       wss->length, wss->buffer);
		    zframe_destroy(&z_pbframe);
		}
		return 0;
	    }
	    break;

	case ZMQ_SUB:
	case ZMQ_XSUB:
	    lwsl_err("dropping frame '%.*s'\n",wss->length, wss->buffer);
	    break;
	default:
	    break;
	}
	// a frame was received from the websocket client
	f = zframe_new (wss->buffer, wss->length);
	zframe_print (f, "user_policy FROM_WS:");
	return zframe_send(&f, wss->socket, 0);

    case ZWS_TO_WS:
	{
	    char *topic = NULL;

	    m = zmsg_recv(wss->socket);
	    if ((wss->socket_type == ZMQ_SUB) ||
		(wss->socket_type == ZMQ_XSUB)) {
		topic = zmsg_popstr (m);
		assert(zstr_sendf(wss->wsq_out, "{ topic = \\\"%s\\\"}\n", topic) == 0);
		free(topic);
	    }

	    while ((f = zmsg_pop (m)) != NULL) {
		// zframe_print (f, "user_policy TO_WS:");

		if (!c.ParseFromArray(zframe_data(f), zframe_size(f))) {
		    char *hex = zframe_strhex(f);
		    lwsl_err("cant protobuf parse from %s",
			     hex);
		    free(hex);
		    zframe_destroy(&f);
		    break;
		}
		// this breaks - probably needs some MergeFrom* pb method
		// if (!c.has_topic() && (topic != NULL))
		//     c.set_topic(topic); // tack on

		try {
		    std::string json = pb2json(c);
		    zframe_t *z_jsonframe = zframe_new( json.c_str(), json.size());
		    assert(zframe_send(&z_jsonframe, wss->wsq_out, 0) == 0);
		} catch (std::exception &ex) {
		    lwsl_err("to_ws: pb2json exception: %s\n",
			     ex.what());
		    std::string text;
		    if (TextFormat::PrintToString(c, &text))
			fprintf(stderr, "container: %s\n", text.c_str());
		}
	    }
	    zmsg_destroy(&m);
	}
	break;

    default:
	break;
    }
    return 0;
}
