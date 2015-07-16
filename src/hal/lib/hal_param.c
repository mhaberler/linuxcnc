// HAL param API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

/***********************************************************************
*                       "PARAM" FUNCTIONS                              *
************************************************************************/

static int hal_param_newfv(hal_type_t type,
			   hal_param_dir_t dir,
			   volatile void *data_addr,
			   int owner_id,
			   const char *fmt,
			   va_list ap)
{
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid too long for name starting '%s'\n",
	       sz, name);
	return -ENOMEM;
    }
    return hal_param_new(name, type, dir, (void *) data_addr, owner_id);
}

int hal_param_bit_newf(hal_param_dir_t dir, hal_bit_t * data_addr,
		       int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_BIT, dir, (void*)data_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_float_newf(hal_param_dir_t dir, hal_float_t * data_addr,
			 int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_FLOAT, dir, (void*)data_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_u32_newf(hal_param_dir_t dir, hal_u32_t * data_addr,
		       int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_U32, dir, (void*)data_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_s32_newf(hal_param_dir_t dir, hal_s32_t * data_addr,
		       int owner_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_S32, dir, (void*)data_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

// printf-style version of hal_param_new()
int hal_param_newf(hal_type_t type,
		   hal_param_dir_t dir,
		   volatile void * data_addr,
		   int owner_id,
		   const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(type, dir, data_addr, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}


/* this is a generic function that does the majority of the work. */

int hal_param_new(const char *name,
		  hal_type_t type,
		  hal_param_dir_t dir,
		  volatile void *data_addr,
		  int owner_id)
{
    hal_param_t *new;

    if (hal_data == 0) {
	hal_print_error("%s: called before init", __FUNCTION__);
	return -EINVAL;
    }

    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	hal_print_error("%s: param type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U3",
			__FUNCTION__);
	return -EINVAL;
    }

    if (dir != HAL_RO && dir != HAL_RW) {
	hal_print_error("%s: param direction not one of HAL_RO, or HAL_RW",
			__FUNCTION__);
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	hal_print_error("%s: parameter name '%s' is too long", __FUNCTION__, name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	hal_print_error("%s: called while HAL locked", __FUNCTION__);
	return -EPERM;
    }
    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	HALDBG("creating parameter '%s'\n", name);

	/* validate comp_id */
	comp = halpr_find_owning_comp(owner_id);
	if (comp == 0) {
	    /* bad comp_id */
	    HALERR("param '%s': owning component %d not found\n",
		   name, owner_id);
	    return -EINVAL;
	}

	/* validate passed in pointer - must point to HAL shmem */
	if (! SHMCHK(data_addr)) {
	    /* bad pointer */
	    HALERR("param '%s': data_addr not in shared memory\n", name);
	    return -EINVAL;
	}

	// this will be 0 for legacy comps which use comp_id
	hal_inst_t *inst = halpr_find_inst_by_id(owner_id);
	int inst_id = (inst ? hh_get_id(&inst->hdr) : 0);

	// instances may create params post hal_ready
	// never understood the restriction in the first place
	if ((inst_id == 0) && (comp->state > COMP_INITIALIZING)) {
	    HALERR("component '%s': %s called after hal_ready",
		   name,  __FUNCTION__);
	    return -EINVAL;
	}

	// allocate new parameter descriptor
	if ((new = shmalloc_desc(sizeof(hal_param_t))) == NULL)
	    NOMEM("param '%s'",  name);
	hh_init_hdrf(&new->hdr, HAL_PARAM, owner_id, "%s", name);

	new->data_ptr = SHMOFF(data_addr);
	new->type = type;
	new->dir = dir;

	// make it visible
	halg_add_object(false, (hal_object_ptr)new);
    }
    return 0;
}

/* wrapper functs for typed params - these call the generic funct below */

int hal_param_bit_set(const char *name, int value)
{
    return hal_param_set(name, HAL_BIT, &value);
}

int hal_param_float_set(const char *name, double value)
{
    return hal_param_set(name, HAL_FLOAT, &value);
}

int hal_param_u32_set(const char *name, unsigned long value)
{
    return hal_param_set(name, HAL_U32, &value);
}

int hal_param_s32_set(const char *name, signed long value)
{
    return hal_param_set(name, HAL_S32, &value);
}

/* this is a generic function that does the majority of the work */

int hal_param_set(const char *name, hal_type_t type, void *value_addr)
{

    void *d_ptr;

    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_PARAMS);
    CHECK_STRLEN(name, HAL_NAME_LEN);

    HALDBG("setting parameter '%s'\n", name);

    {
	hal_param_t *param __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* search param list for name */
	param = halpr_find_param_by_name(name);
	if (param == 0) {
	    /* parameter not found */
	    HALERR("parameter '%s' not found\n", name);
	    return -EINVAL;
	}
	/* found it, is type compatible? */
	if (param->type != type) {
	    HALERR("parameter '%s': type mismatch %d != %d\n",
		   name, param->type, type);
	    return -EINVAL;
	}
	/* is it read only? */
	if (param->dir == HAL_RO) {
	    HALERR("parameter '%s': param is not writable\n", name);
	    return -EINVAL;
	}
	/* everything is OK, set the value */
	d_ptr = SHMPTR(param->data_ptr);
	switch (param->type) {
	case HAL_BIT:
	    if (*((int *) value_addr) == 0) {
		*(hal_bit_t *) (d_ptr) = 0;
	    } else {
		*(hal_bit_t *) (d_ptr) = 1;
	    }
	    break;
	case HAL_FLOAT:
	    *((hal_float_t *) (d_ptr)) = *((double *) (value_addr));
	    break;
	case HAL_S32:
	    *((hal_s32_t *) (d_ptr)) = *((signed long *) (value_addr));
	    break;
	case HAL_U32:
	    *((hal_u32_t *) (d_ptr)) = *((unsigned long *) (value_addr));
	    break;
	default:
	    /* Shouldn't get here, but just in case... */
	    HALERR("parameter '%s': bad type %d setting param\n",
		   name, param->type);
	    return -EINVAL;
	}
    }
    return 0;
}
