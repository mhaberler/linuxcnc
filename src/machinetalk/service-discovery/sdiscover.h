// service discovery - client side API


#ifndef SDISCOVER_H
#define SDISCOVER_H

#include "discovery.h"
#include "czmq.h"


#ifdef __cplusplus
extern "C" {
#endif

    typedef struct _sdreq sdreq_t;


    // init service discovery for a particular instance id
    // use port 0 for default
    extern sdreq_t *sd_new(int port, int instance);

    // return the socket fd registered by sd_new()
    // usage example: integrate into event loop
    int sd_socket(sdreq_t *self);

    // add  (protocol, minversion, api) to wishlist
    // returns 0 on sucess, -1 on error
    extern int sd_add(sdreq_t *self, unsigned stype, unsigned version, unsigned api);

    // log service requests and replies
    // returns 0 on sucess, -1 on error
    extern void sd_log(sdreq_t *self, int trace);

    // just broadcast the probe frame, without waiting for a reply
    // usage example: integrate into event loop
    extern int sd_send_probe(sdreq_t *self);

    // run the query on the wishlist
    // returns 0 on sucess (all services found)
    // -ETIMEDOUT on timeout
    int sd_query(sdreq_t *self, int timeoutms);

    // print the result
    void sd_dump(const char *tag, sdreq_t *self);

    // result accessors - returned strings must be free()'d
    const char *sd_ipaddress(sdreq_t *self, int stype);
    int sd_port(sdreq_t *self, int stype);
    int sd_version(sdreq_t *self, int stype);
    const char *sd_uri(sdreq_t *self, int stype);
    const char *sd_description(sdreq_t *self, int stype);

    // destroy discovery
    void sd_destroy(sdreq_t **self);

#ifdef __cplusplus
}
#endif

#endif // SDISCOVER_H
