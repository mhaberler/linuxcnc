from .hal_priv cimport MAX_EPSILON, hal_data_u
from .hal_util cimport shmptr, pin_linked, linked_signal,py2hal,hal2py


def describe_hal_type(haltype):
    if haltype == HAL_FLOAT:
        return 'float'
    elif haltype == HAL_BIT:
        return 'bit'
    elif haltype == HAL_U32:
        return 'u32'
    elif haltype == HAL_S32:
        return 's32'
    else:
        return 'unknown'

def describe_hal_dir(haldir):
    if haldir == HAL_IN:
        return 'in'
    elif haldir == HAL_OUT:
        return 'out'
    elif haldir == HAL_IO:
        return 'io'
    else:
        return 'unknown'


cdef class _Pin(HALObject):
#    cdef hal_data_u **_storage

    def __cinit__(self, *args,  init=None, eps=0, wrap=True, lock=True):
        hal_required()
        # storage will be nonzero only if the pin was created
        # by this class:
        #self._storage = NULL

        # _Pin() has slightly different calling conventions due to
        # reasons lost in history, so fudge the wrap attribute
        # _Pin(<string>,**kw) will wrap an existing pin
        wrap = (len(args) == 1)
        with HALMutexIf(lock):
            if wrap:
                name = args[0] # string - wrapping existing pin
                self._o.pin = halg_find_object_by_name(0,
                                                       hal_const.HAL_PIN,
                                                       name).pin
                if self._o.pin == NULL:
                    raise RuntimeError("no such pin %s" % name)
            else:
                # create a new pin and wrap it
                comp = args[0]  # a Component instance
                name = args[1]  # pin name, string
                t = args[2]     # type, int
                dir = args[3]   # direction, int

                # prepend comp name and "."
                name = "{}.{}".format(comp.name, name)

                if (eps < 0) or (eps > MAX_EPSILON-1):
                    raise RuntimeError("pin %s : epsilon"
                                       " index out of range" % (name, eps))

                # self._storage = <hal_data_u **>halg_malloc(0, sizeof(hal_data_u *))
                # if self._storage == NULL:
                #     raise RuntimeError("Fail to allocate"
                #                        " HAL memory for pin %s" % name)
                self._o.pin = halg_pin_new(0, name, t, dir,
                                 NULL, #v2 # <void **>(self._storage),
                                 (<Component>comp).id)
                if self._o.pin == NULL:
                    raise RuntimeError("Fail to create pin %s:"
                                       " %d %s" % (name, _halerrno, hal_lasterror()))
                # self._o.pin = halg_find_object_by_name(0,
                #                                        hal_const.HAL_PIN,
                #                                        name).pin
                self._o.pin.eps_index = eps
                if self._o.pin == NULL:
                    raise RuntimeError("failed to lookup newly created pin %s" % name)
            # handle initial value assignment!!

    property linked:
        def __get__(self): return pin_linked(self._o.pin)

    property signame:
        def __get__(self):
            if not  pin_linked(self._o.pin): return None  # raise exception?
            return hh_get_name(&linked_signal(self._o.pin).hdr)

    property signal:
        def __get__(self):
            if not  pin_linked(self._o.pin): return None  # raise exception?
            return signals[self.signame]

    property type:
        def __get__(self):
             return self._o.pin.type

    property epsilon:
        def __get__(self): return hal_data.epsilon[self._o.pin.eps_index]

    property eps:
        def __get__(self): return self._o.pin.eps_index
        def __set__(self, int eps):
            if (eps < 0) or (eps > MAX_EPSILON-1):
                raise RuntimeError("pin %s : epsilon index out of range" %
                                   (hh_get_name(&self._o.pin.hdr), eps))
            self._o.pin.eps_index = eps

    property dir:
        def __get__(self): return self._o.pin.dir

    def link(self, arg):
        # check if we have a pin or a list of pins
        if isinstance(arg, Pin) \
           or (isinstance(arg, str) and (arg in pins)) \
           or hasattr(arg, '__iter__'):
            return net(self, arg)  # net is more verbose than link

        # we got a signal or a new signal
        return net(arg, self)

    def __iadd__(self, pins):
        return self.link(pins)

    def unlink(self):
        r = hal_unlink(hh_get_name(&self._o.pin.hdr))
        if r:
            raise RuntimeError("Failed to unlink pin %s: %d - %s" %
                               (hh_get_name(&self._o.pin.hdr), r, hal_lasterror()))

    def _set(self, v):
        cdef hal_data_u *_dptr
        #if self._storage == NULL: # an existing pin, wrapped

        if pin_linked(self._o.pin):
            raise RuntimeError("cannot set value of linked pin %s:" %
                               hh_get_name(&self._o.pin.hdr))

        # retrieve address of dummy signal
        _dptr = <hal_data_u *>&self._o.pin.dummysig
        return py2hal(self._o.pin.type, _dptr, v)
        # else:
        #     # a pin we created
        #     return py2hal(self._o.pin.type, self._storage[0], v)

    def _get(self):
        cdef hal_data_u *_dptr
        cdef hal_sig_t *_sig
        #if self._storage == NULL: # an existing pin, wrapped
        if pin_linked(self._o.pin):
            # get signal's data address
            _sig = <hal_sig_t *>shmptr(self._o.pin.signal);
            _dptr = <hal_data_u *>shmptr(_sig.data_ptr);
        else:
            # retrieve address of dummy signal
            _dptr = <hal_data_u *>&self._o.pin.dummysig
        return hal2py(self._o.pin.type, _dptr)
        # else:
        #     # a pin we allocated storage for
        #     return hal2py(self._o.pin.type, self._storage[0])


class Pin(_Pin):
    def __init__(self, *args, init=None,eps=0,wrap=True,lock=True):
        if len(args) == 1: # wrapped existing pin
            t = self.type
            dir = self.dir
            name = self.name
        else:
            t = args[2]   # created new pin
            dir = args[3]

        if relaxed or dir != HAL_IN:
            self.set = self._set
        if relaxed or dir != HAL_OUT:
            self.get = self._get
        if init:
            self.set(init)

    def set(self, v): raise NotImplementedError("Pin is read-only")
    def get(self): raise NotImplementedError("Pin is write-only")

    def __repr__(self):
        return "<hal.Pin %s %s %s %s>" % (self.name,
                                          describe_hal_type(self.type),
                                          describe_hal_dir(self.dir),
                                          self.get())
# instantiate the pins dict
_wrapdict[hal_const.HAL_PIN] = Pin
pins = HALObjectDict(hal_const.HAL_PIN)
