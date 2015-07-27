// HAL memory allocator functions

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"


// part of public API
void *halg_malloc(const int use_hal_mutex, size_t size)
{
    WITH_HAL_MUTEX_IF(use_hal_mutex);

    if (hal_data == 0) {
	HALERR("called before init");
	return 0;
    }
    void *retval = shmalloc_rt(size);
    if (retval == NULL)
	HALERR("out of rt memory - allocating %zu bytes", size);
    return retval;
}

// HAL library internal use only

void shmfree_desc(void *p)
{
    rtapi_free(&hal_data->heap, p);
}

int heap_addmem(size_t click)
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
	heap_addmem(HAL_HEAP_INCREMENT);

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
