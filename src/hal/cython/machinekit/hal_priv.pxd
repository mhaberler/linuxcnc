# hal_priv.h declarations

from .hal cimport *

cdef extern from "hal_priv.h":
    int MAX_EPSILON
    int HAL_MAX_RINGS

    ctypedef struct hal_data_t:
        int version
        unsigned long mutex
        hal_s32_t shmem_avail
        int shmem_bot
        int shmem_top
        long base_period
        int threads_running
        int exact_base_period
        unsigned char lock
        # RTAPI_DECLARE_BITMAP(rings, HAL_MAX_RINGS);
        double *epsilon #[MAX_EPSILON]

    hal_data_t *hal_data
    char *hal_shmem_base

    ctypedef union hal_data_u:
        hal_bit_t b
        hal_s32_t s
        hal_s32_t u
        hal_float_t f

    ctypedef struct hal_pin_t:
        int next_ptr
        int data_ptr_addr
        int owner_ptr
        int signal
        hal_data_u dummysig
        int oldname
        hal_type_t type
        hal_pin_dir_t dir
        int handle
        int flags
        unsigned char eps_index
        #char name[HAL_NAME_LEN + 1];	/* pin name */
        char *name


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
