

cdef class Instance(HALObject):
    #cdef hal_inst_t *_inst
    cdef hal_comp_t *_comp

    def __cinit__(self, name):
        hal_required()
        with HALMutex():
            # NB: self._o lives in the HALObject
            self._o.inst = halpr_find_inst_by_name(name)
            if self._o.inst == NULL:
                raise RuntimeError("instance %s does not exist" % name)

    # def delete(self):
    #     r = hal_inst_delete(self._inst.name)
    #     if (r < 0):
    #         raise RuntimeError("Fail to delete instance %s: %s" % (self._name, hal_lasterror()))

    property owner:
        def __get__(self):
            with HALMutex():
                self._comp = halpr_find_owning_comp(self.id)
                if self._comp == NULL:
                    raise RuntimeError("BUG: Failed to find"
                                       " owning comp %d of instance %s: %s" %
                                       (self.id, self.name, hal_lasterror()))
            # XXX use get_unlocked here!
            return Component(hh_get_name(&self._comp.hdr), wrap=True)

    property size:
        def __get__(self): return self._o.inst.inst_size

    property pins:
        def __get__(self):
            ''' return a list of Pin objects owned by this instance'''
            pinnames = []
            with HALMutex():
                # collect pin names
                pinnames = owned_names(0, hal_const.HAL_PIN,
                                       hh_get_id(&self._o.inst.hdr))
                pinlist = []
                for n in pinnames:
                    pinlist.append(pins.__getitem_unlocked__(n))
                return pinlist

    def pin(self, name, base=None):
        ''' return component Pin object, base does not need to be supplied if pin name matches component name '''
        if base == None:
            base = self.name
        return Pin('%s.%s' % (base, name))


    # TODO: add a bufferview of the shm area
