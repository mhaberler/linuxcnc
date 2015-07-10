//  non-inlined  hal_object_t accessors
//  inlined accessors are in hal_object.h
#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_object.h"
#include "hal_list.h"

int hh_set_namefv(halhdr_t *hh, const char *fmt, va_list ap)
{
    int sz = rtapi_vsnprintf(hh->_name, sizeof(hh->_name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid for name starting with '%s'",
	       sz, hh->_name);
        return -ENOMEM;
    }
    return 0;
}

int  hh_init_hdrfv(halhdr_t *hh,
		   const hal_object_type type,
		   const int owner_id,
		   const char *fmt, va_list ap)
{
    dlist_init_entry(&hh->links);
    hh_set_type(hh, type);
    hh_set_id(hh, rtapi_next_handle());
    hh_set_owner_id(hh, owner_id);
    hh_set_valid(hh);
    return hh_set_namefv(hh, fmt, ap);
}

int  hh_init_hdrf(halhdr_t *hh,
		  const hal_object_type type,
		  const int owner_id,
		  const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hh_init_hdrfv(hh, type, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hh_clear_hdr(halhdr_t *hh)
{
    int ret = hh_is_valid(hh);
    hh_set_id(hh, 0);
    hh_set_owner_id(hh, 0);
    hh_clear_name(hh);
    hh_set_invalid(hh);
    return ret;
}


int halg_foreach(bool use_hal_mutex,
		 foreach_args_t *args,
		 hal_object_callback_t callback)
{
    halhdr_t *hh;
    int nvisited = 0, result;

    CHECK_NULL(args);

    WITH_HAL_MUTEX_IF(use_hal_mutex);
    dlist_for_each_entry(hh, OBJECTLIST, links) {

	// 1. select by type if given
	if ((args->type && (hh_get_type(hh) == args->type)) || (args->type == 0)) {

	    // 2. by id if nonzero
	    // 3. by name if non-NULL - prefix match OK for name strings
	    // if both are given, either will create a match

	    if  ((args->id && (args->id == hh_get_id(hh))) ||
		 (!args->name || (strcmp(hh_get_name(hh), args->name)) == 0)) {

		nvisited++;
		if (callback) {
		    result = callback((hal_object_ptr)hh, args);
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
    // if we get here, we ran through all the objects,
    // so return count of objects visited
    return nvisited;
}


#ifdef RTAPI

EXPORT_SYMBOL(hh_set_namefv);
//EXPORT_SYMBOL(hh_set_namef);
EXPORT_SYMBOL(hh_init_hdrf);
EXPORT_SYMBOL(hh_clear_hdr);
EXPORT_SYMBOL(halg_foreach);
#endif
