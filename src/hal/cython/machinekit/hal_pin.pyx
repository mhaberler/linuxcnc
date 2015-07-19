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
    cdef hal_data_u **_storage

    def __cinit__(self, *args,  init=None, eps=0, lock=True):
        hal_required()
        self._storage = NULL
        if len(args) == 1:
            #  wrapping existing pin, args[0] = name
            self._o.pin = halg_find_object_by_name(lock,
                                                   hal_const.HAL_PIN,
                                                   args[0]).pin
            if self._o.pin == NULL:
                raise RuntimeError("no such pin %s" % args[0])
        else:
            # create a new pin and wrap it
            comp = args[0]
            name = args[1]
            t = args[2]
            dir = args[3]
            if (eps < 0) or (eps > MAX_EPSILON-1):
                raise RuntimeError("pin %s : epsilon"
                                   " index out of range" % (name, eps))

            self._storage = <hal_data_u **>halg_malloc(lock, sizeof(hal_data_u *))
            if self._storage == NULL:
                raise RuntimeError("Fail to allocate"
                                   " HAL memory for pin %s" % name)

            name = "{}.{}".format(comp.name, name)
            r = halg_pin_new(lock, name, t, dir,
                             <void **>(self._storage),
                             (<Component>comp).ho_id(comp))
            if r:
                raise RuntimeError("Fail to create pin %s:"
                                   " %d %s" % (name, r, hal_lasterror()))
            self._o.pin = halg_find_object_by_name(lock,
                                                 hal_const.HAL_PIN,
                                                 args[0]).pin
            self._o.pin.eps_index = eps

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
        if self._storage == NULL: # an existing pin, wrapped

            if pin_linked(self._o.pin):
                raise RuntimeError("cannot set value of linked pin %s:" %
                                   hh_get_name(&self._o.pin.hdr))

            # retrieve address of dummy signal
            _dptr = <hal_data_u *>&self._o.pin.dummysig
            return py2hal(self._o.pin.type, _dptr, v)
        else:
            # a pin we created
            return py2hal(self._o.pin.type, self._storage[0], v)

    def _get(self):
        cdef hal_data_u *_dptr
        cdef hal_sig_t *_sig
        if self._storage == NULL: # an existing pin, wrapped
            if pin_linked(self._o.pin):
                # get signal's data address
                _sig = <hal_sig_t *>shmptr(self._o.pin.signal);
                _dptr = <hal_data_u *>shmptr(_sig.data_ptr);
            else:
                # retrieve address of dummy signal
                _dptr = <hal_data_u *>&self._o.pin.dummysig
            return hal2py(self._o.pin.type, _dptr)
        else:
            # a pin we allocated storage for
            return hal2py(self._o.pin.type, self._storage[0])


class Pin(_Pin):
    def __init__(self, *args, init=None,eps=0,lock=True):
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
