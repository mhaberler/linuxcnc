# vim: sts=4 sw=4 et

cimport hal_const
# Copyright Pavel Shramov, 4/2014
# see http://psha.org.ru/cgit/psha/emc2.git/commit/?h=wip-cython
# License: MIT

cdef extern from "hal.h":
    int hal_init(char *name)
    int hal_exit(int comp_id)
    int hal_ready(int comp_id)
    const char * hal_comp_name(int comp_id)
    const char * hal_lasterror()
    hal_print_msg(int level, const char *msg)

    void * hal_malloc(long size)
    int hal_start_threads()
    int hal_stop_threads()

    ctypedef void (*hal_constructor_t) (const char *name, const int argc, const char**argv)

    ctypedef void (*hal_destructor_t) (const char *name, void *inst, const int inst_size)


    # superset of hal_init()
    int hal_xinit(int mode, int userarg1, int userarg2,
                  hal_constructor_t ctor, hal_destructor_t dtor, char *name)

    ctypedef int hal_bit_t
    ctypedef float hal_float_t
    ctypedef int hal_s32_t
    ctypedef unsigned hal_u32_t

    int hal_signal_new(const char *sig, hal_const.hal_type_t)
    int halg_signal_new(const int use_hal_mutex,
                       const char *sig,
                       hal_const.hal_type_t)
    int halg_signal_delete(const int use_hal_mutex,
                           const char *sig)
    int hal_signal_delete(const char *sig)

    int halg_link(const int use_hal_mutex,
                  const char *pin_name,
                  const char *sig_name)
    int hal_link(const char *pin, const char *sig)
    int halg_unlink(const int use_hal_mutex, const char *pin_name)

    int hal_unlink(const char *pin)
    int halg_pin_new(const int use_hal_mutex,
                     const char *name,
                     const int type,
                     const int dir,
                     void **data_ptr_addr,
                     const int owner_id)
    int hal_pin_new(const char *name, int type, int dir,
        void **data_ptr_addr, int comp_id)

    int hal_add_funct_to_thread(const char *funct_name,
                                const char *thread_name, int position)

    int hal_del_funct_from_thread(const char *funct_name, const char *thread_name)

    char *hal_lasterror()
