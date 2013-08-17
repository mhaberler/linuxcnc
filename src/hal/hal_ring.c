
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_ring.h"		/* HAL ringbuffer decls */


static hal_ring_t *alloc_ring_struct(void);
// currently rings are never freed.
static int next_ring_id(void);
extern void *shmalloc_dn(long int size);

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
    {
	hal_ring_t *ptr __attribute__((cleanup(halpr_autorelease_mutex)));

	rtapi_mutex_get(&(hal_data->mutex));

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

	rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d creating ring '%s'\n",
			rtapi_instance, name);

	// make total allocation fit ringheader, ringbuffer and scratchpad
	rbdesc->total_size = ring_memsize( flags, size, sp_size);

	// allocate shared memory segment for ring and init
	rbdesc->ring_shmkey = OS_KEY((RTAPI_RING_SHM_KEY + ring_id), rtapi_instance);

	// allocate an RTAPI shm segment owned by the allocating module
	if ((rbdesc->ring_shmid = rtapi_shmem_new(rbdesc->ring_shmkey, module_id,
						  rbdesc->total_size)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_new: rtapi_shmem_new(0x%8.8x,%d,%zu) failed: %d\n",
			    rtapi_instance,
			    rbdesc->ring_shmkey, module_id,
			    rbdesc->total_size, rbdesc->ring_shmid);
	    return  -ENOMEM;
	}

	// map the segment now so we can fill in the ringheader details
	if ((retval = rtapi_shmem_getptr(rbdesc->ring_shmid,
					 (void **)&rhptr)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_new: rtapi_shmem_getptr for %d failed %d\n",
			    rtapi_instance, rbdesc->ring_shmid, retval);
	    return -ENOMEM;
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
    }
    // automatic unlock by scope exit
}

int hal_ring_attach(const char *name, ringbuffer_t *rbptr, int module_id)
{
    hal_ring_t *rbdesc;
    int shmid;
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

	// map in the shm segment
	if ((shmid = rtapi_shmem_new_inst(rbdesc->ring_shmkey,
					  rtapi_instance, module_id, 0)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach(%s): rtapi_shmem_new_inst() failed %d\n",
			    rtapi_instance,name, shmid);
	    return -EINVAL;
	}
	// make it accessible
	if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach: rtapi_shmem_getptr %d failed %d\n",
			    rtapi_instance, rbdesc->ring_shmid, retval);
	    return -ENOMEM;
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

#if 0 // for now we never free rings - no need
static void free_ring_struct(hal_ring_t * p)
{
    /* add it to free list */
    p->next_ptr = hal_data->ring_free_ptr;
    hal_data->ring_free_ptr = SHMOFF(p);
}
#endif

#ifdef RTAPI

EXPORT_SYMBOL(hal_ring_new);
EXPORT_SYMBOL(hal_ring_detach);
EXPORT_SYMBOL(hal_ring_attach);

#endif
