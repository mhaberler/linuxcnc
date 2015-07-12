
cdef extern from "hal_object.h":

    ctypedef struct halhdr_t:
       pass

    int hh_get_id(halhdr_t *h)
    int hh_get_owner_id(halhdr_t *h)
    char *hh_get_name(halhdr_t *h)

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

# typedef int (*hal_object_callback_t)  (hal_object_ptr object,
# 				       foreach_args_t *args)
# int halg_foreach(bool use_hal_mutex,
# 		 foreach_args_t *args,
# 		 const hal_object_callback_t callback)
