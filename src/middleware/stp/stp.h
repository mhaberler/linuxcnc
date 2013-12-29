#ifndef _STP_H
#define _STP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hal.h"

    typedef struct _blob {
	const void **bref;
	size_t *bsize;
    } stpblob_t;

    typedef union {
	hal_bit_t *b;
	hal_s32_t *s;
	hal_s32_t *u;
	hal_float_t *f;
	stpblob_t blob;
    } stp_valueref_u;

    typedef union {
	hal_bit_t b;
	hal_s32_t s;
	hal_s32_t u;
	hal_float_t f;
	bool changed;
    } stp_valuetrack_u;

#ifdef __cplusplus
}
#endif
#endif // STP_H
