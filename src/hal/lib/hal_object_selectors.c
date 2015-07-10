// predefined selector callbacks for the halg_foreach() iterator
//
// see hal_objects.h:halg_foreach for halg_foreach semantics
// see hal_object_selectors.h for selector usage

#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_object.h"
#include "hal_list.h"

int yield_match(hal_object_ptr o, foreach_args_t *args)
{
    args->user_ptr1 = o.any;
    return 1;  // terminate visit on first match
}

int yield_count(hal_object_ptr o, foreach_args_t *args)
{
    args->user_arg1++;
    return 0; // continue visiting
}

int yield_versioned_vtable_object(hal_object_ptr o, foreach_args_t *args)
{
    if (o.vtable->version == args->user_arg1) {
	args->user_ptr1 = o.any;
	return 1;  // terminate visit on match
    }
    return 0;
}

int count_subordinate_objects(hal_object_ptr o, foreach_args_t *args)
{
    if (hh_get_owner_id(o.hdr) == args->user_arg1) {
	args->user_arg2++;
    }
    return 0; // continue visiting
}
