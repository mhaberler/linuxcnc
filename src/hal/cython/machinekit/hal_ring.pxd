# hal_ring.h definitions

from .hal cimport *
from .ring cimport ringbuffer_t

cdef extern from "hal_ring.h":
    ctypedef struct hal_ring_t:
        char *name
        int next_ptr
        int ring_id
        int ring_shmkey
        int total_size
        unsigned ring_offset
        unsigned flags
        int handle
        int paired_handle
        unsigned encodings
        unsigned haltalk_zeromq_stype
        unsigned haltalk_adopt
        unsigned haltalk_announce
        unsigned haltalk_writes

    int hal_ring_new(const char *name, int size, int spsize, int mode)
    int hal_ring_delete(const char *name)
    int hal_ring_attachf(ringbuffer_t *rb, unsigned *flags,const char *name)
    int hal_ring_detach(ringbuffer_t *rb)

    int hal_ring_setflag(const int ring_id, const unsigned flag, unsigned value)
    int hal_ring_getflag(const int ring_id, const unsigned , unsigned *value)
    int hal_ring_pair(const int this_ring, const int other_ring)

    # not part of public API. Use with HAL lock engaged.
    hal_ring_t *halpr_find_ring_by_name(const char *name)
    hal_ring_t *halpr_find_ring_by_id(const int id)
