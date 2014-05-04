/*
 * minimalistic zeroconf interface
 */

#ifndef _MK_ZEROCONF_H
#define _MK_ZEROCONF_H

#include <inttypes.h>
#include <avahi-common/strlst.h>

#ifdef __cplusplus
#define BEGIN_DECLS extern "C" {
#define END_DECLS }
#else
#define BEGIN_DECLS
#define END_DECLS
#endif

BEGIN_DECLS

#define MACHINEKIT_DNS_SERVICE_TYPE "_machinekit._tcp"

typedef struct {
    char *name;
    int ipv6;   // if 0 - IPV4 only announcements
    int port;
    const char *type;
    AvahiStringList *txt;
} zservice_t;

void *mk_zeroconf_register(const zservice_t *s);
int   mk_zeroconf_unregister(void *);


END_DECLS

#endif
