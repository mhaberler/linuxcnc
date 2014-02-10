#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>

#include "lws_config.h"
/* #include <libwebsockets.h> */
/* #include <private-libwebsockets.h> */

/* #include <zwsproxy.h> */
/* #include <zwsproxy-private.h> */

#include <zwsproxy.c> // a wart, but I dont get cmake
static    int debug = 0;

static void
echo_thread(void *args, zctx_t *ctx, void *pipe)
{

    void *rs = zsocket_new (ctx, ZMQ_ROUTER);
    assert(rs);
    zsocket_bind(rs, "inproc://echo");

    while (1) {
	zmsg_t *rx = zmsg_recv(rs);
	if (!rx) {
	    perror("rx");
	    break;
	}
	if (debug)
	    zmsg_dump(rx);
	zmsg_send(&rx, rs);
    }
}

// example per-session userdata
struct relay_userdata {
    int hwm;
};

// example user-defined relay policy handler
static int
user_policy(zwsproxy_t *self,
	    zws_session_t *wss,
	    zwscb_type type)
{
    lwsl_debug("%s op=%d\n",__func__,  type);
    zmsg_t *m;
    zframe_t *f;
    struct relay_userdata *ud;

    switch (type) {

    case ZWS_CONNECTING:
	// example per-session data allocation and usage
	wss->user_data = zmalloc (sizeof (struct relay_userdata));
	assert(wss->user_data);
	ud =  (struct relay_userdata *)wss->user_data;
	ud->hwm = -1;

	// implement a new option
	UriQueryListA *q = wss->queryList;
	while (q != NULL) {
	    if (!strcmp(q->key,"hwm")) {
		ud->hwm = atoi(q->value);
		lwsl_debug("%s: hwm=%d\n", __func__, ud->hwm);
	    }
	    q = q->next;
	}

	// > 1 indicates: run the default policy ZWS_CONNECTING code too
	return 1;
	break;

    case ZWS_ESTABLISHED:
	ud =  (struct relay_userdata *)wss->user_data;
	// use the option extracted during ZWS_CONNECTING
	if (ud->hwm > 0)
	    zsocket_set_sndhwm (wss->socket, ud->hwm);

	// example action on connect: send a frame to the websocket
	zstr_send (wss->wsq_out, "to websocket: CONNECTED");
	// send a frame to the zmq
	zstr_send (wss->socket, "to zmq: CONNECTED");
	break;

    case ZWS_CLOSE:
	// any cleanup action
	free(wss->user_data);
	break;

    case ZWS_FROM_WS:
	// a frame was received from the websocket client
	f = zframe_new (wss->buffer, wss->length);
	zframe_print (f, "user_policy FROM_WS:");
	return zframe_send(&f, wss->socket, 0);

    case ZWS_TO_WS:
	// a zmq frame was received, decide how to send to websocket client
	m = zmsg_recv(wss->socket);
	while ((f = zmsg_pop (m)) != NULL) {
	    zframe_print (f, "user_policy TO_WS:");
	    assert(zframe_send(&f, wss->wsq_out, 0) == 0);
	}
	zmsg_destroy(&m);
	break;

    default:
	break;
    }
    return 0;
}

static struct option options[] = {
    { "extensions",	no_argument,	NULL, 'e' },
    { "debug",	required_argument,	NULL, 'd' },
    { "ssl",	no_argument,		NULL, 's' },
    { "port",	required_argument,	NULL, 'p' },
    { "interface",  required_argument,	NULL, 'i' },
    { "wwwdir",	required_argument,	NULL, 'w' },
    { "certpath",  required_argument,	NULL, 'C' },
    { "keypath",  required_argument,	NULL, 'K' },
    { "help",  no_argument,	NULL, 'h' },
    { NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
    int n = 0;
    int use_ssl = 0;
    int debug = 0;
    char interface_name[128] = "";
    const char *www_dir = NULL;
    const char *cert_path = NULL;
    const char *key_path = NULL;
    void *zctx = zctx_new();
    struct lws_context_creation_info info = {0};

    info.port = 7681;
    info.gid = -1;
    info.uid = -1;

    while (n >= 0) {
	n = getopt_long(argc, argv, "ed:sp:i:w:C:K:hu", options, NULL);
	if (n < 0)
	    continue;
	switch (n) {
	case 'e':
	    info.extensions = libwebsocket_get_internal_extensions();
	    break;
	case 'd':
	    debug = atoi(optarg);
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
	    info.iface = interface_name;
	    break;
	case 'w':
	    www_dir = optarg;
	    printf("serving html from \"%s\"\n", www_dir);
	    break;
	case 'C':
	    cert_path = optarg;
	    break;
	case 'K':
	    key_path = optarg;
	    break;
	case 'h':
	    fprintf(stderr, "Usage: zwsproxy <options>\noptions are:\n"
		    "\t[--port=<p>]\tport number to bind to\n"
		    "\t[--interface=<interface to bind to>]\n"
		    "\t[--extensions]\tenable built-in extensions\n"
		    "\t[--ssl --certpath= <server certificate> --keypath=<server key>]\t enable SSL\n"
		    "\t[--wwwdir=<directory to serve files from>]\n"
		    "\t[--debug=<log bitfield>]\n"
		    "\n");
	    exit(1);
	}
    }
    if (use_ssl) {
	if ((cert_path == NULL) || (key_path == NULL)) {
	    fprintf(stderr, "need both  --certpath= <server certificate> --keypath=<server key> for SSL\n");
	    exit(1);
	}
	info.ssl_cert_filepath = cert_path;
	info.ssl_private_key_filepath = key_path;
    }

    // 'zeroMQ ping': an internal echo server, reachable as inproc://echo
    // type = ROUTER, so connect as type=dealer
    zthread_fork (zctx, echo_thread, NULL);

    zwsproxy_t *zws = zwsproxy_new(zctx, www_dir, &info);

    zwsproxy_set_log(debug, NULL);

    // add a user-defined relay policy
    zwsproxy_add_policy(zws, "user", user_policy);

    // start serving
    zwsproxy_start(zws);

    sleep(2000);

    // exit server thread
    zwsproxy_exit(&zws);
    exit(0);
}
