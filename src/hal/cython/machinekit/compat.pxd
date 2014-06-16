# vim: sts=4 sw=4 et

cdef extern from "rtapi_bitops.h":
    int RTAPI_BIT(int b)

cdef extern from "rtapi_compat.h":
    cdef int  FLAVOR_DOES_IO
    cdef int  FLAVOR_KERNEL_BUILD
    cdef int  FLAVOR_RTAPI_DATA_IN_SHM
    cdef int  POSIX_FLAVOR_FLAGS
    cdef int  RTPREEMPT_FLAVOR_FLAGS
    cdef int  RTAI_KERNEL_FLAVOR_FLAGS
    cdef int  XENOMAI_KERNEL_FLAVOR_FLAGS
    cdef int  XENOMAI_FLAVOR_FLAGS

    ctypedef struct flavor_t:
        const char *name
        const char *mod_ext
        const char *so_ext
        const char *build_sys
        int id
        unsigned long flags

    int is_module_loaded(const char *module)
    int load_module(const char *module, const char *modargs)
    int run_module_helper(const char *format)
    long int simple_strtol(const char *nptr, char **endptr, int base)

    int kernel_is_xenomai()
    int kernel_is_rtai()
    int kernel_is_rtpreempt()

    int xenomai_gid()
    int user_in_xenomai_group()
    int kernel_instance_id()

    flavor_t flavors[]
    flavor_t *flavor_byname(const char *flavorname)
    flavor_t *flavor_byid(int flavor_id)
    flavor_t *default_flavor()
    int module_path(char *result, const char *basename)
    int get_rtapi_config(char *result, const char *param, int n)
