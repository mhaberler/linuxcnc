// HAL signal API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_group.h"
#include "hal_internal.h"

/***********************************************************************
*                      "SIGNAL" FUNCTIONS                              *
************************************************************************/

int halg_signal_new(const int use_hal_mutex,
		    const char *name, hal_type_t type)
{
    hal_sig_t *new;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(name, HAL_NAME_LEN);
    HALDBG("creating signal '%s'", name);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	void *data_addr;

	/* check for an existing signal with the same name */
	if (halpr_find_sig_by_name(name) != 0) {
	    HALERR("duplicate signal '%s'", name);
	    return -EINVAL;
	}
	/* allocate memory for the signal value */
	switch (type) {
	case HAL_BIT:
	    data_addr = shmalloc_rt(sizeof(hal_bit_t));
	    break;
	case HAL_S32:
	    data_addr = shmalloc_rt(sizeof(hal_u32_t));
	    break;
	case HAL_U32:
	    data_addr = shmalloc_rt(sizeof(hal_s32_t));
	    break;
	case HAL_FLOAT:
	    data_addr = shmalloc_rt(sizeof(hal_float_t));
	    break;
	default:
	    HALERR("signal '%s': illegal signal type %d'", name, type);
	    return -EINVAL;
	    break;
	}

	// allocate signal descriptor
	if ((new = halg_create_objectf(0, sizeof(hal_sig_t),
				       HAL_SIGNAL, 0, name)) == NULL)
	    return -ENOMEM;

	/* initialize the signal value */
	switch (type) {
	case HAL_BIT:
	    *((hal_bit_t *) data_addr) = 0;
	    break;
	case HAL_S32:
	    *((hal_s32_t *) data_addr) = 0;
	    break;
	case HAL_U32:
	    *((hal_u32_t *) data_addr) = 0;
	    break;
	case HAL_FLOAT:
	    *((hal_float_t *) data_addr) = 0.0;
	    break;
	default:
	    break;
	}
	/* initialize the structure */
	new->data_ptr = SHMOFF(data_addr);
	new->type = type;
	new->readers = 0;
	new->writers = 0;
	new->bidirs = 0;

	// make it visible
	halg_add_object(false, (hal_object_ptr)new);
    }
    return 0;
}

// walk members and count references back to the signal descriptor
// normally this would be done by object ID, but for speed reasons
// (group matching) group members directly refer to the signal

int halg_signal_delete(const int use_hal_mutex, const char *name)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(name, HAL_NAME_LEN);
    HALDBG("deleting signal '%s'", name);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_sig_t *sig = halpr_find_sig_by_name(name);

	if (sig == NULL) {
	    HALERR("signal '%s' not found",  name);
	    return -ENOENT;
	}

	// free_sig_struct will unlink any linked pins
	// before freeing the signal descriptor
	return free_sig_struct(sig);
    }
}

// this implements Charles' 'handshake signal' idea.
// (barrier inheritance on linking)
static int propagate_barriers_cb(hal_pin_t *pin,
				 hal_sig_t *sig,
				 void *user)
{
    bool rmb = hh_get_rmb(&sig->hdr);
    bool wmb = hh_get_wmb(&sig->hdr);
    bool pin_rmb = hh_get_rmb(&pin->hdr);
    bool pin_wmb = hh_get_wmb(&pin->hdr);

    switch (pin->dir) {
    case HAL_IN:

	// Carles - please fill in here:

	// mah guess:
	if (rmb)
	    // only set, but do not clear by inheritance
	    hh_set_rmb(&pin->hdr, rmb);

	// mah guess: no significance
	//if (wmb)
	//	hh_set_wmb(&pin->hdr, WHAT);
	break;

    case  HAL_OUT:
	// mah guess: no significance
	// if (rmb)
	//    hh_set_rmb(&pin->hdr, rmb);

	// mah guess:
	if (wmb)
	    hh_set_wmb(&pin->hdr, rmb);
	break;

    case HAL_IO:
	// and here:
	if (rmb)
	    hh_set_rmb(&pin->hdr, rmb);
	if (wmb)
	    hh_set_wmb(&pin->hdr, rmb);
	break;

    default: ;
    }
    HALDBG("propagating barriers from signal '%s' to pin '%s':"
	   " rmb: %d->%d  wmb: %d->%d",
	   ho_name(sig),
	   ho_name(pin),
	   pin_rmb, hh_get_rmb(&pin->hdr),
	   pin_wmb, hh_get_wmb(&pin->hdr));
    return 0;
}

int halg_signal_propagate_barriers(const int use_hal_mutex,
				   const hal_sig_t *sig)
{
    CHECK_HALDATA();
    CHECK_NULL(sig);

    //a specialized form of halg_foreach_pin_by_signal()
    foreach_args_t args =  {
	.type = HAL_PIN,
	.user_ptr1 = (hal_sig_t *) sig,
	.user_ptr2 = propagate_barriers_cb,
    };
    halg_foreach(use_hal_mutex, &args, pin_by_signal_callback);
    return 0;
}

// set read and/or write barriers on a HAL signal,
// and propagate to linked pins
// read_barrier, write_barrier values:
//   0..unset
//   1..set
//  -1..leave as is
int halg_signal_setbarriers(const int use_hal_mutex,
			    const char *name,
			    const int read_barrier,
			    const int write_barrier)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(name, HAL_MAX_NAME_LEN);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_sig_t *sig = halpr_find_sig_by_name(name);

	if (sig == NULL) {
	    HALERR("signal '%s' not found",  name);
	    _halerrno = -ENOENT;
	    return -ENOENT;
	}
	halg_object_setbarriers(0,(hal_object_ptr) sig,
				read_barrier,
				write_barrier);

	// now propagate any changes to linked pins
	halg_signal_propagate_barriers(0, sig);
    }
    return 0;
}



int halg_link(const int use_hal_mutex,
	      const char *pin_name,
	      const char *sig_name)
{
    hal_sig_t *sig;
    hal_comp_t *comp;
    void **data_ptr_addr, *data_addr;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(pin_name, HAL_NAME_LEN);
    CHECK_STRLEN(sig_name, HAL_NAME_LEN);
    HALDBG("linking pin '%s' to '%s'",
	   pin_name,
	   sig_name);
    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_pin_t *pin;

	/* locate the pin */
	pin = halpr_find_pin_by_name(pin_name);
	if (pin == 0) {
	    HALERR("pin '%s' not found", pin_name);
	    return -EINVAL;
	}
	/* locate the signal */
	sig = halpr_find_sig_by_name(sig_name);
	if (sig == 0) {
	    HALERR("signal '%s' not found", sig_name);
	    return -EINVAL;
	}
	/* found both pin and signal, are they already connected? */
	if (SHMPTR(pin->signal) == sig) {
	    HALWARN("pin '%s' already linked to '%s'", pin_name, sig_name);
	    return 0;
	}
	/* is the pin connected to something else? */
	if (pin->signal) {
	    sig = SHMPTR(pin->signal);
	    HALERR("pin '%s' is linked to '%s', cannot link to '%s'",
		   pin_name, ho_name(sig), sig_name);
	    return -EINVAL;
	}
	/* check types */
	if (pin->type != sig->type) {
	    HALERR("type mismatch '%s':%d <- '%s':%d",
		   pin_name, pin->type,
		   sig_name, sig->type);
	    return -EINVAL;
	}
	/* linking output pin to sig that already has output or I/O pins? */
	if ((pin->dir == HAL_OUT) && ((sig->writers > 0) || (sig->bidirs > 0 ))) {
	    HALERR("signal '%s' already has output or I/O pin(s)", sig_name);
	    return -EINVAL;
	}
	/* linking bidir pin to sig that already has output pin? */
	if ((pin->dir == HAL_IO) && (sig->writers > 0)) {
	    HALERR("signal '%s' already has output pin", sig_name);
	    return -EINVAL;
	}
        /* everything is OK, make the new link */
	if (hh_get_legacy(&pin->hdr)) {
	    data_ptr_addr = SHMPTR(pin->data_ptr_addr);
	    comp = halpr_find_owning_comp(ho_owner_id(pin));
	    data_addr = comp->shmem_base + sig->data_ptr;
	    *data_ptr_addr = data_addr;
	} else {
	    pin->data_ptr = sig->data_ptr;
	}

	if (( sig->readers == 0 ) && ( sig->writers == 0 ) &&
	    ( sig->bidirs == 0 )) {

	    // this signal is not linked to any pins
	    // copy value from pin's "dummy" field,
	    // making it 'inherit' the value of the first pin
	    data_addr = hal_shmem_base + sig->data_ptr;

	    // assure proper typing on assignment, assigning a hal_data_u is
	    // a surefire cause for memory corrupion as hal_data_u is larger
	    // than hal_bit_t, hal_s32_t, and hal_u32_t - this works only for 
	    // hal_float_t (!)
	    // my old, buggy code:
	    //*((hal_data_u *)data_addr) = pin->dummysig;

	    switch (pin->type) {
	    case HAL_BIT:
		*((hal_bit_t *) data_addr) = pin->dummysig.b;
		break;
	    case HAL_S32:
		*((hal_s32_t *) data_addr) = pin->dummysig.s;
		break;
	    case HAL_U32:
		*((hal_u32_t *) data_addr) = pin->dummysig.u;
		break;
	    case HAL_FLOAT:
		*((hal_float_t *) data_addr) = pin->dummysig.f;
		break;
	    default:
		HALBUG("pin '%s' has invalid type %d !!\n",
		       ho_name(pin), pin->type);
		return -EINVAL;
	    }

	    /* update the signal's reader/writer/bidir counts */
	    if ((pin->dir & HAL_IN) != 0) {
		sig->readers++;
	    }
	    if (pin->dir == HAL_OUT) {
		sig->writers++;
	    }
	    if (pin->dir == HAL_IO) {
		sig->bidirs++;
	    }
	    /* and update the pin */
	    pin->signal = SHMOFF(sig);

	    // propagate the pin->signal assignment because
	    // halg_signal_propagate_barriers() triggers on
	    // pin->signal == SHMOFF(sig)
	    rtapi_smp_wmb();
	    halg_signal_propagate_barriers(0, sig);
	}
    }
    return 0;
}

int halg_unlink(const int use_hal_mutex,
		const char *pin_name)
{

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(pin_name, HAL_NAME_LEN);
    HALDBG("unlinking pin '%s'", pin_name);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_pin_t *pin;

	/* locate the pin */
	pin = halpr_find_pin_by_name(pin_name);

	if (pin == 0) {
	    /* not found */
	    HALERR("pin '%s' not found", pin_name);
	    return -EINVAL;
	}

	/* found pin, unlink it */
	unlink_pin(pin);

	return 0;
    }
}

int pin_by_signal_callback(hal_object_ptr o, foreach_args_t *args)
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

static int unlink_pin_callback(hal_pin_t *pin, hal_sig_t *sig, void *user)
{
    unlink_pin(pin);
    return 0; // continue
}

int free_sig_struct(hal_sig_t * sig)
{
    // unlink any pins linked to this signal
    halg_foreach_pin_by_signal(0, sig, unlink_pin_callback, NULL);
    return halg_free_object(false, (hal_object_ptr) sig);
}
