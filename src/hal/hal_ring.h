#ifndef HAL_RING_H
#define HAL_RING_H

#include <rtapi.h>
#include <ring.h>


RTAPI_BEGIN_DECLS

// a ring buffer exists always relative to a local instance
// it is 'owned' by the creating module (primarily because rtapi_shm_new()
// wants a module_id param); however any module may attach to.
typedef struct {
    char name[HAL_NAME_LEN + 1]; // ring HAL name
    int next_ptr;		 // next ring in used/free lists
    int ring_shmid;              // RTAPI shm handle
    int ring_shmkey;             // RTAPI shm key (a tad redundant)
    int total_size;              // size of shm segment allocated
    int owner;                   // creating HAL module
} hal_ring_t;

// a ring buffer attachment may refer to a ring buffer in any instance
typedef struct {
    char name[HAL_NAME_LEN + 1]; // attached ring HAL name
    int ring_inst;               // instance it's attached in
    int ring_shmid;              // RTAPI shm handle within this instance
    int next_ptr;		 // next ring attachment in used/free lists
    int owner;                   // attaching HAL module
} hal_ring_attachment_t;

// some components use a fifo and a scratchpad shared memory area,
// like sampler.c and streamer.c. ringbuffer_t supports this through
// the optional scratchpad, which is created if spsize is > 0
// on hal_ring_new(). The scratchpad size is recorded in
// ringheader_t.scratchpad_size.

// generic ring methods for all modes:

/* create a named ringbuffer, owned by comp module_id
 * mode is one of: MODE_RECORD, MODE_STREAM
 * optionally or'd with USE_RMUTEX, USE_WMUTEX
 * spsize > 0 will allocate a shm scratchpad buffer
 * accessible through ringbuffer_t.scratchpad/ringheader_t.scratchpad
 */
int hal_ring_new(const char *name, int size, int spsize, int module_id, int mode);

/* detach a ringbuffer */
int hal_ring_detach(const char *name, ringbuffer_t *rb);

/* make an existing ringbuffer accessible to a component
 * rb must point to storage of type ringbuffer_t
 */
int hal_ring_attach(const char *name, ringbuffer_t *rb, int module_id);

//hal_ring_t * halpr_find_ring_by_name(hal_data_t *hd, const char *name);
hal_ring_t *halpr_find_ring_by_name(hal_data_t *hd, const char *name);

RTAPI_END_DECLS

#endif /* HAL_RING_PRIV_H */
