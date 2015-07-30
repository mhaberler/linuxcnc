#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include "config.h"
#include <rtapi.h>

RTAPI_BEGIN_DECLS

// see type aliases for hal_pin_t and hal_sig_t in hal.h ca line 530

// see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

// NB these setters/getters work for V2 pins only which use hal_pin_t.data_ptr,
// instead of the legacy hal_pin_t.data_ptr_addr and hal_malloc()'d
// <haltype>*
// this means atomics+barrier support is possible only with V2 pins (!).

// — Built-in Function: type __atomic_add_fetch (type *ptr, type val, int memorder)
// — Built-in Function: type __atomic_sub_fetch (type *ptr, type val, int memorder)
// — Built-in Function: type __atomic_and_fetch (type *ptr, type val, int memorder)
// — Built-in Function: type __atomic_xor_fetch (type *ptr, type val, int memorder)
// — Built-in Function: type __atomic_or_fetch (type *ptr, type val, int memorder)
// — Built-in Function: type __atomic_nand_fetch (type *ptr, type val, int memorder)
// These built-in functions perform the operation suggested by the name, and return the result of the operation. That is,
//         { *ptr op= val; return *ptr; }

// should setters be lvalues?
// add increment, and, or, not, xor


#define _PINSET(ptr,member,type,value)				\
    __atomic_store((type *)SHMPTR(ptr.member->data_ptr),	\
		   &(value),					\
		   RTAPI_MEMORY_MODEL);				\
    if (unlikely(hh_get_wmb(&ptr.member->hdr)))			\
	rtapi_smp_wmb();

#define _PINGET(ptr,member,type)			 \
    if (unlikely(hh_get_rmb(&ptr.member->hdr)))		 \
	rtapi_smp_rmb();				 \
    type __value;						 \
    __atomic_load((type *) SHMPTR(ptr.member->data_ptr), \
		  &__value, RTAPI_MEMORY_MODEL);		 \
    return __value;

#ifdef NOTYET
// more generic versions of the above
#define _PINOP_VOID(aop, ptr, member, type, value)	\
    aop((type *)SHMPTR(ptr.member->data_ptr),		\
	&(value),					\
	RTAPI_MEMORY_MODEL);				\
    if (unlikely(hh_get_wmb(&ptr.member->hdr)))		\
	rtapi_smp_wmb();

#define _PINOP_VALUE(aop, ptr, member, type, value)		\
    type result = aop((type *) SHMPTR(ptr.member->data_ptr),	\
		      &(value),					\
		      RTAPI_MEMORY_MODEL);			\
    if (unlikely(hh_get_wmb(&ptr.member->hdr)))			\
	rtapi_smp_wmb();					\
    return result;

// void
static inline void incr_u32_pin(u32_pin_ptr p, const hal_u32_t value) {
    _PINOP_VOID(__atomic_add_fetch, p, _up, hal_u32_t, value);
}

// returning result
static inline hal_u32_t incr_u32_pinv(u32_pin_ptr p, const hal_u32_t value) {
    _PINOP_VALUE(__atomic_add_fetch, p, _up, hal_u32_t, value);
}

#endif

// typed pin setters
static inline void set_bit_pin(bit_pin_ptr p, const hal_bit_t value) {
    _PINSET(p, _bp, hal_bit_t, value);
}
static inline void set_s32_pin(s32_pin_ptr p, const hal_s32_t value) {
    _PINSET(p, _sp, hal_s32_t, value);
}
static inline void set_u32_pin(u32_pin_ptr p, const hal_u32_t value) {
    _PINSET(p, _up, hal_u32_t, value);
}
static inline void set_float_pin(float_pin_ptr p, const hal_float_t value) {
    _PINSET(p, _fp, hal_float_t, value);
}

// typed pin getters
static inline hal_bit_t get_bit_pin(const bit_pin_ptr p) {
    _PINGET(p, _bp, hal_bit_t);
}
static inline hal_s32_t get_s32_pin(const s32_pin_ptr p) {
    _PINGET(p, _sp, hal_s32_t);
}
static inline hal_u32_t get_u32_pin(const u32_pin_ptr p) {
    _PINGET(p, _up, hal_u32_t);
}
static inline hal_float_t get_float_pin(const float_pin_ptr p) {
    _PINGET(p, _fp, hal_float_t);
}

// typed signal getters
// the data_ptr reference is gone - directly reference
// sig.value, a hal_data_u
#define _SIGGET(ptr,member,sigtype, hal_data_u_tag)		 \
    if (unlikely(hh_get_rmb(&ptr.member->hdr)))			 \
	rtapi_smp_rmb();					 \
    sigtype value;						 \
    __atomic_load(&ptr.member->value.hal_data_u_tag,		 \
		  &value, RTAPI_MEMORY_MODEL);			 \
    return value;

static inline hal_bit_t get_bit_sig(const bit_sig_ptr s) {
    _SIGGET(s, _bs, hal_bit_t,b)
}
static inline hal_s32_t get_s32_sig(const s32_sig_ptr s) {
    _SIGGET(s, _ss, hal_s32_t, s)
}
static inline hal_u32_t get_u32_sig(const u32_sig_ptr s) {
    _SIGGET(s, _us, hal_u32_t, u)
}
static inline hal_float_t get_float_sig(const float_sig_ptr s) {
    _SIGGET(s, _fs, hal_float_t, f)
}

// typed signal setters
#define _SIGSET(PTR,MEMBER,SIGTYPE, TAG, VALUE)			\
    __atomic_store((SIGTYPE *)&PTR.MEMBER->value.TAG,		\
		   &(VALUE),					\
		   RTAPI_MEMORY_MODEL);				\
    if (unlikely(hh_get_wmb(&PTR.MEMBER->hdr)))			\
	rtapi_smp_wmb();

static inline void set_bit_sig(bit_sig_ptr s, const hal_bit_t value) {
    _SIGSET(s, _bs, hal_bit_t, b, value);
}
static inline void set_s32_sig(s32_sig_ptr s, const hal_s32_t value) {
    _SIGSET(s, _ss, hal_s32_t, s, value);
}
static inline void set_u32_sig(u32_sig_ptr s, const hal_u32_t value) {
    _SIGSET(s, _us, hal_u32_t, u, value);
}
static inline void set_float_sig(float_sig_ptr s, const hal_float_t value) {
    _SIGSET(s, _fs, hal_float_t, f, value);
}



// typed validity tests for pins and signals
// bonus: comparing to NULL is not permitted, need such accessors
// so folks dont get the idea this is a pointer
static inline bool bit_pin_null(const bit_pin_ptr b) {
    return b._bp == NULL;
}
static inline bool s32_pin_null(const s32_pin_ptr b) {
    return b._sp == NULL;
}
static inline bool u32_pin_null(const u32_pin_ptr b) {
    return b._up == NULL;
}
static inline bool float_pin_null(const float_pin_ptr b) {
    return b._fp == NULL;
}
static inline bool bit_sig_null(const bit_sig_ptr s) {
    return s._bs == NULL;
}
static inline bool s32_sig_null(const s32_sig_ptr s) {
    return s._ss == NULL;
}
static inline bool u32_sig_null(const u32_sig_ptr s) {
    return s._us == NULL;
}
static inline bool float_sig_null(const float_sig_ptr s) {
    return s._fs == NULL;
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


RTAPI_END_DECLS
#endif // HAL_ACCESSOR_H
