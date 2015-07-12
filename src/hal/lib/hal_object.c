//  non-inlined  hal_object_t accessors
//  inlined accessors are in hal_object.h
#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_object.h"
#include "hal_list.h"
#include "hal_internal.h"

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
    dlist_init_entry(&hh->list);
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

void free_halobject(hal_object_ptr o)
{
    unlink_object(o.hdr);
    hh_clear_hdr(o.hdr);
    shmfree_desc(o.hdr);
}

int halg_foreach(bool use_hal_mutex,
		 foreach_args_t *args,
		 hal_object_callback_t callback)
{
    halhdr_t *hh, *tmp;
    int nvisited = 0, result;

    CHECK_NULL(args);

    // run with HAL mutex if use_hal_mutex nonzero:
    WITH_HAL_MUTEX_IF(use_hal_mutex);

    dlist_for_each_entry_safe(hh, tmp, OBJECTLIST, list) {

	// cop out if the object list emptied by now
	// (may happen through callbacks when deleting)
	if (dlist_empty_careful(&hh->list))
	    break;

	// 1. select by type if given
	if (args->type && (hh_get_type(hh) != args->type))
	    continue;

	// 2. by id if nonzero
	if  (args->id && (args->id != hh_get_id(hh)))
	    continue;

	// 3. by owner id if nonzero
	if (args->owner_id && (args->owner_id != hh_get_owner_id(hh)))
	    continue;

	// 4. by owning comp (directly-legacy case, or indirectly -
	// for pins, params and functs owned by an instance).
	// see comments near the foreach_args definition in hal_object.h.
	if (args->owning_comp) {
	    hal_comp_t *oc = halpr_find_owning_comp(hh_get_owner_id(hh));
	    if (oc == NULL)
		continue;  // a bug, halpr_find_owning_comp will log already
	    if (!(oc->comp_id == args->owning_comp))
		continue;
	}

	// 5. by name if non-NULL - prefix match OK for name strings
	if (args->name && strcmp(hh_get_name(hh), args->name))
	    continue;

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
	    // just count matces
	}
    }
    // no match, try the next one

    // if we get here, we ran through all the matched objects,
    // so return match count
    return nvisited;
}


#ifdef RTAPI

EXPORT_SYMBOL(hh_set_namefv);
//EXPORT_SYMBOL(hh_set_namef);
EXPORT_SYMBOL(hh_init_hdrf);
EXPORT_SYMBOL(hh_clear_hdr);
EXPORT_SYMBOL(halg_foreach);
#endif
