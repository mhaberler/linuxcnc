/* zeroconf announce/withdraw tailored for machinekit purposes
 */

#ifndef _MK_ZEROCONF_HH
#define _MK_ZEROCONF_HH

#include <uuid/uuid.h>
#include <czmq.h>

#include "czmq-watch.h"
#include "ll-zeroconf.hh"
#include "mk-zeroconf-types.h"

#ifdef __cplusplus
extern "C" {
#endif

    register_context_t * zeroconf_service_announce(const char *name,
						   const char *subtype,
						   int port,
						   char *dsn,
						   const char *service_uuid,
						   uuid_t process_uuid,
						   const char *tag,
						   AvahiCzmqPoll *av_loop);
    int zeroconf_service_withdraw( register_context_t *publisher);

#ifdef __cplusplus
}
#endif

#endif
