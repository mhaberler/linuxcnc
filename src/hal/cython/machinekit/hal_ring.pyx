from libc.errno cimport EAGAIN
from libc.string cimport memcpy
from buffer cimport PyBuffer_FillInfo
from cpython.bytes cimport PyBytes_AsString, PyBytes_Size, PyBytes_FromStringAndSize
from cpython.string cimport PyString_FromStringAndSize
from cpython cimport bool

from .ring cimport *

cdef class mview:
    cdef void *base
    cdef int size

    def __cinit__(self, long base, size):
        self.base = <void *>base
        self.size = size

    def __getbuffer__(self, Py_buffer *view, int flags):
        r = PyBuffer_FillInfo(view, self, self.base, self.size, 0, flags)
        view.obj = self

cdef class  _Ring:
    cdef ringbuffer_t _rb
    cdef hal_ring_t *_hr
    cdef public uint32_t flags,aflags

    def __cinit__(self,
                  char *name = NULL,
                  int id = 0,
                  int size = 0,
                  int scratchpad_size = 0,
                  int type = RINGTYPE_RECORD,
                  bool use_rmutex = False,
                  bool use_wmutex = False,
                  bool in_halmem = False,
                  bool adopt = False,
                  bool announce = False,
                  bool haltalk_writes = False,
                  int sockettype = -1,
                  int encodings = 0):
        self._hr = NULL
        self.flags = (type & RINGTYPE_MASK);
        if use_rmutex: self.flags |= USE_RMUTEX;
        if use_wmutex: self.flags |= USE_RMUTEX;
        if in_halmem:  self.flags |= ALLOC_HALMEM;

        hal_required()
        if size == 0:
            with HALMutex():
                if id > 0:
                    # wrap existing ring by id
                    if name != NULL:
                        raise RuntimeError("cant give both name=<string> and id=<int>")
                    self._hr = halpr_find_ring_by_id(id)
                    if self._hr == NULL:
                        raise NameError("no such existing ring id %d" % id)
                else:
                    # wrap existing ring by name
                    if name == NULL:
                        raise RuntimeError("no name= given")
                    self._hr = halpr_find_ring_by_name(name)
                    if self._hr == NULL:
                        raise NameError("no such existing ring '%s'" % name)
        else:
            # size > 0, so create - must have a name
            if name == NULL:
                raise RuntimeError("no name= given")
            if hal_ring_new(name, size, scratchpad_size, self.flags) < 0:
                raise RuntimeError("hal_ring_new(%s) failed: %s" %
                                   (name, hal_lasterror()))
            with HALMutex():
                self._hr = halpr_find_ring_by_name(name)
                if self._hr == NULL:
                    raise RuntimeError("BUG: ring '%s' not found after creating!" % (name))

        # ok, created or looked up - self._hr is valid, now attach:
        if hal_ring_attachf(&self._rb, &self.aflags, name) < 0:
                raise NameError("hal_ring_attachf(%s) failed: %s" %
                                   (name, hal_lasterror()))

        # if this ring was freshly created, fill in flags
        if size:
            self._hr.encodings = encodings
            self._hr.haltalk_zeromq_stype = sockettype
            self._hr.haltalk_adopt = adopt
            self._hr.haltalk_announce = announce
            self._hr.haltalk_writes = haltalk_writes

    def __dealloc__(self):
        if self._hr != NULL:
            name = self._hr.name
            r = hal_ring_detach(&self._rb)
            if r:
                raise RuntimeError("hal_ring_detach() failed: %d %s" %
                                       (r, hal_lasterror()))

    def __iter__(self):
        return RingIter(self)

    def paired_ring(self):
        if self._hr.paired_handle == 0:
            raise KeyError("ring %s has no paired ring" % (self._hr.name))
        with HALMutex():
            name = ""
            pr = halpr_find_ring_by_id(self._hr.paired_handle)
            if pr == NULL:
                raise RuntimeError("BUG: no such ring id %d" % (self._hr.paired_handle))
            name = pr.name
        return Ring(name=pr.name)

    property available:
        def __get__(self): return record_write_space(self._rb.header)

    property writer:
        '''ring writer attribute. Advisory in nature.'''
        def __get__(self): return self._rb.header.writer
        def __set__(self,int value):  self._rb.header.writer = value

    property reader:
        def __get__(self): return self._rb.header.reader
        def __set__(self,int value):  self._rb.header.reader = value

    property size:
        def __get__(self): return self._rb.header.size

    property type:
        def __get__(self): return self._rb.header.type

    property in_halmem:
        def __get__(self): return (self._hr.flags & ALLOC_HALMEM != 0)

    property rmutex_mode:
        def __get__(self): return (ring_use_rmutex(&self._rb) != 0)

    property wmutex_mode:
        def __get__(self): return (ring_use_wmutex(&self._rb) != 0)

    property name:
        def __get__(self): return self._hr.name

    property scratchpad_size:
        def __get__(self): return ring_scratchpad_size(&self._rb)

    property scratchpad:
        def __get__(self):
            cdef size_t ss = ring_scratchpad_size(&self._rb)
            if ss == 0:
                return None
            return memoryview(mview(<long>self._rb.scratchpad, ss))

    property id:
        ''' HAL object id '''
        def __get__(self): return self._hr.handle

    property paired_id:
        ''' HAL object id of a paired ring'''
        def __get__(self): return self._hr.paired_handle
        def __set__(self,int value):  self._hr.paired_handle = value

    property encodings:
        ''' encodings understood by reader, advisory in nature.'''
        def __get__(self): return self._hr.encodings
        def __set__(self,unsigned value):  self._hr.encodings = value

    property adopt:
        ''' adopt by haltalk, advisory'''
        def __get__(self): return self._hr.haltalk_adopt
        def __set__(self,bint value):  self._hr.haltalk_adopt = value

    property announce:
        ''' announce via mDNS by haltalk, advisory'''
        def __get__(self): return self._hr.haltalk_announce
        def __set__(self,bint value):  self._hr.haltalk_announce = value

    property haltalk_writes:
        ''' haltalk should serve write side of this ring, advisory'''
        def __get__(self): return self._hr.haltalk_writes
        def __set__(self,bint value):  self._hr.haltalk_writes = value

    property sockettype:
        ''' zeroMQ socket type to serve this ring, advisory'''
        def __get__(self): return self._hr.haltalk_zeromq_stype
        def __set__(self,unsigned value):  self._hr.haltalk_zeromq_stype = value


cdef class _RecordRing(_Ring):

    def __cinit__(self, **kwargs):
        if self._rb.header.type != RINGTYPE_RECORD:
            raise RuntimeError("ring '%s' not a record ring: type=%d" %
                               (self._hr.name,self._rb.header.type))

    def write(self, s):
        cdef void * ptr
        cdef size_t size = PyBytes_Size(s)
        cdef int r = record_write_begin(&self._rb, &ptr, size)
        if r:
            if r != EAGAIN:
                raise IOError("Ring %s write failed: %d %s" %
                                   (r,self._hr.name))
            return False
        memcpy(ptr, PyBytes_AsString(s), size)
        record_write_end(&self._rb, ptr, size)
        return True

    def read(self):
        cdef const void * ptr
        cdef size_t size

        cdef int r = record_read(&self._rb, &ptr, &size)
        if r:
            if r != EAGAIN:
                raise IOError("Ring %s read failed: %d %s" %
                                   (r,self._hr.name))
            return None
        return memoryview(mview(<long>ptr, size))

    def shift(self):
        record_shift(&self._rb)




cdef class _StreamRing(_Ring):

    def __cinit__(self, **kwargs):
        if self._rb.header.type != RINGTYPE_STREAM:
            raise RuntimeError("ring '%s' not a stream ring: type=%d" %
                               (self._hr.name,self._rb.header.type))


    def flush(self):
        '''clear the buffer contents. Note this is not thread-safe
        unless all readers and writers use a r/w mutex.'''

        return stream_flush(&self._rb)

    def consume(self, int nbytes):
        '''remove argument number of bytes from stream.
        May raise IOError if more than the number of
        available bytes are consumed'''

        cdef size_t avail
        avail = stream_read_space(self._rb.header)
        if (nbytes > <int>avail):
            raise IOError("consume(%d): argument larger than bytes available (%zu)" %
                          (nbytes, avail))
        stream_read_advance(&self._rb, nbytes);

    def next(self):
        '''returns the number of bytes readable or 0 if no data is available.'''
        return stream_read_space(self._rb.header)

    def write(self, s):
        '''write to ring. Returns 0 on success.
        nozero return value indicates the number
	of bytes actually written. '''

        return stream_write(&self._rb,  PyBytes_AsString(s), PyBytes_Size(s))

    def read(self):
        ''' return all bytes readable as a string, or None'''
        cdef ringvec_t v[2]
        stream_get_read_vector(&self._rb, v)

        if v[0].rv_len:
            b1 = PyString_FromStringAndSize(<const char *>v[0].rv_base, v[0].rv_len)
            if v[1].rv_len == 0:
                stream_read_advance(&self._rb, v[0].rv_len)
                return b1

            b2 = PyString_FromStringAndSize(<const char *>v[1].rv_base, v[1].rv_len)
            stream_read_advance(&self._rb, v[0].rv_len + v[1].rv_len)
            return b1 + b2
        return None


cdef class _MultiframeRing(_Ring):
    cdef msgbuffer_t _mrb

    def __cinit__(self, **kwargs):
        self._mrb.ring = &self._rb
        if self._rb.header.type != RINGTYPE_MULTIPART:
            raise RuntimeError("ring '%s' not a multiframe ring: type=%d" %
                               (self._hr.name,self._rb.header.type))

    def read(self):
        cdef ringvec_t rv
        msg_read_abort(&self._mrb)
        while frame_readv(&self._mrb, &rv) == 0:
            yield ringvec(<long>rv.rv_base, rv.rv_len, rv.rv_flags)
            frame_shift(&self._mrb)

    def shift(self):
        msg_read_flush(&self._mrb)

    def write(self, s, format = 0, more = 0, msgid = 0, unused = 0, eor = 0, flags = 0):
        cdef mflag_t param
        if format+more+msgid+unused > 0:
            if flags > 0:
                raise IOError("either use flags=, or format=,"
                              "more=,msgid=,eor=,unused=, but not both")
            param.f.msgid = msgid
            param.f.format = format
            param.f.more = more
            param.f.eor = eor
            param.f.unused = unused
        else:
            param.u = flags
        cdef void * ptr
        cdef size_t size = PyBytes_Size(s)
        r = frame_write(&self._mrb, PyBytes_AsString(s), size, param.u)
        if not r:
            return True
        if r != EAGAIN:
            raise IOError("Ring write failed")
        return False

    def flush(self):
        msg_write_flush(&self._mrb)

    def ready(self):
        return record_next_size(self._mrb.ring) > -1


cdef class RingIter:
    cdef ringiter_t _iter

    def __cinit__(self, _Ring ring):
        if record_iter_init(&(<_Ring>(ring))._rb, &self._iter):
            raise RuntimeError("Failed to initialize ring iter")

    def read(self):
        cdef const void * ptr
        cdef size_t size
        cdef int r = record_iter_read(&self._iter, &ptr, &size)
        if r:
            if r != EAGAIN:
                raise IOError("Ring read failed")
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
    cdef mflag_t flags

    def __cinit__(self, long base, size, flags):
        self.data = mview(base, size)
        self.flags.u = flags

    property data:
        def __get__(self):
            return memoryview(self.data)

    property flags:
        def __get__(self): return self.flags.u

    property msgid:
        def __get__(self): return self.flags.f.msgid

    property format:
        def __get__(self): return self.flags.f.format

    property more:
        def __get__(self): return self.flags.f.more

    property eor:
        def __get__(self): return self.flags.f.eor

    property unused:
        def __get__(self): return self.flags.f.unused


# hal_iter callback: add ring names into list
cdef int _collect_ring_names(hal_ring_t *ring,  void *userdata):
    arg =  <object>userdata
    arg.append(ring.name)
    return 0

def rings():
    ''' return list of ring names'''
    hal_required()
    names = []
    with HALMutex():
        rc = halpr_foreach_ring(NULL, _collect_ring_names, <void *>names);
        if rc < 0:
            raise RuntimeError("halpr_foreach_ring failed %d" % rc)
    return names

def delete_ring(char *name):
    hal_required()
    rc = hal_ring_delete(name)
    if rc < 0:
        raise RuntimeError("deleting ring '%s' failed: %s" % (name,strerror(-rc)))

def pair_rings(char *r1, char *r2):
    hal_required()
    id1 = 0
    id2 = 0
    with HALMutex():
        hr1 = halpr_find_ring_by_name(r1)
        if hr1 == NULL:
            raise RuntimeError("no such ring '%s'" % (r1))
        hr2 = halpr_find_ring_by_name(r2)
        if hr2 == NULL:
            raise RuntimeError("no such ring '%s'" % (r2))
        id1 = hr1.handle
        id2 = hr2.handle
    rc = hal_ring_pair(id1,id2)
    if rc < 0:
        raise RuntimeError("pairing '%s' '%s' failed: %s" % (r1,r2,strerror(-rc)))

def Ring(*args, **kwargs):
    ''' factory for new and existing rings'''

    cdef int type
    cdef hal_ring_t *hr
    hal_required()

    if len(args) == 1:
        # fixup old calling convention
        kwargs["name"] = args[0]

    type =  kwargs.get('type', RINGTYPE_RECORD) # the default

    # need to determine type for existing rings to properly wrap them
    if 'name' in kwargs:
        # wrapping by name
        with HALMutex():
            hr = halpr_find_ring_by_name(kwargs["name"])
            if hr != NULL:
                type = (hr.flags  & RINGTYPE_MASK)

    if 'id' in kwargs:
        # wrapping by id
        with HALMutex():
            hr = halpr_find_ring_by_id(kwargs["id"])
            if hr:
                type = (hr.flags  & RINGTYPE_MASK)

    if type == RINGTYPE_RECORD:
        return _RecordRing(**kwargs)
    if type == RINGTYPE_MULTIPART:
        return _MultiframeRing(**kwargs)
    if type == RINGTYPE_STREAM:
        return _StreamRing(**kwargs)

    raise RuntimeError("no such ring type: %d" % (type))

