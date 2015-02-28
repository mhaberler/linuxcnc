// HAL instance API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"


// interal: alloc/free
static hal_inst_t *alloc_inst_struct(void);
static void free_inst_struct(hal_inst_t *inst);
static void free_inst_struct(hal_inst_t *inst);

#if 0
int hal_init_inst(const char *name, hal_constructor_t ctor, hal_destructor_t dtor);

int hal_init_inst(const char *name, hal_constructor_t ctor, hal_destructor_t dtor);

// backwards compatibility:
static inline int hal_init(const char *name) {
#ifdef RTAPI
    return hal_init_mode(name, TYPE_RT, 0, 0);
#else
    return hal_init_mode(name, TYPE_USER, 0, 0);
#endif
}

#endif
int hal_inst_create(const char *name, const int comp_id, const int size,
		    void **inst_data)
{
    if (hal_data == 0) {
	hal_print_error("%s: called before init", __FUNCTION__);
	return -EINVAL;
    }
    if (name == NULL) {
	hal_print_error("%s: called with NULL name", __FUNCTION__);
	return -EINVAL;
    }
    if (size && (inst_data == NULL)) {
	hal_print_error("%s: size %d but inst_data NULL", __FUNCTION__, size);
	return -EINVAL;
    }
    {
	hal_inst_t *inst  __attribute__((cleanup(halpr_autorelease_mutex)));
	hal_comp_t *comp;
	void *m = NULL;
	rtapi_mutex_get(&(hal_data->mutex));

	// comp must exist
	if ((comp = halpr_find_comp_by_id(comp_id)) == 0) {
	    hal_print_error("%s: comp %d not found", __FUNCTION__, comp_id);
	    return -ENOENT;
	}

	// inst may not exist
	if ((inst = halpr_find_inst_by_name(name)) == 0) {
	    hal_print_error("%s: instance %s already exists", __FUNCTION__, name);
	    return -EEXIST;
	}

	if (size > 0) {
	    m = shmalloc_up(size);
	    if (m == NULL) {
		hal_print_error("%s: instance %s: cant allocate %d bytes", __FUNCTION__, name, size);
		return -ENOMEM;
	    }
	}

	// allocate instance descriptor
	if ((inst = alloc_inst_struct()) == NULL) {
	    hal_print_error("insufficient memory for instance '%s'\n", name);
	    return -ENOMEM;
	}
	inst->owner_ptr = SHMOFF(comp);
	inst->inst_id = rtapi_next_handle();
	inst->inst_data = m;
	inst->inst_size = size;
	rtapi_snprintf(inst->name, sizeof(inst->name), "%s", name);

	// make it visible
	inst->next_ptr = hal_data->inst_list_ptr;
	hal_data->inst_list_ptr = SHMOFF(inst);
  }
  return 0;
}

int hal_inst_delete(const char *name)
{
    if (hal_data == 0) {
	hal_print_error("%s: called before init", __FUNCTION__);
	return -EINVAL;
    }
    if (name == NULL) {
	hal_print_error("%s: called with NULL name", __FUNCTION__);
	return -EINVAL;
    }
    {
	hal_inst_t *inst  __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	// inst must exist
	if ((inst = halpr_find_inst_by_name(name)) == NULL) {
	    hal_print_error("%s: instance %s does not exist", __FUNCTION__, name);
	    return -EEXIST;
	}
	// this does most of the heavy lifting
	free_inst_struct(inst);
  }
  return 0;
}


hal_inst_t *halpr_find_inst_by_name(const char *name)
{
    int next;
    hal_inst_t *inst;

    /* search inst list for 'name' */
    next = hal_data->inst_list_ptr;
    while (next != 0) {
	inst = SHMPTR(next);
	if (strcmp(inst->name, name) == 0) {
	    /* found a match */
	    return inst;
	}
	if (strcmp(inst->name, name) == 0) {
	    /* found a match */
	    return inst;
	}
	/* didn't find it yet, look at next one */
	next = inst->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

// lookup instance by instance ID
hal_inst_t *halpr_find_inst_by_id(const int id)
{
    int next;
    hal_inst_t *inst;

    next = hal_data->inst_list_ptr;
    while (next != 0) {
	inst = SHMPTR(next);
	if (inst->inst_id == id) {
	    return inst;
	}
	next = inst->next_ptr;
    }
    return 0;
}

/** The 'halpr_find_pin_by_instance()' function find pins owned by a specific
    instance.  If 'start' is NULL, they start at the beginning of the
    appropriate list, and return the first item owned by 'instance'.
    Otherwise they assume that 'start' is the value returned by a prior
    call, and return the next matching item.  If no match is found, they
    return NULL.
*/
hal_pin_t *halpr_find_pin_by_instance(hal_inst_t * inst, hal_pin_t * start)
{
    int inst_ptr, next;
    hal_pin_t *pin;

    /* get offset of 'inst' component */
    inst_ptr = SHMOFF(inst);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of pin list */
	next = hal_data->pin_list_ptr;
    } else {
	/* no, start at next pin */
	next = start->next_ptr;
    }
    while (next != 0) {
	pin = SHMPTR(next);
	if (pin->instance_ptr == inst_ptr) {
	    /* found a match */
	    return pin;
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

/** The 'halpr_find_param_by_instance()' function find params owned by a specific
    instance.  If 'start' is NULL, they start at the beginning of the
    appropriate list, and return the first item owned by 'instance'.
    Otherwise they assume that 'start' is the value returned by a prior
    call, and return the next matching item.  If no match is found, they
    return NULL.
*/
hal_param_t *halpr_find_param_by_instance(hal_inst_t * inst, hal_param_t * start)
{
    int inst_ptr, next;
    hal_param_t *param;

    /* get offset of 'inst'  */
    inst_ptr = SHMOFF(inst);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of param list */
	next = hal_data->param_list_ptr;
    } else {
	/* no, start at next param */
	next = start->next_ptr;
    }
    while (next != 0) {
	param = SHMPTR(next);
	if (param->instance_ptr == inst_ptr) {
	    /* found a match */
	    return param;
	}
	/* didn't find it yet, look at next one */
	next = param->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

/** The 'halpr_find_funct_by_instance()' function find functs owned by a specific
    instance.  If 'start' is NULL, they start at the beginning of the
    appropriate list, and return the first item owned by 'instance'.
    Otherwise they assume that 'start' is the value returned by a prior
    call, and return the next matching item.  If no match is found, they
    return NULL.
*/
hal_funct_t *halpr_find_funct_by_instance(hal_inst_t * inst, hal_funct_t * start)
{
    int inst_ptr, next;
    hal_funct_t *funct;

    /* get offset of 'inst'  */
    inst_ptr = SHMOFF(inst);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of funct list */
	next = hal_data->funct_list_ptr;
    } else {
	/* no, start at next funct */
	next = start->next_ptr;
    }
    while (next != 0) {
	funct = SHMPTR(next);
	if (funct->instance_ptr == inst_ptr) {
	    /* found a match */
	    return funct;
	}
	/* didn't find it yet, look at next one */
	next = funct->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

static hal_inst_t *alloc_inst_struct(void)
{
    hal_inst_t *hi;

    /* check the free list */
    if (hal_data->inst_free_ptr != 0) {
	/* found a free structure, point to it */
	hi = SHMPTR(hal_data->inst_free_ptr);
	/* unlink it from the free list */
	hal_data->inst_free_ptr = hi->next_ptr;
	hi->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	hi = shmalloc_dn(sizeof(hal_inst_t));
    }
    if (hi) {
	/* make sure it's empty */
	hi->next_ptr = 0;
	hi->owner_ptr = 0;
	hi->inst_id = 0;
	hi->inst_data = NULL;
	hi->inst_size = 0;
	hi->name[0] = '\0';
    }
    return hi;
}

// almost a copy of free_comp_struct(), but based on instance
// unlinks & deletes all pins owned by this instance
// deletes params owned by this instance
// unlinks and frees functs expored by this instance
static void free_inst_struct(hal_inst_t * inst)
{
    int *prev, next;
#ifdef RTAPI
    hal_funct_t *funct;
#endif /* RTAPI */
    hal_pin_t *pin;
    hal_param_t *param;

    /* can't delete the instance until we delete its "stuff" */
    /* need to check for functs only if a realtime component */
#ifdef RTAPI
    /* search the function list for this instance's functs */
    prev = &(hal_data->funct_list_ptr);
    next = *prev;
    while (next != 0) {
	funct = SHMPTR(next);
	if (SHMPTR(funct->instance_ptr) == inst) {
	    /* this function belongs to this instance, unlink from list */
	    *prev = funct->next_ptr;
	    /* and delete it */
	    free_funct_struct(funct);
	} else {
	    /* no match, try the next one */
	    prev = &(funct->next_ptr);
	}
	next = *prev;
    }
#endif /* RTAPI */
    /* search the pin list for this instance's pins */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (next != 0) {
	pin = SHMPTR(next);
	if (SHMPTR(pin->instance_ptr) == inst) {
	    /* this pin belongs to our instance, unlink from list */
	    *prev = pin->next_ptr;
	    /* and delete it */
	    free_pin_struct(pin);
	} else {
	    /* no match, try the next one */
	    prev = &(pin->next_ptr);
	}
	next = *prev;
    }
    /* search the parameter list for this instance's parameters */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (next != 0) {
	param = SHMPTR(next);
	if (SHMPTR(param->instance_ptr) == inst) {
	    /* this param belongs to our instance, unlink from list */
	    *prev = param->next_ptr;
	    /* and delete it */
	    free_param_struct(param);
	} else {
	    /* no match, try the next one */
	    prev = &(param->next_ptr);
	}
	next = *prev;
    }
    /* now we can delete the instance itself */
    inst->owner_ptr = 0;
    inst->inst_id = 0;
    inst->inst_data = NULL; // NB - loosing HAL memory here
    inst->inst_size = 0;
    inst->name[0] = '\0';
    /* add it to free list */
    inst->next_ptr = hal_data->inst_free_ptr;
    hal_data->inst_free_ptr = SHMOFF(inst);
}
