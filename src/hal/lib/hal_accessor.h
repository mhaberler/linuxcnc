#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include <rtapi.h>

RTAPI_BEGIN_DECLS
typedef struct { hal_pin_t bp; } bit_pin_t;
typedef struct { hal_pin_t sp; } s32_pin_t;
typedef struct { hal_pin_t up; } u32_pin_t;
typedef struct { hal_pin_t fp; } float_pin_t;

// see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

#define _PINSET(ptr,member,type,value)			\
    type *vp = SHMPTR(ptr->member.data_ptr);		\
    __atomic_store(vp, &(value), RTAPI_MEMORY_MODEL);	\
    if (unlikely(hh_get_wmb(&ptr->member.hdr)))		\
	rtapi_smp_wmb();


#define _PINGET(ptr,member,type)			 \
    if (unlikely(hh_get_rmb(&ptr->member.hdr)))		 \
	rtapi_smp_rmb();				 \
    type value;						 \
    __atomic_load((type *) SHMPTR(ptr->member.data_ptr), \
		  &value, RTAPI_MEMORY_MODEL);		 \
    return value;

static inline void set_bit_pin(bit_pin_t *p, const hal_bit_t value) {
    _PINSET(p, bp, hal_bit_t, value);
}
static inline void set_s32_pin(s32_pin_t *p, const hal_s32_t value) {
    _PINSET(p, sp, hal_s32_t, value);
}
static inline void set_u32_pin(u32_pin_t *p, const hal_u32_t value) {
    _PINSET(p, up, hal_u32_t, value);
}
static inline void set_float_pin(float_pin_t *p, const hal_float_t value) {
    _PINSET(p, fp, hal_float_t, value);
}

static inline hal_bit_t get_bit_pin(const bit_pin_t *p) {
    _PINGET(p, bp, hal_bit_t)
}
static inline hal_s32_t get_s32_pin(const s32_pin_t *p) {
        _PINGET(p, sp, hal_s32_t)
}
static inline hal_u32_t get_u32_pin(const u32_pin_t *p) {
        _PINGET(p, up, hal_u32_t)
}
static inline hal_float_t get_float_pin(const float_pin_t *p) {
    _PINGET(p, fp, hal_float_t)
}

RTAPI_END_DECLS
#endif // HAL_ACCESSOR_H
