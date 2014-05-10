# hal_priv.h bindings
from .hal cimport *
#cdef int:


cdef extern from "hal_priv.h":
#int HAL_NAME_SIZE = HAL_NAME_LEN + 1

    ctypedef struct hal_comp_t:
        int next_ptr
        int comp_id
        int mem_id
        int type
        int state
        long int last_update
        long int last_bound
        long int last_unbound
        int pid
        void *shmem_base
        #char name[HAL_NAME_SIZE]
        char *name
#        constructor make
        int insmod_args
        int userarg1
        int userarg2

    hal_comp_t *halpr_find_comp_by_name(const char *name)
    hal_comp_t *halpr_find_comp_by_id(int id)
