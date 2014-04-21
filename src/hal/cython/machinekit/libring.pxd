# vim: sts=4 sw=4 et

from libc.stdint cimport uint64_t

cdef extern from "ring.h":
    ctypedef struct ringbuffer_t:
        void * header
        char * buf

    ctypedef struct ringiter_t:
        const ringbuffer_t * ring
        uint64_t generation
        size_t * offset

    # int ring_init(ringbuffer_t *ring, size_t size, void * memory)
    # void ring_free(ringbuffer_t *ring)

    int record_write_begin(ringbuffer_t *ring, void ** data, size_t size)
    int record_write_end(ringbuffer_t *ring, void * data, size_t size)
    int record_write(ringbuffer_t *ring, const void * data, size_t size)

    int record_read(const ringbuffer_t *ring, const void **data, size_t *size)
    int record_shift(ringbuffer_t *ring)

    int record_iter_init(const ringbuffer_t *ring, ringiter_t *iter)
    int record_iter_shift(ringiter_t *iter)
    int record_iter_read(const ringiter_t *iter, const void **data, size_t *size)

cdef extern from "multiframe.h":
    ctypedef struct msgbuffer_t:
        ringbuffer_t * ring

    ctypedef struct ringvec_t:
        void  *rv_base
        size_t rv_len
        int    rv_flags

    int frame_write_begin(msgbuffer_t *ring, void ** data, size_t size, int flags)
    int frame_write_end(msgbuffer_t *ring, size_t size)
    int frame_write(msgbuffer_t *ring, const void * data, size_t size, int flags)
    int frame_writev(msgbuffer_t *ring, ringvec_t *rv)
    int msg_write_flush(msgbuffer_t *ring)
    int msg_write_abort(msgbuffer_t *ring)

    int frame_read(msgbuffer_t *ring, const void **data, size_t *size, int *flags)
    int frame_readv(msgbuffer_t *ring, ringvec_t *rv)
    int frame_shift(msgbuffer_t *ring)
    int msg_read_flush(msgbuffer_t *ring)
    int msg_read_abort(msgbuffer_t *ring)
