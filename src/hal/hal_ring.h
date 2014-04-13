#ifndef HAL_RING_H
#define HAL_RING_H

#include <rtapi.h>
#include <ring.h>
#include <bufring.h>


RTAPI_BEGIN_DECLS

// a ring buffer exists always relative to a local instance
// it is 'owned' by the creating module (primarily because rtapi_shm_new()
// wants a module_id param); however any module may attach to it.

typedef struct {
    char name[HAL_NAME_LEN + 1]; // ring HAL name
    int next_ptr;		 // next ring in used/free lists
    int ring_id;                 // as per alloc bitmap
    int ring_shmkey;             // RTAPI shm key - if in shmseg
    int total_size;              // size of shm segment allocated
    int owner;                   // creating HAL module
    unsigned ring_offset;        // if created in HAL shared memory
    unsigned flags;
    int handle;                  // unique ID
} hal_ring_t;

// some components use a fifo and a scratchpad shared memory area,
// like sampler.c and streamer.c. ringbuffer_t supports this through
// the optional scratchpad, which is created if spsize is > 0
// on hal_ring_new(). The scratchpad size is recorded in
// ringheader_t.scratchpad_size.

// generic ring methods for all modes:

// create a named ringbuffer, owned by comp module_id
// mode is one of: MODE_RECORD, MODE_STREAM
// optionally or'd with USE_RMUTEX, USE_WMUTEX
// spsize > 0 will allocate a shm scratchpad buffer
// accessible through ringbuffer_t.scratchpad/ringheader_t.scratchpad
int hal_ring_new(const char *name, int size, int spsize, int module_id, int mode);

// delete a ring buffer. Can be done only by the creating module.
// will fail if the refcount is > 0 (meaning the ring is still attached somewhere).
// will fail if not the creating module_id (which can be ignored).
int hal_ring_delete(const char *name, int module_id);

// make an existing ringbuffer accessible to a component
// rb must point to storage of type ringbuffer_t.
// Increases the reference count.
// store halring flags in *flags if non-zero.
int hal_ring_attach(const char *name, ringbuffer_t *rb, int module_id, unsigned *flags);

// detach a ringbuffer. Decreases the reference count.
int hal_ring_detach(const char *name, ringbuffer_t *rb);

// not part of public API. Use with HAL lock engaged.
hal_ring_t *halpr_find_ring_by_name(const char *name);

RTAPI_END_DECLS

#endif /* HAL_RING_PRIV_H */
