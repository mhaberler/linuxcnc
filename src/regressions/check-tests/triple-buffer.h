#ifndef _TRIPLE_BUFFER_H
#define _TRIPLE_BUFFER_H
#include <memory.h>
#include <rtapi_atomics.h>

// http://remis-thoughts.blogspot.co.at/2012/01/triple-buffering-as-concurrency_30.html
// rewritten to use rtapi_cas

/* bit flags are (unused) (new write) (2x dirty) (2x clean) (2x snap) */
// TBD: cacheline padding before/for flags
#define TRIPLE_BUFFER_TYPE(TYPENAME, TYPE)	\
    struct TYPENAME {				\
	TYPE buffer[3];				\
	uint_fast8_t flags;			\
    }

/* initially dirty = 0, clean = 1 and snap = 2 */
#define TRIPLE_BUFFER_NEW(NAME,TYPENAME)	\
    struct TYPENAME NAME;			\
    memset(&NAME, 0, sizeof(NAME));		\
    NAME.flags = 0x6;

#define TRIPLE_BUFFER_SNAP_PTR(BUFFER) &BUFFER.buffer[rtapi_load_u8(&BUFFER.flags) & 0x3]
#define TRIPLE_BUFFER_WRITE_PTR(BUFFER) &BUFFER.buffer[(rtapi_load_u8(&BUFFER.flags) & 0x30) >> 4]

#define TRIPLE_BUFFER_NEW_SNAP(BUFFER)					\
    do {								\
	uint_fast8_t flagsNow = rtapi_load_u8(&BUFFER.flags);		\
	uint_fast8_t newFlags;						\
	do {								\
	    if ((flagsNow & 0x40) == 0)					\
		break;							\
	    newFlags = (flagsNow & 0x30) |				\
		((flagsNow & 0x3) << 2) |				\
		((flagsNow & 0xC) >> 2);				\
	} while (!rtapi_cas_u8(&BUFFER.flags,  flagsNow, newFlags));	\
    } while(0)

#define TRIPLE_BUFFER_FLIP_WRITER(BUFFER)				\
    do {								\
	uint_fast8_t flagsNow;						\
	uint_fast8_t newFlags;						\
	do {								\
	    flagsNow = rtapi_load_u8(&BUFFER.flags);			\
	    newFlags = 0x40 |						\
		((flagsNow & 0xC) << 2) |				\
		((flagsNow & 0x30) >> 2) |				\
		(flagsNow & 0x3);					\
	} while (!rtapi_cas_u8(&BUFFER.flags,  flagsNow, newFlags));	\
    } while(0)


#endif
