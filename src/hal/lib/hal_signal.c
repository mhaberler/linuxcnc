// HAL signal API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_iter.h"
#include "hal_group.h"
#include "hal_internal.h"

void free_sig_struct(hal_sig_t * sig);

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
    HALDBG("creating signal '%s' lock=%d", name, use_hal_mutex);

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
	if ((new = shmalloc_desc(sizeof(hal_sig_t))) == NULL)
	    NOMEM("signal '%s'",  name);
	hh_init_hdrf(&new->hdr, HAL_SIGNAL, 0, "%s", name);

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
// descriptor
int count_memberships(hal_object_ptr o, foreach_args_t *args)
{
    hal_member_t *m = o.member;
    if (m->sig_ptr == args->user_arg1)
	args->user_arg2++; // # of member refs
    return 0; // continue
}

int halg_signal_delete(const int use_hal_mutex, const char *name)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(name, HAL_NAME_LEN);
    HALDBG("deleting signal '%s' lock=%d", name, use_hal_mutex);

    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);
	hal_sig_t *sig = halpr_find_sig_by_name(name);

	if (sig == NULL) {
	    HALERR("signal '%s' not found",  name);
	    return -ENOENT;
	}

	// check if this signal is a member in a group
	foreach_args_t args =  {
	    .type = HAL_MEMBER,
	    .user_arg1 = SHMOFF(sig)
	};
	halg_foreach(0, &args, count_memberships);
	if (args.user_arg2) {
	    HALERR("cannot delete signal '%s'"
		   " since it is a member of a group",
		   name);
	    return -EBUSY;
	}
	// free_sig_struct will unlink any linked pins
	// before freeing the signal descriptor
	free_sig_struct(sig);
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
    HALDBG("linking pin '%s' to '%s' lock=%d",
	   pin_name,
	   sig_name,
	   use_hal_mutex);
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
	    HALERR("type mismatch '%s' <- '%s'", pin_name, sig_name);
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
        data_ptr_addr = SHMPTR(pin->data_ptr_addr);
	comp = halpr_find_owning_comp(ho_owner_id(pin));
	data_addr = comp->shmem_base + sig->data_ptr;
	*data_ptr_addr = data_addr;

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
    }
    return 0;
}

int halg_unlink(const int use_hal_mutex,
		const char *pin_name)
{

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    CHECK_STRLEN(pin_name, HAL_NAME_LEN);
    HALDBG("unlinking pin '%s' lock=%d",
	   pin_name, use_hal_mutex);

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

static int unlink_pin_callback(hal_pin_t *pin, hal_sig_t *sig, void *user)
{
    unlink_pin(pin);
    return 0; // continue
}

void free_sig_struct(hal_sig_t * sig)
{
    // unlink any pins linked to this signal
    halg_foreach_pin_by_signal(0, sig, unlink_pin_callback, NULL);
    halg_free_object(false, (hal_object_ptr) sig);
}
