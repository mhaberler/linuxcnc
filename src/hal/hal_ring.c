
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_ring.h"		/* HAL ringbuffer decls */


static hal_ring_t *alloc_ring_struct(void);
static void free_ring_struct(hal_ring_t * p);
static int next_ring_id(void);

/***********************************************************************
*                     Public HAL ring functions                        *
************************************************************************/
int hal_ring_new(const char *name, int size, int sp_size, int module_id, int flags)
{
    hal_ring_t *rbdesc;
    int *prev, next, cmp, retval;
    int ring_id;
    ringheader_t *rhptr;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_new called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (!name) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_new() called with NULL name\n",
			rtapi_instance);
    }
    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: ring name '%s' is too long\n",
			rtapi_instance, name);
	return -EINVAL;
    }
    {
	hal_ring_t *ptr __attribute__((cleanup(halpr_autorelease_mutex)));

	rtapi_mutex_get(&(hal_data->mutex));

	if (hal_data->lock & HAL_LOCK_LOAD)  {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: hal_ring_new called while HAL locked\n",
			    rtapi_instance);
	    return -EPERM;
	}

	// make sure no such ring name already exists
	if ((ptr = halpr_find_ring_by_name(name)) != 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: ring '%s' already exists\n",
			    rtapi_instance, name);
	    return -EEXIST;
	}
	// allocate a new ring id - needed since we dont track ring shm
	// segments in RTAPI
	if ((ring_id = next_ring_id()) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d cant allocate new ring id for '%s'\n",
			    rtapi_instance, name);
	    return -ENOMEM;
	}

	// allocate a new ring descriptor
	if ((rbdesc = alloc_ring_struct()) == 0) {
	    // alloc failed
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: insufficient memory for ring '%s'\n", name);
	    return -ENOMEM;
	}

	rbdesc->hrflags = 0;
	rbdesc->ring_id = ring_id;

	// make total allocation fit ringheader, ringbuffer and scratchpad
	rbdesc->total_size = ring_memsize( flags, size, sp_size);

	rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d creating ring '%s' total_size=%d\n",
			rtapi_instance, name, rbdesc->total_size);

	if (flags & ALLOC_HALMEM) {
	    rbdesc->hrflags |= ALLOC_HALMEM;
	    void *ringmem = shmalloc_up(rbdesc->total_size);
	    if (ringmem == NULL) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: insufficient HAL memory for ring '%s'\n", name);
		return -ENOMEM;
	    }
	    rbdesc->ring_offset = SHMOFF(ringmem);
	    rhptr = ringmem;
	} else {
	    // allocate shared memory segment for ring and init
	    rbdesc->ring_shmkey = OS_KEY((RTAPI_RING_SHM_KEY + ring_id), rtapi_instance);

	    int shmid;

	    // allocate an RTAPI shm segment owned by the allocating module
	    if ((shmid = rtapi_shmem_new(rbdesc->ring_shmkey, module_id,
					 rbdesc->total_size)) < 0) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d hal_ring_new: rtapi_shmem_new(0x%8.8x,%d) failed: %d\n",
				rtapi_instance,
				rbdesc->ring_shmkey, module_id,
				rbdesc->total_size);
		return  -ENOMEM;
	    }

	    // map the segment now so we can fill in the ringheader details
	    if ((retval = rtapi_shmem_getptr(shmid,
					     (void **)&rhptr)) < 0) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d hal_ring_new: rtapi_shmem_getptr for %d failed %d\n",
				rtapi_instance, shmid, retval);
		return -ENOMEM;
	    }
	}
	ringheader_init(rhptr, flags, size, sp_size);
	rhptr->refcount = 0; // on hal_ring_attach: increase; on hal_ring_detach: decrease
	rtapi_snprintf(rbdesc->name, sizeof(rbdesc->name), "%s", name);
	rbdesc->next_ptr = 0;
	rbdesc->owner = module_id;

	// search list for 'name' and insert new structure
	prev = &(hal_data->ring_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		rbdesc->next_ptr = next;
		*prev = SHMOFF(rbdesc);
		return 0;
	    }
	    ptr = SHMPTR(next);
	    cmp = strcmp(ptr->name, rbdesc->name);
	    if (cmp > 0) {
		/* found the right place for it, insert here */
		rbdesc->next_ptr = next;
		*prev = SHMOFF(rbdesc);
		return 0;
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
	// automatic unlock by scope exit
    }
}

int hal_ring_delete(const char *name, int module_id)
{
    int retval;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_delete called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (!name) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_delete() called with NULL name\n",
			rtapi_instance);
    }
    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: ring name '%s' is too long\n",
			rtapi_instance, name);
	return -EINVAL;
    }
    {
	hal_ring_t *hrptr __attribute__((cleanup(halpr_autorelease_mutex)));

	rtapi_mutex_get(&(hal_data->mutex));

	if (hal_data->lock & HAL_LOCK_LOAD)  {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: hal_ring_delete called while HAL locked\n",
			    rtapi_instance);
	    return -EPERM;
	}

	// ring must exist
	if ((hrptr = halpr_find_ring_by_name(name)) == 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: ring '%s' not found\n",
			    rtapi_instance, name);
	    return -ENOENT;
	}

	ringheader_t *rhptr;
	int shmid;

	if (hrptr->hrflags & ALLOC_HALMEM) {
	    // ring exists as HAL memory.
	    rhptr = SHMPTR(hrptr->ring_offset);
	} else {
	    // ring exists as shm segment. Retrieve shared memory address.
	    if ((shmid = rtapi_shmem_new_inst(hrptr->ring_shmkey,
					      rtapi_instance, module_id,
					      0 )) < 0) {
		if (shmid != -EEXIST)  {
		    rtapi_print_msg(RTAPI_MSG_WARN,
				    "HAL:%d hal_ring_delete(%s): rtapi_shmem_new_inst() failed %d\n",
				    rtapi_instance, name, shmid);
		    return shmid;
		}
	    }
	    if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr))) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d hal_ring_delete: rtapi_shmem_getptr %d failed %d\n",
				rtapi_instance, shmid, retval);
		return -ENOMEM;
	    }
	}
	// assure attach/detach balance is zero:
	if (rhptr->refcount) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_delete: '%s' still attached - refcount=%d\n",
			    rtapi_instance, name, rhptr->refcount);
	    return -EBUSY;
	}

	rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d deleting ring '%s'\n",
			rtapi_instance, name);
	if (hrptr->hrflags & ALLOC_HALMEM) {
	    ; // if there were a HAL memory free function, call it here
	} else {
	    if ((retval = rtapi_shmem_delete(shmid, module_id)) < 0)  {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d hal_ring_delete(%s): rtapi_shmem_delete(%d,%d) failed: %d\n",
				rtapi_instance, name, shmid, module_id, retval);
		return retval;
	    }
	}
	// search for the ring (again..)
	int *prev = &(hal_data->ring_list_ptr);
	int next = *prev;
	while (next != 0) {
	    hrptr = SHMPTR(next);
	    if (strcmp(hrptr->name, name) == 0) {
		// this is the right ring
		// unlink from list
		*prev = hrptr->next_ptr;
		// and delete it, linking it on the free list
		free_ring_struct(hrptr);
		return 0;
	    }
	    // no match, try the next one
	    prev = &(hrptr->next_ptr);
	    next = *prev;
	}

	rtapi_print_msg(RTAPI_MSG_ERR, "HAL:%d BUG: deleting ring '%s'; not found in ring_list?\n",
			rtapi_instance, name);
	return -ENOENT;
    }

}

int hal_ring_attach(const char *name, ringbuffer_t *rbptr, int module_id)
{
    hal_ring_t *rbdesc;
    ringheader_t *rhptr;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: hal_ring_attach called before init\n");
	return -EINVAL;
    }

    // no mutex(es) held up to here
    {
	int retval  __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	if ((rbdesc = halpr_find_ring_by_name(name)) == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach: no such ring '%s'\n",
			    rtapi_instance, name);
	    return -EINVAL;
	}

	if (rbdesc->hrflags & ALLOC_HALMEM) {
	    rhptr = SHMPTR(rbdesc->ring_offset);
	} else {
	    int shmid;

	    // map in the shm segment - size 0 means 'must exist'
	    if ((shmid = rtapi_shmem_new_inst(rbdesc->ring_shmkey,
					       rtapi_instance, module_id,
					   0 )) < 0) {
		if (retval != -EEXIST)  {
		    rtapi_print_msg(RTAPI_MSG_WARN,
				    "HAL:%d hal_ring_attach(%s): rtapi_shmem_new_inst() failed %d\n",
				    rtapi_instance, name, retval);
		    return retval;
		}
		// tried to map shm again. May happen in halcmd_commands:print_ring_info().
		// harmless.
	    }
	    // make it accessible
	    if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr))) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d hal_ring_attach: rtapi_shmem_getptr %d failed %d\n",
				rtapi_instance, shmid, retval);
		return -ENOMEM;
	    }
	}
	// record usage in ringheader
	rhptr->refcount++;
	// fill in ringbuffer_t
	ringbuffer_init(rhptr, rbptr);

	// hal mutex unlock happens automatically on scope exit
    }
    return 0;
}

int hal_ring_detach(const char *name, ringbuffer_t *rbptr)
{

    if ((rbptr == NULL) || (rbptr->magic != RINGBUFFER_MAGIC)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach: invalid ringbuffer\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    // no mutex(es) held up to here
    {
	int retval __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	ringheader_t *rhptr = rbptr->header;
	rhptr->refcount--;
	rbptr->magic = 0;  // invalidate FIXME

	// unlocking happens automatically on scope exit
    }
    return 0;
}

//NB: not HAL lock
hal_ring_t *halpr_find_ring_by_name(const char *name)
{
    int next;
    hal_ring_t *ring;

    /* search ring list for 'name' */
    next = hal_data->ring_list_ptr;
    while (next != 0) {
	ring = SHMPTR(next);
	if (strcmp(ring->name, name) == 0) {
	    /* found a match */
	    return ring;
	}
	/* didn't find it yet, look at next one */
	next = ring->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
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
    int i, ring_shmkey;
    for (i = 0; i < HAL_MAX_RINGS; i++) {
	if (!RTAPI_BIT_TEST(hal_data->rings,i)) {  // unused
#if 1  // paranoia
	    ring_shmkey = OS_KEY(HAL_KEY, rtapi_instance);
	    // test if foreign instance exists
	    if (!rtapi_shmem_exists(ring_shmkey)) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL_LIB:%d BUG: next_ring_id(%d) - shm segment exists (%x)\n",
				rtapi_instance, i, ring_shmkey);
		return -EEXIST;
	    }
#endif
	    RTAPI_BIT_SET(hal_data->rings,i);      // allocate
	    return i;
	}
    }
    return -EBUSY; // no more slots available
}

static hal_ring_t *alloc_ring_struct(void)
{
    hal_ring_t *p;

    /* check the free list */
    if (hal_data->ring_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->ring_free_ptr);
	/* unlink it from the free list */
	hal_data->ring_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_ring_t));
    }
    return p;
}

static void free_ring_struct(hal_ring_t * p)
{
    /* add it to free list */
    p->next_ptr = hal_data->ring_free_ptr;
    hal_data->ring_free_ptr = SHMOFF(p);
}


#ifdef RTAPI

EXPORT_SYMBOL(hal_ring_new);
EXPORT_SYMBOL(hal_ring_delete);
EXPORT_SYMBOL(hal_ring_detach);
EXPORT_SYMBOL(hal_ring_attach);

#endif
