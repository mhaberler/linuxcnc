#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include "config.h"
#include <rtapi.h>

RTAPI_BEGIN_DECLS

#ifdef HAVE_CK
#include <ck_pr.h>
#endif


// see type aliases for hal_pin_t and hal_sig_t in hal.h ca line 530

// see https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html

// NB these setters/getters work for V2 pins only which use hal_pin_t.data_ptr,
// instead of the legacy hal_pin_t.data_ptr_addr and hal_malloc()'d
// <haltype>*
// this means atomics+barrier support is possible only with V2 pins (!).

// void __atomic_load (type *ptr, type *ret, int memorder)  returns the contents of *ptr in *ret.
// void __atomic_store (type *ptr, type val, int memorder) writes val into *ptr.
//
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

#define PIN_MEMORY_MODEL     RTAPI_MEMORY_MODEL
#define SIGNAL_MEMORY_MODEL  RTAPI_MEMORY_MODEL


// set a hal_data_u referenced via an offset field in a descriptor
// this should work for v2 pins and params as long as both have
// the value ptr called 'data_ptr'
#define _PPSET(P, MEMBER, TAG, VALUE)					\
    __atomic_store(&((hal_data_u *)SHMPTR(P.MEMBER->data_ptr))->TAG,	\
		   &(VALUE),						\
		   PIN_MEMORY_MODEL);					\
    if (unlikely(hh_get_wmb(&p.MEMBER->hdr)))				\
	rtapi_smp_wmb();


// typed pin setters
static inline void set_bit_pin(bit_pin_ptr p, const hal_bit_t value) {
    _PPSET( p, _bp, _b, value);
}
static inline void set_s32_pin(s32_pin_ptr p, const hal_s32_t value) {
    _PPSET( p, _sp, _s, value);
}
static inline void set_u32_pin(u32_pin_ptr p, const hal_u32_t value) {
    _PPSET( p, _up, _u, value);
}
static inline void set_float_pin(float_pin_ptr p, const hal_float_t value) {
    _PPSET( p, _fp, _f, value);
}
#define _PPINCR(P, MEMBER, TAG, VALUE)					\
    __atomic_add_fetch(&((hal_data_u *)SHMPTR(P.MEMBER->data_ptr))->TAG, \
		       (VALUE),					\
		       PIN_MEMORY_MODEL);				\
    if (unlikely(hh_get_wmb(&p.MEMBER->hdr)))				\
	rtapi_smp_wmb();

// typed pin incrementers
static inline void incr_s32_pin(s32_pin_ptr p, const hal_s32_t value) {
    _PPINCR( p, _sp, _s, value);
}
static inline void incr_u32_pin(u32_pin_ptr p, const hal_u32_t value) {
    _PPINCR( p, _up, _u, value);
}

static inline hal_bit_t toggle_bit_pin(bit_pin_ptr p) {
    if (unlikely(hh_get_rmb(&p._bp->hdr)))
	rtapi_smp_rmb();
    hal_bit_t r = __atomic_xor_fetch(&((hal_data_u *)SHMPTR(p._bp->data_ptr))->_b,
				     1, PIN_MEMORY_MODEL);
    if (unlikely(hh_get_wmb(&p._bp->hdr)))
	rtapi_smp_wmb();
    return r;
}

// get a hal_data_u referenced via an offset field in a descriptor
// this should work for v2 pins and params.
// unfortunately __typeof__(uniontype.field) does not work
#define _PPGET(P, MEMBER, TYPE, TAG)					\
    TYPE _ret;								\
    if (unlikely(hh_get_rmb(&P.MEMBER->hdr)))				\
	rtapi_smp_rmb();						\
    __atomic_load(&((hal_data_u *)SHMPTR(P.MEMBER->data_ptr))->TAG,	\
		  &_ret,						\
		  PIN_MEMORY_MODEL);					\
    return _ret;

// pin getters
static inline hal_bit_t get_bit_pin(const bit_pin_ptr p) {
    _PPGET(p, _bp, hal_bit_t, _b);
}
static inline hal_s32_t get_s32_pin(const s32_pin_ptr p) {
    _PPGET(p, _sp, hal_s32_t, _s);
}
static inline hal_u32_t get_u32_pin(const u32_pin_ptr p) {
    _PPGET(p, _up, hal_u32_t, _u);
}
static inline hal_float_t get_float_pin(const float_pin_ptr p) {
    _PPGET(p, _fp, hal_float_t, _f);
}


// typed param setters
// params work the same as v2 pins
static inline void set_bit_param(bit_param_ptr p, const hal_bit_t value) {
    _PPSET( p, _bpar, _b, value);
}
static inline void set_s32_param(s32_param_ptr p, const hal_s32_t value) {
    _PPSET( p, _spar, _s, value);
}
static inline void set_u32_param(u32_param_ptr p, const hal_u32_t value) {
    _PPSET( p, _upar, _u, value);
}
static inline void set_float_param(float_param_ptr p, const hal_float_t value) {
    _PPSET( p, _fpar, _f, value);
}
// param getters
static inline hal_bit_t get_bit_param(const bit_param_ptr p) {
    _PPGET(p, _bpar, hal_bit_t, _b);
}
static inline hal_s32_t get_s32_param(const s32_param_ptr p) {
    _PPGET(p, _spar, hal_s32_t, _s);
}
static inline hal_u32_t get_u32_param(const u32_param_ptr p) {
    _PPGET(p, _upar, hal_u32_t, _u);
}
static inline hal_float_t get_float_param(const float_param_ptr p) {
    _PPGET(p, _fpar, hal_float_t, _f);
}

// signal getters
#define _SIGGET(P, MEMBER, TYPE, TAG)					\
    if (unlikely(hh_get_rmb(&P.MEMBER->hdr)))				\
	rtapi_smp_rmb();						\
    TYPE _ret;								\
    __atomic_load(&P.MEMBER->value.TAG,					\
		  &_ret,						\
		  SIGNAL_MEMORY_MODEL);					\
    return _ret;

static inline hal_bit_t get_bit_sig(const bit_sig_ptr s) {
   _SIGGET( s, _bs, hal_bit_t, _b);
}
static inline hal_s32_t get_s32_sig(const s32_sig_ptr s) {
   _SIGGET( s, _ss, hal_s32_t, _s);
}
static inline hal_u32_t get_u32_sig(const u32_sig_ptr s) {
   _SIGGET( s, _us, hal_u32_t, _u);
}
static inline hal_float_t get_float_sig(const float_sig_ptr s) {
    _SIGGET( s, _fs, hal_float_t, _f);
}

// signal getters
#define _SIGSET(P, MEMBER, TAG, VALUE)		\
    __atomic_store(&P.MEMBER->value.TAG,			\
		   &(VALUE),					\
		   SIGNAL_MEMORY_MODEL);			\
    if (unlikely(hh_get_wmb(&P.MEMBER->hdr)))			\
	rtapi_smp_wmb();

static inline void set_bit_sig(bit_sig_ptr s, const hal_bit_t value) {
   _SIGSET(s, _bs, _b, value);
}
static inline void set_s32_sig(s32_sig_ptr s, const hal_s32_t value) {
   _SIGSET(s, _ss, _s, value);
}
static inline void set_u32_sig(u32_sig_ptr s, const hal_u32_t value) {
   _SIGSET(s, _us, _u, value);
}
static inline void set_float_sig(float_sig_ptr s, const hal_float_t value) {
   _SIGSET(s, _fs, _f, value);
}


// typed validity tests for pins and signals
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
