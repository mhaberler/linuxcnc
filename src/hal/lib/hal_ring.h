#ifndef HAL_RING_H
#define HAL_RING_H

#include <rtapi.h>
#include <ring.h>
#include <multiframe.h>
#include <multiframe_flag.h>


RTAPI_BEGIN_DECLS

// a ring buffer exists always relative to a local instance
// it is owned by the hal_lib component since because rtapi_shm_new() requires a module id
// any module may attach to it.

typedef struct {
    char name[HAL_NAME_LEN + 1]; // ring HAL name
    int next_ptr;		 // next ring in used/free lists
    int ring_id;                 // as per alloc bitmap
    int ring_shmkey;             // RTAPI shm key - if in shmseg
    int total_size;              // size of shm segment allocated
    unsigned ring_offset;        // if created in HAL shared memory
    unsigned flags;              // RTAPI-level flags
    int handle;                  // unique ID
    int paired_handle;           // unique ID of a paired ring (>0)
    __u8 encodings;              // bitmap of hal_ring_encodings_t
    __u8 haltalk_zeromq_stype : 5;   // tell haltalk which socket type to use
    __u8 haltalk_adopt : 1;      // haltalk shall adopt this ring
    __u8 haltalk_announce : 1;   // haltalk shall announce this ring via mDNS
    __u8 haltalk_writes : 1;     // haltalk reads if zero
} hal_ring_t;

// some components use a fifo and a scratchpad shared memory area,
// like sampler.c and streamer.c. ringbuffer_t supports this through
// the optional scratchpad, which is created if spsize is > 0
// on hal_ring_new(). The scratchpad size is recorded in
// ringheader_t.scratchpad_size.

// generic ring methods for all modes:

// create a named ringbuffer, owned by hal_lib
// takes a name, and RTAPI-level flags:

// ------------ RTAPI-level ring flags -----------
// mode contains options which are relevant at the rtai/ring.h
// level. It is an 'or' of:

// exposed in ringheader_t.type:
// #define RINGTYPE_RECORD    0
// #define RINGTYPE_MULTIPART RTAPI_BIT(0)
// #define RINGTYPE_STREAM    RTAPI_BIT(1)

// mode flags passed in by ring_new
// exposed in ringheader_t.{use_rmutex, use_wmutex, alloc_halmem}
// #define USE_RMUTEX       RTAPI_BIT(2)
// #define USE_WMUTEX       RTAPI_BIT(3)
// #define ALLOC_HALMEM     RTAPI_BIT(4)

// also handled at the ring.h level:
// spsize > 0 will allocate a shm scratchpad buffer
// accessible through ringbuffer_t.scratchpad/ringheader_t.scratchpad

// ---------------- HAL-level ring flags --------------------
//
// there is a separate set of flags which apply strictly at the
// HAL level and are not 'passed down' to the ring.h code as they
// have no meaning there. The categories are:

// encoding - indicates which type(s) of encoding must be present
//            if writing a message into this ring
//            set by reader, inspected by writer
//            the encoding must be present in the actual write
//            operation in the multiframe flag, see multiframe_flags.h
//            inspected by readers and writers
//            a bitmap of mf_encoding_t flags
//
// see also : rtapi/multiframe_flag.h
//
// to indicate support for understanding a particular encoding,
// set the corresponding bit(s) in hal_ring_t.encodings:
// NB: encoding is a bitmap, whereas mfields_t.format is one
// of the underlying values (better be one only ;)
typedef enum {
	RE_TEXT         =  RTAPI_BIT(MF_STRING),      // payload is a printable string
	RE_PROTOBUF     =  RTAPI_BIT(MF_PROTOBUF),    // payload is in protobuf wire format
	RE_NPB_CSTRUCT  =  RTAPI_BIT(MF_NPB_CSTRUCT), // payload is in nanopb C struct format
	RE_JSON         =  RTAPI_BIT(MF_JSON),        // payload is a JSON object (string)
	RE_GPB_TEXTFORMAT  =  RTAPI_BIT(MF_GPB_TEXTFORMAT),// payload is google::protobuf::TextFormat (string)
	RE_XML          =  RTAPI_BIT(MF_XML),         // payload is XML format (string)
        RE_LEGACY_MOTCMD   =  RTAPI_BIT(MF_LEGACY_MOTCMD),     // motion command C structs
        RE_LEGACY_MOTSTAT   =  RTAPI_BIT(MF_LEGACY_MOTSTAT),   // motion status C structs

	//   RE_UNUSED1      =  RTAPI_BIT(MF_UNUSED1),     // unused in base code - user extensions
} hal_ring_encodings_t;
#define RE_MAX 255

// ring pairing: indicates there is a second ring which is to be used
//               as a matching channel in the other direction
//               indicated by non-zero handle,
//               to look up, use halpr_find_ring_by_id()
//               set in one of the two rings only,the paired ring
//               may not have any haltalk_* flags set (!)

// flags for driving haltalk behavior:
//
// haltalk adoption: this ring is to be served by haltalk.
// haltalk announcements: if served by haltalk, announce via mDNS
// haltalk direction: read or write by haltalk
// zeroMQ socket type: PUSH/PULL/SUB/PUB/DEALER/ROUTER etc
//               hints to haltalk how to serve this ring
//               for dual-direction channels like DEALER or ROUTER
//               a paired ring can be used for the other direction
//               needs to be set in one of the rings, the paired ring's
//               flags are ignored
//               uses the pb::socketType enum
//               to set, include types.npb.h and use one of
//               pb_socketType_ST_ZMQ_<sockettype> #defines

// named HAL rings shared memory segments are owned by the hal_lib component,
// since only a component may own shm segments, but rings live outside
// components - normal components do not make sense as owners since
// their lifetime might be shorter than the ring, and making rings permanent
// gets around referential integrity issues

int hal_ring_new(const char *name, int size, int sp_size, int mode);

// printf-style version of the above
int hal_ring_newf(int size, int sp_size, int mode, const char *fmt, ...)
    __attribute__((format(printf,4,5)));

// delete a ring buffer.
// will fail if the refcount is > 0 (meaning the ring is still attached somewhere).
int hal_ring_delete(const char *name);

// printf-style version of the above
int hal_ring_deletef(const char *fmt, ...)
    __attribute__((format(printf,1,2)));

// make an existing ringbuffer accessible to a component, or test for
// existence and flags of a ringbuffer
//
// to attach:
//     rb must point to storage of type ringbuffer_t.
//     Increases the reference count on successful attach
//     store halring flags in *flags if non-zero.
//
// to test for existence:
//     hal_ring_attachf(NULL, NULL, name) returns 0 if the ring exists, < 0 otherwise
//
// to test for existence and retrieve the ring's flags:
//     hal_ring_attachf(NULL, &f, name) - if the ring exists, returns 0
//     and the ring's flags are returned in f
//

// printf-style version of the above
int hal_ring_attachf(ringbuffer_t *rb, unsigned *flags, const char *fmt, ...)
    __attribute__((format(printf,3,4)));

// detach a ringbuffer. Decreases the reference count.
int hal_ring_detach(ringbuffer_t *rb);

typedef enum {
    // second argument to hal_ring_setflag/getflag to indicate
    // operation
    HF_ENCODINGS = 1,
    HF_HALTALK_ADOPT,
    HF_HALTALK_ANNOUNCE,
    HF_HALTALK_WRITES,
    HF_ZEROMQ_SOCKETTYPE,
} halring_flag_t;

// setter/getter for these flags
int hal_ring_setflag(const int ring_id, const unsigned flagtype, unsigned value);
int hal_ring_getflag(const int ring_id, const unsigned flagtype, unsigned *value);

// pair a ring to this ring by ID
int hal_ring_pair(const int this_ring, const int other_ring);


// not part of public API. Use with HAL lock engaged.
hal_ring_t *halpr_find_ring_by_name(const char *name);
hal_ring_t *halpr_find_ring_by_id(const int id);

// no-lock version of key methods
int halpr_ring_new(const char *name, int size, int sp_size, int mode);
int halpr_ring_attach_by_desc(hal_ring_t *r, ringbuffer_t *rb, unsigned *flags);
int halpr_ring_attach_by_id(const int id, ringbuffer_t *rbptr,unsigned *flags);
int halpr_ring_attach_by_name(const char *name, ringbuffer_t *rbptr, unsigned *flags);
int halpr_ring_detach(ringbuffer_t *rbptr);

RTAPI_END_DECLS

#endif /* HAL_RING_H */
