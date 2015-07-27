
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"
#include "hal_ring.h"		/* HAL ringbuffer decls */

static int next_ring_id(void);
/* static int hal_ring_newfv(int size, int sp_size, int flags, */
/* 			  const char *fmt, va_list ap); */
/* static int hal_ring_deletefv(const char *fmt, va_list ap); */
/* static int hal_ring_attachfv(ringbuffer_t *rb, unsigned *flags, */
/* 			     const char *fmt, va_list ap); */
/* static int hal_ring_detachfv(ringbuffer_t *rb, const char *fmt, va_list ap); */

/***********************************************************************
*                     Public HAL ring functions                        *
************************************************************************/
hal_ring_t *halg_ring_newfv(const int use_hal_mutex,
			    const int size,
			    const int sp_size,
			    const int mode,
			    const char *fmt,
			    const va_list ap)
{
    int retval;
    int ring_id;
    ringheader_t *rhptr;

    PCHECK_HALDATA();
    PCHECK_STR(fmt);
    PCHECK_LOCK(HAL_LOCK_LOAD);

    char *name, buf[HAL_MAX_NAME_LEN + 1];
    name = fmt_ap(buf, sizeof(buf), fmt, ap);
    if (name == NULL)
	return NULL;

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);

	// make sure no such ring name already exists
	hal_ring_t *rptr = halg_find_object_by_name(0, HAL_RING, name).ring;
	if (rptr != NULL) {
	    HALERR("ring '%s' already exists", name);
	    _halerrno = -EEXIST;
	    return NULL;
	}

	// allocate ring descriptor
	if ((rptr = halg_create_object(0, sizeof(hal_ring_t),
					 HAL_RING, 0, name)) == NULL) {
	    return NULL; // _halerrno set in halg_create_object
	}
	// allocate a new ring id - needed since we dont track ring shm
	// segments in RTAPI
	if ((ring_id = next_ring_id()) < 0) {
	    goto FAIL; // _halerrno set in next_ring_id
	}

	rptr->flags = mode;
	rptr->ring_id = ring_id;

	// make total allocation fit ringheader, ringbuffer and scratchpad
	rptr->total_size = ring_memsize( rptr->flags, size, sp_size);

	if (rptr->flags & ALLOC_HALMEM) {
	    void *ringmem = shmalloc_desc(rptr->total_size);
	    if (ringmem == NULL) {
		HALERR("ring '%s' size %d - insufficient HAL memory for ring",
		      name,rptr->total_size);
		_halerrno = -ENOMEM;
		goto FAIL;
	    }
	    rptr->ring_offset = SHMOFF(ringmem);
	    rhptr = ringmem;
	} else {
	    // allocate shared memory segment for ring and init
	    rptr->ring_shmkey = OS_KEY((RTAPI_RING_SHM_KEY + ring_id), rtapi_instance);

	    int shmid;

	    // allocate an RTAPI shm segment owned by HAL_LIB_xxx
	    if ((shmid = rtapi_shmem_new(rptr->ring_shmkey, lib_module_id,
					 rptr->total_size)) < 0) {
		HALERR("rtapi_shmem_new(0x%8.8x,%d) failed: %d",
		       rptr->ring_shmkey, lib_module_id,
		       rptr->total_size);
		_halerrno = shmid;
		goto FAIL;
	    }
	    // map the segment now so we can fill in the ringheader details
	    if ((retval = rtapi_shmem_getptr(shmid,
					     (void **)&rhptr, 0)) < 0) {
		HALERR("rtapi_shmem_getptr for %d failed %d",
		       shmid, retval);
		_halerrno = retval;
		goto FAIL;
	    }
	}

	HALDBG("created ring '%s' in %s, total_size=%d",
	       name, (rptr->flags & ALLOC_HALMEM) ? "halmem" : "shm",
	       rptr->total_size);

	ringheader_init(rhptr, rptr->flags, size, sp_size);
	rhptr->refcount = 0; // on hal_ring_attach: increase; on hal_ring_detach: decrease

	// make it visible
	halg_add_object(false, (hal_object_ptr)rptr);
	return rptr;

    FAIL:
	if (rptr)
	    shmfree_desc(rptr);
	return NULL;
    } // automatic unlock by scope exit
}


int free_ring_struct(hal_ring_t *hrptr)
{
    ringheader_t *rhptr;
    int shmid = -1;
    int retval;

    if (hrptr->flags & ALLOC_HALMEM) {
	// ring exists as HAL memory.
	rhptr = SHMPTR(hrptr->ring_offset);
    } else {
	// ring exists as shm segment. Retrieve shared memory address.
	if ((shmid = rtapi_shmem_new_inst(hrptr->ring_shmkey,
					  rtapi_instance, lib_module_id,
					  0 )) < 0) {
	    if (shmid != -EEXIST)  {
		HALERR("ring '%s': rtapi_shmem_new_inst() failed %d",
		       ho_name(hrptr), shmid);
		return shmid;
	    }
	}
	if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr, 0))) {
	    HALERR("ring '%s': rtapi_shmem_getptr %d failed %d",
		   ho_name(hrptr), shmid, retval);
	    return -ENOMEM;
	}
    }
    // assure attach/detach balance is zero:
    if (rhptr->refcount) {
	HALERR("ring '%s' still attached - refcount=%d",
	       ho_name(hrptr), rhptr->refcount);
	return -EBUSY;
    }

    HALDBG("deleting ring '%s'", ho_name(hrptr));
    if (hrptr->flags & ALLOC_HALMEM) {
	shmfree_desc(rhptr);
    } else {
	if ((retval = rtapi_shmem_delete(shmid, lib_module_id)) < 0)  {
	    HALERR("ring '%s': rtapi_shmem_delete(%d,%d) failed: %d",
		   ho_name(hrptr), shmid, lib_module_id, retval);
	    return retval;
	}
    }
    // free descriptor. May return -EBUSY if ring referenced.
    return halg_free_object(false, (hal_object_ptr)hrptr);
}


/* int hal_ring_deletef(const char *fmt, ...) */
/* { */
/*     va_list ap; */
/*     int ret; */
/*     va_start(ap, fmt); */
/*     ret = hal_ring_deletefv(fmt, ap); */
/*     va_end(ap); */
/*     return ret; */
/* } */


int halg_ring_deletefv(const int use_hal_mutex,
		       const char *fmt,
		       const va_list ap)
{

    CHECK_HALDATA();
    CHECK_STR(fmt);
    char *name, buf[HAL_MAX_NAME_LEN + 1];
    name = fmt_ap(buf, sizeof(buf), fmt, ap);
    if (name == NULL)
	return _halerrno;

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);

	// ring must exist
	hal_ring_t *rptr = halg_find_object_by_name(0, HAL_RING, name).ring;
	if (rptr  == NULL) {
	    HALERR("ring '%s' not found", name);
	    return -ENOENT;
	}
	free_ring_struct(rptr);
    }
    return 0;
}

/* int hal_ring_attachf(ringbuffer_t *rb, unsigned *flags, const char *fmt, ...) */
/* { */
/*     va_list ap; */
/*     int ret; */
/*     va_start(ap, fmt); */
/*     ret = hal_ring_attachfv(rb, flags, fmt, ap); */
/*     va_end(ap); */
/*     return ret; */
/* } */

int halg_ring_attachfv(const int use_hal_mutex,
		       ringbuffer_t *rbptr,
		       unsigned *flags,
		       const char *fmt,
		       const va_list ap)
{
    CHECK_HALDATA();
    CHECK_STR(fmt);

    char *name, buf[HAL_MAX_NAME_LEN + 1];
    name = fmt_ap(buf, sizeof(buf), fmt, ap);
    if (name == NULL)
	return _halerrno;
    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	int retval;
	ringheader_t *rhptr;

	hal_ring_t *rptr = halg_find_object_by_name(0, HAL_RING, name).ring;
	if (rptr == NULL) {
	    HALERR("no such ring '%s'", name);
	    return -ENOENT;
	}

	// calling hal_ring_attach(name, NULL, NULL) is a way to determine
	// if a given ring exists.
	// hal_ring_attach(name, NULL, &flags) is a way to inspect the flags
	// of an existing ring without actually attaching it.
	if (rptr == NULL) {
	    if (flags)
		*flags = rptr->flags;
	    return 0;
	}

	if (rptr->flags & ALLOC_HALMEM) {
	    rhptr = SHMPTR(rptr->ring_offset);
	} else {
	    int shmid;

	    // map in the shm segment - size 0 means 'must exist'
	    if ((retval = rtapi_shmem_new_inst(rptr->ring_shmkey,
					       rtapi_instance, lib_module_id,
					   0 )) < 0) {
		if (retval != -EEXIST)  {
		    HALERR("ring '%s': rtapi_shmem_new_inst() failed %d",
			   name, retval);
		    return retval;
		}
		// tried to map shm again. May happen in halcmd_commands:print_ring_info().
		// harmless.
	    }
	    shmid = retval;

	    // make it accessible
	    if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr, 0))) {
		HALERR("ring '%s': rtapi_shmem_getptr %d failed %d",
		       name, shmid, retval);
		return -ENOMEM;
	    }
	}
	// record usage in ringheader
	rhptr->refcount++;
	// fill in ringbuffer_t
	ringbuffer_init(rhptr, rbptr);

	if (flags)
	    *flags = rptr->flags;
	// hal mutex unlock happens automatically on scope exit
    }
    return 0;
}


int halg_ring_detachfv(const int use_hal_mutex,
		       ringbuffer_t *rbptr,
		       const char *fmt,
		       va_list ap)
{

    CHECK_HALDATA();
    CHECK_STR(fmt);
    CHECK_LOCK(HAL_LOCK_CONFIG);

    char *name, buf[HAL_MAX_NAME_LEN + 1];
    name = fmt_ap(buf, sizeof(buf), fmt, ap);
    if (name == NULL)
	return _halerrno;

    if ((rbptr == NULL) || (rbptr->magic != RINGBUFFER_MAGIC)) {
	HALERR("ring '%s': invalid ringbuffer", name);
	return -EINVAL;
    }

    // no mutex(es) held up to here
    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);

	ringheader_t *rhptr = rbptr->header;
	rhptr->refcount--;
	rbptr->magic = 0;  // invalidate FIXME
    }
    return 0;
}

/***********************************************************************
*                    Internal HAL ring support functions               *
************************************************************************/

// we manage ring shm segments through a bitmap visible in hal_data
// instead of going through separate (and rather useless) RTAPI methods
// this removes both reliance on a visible RTAPI data segment, which the
// userland flavors dont have, and simplifies RTAPI.
// the shared memory key of a given ring id and instance is defined as
// OS_KEY(RTAPI_RING_SHM_KEY+id, instance).

static int next_ring_id(void)
{
    int i;
    for (i = 0; i < HAL_MAX_RINGS; i++) {
	if (!RTAPI_BIT_TEST(hal_data->rings,i)) {  // unused
	    RTAPI_BIT_SET(hal_data->rings,i);      // allocate
	    return i;
	}
    }
    HALERR("out of ring id's, HAL_MAX_RINGS = %d",HAL_MAX_RINGS);
    _halerrno = -ENOENT;
    return -ENOENT; // no more slots available
}

/* // varargs helpers */
/* static int hal_ring_newfv(int size, int sp_size, int flags, */
/* 			  const char *fmt, va_list ap) */
/* { */
/*     char name[HAL_NAME_LEN + 1]; */
/*     int sz; */
/*     sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap); */
/*     if(sz == -1 || sz > HAL_NAME_LEN) { */
/* 	HALERR("length %d too long for name starting '%s'", */
/* 	       sz, name); */
/*         return -ENOMEM; */
/*     } */
/*     return halg_ring_new(1, name, size, sp_size, flags); */
/* } */


/* static int hal_ring_deletefv(const char *fmt, va_list ap) */
/* { */
/*     char name[HAL_NAME_LEN + 1]; */
/*     int sz; */
/*     sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap); */
/*     if(sz == -1 || sz > HAL_NAME_LEN) { */
/* 	HALERR("length %d too long for name starting '%s'", */
/* 	       sz, name); */
/*         return -ENOMEM; */
/*     } */
/*     return halg_ring_delete(1, name); */
/* } */

/* static int hal_ring_attachfv(ringbuffer_t *rb, */
/* 			     unsigned *flags, */
/* 			     const char *fmt, */
/* 			     va_list ap) */
/* { */
/*     char name[HAL_NAME_LEN + 1]; */
/*     int sz; */
/*     sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap); */
/*     if(sz == -1 || sz > HAL_NAME_LEN) { */
/* 	HALERR("length %d too long for name starting '%s'", */
/* 	       sz, name); */
/*         return -ENOMEM; */
/*     } */
/*     return halg_ring_attach(1, name, rb, flags); */
/* } */

/* static int hal_ring_detachfv(ringbuffer_t *rb, const char *fmt, va_list ap) */
/* { */
/*     char name[HAL_NAME_LEN + 1]; */
/*     int sz; */
/*     sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap); */
/*     if(sz == -1 || sz > HAL_NAME_LEN) { */
/* 	HALERR("length %d too long for name starting '%s'", */
/* 	       sz, name); */
/*         return -ENOMEM; */
/*     } */
/*     return halg_ring_detach(1,name, rb); */
/* } */

#ifdef RTAPI

EXPORT_SYMBOL(halg_ring_newfv);
EXPORT_SYMBOL(halg_ring_deletefv);
EXPORT_SYMBOL(halg_ring_attachfv);
EXPORT_SYMBOL(halg_ring_detachfv);

#endif
