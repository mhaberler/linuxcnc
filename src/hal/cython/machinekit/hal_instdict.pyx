# Instances pseudo dictionary


cdef class Instances:
    cdef dict insts

    def __cinit__(self):
        self.insts = dict()

    def __getitem__(self, char *name):
        hal_required()

        if isinstance(name, int):
            return object_names(1, hal_const.HAL_INST)[name]

        if name in self.insts:
            return self.insts[name]

        cdef hal_inst_t *p
        p = halg_find_object_by_name(1, hal_const.HAL_INST, name).inst
        if p == NULL:
            raise NameError, "no such inst: %s" % (name)
        inst =  Instance(name)
        self.insts[name] = inst
        return inst

    def __contains__(self, arg):
        if isinstance(arg, Instance):
            arg = arg.name
        try:
            self.__getitem__(arg)
            return True
        except NameError:
            return False

    def __len__(self):
        hal_required()
        return object_count(1, hal_const.HAL_INST)

    def __call__(self):
        hal_required()
        return object_names(1, hal_const.HAL_INST)

    def __repr__(self):
        hal_required()
        instdict = {}
        for name in object_names(1, hal_const.HAL_INST):
            instdict[name] = self[name]
        return str(instdict)

instances = Instances()
