//  non-inlined  hal_object_t accessors
//  inlined accessors are in hal_object.h
#include "config.h"
#include "rtapi.h"
#include "hal.h"
#include "hal_object.h"
#include "hal_priv.h"
//#include "hal_internal.h"

int hh_set_namefv(halobj_t *o, const char *fmt, va_list ap)
{
    int sz = rtapi_vsnprintf(o->_name, sizeof(o->_name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        HALERR("length %d invalid for name starting with '%s'",
	       sz, o->_name);
        return -ENOMEM;
    }
    return 0;
}

int  hh_init_hdrfv(halobj_t *o,
		   const int owner_id,
		   const char *fmt, va_list ap)
{
    hh_set_id(o, rtapi_next_handle());
    hh_set_owner_id(o, owner_id);
    hh_set_valid(o);
    return hh_set_namefv(o, fmt, ap);
}

int  hh_init_hdrf(halobj_t *o,
		  const int owner_id,
		  const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hh_init_hdrfv(o, owner_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hh_clear_hdr(halobj_t *o)
{
    int ret = hh_is_valid(o);
    hh_set_id(o, 0);
    hh_set_owner_id(o, 0);
    hh_clear_name(o);
    hh_set_invalid(o);
    return ret;
}

#ifdef RTAPI

EXPORT_SYMBOL(hh_set_namefv);
EXPORT_SYMBOL(hh_set_namef);
EXPORT_SYMBOL(hh_init_hdrf);
EXPORT_SYMBOL(hh_clear_hdr);

#endif
