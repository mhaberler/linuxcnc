#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include <rtapi.h>

RTAPI_BEGIN_DECLS

// type aliases for hal_pin_t - makes compiler
// detect type mismatch between <type1> and <type2>:
//
//   set_<type1>_pin(<type2>pinptr,...) and
//   get_<type1>_pin(<type2>pinptr)

typedef struct { hal_pin_t _bp; } bit_pin_t;
typedef struct { hal_pin_t _sp; } s32_pin_t;
typedef struct { hal_pin_t _up; } u32_pin_t;
typedef struct { hal_pin_t _fp; } float_pin_t;

// same trick for signals
typedef struct { hal_sig_t _bs; } bit_sig_t;
typedef struct { hal_sig_t _ss; } s32_sig_t;
typedef struct { hal_sig_t _us; } u32_sig_t;
typedef struct { hal_sig_t _fs; } float_sig_t;

// see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

// NB these setters/getters work for V2 pins only which use hal_pin_t.data_ptr,
// instead of the legacy hal_pin_t.data_ptr_addr and hal_malloc()'d
// <haltype>*
// this means atomics+barrier support is possible only with V2 pins (!).

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

// typed pin setters
static inline void set_bit_pin(bit_pin_t *p, const hal_bit_t value) {
    _PINSET(p, _bp, hal_bit_t, value);
}
static inline void set_s32_pin(s32_pin_t *p, const hal_s32_t value) {
    _PINSET(p, _sp, hal_s32_t, value);
}
static inline void set_u32_pin(u32_pin_t *p, const hal_u32_t value) {
    _PINSET(p, _up, hal_u32_t, value);
}
static inline void set_float_pin(float_pin_t *p, const hal_float_t value) {
    _PINSET(p, _fp, hal_float_t, value);
}

// typed pin getters
static inline hal_bit_t get_bit_pin(const bit_pin_t *p) {
    _PINGET(p, _bp, hal_bit_t);
}
static inline hal_s32_t get_s32_pin(const s32_pin_t *p) {
    _PINGET(p, _sp, hal_s32_t);
}
static inline hal_u32_t get_u32_pin(const u32_pin_t *p) {
    _PINGET(p, _up, hal_u32_t);
}
static inline hal_float_t get_float_pin(const float_pin_t *p) {
    _PINGET(p, _fp, hal_float_t);
}

// typed signal getters
#define _SIGGET(ptr,member,type)			 \
    if (unlikely(hh_get_rmb(&ptr->member.hdr)))		 \
	rtapi_smp_rmb();				 \
    type value;						 \
    __atomic_load((type *) SHMPTR(ptr->member.data_ptr), \
		  &value, RTAPI_MEMORY_MODEL);		 \
    return value;
static inline hal_bit_t get_bit_sig(const bit_sig_t *s) {
    _SIGGET(s, _bs, hal_bit_t)
}

RTAPI_END_DECLS
#endif // HAL_ACCESSOR_H
