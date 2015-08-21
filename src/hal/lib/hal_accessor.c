#include "config.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_accessor.h"

// allocators for accessor-style pins
// passing NULL for data_ptr_addr in halg_pin_newfv makes them v2 pins

bit_pin_ptr halx_pin_bit_newf(const hal_pin_dir_t dir,
			      const int owner_id,
			      const char *fmt, ...)
{
    va_list ap;
    bit_pin_ptr p;
    hal_data_u defval = {._b = 0};
    va_start(ap, fmt);
    p._bp = hal_off_safe(halg_pin_newfv(1, HAL_BIT, dir, NULL,
					owner_id, defval, fmt, ap));
    va_end(ap);
    return p;
}

float_pin_ptr halx_pin_float_newf(const hal_pin_dir_t dir,
				  const int owner_id,
				  const char *fmt, ...)
{
    va_list ap;
    float_pin_ptr p;
    hal_data_u defval = {._f = 0.0};
    va_start(ap, fmt);
    p._fp = hal_off_safe(halg_pin_newfv(1, HAL_FLOAT, dir, NULL,
					owner_id, defval, fmt, ap));
    va_end(ap);
    return p;
}

u32_pin_ptr halx_pin_u32_newf(const hal_pin_dir_t dir,
		      const int owner_id,
		      const char *fmt, ...)
{
    va_list ap;
    u32_pin_ptr p;
    hal_data_u defval = {._u = 0};
    va_start(ap, fmt);
    p._up = hal_off_safe(halg_pin_newfv(1, HAL_U32, dir, NULL,
					owner_id, defval, fmt, ap));
    va_end(ap);
    return p;
}

s32_pin_ptr halx_pin_s32_newf(const hal_pin_dir_t dir,
		      const int owner_id,
		      const char *fmt, ...)
{
    va_list ap;
    s32_pin_ptr p;
    hal_data_u defval = {._s = 0};
    va_start(ap, fmt);
    p._sp = hal_off_safe(halg_pin_newfv(1,HAL_S32, dir, NULL,
					owner_id, defval, fmt, ap));
    va_end(ap);
    return p;
}


const char *hals_pindir(const hal_pin_dir_t dir)
{
    switch (dir) {
    case HAL_IN:
	return "IN";
    case HAL_OUT:
	return "OUT";
    case HAL_IO:
	return "I/O";
    default:
	return "*invalid*";
    }
}

const char *hals_type(const hal_type_t type)
{
    switch (type) {
    case HAL_BIT:
	return "bit";
    case HAL_FLOAT:
	return "float";
    case HAL_S32:
	return "s32";
    case HAL_U32:
	return "u32";
    default:
	return "*invalid*";
    }
}


void hal_typefailure(const char *file,
		     const int line,
		     const int object_type,
		     const int value_type)
{
    rtapi_print_msg(RTAPI_MSG_ERR,
		    "%s:%d TYPE VIOLATION: object type=%s value type=%s",
		    file,
		    line,
		    hals_type(object_type),
		    hals_type(value_type));
}
