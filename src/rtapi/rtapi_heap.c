/********************************************************************
 * Copyright (C) 2014 Michael Haberler <license AT mah DOT priv DOT at>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ********************************************************************/

#include "rtapi.h"
#include "rtapi_heap.h"
#include "rtapi_heap_private.h"
#include "rtapi_export.h"
#include "rtapi_bitops.h"
#ifdef ULAPI
#include <stdio.h>
#endif
// this is straight from the malloc code in:
// K&R The C Programming Language, Edition 2, pages 185-189
// adapted to use offsets relative to the heap descriptor
// so it can be used in as a shared memory resident malloc

void heap_print(struct rtapi_heap *h, int level, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

#ifdef RTAPI
    if (!h->msg_handler)
	return;
    h->msg_handler(level, fmt, args);
#else
    //  vfprintf(stderr,  fmt, args);
#endif
    va_end(args);
}

// scoped lock helper
static void malloc_autorelease_mutex(rtapi_atomic_type **mutex) {
    rtapi_mutex_give(*mutex);
}

void _rtapi_free(struct rtapi_heap *h, void *);

void *_rtapi_malloc(struct rtapi_heap *h, size_t nbytes)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    rtapi_malloc_hdr_t *p, *prevp;
    size_t nunits  = (nbytes + sizeof(rtapi_malloc_hdr_t) - 1) / sizeof(rtapi_malloc_hdr_t) + 1;

    // heaps are explicitly initialized, see rtapi_heap_init()
    // if ((prevp = h->freep) == NULL) {	// no free list yet
    // 	h->base.s.ptr = h->freep = prevp = &h->base;
    // 	h->base.s.size = 0;
    // }
    rtapi_malloc_hdr_t *freep = heap_ptr(h, h->free_p);

    prevp = freep;
    for (p = heap_ptr(h, prevp->s.next); ; prevp = p, p = heap_ptr(h, p->s.next)) {
	if (p->s.size >= nunits) {	/* big enough */
	    if (p->s.size == nunits)	/* exactly */
		prevp->s.next = p->s.next;
	    else {				/* allocate tail end */
		p->s.size -= nunits;
		p += p->s.size;
		p->s.size = nunits;
	    }
	    h->free_p = heap_off(h, prevp);
	    if (h->flags & RTAPIHEAP_TRACE_MALLOC)
		heap_print(h, RTAPI_MSG_DBG, "malloc req=%zu actual=%zu at %p\n",
			   nbytes, _rtapi_allocsize(p+1), p);
	    return (void *)(p+1);
	}
	if (p == freep)	{	/* wrapped around free list */
	    heap_print(h, RTAPI_MSG_INFO, "rtapi_malloc: out of memory"
		       " (size=%zu arena=%zu)\n", nbytes, h->arena_size);
	    //if ((p = morecore(nunits)) == NULL)
	    return NULL;	/* none left */
	}
    }
}

void _rtapi_free(struct rtapi_heap *h,void *ap)
{
    unsigned long *m __attribute__((cleanup(malloc_autorelease_mutex))) = &h->mutex;
    rtapi_mutex_get(m);

    rtapi_malloc_hdr_t *bp, *p;
    rtapi_malloc_hdr_t *freep =  heap_ptr(h,h->free_p);

    bp = (rtapi_malloc_hdr_t *)ap - 1;	// point to block header
    for (p = freep;
	 !(bp > p && bp < (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next));
	 p = heap_ptr(h,p->s.next))
	if (p >= (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next) &&
	    (bp > p || bp < (rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))) {
	    // freed block at start or end of arena
	    if (h->flags & RTAPIHEAP_TRACE_FREE)
		heap_print(h, RTAPI_MSG_DBG, "freed block at start or end of arena\n");
	    break;
	}

    if (bp + bp->s.size == ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))) {
	// join to upper neighbor
	bp->s.size += ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))->s.size;
	bp->s.next = ((rtapi_malloc_hdr_t *)heap_ptr(h,p->s.next))->s.next;
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_DBG, "join upper\n");
    } else
	bp->s.next = p->s.next;
    if (p + p->s.size == bp) {
	// join to lower nbr
	p->s.size += bp->s.size;
	p->s.next = bp->s.next;
	if (h->flags & RTAPIHEAP_TRACE_FREE)
	    heap_print(h, RTAPI_MSG_DBG, "join lower\n");
    } else
	p->s.next = heap_off(h,bp);
    h->free_p = heap_off(h,p);
}

// returns number of bytes available for use
size_t _rtapi_allocsize(void *ap)
{
    rtapi_malloc_hdr_t *p = (rtapi_malloc_hdr_t *) ap - 1;
    return (p->s.size -1) * sizeof (rtapi_malloc_hdr_t);
}

void *_rtapi_calloc(struct rtapi_heap *h, size_t nelem, size_t elsize)
{
    void *p = _rtapi_malloc (h,nelem * elsize);
    if (!p)
        return NULL;
    memset(p, 0, nelem * elsize);
    return p;
}

void *_rtapi_realloc(struct rtapi_heap *h, void *ptr, size_t size)
{
    void *p = _rtapi_malloc (h, size);
    if (!p)
        return (p);
    size_t sz = _rtapi_allocsize (ptr);
    memcpy(p, ptr, (sz > size) ? size : sz);
    _rtapi_free(h, ptr);
    return p;
}

size_t _rtapi_heap_print_freelist(struct rtapi_heap *h)
{
    size_t free = 0;
    rtapi_malloc_hdr_t *p, *prevp, *freep = heap_ptr(h,h->free_p);
    prevp = freep;
    for (p = heap_ptr(h,prevp->s.next); ; prevp = p, p = heap_ptr(h,p->s.next)) {
	if (p->s.size) {
	    heap_print(h, RTAPI_MSG_DBG, "%d at %p\n",
		       p->s.size * sizeof(rtapi_malloc_hdr_t),
		       (void *)(p + 1));
	    free += p->s.size;
	}
	if (p == freep) {
	    heap_print(h, RTAPI_MSG_DBG, "end of free list %p, free=%zu\n", p, free);
	    return free;
	}
    }
}

int _rtapi_heap_addmem(struct rtapi_heap *h, void *space, size_t size)
{
    if (space < (void*) h) return -EINVAL;
    //    if (size < RTAPI_HEAP_MIN_ALLOC) return -EINVAL;
    rtapi_malloc_hdr_t *arena = space;
    arena->s.size = size / sizeof(rtapi_malloc_hdr_t);
    _rtapi_free(h, (void *) (arena + 1));
    return 0;
}

//msg_handler_t __attribute__((weak)) default_rtapi_msg_handler;

int _rtapi_heap_init(struct rtapi_heap *heap)
{
    heap->base.s.next = 0; // because the first element in the heap ist the header
    heap->free_p = 0;      // and free list sentinel
    heap->base.s.size = 0;
    heap->mutex = 0;
    heap->arena_size = 0;
    heap->flags = 0;
    heap->msg_handler = NULL;
    return 0;
}

int  _rtapi_heap_setflags(struct rtapi_heap *heap, int flags)
{
    int f = heap->flags;
    heap->flags = flags;
    return f;
}

void *_rtapi_heap_setloghdlr(struct rtapi_heap *heap, void  *p)
{
    void *h = heap->msg_handler;
    heap->msg_handler = p;
    return h;
}

size_t _rtapi_heap_status(struct rtapi_heap *h, struct rtapi_heap_stat *hs)
{
    hs->total_avail = 0;
    hs->fragments = 0;
    hs->largest = 0;

    rtapi_malloc_hdr_t *p, *prevp, *freep = heap_ptr(h, h->free_p);
    prevp = freep;
    for (p = heap_ptr(h, prevp->s.next); ; prevp = p, p = heap_ptr(h, p->s.next)) {
	if (p->s.size) {
	    hs->fragments++;
	    hs->total_avail += p->s.size;
	    if (p->s.size > hs->largest)
		hs->largest = p->s.size;
	}
	if (p == freep) {
	    hs->total_avail *= sizeof(rtapi_malloc_hdr_t);
	    hs->largest *= sizeof(rtapi_malloc_hdr_t);
	    return hs->largest;
	}
    }
}

