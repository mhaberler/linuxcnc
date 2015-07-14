#include "hal_iter.h"


int halpr_foreach_ring(const char *name,
		       hal_ring_callback_t callback, void *cb_data)
{
    hal_ring_t *ring;
    int next;
    int nvisited = 0;
    int result;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);

    /* search for the ring */
    next = hal_data->ring_list_ptr;
    while (next != 0) {
	ring = SHMPTR(next);
	if (!name || (strcmp(ring->name, name)) == 0) {
	    nvisited++;
	    /* this is the right ring */
	    if (callback) {
		result = callback(ring, cb_data);
		if (result < 0) {
		    // callback signaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited rings.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count rings
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = ring->next_ptr;
    }
    /* if we get here, we ran through all the rings, so return count */
    return nvisited;
}

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
