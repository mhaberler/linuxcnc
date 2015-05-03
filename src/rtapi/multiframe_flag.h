#ifndef _MULTIFRAME_FLAG_H
#define _MULTIFRAME_FLAG_H

typedef enum {
    MF_STRING         =  0,    // payload is a printable string
    MF_PROTOBUF       =  1,    // payload is in protobuf wire format
    MF_NPB_CSTRUCT    =  2,    // payload is in nanopb C struct format
    MF_JSON           =  3,    // payload is a JSON object (string)
    MF_GPB_TEXTFORMAT =  4,    // payload is google::protobuf::TextFormat (string)
    MF_XML            =  5,    // payload is XML format (string)
    MF_LEGACY_MOTCMD  =  6,    // motion command C structs, with cmd_code_t type tag
    MF_LEGACY_MOTSTAT =  7,    // motion status C structs, with cmd_status_t type tag

    // add here as needed and change the #define to point to the last encoding
    // used in the base code
#define MF_LAST  MF_LEGACY_MOTSTAT

    MF_UNUSED1        =  8,    // unused in code base  - user extensions
    MF_UNUSED2        =  9,    //
    // ...

} mf_encoding_t;


// disposition of the __u32 flags value
typedef struct {
    __u32 msgid     : 12; // must hold all proto msgid values (!)
    __u32 format    : 4;  // an mf_encoding_t - how to interpret the message
    __u32 more      : 1;  // zeroMQ marker for multiframe messages
    __u32 eor       : 1;  // zeroMQ marker end-of-route, next frame is payload
                          // zeroMQ route tags are marked by msgid == MSGID_HOP
    __u32 unused    : 14; // spare
} mfields_t;


typedef union {
    __u32     u;
    mfields_t f;
} mflag_t;


#endif // _MULTIFRAME_FLAG_H
