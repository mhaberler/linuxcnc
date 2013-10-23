#include "lws_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <czmq.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include "../lib/libwebsockets.h"

const char *mimetype(const char *mimetypes, const char *ext);
const char *mime_types = "/etc/mime.types";

int force_exit = 0;

typedef struct {
    char *resource_path;
    char *www_dir;
    char *proto_name;
    zctx_t *zmq_ctx;
    struct ev_loop* loop;
    zmsg_t *txmsg;
    int debug_level;
} config_t;

enum demo_protocols {
    PROTOCOL_HTTP = 0,
    PROTOCOL_ZMQ  = 1,
    PROTOCOL_LOG  = 2
};


// minimal HTTP server
// serves file from resource_path
// security left as exercise for the reader
static int callback_http(struct libwebsocket_context *context,
			 struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *user,
			 void *in, size_t len)
{
    char client_name[128];
    char client_ip[128];
    char buf[PATH_MAX];

    config_t *cfg =  libwebsocket_context_user (context);
    const char *ext, *mt = NULL;

    switch (reason) {

    case LWS_CALLBACK_HTTP:
	if (cfg->www_dir == NULL)
	    return -1; // not configured
	ext = strchr((const char *)in, '.');
	if (ext)
	    mt = mimetype(mime_types, ++ext);
	if (mt == NULL)
	    mt = "text/hmtl";

	sprintf(buf, "%s/%s", cfg->www_dir, (char *)in);
	lwsl_info("serving '%s' mime type='%s'\n", buf, mt);

	if (libwebsockets_serve_http_file(context, wsi, buf, mt))
	    return -1; /* through completion or error, close the socket */
	break;

    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
	libwebsockets_get_peer_addresses(context, wsi, (int)(long)in, client_name,
					 sizeof(client_name), client_ip, sizeof(client_ip));
	lwsl_info("HTTP connect from %s (%s)\n",  client_name, client_ip);
	break;

    default:
	break;
    }

    return 0;
}

struct per_session_data__zmq {
    void *socket;
    int number;
    int socket_type;
    struct ev_io watcher;
    struct libwebsocket *wsi;
    struct libwebsocket_context *context;
    int proto;
};

void zmq_watcher_cb(struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    int n, m;
    unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 +
		      LWS_SEND_BUFFER_POST_PADDING];
    unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
    int events;
    struct per_session_data__zmq *pss = (struct per_session_data__zmq *) watcher->data;

    while (1) {
	events = zsocket_events(pss->socket);
        if (events  < 0)    {
	    fprintf(stderr, "ZMQ_EVENTS failed: %s\n", strerror(errno));
	    return;
	}
	if (events == 0)
	    return;

        if (events & ZMQ_POLLIN)  {
	    zmsg_t *msg = zmsg_recv (pss->socket);
	    zframe_t *frame;
	    config_t *cfg =  libwebsocket_context_user (pss->context);
	    if (cfg->debug_level) {
		fprintf(stderr, "POLLIN RX proto=%d: ", pss->proto);
		zmsg_dump (msg);
	    }

	    while ((frame = zmsg_pop(msg)) != NULL) {
		n = zframe_size(frame);
		memcpy(p, zframe_data(frame), n);
		m = libwebsocket_write(pss->wsi, p, n, LWS_WRITE_TEXT);
		if (m < n) {
		    lwsl_err("ERROR %d writing to di socket\n", n);
		}
		zframe_destroy (&frame);
	    }
	} else
	    return;
	// ignore ZMQ_POLLOUT - it's always writable
    }
}

static int
callback_zmq(struct libwebsocket_context *context,
			struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
			void *user, void *in, size_t len)
{
    int  rc, zmq_fd;
    struct per_session_data__zmq *pss = (struct per_session_data__zmq *)user;
    config_t *cfg =  libwebsocket_context_user (context);
    const struct libwebsocket_protocols *proto;
    zmsg_t *msg ;

    switch (reason) {

    case LWS_CALLBACK_ESTABLISHED:
	proto = libwebsockets_get_protocol(wsi);
	lwsl_info("callback_zmq: LWS_CALLBACK_ESTABLISHED proto=%s index=%d\n",
		  proto->name, proto->protocol_index);

	pss->context = context;
	pss->wsi = wsi;
	pss->proto = proto->protocol_index;

	switch (proto->protocol_index) {
	case  PROTOCOL_ZMQ:

	    pss->socket = zsocket_new (cfg->zmq_ctx, ZMQ_REQ);
	    assert(pss->socket);
	    zsocket_set_linger (pss->socket, 0);

	    rc = zsocket_connect (pss->socket, "tcp://127.0.0.1:54327");
	    assert(rc == 0);

	case PROTOCOL_LOG:
	    pss->socket = zsocket_new (cfg->zmq_ctx, ZMQ_SUB);
	    assert(pss->socket);
	    zsocket_set_linger (pss->socket, 0);

	    zsocket_set_subscribe (pss->socket,"");
	    rc = zsocket_connect (pss->socket, "tcp://127.0.0.1:54328");
	    assert(rc == 0);
	    break;
	}
	zmq_fd =  zsocket_fd (pss->socket);
	assert(zmq_fd > 0);

	pss->watcher.data = (void *) pss;
	ev_io_init(&pss->watcher, zmq_watcher_cb, zmq_fd, EV_READ);
	ev_io_start(cfg->loop, &pss->watcher);
	break;

    case LWS_CALLBACK_CLOSED:
	lwsl_info("callback_zmq: LWS_CALLBACK_CLOSED\n");
	ev_io_stop (cfg->loop, &pss->watcher);
	zsocket_destroy (cfg->zmq_ctx, pss->socket);
	break;

    case LWS_CALLBACK_RECEIVE:
	lwsl_info("callback_zmq: LWS_CALLBACK_RECEIVE '%.*s'\n", len, in);
	if (libwebsockets_remaining_packet_payload (wsi) > 0) {
	    lwsl_info("------- receive buffer too small, increase!\n");
	}
	switch (pss->proto) {
	case PROTOCOL_ZMQ:
	    msg = zmsg_new ();
	    zmsg_pushmem (msg, in, len);
	    if (cfg->debug_level)
		zmsg_dump (msg);
	    rc = zmsg_send (&msg, pss->socket);
	    assert (msg == NULL);
	    assert (rc == 0);

	    // essential: kick the zmq watcher
	    ev_feed_event(cfg->loop,  &pss->watcher, EV_READ);

	    break;
	case PROTOCOL_LOG:
	    lwsl_info("receive is a noop for log\n");
	    break;
	}
	break;

    default:
	break;
    }

    return 0;
}



/* list of supported protocols and callbacks */

static struct libwebsocket_protocols protocols[] = {
    /* first protocol must always be HTTP handler */

    {
	"http-only",		/* name */
	callback_http,		/* callback */
	0,
	0,			/* max frame size / rx buffer */
    },
    {
	"zmq-protocol",
	callback_zmq,
	sizeof(struct per_session_data__zmq),
	1024,
    },
    {
	"log",
	callback_zmq,
	sizeof(struct per_session_data__zmq),
	1024,
    },
    { NULL, NULL, 0, 0 } /* terminator */
};

static struct option options[] = {
    { "help",	no_argument,		NULL, 'h' },
    { "extensions",	no_argument,	NULL, 'e' },
    { "debug",	required_argument,	NULL, 'd' },
    { "port",	required_argument,	NULL, 'p' },
    { "ssl",	no_argument,		NULL, 's' },
    { "interface",  required_argument,	NULL, 'i' },
    { "closetest",  no_argument,	NULL, 'c' },
    { "resource_path", required_argument,NULL, 'r' },
    { "wwwdir", required_argument,      NULL, 'w' },
    { NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
    char cert_path[1024];
    char key_path[1024];
    int n = 0;
    int use_ssl = 0;
    struct libwebsocket_context *context;
    int opts = 0;
    char interface_name[128] = "";
    const char *iface = NULL;
    int syslog_options = LOG_PID | LOG_PERROR;
    struct lws_context_creation_info info;
    config_t conf;

    conf.resource_path = ".";
    conf.www_dir = "";
    conf.proto_name = "";
    conf.debug_level = 7;

    memset(&info, 0, sizeof info);
    info.port = 7681;

    while (n >= 0) {
	n = getopt_long(argc, argv, "i:hsp:d:r:ew:", options, NULL);
	if (n < 0)
	    continue;
	switch (n) {
	case 'e':
	    info.extensions = libwebsocket_get_internal_extensions();
	    break;
	case 'd':
	    conf.debug_level = atoi(optarg);
	    break;
	case 's':
	    use_ssl = 1;
	    break;
	case 'p':
	    info.port = atoi(optarg);
	    break;
	case 'i':
	    strncpy(interface_name, optarg, sizeof interface_name);
	    interface_name[(sizeof interface_name) - 1] = '\0';
	    iface = interface_name;
	    break;
	case 'r':
	    conf.resource_path = optarg;
	    printf("Setting resource path to \"%s\"\n", conf.resource_path);
	    break;
	case 'w':
	    conf.www_dir = optarg;
	    printf("Setting www dir to \"%s\"\n", conf.www_dir);
	    break;
	case 'h':
	    fprintf(stderr, "Usage: test-server "
		    "[--port=<p>] [--ssl] "
		    "[-d <log bitfield>] "
		    "[--resource_path <path>]\n");
	    exit(1);
	}
    }

    setlogmask(LOG_UPTO (LOG_DEBUG));
    openlog("lwsts", syslog_options, LOG_DAEMON);
    lws_set_log_level(conf.debug_level, lwsl_emit_syslog);

    conf.zmq_ctx = zctx_new();
    conf.loop = ev_loop_new(0);

    info.user = (void *) &conf;
    info.iface = iface;
    info.protocols = protocols;

    if (!use_ssl) {
	info.ssl_cert_filepath = NULL;
	info.ssl_private_key_filepath = NULL;
    } else {
	if (strlen(conf.resource_path) > sizeof(cert_path) - 32) {
	    lwsl_err("resource path too long\n");
	    return -1;
	}
	sprintf(cert_path, "%s/libwebsockets-test-server.pem",
		conf.resource_path);
	if (strlen(conf.resource_path) > sizeof(key_path) - 32) {
	    lwsl_err("resource path too long\n");
	    return -1;
	}
	sprintf(key_path, "%s/libwebsockets-test-server.key.pem",
		conf.resource_path);

	info.ssl_cert_filepath = cert_path;
	info.ssl_private_key_filepath = key_path;
    }
    info.gid = -1;
    info.uid = -1;
    info.options = opts;

    context = libwebsocket_create_context(&info);
    if (context == NULL) {
	lwsl_err("libwebsocket init failed\n");
	return -1;
    }

    libwebsocket_initloop(context, conf.loop);

    // main event loop
    ev_run(conf.loop, 0);

    libwebsocket_context_destroy(context);
    zctx_destroy (&conf.zmq_ctx);

    lwsl_notice("libwebsockets-test-zmq exited cleanly\n");
    closelog();
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
	return FALSE;

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
