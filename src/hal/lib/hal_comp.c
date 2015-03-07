// HAL components API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

#if defined(ULAPI)
#include <sys/types.h>		/* pid_t */
#include <unistd.h>		/* getpid() */
#endif

hal_comp_t *halpr_alloc_comp_struct(void);
static void free_comp_struct(hal_comp_t * comp);

extern int init_hal_data(void);

int hal_xinit(const char *name,
	      const int type,
	      const int userarg1,
	      const int userarg2,
	      const hal_constructor_t ctor,
	      const hal_destructor_t dtor)
{
    int comp_id, retval;
    char rtapi_name[RTAPI_NAME_LEN + 1];
    char hal_name[HAL_NAME_LEN + 1];

    // tag message origin field
    rtapi_set_logtag("hal_lib");

    CHECK_STRLEN(name, HAL_NAME_LEN);

    // sanity: these must have been inited before by the
    // respective rtapi.so/.ko module
    CHECK_NULL(rtapi_switch);

    if ((dtor != NULL) && (ctor == NULL)) {
	hal_print_error("%s: %s - NULL constructor doesnt make"
			" sense with non-NULL destructor",
			__FUNCTION__, name);
	return -EINVAL;
    }

    // rtapi initialisation already done
    // since this happens through the constructor
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL: initializing component '%s' type=%d arg1=%d arg2=%d/0x%x",
		    name, type, userarg1, userarg2, userarg2);

    /* copy name to local vars, truncating if needed */
    rtapi_snprintf(rtapi_name, RTAPI_NAME_LEN, "HAL_%s", name);
    rtapi_snprintf(hal_name, sizeof(hal_name), "%s", name);

    /* do RTAPI init */
    comp_id = rtapi_init(rtapi_name);
    if (comp_id < 0) {
	HALERR("rtapi init(%s) failed", rtapi_name);
	return -EINVAL;
    }

    // global_data MUST be at hand now:
    HAL_ASSERT(global_data != NULL);

    // NB: the HAL shm segment might not be in place yet
    // (i.e. hal_data and hal_shmem_base might be NULL)
    // this code is common for ULAPI and RTAPI.
    if (lib_mem_id < 0) {

	// the hal_lib component is initializing.
	// acquire the HAL shm segment
	// get HAL shared memory block from RTAPI
	int shm_id = rtapi_shmem_new(HAL_KEY,
				     comp_id,
				     global_data->hal_size);
	if (shm_id < 0) {
	    HALERR("hal_lib(%d): failed to allocate HAL shm %x, rc=%d",
		   comp_id, HAL_KEY, shm_id);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	// retrieve address of HAL shared memory segment
	void *mem;
	retval = rtapi_shmem_getptr(shm_id, &mem, 0);
	if (retval < 0) {
	    HALERR("hal_lib(%d): failed to acquire HAL shm %x, id=%d rc=%d",
		   comp_id, HAL_KEY, shm_id, retval);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	// set up internal pointers to shared mem and data structure
	hal_shmem_base = (char *) mem;
	hal_data = (hal_data_t *) mem;

	// record hal_lib comp_id
	lib_module_id = comp_id;
	// and the HAL shm segmed id
	lib_mem_id = shm_id;

#ifdef RTAPI
	// RTAPI: only on hal_lib initialisation
	// set up the HAL shm segment
	if (type == TYPE_HALLIB) {
	    retval = init_hal_data();

	    if ( retval ) {
		HALERR("could not init HAL shared memory rc=%d\n", retval);
		rtapi_exit(comp_id);
		return -EINVAL;
	    }
	    retval = hal_proc_init();
	    if ( retval ) {
		hal_print_msg(RTAPI_MSG_ERR,
			      "HAL_LIB: ERROR:%d could not init /proc files\n",
			      rtapi_instance);
		rtapi_exit(lib_module_id);
		return -EINVAL;
	    }
	}
#endif
    }

    // tag message origin field since ulapi autoload re-tagged them
    rtapi_set_logtag("hal_lib");
#ifdef ULAPI
    //    hal_rtapi_attach();
#endif
    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before manipulating the shared data */
	rtapi_mutex_get(&(hal_data->mutex));
	/* make sure name is unique in the system */
	if (halpr_find_comp_by_name(hal_name) != 0) {
	    /* a component with this name already exists */
	    HALERR("duplicate component name '%s'", hal_name);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	/* allocate a new component structure */
	comp = halpr_alloc_comp_struct();
	if (comp == 0) {
	    HALERR("insufficient memory for component '%s'", hal_name);
	    rtapi_exit(comp_id);
	    return -ENOMEM;
	}

	/* initialize the comp structure */
	comp->userarg1 = userarg1;
	comp->userarg2 = userarg2;
	comp->comp_id = comp_id;
	comp->type = type;
	comp->ctor = ctor;
	comp->dtor = dtor;
#ifdef RTAPI
	comp->pid = 0;   //FIXME revisit this
#else /* ULAPI */
	// a remote component starts out disowned
	comp->pid = comp->type == TYPE_REMOTE ? 0 : getpid(); //FIXME revisit this
#endif
	comp->state = COMP_INITIALIZING;
	comp->last_update = 0;
	comp->last_bound = 0;
	comp->last_unbound = 0;
	comp->shmem_base = hal_shmem_base;
	comp->insmod_args = 0;
	rtapi_snprintf(comp->name, sizeof(comp->name), "%s", hal_name);
	/* insert new structure at head of list */
	comp->next_ptr = hal_data->comp_list_ptr;
	hal_data->comp_list_ptr = SHMOFF(comp);

    }
    // scope exited - mutex released

    rtapi_print_msg(RTAPI_MSG_DBG,"%s: component '%s' id=%d initialized",
		    __FUNCTION__, hal_name, comp_id);
    return comp_id;
}


int hal_xexit(int comp_id,  const int type)
{
    int *prev, next, comptype;
    char name[HAL_NAME_LEN + 1];

    CHECK_HALDATA();

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: removing component %d",
		  __FUNCTION__, comp_id);

    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

	/* grab mutex before manipulating list */
	rtapi_mutex_get(&(hal_data->mutex));
	/* search component list for 'comp_id' */
	prev = &(hal_data->comp_list_ptr);
	next = *prev;
	if (next == 0) {
	    /* list is empty - should never happen, but... */
	    HALERR("no components defined");
	    return -EINVAL;
	}
	comp = SHMPTR(next);
	while (comp->comp_id != comp_id) {
	    /* not a match, try the next one */
	    prev = &(comp->next_ptr);
	    next = *prev;
	    if (next == 0) {
		/* reached end of list without finding component */
		HALERR("no such component with id %d", comp_id);
		return -EINVAL;
	    }
	    comp = SHMPTR(next);
	}

	// record type, since we're about to zap the comp in free_comp_struct()
	comptype = comp->type;

	/* save component name for later */
	rtapi_snprintf(name, sizeof(name), "%s", comp->name);
	/* get rid of the component */
	free_comp_struct(comp);

	// unlink the comp only now as free_comp_struct() must
	// determine ownership of pins/params/functs and this
	// requires access to the current comp, too
	// since this is all under lock it should not matter
	*prev = comp->next_ptr;

	// add it to free list
	comp->next_ptr = hal_data->comp_free_ptr;
	hal_data->comp_free_ptr = SHMOFF(comp);

	// scope exit - mutex released
    }

    // if unloading the hal_lib component, destroy HAL shm
    if (comptype == TYPE_HALLIB) {
	int retval;

	/* release RTAPI resources */
	retval = rtapi_shmem_delete(lib_mem_id, comp_id);
	if (retval) {
	    hal_print_msg(RTAPI_MSG_ERR,
			  "HAL_LIB:%d rtapi_shmem_delete(%d,%d) failed: %d\n",
			  rtapi_instance, lib_mem_id, comp_id, retval);
	}
	// HAL shm is history, take note ASAP
	lib_mem_id = -1;
	hal_shmem_base = NULL;
	hal_data = NULL;;

	retval = rtapi_exit(comp_id);
	if (retval) {
	    hal_print_msg(RTAPI_MSG_ERR,
			  "HAL_LIB:%d rtapi_exit(%d) failed: %d\n",
			  rtapi_instance, lib_module_id, retval);
	}
	// the hal_lib RTAPI module is history, too
	// in theory we'd be back to square 1
	lib_module_id = -1;

    } else {
	// the standard case
	rtapi_exit(comp_id);
    }

    rtapi_print_msg(RTAPI_MSG_DBG,"%s: component '%s' id=%d removed",
		    __FUNCTION__, name, comp_id);

    return 0;
}


int hal_ready(int comp_id) {
    int next;
    hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));

    /* search component list for 'comp_id' */
    next = hal_data->comp_list_ptr;
    if (next == 0) {
	/* list is empty - should never happen, but... */
	HALERR("BUG: no components defined - %d", comp_id);
	return -EINVAL;
    }

    comp = SHMPTR(next);
    while (comp->comp_id != comp_id) {
	/* not a match, try the next one */
	next = comp->next_ptr;
	if (next == 0) {
	    /* reached end of list without finding component */
	    HALERR("component %d not found", comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
    }
    if(comp->state > COMP_INITIALIZING) {
	HALERR("component '%s' id %d already ready (state %d)",
	       comp->name, comp->comp_id, comp->state);
        return -EINVAL;
    }
    comp->state = (comp->type == TYPE_REMOTE ?  COMP_UNBOUND : COMP_READY);
    return 0;
}

char *hal_comp_name(int comp_id)
{
    hal_comp_t *comp;
    char *result = NULL;
    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_id(comp_id);
    if(comp) result = comp->name;
    rtapi_mutex_give(&(hal_data->mutex));
    return result;
}

hal_comp_t *halpr_find_comp_by_name(const char *name)
{
    int next;
    hal_comp_t *comp;

    /* search component list for 'name' */
    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = SHMPTR(next);
	if (strcmp(comp->name, name) == 0) {
	    /* found a match */
	    return comp;
	}
	/* didn't find it yet, look at next one */
	next = comp->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_comp_t *halpr_find_comp_by_id(int id)
{
    int next;
    hal_comp_t *comp;

    /* search list for 'comp_id' */
    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = SHMPTR(next);
	if (comp->comp_id == id) {
	    /* found a match */
	    return comp;
	}
	/* didn't find it yet, look at next one */
	next = comp->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

// use only for owner_ids of pins, params or functs
// may return NULL if buggy using code
hal_comp_t *halpr_find_owning_comp(const int owner_id)
{
    hal_comp_t *comp = halpr_find_comp_by_id(owner_id);
    if (comp != NULL)
	return comp;  // legacy case: the owner_id refers to a comp

    // nope, so it better be an instance
    hal_inst_t *inst = halpr_find_inst_by_id(owner_id);
    if (inst == NULL) {
	HALERR("BUG: owner_id %d refers neither to a hal_comp_t nor an hal_inst_t",
	       owner_id);
	return NULL;
    }

    // found the instance. Retrieve its owning comp:
    comp =  halpr_find_comp_by_id(inst->owner_id);
    if (comp == NULL) {
	// really bad. an instance which has no owning comp?
	HALERR("BUG: instance %s/%d's owner_id %d refers to a non-existant comp",
	       inst->name, inst->inst_id, inst->owner_id);
    }
    return comp;
}


hal_comp_t *halpr_alloc_comp_struct(void)
{
    hal_comp_t *p;

    /* check the free list */
    if (hal_data->comp_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->comp_free_ptr);
	/* unlink it from the free list */
	hal_data->comp_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_comp_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->comp_id = 0;
	p->type = TYPE_INVALID;
	p->state = COMP_INVALID;
	p->shmem_base = 0;
	p->name[0] = '\0';
    }
    return p;
}

static void free_comp_struct(hal_comp_t * comp)
{
    int *prev, next;
#ifdef RTAPI
    hal_funct_t *funct;
#endif /* RTAPI */
    hal_pin_t *pin;
    hal_param_t *param;
    hal_inst_t *inst;

    /* can't delete the component until we delete its "stuff" */
    /* need to check for functs only if a realtime component */
#ifdef RTAPI
    /* search the function list for this component's functs */
    prev = &(hal_data->funct_list_ptr);
    next = *prev;
    while (next != 0) {
	funct = SHMPTR(next);
	hal_comp_t *owner = halpr_find_owning_comp(funct->owner_id);
	if (owner == comp) {
	    /* this function belongs to our component, unlink from list */
	    *prev = funct->next_ptr;
	    /* and delete it */
	    free_funct_struct(funct);
	} else {
	    /* no match, try the next one */
	    prev = &(funct->next_ptr);
	}
	next = *prev;
    }

    // now that the funct is gone, call the dtor for each instance
    if (comp->dtor) {
	//NB - pins, params etc still intact
	next = hal_data->inst_list_ptr;
	while (next != 0) {
	    inst = SHMPTR(next);
	    if (inst->owner_id == comp->comp_id) {
		// this instance is owned by this comp, call destructor
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: calling custom destructor(%s,%s)", __FUNCTION__,
				comp->name, inst->name);
		comp->dtor(inst->name, inst->inst_data, inst->inst_size);
	    }
	    next = inst->next_ptr;
	}
    }
#endif /* RTAPI */

    /* search the pin list for this component's pins */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (next != 0) {
	pin = SHMPTR(next);
	if (pin->owner_id == comp->comp_id) {
	    /* this pin belongs to our component, unlink from list */
	    *prev = pin->next_ptr;
	    /* and delete it */
	    free_pin_struct(pin);
	} else {
	    /* no match, try the next one */
	    prev = &(pin->next_ptr);
	}
	next = *prev;
    }
    /* search the parameter list for this component's parameters */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (next != 0) {
	param = SHMPTR(next);
	if (param->owner_id == comp->comp_id) {
	    /* this param belongs to our component, unlink from list */
	    *prev = param->next_ptr;
	    /* and delete it */
	    free_param_struct(param);
	} else {
	    /* no match, try the next one */
	    prev = &(param->next_ptr);
	}
	next = *prev;
    }

    // search the instance list and unlink instances owned by this comp
    prev = &(hal_data->inst_list_ptr);
    next = *prev;
    while (next != 0) {
	inst = SHMPTR(next);
	if (inst->owner_id == comp->comp_id) {
	    // this instance is owned by this comp
	    *prev = inst->next_ptr;
	    // zap the instance structure
	    inst->owner_id = 0;
	    inst->inst_id = 0;
	    inst->inst_data = NULL; // NB - loosing HAL memory here
	    inst->inst_size = 0;
	    inst->name[0] = '\0';
	    // add it to free list
	    inst->next_ptr = hal_data->inst_free_ptr;
	    hal_data->inst_free_ptr = SHMOFF(inst);
	} else {
	    prev = &(inst->next_ptr);
	}
	next = *prev;
    }

    /* now we can delete the component itself */
    /* clear contents of struct */
    comp->comp_id = 0;
    comp->type = TYPE_INVALID;
    comp->state = COMP_INVALID;
    comp->last_bound = 0;
    comp->last_unbound = 0;
    comp->last_update = 0;
    comp->shmem_base = 0;
    comp->name[0] = '\0';

}
