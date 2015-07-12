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
#include "hal_internal.h"

int yield_match(hal_object_ptr o, foreach_args_t *args)
{
    args->user_ptr1 = o.any;
    return 1;  // terminate visit on first match
}

int yield_count(hal_object_ptr o, foreach_args_t *args)
{
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

int yield_foreach(hal_object_ptr o, foreach_args_t *args)
{
    hal_object_callback_t callback = args->user_ptr1;
    return callback(o, args->user_ptr2);
}


int free_object(hal_object_ptr o, foreach_args_t *args)
{
    HALDBG("name=%s id=%d owner=%d type=%d seltype=%d selid=%d selowner=%d",
	   hh_get_name(o.hdr),
	   hh_get_id(o.hdr),
	   hh_get_owner_id(o.hdr),
	   hh_get_type(o.hdr),
	   args->type,
	   args->id,
	   args->owner_id);

    switch (args->type) {

    case HAL_PIN:
	free_pin_struct(o.pin);
	break;

    case HAL_PARAM:
	//free_param_struct(o.param);
	free_halobject((hal_object_ptr)o);
	break;

    case HAL_INST:
	free_inst_struct(o.inst);
	break;

#ifdef RTAPI
    case HAL_THREAD:
	free_thread_struct(o.thread);
	break;

    case HAL_FUNCT:
	free_funct_struct(o.funct);
	break;
#endif

    case HAL_COMP_RT:
    case HAL_COMP_USER:
    case HAL_COMP_REMOTE:
	free_comp_struct(o.comp);
	break;

    case HAL_SIGNAL:
	free_sig_struct(o.sig);
	break;


#ifdef LATER
 

    case HAL_RING:
	free_ring_struct(o.ring);
	break;

    case HAL_GROUP:
	free_group_struct(o.group);
	break;

    case HAL_MEMBER_SIGNAL:
    case HAL_MEMBER_GROUP:
    case HAL_MEMBER_PIN:
	free_member_struct(o.member);
	break;

    case HAL_VTABLE:
	free_halobject(o.any);
	break;
#endif
    default:
	HALBUG("type %d not supported (object type=%d)",
	       args->type, hh_get_type(o.hdr));
	return -1;
    }
    return 0; // continue visiting
}

int yield_count_owned_by_comp(hal_object_ptr o, foreach_args_t *args)
{
    int owner_id = hh_get_owner_id(o.hdr);
    hal_comp_t *owner = halpr_find_owning_comp(args->user_arg1);
    if ((owner != NULL) && (owner->comp_id == owner_id))
	args->user_arg2++;
    return 0;
}
