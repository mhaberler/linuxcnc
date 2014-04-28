# vim: sts=4 sw=4 et

from libc.errno cimport EAGAIN
from libc.string cimport memcpy
from cpython.buffer cimport PyBuffer_FillInfo
from cpython.bytes cimport PyBytes_AsString, PyBytes_Size, PyBytes_FromStringAndSize

from .hal cimport *

cdef extern int lib_module_id
cdef int comp_id = -1


# XXX this is a confused start at exposing  HAL groups as per http://goo.gl/u99dZv
# a halcmd example for groups/member use is here: http://goo.gl/iEjOwV
# I had those exposed fine in boost here: http://goo.gl/MDGLdi
# but I'd rather switch to Cython
# class_<HalMember>("Member",no_init)
#      .add_property("userarg1", &HalMember::get_userarg1, &HalMember::set_userarg1)
#      .add_property("epsilon", &HalMember::get_epsilon, &HalMember::set_epsilon)
#      .add_property("type", &HalMember::get_type)
#      .add_property("handle", &HalMember::get_handle)
#      ;

#  class_<HalGroup>("HalGroup",no_init)
#      .add_property("name", &HalGroup::get_name)
#      .add_property("refcount", &HalGroup::get_refcount, &HalGroup::set_refcount)
#      .add_property("userarg1", &HalGroup::get_userarg1, &HalGroup::set_userarg1)
#      .add_property("userarg2", &HalGroup::get_userarg2, &HalGroup::set_userarg2)
#      .add_property("handle", &HalGroup::get_handle)
#      .def("compile", &HalGroup::compile)
#      .def("changed", &HalGroup::changed)
#      .def("members", &HalGroup::members)
#      ;
#
# to get at the list of groups:
#    def("groups", groups);


cdef extern from "hal_group.h" :

    # XXX not sure how to expose these structs
    # XXX these probably should go into hal.pxd?

    ctypedef struct hal_member_t:
        int next_ptr
        int sig_member_ptr
        int group_member_ptr
        int userarg1
        double epsilon
        int handle

    ctypedef struct  hal_group_t:
        int next_ptr
        int refcount
        int userarg1
        int userarg2
        int handle
        char *name
        int member_ptr

    ctypedef struct hal_compiled_group_t:
        int magic
        hal_group_t *group
        int n_members
        int mbr_index
        int mon_index
        hal_member_t  **member
        unsigned long *changed
        int n_monitored
        hal_data_u    *tracking
        unsigned long user_flags
        void *user_data

        # XXX - I used callbacks for group reporting, see halextmodule:
        # XXX for C API usage see http://goo.gl/puiJeD

        # typedef int (*group_report_callback_t)(int,  hal_compiled_group_t *,
        # 				      hal_sig_t *sig, void *cb_data);

        # typedef int (*hal_group_callback_t)(hal_group_t *group,  void *cb_data);


        # extern int hal_group_new(const char *group, int arg1, int arg2);
        # extern int hal_group_delete(const char *group);

        # extern int hal_member_new(const char *group, const char *member, int arg1, double epsilon);
        # extern int hal_member_delete(const char *group, const char *member);

        # extern int hal_cgroup_report(hal_compiled_group_t *cgroup,
        # 			     group_report_callback_t report_cb,
        # 			     void *cb_data, int force_all);
        # extern int hal_cgroup_free(hal_compiled_group_t *cgroup);

        # // using code is supposed to hal_ref_group() when starting to use it
        # // this prevents group change or deletion during use
        # extern int hal_ref_group(const char *group);
        # // when done, a group should be unreferenced:
        # extern int hal_unref_group(const char *group);



cdef class Member:
    cdef hal_member_t *_member

    # XXX need to expose hal_member_t fields here

    def __cinit__(self, char *group, char *member, int arg1 = 0, double epsilon = 0.00001):
        if hal_member_new(group, member, arg1, epsilon):
            raise RuntimeError("hal_member_new failed")

    def __dealloc__(self):
        pass
        # ring_free(&self._ring)


cdef class Group:
    cdef hal_group_t *_group

    # XXX need to expose hal_group_t fields here

    def __cinit__(self, char *name, int arg1 = 0, int arg2 = 0):
        if hal_group_new(name, arg1, arg2):
            raise RuntimeError("hal_group_new failed")
#        else:
            # XXX find member
            # needs hal_mutex like so:
            # what I would need here is to call
            # hal_group_t *halpr_find_group_by_name(const char *name)
            # this must happen under hal_mutex:
            # {
            #    int dummy __attribute__((cleanup(halpr_autorelease_mutex)));
            #    rtapi_mutex_get(&(hal_data->mutex));
            #    self._hal_group = halpr_find_group_by_name(name)
            #    if (!self._hal_group)
            #       raise Bug
            # }


    def __dealloc__(self):
        pass
        # ring_free(&self._ring)


# module init function
def pygroup_init():
    global comp_id,lib_module_id
    cdef modname
    if (lib_module_id < 0):
        modname = "pygroup%d" % getpid()
        comp_id = hal_init(modname)
        hal_ready(comp_id)

# module exit handler
def pygroup_exit():
    global comp_id
    if comp_id > -1:
        hal_exit(comp_id)

pygroup_init()

import atexit
atexit.register(pygroup_exit)
