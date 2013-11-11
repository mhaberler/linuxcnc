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
#include <signal.h>
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
#include <poll.h>
#include <sys/signalfd.h>
#include <assert.h>
#include <string>
using namespace std;

#include <rtapi.h>
#include "rtapi/shmdrv/shmdrv.h"
#include "ring.h"
#include "czmq.h"

#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/log.pb.h>
#include <protobuf/generated/message.pb.h>
using namespace google::protobuf;

#include <protobuf/json2pb/json2pb.h>
#include <jansson.h> // just for library version tag

/* Enable libev io loop */
#define LWS_USE_LIBEV
/* Turn off external polling support */
#define LWS_NO_EXTERNAL_POLL

#include <libwebsockets.h>

typedef struct {
    void *socket;
    struct ev_io watcher;
} wslog_sess_t;

typedef struct {
    const char *www_dir;
    int debug_level;
    zctx_t *z_context;
    const char *uri;
    struct ev_loop* loop;
    int num_log_connections;
} wsconfig_t;

// message_poll_cb() needs to inspect num_log_connections
static wsconfig_t ws_config;

static int callback_wslog(struct libwebsocket_context *context,
			  struct libwebsocket *wsi,
			  enum libwebsocket_callback_reasons reason,
			  void *user, void *in, size_t len);

static int callback_http(struct libwebsocket_context *context,
			 struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *user,
			 void *in, size_t len);

enum wslog_protocols {
    PROTOCOL_HTTP  = 0,
    PROTOCOL_WSLOG = 1,
};

static struct libwebsocket_protocols protocols[] = {
    {
	"http-only",		/* name */
	callback_http,		/* callback */
	0,
	0,			/* max frame size / rx buffer */
    },
    {
	"log",
	callback_wslog,
	sizeof(wslog_sess_t),
	0,
    },
    { NULL, NULL, 0, 0 } /* terminator */
};
static struct libwebsocket_context *ws_context;

static const char *mime_types = "/etc/mime.types";


#ifndef SYSLOG_FACILITY
#define SYSLOG_FACILITY LOG_LOCAL1  // where all rtapi/ulapi logging goes
#endif

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

// messages tend to come bunched together, e.g during startup and shutdown
// poll faster if a message was read, and decay the poll timer up to msg_poll_max
// if no messages are pending
// this way we retrieve bunched messages fast without much overhead in idle times

static int msg_poll_max = 200; // maximum msgq checking interval, mS
static int msg_poll_min = 1;   // minimum msgq checking interval
static int msg_poll_inc = 2;   // increment interval if no message read up to msg_poll_max
static int msg_poll = 1;       // current delay; startup fast
static int msgd_exit;          // flag set by signal handler to start shutdown

int shmdrv_loaded;
long page_size;
global_data_t *global_data;
static ringbuffer_t rtapi_msg_buffer;   // ring access strcuture for messages
static const char *progname;
static char proctitle[20];
static int exit_code  = 0;

static void *logpub;  // zeromq log publisher socket
static zctx_t *zmq_ctx;
static const char *logpub_uri = "tcp://127.0.0.1:5550";

// static const char *rtapi_levelname[] = {
//     "none",
//     "err"
//     "warn",
//     "info",
//     "dbg",
//     "all"
// };
static const char *origins[] = { "kernel","rt","user" };
//static const char *encodings[] = { "ascii","stashf","protobuf" };

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

    // next value returned by rtapi_init (userland threads)
    // those dont use fixed sized arrays
    // start at 1 because the meaning of zero is special
    data->next_module_id = 1;

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

static void signal_handler(int sig)
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

    switch (sig) {
    case SIGTERM:
    case SIGINT:
	syslog_async(LOG_INFO, "msgd:%d: %s - shutting down\n",
	       rtapi_instance, strsignal(sig));

	// hint if error ring couldnt be served fast enough,
	// or there was contention
	// none observed so far
	// this might be interesting to hear about
	if (global_data && (global_data->error_ring_full ||
			    global_data->error_ring_locked))
	    syslog_async(LOG_INFO, "msgd:%d: message ring stats: full=%d locked=%d ",
		   rtapi_instance,
		   global_data->error_ring_full,
		   global_data->error_ring_locked);
	msgd_exit++;
	break;

    default: // pretty bad
	syslog_async(LOG_ERR,
	       "msgd:%d: caught signal %d '%s' - dumping core (current dir=%s)\n",
	       rtapi_instance, sig, strsignal(sig), get_current_dir_name());
	closelog_async();
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

//static void zfree_cb(void *data, void *args) { free(data); }

static void message_poll_cb(struct ev_loop *loop,
			    struct ev_timer *timer,
			    int revents)
{
    rtapi_msgheader_t *msg;
    size_t msg_size;
    size_t payload_length;
    int retval;
    char *cp;
    int sigfd;
    sigset_t sigset;
    Container container;
    LogMessage *logmsg;
    zframe_t *z_pbframe, *z_jsonframe;
    std::string json;

    // sigset of all the signals that we're interested in
    retval = sigemptyset(&sigset);        assert(retval == 0);
    retval = sigaddset(&sigset, SIGINT);  assert(retval == 0);
    retval = sigaddset(&sigset, SIGKILL); assert(retval == 0);
    retval = sigaddset(&sigset, SIGTERM); assert(retval == 0);
    retval = sigaddset(&sigset, SIGSEGV); assert(retval == 0);
    retval = sigaddset(&sigset, SIGFPE);  assert(retval == 0);

    // block the signals in order for signalfd to receive them
    retval = sigprocmask(SIG_BLOCK, &sigset, NULL); assert(retval == 0);

    sigfd = signalfd(-1, &sigset, 0);
    assert(sigfd != -1);

    struct pollfd pfd[1];
    int ret;

    pfd[0].fd = sigfd;
    pfd[0].events = POLLIN | POLLERR | POLLHUP;

    global_data->magic = GLOBAL_READY;

    do {
	if (global_data->rtapi_app_pid == 0) {
	    syslog_async(LOG_ERR,
		   "msgd:%d: rtapi_app exit detected - shutting down",
		   rtapi_instance);
	    msgd_exit++;
	}
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
		// channel as per loglevel
		container.set_type(pb::MT_LOG_MESSAGE);

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
			syslog(LOG_ERR,"zstr_sendm(%s,pb2): %s",
			       logpub_uri, strerror(errno));

		    // and the actual pb2-encoded message
		    // zframe_send() deallocates the frame after sending,
		    // and frees pb_buffer through zfree_cb()
		    if (zframe_send(&z_pbframe, logpub, 0))
			syslog(LOG_ERR,"zframe_send(%s): %s",
			       logpub_uri, strerror(errno));

		    // convert to JSON only if we have actual connections
		    // to the log websocket:
		    if (ws_config.num_log_connections > 0) {
			json = pb2json(container);
			z_jsonframe = zframe_new( json.c_str(), json.size());

			if (zstr_sendm(logpub, "json"))
			    syslog(LOG_ERR,"zstr_sendm(%s,json): %s",
				   logpub_uri, strerror(errno));
			if (zframe_send(&z_jsonframe, logpub, 0))
			    syslog(LOG_ERR,"zframe_send(%s): %s",
				   logpub_uri, strerror(errno));

			// cause a writable callback on all log sockets:
			const struct libwebsocket_protocols *log =
			    &protocols[PROTOCOL_WSLOG];
			libwebsocket_callback_on_writable_all_protocol(log);
		    }
		} else {
		    syslog(LOG_ERR, "container serialization failed");
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
	ret = poll(pfd, 1, msg_poll);
	if (ret < 0) {
	    syslog_async(LOG_ERR, "msgd:%d: poll(): %s - shutting down\n",
		   rtapi_instance, strerror(errno));
	    msgd_exit++;
	} else if (pfd[0].revents & POLLIN) { // signal received
	    struct signalfd_siginfo info;
	    size_t bytes = read(sigfd, &info, sizeof(info));
	    assert(bytes == sizeof(info));
	    signal_handler(info.ssi_signo);
	}

	msg_poll += msg_poll_inc;
	if (msg_poll > msg_poll_max)
	    msg_poll = msg_poll_max;

    } while (!msgd_exit);

    return 0;
}


static struct option long_options[] = {
    {"help",  no_argument,          0, 'h'},
    {"stderr",  no_argument,        0, 's'},
    {"foreground",  no_argument,    0, 'F'},
    {"usrmsglevel", required_argument, 0, 'u'},
    {"rtmsglevel", required_argument, 0, 'r'},
    {"instance", required_argument, 0, 'I'},
    {"instance_name", required_argument, 0, 'i'},
    {"flavor",   required_argument, 0, 'f'},
    {"halsize",  required_argument, 0, 'H'},
    {"halstacksize",  required_argument, 0, 'R'},
    {"shmdrv",  no_argument,        0, 'S'},
    { "uri", required_argument,	 NULL, 'U' },
    { "wwwdir", required_argument,      NULL, 'w' },
    { "port",	required_argument,	NULL, 'p' },
    { "wsdebug",required_argument,	NULL, 'W' },
    {0, 0, 0, 0}
};

int main(int argc, char **argv)
{
    int c, i, retval;
    int option = LOG_NDELAY;
    pid_t pid, sid;
    size_t argv0_len, procname_len, max_procname_len;

    struct lws_context_creation_info ws_info;

    memset(&ws_info, 0, sizeof ws_info);
    ws_info.port = 7681;


    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    progname = argv[0];
    shm_common_init();

    while (1) {
	int option_index = 0;
	int curind = optind;
	c = getopt_long (argc, argv, "hI:sFf:i:SU:w:p:W",
			 long_options, &option_index);
	if (c == -1)
	    break;
	switch (c)	{
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
	    ws_config.www_dir = strdup(optarg);
	    break;
	case 'p':
	    ws_info.port = atoi(optarg);
	    break;
	case 'W':
	    ws_config.debug_level = atoi(optarg);
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
	fprintf(stderr, "%s: FATAL - %s requires the shmdrv module loaded\n",
		progname, use_shmdrv ? "--shmdrv" : flavor->name);
	exit(EXIT_FAILURE);
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

   if (logpub_uri) {
	// start the zeromq log publisher socket
	zmq_ctx  = zctx_new();
	logpub = zsocket_new(zmq_ctx, ZMQ_XPUB);
	zsocket_set_xpub_verbose (logpub, 1);  // enable reception
	if (zsocket_bind(logpub, logpub_uri) > 0) {
	    syslog(LOG_INFO,"publishing log messages at %s\n",
		   logpub_uri);

	    lws_set_log_level(ws_config.debug_level, lwsl_emit_syslog);
	    ws_config.z_context = zctx_new();
	    ws_config.uri = logpub_uri;
	    ws_config.loop = loop;
	    ws_info.user = (void *) &ws_config;
	    ws_info.protocols = protocols;
	    ws_info.gid = -1;
	    ws_info.uid = -1;
	    ws_info.options = 0;
	    ws_context = libwebsocket_create_context(&ws_info);
	    if (ws_context == NULL) {
		syslog(LOG_ERR, "libwebsocket init failed");
	    } else
		libwebsocket_initloop(ws_context, loop);

	} else {
	    syslog(LOG_INFO,"zsocket_bind(%s) failed: %s\n",
		   logpub_uri, strerror(errno));
	    logpub = NULL;
	}
    }

    if ((global_data->rtapi_msgd_pid != 0) &&
	kill(global_data->rtapi_msgd_pid, 0) == 0) {
	fprintf(stderr,"%s: another rtapi_msgd is already running (pid %d), exiting\n",
		progname, global_data->rtapi_msgd_pid);
	exit(EXIT_FAILURE);
    } else {
	global_data->rtapi_msgd_pid = getpid();
    }

    // workaround bug: https://github.com/warmcat/libwebsockets/issues/10
#if 0
    close(STDIN_FILENO);
#endif
    close(STDOUT_FILENO);
    if (!log_stderr)
	close(STDERR_FILENO);

    message_thread();

    // signal received - check if rtapi_app running, and shut it down

    cleanup_actions();
    closelog_async();
    exit(exit_code);
}

const char *mimetype(const char *mimetypes, const char *ext);


// minimal HTTP server, intended to serve some files just to be able
// to display a websockets-based JavaScript UI
// serves file under www_dir - no serving of files with '..' in path
static int callback_http(struct libwebsocket_context *context,
			 struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *user,
			 void *in, size_t len)
{
    char client_name[128];
    char client_ip[128];
    char buf[PATH_MAX];
    wsconfig_t *cfg = (wsconfig_t *) libwebsocket_context_user (context);
    const char *ext, *mt, *s = NULL;
    struct stat sb;

    switch (reason) {
    case LWS_CALLBACK_HTTP:
	if (cfg->www_dir == NULL) {
	    syslog(LOG_ERR, "closing HTTP connection - wwwdir not configured");
	    return -1;
	}
	if (strstr((const char *)in, "..")) {
	    syslog(LOG_ERR,
		   "closing HTTP connection: not serving files with '..': '%s'",
		   (char *) in);
	    return -1;
	}
	ext = strchr((const char *)in, '.');
	if (ext)
	    s = mimetype(mime_types, ++ext);
	mt = (s == NULL) ? "text/hmtl" : s;

	sprintf(buf, "%s/%s", cfg->www_dir, (char *)in);
	if (!((stat(buf, &sb) == 0)  && S_ISREG(sb.st_mode))) {
	    syslog(LOG_ERR, "HTTP: file not found : %s", buf);
	    return -1;
	}
	syslog(LOG_DEBUG, "HTTP serving '%s' mime type='%s'", buf, mt);

	if (libwebsockets_serve_http_file(context, wsi, buf, mt)) {
	    if (s) free((void *) s);
	    return -1; /* through completion or error, close the socket */
	}
	if (s) free((void *) s);
	break;

    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
	libwebsockets_get_peer_addresses(context, wsi, (int)in, client_name,
					 sizeof(client_name), client_ip, sizeof(client_ip));
	syslog(LOG_DEBUG,"HTTP connect from %s (%s)\n",  client_name, client_ip);
	break;

    default:
	break;
    }
    return 0;
}

static int
callback_wslog(struct libwebsocket_context *context,
	       struct libwebsocket *wsi,
	       enum libwebsocket_callback_reasons reason,
	       void *user, void *in, size_t len)
{
    const struct libwebsocket_protocols *proto;
    char client_name[128];
    char client_ip[128];
    unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 +
		      LWS_SEND_BUFFER_POST_PADDING];
    unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
    int n, m, rc;
    wslog_sess_t *wss = (wslog_sess_t *)user;
    wsconfig_t *cfg = (wsconfig_t *) libwebsocket_context_user (context);
    zmsg_t *msg;
    zframe_t *channel,*logframe;
    std::string json;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED:
	proto = libwebsockets_get_protocol(wsi);
	libwebsockets_get_peer_addresses(context, wsi,
					 libwebsocket_get_socket_fd(wsi),
					 client_name,
					 sizeof(client_name),
					 client_ip,
					 sizeof(client_ip));

	// make the ws instance an internal subscriber
	wss->socket = zsocket_new (cfg->z_context, ZMQ_XSUB);
	assert(wss->socket);
	zsocket_set_linger (wss->socket, 0);

	rc = zsocket_connect (wss->socket, (char *) cfg->uri);
	assert(rc == 0);

	// subscribe XPUP-style by sending a message
	// we're interested in the json updates only:
	zstr_send (wss->socket, "\001json");

	cfg->num_log_connections++;

	syslog(LOG_DEBUG,"Websocket/%s upgrade from %s (%s), %d subscriber(s)",
	       proto->name, client_name, client_ip, cfg->num_log_connections);

	break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
	while (1) {
	    // handle all pending messages
	    zmq_pollitem_t items[] = { {wss->socket, 0, ZMQ_POLLIN, 0} };

	    int rc = zmq_poll(items, 1, 10 * ZMQ_POLL_MSEC );
	    if (rc == -1)
		break;
	    if (items [0].revents & ZMQ_POLLIN) {
		msg = zmsg_recv(wss->socket);
		channel = zmsg_pop(msg);
		logframe = zmsg_pop(msg);

		n = zframe_size(logframe);
		memcpy(p, zframe_data(logframe), n);
		zframe_destroy (&logframe);
		zframe_destroy (&channel);

		m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
		if (m < n)
		    syslog(LOG_ERR, "ERROR %d writing to log websocket", n);
	    } else
		break;
	}
	break;

    case LWS_CALLBACK_CLOSED:
	proto = libwebsockets_get_protocol(wsi);
	libwebsockets_get_peer_addresses(context, wsi,
					 libwebsocket_get_socket_fd(wsi),
					 client_name,
					 sizeof(client_name),
					 client_ip,
					 sizeof(client_ip));

	zsocket_destroy(cfg->z_context, wss->socket);
	cfg->num_log_connections--;
	syslog(LOG_ERR,"Websocket/%s disconnect: %s (%s), %d subscriber(s)",
	       proto->name, client_name, client_ip, cfg->num_log_connections);
	break;

    case LWS_CALLBACK_RECEIVE:
	syslog(LOG_DEBUG, "writing to a log socket has no effect: '%.*s'",
	       len, (char *)in);
	// TODO: read & parse JSON into a pb::Container
	// container is either a subscribe message or some other command
	// like set log level
	// WS side just sends container-as-json
	break;

    default:
	break;
    }
    return 0;
}

const char *
mimetype(const char *mimetypes, const char *ext)
{
    FILE *fp;
    char *exts;
    char buf[PATH_MAX];
    char *mt;

    if ((fp = fopen(mimetypes, "r")) == NULL)
	return 0;

    while ((fgets(buf, sizeof(buf), fp)) != NULL) {
	if (buf[0] == '#' || buf[0] == '\n')
	    continue;

	if ((mt = strtok(buf, " \t\n")) != NULL) {
	    while ((exts = strtok(NULL, " \t\n")) != NULL) {
		if (strcasecmp(ext, exts) == 0) {
		    fclose(fp);
		    return strdup(mt); // result must be free()'d
		}
	    }
	}
    }
    fclose(fp);
    return NULL;
}
