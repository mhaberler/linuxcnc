



#= {
#    hal_const.HAL_PIN : Pin,
#    hal_const.HAL_SIGNAL        : Signal,
#    hal_const.HAL_PARAM         : Param,
#    hal_const.HAL_THREAD        : Thread,
#    hal_const.HAL_FUNCT         : 5,
#    hal_const.HAL_COMPONENT     : Component,
#    hal_const.HAL_VTABLE        : 7,
#    hal_const.HAL_INST          : Instance,
#    hal_const.HAL_RING          : 9,
#    hal_const.HAL_GROUP         : 10,
#    hal_const.HAL_MEMBER        : 11,
#}


# generic dictionary of HAL objects.
# instantiated with object type.

cdef class HALObjectDict:

    cdef int  _type
    cdef dict _objects

    def __cinit__(self, int type):
        if not type in _wrapdict:
            raise RuntimeError("unsupported type %d" % type)
        self._type = type
        self._objects = dict()

    # supposed to be 'private' - must be called
    # with HAL mutex held (!)

    def __getitem_unlocked__(self, name):
        hal_required()

        # this one is silly - and slow:
        # array-type indexing with ints
        # leave out for now, no good use case.
        # if isinstance(name, int):
        #     return object_names(0, self._type)[name]

        if name in self._objects:
            # already wrapped
            return self._objects[name]

        cdef hal_object_ptr ptr
        ptr = halg_find_object_by_name(0, self._type, name)
        if ptr.any == NULL:
            raise NameError, "no such %s: %s" % (hal_strtype(self._type), name)
        method = _wrapdict[self._type]
        w = method(name, lock=False)
        # add new wrapper
        self._objects[name] = w
        return w

    def __getitem__(self, name):
        with HALMutex():
            return self.__getitem_unlocked__(name)

    def __contains__(self, arg):
        if isinstance(arg, HALObject):
            arg = arg.name
        try:
            self.__getitem__(arg)
            return True
        except NameError:
            return False

    def __len__(self):
        hal_required()
        return object_count(1, self._type)

    def __call__(self):
        hal_required()
        return object_names(1, self._type)

    def __repr__(self):
        hal_required()
        d = {}
        for name in object_names(1, self._type):
            d[name] = self[name]
        return str(d)

# example instantiation:
# _wrapdict[hal_const.HAL_INST] = Instance
# instances = HALObjectDict(hal_const.HAL_INST)
