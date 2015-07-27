#ifndef HAL_RING_H
#define HAL_RING_H

#include <rtapi.h>
#include <ring.h>
#include <multiframe.h>
#include "hal_internal.h"


RTAPI_BEGIN_DECLS

// the HAL ring descriptor
//
// formally HAL ring objects are top-level and do not depend on any other object
// in practice they are owned by hal_lib since because rtapi_shm_new()
// requires a module id.
// Making rings owned by the hal_lib component also has the benefit of automatic
// deallocation of shm segments on hal_exit() of the hal_lib component.
typedef struct hal_ring {
    halhdr_t hdr;		// common HAL object header
    int ring_id;                 // as per alloc bitmap
    int ring_shmkey;             // RTAPI shm key - if in shmseg
    int total_size;              // size of shm segment allocated
    unsigned ring_offset;        // if created in HAL shared memory
    unsigned flags;
} hal_ring_t;

// a plug is a read or write hookup to a HAL ring.
//
// the HAL ring must exist at the time a plug is created.
//
// a plug's name derives from the HAL ring's name:
//   read plug:  '<HAL ring name>.read'
//   write plug: '<HAL ring name>.write'
//
// a plug is a HAL object, and can be owned by another HAL object, among others
// a comp or an instance (but not limited to those; a thread could own a plug as well)
// a plug will be deleted on destruction of the owning object (e comp, ring, thread)
//
typedef struct hal_plug {
    halhdr_t hdr;		   // common HAL object header
                                   // hdr.owner_id refers to the owning object (comp, inst..)
    int    ring_handle;            // reference to HAL ring ID  (hal_ring.hdr._id)
    ringbuffer_t rb;               // per-process attach object, meaning only in owner
} hal_plug_t;

// some components use a fifo and a scratchpad shared memory area,
// like sampler.c and streamer.c. ringbuffer_t supports this through
// the optional scratchpad, which is created if spsize is > 0
// on hal_ring_new(). The scratchpad size is recorded in
// ringheader_t.scratchpad_size.

// generic ring methods for all modes:

// create a named ringbuffer, owned by hal_lib
//
// mode is an or of:

// exposed in ringheader_t.type:
// #define RINGTYPE_RECORD    0
// #define RINGTYPE_MULTIPART RTAPI_BIT(0)
// #define RINGTYPE_STREAM    RTAPI_BIT(1)

// mode flags passed in by ring_new
// exposed in ringheader_t.{use_rmutex, use_wmutex, alloc_halmem}
// #define USE_RMUTEX       RTAPI_BIT(2)
// #define USE_WMUTEX       RTAPI_BIT(3)
// #define ALLOC_HALMEM     RTAPI_BIT(4)

// spsize > 0 will allocate a shm scratchpad buffer
// accessible through ringbuffer_t.scratchpad/ringheader_t.scratchpad


// named HAL rings are owned by the HAL_LIB_<pid> RTAPI module
// components do not make sense as owners since their lifetime
// might be shorter than the ring

// base function
hal_ring_t *halg_ring_newfv(const int use_hal_mutex,
			    const int size,
			    const int sp_size,
			    const int mode,
			    const char *fmt,
			    const va_list ap);

// printf-style variant layered ontop
static inline hal_ring_t *halg_ring_newf(const int use_hal_mutex,
					 const int size,
					 const int sp_size,
					 const int mode,
					 const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    hal_ring_t *rp = halg_ring_newfv(use_hal_mutex, size, sp_size, mode, fmt, ap);
    va_end(ap);
    return rp;
}

// XXX: legacy function, phasing out
/* static inline int hal_ring_new(const char *name, */
/* 			       int size, */
/* 			       int sp_size, */
/* 			       int mode)  { */
/*     hal_ring_t *rp = halg_ring_newf(1, size, sp_size, mode,name); */
/*     if (rp) */
/* 	return 0; */
/*     return _halerrno; */
/* } */


/* // printf-style version of the above */
/* int hal_ring_newf(int size, int sp_size, int mode, const char *fmt, ...) */
/*     __attribute__((format(printf,4,5))); */

// delete a ring buffer.
// will fail if the refcount is > 0 (meaning the ring is still attached somewhere).
//int halg_ring_delete(const int use_hal_mutex, const char *name);
int halg_ring_deletefv(const int use_hal_mutex,
		       const char *fmt,
		       const va_list ap);

static inline int halg_ring_deletef(const int use_hal_mutex,const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret  = halg_ring_deletefv(use_hal_mutex, fmt, ap);
    va_end(ap);
    return ret;
}

// printf-style version of the above
/* int hal_ring_deletef(const char *fmt, ...) */
/*     __attribute__((format(printf,1,2))); */

// make an existing ringbuffer accessible to a component, or test for
// existence and flags of a ringbuffer
//
// to attach:
//     rb must point to storage of type ringbuffer_t.
//     Increases the reference count on successful attach
//     store halring flags in *flags if non-zero.
//
// to test for existence:
//     hal_ring_attach(name, NULL, NULL) returns 0 if the ring exists, < 0 otherwise
//
// to test for existence and retrieve the ring's flags:
//     hal_ring_attach(name, NULL, &f) - if the ring exists, returns 0
//     and the ring's flags are returned in f
//
// base function
int halg_ring_attachfv(const int use_hal_mutex,
		       ringbuffer_t *rbptr,
		       unsigned *flags,
		       const char *fmt,
		       const va_list ap);

static inline int halg_ring_attachf(const int use_hal_mutex,
				    ringbuffer_t *rbptr,
				    unsigned *flags,
				    const char *fmt,
				    ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = halg_ring_attachfv(use_hal_mutex, rbptr, flags, fmt, ap);
    va_end(ap);
    return ret;

}

/* static inline int hal_ring_attach(const char *name, */
/* 				  ringbuffer_t *rb, */
/* 				  unsigned *flags) { */
/*     return halg_ring_attach(1, name, rb, flags); */
/* } */

/* // printf-style version of the above */
/* int hal_ring_attachf(ringbuffer_t *rb, unsigned *flags, const char *fmt, ...) */
/*     __attribute__((format(printf,3,4))); */


int halg_ring_detachfv(const int use_hal_mutex,
		       ringbuffer_t *rbptr,
		       const char *fmt,
		       va_list ap);

// detach a ringbuffer. Decreases the reference count.

static inline int halg_ring_detachf(const int use_hal_mutex, ringbuffer_t *rb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = halg_ring_detachfv(use_hal_mutex, rb, fmt, ap);
    va_end(ap);
    return ret;
}

/* // printf-style version of the above */
/* int hal_ring_detachf(ringbuffer_t *rb, const char *fmt, ...) */
/*     __attribute__((format(printf,2,3))); */

// not part of public API. Use with HAL lock engaged.

static inline hal_ring_t *halpr_find_ring_by_name(const char *name){
    return halg_find_object_by_name(0, HAL_RING, name).ring;
}
static inline hal_ring_t *halpr_find_ring_by_id(const int id){
    return halg_find_object_by_id(0, HAL_RING, id).ring;
}


RTAPI_END_DECLS

#endif /* HAL_RING_H */
