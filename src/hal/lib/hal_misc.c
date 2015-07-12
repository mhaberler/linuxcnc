// HAL miscellaneous functions which dont clearly fit elsewhere

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"
#include "hal_object_selectors.h"

/***********************************************************************
*                     utility functions, mostly used by haltalk        *
*                                                                      *
************************************************************************/

// return number of pins in a component, legacy or all-insts
int halpr_pin_count(const char *name)
{
    hal_comp_t *comp = halpr_find_comp_by_name(name);
    if (comp == 0)
	return -ENOENT;

    foreach_args_t args =  {
	.type = HAL_PIN,
	.owning_comp = comp->comp_id,
    };
    return halg_foreach(0, &args, NULL);

#if 0
    hal_comp_t *comp;
    hal_comp_t *owner;
    hal_pin_t *pin;
    int count = 0;

    comp = halpr_find_comp_by_name(name);
    if (comp == 0)
	return -ENOENT;

    int next = hal_data->pin_list_ptr;
    while (next != 0) {
	pin = (hal_pin_t *)SHMPTR(next);
	owner = halpr_find_owning_comp(ho_owner_id(pin));
	if (owner->comp_id == comp->comp_id)
	    count++;
	next = pin->next_ptr;
    }
    return count;
#endif
}

// return number of params in a component, legacy or all-insts
int
halpr_param_count(const char *name)
{
    hal_comp_t *comp = halpr_find_comp_by_name(name);
    if (comp == 0)
	return -ENOENT;

    foreach_args_t args =  {
	.type = HAL_PARAM,
	.owning_comp = comp->comp_id,
    };
    return halg_foreach(0, &args, NULL);

#if 0
    hal_comp_t *owner;
    int count = 0;

    int next = hal_data->param_list_ptr;
    while (next != 0) {
	hal_param_t *param = (hal_param_t *)SHMPTR(next);
	owner = halpr_find_owning_comp(param->owner_id);
	if (owner->comp_id == comp->comp_id)
	    count++;
	next = param->next_ptr;
    }
    return count;
#endif
}

// hal mutex scope-locked version of halpr_find_pin_by_name()
hal_pin_t *
hal_find_pin_by_name(const char *name)
{
    hal_pin_t *p __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));
    p = halpr_find_pin_by_name(name);
    return p;
}

int
hal_comp_state_by_name(const char *name)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));

    comp = halpr_find_comp_by_name(name);
    if (comp == NULL)
	return -ENOENT;
    return comp->state;
}
