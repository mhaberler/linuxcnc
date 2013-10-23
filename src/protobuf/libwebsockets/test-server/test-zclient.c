#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>

#ifdef CMAKE_BUILD
#include "lws_config.h"
#endif

#include "../lib/libwebsockets.h"

static int was_closed;
static int deny_deflate;
static int deny_mux;
static int force_exit = 0;

enum demo_protocols {

    PROTOCOL_DUMB_INCREMENT,
};


/* dumb_increment protocol */

static int
callback_dumb_increment(struct libwebsocket_context *this,
			struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
			void *user, void *in, size_t len)
{
    int m,n;
    unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 +
		      LWS_SEND_BUFFER_POST_PADDING];
    unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];


    switch (reason) {

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
	lwsl_info("callback_zmq: "
		  "LWS_CALLBACK_ESTABLISHED\n");
	n = sprintf((char *)p, "from client");
	m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
	if (m < n) {
	    lwsl_err("ERROR %d writing to di socket\n", n);
	    return -1;
	}

	break;

    case LWS_CALLBACK_CLOSED:
	fprintf(stderr, "LWS_CALLBACK_CLOSED\n");
	was_closed = 1;
	break;

    case LWS_CALLBACK_CLIENT_RECEIVE:
	((char *)in)[len] = '\0';
	fprintf(stderr, "rx %d '%s'\n", (int)len, (char *)in);
	n = sprintf((char *)p, "bounce from client '%.*s'", (int)len, (char *)in);
	m = libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
	if (m < n) {
	    lwsl_err("ERROR %d writing to di socket\n", n);
	    return -1;
	}
	break;

    case LWS_CALLBACK_CLIENT_WRITEABLE:
	fprintf(stderr, "LWS_CALLBACK_CLIENT_WRITEABLE\n");

	break;

	/* because we are protocols[0] ... */

    case LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED:
	if ((strcmp(in, "deflate-stream") == 0) && deny_deflate) {
	    fprintf(stderr, "denied deflate-stream extension\n");
	    return 1;
	}
	if ((strcmp(in, "deflate-frame") == 0) && deny_deflate) {
	    fprintf(stderr, "denied deflate-frame extension\n");
	    return 1;
	}
	if ((strcmp(in, "x-google-mux") == 0) && deny_mux) {
	    fprintf(stderr, "denied x-google-mux extension\n");
	    return 1;
	}

	break;

    default:
	lwsl_info("callback_zmq: reason=%d\n", reason);
	break;
    }

    return 0;
}



/* list of supported protocols and callbacks */

static struct libwebsocket_protocols protocols[] = {
    {
	"zmq-protocol",
	callback_dumb_increment,
	0,
	20,
    },

    { NULL, NULL, 0, 0 } /* end */
};

void sighandler(int sig)
{
    force_exit = 1;
}

static struct option options[] = {
    { "help",	no_argument,		NULL, 'h' },
    { "debug",      required_argument,      NULL, 'd' },
    { "port",	required_argument,	NULL, 'p' },
    { "ssl",	no_argument,		NULL, 's' },
    { "version",	required_argument,	NULL, 'v' },
    { "undeflated",	no_argument,		NULL, 'u' },
    { "nomux",	no_argument,		NULL, 'n' },
    { "longlived",	no_argument,		NULL, 'l' },
    { NULL, 0, 0, 0 }
};


int main(int argc, char **argv)
{
    int n = 0;
    int ret = 0;
    int port = 7681;
    int use_ssl = 0;
    struct libwebsocket_context *context;
    const char *address;
    struct libwebsocket *wsi_dumb;
    int ietf_version = -1; /* latest */
    struct lws_context_creation_info info;


    memset(&info, 0, sizeof info);
    if (argc < 2)
	goto usage;

    while (n >= 0) {
	n = getopt_long(argc, argv, "nuv:hsp:d:l", options, NULL);
	if (n < 0)
	    continue;
	switch (n) {
	case 'd':
	    lws_set_log_level(atoi(optarg), NULL);
	    break;
	case 's':
	    use_ssl = 2; /* 2 = allow selfsigned */
	    break;
	case 'p':
	    port = atoi(optarg);
	    break;
	case 'v':
	    ietf_version = atoi(optarg);
	    break;
	case 'u':
	    deny_deflate = 1;
	    break;
	case 'n':
	    deny_mux = 1;
	    break;
	case 'h':
	    goto usage;
	}
    }

    if (optind >= argc)
	goto usage;

    signal(SIGINT, sighandler);

    address = argv[optind];

    /*
     * create the websockets context.  This tracks open connections and
     * knows how to route any traffic and which protocol version to use,
     * and if each connection is client or server side.
     *
     * For this client-only demo, we tell it to not listen on any port.
     */

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
#ifndef LWS_NO_EXTENSIONS
    info.extensions = libwebsocket_get_internal_extensions();
#endif
    info.gid = -1;
    info.uid = -1;

    context = libwebsocket_create_context(&info);
    if (context == NULL) {
	fprintf(stderr, "Creating libwebsocket context failed\n");
	return 1;
    }

    /* create a client websocket using dumb increment protocol */

    wsi_dumb = libwebsocket_client_connect(context, address, port, use_ssl,
					   "/", argv[optind], argv[optind],
					   protocols[PROTOCOL_DUMB_INCREMENT].name, ietf_version);

    if (wsi_dumb == NULL) {
	fprintf(stderr, "libwebsocket dumb connect failed\n");
	ret = 1;
	goto bail;
    }

    fprintf(stderr, "Websocket connections opened\n");

    /*
     * sit there servicing the websocket context to handle incoming
     * packets, and drawing random circles on the mirror protocol websocket
     */

    n = 0;
    while (n >= 0 && !was_closed && !force_exit) {
	n = libwebsocket_service(context, 10);

	if (n < 0)
	    continue;
    }

 bail:
    fprintf(stderr, "Exiting\n");

    libwebsocket_context_destroy(context);

    return ret;

 usage:
    fprintf(stderr, "Usage: libwebsockets-test-client "
	    "<server address> [--port=<p>] "
	    "[--ssl] [-k] [-v <ver>] "
	    "[-d <log bitfield>] [-l]\n");
    return 1;
}
