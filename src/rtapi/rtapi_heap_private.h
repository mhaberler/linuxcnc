#ifndef _RTAPI_HEAP_PRIVATE_INCLUDED
#define _RTAPI_HEAP_PRIVATE_INCLUDED

#include "rtapi.h"
#include "rtapi_bitops.h" // rtapi_atomic_type
#include "stdarg.h"

#include "rtapi_heap.h"

// assumptions:
// heaps live in a single shared memory segment
// the arena(s) are allocated above the rtapi_heap structure in a particular segment
// the offsets used in the rtapi_heap and rtapi_malloc_header structure
// are offsets from the rtapi_heap structure.

#ifndef RTAPI_MALLOC_ALIGN
#define RTAPI_MALLOC_ALIGN 1
#endif

typedef struct rtapi_malloc_align {
    double _align[RTAPI_MALLOC_ALIGN];
} rtapi_malloc_align_t;


union rtapi_malloc_header {
    struct hdr {
	size_t   next;	// next block if on free list
	unsigned size;	// size of this block
    } s;
    rtapi_malloc_align_t align;	// unused - force alignment of blocks
};

typedef union rtapi_malloc_header rtapi_malloc_hdr_t;

struct rtapi_heap {
    rtapi_malloc_hdr_t base;
    size_t free_p;
    size_t arena_size;
    rtapi_atomic_type mutex;
    int flags; // debugging, tracing etc
    size_t requested;
    size_t allocated;
    size_t freed;
    heap_print_t msg_handler;
};

static inline void *heap_ptr(struct rtapi_heap *base, size_t offset) {
    return ((unsigned char *)base + offset);
}

static inline size_t heap_off(struct rtapi_heap *base, void *p) {
    return ((unsigned char *)p - (unsigned char *)base);
}


#endif // _RTAPI_HEAP_PRIVATE_INCLUDED
