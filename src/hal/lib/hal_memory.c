// HAL memory allocator functions

// the heap where HAL descriptors are stored
// grows upward from right after other hal_data data items

// This heap is unavailable until after init_hal_data()
// referenced through &hal_data->heap

// the top of HAL shm is used for  downwards allocation
// of RT storage through shmalloc_rt() as used by hal_malloc()

// any non-RT related stuff like strings goes
// to the heap in the global segment
// global_heap is available at hal_lib load time since expored by rtapi
// the only place where the global heap is referenced is hal_object.c
// extern struct rtapi_heap *global_heap;


//NB: take care to return memory to the proper heap.

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

// part of public API
void *halg_malloc(const int use_hal_mutex, size_t size)
{
    PCHECK_NULL(hal_data);
    {
        WITH_HAL_MUTEX_IF(use_hal_mutex);

	void *retval = shmalloc_rt(size);
	if (retval == NULL)
	    HALERR("out of rt memory - allocating %zu bytes", size);

	hal_data->hal_malloced += size; // beancounting
	return retval;
    }
}

// HAL library internal use only

void shmfree_desc(void *p)
{
    if (hal_data == NULL) {
	HALERR("freeing NULL pointer");
	return;
    }
    rtapi_free(&hal_data->heap, p);
}

int hal_heap_addmem(size_t click)
{
    size_t actual = RTAPI_ALIGN(click, HAL_ARENA_ALIGN);

    HALDBG("extending arena by %zu bytes", actual);

    if (hal_freemem() < HAL_HEAP_MINFREE) {
	HALERR("can't extend arena - below minfree: %zu", hal_freemem());
	return 0;
    }
    // TBD: lock this. Probably best to use the rtapi heap mutex.
    if (rtapi_heap_addmem(&hal_data->heap,
			  SHMPTR(hal_data->shmem_bot),
			  actual)) {
	HALERR("rtapi_heap_addmem(%zu) failed", actual);
	_halerrno = -ENOMEM;
	return -ENOMEM;
    }
    hal_data->shmem_bot += actual;
    return 0;
}

void *shmalloc_desc(size_t size)
{
    void *retval = rtapi_calloc(&hal_data->heap, 1, size);

    // extend shm arena on failure
    if (retval == NULL) {
	hal_heap_addmem(HAL_HEAP_INCREMENT);

	retval = rtapi_calloc(&hal_data->heap, 1, size);
	if (retval == NULL)
	    HALERR("giving up - can't allocate %zu bytes", size);
	_halerrno = -ENOMEM;
    }
    return retval;
}

void *shmalloc_rt(size_t size)
{
    long int tmp_top;
    void *retval;

    /* tentatively allocate memory */
    tmp_top = hal_data->shmem_top - size;
    /* deal with alignment requirements */
    if (size >= 8) {
	/* align on 8 byte boundary */
	tmp_top &= (~7);
    } else if (size >= 4) {
	/* align on 4 byte boundary */
	tmp_top &= (~3);
    } else if (size == 2) {
	/* align on 2 byte boundary */
	tmp_top &= (~1);
    }
    /* is there enough memory available? */
    if (tmp_top < hal_data->shmem_bot) {
	/* no */
	HALERR("giving up - can't allocate %zu bytes", size);
	_halerrno = -ENOMEM;
	return 0;
    }
    size_t waste = hal_data->shmem_top - tmp_top - size;
    hal_data->rt_alignment_loss += waste;

    /* memory is available, allocate it */
    retval = SHMPTR(tmp_top);
    hal_data->shmem_top = tmp_top;
    return retval;
}

void report_heapstatus(const char *tag,  struct rtapi_heap *h)
{
	struct rtapi_heap_stat hs = {};
	rtapi_heap_status(h, &hs);
	HALDBG("%s heap status\n", tag);
	HALDBG("  arena=%zu totail_avail=%zu fragments=%zu largest=%zu\n",
	       hs.arena_size, hs.total_avail, hs.fragments, hs.largest);
	HALDBG("  requested=%zu allocated=%zu freed=%zu waste=%zu%%\n",
	       hs.requested, hs.allocated, hs.freed,
	       hs.allocated ?
	       (hs.allocated - hs.requested)*100/hs.allocated : 0);
}

void report_memory_usage(void)
{
	report_heapstatus("HAL heap", &hal_data->heap);

	report_heapstatus("global heap", global_heap);
	HALDBG("  strings on global heap: alloc=%zu freed=%zu balance=%zu\n",
	       hal_data->str_alloc,
	       hal_data->str_freed,
	       hal_data->str_alloc - hal_data->str_freed);

	size_t rtalloc = (size_t)(global_data->hal_size - hal_data->shmem_top);
	HALDBG("  RT objects: %zu  alignment loss: %zu  (%zu%%)\n",
	       rtalloc,
	       hal_data->rt_alignment_loss,
	       rtalloc ?
	       (hal_data->rt_alignment_loss * 100/rtalloc) : 0);
	HALDBG("  hal_malloc():   %zu\n",
	       hal_data->hal_malloced);
	HALDBG("  unused:   %ld\n",
	       (long)( hal_data->shmem_top - hal_data->shmem_bot));

}
