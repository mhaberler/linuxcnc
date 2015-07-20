from .hal_util cimport shmptr #hal2py, py2hal, shmptr, valid_dir, valid_type
from .hal_priv cimport MAX_EPSILON, hal_data
from .hal_group cimport *
from .rtapi cimport  RTAPI_BIT_TEST


cdef class Group(HALObject):
    cdef hal_compiled_group_t *_cg

    def __cinit__(self, char *name, int arg1=0, int arg2=0, lock=True):
        hal_required()
        self._cg = NULL
        self._o.group = halg_find_object_by_name(lock,
                                                 hal_const.HAL_GROUP,
                                                 name).group
        if self._o.group == NULL:
            # not found, create a new group
            r = halg_group_new(lock, name, arg1, arg2)
            if r:
                raise RuntimeError("Failed to create group %s: %s" %
                                   (name, hal_lasterror()))
            self._o.group = halg_find_object_by_name(lock,
                                                     hal_const.HAL_GROUP,
                                                     name).group
            if self._o.group == NULL:
                raise InternalError("BUG: cannot find group '%s'" % name)

        else:
            # if wrapping existing group and args are nonzero, they better match up
            if arg1:
                if self.userarg1 != arg1:
                    raise RuntimeError("userarg1 does not match for existing group %s: %d, was %d" %
                                       (name, arg1, self.userarg1))
            if arg2:
                if self.userarg2 != arg2:
                    raise RuntimeError("userarg2 does not match for existing group %s: %d, was %d" %
                                       (name, arg2, self.userarg2))

    # def signal_members(self):  
    #     result = []
    #     rc = halpr_foreach_member(self.name, _list_signal_members_cb,
    #                               <void*>result, RESOLVE_NESTED_GROUPS);
    #     if rc < 0:
    #         raise RuntimeError("signal_members: halpr_foreach_member(%s) failed %d" %
    #                            (self.name,rc))
    #     return result

    def members(self):  # members resolved into signals
        return owned_names(1, hal_const.HAL_GROUP, self.id)

    def changed(self):
        cdef hal_sig_t *s
        if self._cg == NULL:
            self.compile()
        if self._cg.n_monitored == 0:
            return []
        if hal_cgroup_match(self._cg) == 0:
            return []
        result = []
        for i in range(self._cg.n_members):
            if RTAPI_BIT_TEST(self._cg.changed, i):
                s = <hal_sig_t *>shmptr(self._cg.member[i].sig_ptr)
                result.append(signals[hh_get_name(&s.hdr)])
        return result

    def compile(self):
        with HALMutex():
            hal_cgroup_free(self._cg)
            rc =  halpr_group_compile(self.name, &self._cg)
            if rc < 0:
                raise RuntimeError("hal_group_compile(%s) failed: %s" %
                                   (self.name, hal_lasterror()))

    def add(self, member, int arg1=0, int eps_index=0):
        if isinstance(member, Signal):
            member = member.name
        rc = halg_member_new(1, self.name, member, arg1, eps_index)
        if rc:
            raise RuntimeError("Failed to add member '%s' to  group '%s': %s" %
                               (member, self.name, hal_lasterror()))

    def delete(self, member):
        if isinstance(member, Signal) or isinstance(member, Group):
            member = member.name
        rc = halg_member_delete(1, self.name, member)
        if rc:
            raise RuntimeError("Failed to delete member '%s' from  group '%s': %s" %
                               (member, self.name, hal_lasterror()))
    property userarg1:
        def __get__(self): return self._o.group.userarg1
        def __set__(self, int r): self._o.group.userarg1 = r

    property userarg2:
        def __get__(self): return self._o.group.userarg2
        def __set__(self, int r): self._o.group.userarg2 = r



# see last answer why this is required:
# http://stackoverflow.com/questions/12204441/passing-c-pointer-as-argument-into-cython-function

# cdef Member_Init(hal_member_t *m):
#       result = Member()
#       result._m = m
#       return result

cdef class Member(HALObject):
    cdef hal_sig_t *s
    cdef hal_group_t *g

    def __cinit__(self):
        hal_required()

    property item:
        def __get__(self):
            if self._o.member.sig_ptr == 0:
                raise InternalError("BUG: __call__: sig_ptr zero")
            # signal has same name as member
            return signals[self.name]

    property epsilon:
        def __get__(self):
            return hal_data.epsilon[self._o.member.eps_index]

    property eps:
        def __get__(self): return self._o.member.eps_index
        def __set__(self, int eps):
            if (eps < 0) or (eps > MAX_EPSILON-1):
                raise InternalError("member %s : epsilon index out of range" % (self._name(), eps))
            self._o.member.eps_index = eps

    property userarg1:
        def __get__(self): return self._o.member.userarg1

_wrapdict[hal_const.HAL_GROUP] = Group
groups = HALObjectDict(hal_const.HAL_GROUP)
