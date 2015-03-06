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


int hal_xinit(const char *name, const int type,
	      const int userarg1, const int userarg2,
	      const hal_constructor_t ctor,  const hal_destructor_t dtor)
{
    int comp_id;
    char rtapi_name[RTAPI_NAME_LEN + 1];
    char hal_name[HAL_NAME_LEN + 1];

    // tag message origin field
    rtapi_set_logtag("hal_lib");

    if (name == 0) {
	hal_print_error("%s: no component name", __FUNCTION__);
	return -EINVAL;
    }
    if (strlen(name) > HAL_NAME_LEN) {
	hal_print_error("%s: component name '%s' is too long\n", __FUNCTION__, name);
	return -EINVAL;
    }
    if ((dtor != NULL) && (ctor == NULL)) {
	hal_print_error("%s: %s - NULL constructor doesnt make sense with non-NULL destructor",
			__FUNCTION__, name);
	return -EINVAL;
    }

    // rtapi initialisation already done
    // since this happens through the constructor
    hal_print_msg(RTAPI_MSG_DBG,
		    "HAL: initializing component '%s' type=%d arg1=%d arg2=%d/0x%x\n",
		    name, type, userarg1, userarg2, userarg2);
    /* copy name to local vars, truncating if needed */
    rtapi_snprintf(rtapi_name, RTAPI_NAME_LEN, "HAL_%s", name);
    rtapi_snprintf(hal_name, sizeof(hal_name), "%s", name);

    /* do RTAPI init */
    comp_id = rtapi_init(rtapi_name);
    if (comp_id < 0) {
	hal_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: rtapi init failed\n");
	return -EINVAL;
    }
    // tag message origin field since ulapi autoload re-tagged them
    rtapi_set_logtag("hal_lib");
#ifdef ULAPI
    hal_rtapi_attach();
#endif
    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before manipulating the shared data */
	rtapi_mutex_get(&(hal_data->mutex));
	/* make sure name is unique in the system */
	if (halpr_find_comp_by_name(hal_name) != 0) {
	    /* a component with this name already exists */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: duplicate component name '%s'\n", hal_name);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	/* allocate a new component structure */
	comp = halpr_alloc_comp_struct();
	if (comp == 0) {
	    /* couldn't allocate structure */
	    hal_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: insufficient memory for component '%s'\n", hal_name);
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
    /* done */
    hal_print_msg(RTAPI_MSG_DBG,
		  "%s(%s) component initialized id=%d",
		  __FUNCTION__, hal_name, comp_id);
    return comp_id;
}


int hal_exit(int comp_id)
{
    int *prev, next;
    char name[HAL_NAME_LEN + 1];

    if (hal_data == 0) {
	hal_print_error("%s(%d) exit called before init", __FUNCTION__, comp_id);
	return -EINVAL;
    }
    hal_print_msg(RTAPI_MSG_DBG, "%s(%d) removing component",
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
	    hal_print_error("%s(%d) no component defined", __FUNCTION__, comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
	while (comp->comp_id != comp_id) {
	    /* not a match, try the next one */
	    prev = &(comp->next_ptr);
	    next = *prev;
	    if (next == 0) {
		/* reached end of list without finding component */
		hal_print_error("%s(%d) no such component", __FUNCTION__, comp_id);
		return -EINVAL;
	    }
	    comp = SHMPTR(next);
	}

	/* save component name for later */
	rtapi_snprintf(name, sizeof(name), "%s", comp->name);
	/* get rid of the component */
	free_comp_struct(comp); //BUG-comp already unlinked!!!

	// unlink the comp only now as free_comp_struct() must
	// determine ownership of pins/params/functs and this
	// requires access to the current comp, too
	// since this is all under lock it should not matter
	*prev = comp->next_ptr;

	// add it to free list
	comp->next_ptr = hal_data->comp_free_ptr;
	hal_data->comp_free_ptr = SHMOFF(comp);

	/*! \todo Another #if 0 */
#if 0
	/*! \todo FIXME - this is the beginning of a two pronged approach to managing
	  shared memory.  Prong 1 - re-init the shared memory allocator whenever
	  it is known to be safe.  Prong 2 - make a better allocator that can
	  reclaim memory allocated by components when those components are
	  removed. To be finished later. */
	/* was that the last component? */
	if (hal_data->comp_list_ptr == 0) {
	    /* yes, are there any signals or threads defined? */
	    if ((hal_data->sig_list_ptr == 0) && (hal_data->thread_list_ptr == 0)) {
		/* no, invalidate "magic" number so shmem will be re-inited when
		   a new component is loaded */
		hal_data->magic = 0;
	    }
	}
#endif
	// scope exit - mutex released
    }
    // the RTAPI resources are now released
    // on hal_lib shared library unload
    rtapi_exit(comp_id);
    /* done */
    hal_print_msg(RTAPI_MSG_DBG,"%s(%d): component removed, name = '%s'\n",
		  __FUNCTION__,comp_id, name);

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
	hal_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }

    comp = SHMPTR(next);
    while (comp->comp_id != comp_id) {
	/* not a match, try the next one */
	next = comp->next_ptr;
	if (next == 0) {
	    /* reached end of list without finding component */
	    hal_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: component %d not found\n", comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
    }
    if(comp->state > COMP_INITIALIZING) {
        hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: Component '%s' already ready (%d)\n",
			comp->name, comp->state);
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
	hal_print_error("BUG: %s(%d): owner_id refers neither to a hal_comp_t nor an hal_inst_t",
			__FUNCTION__, owner_id);
	return NULL;
    }

    // found the instance. Retrieve its owning comp:
    comp =  halpr_find_comp_by_id(inst->owner_id);
    if (comp == NULL) {
	// really bad. an instance which has no owning comp?
	hal_print_error("BUG: %s(%d): instance %s/%d's owner_id %d refers to a non-existant comp",
			__FUNCTION__, owner_id, inst->name, inst->inst_id, inst->owner_id);
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
#if 0
Mar  6 07:40:01 nwheezy msgd:0: hal_lib:15589:rt hal_exit(74) removing component
Mar  6 07:40:01 nwheezy msgd:0: hal_lib:15589:rt HAL error: funct delinst 67 owner? hal_lib OK

BUG
Mar  6 07:40:01 nwheezy msgd:0: hal_lib:15589:rt HAL: BUG: halpr_find_owning_comp(74): owner_id refers neither to a hal_comp_t nor an hal_inst_t

Mar  6 07:40:01 nwheezy msgd:0: hal_lib:15589:rt HAL error: funct foo.funct 74 owner? NULL
#endif

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

	hal_print_error("funct %s %d owner? %s",
			funct->name, funct->owner_id, owner? owner->name:"NULL");

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
		hal_print_msg(RTAPI_MSG_DBG,
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
