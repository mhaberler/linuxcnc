# hal_ring.h definitions

from .hal cimport *
from .hal_priv cimport halhdr_t
from .ring cimport ringbuffer_t

cdef extern from "hal_ring.h":
    ctypedef struct hal_ring_t:
        halhdr_t hdr
        int ring_id
        int ring_shmkey
        int total_size
        unsigned ring_offset
        unsigned flags


    int hal_ring_new(const char *name, int size, int spsize, int mode)
    int hal_ring_delete(const char *name)
    int hal_ring_attach(const char *name, ringbuffer_t *rb, unsigned *flags)
    int hal_ring_detach(const char *name, ringbuffer_t *rb)

    # not part of public API. Use with HAL lock engaged.
    hal_ring_t *halpr_find_ring_by_name(const char *name)
