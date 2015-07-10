
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

#if defined(ULAPI)
#include <stdio.h>
#include <sys/types.h>		/* pid_t */
#include <unistd.h>		/* getpid() */
#endif

static hal_vtable_t *alloc_vtable_struct(void);
static void free_vtable_struct(hal_vtable_t *c);

/***********************************************************************
*                     Public HAL vtable functions                       *
************************************************************************/
int hal_export_vtable(const char *name, int version, void *vtref, int comp_id)
{
    CHECK_HALDATA();
    CHECK_STRLEN(name, HAL_NAME_LEN);
    CHECK_NULL(vtref);
    CHECK_LOCK(HAL_LOCK_LOAD);

    HALDBG("exporting vtable '%s' version=%d owner=%d at %p",
	   name, version, comp_id, vtref);

    {
	WITH_HAL_MUTEX();
	hal_vtable_t *vt;

	// make sure no such vtable name already exists
	if ((vt = halpr_find_vtable_by_name(name, version)) != 0) {
	    HALERR("vtable '%s' already exists", name);
	    return -EEXIST;
	}

	// allocate a new vtable descriptor in the HAL shm segment
	if ((vt = alloc_vtable_struct()) == NULL)
	    NOMEM("vtable '%s'",  name);

	hh_init_hdrf(&vt->hdr, HAL_VTABLE, comp_id, "%s", name);
	vt->refcount = 0;
	vt->vtable  =  vtref;
	vt->version =  version;
#ifdef RTAPI
	vt->context = 0;  // this is in RTAPI, and can be shared
#else
	vt->context = getpid(); // in per-process memory, no shareable code
#endif
	dlist_add_before(&vt->hdr.links, OBJECTLIST);

	HALDBG("created vtable '%s' vtable=%p version=%d",
	       hh_get_name(&vt->hdr), vt->vtable, vt->version);

	// automatic unlock by scope exit
	return hh_get_id(&vt->hdr);
    }
}


int hal_remove_vtable(int vtable_id)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_LOAD);

    {
	WITH_HAL_MUTEX();
	hal_vtable_t *vt;

	// make sure no such vtable name already exists
	if ((vt = halpr_find_vtable_by_id(vtable_id)) == NULL) {
	    HALERR("vtable %d not found", vtable_id);
	    return -ENOENT;
	}
	// still referenced?
	if (vt->refcount > 0) {
	    HALERR("vtable %d busy (refcount=%d)",
		   vtable_id, vt->refcount);
	    return -ENOENT;

	}
	dlist_remove_entry(&vt->hdr.links);
	free_vtable_struct(vt);
	HALDBG("vtable %s/%d version %d removed",
	       hh_get_name(&vt->hdr), vtable_id,  vt->version);
	return 0;
    }
}

// returns vtable_id (handle) or error code
// increases refcount
int hal_reference_vtable(const char *name, int version, void **vtableref)
{
    CHECK_HALDATA();
    CHECK_STRLEN(name, HAL_NAME_LEN);
    CHECK_NULL(vtableref);
    CHECK_LOCK(HAL_LOCK_LOAD);

    {
	WITH_HAL_MUTEX();
	hal_vtable_t *vt;

	// make sure no such vtable name already exists
	if ((vt = halpr_find_vtable_by_name(name, version)) == NULL) {
	    HALERR("vtable '%s' version %d not found", name, version);
	    return -ENOENT;
	}

	// make sure it's in the proper context
#ifdef RTAPI
	int context = 0;  // this is in RTAPI, and can be shared
#else
	int context = getpid(); // in per-process memory, no shareable code
#endif
	if (vt->context != context) {
	    HALERR("vtable %s version %d: "
		   "context mismatch - found context %d",
		   name, version, vt->context);
	    return -ENOENT;
	}

	vt->refcount += 1;
	*vtableref = vt->vtable;
	HALDBG("vtable %s,%d found vtable=%p context=%d",
	       hh_get_name(&vt->hdr), vt->version, vt->vtable, vt->context);

	// automatic unlock by scope exit
	return hh_get_id(&vt->hdr);
    }
}

// drops refcount
int hal_unreference_vtable(int vtable_id)
{
    CHECK_HALDATA();
    {
	WITH_HAL_MUTEX();
	hal_vtable_t *vt;

	// make sure no such vtable name already exists
	if ((vt = halpr_find_vtable_by_id(vtable_id)) == NULL) {
	    HALERR("vtable %d not found", vtable_id);
	    return -ENOENT;
	}

	// make sure it's in the proper context
#ifdef RTAPI
	int context = 0;  // this is in RTAPI, and can be shared
#else
	int context = getpid(); // in per-process memory, no shareable code
#endif
	if (vt->context != context) {
	    HALERR("vtable %s/%d: "
		   "context mismatch - calling context %d vtable context %d",
		   hh_get_name(&vt->hdr), vtable_id, context, vt->context);
	    return -ENOENT;
	}

	vt->refcount -= 1;
	HALDBG("vtable %s/%d refcount=%d",
	       hh_get_name(&vt->hdr),
	       vtable_id, vt->refcount);

	// automatic unlock by scope exit
	return 0;
    }
}

// private HAL API

hal_vtable_t *halpr_find_vtable_by_name(const char *name, int version)
{
    foreach_args_t args =  {
	.type = HAL_VTABLE,
	.name = name,
	.user_arg1 = version,
	.user_ptr1 = NULL
    };
    if (halg_foreach(false, &args, yield_versioned_vtable_object) == 1)
	return args.user_ptr1;
    return NULL;
}

hal_vtable_t *halpr_find_vtable_by_id(int vtable_id)
{
    foreach_args_t args =  {
	.type = HAL_VTABLE,
	.id = vtable_id,
	.user_ptr1 = NULL
    };
    if (halg_foreach(false, &args, yield_match) == 1)
	return args.user_ptr1;
    return NULL;
}

static hal_vtable_t *alloc_vtable_struct(void)
{
    HALDBG("FIXME");
    hal_vtable_t *p = shmalloc_dn(sizeof(hal_vtable_t));
    return p;
}

static void free_vtable_struct(hal_vtable_t * p)
{
    HALDBG("FIXME");
}


#ifdef RTAPI

EXPORT_SYMBOL(hal_export_vtable);
EXPORT_SYMBOL(hal_remove_vtable);
EXPORT_SYMBOL(hal_reference_vtable);
EXPORT_SYMBOL(hal_unreference_vtable);
EXPORT_SYMBOL(halpr_find_vtable_by_name);
EXPORT_SYMBOL(halpr_find_vtable_by_id);
#endif
