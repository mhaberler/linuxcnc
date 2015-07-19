# Components pseudo dictionary

# # callback: add comp names into list
# cdef int _collect_comp_names(hal_comp_t *comp,  void *userdata):
#     arg =  <object>userdata
#     if  isinstance(arg, list):
#         arg.append(hh_get_name(&comp.hdr))
#         return 0
#     else:
#         return -1

# cdef list comp_names():
#     names = []

#     with HALMutex():
#         rc = halpr_foreach_comp(NULL,  _collect_comp_names, <void *>names);
#         if rc < 0:
#             raise RuntimeError("comp_names: halpr_foreach_comp failed %d: %s" %
#                                (rc, hal_lasterror()))
#     return names

# cdef int comp_count():
#     with HALMutex():
#         rc = halpr_foreach_comp(NULL, NULL, NULL);
#         if rc < 0:
#             raise RuntimeError("comp_count: halpr_foreach_comp failed %d: %s" %
#                                (rc, hal_lasterror()))
#     return rc


cdef class Components:
    cdef dict comps

    def __cinit__(self):
        self.comps = dict()

    def __getitem__(self, char *name):
        hal_required()

        if isinstance(name, int):
            return object_names(1, hal_const.HAL_COMPONENT)[name]

        if name in self.comps:
            return self.comps[name]

        cdef hal_comp_t *comp
        comp = halg_find_object_by_name(1, hal_const.HAL_COMPONENT, name).comp
        if comp == NULL:
            raise NameError, "no such component: %s" % (name)

        c = Component(name, wrap=True)
        self.comps[name] = c
        return c

    def __contains__(self, arg):
        if isinstance(arg, Component):
            arg = arg.name
        try:
            self.__getitem__(arg)
            return True
        except NameError:
            return False

    def __len__(self):
        hal_required()
        return object_count(1, hal_const.HAL_COMPONENT)

    def __call__(self):
        hal_required()
        return object_names(1, hal_const.HAL_COMPONENT)

    def __repr__(self):
        hal_required()
        compdict = {}
        for name in object_names(1, hal_const.HAL_COMPONENT):
            compdict[name] = self[name]
        return str(compdict)


components = Components()
