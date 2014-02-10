#ifndef _ZWSPROXY_PRIVATE_H_INCLUDED
#define _ZWSPROXY_PRIVATE_H_INCLUDED

#include <uriparser/Uri.h>

// per-session data
typedef struct zws_session_data {
    void *socket; // zmq destination
    zmq_pollitem_t pollitem;
    int socket_type;
    int txmode;

    void *wsq_in;
    void *wsq_out;
    zmq_pollitem_t wsqin_pollitem;

    // adapt to largest frame as we go
    unsigned char  *txbuffer;
    size_t txbufsize;

    void *user_data; // if any: allocate in ZWS_CONNECT, freed in ZWS_CLOSE

    // the current frame received from WS, for the ZWS_FROM_WS callback
    void *buffer;
    size_t length;

    zframe_t *current;   // partially sent frame (to ws)
    size_t already_sent; // how much of current was sent already

    // needed for websocket writable callback
    struct libwebsocket *wsiref;
    struct libwebsocket_context *ctxref;

    // URI/args state
    UriUriA u;
    UriQueryListA *queryList;

    // the policy applied to this session
    zwscvt_cb  policy;

    // stats counters:
    int wsin_bytes, wsin_msgs;
    int wsout_bytes, wsout_msgs;
    int zmq_bytes, zmq_msgs;

    int partial;
    int partial_retry;
    int completed;
} zws_session_t;

// return values:
// 0: success, -1: error; closes connection
// >0: invoke default policy callback
typedef  int (*zwscvt_cb)(zwsproxy_t *self,       // server instance
			  zws_session_t *s,       // session
			  zwscb_type type);       // which callback

typedef struct zwspolicy {
    const char *name;
    zwscvt_cb  callback;
} zwspolicy_t;

// per-instance data
typedef struct zwsproxy {
    void *ctx;
    int debug;
    const char *www_dir;
    void *cmdpipe;
    zloop_t *loop; // for wsproxy_thread interaction
    struct lws_context_creation_info info;
    zlist_t *policies;
} zwsproxy_t;


#endif // _ZWSPROXY_PRIVATE_H_INCLUDED
