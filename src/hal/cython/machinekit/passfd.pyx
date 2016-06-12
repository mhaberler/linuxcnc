

cdef extern from "mk-passfd.h":

    int mt_passfd_socket(const int instance_id)
    int mt_send_fd(const int socket, const char *name, const int fd)
    int mt_fetch_fd(const int socket, const char *name, int *pfd)

cdef class Passfd:
    cdef int _socket
    cdef int _instance_id

    def __cinit__(self, int instance_id = 0):
        self._instance_id = instance_id
        self._socket = mt_passfd_socket(self._instance_id)

    property socket:
        def __get__(self): return self._socket

    def fetch(self, char *name):
        cdef int fd, retval
        retval = mt_fetch_fd(self._socket, name, &fd)
        if retval:
            raise RuntimeError("mt_fetch_fd(%s) failed" % name)
        return fd
