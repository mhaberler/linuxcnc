#include "hal_iter.h"


static int pin_by_signal_callback(hal_object_ptr o, foreach_args_t *args)
{
    hal_sig_t *sig = args->user_ptr1;
    hal_pin_signal_callback_t cb = args->user_ptr2;

    if (o.pin->signal == SHMOFF(sig)) {
	if (cb) return cb(o.pin, sig, args->user_ptr3);
    }
    return 0;
}

int halg_foreach_pin_by_signal(const int use_hal_mutex,
			       hal_sig_t *sig,
			       hal_pin_signal_callback_t cb,
			       void *user)
{
    foreach_args_t args =  {
	.type = HAL_PIN,
	.user_ptr1 = sig,
	.user_ptr2 = cb,
	.user_ptr3 = user
    };
    return halg_foreach(use_hal_mutex,
			&args,
			pin_by_signal_callback);
}


int halg_foreach_type(const int use_hal_mutex,
		      const int type,
		      const char *name,
		      hal_pertype_callback_t cb,
		      void *arg)
{
    foreach_args_t args =  {
	.type = type,
	.name = (char *) name,
	.user_ptr1 = cb,
	.user_ptr1 = arg
    };
    return halg_foreach(use_hal_mutex,
			&args,
			yield_foreach);
}

hal_object_ptr halg_find_object_by_name(const int use_hal_mutex,
					const int type,
					const char *name)
{
    foreach_args_t args =  {
	.type = type,
	.name = (char *)name,
    };
    if (halg_foreach(use_hal_mutex, &args, yield_match))
	return (hal_object_ptr) args.user_ptr1;
    return HO_NULL;
}

hal_object_ptr halg_find_object_by_id(const int use_hal_mutex,
				      const int type,
				      const int id)
{
    foreach_args_t args =  {
	.type = type,
	.id = id
    };
    if (halg_foreach(use_hal_mutex, &args, yield_match) == 1)
	return (hal_object_ptr) args.user_ptr1;
    return HO_NULL;
}


