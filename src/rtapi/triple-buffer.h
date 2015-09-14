#ifndef _TRIPLE_BUFFER_H
#define _TRIPLE_BUFFER_H
#include <stdbool.h>
#include <memory.h>
#include <rtapi_atomics.h>

// http://remis-thoughts.blogspot.co.at/2012/01/triple-buffering-as-concurrency_30.html
// rewritten to use rtapi_cas


// declare a triple buffer like so:
//
// struct foo {
//     int whatever;
// }
//
// TB_FLAG(foo_flag);         // alternatively:
// TB_FLAG_FAST(foo_flag);    // a cache-friendly flag trading off memory for speed
// struct foo foo_buffer[3];  // actual triple buffer
//
// init the tb flag:
//     rtapi_tb_init(&foo_flag);
//
// write to the current buffer:
//     foo_buffer[rtapi_tb_write_idx(&foo_flag)].whatever = 42;
//
// flip the current write buffer:
//     rtapi_tb_flip(&foo_flag);
//
// take a new snapshot and work with it:
//
// if (rtapi_tb_snapshot(&foo_flag)) {
//      // new snapshot data available
//      // access like so:
//      blah = foo_buffer[rtapi_tb_snap_idx(&foo_flag)].whatever
// } else {
//       // no new snapshot available
// }


// declare a triple buffer flag:
#define TB_FLAG(name) hal_u8_t name

// cache-friendly version of triple buffer flag declaration
#define TB_FLAG_FAST(name)					\
    hal_u8_t name __attribute__((aligned(RTAPI_CACHELINE)));	\
    char __##name##pad[RTAPI_CACHELINE - sizeof(hal_u8_t)];

// bit flags are (unused) (new write) (2x dirty) (2x clean) (2x snap)
// initially dirty = 0, clean = 1 and snap = 2
static inline void rtapi_tb_init(hal_u8_t *flags) {
    *flags = 0x6;
}

// index of current snap buffer
static inline hal_u8_t rtapi_tb_snap_idx(hal_u8_t *flags) {
    return rtapi_load_u8(flags) & 0x3;
}

// index of current write buffer
static inline hal_u8_t rtapi_tb_write_idx(hal_u8_t *flags) {
    return (rtapi_load_u8(flags) & 0x30) >> 4;
}

// create a new snapshot.
// returns false if no new data available.
static inline bool rtapi_tb_snapshot(hal_u8_t *flags) {
	hal_u8_t flagsNow = rtapi_load_u8(flags);
	hal_u8_t newFlags;
	do {
	    if ((flagsNow & 0x40) == 0)
		return false;
	    newFlags = (flagsNow & 0x30) |
		((flagsNow & 0x3) << 2) |
		((flagsNow & 0xC) >> 2);
	} while (!rtapi_cas_u8(flags,  flagsNow, newFlags));
	return true;
}

// flip the write buffer
static inline void rtapi_tb_flip(hal_u8_t *flags) {
    hal_u8_t flagsNow;
    hal_u8_t newFlags;
    do {
	flagsNow = rtapi_load_u8(flags);
	newFlags = 0x40 |
	    ((flagsNow & 0xC) << 2) |
	    ((flagsNow & 0x30) >> 2) |
	    (flagsNow & 0x3);
    } while (!rtapi_cas_u8(flags,  flagsNow, newFlags));
}

#endif
