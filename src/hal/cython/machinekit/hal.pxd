# vim: sts=4 sw=4 et

# Copyright Pavel Shramov, 4/2014
# see http://psha.org.ru/cgit/psha/emc2.git/commit/?h=wip-cython
# License: MIT

cdef extern from "hal.h":
    int hal_init(const char *name)
    int hal_exit(int comp_id)
    int hal_ready(int comp_id)
    const char * hal_comp_name(int comp_id)

    void * hal_malloc(long size)

    # XXX new - superset of hal_init()
    int hal_init_mode(const char *name, int mode, int userarg1, int userarg2)

    # XXX acquiring and binding remote components
    int hal_bind(const char *comp)
    int hal_unbind(const char *comp)
    int hal_acquire(const char *comp, int pid)
    int hal_release(const char *comp_name)


    ctypedef enum hal_type_t:
        HAL_TYPE_UNSPECIFIED
        HAL_BIT
        HAL_FLOAT
        HAL_S32
        HAL_U32

    ctypedef enum hal_pin_dir_t:
        HAL_DIR_UNSPECIFIED
        HAL_IN
        HAL_OUT
        HAL_IO

    ctypedef enum hal_param_dir_t:
        HAL_RO
        HAL_RW

    ctypedef enum comp_type:
        TYPE_INVALID
        TYPE_RT
        TYPE_USER
        TYPE_INSTANCE
        TYPE_REMOTE

    ctypedef enum comp_state:
        COMP_INVALID
        COMP_INITIALIZING
        COMP_UNBOUND
        COMP_BOUND
        COMP_READY

    ctypedef int hal_bit_t
    ctypedef float hal_float_t
    ctypedef int hal_s32_t
    ctypedef int hal_u32_t

    int hal_signal_new(const char *sig, hal_type_t)
    int hal_signal_delete(const char *sig)

    int hal_link(const char *pin, const char *sig)
    int hal_unlink(const char *pin)

    int hal_pin_new(const char *name, hal_type_t type, hal_pin_dir_t dir,
        void **data_ptr_addr, int comp_id)

# XXX ops for remote components
cdef extern from "hal_rcomp.h":

    ctypedef struct hal_comp_t:
        pass

    ctypedef enum rcompflags_t:
        RCOMP_ACCEPT_VALUES_ON_BIND

        # XXX
        # typedef int(*comp_report_callback_t)(int,  hal_compiled_comp_t *,
	# 			     hal_pin_t *pin,
	# 			     hal_data_u *value,
	# 			     void *cb_data);

    ctypedef struct hal_compiled_comp_t:
        pass

    int hal_compile_comp(const char *name, hal_compiled_comp_t **ccomp)
    int hal_ccomp_match(hal_compiled_comp_t *ccomp)

    # XXX NB: a callback here, see halextmodule.cc how I did this in boost:
    # int hal_ccomp_report(hal_compiled_comp_t *ccomp,
    #     		    comp_report_callback_t report_cb,
    #     		    void *cb_data, int report_all)
    int hal_ccomp_free(hal_compiled_comp_t *ccomp)
    int hal_ccomp_args(hal_compiled_comp_t *ccomp, int *arg1, int *arg2)






"""
int hal_pin_bit_new(const char *name, hal_pin_dir_t dir,
    hal_bit_t ** data_ptr_addr, int comp_id)
int hal_pin_float_new(const char *name, hal_pin_dir_t dir,
    hal_float_t ** data_ptr_addr, int comp_id)
int hal_pin_u32_new(const char *name, hal_pin_dir_t dir,
    hal_u32_t ** data_ptr_addr, int comp_id)
int hal_pin_s32_new(const char *name, hal_pin_dir_t dir,
    hal_s32_t ** data_ptr_addr, int comp_id)
"""

