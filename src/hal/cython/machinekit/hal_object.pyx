# generic finders: find names, count of a given type of object
cdef int _append_name_cb(hal_object_ptr o,  foreach_args_t *args):
    arg =  <object>args.user_ptr1
    arg.append(hh_get_name(o.hdr))
    return 0

cdef list object_names(int lock, int type):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names

cdef int object_count(int lock,int type):
    cdef foreach_args_t args = nullargs
    args.type = type
    return halg_foreach(lock, &args, NULL)

# returns the names of owned objects of a give type
# owner id might be id of a comp or an inst
cdef list owned_names(int lock, int type, int owner_id):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.owner_id = owner_id
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names

cdef list comp_owned_names(int lock, int type, int comp_id):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.owning_comp = comp_id
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names



# methods and properties to expose the
# common HAL object header.

# see answer by ldav1s in
# http://stackoverflow.com/questions/12204441/passing-c-pointer-as-argument-into-cython-function
# why this is required:
# cdef HALObject_Init(hal_object_ptr o):
#       result = HALObject()
#       result._o = o
#       return result

cdef class HALObject:
    cdef hal_object_ptr _o

    cdef _alive_check(self):
        if self._o.any == NULL:
            raise RuntimeError("NULL object header")
        if hh_valid(self._o.hdr) == 0:
            raise RuntimeError("invalid object detected")

    property name:
        def __get__(self):
            self._alive_check()
            return hh_get_name(self._o.hdr)

    property id:
        def __get__(self):
            self._alive_check()
            return hh_get_id(self._o.hdr)

    property handle:  # deprecated name
        def __get__(self):
            return self.id

    property owner_id:
        def __get__(self):
            self._alive_check()
            return hh_get_owner_id(self._o.hdr)

    property type:
        def __get__(self):
            self._alive_check()
            return hh_get_type(self._o.hdr)

    property refcnt:
        def __get__(self):
            self._alive_check()
            return hh_get_refcnt(self._o.hdr)

    property valid:
        def __get__(self):
            return hh_valid(self._o.hdr) == 1

    cdef incref(self):
        self._alive_check()
        return hh_incr_refcnt(self._o.hdr)

    cdef decref(self):
        self._alive_check()
        return hh_decr_refcnt(self._o.hdr)
