#ifndef  HAL_ACCESSOR_H
#define  HAL_ACCESSOR_H
#include "config.h"
#include <rtapi.h>
//#undef HAVE_CK

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

void hal_typefailure(const char *file,
		     const int line,
		     const int object_type,
		     const int value_type);

#ifndef FAIL_STOP
#define FAIL_STOP(file, line, otype, expected)				\
    do {								\
	hal_typefailure(file, line, otype, expected);			\
    } while (0)
#endif

#if CHECK_ACCESSOR_TYPE
// optionally compiled-in runtime check on type compatibility
// when using raw descriptors
#define _CHECK(otype, vtype)			\
    if (otype != vtype)		\
	FAIL_STOP(__FILE__, __LINE__, otype, vtype);
#else
#define _CHECK(otype, vtype)
#endif



#ifdef HAVE_CK
#define BITCAST    (char *) // (hal_bit_t *)
#define S32CAST    (int  *) //(hal_s32_t *)
#define U32CAST    (unsigned *) //(hal_u32_t *)
#define FLOATCAST  (double *) //(hal_float_t *)
#define BITSTORE   pr_store_char
#define S32STORE   pr_store_int
#define U32STORE   pr_store_uint
#define FLOATSTORE pr_store_double
#define BITLOAD    pr_load_char
#define S32LOAD    pr_load_int
#define U32LOAD    pr_load_uint
#define FLOATLOAD  pr_load_double

#define _STORE(dest, value, op, cast) ck_##op(cast dest, value)
#define _LOAD(src, op, cast)          ck_##op(cast src)

#else
#define BITCAST   // (hal_bit_t *)
#define S32CAST   // (hal_s32_t *)
#define U32CAST   // (hal_u32_t *)
#define FLOATCAST // (hal_float_t *)
#define BITSTORE   __atomic_store_1
#define S32STORE   __atomic_store_4
#define U32STORE   __atomic_store_4
#define FLOATSTORE __atomic_store_8
#define BITLOAD    __atomic_load_1
#define S32LOAD    __atomic_load_4
#define U32LOAD    __atomic_load_4
#define FLOATLOAD  __atomic_load_8

#define _STORE(dest, value, op, type) op(dest, value, RTAPI_MEMORY_MODEL)
#define _LOAD(src, op, cast)          op(src, RTAPI_MEMORY_MODEL)
#endif


// how a HAL value is set, given a pointer to a hal_data_u,
// including any memory barrier
#define _SETVALUE(U, DESC, TAG, VALUE, OP, TYPE)			\
    _STORE(&U->TAG, VALUE, OP, TYPE);					\
    if (unlikely(hh_get_wmb(&DESC->hdr)))				\
	rtapi_smp_wmb();

// how a HAL value is retrieved, given a pointer to a hal_data_u,
// including any memory barrier
#define _GETVALUE(U, DESC, TAG, OP, CAST)				\
    if (unlikely(hh_get_rmb(&DESC->hdr)))				\
	rtapi_smp_rmb();						\
    return _LOAD(&U->TAG, OP, CAST);

// atomically increment a value (integral types only)
// unclear how to do the equivalent of an __atomic_add_fetch
// with ck, so use gcc intrinsics for now:
#define _INCREMENT(U, DESC, TYPE, TAG, VALUE)				\
    TYPE rvalue = __atomic_add_fetch(&U->TAG, VALUE,			\
				     RTAPI_MEMORY_MODEL);		\
    if (unlikely(hh_get_wmb(&DESC->hdr)))				\
	rtapi_smp_wmb();						\
    return rvalue;


// export context-independent setters which are strongly typed,
// and context-dependent accessors with a descriptor argument,
// and an optional runtime type check
#define PINSETTER(type, otype, tag, op, cast)				\
    static inline const hal_##type##_t					\
    set_##type##_pin(type##_pin_ptr p,					\
		     const hal_##type##_t value) {			\
    hal_pin_t *pin =							\
	(hal_pin_t *)hal_ptr(p._##tag##p);				\
    hal_data_u *u =							\
	(hal_data_u *)hal_ptr(pin->data_ptr);				\
    _SETVALUE(u, pin, _##tag, value, op, cast);				\
    return value;							\
    }									\
									\
    static inline const hal_##type##_t					\
    _set_##type##_pin(hal_pin_t *pin,					\
		      const hal_##type##_t value) {			\
    hal_data_u *u =							\
	(hal_data_u *)hal_ptr(pin->data_ptr);				\
    _CHECK(pin_type(pin), otype);					\
    _SETVALUE(u, pin, _##tag, value, op, cast);				\
    return value;							\
    }


// emit typed pin setters
PINSETTER(bit,   HAL_BIT,   b, BITSTORE,   BITCAST);
PINSETTER(s32,   HAL_S32,   s, S32STORE,   S32CAST);
PINSETTER(u32,   HAL_U32,   u, U32STORE,   U32CAST);
PINSETTER(float, HAL_FLOAT, f, FLOATSTORE, FLOATCAST);


#define PINGETTER(type, otype, letter, op, cast)			\
    static inline const hal_##type##_t					\
	 get_##type##_pin(const type##_pin_ptr p) {			\
    const hal_pin_t *pin =						\
	(const hal_pin_t *)hal_ptr(p._##letter##p);			\
    const hal_data_u *u =						\
	(const hal_data_u *)hal_ptr(pin->data_ptr);			\
    _GETVALUE(u, pin, _##letter, op, cast);				\
    }									\
									\
    static inline const hal_##type##_t					\
    _get_##type##_pin(const hal_pin_t *pin) {				\
	const hal_data_u *u =						\
	    (const hal_data_u *)hal_ptr(pin->data_ptr);			\
	_CHECK(pin_type(pin), otype)					\
        _GETVALUE(u, pin, _##letter, op, cast)			        \
    }


// emit typed pin getters
PINGETTER(bit,   HAL_BIT,   b, BITLOAD,   BITCAST);
PINGETTER(s32,   HAL_S32,   s, S32LOAD,   S32CAST);
PINGETTER(u32,   HAL_U32,   u, U32LOAD,   U32CAST);
PINGETTER(float, HAL_FLOAT, f, FLOATLOAD, FLOATCAST);


#define PIN_INCREMENTER(type, tag)					\
    static inline const hal_##type##_t					\
	 incr_##type##_pin(type##_pin_ptr p,				\
			   const hal_##type##_t value) {		\
        hal_pin_t *pin = (hal_pin_t *)hal_ptr(p._##tag##p);		\
	hal_data_u *u = (hal_data_u*)hal_ptr(pin->data_ptr);		\
	_INCREMENT(u, pin, hal_##type##_t,  _##tag, value);		\
    }									\
									\
    static inline const hal_##type##_t					\
    _incr_##type##_pin(hal_pin_t *pin,					\
		       const hal_##type##_t value) {			\
	hal_data_u *u = (hal_data_u*)hal_ptr(pin->data_ptr);		\
	_INCREMENT(u, pin, hal_##type##_t,  _##tag, value)		\
    }

// typed pin incrementers
PIN_INCREMENTER(s32, s)
PIN_INCREMENTER(u32, u)

// signal getters
#define SIGGETTER(type, otype, letter, op, cast)			\
    static inline const hal_##type##_t					\
    get_##type##_sig(const type##_sig_ptr s) {				\
    const hal_sig_t *sig =						\
	(const hal_sig_t *)hal_ptr(s._##letter##s);			\
    hal_data_u *u = (hal_data_u*)&sig->value;				\
    _GETVALUE(u, sig, _##letter, op, cast);			\
    }									\
									\
    static inline const hal_##type##_t					\
    _get_##type##_sig(const hal_sig_t *sig) {				\
    _CHECK(sig_type(sig), otype);					\
    hal_data_u *u = (hal_data_u*)&sig->value;				\
    _GETVALUE(u, sig, _##letter, op, cast);			\
    }

// emit typed signal getters
SIGGETTER(bit,   HAL_BIT,   b, BITLOAD,   BITCAST);
SIGGETTER(s32,   HAL_S32,   s, S32LOAD,   S32CAST);
SIGGETTER(u32,   HAL_U32,   u, U32LOAD,   U32CAST);
SIGGETTER(float, HAL_FLOAT, f, FLOATLOAD, FLOATCAST);

// signal setters - halcmd, python bindings use only (like setting initial value)
#define _SIGSET(OFFSET, TAG, VALUE, OP, TYPE)				\
    _STORE(&u->TAG, VALUE, OP, TYPE);					\
    if (unlikely(hh_get_wmb(&sig->hdr)))				\
	rtapi_smp_wmb();

#define SIGSETTER(type, otype, letter, op, cast)				\
    static inline const hal_##type##_t					\
    set_##type##_sig(type##_sig_ptr s,					\
		     const hal_##type##_t value) {			\
	hal_sig_t *sig =						\
	    (hal_sig_t *)hal_ptr(s._##letter##s);			\
	hal_data_u *u = &sig->value;					\
	_SETVALUE(u, sig, _##letter, value, op, cast);			\
	return value;							\
    }									\
									\
    static inline const hal_##type##_t					\
    _set_##type##_sig(hal_sig_t *sig,					\
		      const hal_##type##_t value) {			\
	hal_data_u *u = &sig->value;					\
	_SETVALUE(u, sig, _##letter, value, op, cast);			\
	return value;							\
    }


// emit typed signal setters
SIGSETTER(bit,   HAL_BIT,   b, BITSTORE,   BITCAST);
SIGSETTER(s32,   HAL_S32,   s, S32STORE,   S32CAST);
SIGSETTER(u32,   HAL_U32,   u, U32STORE,   U32CAST);
SIGSETTER(float, HAL_FLOAT, f, FLOATSTORE, FLOATCAST);


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

// convert hal_data_u to string
int hals_value(char *buffer,
	       const size_t s,
	       const hal_type_t type,
	       const hal_data_u *u);

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

// as above, but with explicit default value as 3rd argument
bit_pin_ptr halxd_pin_bit_newf(const hal_pin_dir_t dir,
			       const int owner_id,
			       const hal_bit_t defval,
			       const char *fmt, ...)
    __attribute__((format(printf,4,5)));

float_pin_ptr halxd_pin_float_newf(const hal_pin_dir_t dir,
				   const int owner_id,
				   const hal_float_t defval,
				   const char *fmt, ...)
    __attribute__((format(printf,4,5)));

u32_pin_ptr halxd_pin_u32_newf(const hal_pin_dir_t dir,
			       const int owner_id,
			       const hal_u32_t defval,
			       const char *fmt, ...)
    __attribute__((format(printf,4,5)));

s32_pin_ptr halxd_pin_s32_newf(const hal_pin_dir_t dir,
			       const int owner_id,
			       const hal_s32_t defval,
			       const char *fmt, ...)
    __attribute__((format(printf,4,5)));

RTAPI_END_DECLS
#endif // HAL_ACCESSOR_H
