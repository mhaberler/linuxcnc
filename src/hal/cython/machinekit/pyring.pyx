# vim: sts=4 sw=4 et

from libc.errno cimport EAGAIN
from libc.string cimport memcpy
from cpython.buffer cimport PyBuffer_FillInfo
from cpython.bytes cimport PyBytes_AsString, PyBytes_Size, PyBytes_FromStringAndSize

from libring cimport *
from .hal cimport *

from os import getpid  # XXX I just want use getpid(), not export it?

cdef extern int lib_module_id
cdef int comp_id = -1

cdef extern from "hal_ring.h" :

    # XXX this is roughly as from: http://goo.gl/Rb28IZ
    # XXX how do I expose this?
    ctypedef struct hal_ring_t:
        const char *name   # XXX a char array - is this right?
        int next_ptr
        int ring_id
        int ring_shmkey
        int total_size
        unsigned ring_offset
        unsigned flags
        int handle

    int hal_ring_new(const char *name, int size, int spsize, int mode)
    int hal_ring_delete(const char *name)
    int hal_ring_attach(const char *name, ringbuffer_t *rb, unsigned *flags)
    int hal_ring_detach(const char *name, ringbuffer_t *rb)


# attach to HAL only if needed
def pyring_hal_init():
    global comp_id,lib_module_id
    cdef modname
    if (lib_module_id < 0):
        modname = "pyring%d" % getpid()
        comp_id = hal_init(modname)
        if comp_id > -1:
            hal_ready(comp_id)
        else:
            raise RuntimeError("hal_init() failed - realtime not started?")

# module exit handler
def pyring_exit():
    global comp_id
    if comp_id > -1:
        hal_exit(comp_id)


import atexit                 # XXX I just want use atexit(), not export it?
atexit.register(pyring_exit)

cdef class mview:
    cdef void *base
    cdef int size

    def __cinit__(self, long base, size):
        self.base = <void *>base
        self.size = size

    def __getbuffer__(self, Py_buffer *view, int flags):
        r = PyBuffer_FillInfo(view, self, self.base, self.size, 0, flags)
        view.obj = self

cdef class Ring:
    cdef ringbuffer_t _ring
    cdef hal_ring_t *_hal_ring

    # XXX this ctor assumes a named HAL ring.
    # can one have a second constructor which takes say a buffer or memoryview object
    # eg for usage with a posix shm segment, all without HAL?

    def __cinit__(self, char *name, int size = 0, int sp_size = 0, unsigned int flags = 0):

        # attach to HAL by creating a dummy halcomp - needed only for accessing HAL rings
        if (lib_module_id < 0): # once
            pyring_hal_init()
        if size:
            if hal_ring_new(name, size, sp_size, flags):
                raise RuntimeError("hal_ring_new failed")
        if hal_ring_attach(name, &self._ring, &flags):
                raise RuntimeError("hal_ring_attach failed")

        # XXX ok, the HAL object exists, and the ring is attached
        # what I would need here is to call
        # hal_ring_t *halpr_find_ring_by_name(const char *name) to init the _hal_ring *
        # this must happen under hal_mutex, I usually do this so in C:
        # {
	#    int dummy __attribute__((cleanup(halpr_autorelease_mutex)));
	#    rtapi_mutex_get(&(hal_data->mutex));
	#    self._hal_ring = halpr_find_ring_by_name(name)
        #    if (!self._hal_ring)
        #       raise Bug
        # }

    # XXX a second constructor for buffer object like so?
    # def __cinit__(self, buffer):
    #    .....

    def __dealloc__(self):
        pass
        # ring_free(&self._ring)

    def write(self, s):
        cdef void * ptr
        cdef size_t size = PyBytes_Size(s)
        cdef int r = record_write_begin(&self._ring, &ptr, size)
        if r:
            if r != EAGAIN:
                raise RuntimeError("Ring write failed")
            return False
        memcpy(ptr, PyBytes_AsString(s), size)
        record_write_end(&self._ring, ptr, size)
        return True

    def read(self):
        cdef const void * ptr
        cdef size_t size
        cdef int r = record_read(&self._ring, &ptr, &size)
        if r:
            if r != EAGAIN:
                raise RuntimeError("Ring read failed")
            return None
        return memoryview(mview(<long>ptr, size))

    def shift(self):
        record_shift(&self._ring)

    def __iter__(self):
        return RingIter(self)

cdef class RingIter:
    cdef ringiter_t _iter

    def __cinit__(self, ring):
        if record_iter_init(&(<Ring>(ring))._ring, &self._iter):
            raise RuntimeError("Failed to initialize ring iter")

    def read(self):
        cdef const void * ptr
        cdef size_t size
        cdef int r = record_iter_read(&self._iter, &ptr, &size)
        if r:
            if r != EAGAIN:
                raise RuntimeError("Ring read failed")
            return None
        return memoryview(mview(<long>ptr, size))

    def shift(self):
        record_iter_shift(&self._iter)

    def __iter__(self):
        return self

    def __next__(self):
        r = self.read()
        if r is None:
            raise StopIteration("Ring is empty")
        s = memoryview(r.tobytes())
        self.shift()
        return s

    def next(self): return self.__next__()

cdef class ringvec:
    cdef mview data
    cdef int flags

    def __cinit__(self, long base, size, flags):
        self.data = mview(base, size)
        self.flags = flags

    property data:
        def __get__(self):
            return memoryview(self.data)

    property flags:
        def __get__(self): return self.flags

cdef class BufRing:
    cdef msgbuffer_t _ring
    cdef object _pyring

    def __cinit__(self, ring):
        self._pyring = ring
        self._ring.ring = &(<Ring>ring)._ring

    def __init__(self, ring):
        pass

    def read(self):
        cdef ringvec_t rv
        msg_read_abort(&self._ring)
        while frame_readv(&self._ring, &rv) == 0:
            yield ringvec(<long>rv.rv_base, rv.rv_len, rv.rv_flags)
            frame_shift(&self._ring)

    def shift(self):
        msg_read_flush(&self._ring)

    def write(self, s, flags = 0):
        cdef void * ptr
        cdef size_t size = PyBytes_Size(s)
        r = frame_write(&self._ring, PyBytes_AsString(s), size, flags)
        if not r:
            return True
        if r != EAGAIN:
            raise RuntimeError("Ring write failed")
        return False

    def flush(self):
        msg_write_flush(&self._ring)
