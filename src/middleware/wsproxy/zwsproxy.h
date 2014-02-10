#ifndef _ZWSPROXY_H_INCLUDED
#define _ZWSPROXY_H_INCLUDED

#include <czmq.h>
#include <libwebsockets.h>

typedef enum zwscvt_type {
    ZWS_CONNECTING,
    ZWS_ESTABLISHED,
    ZWS_CLOSE,
    ZWS_FROM_WS,
    ZWS_TO_WS,
} zwscb_type;

typedef struct zwsproxy zwsproxy_t;
typedef struct zws_session_data zws_session_t;

typedef  int (*zwscvt_cb)(zwsproxy_t *self,       // server instance
			  zws_session_t *s,       // session
			  zwscb_type type);       // which callback
typedef  void (*zwslog_cb)(int level, const char *line);


#ifdef __cplusplus
extern "C" {
#endif


/**
 * zwsproxy_new() - create a Websockets proxy thread
 * @ctx:	the zeroMQ context
 * @wwwdir:     directory to serve files with HTTP from; use NULL to disable HTTP file service.
 * @info:	libwebsockets setup structure; see libwebsockets documentation.
 *              the 'protocols' parameter is set internally and is ignored.
 *
 * The URI per se is not interpreted; the query arguments are.
 *
 * Meaning of query arguments:
 *
 * @connect=<uri>: ZMQ socket URI to connect to, see the zeroMQ documentation
 * @bind=<uri>: ZMQ socket URI to bind to, see the zeroMQ documentation
 *              at least one bind or connect URI must be given
 * @text:       use text write (by default, uses binary write)
 * @identity=ident:   ZMQ socket identity (optional)
 * @type=dealer|sub|xsub: ZMQ socket type
 * @subscribe=topic:  ZMQ subscribe topics (sub and xsub only).
 *              use several subscribe=topic args to subscribe to several channels
 * @policy=policyname:  invoke a user-defined policy handler set with zwsproxy_add_policy()
 *
 * Example:     wss://127.0.0.1:7682/?connect=inproc://echo&type=dealer&identity=wsclient
 *              create a dealer socket
 *              set socket identity to 'wsclient'
 *              connect to 'inproc://echo'
 *
 * zwsproxy_new() creates the data structures, but blocks service until
 * zwsproxy_start() is called.
 */
zwsproxy_t *zwsproxy_new(void *ctx, const char *wwwdir, struct lws_context_creation_info *info);

/**
 * zwsproxy_debug() - terminate a Websockets proxy thread
 * @self:        proxy handle returned by zwsproxy_new()
 * @debug:       debug level - see libwebsockets docs for interpretation
 * @dest:        set a log handler callback. By default log will go to stderr.
 *
 */
int zwsproxy_debug(zwsproxy_t *self, int debug, zwslog_cb *dest);

/**
 * zwsproxy_start() - activate a Websockets proxy thread
 * @self:         proxy handle returned by zwsproxy_new()
 *
 */
int zwsproxy_start(zwsproxy_t *self);

/**
 * zwsproxy_exit() - terminate a Websockets proxy thread
 * @self:         proxy handle returned by zwsproxy_new()
 *
 * proxy thread must have been activated by zwsproxy_start()
 * returns 0 on success, -1 if proxy thread already exited.
 * sets self to NULL.
 */
void zwsproxy_exit(zwsproxy_t **self);

/**
 * zwsproxy_add_policy() - add a named relaying policy
 * @self:         proxy handle returned by zwsproxy_new()
 * @name:         policy name; referred to in URI as policy=<name>
 * @cb:           policy callback function
 * this should happen after zwsproxy_new() and zwsproxy_start()
 */
void zwsproxy_add_policy(zwsproxy_t *self, const char *name, zwscvt_cb cb);

/**
 * zwsmimetype() - return mime type based on file extension
 * @ext: the file extension, including '.'
 */
#ifdef __cplusplus
}
#endif


#endif // _ZWSPROXY_H_INCLUDED
