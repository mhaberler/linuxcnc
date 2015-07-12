#include "hal_iter.h"

int halpr_foreach_comp(const char *name,
		       hal_comp_callback_t callback, void *cb_data)
{
    hal_comp_t *comp;
    int next;
    int nvisited = 0;
    int result;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);

    /* search for the comp */
    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = SHMPTR(next);
	if (!name || (strcmp(comp->name, name)) == 0) {
	    nvisited++;
	    /* this is the right comp */
	    if (callback) {
		result = callback(comp, cb_data);
		if (result < 0) {
		    // callback signaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited comps.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count comps
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = comp->next_ptr;
    }
    /* if we get here, we ran through all the comps, so return count */
    return nvisited;
}

#if 0
int halpr_foreach_sig(const char *name,
		       hal_sig_callback_t callback, void *cb_data)
{
    hal_sig_t *sig;
    int next;
    int nvisited = 0;
    int result;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);

    /* search for the sig */
    next = hal_data->sig_list_ptr;
    while (next != 0) {
	sig = SHMPTR(next);
	if (!name || (strcmp(sig->name, name)) == 0) {
	    nvisited++;
	    /* this is the right sig */
	    if (callback) {
		result = callback(sig, cb_data);
		if (result < 0) {
		    // callback signaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited sigs.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count sigs
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = sig->next_ptr;
    }
    /* if we get here, we ran through all the sigs, so return count */
    return nvisited;
}
#endif

int halpr_foreach_thread(const char *name,
		       hal_thread_callback_t callback, void *cb_data)
{
    hal_thread_t *thread;
    int next;
    int nvisited = 0;
    int result;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);

    /* search for the thread */
    next = hal_data->thread_list_ptr;
    while (next != 0) {
	thread = SHMPTR(next);
	if (!name || (strcmp(thread->name, name)) == 0) {
	    nvisited++;
	    /* this is the right thread */
	    if (callback) {
		result = callback(thread, cb_data);
		if (result < 0) {
		    // callback threadnaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited threads.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count threads
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = thread->next_ptr;
    }
    /* if we get here, we ran through all the threads, so return count */
    return nvisited;
}

#if 0
int halpr_foreach_funct(const char *name,
		       hal_funct_callback_t callback, void *cb_data)
{
    hal_funct_t *funct;
    int next;
    int nvisited = 0;
    int result;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);

    /* search for the funct */
    next = hal_data->funct_list_ptr;
    while (next != 0) {
	funct = SHMPTR(next);
	if (!name || (strcmp(funct->name, name)) == 0) {
	    nvisited++;
	    /* this is the right funct */
	    if (callback) {
		result = callback(funct, cb_data);
		if (result < 0) {
		    // callback signaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited functs.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count functs
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = funct->next_ptr;
    }
    /* if we get here, we ran through all the functs, so return count */
    return nvisited;
}
#endif
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
    if (halg_foreach(use_hal_mutex, &args, yield_match))
	return (hal_object_ptr) args.user_ptr1;
    return HO_NULL;
}
