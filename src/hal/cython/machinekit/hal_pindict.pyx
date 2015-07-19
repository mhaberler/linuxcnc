# Pins pseudo dictionary


cdef class Pins:
    cdef dict pins

    def __cinit__(self):
        self.pins = dict()

    # supposed to be 'private' - must be called
    # with HAL mutex held (!)
    # see hal_signal.pyx for a use case
    def __getitem_unlocked__(self, name):
        hal_required()

        if isinstance(name, int):
            return object_names(0, hal_const.HAL_PIN)[name]

        if name in self.pins:
            return self.pins[name]
        cdef hal_pin_t *p
        p = halg_find_object_by_name(0, hal_const.HAL_PIN, name).pin
        if p == NULL:
            raise NameError, "no such pin: %s" % (name)
        pin =  Pin(name, lock=False)
        self.pins[name] = pin
        return pin

    def __getitem__(self, name):
        with HALMutex():
            return self.__getitem_unlocked__(name)

    def __contains__(self, arg):
        if isinstance(arg, Pin):
            arg = arg.name
        try:
            self.__getitem__(arg)
            return True
        except NameError:
            return False

    def __len__(self):
        hal_required()
        return object_count(1, hal_const.HAL_PIN)

    def __call__(self):
        hal_required()
        return object_names(1, hal_const.HAL_PIN)

    def __repr__(self):
        hal_required()
        pindict = {}
        for name in object_names(1, hal_const.HAL_PIN):
            pindict[name] = self[name]
        return str(pindict)

pins = Pins()
