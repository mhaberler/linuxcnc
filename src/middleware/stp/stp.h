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


    int retcode(void *socket); // receive and parse return code on a socket

    enum stp_log_levels {
	STP_ERR = 1 << 0,
	STP_WARN = 1 << 1,
	STP_NOTICE = 1 << 2,
	STP_INFO = 1 << 3,
	STP_DEBUG = 1 << 4,
    };

    void _stp_log(int filter, const char *format, ...);
    void stp_set_log_level(int level,
			   void (*log_emit_function)(int level, const char *line));

    /* notice, warn and log are always compiled in */
#define stp_notice(...) _stp_log(STP_NOTICE, __VA_ARGS__)
#define stp_warn(...) _stp_log(STP_WARN, __VA_ARGS__)
#define stp_err(...) _stp_log(STP_ERR, __VA_ARGS__)
/*
 *  weaker logging can be deselected at configure time using --disable-debug
 *  that gets rid of the overhead of checking while keeping _warn and _err
 *  active
 */
#ifdef _DEBUG
#define stp_info(...) _stp_log(STP_INFO, __VA_ARGS__)
#define stp_debug(...) _stp_log(STP_DEBUG, __VA_ARGS__)
#else /* no debug */
#define stp_info(...)
#define stp_debug(...)
#endif


#ifdef __cplusplus
}
#endif
#endif // STP_H
