#ifndef _MULTIFRAME_FLAG_H
#define _MULTIFRAME_FLAG_H

typedef enum {
    // rtproxy couldnt determine what this is, so leave it to RTcomp
    MF_UNSPECIFIED    = 0,

    MF_ORIGINATOR     = 1,    // from
    MF_TARGET         = 2,    // to
    MF_PROTOBUF       = 3,    // payload in protobuf wire format
    MF_NPB_CSTRUCT    = 4,    // payload is in nanopb C struct format
} mframetype_t;


// if MF_PROTOBUF or MF_DESERIALIZED,
// pbmsgtype denotes the protobuf message type contained
typedef enum {
    NPB_UNSPECIFIED   = 0,
    NPB_CONTAINER     = 1,
    NPB_RTMESSAGE     = 2,
} npbtype_t;

// disposition of the __u32 flags value
typedef struct {
    __u32 frametype : 8;
    __u32 pbmsgtype : 8;
    __u32 count     : 8;  // if repeated message
    __u32 __unused  : 8;
} mfields_t;


typedef union {
    __u32     u;
    mfields_t f;
} mflag_t;


#endif // _MULTIFRAME_FLAG_H
