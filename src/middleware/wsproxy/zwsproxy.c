// embeddable zeroMQ/websockets proxy server
// see zwsmain.c and zwsproxy.h for usage information
//
// the server runs as a separate thread.
// messages to the websocket are funneled through a zmq PAIR pipe
// (similar to the ringbuffer in test-server.c but using zeroMQ means).

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libwebsockets.h>

#include <zwsproxy.h>
#include <zwsproxy-private.h>

#define LWS_SERVICE_TIMER 0 //200    // msec; unclear if needed; works fine so far - set to 0 to disable
#define LWS_INITIAL_TXBUFFER 4096  // transmit buffer grows as needed
#define LWS_TXBUFFER_EXTRA 256   // add to current required size if growing tx buffer
#ifdef LWS_MAX_HEADER_LEN
#define MAX_HEADER_LEN LWS_MAX_HEADER_LEN
#else
#define MAX_HEADER_LEN 1024
#endif

// this callback defines how frames are relayed between websockets and zmq
// it can be replaced by a user-defined function referred to by name in the URI
// a named policy may be added by calling zwsproxy_add_policy()
// see example code in zwsmain.c
static int default_policy(zwsproxy_t *self,
			  zws_session_t *s,
			  zwscb_type type);

static int callback_http(struct libwebsocket_context *context,
			 struct libwebsocket *wsi,
			 enum libwebsocket_callback_reasons reason, void *user,
			 void *in, size_t len);
static void wsproxy_thread(void *args, zctx_t *ctx, void *pipe);
static int libws_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *context);
static int zmq_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *context);
static int wsqin_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *context);
const char *zwsmimetype(const char *ext);

// map poll(2) revents to ZMQ event masks and back
static inline int poll2zmq(int mask)
{
    int result = 0;
    if (mask & POLLIN) result |= ZMQ_POLLIN;
    if (mask & POLLOUT) result |= ZMQ_POLLOUT;
    if (mask & POLLERR) result |= ZMQ_POLLERR;
    return result;
}

static inline int zmq2poll(int mask)
{
    int result = 0;
    if (mask & ZMQ_POLLIN) result |= POLLIN;
    if (mask & ZMQ_POLLOUT) result |= POLLOUT;
    if (mask & ZMQ_POLLERR) result |= POLLERR;
    return result;
}

// -- public API ---

zwsproxy_t *zwsproxy_new(void *ctx, const char *wwwdir, struct lws_context_creation_info *info)
{
    zwsproxy_t *self = (zwsproxy_t *) zmalloc (sizeof (zwsproxy_t));
    assert (self);

    self->ctx = ctx;
    self->www_dir = wwwdir;
    self->loop = zloop_new();
    self->policies = zlist_new();
    zlist_autofree(self->policies);
    self->info = *info;
    self->info.user = self;

    self->info.protocols =  (struct libwebsocket_protocols *)
	zmalloc (sizeof (struct libwebsocket_protocols) * 2); // zero delimited
    assert(self->info.protocols);
    self->info.protocols->name = "http";
    self->info.protocols->callback = callback_http;
    self->info.protocols->per_session_data_size = sizeof(zws_session_t);

    self->cmdpipe = zthread_fork (ctx, wsproxy_thread, self);
    assert (self->cmdpipe);
    char *command = zstr_recv (self->cmdpipe);
    if (command && !strcmp(command, "OK")) {
	zstr_free(&command);
	return self;
    }
    // startup failed
    free(self->info.protocols);
    free(self);
    zstr_free(&command);
    return NULL;
}

void zwsproxy_add_policy(zwsproxy_t *self, const char *name, zwscvt_cb cb)
{
    assert(self);
    zwspolicy_t *policy = (zwspolicy_t *) zmalloc (sizeof (zwspolicy_t));
    assert (policy);
    policy->name = strdup(name);
    policy->callback = cb;
    zlist_append (self->policies, policy);
}

void zwsproxy_set_log(int level, zwslog_cb dest)
{
    lws_set_log_level(level, dest);
}

int zwsproxy_start(zwsproxy_t *self)
{
    assert(self);
    if (self->loop == NULL)
	return -1; // thread already exited
    zstr_send (self->cmdpipe, "START");
    char *reply = zstr_recv (self->cmdpipe);
    int retval = -1;
    if (reply && !strcmp(reply, "STARTED")) {
	zstr_free(&reply);
	retval = 0;
    }
    return retval;
}

void zwsproxy_exit(zwsproxy_t **self)
{
    assert(*self);
    if ((*self)->loop == NULL)
	return; // thread already exited
    zstr_send ((*self)->cmdpipe, "EXIT");
    free((*self)->info.protocols);
    zlist_destroy (&(*self)->policies);
    free(*self);
    *self = NULL;
}

//---- internals ----

// default relay policy:
static int
default_policy(zwsproxy_t *self,
		     zws_session_t *wss,
		     zwscb_type type)
{
    zmsg_t *m;
    zframe_t *f;

    switch (type) {
    case ZWS_CONNECTING:
	{
	    const char *identity = NULL;
	    wss->txmode = LWS_WRITE_BINARY;
	    UriQueryListA *q = wss->queryList;
	    int fd = libwebsocket_get_socket_fd(wss->wsiref);

	    while (q != NULL) {
		lwsl_debug("%s %d: key='%s' value='%s'\n",
			   __func__, fd, q->key,q->value);

		if (!strcmp(q->key,"text")) wss->txmode = LWS_WRITE_TEXT;
		if (!strcmp(q->key,"identity")) identity =  q->value;
		if (!strcmp(q->key,"type")) {
		    if (!strcmp(q->value,"dealer")) wss->socket_type = ZMQ_DEALER;
		    if (!strcmp(q->value,"sub")) wss->socket_type = ZMQ_SUB;
		    if (!strcmp(q->value,"xsub")) wss->socket_type = ZMQ_XSUB;
		    if (!strcmp(q->value,"pub")) wss->socket_type = ZMQ_PUB;
		    if (!strcmp(q->value,"xpub")) wss->socket_type = ZMQ_XPUB;
		    if (!strcmp(q->value,"router")) wss->socket_type = ZMQ_ROUTER;
		}
		q = q->next;
	    }
	    wss->socket = zsocket_new (self->ctx, wss->socket_type);
	    if (wss->socket == NULL) {
		lwsl_err("%s %d: cant create ZMQ socket: %s\n",
			 __func__, fd, strerror(errno));
		return -1;
	    }

	    // bind/connect to all destinations
	    q = wss->queryList;
	    int destcount = 0;
	    while (q != NULL) {
		if (!strcmp(q->key,"connect")) {
		    if (zsocket_connect (wss->socket, q->value)) {
			lwsl_err("%s %d: cant connect to %s: %s\n",
				 __func__, fd, q->value, strerror(errno));
			return -1;
		    } else {
			lwsl_debug("%s %d: connect to %s type %d\n",
				   __func__, fd, q->value, wss->socket_type);
			destcount++;
		    }
		}
		if (!strcmp(q->key,"bind")) {
		    if (zsocket_bind (wss->socket, q->value) < 0) {
			lwsl_err("%s %d: cant bind to %s: %s\n",
				 __func__, fd, q->value, strerror(errno));
			return -1;
		    } else {
			destcount++;
			lwsl_debug("%s %d: bind to %s type %d\n",
				   __func__, fd, q->value, wss->socket_type);
		    }
		}
		q = q->next;
	    }
	    if (destcount == 0) {
		lwsl_err("%s %d: no 'bind' or 'connect' arg given, closing\n",
			 __func__,fd);
		return -1;
	    }

	    if (identity) {
		zsocket_set_identity (wss->socket, identity);
		lwsl_debug("%s %d: set identity to '%s'\n",
			   __func__,fd, identity);
	    }
	    q = wss->queryList;
	    while (q != NULL) {
		if (!strcmp(q->key,"subscribe")) {
		    lwsl_debug( "%s %d: subscribe '%s'\n",
				__func__,fd, q->value);
		    const char *topic = (q->value == NULL) ? "" : q->value;
		    switch (wss->socket_type) {
		    case ZMQ_DEALER:
			lwsl_err("%s %d: subscribe doesnt make sense on DEALER, closing\n",
				 __func__, fd);
			return -1;

		    case ZMQ_SUB:
			zsocket_set_subscribe (wss->socket, topic);
			break;

		    case ZMQ_XSUB:
			{
			    size_t len = strlen(topic) + 1;
			    zframe_t *f = zframe_new (NULL, len);
			    char *s = (char *) zframe_data(f);
			    *s = '\001';
			    strcpy(s + 1, topic);
			    if (zframe_send(&f, wss->socket, 0)) {
				lwsl_err("%s %d: xsub subscribe(%s) msg: \"%s\"\n",
					 __func__, fd, topic, strerror(errno));
				return -1;
			    }
			}
			break;

		    default:
			lwsl_err("%s %d: bad socket type %d, closing\n",
				 __func__,fd, wss->socket_type);
			return -1;
		    }
		}
		q = q->next;
	    }
	}
	break;

    case ZWS_ESTABLISHED:
	break;

    case ZWS_FROM_WS:
	// ws->zmq: just send as standalone frame.

	f = zframe_new (wss->buffer, wss->length);
	// zframe_print (f, "%s FROM_WS:", __func__);
	return zframe_send(&f, wss->socket, 0);
	break;

    case ZWS_TO_WS:
	// zmq->ws: unwrap all frames and send individually by stuffing into wsq_out
	// this might not make sense on subscribe sockets which send the topic frame
	// first
	m = zmsg_recv(wss->socket);
	while ((f = zmsg_pop (m)) != NULL) {
	    wss->zmq_bytes += zframe_size(f);
	    wss->zmq_msgs++;
	    // zframe_print (f, "%s TO_WS:", __func__);
	    zframe_send(&f, wss->wsq_out, 0);
	}
	zmsg_destroy(&m);
	break;

    default:
	break;
    }
    return 0;
}


static int
cmdpipe_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    int retval = 0;

    char *cmd_str = zstr_recv (item->socket);
    assert (cmd_str);
    if (!strcmp(cmd_str,"EXIT")) {
	retval = -1; // exit reactor
    }
    zstr_free(&cmd_str);
    return retval;
}

#if LWS_SERVICE_TIMER > 0
static int
timer_callback(zloop_t *loop, int  timer_id, void *context)
{
    libwebsocket_service ((struct libwebsocket_context *) context, 0);
    return 0;
}
#endif

static void
wsproxy_thread(void *args, zctx_t *ctx, void *pipe)
{
    zwsproxy_t *self = (zwsproxy_t *)args;
    struct libwebsocket_context *wsctx;

    if ((wsctx = libwebsocket_create_context(&self->info)) == NULL) {
	lwsl_err("libwebsocket_create_context failed\n");
	zstr_send (pipe, "INITFAIL");
	return ;
    }

    // watch command pipe for EXIT command
    zmq_pollitem_t cmditem =  { pipe, 0, ZMQ_POLLIN, 0 };
    assert(zloop_poller (self->loop, &cmditem, cmdpipe_readable, self) == 0);
    zstr_send (pipe, "OK");

    // wait for START command
    char *cmd_str = zstr_recv(pipe);
    if (cmd_str && !strcmp(cmd_str, "START")) {
	zstr_free(&cmd_str);
	zstr_send (pipe, "STARTED");
#if LWS_SERVICE_TIMER > 0
	zloop_timer (self->loop, LWS_SERVICE_TIMER, 0, timer_callback, wsctx);
#endif
	zloop_start (self->loop);
	zstr_send (pipe, "EXITED");
    }
    libwebsocket_context_destroy(wsctx);
    zloop_destroy(&self->loop);
    zstr_send (pipe, "OK");
    self->loop = NULL;  // indicate thread exit
    lwsl_info("wsproxy_thread port %d exiting\n", self->info.port);
}

static const UriQueryListA *find_query(zws_session_t *wss, const char *name)
{
    const UriQueryListA *q = wss->queryList;
    while (q != NULL) {
	if (!strcmp(q->key, name))
	    return q;
	q = q->next;
    }
    return NULL;
}

static int set_policy(zws_session_t *wss, zwsproxy_t *cfg)
{
    const UriQueryListA *q;

    if ((q = find_query(wss, "policy")) == NULL) {
	wss->policy = default_policy;
	return 0;
    } else {
	zwspolicy_t *p;
	for (p = (zwspolicy_t *) zlist_first(cfg->policies);
	     p != NULL;
	     p = (zwspolicy_t *) zlist_next(cfg->policies)) {
	    if (!strcmp(q->value, p->name)) {
		lwsl_debug("%s: setting policy '%s'\n",
			   __func__, p->name);
		wss->policy = p->callback;
		break;
	    }
	}
	if (p == NULL) {
	    lwsl_err("%s: no such policy '%s'\n",
		     __func__, q->value);
	    return -1; // close connection
	}
    }
    return 0;
}

// serves files via HTTP from www_dir if not NULL
static int serve_http(struct libwebsocket_context *context,
		      zwsproxy_t *cfg, struct libwebsocket *wsi,
		      void *in, size_t len)
{
    const char *ext, *mt = NULL;
    char buf[PATH_MAX];

    if (cfg->www_dir == NULL) {
	lwsl_err( "closing HTTP connection - www_dir not configured\n");
	return -1;
    }
    snprintf(buf, sizeof(buf), "%s/", cfg->www_dir);
    if (uriUriStringToUnixFilenameA((const char *)in,
				    &buf[strlen(cfg->www_dir)])) {
	lwsl_err("HTTP: cant normalize '%s'\n", (const char *)in);
	return -1;
    }
    if (strstr((const char *)buf + strlen(cfg->www_dir), "..")) {
	lwsl_err("closing HTTP connection: not serving files with '..': '%s'\n",
		 (char *) in);
	return -1;
    }
    ext = strrchr((const char *)in, '.');
    if (ext)
	mt = zwsmimetype(ext);
    if (mt == NULL)
	mt = "text/html";

    if (access(buf, R_OK)) {
	lwsl_err("HTTP: 404 on '%s'\n",buf);
	char m404[PATH_MAX];
	size_t len = snprintf(m404, sizeof(m404),
			      "HTTP/1.0 404 Not Found\r\n"
			      "Content-type: text/html\r\n\r\n"
			      "<html><head><title>404 Not Found</title></head>"
			      "<body><h1>Not Found</h1>"
			      "<p>The requested URL %s "
			      "was not found on this server.</p>"
			      "<address>zwsproxy Port %d</address>"
			      "</body></html>",
			      (char *)in, cfg->info.port);
	libwebsocket_write (wsi, (unsigned char *)m404, len, LWS_WRITE_HTTP);
	return -1;
    }
    lwsl_debug("serving '%s' mime type='%s'\n", buf, mt);
    if (libwebsockets_serve_http_file(context, wsi, buf, mt, NULL))
	return -1;
    return 0;
}


// HTTP + Websockets server
static int
callback_http(struct libwebsocket_context *context,
	      struct libwebsocket *wsi,
	      enum libwebsocket_callback_reasons reason, void *user,
	      void *in, size_t len)
{


    zws_session_t *wss = (zws_session_t *)user;
    zwsproxy_t *cfg = libwebsocket_context_user(context);
    struct libwebsocket_pollargs *pa = (struct libwebsocket_pollargs *)in;

    switch (reason) {

	// any type of connection
    case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
	{
	    char client_name[128];
	    char client_ip[128];
	    libwebsockets_get_peer_addresses(context, wsi, (int)(long)in, client_name,
					     sizeof(client_name),
					     client_ip,
					     sizeof(client_ip));
	    lwsl_info("zwsproxy connect from %s (%s) port %d\n",
		      client_name, client_ip, cfg->info.port);
	    // access control: insert any filter here and return -1 to close connection
	}
	break;

	// basic http serving
    case LWS_CALLBACK_HTTP:
	return serve_http(context, cfg, wsi, in, len);
	break;

	// websockets support
    case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
	{
	    // parse URI and query
	    // set policy if contained in query
	    // call ZWS_CONNECTING callback

	    UriParserStateA state;
	    int itemCount;
	    const UriQueryListA *q;
	    char geturi[MAX_HEADER_LEN];
	    char uriargs[MAX_HEADER_LEN];
	    int retval = 0;
	    int fd = libwebsocket_get_socket_fd(wsi);

	    wss->txbuffer = zmalloc(LWS_INITIAL_TXBUFFER);
	    assert(wss->txbuffer);
	    wss->txbufsize = LWS_INITIAL_TXBUFFER;

	    // extract and parse uri and args
	    lws_hdr_copy(wsi, geturi, sizeof geturi, WSI_TOKEN_GET_URI);
	    int arglen = lws_hdr_copy(wsi, uriargs, sizeof uriargs, WSI_TOKEN_HTTP_URI_ARGS);

	    lwsl_debug("%s %d: uri='%s' args='%s'\n", __func__, fd, geturi, uriargs);

	    state.uri = &wss->u;
	    if (uriParseUriA(&state, geturi) != URI_SUCCESS) {
		lwsl_err("Websocket %d: cant parse URI: '%s' near '%s' rc=%d\n",
			 fd, geturi, state.errorPos, state.errorCode);
		return -1;
	    }
	    if ((retval = uriDissectQueryMallocA(&wss->queryList, &itemCount, uriargs,
						 uriargs + arglen)) != URI_SUCCESS) {
		lwsl_err("Websocket %d: cant dissect query: '%s' rc=%d\n",
			 fd, geturi, retval);
		return -1;
	    }
	    if (set_policy(wss, cfg))
		return -1; // invalid policy - close connection

	    if ((q = find_query(wss, "debug")) != NULL)
		lws_set_log_level(atoi(q->value), NULL);

	    wss->wsiref = wsi;
	    wss->ctxref = context;

	    retval = wss->policy(cfg, wss, ZWS_CONNECTING);
	    if (retval > 0) // user policy indicated to use default policy function
		return default_policy(cfg, wss, ZWS_CONNECTING);
	    else
		return retval;
	}
	break;

    case LWS_CALLBACK_ESTABLISHED:
	{
	    int retval;

	    // the two/from WS pair pipe
	    wss->wsq_out = zsocket_new (cfg->ctx, ZMQ_PAIR);
	    assert (wss->wsq_out);
	    zsocket_bind (wss->wsq_out, "inproc://wsq-%p", wss);

	    wss->wsq_in = zsocket_new (cfg->ctx, ZMQ_PAIR);
	    assert (wss->wsq_in);
	    zsocket_connect (wss->wsq_in, "inproc://wsq-%p", wss);

	    // start watching the to-websocket pipe
	    wss->wsqin_pollitem.socket =  wss->wsq_in;
	    wss->wsqin_pollitem.fd = 0;
	    wss->wsqin_pollitem.events =  ZMQ_POLLIN;
	    assert(zloop_poller (cfg->loop, &wss->wsqin_pollitem,
				 wsqin_socket_readable, wss) == 0);

	    retval = wss->policy(cfg, wss, ZWS_ESTABLISHED);
	    if (retval > 0) // user policy indicated to use default policy function
		default_policy(cfg, wss, ZWS_ESTABLISHED);

	    // start watching the zmq socket
	    wss->pollitem.socket =  wss->socket;
	    wss->pollitem.fd = 0;
	    wss->pollitem.events =  ZMQ_POLLIN;
	    assert(zloop_poller (cfg->loop, &wss->pollitem, zmq_socket_readable, wss) == 0);
	}
	break;

    case LWS_CALLBACK_SERVER_WRITEABLE:
	{
	    size_t m,n;
	    zframe_t *f;

	    // complete any pending partial writes first
	    if (wss->current != NULL) {
		n = zframe_size(wss->current) - wss->already_sent;
		memcpy(&wss->txbuffer[LWS_SEND_BUFFER_PRE_PADDING],
		       zframe_data(wss->current) + wss->already_sent, n);

		lwsl_debug("write leftover %d/%d\n", n,  zframe_size(wss->current));
		m = libwebsocket_write(wsi, &wss->txbuffer[LWS_SEND_BUFFER_PRE_PADDING],
				       n,  wss->txmode);
		wss->wsout_bytes += m;
		if (m < n) {
		    lwsl_debug("stuck again writing leftover %d/%d\n", m,n);
		    wss->already_sent += m;
		    wss->partial_retry++;

		    // disable wsq_in poller while stuck - write cb will take care of this
		    // otherwise this is hammered by zmq readable callbacks because
		    // of pending frames in the wsq_in pipe
		    zloop_poller_end (cfg->loop, &wss->wsqin_pollitem);

		    // reschedule once writable
		    libwebsocket_callback_on_writable(context, wsi);
		    return 0;
		} else {
		    lwsl_debug("leftover completed %d", n);
		    zframe_destroy (&wss->current);
		    wss->already_sent = 0;
		    wss->completed++;

		    // ready to take more - reenable wsq_in poller
		    assert(zloop_poller (cfg->loop, &wss->wsqin_pollitem,
					 wsqin_socket_readable, wss) == 0);
		}
	    }

	    while ((f = zframe_recv_nowait (wss->wsq_in)) != NULL) {
		n = zframe_size(f);
		wss->wsout_msgs++;
		size_t needed  = n + LWS_SEND_BUFFER_PRE_PADDING +
		    LWS_SEND_BUFFER_POST_PADDING;
		if (needed > wss->txbufsize) {
		    // enlarge as needed
		    needed += LWS_TXBUFFER_EXTRA;
		    lwsl_info("Websocket %d: enlarge txbuf %d to %d\n",
			     libwebsocket_get_socket_fd(wsi), wss->txbufsize, needed);
		    free(wss->txbuffer);
		    wss->txbuffer = malloc(needed);
		    assert(wss->txbuffer);
		    wss->txbufsize = needed;
		}
		memcpy(&wss->txbuffer[LWS_SEND_BUFFER_PRE_PADDING], zframe_data(f), n);
		m = libwebsocket_write(wsi, &wss->txbuffer[LWS_SEND_BUFFER_PRE_PADDING],
				       n, wss->txmode);
		wss->wsout_bytes += m;
		if (m < n) {
		    lwsl_debug("Websocket %d: partial write %d/%d\n",
			       libwebsocket_get_socket_fd(wsi), m,n);
		    wss->already_sent = m;
		    wss->current = f;
		    wss->partial++;
		} else {
		    zframe_destroy(&f);
		    wss->already_sent = 0;
		    wss->completed++;
		}
	    }
	}
	break;

    case LWS_CALLBACK_CLOSED:
	{
	    // any wss->user_data is to be deallocated in this callback
	    int retval = wss->policy(cfg, wss, ZWS_CLOSE);
	    if (retval > 0) // user policy indicated to use default policy function
		default_policy(cfg, wss, ZWS_CLOSE);

	    lwsl_err("Websocket %d stats: in %d/%d out %d/%d zmq %d/%d partial=%d retry=%d complete=%d txbuf=%d\n",
		     libwebsocket_get_socket_fd(wsi), wss->wsin_msgs, wss->wsin_bytes,
		     wss->wsout_msgs, wss->wsout_bytes,
		     wss->zmq_msgs, wss->zmq_bytes,
		     wss->partial, wss->partial_retry, wss->completed,
		     wss->txbufsize);

	    // stop watching and destroy the zmq sockets
	    zloop_poller_end (cfg->loop, &wss->pollitem);
	    zloop_poller_end (cfg->loop, &wss->wsqin_pollitem);

	    zsocket_destroy (cfg->ctx, wss->socket);
	    zsocket_destroy (cfg->ctx, wss->wsq_in);
	    zsocket_destroy (cfg->ctx, wss->wsq_out);

	    uriFreeQueryListA(wss->queryList);
	    uriFreeUriMembersA(&wss->u);
	}
	break;

    case LWS_CALLBACK_RECEIVE:
	{
	    wss->buffer = in;
	    wss->length = len;
	    wss->wsin_bytes += len;
	    wss->wsin_msgs++;
	    int retval = wss->policy(cfg, wss, ZWS_FROM_WS);
	    if (retval > 0) // user policy indicated to use default policy function
		return default_policy(cfg, wss, ZWS_FROM_WS);
	    else
		return retval;
	}
	break;

	// external poll support for zloop
    case LWS_CALLBACK_ADD_POLL_FD:
	{
	    int zevents = poll2zmq(pa->events);
	    zmq_pollitem_t additem = { 0, pa->fd, zevents };
	    assert(zloop_poller (cfg->loop, &additem, libws_socket_readable, context) == 0);

	    if (zevents & ZMQ_POLLERR) // dont remove poller on POLLERR
		zloop_set_tolerant (cfg->loop, &additem);
	}
	break;

    case LWS_CALLBACK_DEL_POLL_FD:
	{
	    zmq_pollitem_t delitem = { 0, pa->fd, 0 };
	    zloop_poller_end (cfg->loop, &delitem);
	}
	break;

    case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
	{
	    if (pa->prev_events == pa->events) // nothing to do
		break;

	    // remove existing poller
	    zmq_pollitem_t item = { 0, pa->fd, 0 };
	    zloop_poller_end (cfg->loop, &item);

	    // insert new poller with current event mask
	    item.events = poll2zmq(pa->events);

	    assert(zloop_poller (cfg->loop, &item, libws_socket_readable, context) == 0);
	    if (item.events & ZMQ_POLLERR) // dont remove poller on POLLERR
		zloop_set_tolerant (cfg->loop, &item);
	}
	break;

    default:
	break;
    }
    return 0;
}

static int
libws_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *context)
{
    struct pollfd pollstruct;
    int pevents = zmq2poll(item->revents);

    pollstruct.fd = item->fd;
    pollstruct.revents = pollstruct.events = pevents;
    libwebsocket_service_fd((struct libwebsocket_context *)context, &pollstruct);
    return 0;
}

static int
zmq_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    zws_session_t *wss = (zws_session_t *)arg;
    zwsproxy_t *cfg = libwebsocket_context_user(wss->ctxref);
    return wss->policy(cfg, wss, ZWS_TO_WS);
}

static int
wsqin_socket_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    zws_session_t *wss = (zws_session_t *)arg;
    libwebsocket_callback_on_writable(wss->ctxref, wss->wsiref);
    return 0;
}

static const struct {
    const char *extension;
    const char *mime_type;
} builtin_mime_types[] = {

    { ".html", "text/html" },
    { ".htm",  "text/html" },
    { ".shtm", "text/html" },
    { ".shtml","text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/x-javascript" },
    { ".ico",  "image/x-icon" },
    { ".gif",  "image/gif" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".png",  "image/png" },
    { ".svg",  "image/svg+xml" },
    { ".torrent",  "application/x-bittorrent" },
    { ".wav",  "audio/x-wav" },
    { ".mp3",  "audio/x-mp3" },
    { ".mid",  "audio/mid" },
    { ".m3u",  "audio/x-mpegurl" },
    { ".ram",  "audio/x-pn-realaudio" },
    { ".xml",  "text/xml" },
    { ".xslt", "application/xml" },
    { ".ra",   "audio/x-pn-realaudio" },
    { ".doc",  "application/msword" },
    { ".exe",  "application/octet-stream" },
    { ".zip",  "application/x-zip-compressed" },
    { ".xls",  "application/excel" },
    { ".tgz",  "application/x-tar-gz" },
    { ".tar",  "application/x-tar" },
    { ".gz",   "application/x-gunzip" },
    { ".arj",  "application/x-arj-compressed" },
    { ".rar",  "application/x-arj-compressed" },
    { ".rtf",  "application/rtf" },
    { ".pdf",  "application/pdf" },
    { ".swf",  "application/x-shockwave-flash" },
    { ".mpg",  "video/mpeg" },
    { ".mpeg", "video/mpeg" },
    { ".asf",  "video/x-ms-asf" },
    { ".avi",  "video/x-msvideo" },
    { ".bmp",  "image/bmp" },
    { NULL,  NULL }
};

const char *
zwsmimetype(const char *ext)
{
    int i;
    for ( i = 0; builtin_mime_types[i].extension != NULL; i++)
	if (strcasecmp(ext, builtin_mime_types[i].extension) == 0)
	    return  builtin_mime_types[i].mime_type;
    return NULL;
}
