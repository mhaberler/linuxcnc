#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include "config.h"
#include <rtapi.h>

RTAPI_BEGIN_DECLS

// see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

// NB these setters/getters work for V2 pins only which use hal_pin_t.data_ptr,
// instead of the legacy hal_pin_t.data_ptr_addr and hal_malloc()'d
// <haltype>*
// this means atomics+barrier support is possible only with V2 pins (!).



static inline void *hal_ptr(const shmoff_t offset) {
    return ((char *)hal_shmem_base + offset);
}
static inline shmoff_t hal_off(const void *p) {
    return ((char *)p - (char *)hal_shmem_base);
}
static inline shmoff_t hal_off_safe(const void *p) {
    if (p == NULL) return 0;
    return ((char *)p - (char *)hal_shmem_base);
}


// should setters be rvalues?
#define SETTER_IS_RVALUE 1

#ifdef SETTER_IS_RVALUE
#define STYPE(t)  const hal_##t##_t
#define SRETURN   return value;
#else
#define STYPE(t) void
#define SRETURN
#endif

#ifdef HAVE_CK  // use concurrencykit.org primitives
#define _STORE(dest, value, op, type) ck_##op((type *)dest, value)
#else // use gcc intrinsics
#define _STORE(dest, value, op, type) op(dest, &value, RTAPI_MEMORY_MODEL)
#endif


#define _PINSET(OFFSET, TAG, VALUE, OP, TYPE)				\
    hal_pin_t *pin = (hal_pin_t *)hal_ptr(OFFSET);			\
    hal_data_u *u = (hal_data_u *)hal_ptr(pin->data_ptr);		\
    _STORE(&u->TAG, VALUE, OP, TYPE);					\
    if (unlikely(hh_get_wmb(&pin->hdr)))				\
	rtapi_smp_wmb();

// usage: PINSETTER(bit, b)
#define PINSETTER(type, tag, op, cast)					\
    static inline STYPE(type)						\
    set_##type##_pin(type##_pin_ptr p,					\
		     const hal_##type##_t value) {			\
	_PINSET(p._##tag##p, _##tag, value, op, cast)		\
	SRETURN    							\
    }

// emit typed pin setters
#ifdef HAVE_CK  // use concurrencykit.org primitives

PINSETTER(bit,   b, pr_store_8,  uint8_t)
PINSETTER(s32,   s, pr_store_32, uint32_t)
PINSETTER(u32,   u, pr_store_32, uint32_t)
PINSETTER(float, f, pr_store_64, uint64_t)

#else // use gcc intrinsics

PINSETTER(bit,   b, __atomic_store,)
PINSETTER(s32,   s, __atomic_store,)
PINSETTER(u32,   u, __atomic_store,)
PINSETTER(float, f, __atomic_store,)
#endif

#ifdef HAVE_CK  // use concurrencykit.org primitives
#define _LOAD(src, value, op, cast) value = ck_##op((cast *)src)
#else // use gcc intrinsics
#define _LOAD(dest, value, op, type) op(dest, &value, RTAPI_MEMORY_MODEL)
#endif

// v2 pins only.
#define _PINGET(TYPE, OFFSET, TAG, OP, CAST)					\
    const hal_pin_t *pin = (const hal_pin_t *)hal_ptr(OFFSET);		\
    const hal_data_u *u = (const hal_data_u *)hal_ptr(pin->data_ptr);	\
    if (unlikely(hh_get_rmb(&pin->hdr)))				\
	rtapi_smp_rmb();						\
    TYPE rvalue ;							\
    _LOAD(&u->TAG, rvalue, OP, CAST);					\
    return rvalue ;

#define PINGETTER(type, tag, op, cast)					\
    static inline const hal_##type##_t					\
	 get_##type##_pin(const type##_pin_ptr p) {			\
	_PINGET(hal_##type##_t, p._##tag##p, _##tag, op, cast)		\
    }

// typed pin getters
#ifdef HAVE_CK  // use concurrencykit.org primitives

PINGETTER(bit, b,   pr_load_8,  uint8_t)
PINGETTER(s32, s,   pr_load_32, uint32_t)
PINGETTER(u32, u,   pr_load_32, uint32_t)
PINGETTER(float, f, pr_load_64, uint64_t)

#else

PINGETTER(bit, b,   __atomic_load,)
PINGETTER(s32, s,   __atomic_load,)
PINGETTER(u32, u,   __atomic_load,)
PINGETTER(float, f, __atomic_load,)

#endif

#define _PININCR(TYPE, OFF, TAG, VALUE)					\
    hal_pin_t *pin = (hal_pin_t *)hal_ptr(OFF);				\
    hal_data_u *u = (hal_data_u *)hal_ptr(pin->data_ptr);				\
    TYPE rvalue = __atomic_add_fetch(&u->TAG, (VALUE),			\
				  RTAPI_MEMORY_MODEL);			\
    if (unlikely(hh_get_wmb(&pin->hdr)))				\
	rtapi_smp_wmb();						\
    return rvalue;

#define PIN_INCREMENTER(type, tag)					\
    static inline const hal_##type##_t					\
	 incr_##type##_pin(type##_pin_ptr p,				\
			   const hal_##type##_t value) {		\
	_PININCR(hal_##type##_t, p._##tag##p, _##tag, value)		\
    }

// typed pin incrementers
PIN_INCREMENTER(s32, s)
PIN_INCREMENTER(u32, u)


// signal getters
#define _SIGGET(TYPE, OFFSET, TAG)					\
    const hal_sig_t *sig = (const hal_sig_t *)hal_ptr(OFFSET);		\
    if (unlikely(hh_get_rmb(&sig->hdr)))				\
	rtapi_smp_rmb();						\
    TYPE rvalue ;							\
    __atomic_load(&sig->value.TAG, &rvalue , RTAPI_MEMORY_MODEL);	\
    return rvalue ;

#define SIGGETTER(type, tag)						\
    static inline const hal_##type##_t					\
	 get_##type##_sig(const type##_sig_ptr p) {			\
	_SIGGET(hal_##type##_t, p._##tag##s, _##tag)			\
    }

// emit typed signal getters
SIGGETTER(bit, b)
SIGGETTER(s32, s)
SIGGETTER(u32, u)
SIGGETTER(float, f)


// signal setters - halcmd, python bindings use only (initial value)
#define _SIGSET(OFFSET, TAG, VALUE)					\
    hal_sig_t *sig = (hal_sig_t *)hal_ptr(OFFSET);			\
    __atomic_store(&sig->value.TAG, &VALUE, RTAPI_MEMORY_MODEL);	\
    if (unlikely(hh_get_wmb(&sig->hdr)))				\
	rtapi_smp_wmb();

#define SIGSETTER(type, tag)						\
    static inline STYPE(type)						\
	 set_##type##_sig(type##_sig_ptr s,				\
			  const hal_##type##_t value) {			\
	_SIGSET(s._##tag##s, _##tag, value)				\
	SRETURN 	    						\
    }

// emit typed signal setters
SIGSETTER(bit, b)
SIGSETTER(s32, s)
SIGSETTER(u32, u)
SIGSETTER(float, f)



// typed validity tests for pins and signals
static inline bool bit_pin_null(const bit_pin_ptr b) {
    return b._bp == 0;
}
static inline bool s32_pin_null(const s32_pin_ptr b) {
    return b._sp == 0;
}
static inline bool u32_pin_null(const u32_pin_ptr b) {
    return b._up == 0;
}
static inline bool float_pin_null(const float_pin_ptr b) {
    return b._fp == 0;
}
static inline bool bit_sig_null(const bit_sig_ptr s) {
    return s._bs == 0;
}
static inline bool s32_sig_null(const s32_sig_ptr s) {
    return s._ss == 0;
}
static inline bool u32_sig_null(const u32_sig_ptr s) {
    return s._us == 0;
}
static inline bool float_sig_null(const float_sig_ptr s) {
    return s._fs == 0;
}


// convert hal type to string
const char *hals_type(const hal_type_t type);
// convert pin direction to string
const char *hals_pindir(const hal_pin_dir_t dir);

// pin allocators, in hal_accessor.c
bit_pin_ptr halx_pin_bit_newf(const hal_pin_dir_t dir,
			      const int owner_id,
			      const char *fmt, ...)
    __attribute__((format(printf,3,4)));

float_pin_ptr halx_pin_float_newf(const hal_pin_dir_t dir,
				  const int owner_id,
				  const char *fmt, ...)
    __attribute__((format(printf,3,4)));

u32_pin_ptr halx_pin_u32_newf(const hal_pin_dir_t dir,
			      const int owner_id,
			      const char *fmt, ...)
    __attribute__((format(printf,3,4)));

s32_pin_ptr halx_pin_s32_newf(const hal_pin_dir_t dir,
			      const int owner_id,
			      const char *fmt, ...)
    __attribute__((format(printf,3,4)));


#if NOTYET
static inline hal_bit_t toggle_bit_pin(bit_pin_ptr p) {
    hal_pin_t *pin = hal_ptr(p);
    hal_data_u *u = hal_ptr(pin->data_ptr);
    if (unlikely(hh_get_rmb(&p._bp->hdr)))
	rtapi_smp_rmb();
    hal_bit_t r = __atomic_xor_fetch(&u->_b, 1, RTAPI_MEMORY_MODEL);
    if (unlikely(hh_get_wmb(&p._bp->hdr)))
	rtapi_smp_wmb();
    return r;
}
#endif
RTAPI_END_DECLS
#endif // HAL_ACCESSOR_H
