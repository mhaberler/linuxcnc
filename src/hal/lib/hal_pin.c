// HAL pin API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_group.h"
#include "hal_internal.h"

/***********************************************************************
*                        "PIN" FUNCTIONS                               *
************************************************************************/

int hal_pin_newfv(hal_type_t type,
		  hal_pin_dir_t dir,
		  void ** data_ptr_addr,
		  int owner_id,
		  const char *fmt,
		  va_list ap)
{
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid for name starting '%s'",
	       sz, name);
        return -ENOMEM;
    }
    return hal_pin_new(name, type, dir, data_ptr_addr, owner_id);
}

int hal_pin_bit_newf(hal_pin_dir_t dir,
    hal_bit_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_BIT, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_float_newf(hal_pin_dir_t dir,
    hal_float_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_FLOAT, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_u32_newf(hal_pin_dir_t dir,
    hal_u32_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_U32, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_s32_newf(hal_pin_dir_t dir,
    hal_s32_t ** data_ptr_addr, int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_S32, dir, (void**)data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

// printf-style version of hal_pin_new()
int hal_pin_newf(hal_type_t type,
		 hal_pin_dir_t dir,
		 void ** data_ptr_addr,
		 int owner_id,
		 const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(type, dir, data_ptr_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

/* this is a generic function that does the majority of the work. */

int hal_pin_new(const char *name, hal_type_t type, hal_pin_dir_t dir,
		    void **data_ptr_addr, int owner_id)
{
    hal_pin_t *new;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_LOAD);
    CHECK_STRLEN(name, HAL_NAME_LEN);

    if(*data_ptr_addr)
    {
	HALERR("pin '%s': called with already-initialized memory", name);
    }
    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	HALERR("pin '%s': pin type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U32 (%d)",
	       name, type);
	return -EINVAL;
    }

    if(dir != HAL_IN && dir != HAL_OUT && dir != HAL_IO) {
	HALERR("pin '%s': pin direction not one of HAL_IN, HAL_OUT, or HAL_IO (%d)",
	       name, dir);
	return -EINVAL;
    }

    HALDBG("creating pin '%s'", name);

    {
	WITH_HAL_MUTEX();

	hal_comp_t *comp;

	if (halpr_find_inst_by_name(name) != NULL) {
	    HALERR("duplicate pin '%s'", name);
	    return -EEXIST;
	}

	/* validate comp_id */
	comp = halpr_find_owning_comp(owner_id);
	if (comp == 0) {
	    /* bad comp_id */
	    HALERR("pin '%s': owning component %d not found",
		   name, owner_id);
	    return -EINVAL;
	}

	/* validate passed in pointer - must point to HAL shmem */
	if (! SHMCHK(data_ptr_addr)) {
	    /* bad pointer */
	    HALERR("pin '%s': data_ptr_addr not in shared memory", name);
	    return -EINVAL;
	}

	// this will be 0 for legacy comps which use comp_id
	hal_inst_t *inst = halpr_find_inst_by_id(owner_id);
	int inst_id = (inst ? ho_id(inst) : 0);

	// instances may create pins post hal_ready
	if ((inst_id == 0) && (comp->state > COMP_INITIALIZING)) {
	    // legacy error message. Never made sense.. why?
	    HALERR("pin '%s': hal_pin_new called after hal_ready (%d)",
		   name, comp->state);
	    return -EINVAL;
	}

	// allocate pin descriptor
	if ((new = shmalloc_desc(sizeof(hal_pin_t))) == NULL)
	    NOMEM("pin '%s'",  name);
	hh_init_hdrf(&new->hdr, HAL_PIN, owner_id, "%s", name);

	/* initialize the structure */
	new->data_ptr_addr = SHMOFF(data_ptr_addr);
	new->type = type;
	new->dir = dir;
	new->signal = 0;
	memset(&new->dummysig, 0, sizeof(hal_data_u));
	/* make 'data_ptr' point to dummy signal */
	*data_ptr_addr = comp->shmem_base + SHMOFF(&(new->dummysig));

	// make it visible
	add_object(&new->hdr);
	return 0;
    }
}

void unlink_pin(hal_pin_t * pin)
{
    hal_sig_t *sig;
    hal_comp_t *comp;
    void **data_ptr_addr;
    hal_data_u *dummy_addr, *sig_data_addr;

    /* is this pin linked to a signal? */
    if (pin->signal != 0) {
	/* yes, need to unlink it */
	sig = SHMPTR(pin->signal);
	/* make pin's 'data_ptr' point to its dummy signal */
	data_ptr_addr = SHMPTR(pin->data_ptr_addr);
	comp = halpr_find_owning_comp(ho_owner_id(pin));
	dummy_addr = comp->shmem_base + SHMOFF(&(pin->dummysig));
	*data_ptr_addr = dummy_addr;

	/* copy current signal value to dummy */
	sig_data_addr = (hal_data_u *)(hal_shmem_base + sig->data_ptr);
	dummy_addr = (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig)));

	switch (pin->type) {
	case HAL_BIT:
	    dummy_addr->b = sig_data_addr->b;
	    break;
	case HAL_S32:
	    dummy_addr->s = sig_data_addr->s;
	    break;
	case HAL_U32:
	    dummy_addr->u = sig_data_addr->u;
	    break;
	case HAL_FLOAT:
	    dummy_addr->f = sig_data_addr->f;
	    break;
	default:
	    hal_print_msg(RTAPI_MSG_ERR,
			  "HAL: BUG: pin '%s' has invalid type %d !!\n",
			  ho_name(pin), pin->type);
	}

	/* update the signal's reader/writer counts */
	if ((pin->dir & HAL_IN) != 0) {
	    sig->readers--;
	}
	if (pin->dir == HAL_OUT) {
	    sig->writers--;
	}
	if (pin->dir == HAL_IO) {
	    sig->bidirs--;
	}
	/* mark pin as unlinked */
	pin->signal = 0;
    }
}

#warning REVIVE ME
#if 0
// find a pin by owner id, which may refer to a instance or a comp
hal_pin_t *halpr_find_pin_by_owner_id(const int owner_id, hal_pin_t * start)
{
    int next;
    hal_pin_t *pin;

    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of pin list */
	next = hal_data->pin_list_ptr;
    } else {
	/* no, start at next pin */
	next = start->next_ptr;
    }
    while (next != 0) {
	pin = SHMPTR(next);
	if (pin->owner_id == owner_id) {
	    /* found a match */
	    return pin;
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_pin_t *halpr_find_pin_by_sig(hal_sig_t * sig, hal_pin_t * start)
{
    int sig_ptr, next;
    hal_pin_t *pin;

    /* get offset of 'sig' component */
    sig_ptr = SHMOFF(sig);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of pin list */
	next = hal_data->pin_list_ptr;
    } else {
	/* no, start at next pin */
	next = start->next_ptr;
    }
    while (next != 0) {
	pin = SHMPTR(next);
	if (pin->signal == sig_ptr) {
	    /* found a match */
	    return pin;
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}
#endif

void free_pin_struct(hal_pin_t * pin)
{
    unlink_pin(pin);
    free_halobject((hal_object_ptr) pin);
}
