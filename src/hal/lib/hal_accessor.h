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
#define _PINSET(ptr,member,type,value)			\
    type *vp = SHMPTR(ptr.member->data_ptr);		\
    __atomic_store(vp, &(value), RTAPI_MEMORY_MODEL);	\
    if (unlikely(hh_get_wmb(&ptr.member->hdr)))		\
	rtapi_smp_wmb();

#define _PINGET(ptr,member,type)			 \
    if (unlikely(hh_get_rmb(&ptr.member->hdr)))		 \
	rtapi_smp_rmb();				 \
    type value;						 \
    __atomic_load((type *) SHMPTR(ptr.member->data_ptr), \
		  &value, RTAPI_MEMORY_MODEL);		 \
    return value;

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
// the data_ptr reference should eventually be replaced
// by directly referencing sig.value, a hal_data_u
#define _SIGGET(ptr,member,type)			 \
    if (unlikely(hh_get_rmb(&ptr.member->hdr)))		 \
	rtapi_smp_rmb();				 \
    type value;						 \
    __atomic_load((type *) SHMPTR(ptr.member->data_ptr), \
		  &value, RTAPI_MEMORY_MODEL);		 \
    return value;
static inline hal_bit_t get_bit_sig(const bit_sig_ptr s) {
    _SIGGET(s, _bs, hal_bit_t)
}
static inline hal_s32_t get_s32_sig(const s32_sig_ptr s) {
    _SIGGET(s, _ss, hal_s32_t)
}
static inline hal_u32_t get_u32_sig(const u32_sig_ptr s) {
    _SIGGET(s, _us, hal_u32_t)
}
static inline hal_float_t get_float_sig(const float_sig_ptr s) {
    _SIGGET(s, _fs, hal_float_t)
}

// typed signal setters
#define _SIGSET(ptr,member,type,value)			\
    type *vp = SHMPTR(ptr.member->data_ptr);		\
    __atomic_store(vp, &(value), RTAPI_MEMORY_MODEL);	\
    if (unlikely(hh_get_wmb(&ptr.member->hdr)))		\
	rtapi_smp_wmb();

static inline void set_bit_sig(bit_sig_ptr s, const hal_bit_t value) {
    _SIGSET(s, _bs, hal_bit_t, value);
}
static inline void set_s32_sig(s32_sig_ptr s, const hal_s32_t value) {
    _SIGSET(s, _ss, hal_s32_t, value);
}
static inline void set_u32_sig(u32_sig_ptr s, const hal_u32_t value) {
    _SIGSET(s, _us, hal_u32_t, value);
}
static inline void set_float_sig(float_sig_ptr s, const hal_float_t value) {
    _SIGSET(s, _fs, hal_float_t, value);
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


// pin allocators
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
