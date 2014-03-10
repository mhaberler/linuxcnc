#ifndef SDPUBLISH_H
#define SDPUBLISH_H

#include "discovery.h"
#include "czmq.h"
#include <uuid/uuid.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct _spub spub_t;


    // init service discovery for a particular instance id
    // use port 0 for default

    extern spub_t *sp_new(zctx_t *ctx, int port, int instance, uuid_t uuid);

    // export a service (protocol, version, api) to wishlist
    // returns 0 on sucess, -1 on error
    extern int sp_add(spub_t *self,
		      int stype,
		      int version,
		      const char *ipaddr, // may be null
		      int port,           // -1 if none
		      const char *uri,    // may be null
		      int api,
		      const char *description); // may be null

    // start the service publisher
    // returns 0 on sucess, -1 on error
    extern int sp_start(spub_t *self);

    // log service requests
    // returns 0 on sucess, -1 on error
    extern void sp_log(spub_t *self, int trace);

    // start the service publisher
    extern int sp_destroy(spub_t **self);


#ifdef __cplusplus
}
#endif

#endif // SDPUBLISH_H
