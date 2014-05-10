# vim: sts=4 sw=4 et

cdef extern from "rtapi_shmkeys.h":
    cdef int DEFAULT_MOTION_SHMEM_KEY
    cdef int GLOBAL_KEY
    cdef int SCOPE_SHM_KEY
    cdef int HAL_KEY
    cdef int RTAPI_KEY
    cdef int RTAPI_RING_SHM_KEY

cdef extern from "rtapi.h":
    int rtapi_init(const char *name)
    int rtapi_exit(int comp_id)
    int rtapi_next_handle()

    void rtapi_mutex_give(unsigned long *mutex)
    void rtapi_mutex_get(unsigned long *mutex)
    int rtapi_mutex_try(unsigned long *mutex)

    long long int rtapi_get_time()
    long long int rtapi_get_clocks()

    int rtapi_shmem_new(int key, int module_id, unsigned long int size)
    int rtapi_shmem_delete(int shmem_id, int module_id)
    int rtapi_shmem_getptr(int shmem_id, void **ptr, unsigned long int *size)


