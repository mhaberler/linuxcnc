# Signals pseudo dictionary

# add signal names into list
# cdef int _collect_sig_names(hal_sig_t *sig,  void *userdata):
#     arg =  <object>userdata
#     if  isinstance(arg, list):
#         arg.append(hh_get_name(&sig.hdr))
#         return 0
#     else:
#         return -1

# cdef list sig_names():
#     names = []
#     with HALMutex():
#         rc = halpr_foreach_sig(NULL, _collect_sig_names, <void *>names);
#         if rc < 0:
#             raise RuntimeError("halpr_foreach_sig failed %d" % rc)
#     return names

# cdef int sig_count():
#     with HALMutex():
#         rc = halpr_foreach_sig(NULL, NULL, NULL);
#     return rc

cdef class Signals:
    cdef dict sigs

    def __cinit__(self):
        self.sigs = dict()

    def __getitem__(self, char *name):
        hal_required()

        # index by integer
        if isinstance(name, int):
            return object_names(1, hal_const.HAL_SIGNAL)[name]

        # index by name
        if name in self.sigs:
            return self.sigs[name]

        # if not found in dict, search hal layer and add
        cdef hal_sig_t *s
        s = halg_find_object_by_name(1, hal_const.HAL_SIGNAL, name).sig
        if s == NULL:
            raise NameError, "no such signal: %s" % (name)

        # wrap it
        sig =  Signal(name)
        self.sigs[name] = sig
        return sig

    def __contains__(self, arg):
        if isinstance(arg, Signal):
            arg = arg.name
        try:
            self.__getitem__(arg)
            return True
        except NameError:
            return False

    def __len__(self):
        hal_required()
        return object_count(1, hal_const.HAL_SIGNAL)

    def __delitem__(self, char *name):
        hal_required()
        r = hal_signal_delete(name)
        if r:
            raise RuntimeError("hal_signal_delete %s failed: %d %s" % (name, r, hal_lasterror()))

        # delete from dict as well
        del self.sigs[name]

    def __call__(self):
        hal_required()
        return object_names(1, hal_const.HAL_SIGNAL)

    def __repr__(self):
        hal_required()
        sigdict = {}
        for name in object_names(1, hal_const.HAL_SIGNAL):
            sigdict[name] = self[name]
        return str(sigdict)

signals = Signals()
