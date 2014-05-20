/*
 * minimalistic zeroconf publish interface
 */

#ifndef _MK_ZEROCONF_H
#define _MK_ZEROCONF_H

#include <inttypes.h>
#include <avahi-common/strlst.h>
#include <avahi-common/address.h>
#include <avahi-common/defs.h>
#include "czmq.h"
#include "czmq-watch.h"
#include "mk-zeroconf-types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    AvahiProtocol proto; //  AVAHI_PROTO_INET6,  AVAHI_PROTO_INET, AVAHI_PROTO_UNSPEC
    int port;
    const char *type;
    AvahiStringList *subtypes;
    AvahiIfIndex interface; // usually AVAHI_IF_UNSPEC
    AvahiStringList *txt;   // must contain uuid=<instance uuid>
    zloop_t *loop;
} zservice_t;

    void *mk_zeroconf_register(const zservice_t *s, AvahiCzmqPoll *av_loop);
    int   mk_zeroconf_unregister(void *s);

typedef enum {
    SD_OK,  // service was found, single UUID matched ok if UUID was given
    SD_TIMEOUT,
    SD_RESOLVER_FAILURE,
    SD_BROWSER_FAILURE,
    SD_CLIENT_FAILURE,
    SD_OTHER,
    SD_UNSET
} zresult_t;

typedef struct {
    AvahiProtocol proto;     //  AVAHI_PROTO_INET6,  AVAHI_PROTO_INET, AVAHI_PROTO_UNSPEC
    AvahiIfIndex interface;  // usually AVAHI_IF_UNSPEC
    char *type;
    const char *match;
    char *domain;
    int timeout_ms;

    AvahiStringList *txt;
    AvahiAddress address;
    char *name;
    char *host_name;
    int port;
    AvahiLookupResultFlags flags;
    int result;
} zresolve_t;

    void *mk_zeroconf_resolve(zresolve_t *s);
    int mk_zeroconf_resolve_free(void *p);

#ifdef __cplusplus
}
#endif

#endif
