# generic finders: find names, count of a given type of object

cdef int _append_name_cb(hal_object_ptr o,  foreach_args_t *args):
    arg =  <object>args.user_ptr1
    arg.append(hh_get_name(o.hdr))
    return 0

cdef list object_names(int lock, int type):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names

cdef int object_count(int lock,int type):
    cdef foreach_args_t args = nullargs
    args.type = type
    return halg_foreach(lock, &args, NULL)


# returns the names of owned objects of a give type
# owner id might be id of a comp or an inst
cdef list owned_names(int lock, int type, int owner_id):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.owner_id = owner_id
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names

cdef list comp_owned_names(int lock, int type, int comp_id):
    names = []
    cdef foreach_args_t args = nullargs
    args.type = type
    args.owning_comp = comp_id
    args.user_ptr1 = <void *>names
    halg_foreach(lock, &args, _append_name_cb)
    return names
