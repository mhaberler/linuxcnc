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

#if 0
     void     ck_pr_store_64(uint64_t *target, uint64_t value);
     void     ck_pr_store_32(uint32_t *target, uint32_t value);
     void     ck_pr_store_16(uint16_t *target, uint16_t value);
     void     ck_pr_store_8(uint8_t *target, uint8_t value);

static inline const hal_bit_t set_bit_pin(bit_pin_ptr p, const hal_bit_t value)
{
    hal_pin_t *pin = (hal_pin_t *)hal_ptr(p._bp);
    hal_data_u *u = (hal_data_u *)hal_ptr(pin->data_ptr);
    ck_pr_store_8((char *) &u->_b, value);
    if (__builtin_expect(!!(hh_get_wmb(&pin->hdr)), 0)) ck_pr_fence_store();;
    return value;
}


     uint64_t ck_pr_load_64(const uint64_t *target);
     uint32_t ck_pr_load_32(const uint32_t *target);
     uint16_t ck_pr_load_16(const uint16_t *target);
     uint8_t  ck_pr_load_8(const uint8_t *target);

type __atomic_load_n (type *ptr, int memorder);
void __atomic_load (type *ptr, type *ret, int memorder)
// It returns the contents of *ptr in *ret.
void __atomic_store_n (type *ptr, type val, int memorder)
//This built-in function implements an atomic store operation. It writes val into *ptr.
void __atomic_store (type *ptr, type *val, int memorder)
#endif

//#undef HAVE_CK

#ifdef HAVE_CK
#define BITCAST    (uint8_t *)
#define S32CAST    (uint32_t *) //(hal_s32_t *)
#define U32CAST    (uint32_t *) //(hal_u32_t *)
#define U64CAST    (uint64_t *) //(hal_float_t *)
#define S64CAST    (uint64_t *) //(hal_float_t *)
#define FLOATCAST  (uint64_t *) //(hal_float_t *)

#define BITSTORE   pr_store_8
#define S32STORE   pr_store_32
#define U32STORE   pr_store_32
#define S64STORE   pr_store_64
#define U64STORE   pr_store_64
#define FLOATSTORE pr_store_64

#define BITLOAD    pr_load_8
#define S32LOAD    pr_load_32
#define U32LOAD    pr_load_32
#define S64LOAD    pr_load_64
#define U64LOAD    pr_load_64
#define FLOATLOAD  pr_load_64

#define _STORE(dest, value, op, cast) ck_##op(cast dest, value)
#define _LOAD(src, op, cast)          ck_##op(cast src)

#else
#define BITCAST   // (hal_bit_t *)
#define S32CAST   // (hal_s32_t *)
#define U32CAST   // (hal_u32_t *)
#define FLOATCAST // (hal_float_t *)
#define BITSTORE   __atomic_store_n
#define S32STORE   __atomic_store_n
#define U32STORE   __atomic_store_n
#define S64STORE   __atomic_store_n
#define U64STORE   __atomic_store_n
#define FLOATSTORE __atomic_store_n
#define BITLOAD    __atomic_load_n
#define S32LOAD    __atomic_load_n
#define U32LOAD    __atomic_load_n
#define S64LOAD    __atomic_load_n
#define U64LOAD    __atomic_load_n
#define FLOATLOAD  __atomic_load_n

#define _STORE(dest, value, op, type) op(dest, value, RTAPI_MEMORY_MODEL)
#define _LOAD(src, op, cast)          op(src, RTAPI_MEMORY_MODEL)

#endif


// how a HAL value is set, given a pointer to a hal_data_u,
// including any memory barrier
#define _SETVALUE(OBJ, TAG, VALUE, OP, CAST)				\
    _STORE(&u->TAG, VALUE, OP, CAST);					\
    if (unlikely(hh_get_wmb(&OBJ->hdr)))				\
	rtapi_smp_wmb();

// how a HAL value is retrieved, given a pointer to a hal_data_u,
// including any memory barrier
#define _GETVALUE(OBJ, TAG, VIA, OP, CAST)				\
    if (unlikely(hh_get_rmb(&OBJ->hdr)))				\
	rtapi_smp_rmb();						\
    hal_data_u rv;							\
    rv.VIA = _LOAD(&u->VIA, OP, CAST);					\
    return rv.TAG



// export context-independent setters which are strongly typed,
// and context-dependent accessors with a descriptor argument,
// and an optional runtime type check
#define PINSETTER(TYPE, OTYPE, LETTER, ACCESS, OP, CAST)			\
    static inline const hal_##TYPE##_t					\
    set_##TYPE##_pin(TYPE##_pin_ptr p,					\
		     const hal_##TYPE##_t value) {			\
    hal_pin_t *pin =							\
	(hal_pin_t *)hal_ptr(p._##LETTER##p);				\
    hal_data_u *u =							\
	(hal_data_u *)hal_ptr(pin->data_ptr);				\
    _SETVALUE(pin, ACCESS, value, OP, CAST);				\
    return value;							\
    }									\
									\
    static inline const hal_##TYPE##_t					\
    _set_##TYPE##_pin(hal_pin_t *pin,					\
		      const hal_##TYPE##_t value) {			\
    hal_data_u *u =							\
	(hal_data_u *)hal_ptr(pin->data_ptr);				\
    _CHECK(pin_type(pin), OTYPE);					\
    _SETVALUE(pin, ACCESS, value, OP, CAST);				\
    return value;							\
    }

// emit typed pin setters
PINSETTER(bit,   HAL_BIT,   b,   _b,  BITSTORE,   BITCAST);
PINSETTER(s32,   HAL_S32,   s,   _s,  S32STORE,   S32CAST);
PINSETTER(u32,   HAL_U32,   u,   _u,  U32STORE,   U32CAST);
PINSETTER(u64,   HAL_U64,   lu,  _lu, U64STORE,   U64CAST);
PINSETTER(s64,   HAL_S64,   ls,  _ls, S64STORE,   S64CAST);
PINSETTER(float, HAL_FLOAT, f,   _f,  FLOATSTORE, FLOATCAST);


#define PINGETTER(TYPE, OTYPE, LETTER, ACCESS, OP, CAST)		\
    static inline const hal_##TYPE##_t					\
	 get_##TYPE##_pin(const TYPE##_pin_ptr p) {			\
    const hal_pin_t *pin =						\
	(const hal_pin_t *)hal_ptr(p._##LETTER##p);			\
    const hal_data_u *u =						\
	(const hal_data_u *)hal_ptr(pin->data_ptr);			\
    _GETVALUE(pin, _##LETTER, ACCESS, OP, CAST);			\
    }									\
									\
    static inline const hal_##TYPE##_t					\
    _get_##TYPE##_pin(const hal_pin_t *pin) {				\
	const hal_data_u *u =						\
	    (const hal_data_u *)hal_ptr(pin->data_ptr);			\
	_CHECK(pin_type(pin), OTYPE)					\
        _GETVALUE(pin, _##LETTER, ACCESS, OP, CAST);			\
    }

// emit typed pin getters
PINGETTER(bit,   HAL_BIT,   b,   _b,  BITLOAD,   BITCAST);
PINGETTER(s32,   HAL_S32,   s,   _s,  S32LOAD,   S32CAST);
PINGETTER(u32,   HAL_U32,   u,   _u,  U32LOAD,   U32CAST);
PINGETTER(float, HAL_FLOAT, f,   _f,  FLOATLOAD, FLOATCAST);
PINGETTER(u64,   HAL_U64,   lu,  _lu, U64LOAD,   U64CAST);
PINGETTER(s64,   HAL_S64,   ls,  _ls, S64LOAD,   S64CAST);

#if 0

// atomically increment a value (integral types only)
// unclear how to do the equivalent of an __atomic_add_fetch
// with ck, so use gcc intrinsics for now:
#define _INCREMENT(U, DESC, TYPE, TAG, VALUE)				\
    TYPE rvalue = __atomic_add_fetch(&U->TAG, VALUE,			\
				     RTAPI_MEMORY_MODEL);		\
    if (unlikely(hh_get_wmb(&DESC->hdr)))				\
	rtapi_smp_wmb();						\
    return rvalue;

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
#endif

// signal getters
#define SIGGETTER(TYPE, OTYPE, LETTER, ACCESS, OP, CAST)		\
    static inline const hal_##TYPE##_t					\
    get_##TYPE##_sig(const TYPE##_sig_ptr s) {				\
    const hal_sig_t *sig =						\
	(const hal_sig_t *)hal_ptr(s._##LETTER##s);			\
    hal_data_u *u = (hal_data_u*)&sig->value;				\
    _GETVALUE(sig, _##LETTER, ACCESS, OP, CAST);			\
    }									\
									\
    static inline const hal_##TYPE##_t					\
    _get_##TYPE##_sig(const hal_sig_t *sig) {				\
    _CHECK(sig_type(sig), OTYPE);					\
    hal_data_u *u = (hal_data_u*)&sig->value;				\
    _GETVALUE(sig, _##LETTER, ACCESS, OP, CAST);			\
    }

// emit typed signal getters
SIGGETTER(bit,   HAL_BIT,   b,   _b,  BITLOAD,   BITCAST);
SIGGETTER(s32,   HAL_S32,   s,   _s,  S32LOAD,   S32CAST);
SIGGETTER(u32,   HAL_U32,   u,   _u,  U32LOAD,   U32CAST);
SIGGETTER(float, HAL_FLOAT, f,   _f,  FLOATLOAD, FLOATCAST);
SIGGETTER(u64,   HAL_U64,   lu,  _lu, U64LOAD,   U64CAST);
SIGGETTER(s64,   HAL_S64,   ls,  _ls, S64LOAD,   S64CAST);

#define SIGSETTER(TYPE, OTYPE, LETTER, ACCESS, OP, CAST)		\
    static inline const hal_##TYPE##_t					\
    set_##TYPE##_sig(TYPE##_sig_ptr s,					\
		     const hal_##TYPE##_t value) {			\
	hal_sig_t *sig =						\
	    (hal_sig_t *)hal_ptr(s._##LETTER##s);			\
	hal_data_u *u = &sig->value;					\
	_SETVALUE(sig, ACCESS, value, OP, CAST);			\
	return value;							\
    }									\
									\
    static inline const hal_##TYPE##_t					\
    _set_##TYPE##_sig(hal_sig_t *sig,					\
		      const hal_##TYPE##_t value) {			\
	hal_data_u *u = &sig->value;					\
	_CHECK(sig_type(sig), OTYPE);					\
	_SETVALUE(sig, ACCESS, value, OP, CAST);			\
	return value;							\
    }


// emit typed signal setters
SIGSETTER(bit,   HAL_BIT,   b,   _b,  BITSTORE,   BITCAST);
SIGSETTER(s32,   HAL_S32,   s,   _s,  S32STORE,   S32CAST);
SIGSETTER(u32,   HAL_U32,   u,   _u,  U32STORE,   U32CAST);
SIGSETTER(u64,   HAL_U64,   lu,  _lu, U64STORE,   U64CAST);
SIGSETTER(s64,   HAL_S64,   ls,  _ls, S64STORE,   S64CAST);
SIGSETTER(float, HAL_FLOAT, f,   _f,  FLOATSTORE, FLOATCAST);

// typed NULL tests for pins and signals
#define PINNULL(TYPE, FIELD)						\
    static inline bool TYPE##_pin_null(const TYPE##_pin_ptr p) {	\
	return p.FIELD == 0;						\
}
PINNULL(bit,  _bp)
PINNULL(s32,  _sp)
PINNULL(u32,  _up)
PINNULL(u64,  _lup)
PINNULL(s64,  _lsp)
PINNULL(float,_fp)

#define SIGNULL(TYPE, FIELD)						\
    static inline bool TYPE##_sig_null(const TYPE##_sig_ptr s) {	\
	return s.FIELD == 0;						\
}
SIGNULL(bit,  _bs)
SIGNULL(s32,  _ss)
SIGNULL(u32,  _us)
SIGNULL(u64,  _lus)
SIGNULL(s64,  _lss)
SIGNULL(float,_fs)


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
