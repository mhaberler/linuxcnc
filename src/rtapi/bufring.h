
/********************************************************************
 * Description:  bufring.h
 *
 *               This file, 'bufring.h', implements multipart messages
 *               on top of the ringbuffer record-mode API (see ring.h).
 *
 * Based on ring code by Pavel Shramov, 4/2014,
 * http://psha.org.ru/cgit/psha/ring/
 * License: MIT
 * Integration: Michael Haberler
 *
 * Multipart messages consist of zero or more frames.
 *
 * A frame consists of a buffer of zero or more bytes, and an
 * int flag value, which is not interpreted by the bufring code.
 *
 * This data format is intended to closely match the zeroMQ
 * ZMTP encoding of messages and frames - see
 * http://rfc.zeromq.org/spec:15 for a description of ZMTP
 * framing.
 *
 *
 * Public API:
 * int bring_init(bringbuffer_t *bring, ringbuffer_t *ring);
 * int bring_write_begin(bringbuffer_t *ring, void ** data, size_t size, int flags);
 * int bring_write_end(bringbuffer_t *ring, void * data, size_t size);
 * int bring_write(bringbuffer_t *ring, ringvec_t *rv);
 * int bring_write_flush(bringbuffer_t *ring);

 * int int bring_read(bringbuffer_t *ring, ringvec_t *rv);
 * int bring_shift(bringbuffer_t *ring);
 * int bring_shift_flush(bringbuffer_t *ring);
 *
 ********************************************************************/

#ifndef __BUFFERED_RING_H__
#define __BUFFERED_RING_H__

#include "ring.h"

typedef struct {
    ringbuffer_t *ring;
    void * _write;
    size_t write_size;
    size_t write_off;
    const void * _read;
    size_t read_size;
    size_t read_off;
} bringbuffer_t;

typedef struct {
    __s32 size;
    __s32 flags;
} bring_frame_t;


static inline int bring_init(bringbuffer_t *bring, ringbuffer_t *ring)
{
    memset(bring, 0, sizeof(bringbuffer_t));
    bring->ring = ring;
    return 0;
}

static inline int bring_write_begin(bringbuffer_t *ring, void ** data, size_t size, int flags)
{
    const size_t sz = size + sizeof(bring_frame_t);
    if (!ring->_write) {
	int r = record_write_begin(ring->ring, &ring->_write, sz);
	if (r) return r;
	ring->write_size = sz;
	ring->write_off = 0;
    }
    bring_frame_t * frame = (bring_frame_t *)((unsigned char *)ring->_write + ring->write_off);
    if (ring->write_size < ring->write_off + sizeof(bring_frame_t) + size) {
	// Reallocate
	const void * old = ring->_write;
	int r = record_write_begin(ring->ring, &ring->_write, ring->write_off + sz);
	if (r) return r;
	if (old != ring->_write)
	    memmove(ring->_write, old, ring->write_off);
	ring->write_size = ring->write_off + sz;
    }
    frame->size = size;
    frame->flags = flags;
    *data = frame + 1;
    return 0;
}

static inline int bring_write_end(bringbuffer_t *ring, void * data, size_t size)
{
    if (!ring->_write)
	return EINVAL;
    bring_frame_t *frame = (bring_frame_t *) ring->_write;
    frame->size = size;
    ring->write_off = ring->write_off + sizeof(*frame) + frame->size;
    return 0;
}

// add a frame
static inline int bring_write(bringbuffer_t *ring, ringvec_t *rv)
{
    void * ptr;
    int r = bring_write_begin(ring, &ptr, rv->rv_len, rv->rv_flags);
    if (r) return r;
    memmove(ptr, rv->rv_base, rv->rv_len);
    return bring_write_end(ring, ptr, rv->rv_len);
}

// finish and send off a multipart message, consisting of zero or more frames.
static inline int bring_write_flush(bringbuffer_t *ring)
{
    int r = record_write_end(ring->ring, ring->_write, ring->write_off);
    ring->_write = 0;
    return r;
}

// read a frame without consuming.
static inline int bring_read(bringbuffer_t *ring, ringvec_t *rv)
{
	if (!ring->_read) {
		int r = record_read(ring->ring, &ring->_read, &ring->read_size);
		if (r) return r;
		ring->read_off = 0;
	}
	if (ring->read_off == ring->read_size)
		return EAGAIN; //XXX?
	const bring_frame_t *frame = (bring_frame_t *)((unsigned char *)ring->_read + ring->read_off);
	rv->rv_base = frame + 1;
	rv->rv_len = frame->size;
	rv->rv_flags = frame->flags;
	return 0;
}

// consume a frame.
static inline int bring_shift(bringbuffer_t *ring)
{
    if (!ring->_read || ring->read_off == ring->read_size) return EINVAL;
    const bring_frame_t *frame = (bring_frame_t *)((unsigned char *)ring->_read + ring->read_off);
    ring->read_off += sizeof(*frame) + frame->size;
    return 0;
}

// consume a multi-frame message, regardless how many frames were consumed.
static inline int bring_shift_flush(bringbuffer_t *ring)
{
    if (!ring->_read) return EINVAL;
    ring->_read = 0;
    return record_shift(ring->ring);
}

#endif//__BUFFERED_RING_H__
