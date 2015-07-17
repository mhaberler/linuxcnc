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

#ifdef RTAPI
static int init_hal_data(void);
static int create_instance(const hal_funct_args_t *fa);
static int delete_instance(const hal_funct_args_t *fa);
#endif

int hal_xinitf(const int type,
	       const int userarg1,
	       const int userarg2,
	       const hal_constructor_t ctor,
	       const hal_destructor_t dtor,
	       const char *fmt, ...)
{
    va_list ap;
    char hal_name[HAL_NAME_LEN + 1];

    CHECK_NULL(fmt);
    va_start(ap, fmt);
    int sz = rtapi_vsnprintf(hal_name, sizeof(hal_name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("invalid length %d for name starting with '%s'",
	       sz, hal_name);
        return -EINVAL;
    }
    return halg_xinit(1, type, userarg1, userarg2, ctor, dtor, hal_name);
}

int halg_xinit(const int use_hal_mutex,
	       const int type,
	       const int userarg1,
	       const int userarg2,
	       const hal_constructor_t ctor,
	       const hal_destructor_t dtor,
	       const char *name)
{
    int comp_id, retval;

    rtapi_set_logtag("hal_lib");
    CHECK_STRLEN(name, HAL_NAME_LEN);

    // sanity: these must have been inited before by the
    // respective rtapi.so/.ko module
    CHECK_NULL(rtapi_switch);

    if ((dtor != NULL) && (ctor == NULL)) {
	HALERR("component '%s': NULL constructor doesnt make"
	       " sense with non-NULL destructor", name);
	return -EINVAL;
    }

    // RTAPI initialisation already done
    HALDBG("initializing component '%s' type=%d arg1=%d arg2=%d/0x%x",
	   name, type, userarg1, userarg2, userarg2);

    if ((lib_module_id < 0) && (type != TYPE_HALLIB)) {
	// if hal_lib not inited yet, do so now - recurse
#ifdef RTAPI
	retval = hal_xinit(TYPE_HALLIB, 0, 0, NULL, NULL, "hal_lib");
#else
	retval = hal_xinitf(TYPE_HALLIB, 0, 0, NULL, NULL, "hal_lib%ld",
			    (long) getpid());
#endif
	if (retval < 0)
	    return retval;
    }

    // tag message origin field since ulapi autoload re-tagged them temporarily
    rtapi_set_logtag("hal_lib");

    /* copy name to local vars, truncating if needed */
    char rtapi_name[RTAPI_NAME_LEN + 1];
    char hal_name[HAL_NAME_LEN + 1];

    rtapi_snprintf(hal_name, sizeof(hal_name), "%s", name);
    rtapi_snprintf(rtapi_name, RTAPI_NAME_LEN, "HAL_%s", hal_name);

    /* do RTAPI init */
    comp_id = rtapi_init(rtapi_name);
    if (comp_id < 0) {
	HALERR("rtapi init(%s) failed", rtapi_name);
	return -EINVAL;
    }

    // recursing? init HAL shm
    if ((lib_module_id < 0) && (type == TYPE_HALLIB)) {
	// recursion case, we're initing hal_lib

	// get HAL shared memory from RTAPI
	int shm_id = rtapi_shmem_new(HAL_KEY,
				     comp_id,
				     global_data->hal_size);
	if (shm_id < 0) {
	    HALERR("hal_lib:%d failed to allocate HAL shm %x, rc=%d",
		   comp_id, HAL_KEY, shm_id);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	// retrieve address of HAL shared memory segment
	void *mem;
	retval = rtapi_shmem_getptr(shm_id, &mem, 0);
	if (retval < 0) {
	    HALERR("hal_lib:%d failed to acquire HAL shm %x, id=%d rc=%d",
		   comp_id, HAL_KEY, shm_id, retval);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}
	// set up internal pointers to shared mem and data structure
	hal_shmem_base = (char *) mem;
	hal_data = (hal_data_t *) mem;

#ifdef RTAPI
	// only on RTAPI hal_lib initialization:
	// initialize up the HAL shm segment
	retval = init_hal_data();
	if (retval) {
	    HALERR("could not init HAL shared memory rc=%d", retval);
	    rtapi_exit(lib_module_id);
	    lib_module_id = -1;
	    return -EINVAL;
	}
	retval = hal_proc_init();
	if (retval) {
	    HALERR("could not init /proc files");
	    rtapi_exit(lib_module_id);
	    lib_module_id = -1;
	    return -EINVAL;
	}
#endif
	// record hal_lib comp_id
	lib_module_id = comp_id;
	// and the HAL shm segmed id
	lib_mem_id = shm_id;

    }
    // global_data MUST be at hand now:
    HAL_ASSERT(global_data != NULL);

    // paranoia
    HAL_ASSERT(hal_shmem_base != NULL);
    HAL_ASSERT(hal_data != NULL);
    HAL_ASSERT(lib_module_id > -1);
    HAL_ASSERT(lib_mem_id > -1);
    if (lib_module_id < 0) {
	HALERR("giving up");
	return -EINVAL;
    }

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_comp_t *comp;

	/* make sure name is unique in the system */
	if (halpr_find_comp_by_name(hal_name) != 0) {
	    /* a component with this name already exists */
	    HALERR("duplicate component name '%s'", hal_name);
	    rtapi_exit(comp_id);
	    return -EINVAL;
	}

	// allocate component descriptor
	if ((comp = shmalloc_desc(sizeof(hal_comp_t))) == NULL) {
	    HALERR("insufficient memory for component '%s'", hal_name);
	    rtapi_exit(comp_id);
	    return -ENOMEM;
	}
	// initialize common HAL header fields
	// cant use hh_init_hdrf() because the comp_id comes from
	// rtapi_init(), not rtapi_next_handle(); no point
	// in making this a single-case function
	dlist_init_entry(&comp->hdr.list);
	hh_set_type(&comp->hdr, HAL_COMPONENT);
	hh_set_id(&comp->hdr, comp_id);
	hh_set_owner_id(&comp->hdr, 0);
	hh_set_valid(&comp->hdr);
	hh_set_namef(&comp->hdr, hal_name);

	/* initialize the comp structure */
	comp->userarg1 = userarg1;
	comp->userarg2 = userarg2;
	comp->type = type;  // subtype (RT, USER, REMOTE)
	comp->ctor = ctor;
	comp->dtor = dtor;
#ifdef RTAPI
	comp->pid = 0;
#else /* ULAPI */
	// a remote component starts out disowned
	comp->pid = comp->type == TYPE_REMOTE ? 0 : getpid();
#endif
	comp->state = COMP_INITIALIZING;
	comp->last_update = 0;
	comp->last_bound = 0;
	comp->last_unbound = 0;
	comp->shmem_base = hal_shmem_base;
	comp->insmod_args = 0;

	// make it visible
	halg_add_object(false, (hal_object_ptr)comp);
    }
    // scope exited - mutex released

    // finish hal_lib initialisation
    // in ULAPI this will happen after the recursion on hal_lib%d unwinds

    if (type == TYPE_HALLIB) {
#ifdef RTAPI
	// only on RTAPI hal_lib initialization:
	// export the instantiation support userfuncts
	hal_export_xfunct_args_t ni = {
	    .type = FS_USERLAND,
	    .funct.u = create_instance,
	    .arg = NULL,
	    .owner_id = lib_module_id
	};
	if ((retval = hal_export_xfunctf( &ni, "newinst")) < 0)
	    return retval;

	hal_export_xfunct_args_t di = {
	    .type = FS_USERLAND,
	    .funct.u = delete_instance,
	    .arg = NULL,
	    .owner_id = lib_module_id
	};
	if ((retval = hal_export_xfunctf( &di, "delinst")) < 0)
	    return retval;
#endif
	retval = hal_ready(lib_module_id);
	if (retval)
	    HALERR("hal_ready(%d) failed rc=%d", lib_module_id, retval);
	else
	    HALDBG("%s initialization complete", hal_name);
	return retval;
    }

    HALDBG("%s component '%s' id=%d initialized%s",
	   (ctor != NULL) ? "instantiable" : "legacy",
	   hal_name, comp_id,
	   (dtor != NULL) ? ", has destructor" : "");
    return comp_id;
}


int halg_exit(const int use_hal_mutex, int comp_id)
{
    int comptype;

    CHECK_HALDATA();

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_comp_t *comp = halpr_find_comp_by_id(comp_id);

	if (comp == NULL) {
	    HALERR("no such component with id %d", comp_id);
	    return -EINVAL;
	}

	HALDBG("removing component %d '%s'", comp_id, ho_name(comp));

	// record type, since we're about to zap the comp in free_comp_struct()
	// which would be a dangling reference
	comptype = comp->type;

	// get rid of the component
	// this frees all dependent objects before releasing the comp
	// descriptor per se: functs, pins, params, instances, and in
	// turn, dependent objects of instances
	free_comp_struct(comp);

    } // scope exit - HAL mutex released

    // if unloading the hal_lib component, destroy HAL shm.
    // this must happen without the HAL mutex, because the
    // HAL mutex lives in the very shm segment which we're
    // about to release, which will it make impossible to
    // release the mutex (and leave the HAL shm locked
    // for other processes
    if (comptype == TYPE_HALLIB) {
	int retval;

	/* release RTAPI resources */
	retval = rtapi_shmem_delete(lib_mem_id, comp_id);
	if (retval) {
	    HALERR("rtapi_shmem_delete(%d,%d) failed: %d",
		   lib_mem_id, comp_id, retval);
	}
	// HAL shm is history, take note ASAP
	lib_mem_id = -1;
	hal_shmem_base = NULL;
	hal_data = NULL;;

	retval = rtapi_exit(comp_id);
	if (retval) {
	    HALERR("rtapi_exit(%d) failed: %d",
		   lib_module_id, retval);
	}
	// the hal_lib RTAPI module is history, too
	// in theory we'd be back to square 1
	lib_module_id = -1;

    } else {
	// the standard case
	rtapi_exit(comp_id);
    }
    return 0;
}

int halg_ready(const int use_hal_mutex, int comp_id)
{
    WITH_HAL_MUTEX_IF(use_hal_mutex);

    hal_comp_t *comp = halpr_find_comp_by_id(comp_id);
    if (comp == NULL) {
	HALERR("component %d not found", comp_id);
	return -EINVAL;
    }

    if(comp->state > COMP_INITIALIZING) {
	HALERR("component '%s' id %d already ready (state %d)",
	       ho_name(comp), ho_id(comp), comp->state);
        return -EINVAL;
    }
    comp->state = (comp->type == TYPE_REMOTE ?  COMP_UNBOUND : COMP_READY);
    return 0;
}

const char *hal_comp_name(int comp_id)
{
    WITH_HAL_MUTEX();
    hal_comp_t *comp = halpr_find_comp_by_id(comp_id);
    return (comp == NULL) ? NULL : ho_name(comp);
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

    HAL_ASSERT(ho_type(inst) == HAL_INST);

    // found the instance. Retrieve its owning comp:
    comp =  halpr_find_comp_by_id(ho_owner_id(inst));
    if (comp == NULL) {
	// really bad. an instance which has no owning comp?
	HALERR("BUG: instance %s/%d's comp_id %d refers to a non-existant comp",
	       ho_name(inst), ho_id(inst), ho_owner_id(inst));
    }

    HAL_ASSERT(ho_type(comp) == HAL_COMPONENT);

    return comp;
}


int free_comp_struct(hal_comp_t * comp)
{

    // dont exit if the comp is still reference, eg a remote comp
    // served by haltalk:
    if (ho_referenced(comp)) {
	HALERR("not exiting comp %s - still referenced (refcnt=%d)",
	       ho_name(comp), ho_refcnt(comp));
	return -EBUSY;
    }

    /* can't delete the component until we delete its "stuff" */
    /* need to check for functs only if a realtime component */
#ifdef RTAPI
    // first unlink and destroy all functs, so an RT thread
    // cant trample on the comp while it's being destroyed

    foreach_args_t args =  {
	// search for functs owned by this comp
	.type = HAL_FUNCT,
	.owner_id  = ho_id(comp),
    };
    halg_foreach(0, &args, yield_free);

    // here, technically all the comp's functs are
    // delf'd and not visible anymore

    // now that the funct is gone,
    // exit all the comp's instances
    foreach_args_t iargs =  {
	// search for insts owned by this comp
	.type = HAL_INST,
	.owner_id  = ho_id(comp),
    };
    halg_foreach(0, &iargs, yield_free);

    // here all insts, their pins, params and functs are gone.

#endif /* RTAPI */

    // now work the legacy pins and params which are
    // directly owned by the comp.
    foreach_args_t pinargs =  {
	// wipe params owned by this comp
	.type = HAL_PIN,
	.owner_id  = ho_id(comp),
    };
    halg_foreach(0, &pinargs, yield_free);

    foreach_args_t paramargs =  {
	// wipe params owned by this comp
	.type = HAL_PARAM,
	.owner_id  = ho_id(comp),
    };
    halg_foreach(0, &paramargs, yield_free);

    //  now we can delete the component itself.
    halg_free_object(false, (hal_object_ptr)comp);
    return 0;
}

#ifdef RTAPI

// instantiation handlers
static int create_instance(const hal_funct_args_t *fa)
{
    const int argc = fa_argc(fa);
    const char **argv = fa_argv(fa);

#if 0
    HALDBG("'%s' called, arg=%p argc=%d",
	   fa_funct_name(fa), fa_arg(fa), argc);
    int i;
    for (i = 0; i < argc; i++)
	HALDBG("    argv[%d] = \"%s\"", i,argv[i]);
#endif

    if (argc < 2) {
	HALERR("need component name and instance name");
	return -EINVAL;
    }
    const char *cname = argv[0];
    const char *iname = argv[1];

    hal_comp_t *comp = halpr_find_comp_by_name(cname);
    if (!comp) {
	HALERR("no such component '%s'", cname);
	return -EINVAL;
    }
    if (!comp->ctor) {
	HALERR("component '%s' not instantiable", cname);
	return -EINVAL;
    }
    hal_inst_t *inst = halpr_find_inst_by_name(iname);
    if (inst) {
	HALERR("instance '%s' already exists", iname);
	return -EBUSY;
    }
    return comp->ctor(iname, argc - 2, &argv[2]);
}

static int delete_instance(const hal_funct_args_t *fa)
{
    const int argc = fa_argc(fa);
    const char **argv = fa_argv(fa);

    HALDBG("'%s' called, arg=%p argc=%d",
	   fa_funct_name(fa), fa_arg(fa), argc);
    int i;
    for (i = 0; i < argc; i++)
	HALDBG("    argv[%d] = \"%s\"", i, argv[i]);
    if (argc < 1) {
	HALERR("no instance name given");
	return -EINVAL;
    }
    return hal_inst_delete(argv[0]);
}


/** init_hal_data() initializes the entire HAL data structure,
    by the RT hal_lib component
*/
int init_hal_data(void)
{
    /* has the block already been initialized? */
    if (hal_data->version != 0) {
	/* yes, verify version code */
	if (hal_data->version == HAL_VER) {
	    return 0;
	} else {
	    HALERR("version code mismatch");
	    return -1;
	}
    }
    /* no, we need to init it, grab the mutex unconditionally */
    rtapi_mutex_try(&(hal_data->mutex));

    // some heaps contain garbage, like xenomai
    memset(hal_data, 0, global_data->hal_size);

    /* set version code so nobody else init's the block */
    hal_data->version = HAL_VER;

    /* initialize everything */
    dlist_init_entry(&(hal_data->halobjects));
    dlist_init_entry(&(hal_data->funct_entry_free));
    dlist_init_entry(&(hal_data->threads));

    hal_data->base_period = 0;
    hal_data->exact_base_period = 0;

    hal_data->threads_running = 0;

    RTAPI_ZERO_BITMAP(&hal_data->rings, HAL_MAX_RINGS);
    RTAPI_BIT_SET(hal_data->rings,0);

    /* set up for shmalloc_xx() */
    hal_data->shmem_bot = sizeof(hal_data_t);
    hal_data->shmem_top = global_data->hal_size;
    hal_data->lock = HAL_LOCK_NONE;

    int i;
    for (i = 0; i < MAX_EPSILON; i++)
	hal_data->epsilon[i] = 0.0;
    hal_data->epsilon[0] = DEFAULT_EPSILON;

    // initial heap allocation
    rtapi_heap_init(&hal_data->heap);
    heap_addmem(HAL_HEAP_INITIAL);
    rtapi_heap_setflags(&hal_data->heap, -1);
    rtapi_heap_setloghdlr(&hal_data->heap, rtapi_get_msg_handler());

    /* done, release mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}
#endif
