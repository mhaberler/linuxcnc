//  non-inlined  hal_object_t accessors
//  inlined accessors are in hal_object.h
#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_object.h"
#include "hal_list.h"
//#include "hal_internal.h"

int hh_set_namefv(halhdr_t *o, const char *fmt, va_list ap)
{
    int sz = rtapi_vsnprintf(o->_name, sizeof(o->_name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid for name starting with '%s'",
	       sz, o->_name);
        return -ENOMEM;
    }
    return 0;
}

int  hh_init_hdrfv(halhdr_t *o,
		   const int owner_id,
		   const char *fmt, va_list ap)
{
    dlist_init_entry(&o->links);
    hh_set_id(o, rtapi_next_handle());
    hh_set_owner_id(o, owner_id);
    hh_set_valid(o);
    return hh_set_namefv(o, fmt, ap);
}

int  hh_init_hdrf(halhdr_t *o,
		  const int owner_id,
		  const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hh_init_hdrfv(o, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hh_clear_hdr(halhdr_t *o)
{
    int ret = hh_is_valid(o);
    hh_set_id(o, 0);
    hh_set_owner_id(o, 0);
    hh_clear_name(o);
    hh_set_invalid(o);
    return ret;
}


int halg_foreach(bool use_hal_mutex,
		 int type,         // one of hal_object_type or 0
		 const char *name, // name prefix or NULL
		 hal_object_callback_t callback,
		 void *arg)
{
    halhdr_t *o;
    int nvisited = 0, result;

    WITH_HAL_MUTEX_IF(use_hal_mutex);
    dlist_for_each_entry(o, OBJECTLIST, links) {

	// select by type if given
	if ((type && (hh_get_type(o) == type)) || (type == 0)) {
	    // then by name if given - prefix match OK
	    if (!name || (strcmp(hh_get_name(o), name)) == 0) {
		nvisited++;
		if (callback) {
		    result = callback((hal_object_ptr)o, arg);
		    if (result < 0) {
			// callback signalled an error, pass that back up.
			return result;
		    } else if (result > 0) {
			// callback signalled 'stop iterating'.
			// pass back the number of visited vtables.
			return nvisited;
		    } else {
			// callback signalled 'OK to continue'
			// fall through
		    }
		} else {
		    // null callback passed in,
		    // just count vtables
		    // nvisited already bumped above.
		}
	    }
	}
	// no match, try the next one
    }
    // if we get here, we ran through all the objects, so return count of objects visited
    return nvisited;
}


#ifdef RTAPI

EXPORT_SYMBOL(hh_set_namefv);
EXPORT_SYMBOL(hh_set_namef);
EXPORT_SYMBOL(hh_init_hdrf);
EXPORT_SYMBOL(hh_clear_hdr);
EXPORT_SYMBOL(halg_foreach);
#endif
