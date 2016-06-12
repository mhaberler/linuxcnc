#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"
#include <unistd.h>

#ifdef RTAPI

int hal_get_fd(hal_s32_t *pfd, const char *name)
{
    CHECK_HALDATA();
    CHECK_STRLEN(name, HAL_NAME_LEN);
    {
	hal_pin_t *pin __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));
	pin = halpr_find_pin_by_name(name);
	if (pin == 0)
	    return -ENOENT;
	if (pin->type != HAL_S32)
	    return -EINVAL; // type mismatch
	if (pin->dir != HAL_OUT)
	    return -EINVAL; // dir mismatch
	if (pfd) {
	    hal_s32_t **pinref = SHMPTR(pin->data_ptr_addr);
	    *pfd = **pinref;
	}
    }
    return 0;
}

int hal_close_fd(const char *name)
{
    int fd;
    int retval = hal_get_fd(&fd,name);
    if (!retval)
	return close(fd);
    return retval;
}

int hal_set_fd(const char *name, hal_s32_t fd, int creat)
{
    CHECK_HALDATA();
    CHECK_STRLEN(name, HAL_NAME_LEN);

    hal_pin_t *pin;
    {
	int dummy __attribute__((cleanup(halpr_autorelease_mutex)));

	rtapi_mutex_get(&(hal_data->mutex));
	pin = halpr_find_pin_by_name(name);
    }

    // race window open thanks to stoooopid HAL locking
    hal_s32_t **pinref;

    if (pin == NULL) {
	if (!creat)
	    return -ENOENT;

	pinref = hal_malloc(sizeof(hal_s32_t *));
	if (pinref == 0)
	    return -ENOMEM;

	int retval = hal_pin_s32_newf(HAL_OUT,
				      pinref,
				      lib_module_id,
				      name);
	if (retval)  // leaks pin memory on failure, thanks to hal_malloc
	    return retval;
    } else {
	if (pin->type != HAL_S32)
	    return -EINVAL; // type mismatch
	pinref = SHMPTR(pin->data_ptr_addr);
    }
    **pinref = (hal_s32_t) fd;
    return 0;
}




EXPORT_SYMBOL(hal_get_fd);
EXPORT_SYMBOL(hal_set_fd);
EXPORT_SYMBOL(hal_close_fd);

#endif
