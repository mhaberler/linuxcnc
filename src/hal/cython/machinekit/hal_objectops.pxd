from .hal_priv cimport *
from .hal_group cimport *
from .hal_ring cimport *
from cpython.bool  cimport bool



cdef extern from "hal_object.h":

    ctypedef union hal_object_ptr:
        halhdr_t     *hdr
        hal_comp_t   *comp
        hal_inst_t   *inst
        hal_pin_t    *pin
        hal_param_t  *param
        hal_sig_t    *sig
        hal_group_t  *group
        hal_member_t *member
        hal_funct_t  *funct
        hal_thread_t *thread
        hal_vtable_t *vtable
        hal_ring_t   *ring
        void         *any

    ctypedef struct foreach_args_t:
        int type
        int id
        int owner_id
        int owning_comp
        char *name
        int user_arg1
        int user_arg2
        void *user_ptr1
        void *user_ptr2

    ctypedef int (*hal_object_callback_t) (hal_object_ptr object,  foreach_args_t *args)
    int halg_foreach(int use_hal_mutex,
                     foreach_args_t *args,
                     const hal_object_callback_t callback)

    int hh_get_id(halhdr_t *h)
    int hh_get_owner_id(halhdr_t *h)
    int hh_get_type(halhdr_t *h)
    char *hh_get_name(halhdr_t *h)

    int ho_id(hal_object_ptr h)
    int ho_owner_id(hal_object_ptr h)
    int ho_type(hal_object_ptr h)
    char *ho_name(hal_object_ptr h)
    char *ho_typestr(hal_object_ptr h)
    int hh_snprintf(char *buf, size_t size, halhdr_t *hh)
