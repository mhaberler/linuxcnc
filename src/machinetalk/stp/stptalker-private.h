#ifndef STPTALKER_PRIVATE_H
#define STPTALKER_PRIVATE_H

#include <czmq.h>
#include <zlist.h>

#include <stp.h>

#include <machinetalk/generated/message.pb.h>
using namespace google::protobuf;

struct _stvar {
    const char *name;
    pb::ValueType type;
    stp_valueref_u value;
    stp_valuetrack_u track;
    unsigned handle;
};

struct _stgroup {
    const char *name;
    int interval; // ms
    zlist_t *vars;
};

struct _sttalker {
    void *pipe;
    const char *uri;
    int interval;
    int port;
    int beacon_port;
    int stp_service;
    int beacon_fd;
    bool empty_updates;
    subscribe_cb *subscribe_callback;
    double epsilon;
    zlist_t *groups;
    zctx_t *context;
    void *update_socket;
    pb::Container *update;
    int serial;
};


#endif // STPTALKER_PRIVATE_H
