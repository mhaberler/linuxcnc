/*
 * minimalistic zeroconf interface
 */

#ifndef _MK_ZEROCONF_H
#define _MK_ZEROCONF_H

#include <inttypes.h>
#include <avahi-common/strlst.h>
#include <avahi-common/address.h>

#ifdef __cplusplus
extern "C" {
#endif


#define MACHINEKIT_DNS_SERVICE_TYPE "_machinekit._tcp"

typedef struct {
    const char *name;
    AvahiProtocol proto; //  AVAHI_PROTO_INET6,  AVAHI_PROTO_INET, AVAHI_PROTO_UNSPEC
    int port;
    const char *type;
    AvahiIfIndex interface; // usually AVAHI_IF_UNSPEC
    AvahiStringList *txt;
} zservice_t;

void *mk_zeroconf_register(const zservice_t *s);
int   mk_zeroconf_unregister(void *);


#ifdef __cplusplus
}
#endif

#endif
