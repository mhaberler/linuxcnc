#ifndef STPTRACKER_PRIVATE_H
#define STPTRACKER_PRIVATE_H

#include <czmq.h>
#include <zlist.h>

#include <stp.h>
#include <protobuf/generated/message.pb.h>

using namespace google::protobuf;

struct _mvar {
    const char *name;
    pb::ValueType type;
    stp_valueref_u value;
    int var_updates;
};

struct _mgroup {
    const char *name;
    group_complete_cb *callback;
    zlist_t *vars;
    int serial;
    bool full_update_pending;
    int group_updates;
};

struct _msource_t {
    mservice_t *service;
    zlist_t *groups;
    // protohandler *update
    // protohandler *subscribe, unsubscribe?
};

struct _mservice_t {
    char *uri;
    int socket;
    int protocol;
    int minversion;
    int service_instance;
};


struct _mtracker {
    int discovery_port;
    zlist_t *services;
    pb::Container *update;
};

#endif // STPTRACKER_PRIVATE_H
