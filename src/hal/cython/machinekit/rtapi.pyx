# vim: sts=4 sw=4 et

from .rtapi cimport *
from os import strerror
from cpython.buffer cimport PyBuffer_FillInfo

_HAL_KEY                   = HAL_KEY
_RTAPI_KEY                 = RTAPI_KEY
_RTAPI_RING_SHM_KEY        = RTAPI_RING_SHM_KEY
_GLOBAL_KEY                = GLOBAL_KEY
_DEFAULT_MOTION_SHMEM_KEY  = DEFAULT_MOTION_SHMEM_KEY
_SCOPE_SHM_KEY             = SCOPE_SHM_KEY


cdef class mview:
    cdef void *base
    cdef int size

    def __cinit__(self, long base, size):
        self.base = <void *>base
        self.size = size

    def __getbuffer__(self, Py_buffer *view, int flags):
        r = PyBuffer_FillInfo(view, self, self.base, self.size, 0, flags)
        view.obj = self

cdef class RtapiModule:
    cdef object _name
    cdef int _id

    def __cinit__(self, char *name):
        self._id = -1
        self._name = name
        self._id = rtapi_init(name)
        if self._id < 0:
            raise RuntimeError("Fail to create RTAPI module: %s" % strerror(-self._id))

    def __dealloc__(self):
        if self._id > 0:
            rtapi_exit(self._id)


    def shmem(self, int key, unsigned long size = 0):
        cdef void *ptr
        cdef int shmid

        shmid = rtapi_shmem_new(key, self._id, size)
        if shmid < 0:
            raise RuntimeError("shm segment 0x%x/%d does not exist" % (key,key))
        retval = rtapi_shmem_getptr(shmid, &ptr, &size);
        if retval < 0:
            raise RuntimeError("getptr shm 0x%x/%d failed %d" % (key,key,retval))
        return memoryview(mview(<long>ptr, size))

    property name:
        def __get__(self): return self._name

    property id:
        def __get__(self): return self._id
