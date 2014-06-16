# rtapi_compat.c  python bindings

from .compat cimport *

from os import strerror, getpid
from libc.stdlib cimport malloc, free
from cpython.string cimport PyString_AsString


cdef Flavor_Init(flavor_t *f):
      result = Flavor()
      result._f = f
      return result

cdef class Flavor:
    cdef flavor_t *_f

    property name:
        def __get__(self): return self._f.name

    property mod_ext:
        def __get__(self): return self._f.mod_ext

    property so_ext:
        def __get__(self): return self._f.so_ext

    property build_sys:
        def __get__(self): return self._f.build_sys

    property id:
        def __get__(self): return self._f.id

    property flags:
        def __get__(self): return self._f.flags

def module_loaded(m):
    return is_module_loaded(m)

def is_xenomai():
    return bool(kernel_is_xenomai())

def is_rtai():
    return bool(kernel_is_rtai())

def is_rtpreempt():
    return bool(kernel_is_rtpreempt())

def xenomai_group_id():
    return xenomai_gid()

def in_xenomai_group():
    return bool(user_in_xenomai_group())

def kernel_instance():
    return kernel_instance_id()

def flavor_by_name(name):
    cdef flavor_t *f = flavor_byname(name)
    if f == NULL:
        raise RuntimeError("flavor_byname: no such flavor: %s" % name)
    return Flavor_Init(f)

def flavor_by_id(id):
    cdef flavor_t *f = flavor_byid(id)
    if f == NULL:
        raise RuntimeError("flavor_byid: no such flavor: %d" % id)
    return Flavor_Init(f)


def flavor():
    cdef flavor_t *f = default_flavor()
    if f == NULL:
        raise RuntimeError("BUG: flavor() failed")
    return Flavor_Init(f)

def module_helper(name):
    rc = run_module_helper(name)
    if rc:
        raise RuntimeError("module_helper(%s) failed: %d %s " % (name, rc, strerror(rc)))

def modpath(basename):
    cdef char result[1024]
    rc = module_path(result, basename)
    if rc:
        raise RuntimeError("modpath(%s) failed: %d %s " % (basename, rc, strerror(-rc)))
    return str(<char *>result)

def get_rtapiconfig(param):
    cdef char result[1024]
    rc = get_rtapi_config(result, param, 1024)
    if rc:
        raise RuntimeError("get_rtapiconfig(%s) failed: %d %s " % (param, rc, strerror(-rc)))
    return str(<char *>result)

