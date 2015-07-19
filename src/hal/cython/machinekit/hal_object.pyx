
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
            return hh_is_valid(self._o.hdr)

    cdef incref(self):
        self._alive_check()
        return hh_incr_refcnt(self._o.hdr)

    cdef decref(self):
        self._alive_check()
        return hh_decr_refcnt(self._o.hdr)
