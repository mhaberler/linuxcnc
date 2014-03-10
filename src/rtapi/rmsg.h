#ifndef RMSG_H
#define RMSG_H

#include "rtapi_bitops.h"

typedef enum {
    ENC_PROTOBUF,
    ENC_NANOPB_C,
    ENC_OTHER
} encoding_t;

typedef enum {
    MSGTYPE_CONTAINER,
    MSGTYPE_RTMESSAGE
} msgtype_t;

typedef struct rframe {
    size_t size;
    size_t offset;
    unsigned int msgtype  : 8;
    unsigned int encoding : 8;
    unsigned int flags    : 16;
} rframe_t;

typedef struct rmsg {
    size_t nframes;
    rframe_t frames[0];
} rmsg_t;


static inline size_t rmsg_headersize(size_t nframes)
{
    return sizeof(rmsg_t) + sizeof(rframe_t) * nframes;
}

static inline size_t rframe_size(rmsg_t *r, size_t n)
{
    if (n > r->nframes-1)
	return -1;
    return r->frames[n].size;
}

#endif // RMSG_H
