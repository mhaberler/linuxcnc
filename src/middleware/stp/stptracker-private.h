#ifndef STPTRACKER_PRIVATE_H
#define STPTRACKER_PRIVATE_H

#include <czmq.h>

#include <stp.h>
#include <middleware/generated/message.pb.h>

#include <string>
#include <list>
#include <unordered_map>

using namespace google::protobuf;

typedef struct _mvar {
    const char *name;
    pb::ValueType type;
    stp_valueref_u value;
    int var_updates;
} _mvar_t;

//  handle -> variable descriptor
typedef std::unordered_map<int, _mvar_t *> value_map;

typedef std::list<_mvar_t *> value_list;

typedef struct _mgroup {
    const char *name;
    group_update_complete_cb *callback;
    void *callback_arg;
    value_list vars;
    value_map byhandle;
    int serial;
    int group_updates;
}  _mgroup_t ;

// topic -> group descriptor
typedef std::unordered_map<std::string, _mgroup_t *> group_map;

typedef struct _msource {
    group_map groups;
    void *socket;
    zmq_pollitem_t pollitem;
    mtracker_t *tracker; // backlink for ctx, Container
} _msource_t;
typedef std::list<_msource_t *> source_list;

typedef struct _mtracker {
    zctx_t *ctx;
    source_list sources;
    pb::Container *update;
    void *pipe; // command pipe to updater thread
} _mtracker_t;

#endif // STPTRACKER_PRIVATE_H


    // protocol-specific callbacks
    // protohandler *update
    // protohandler *subscribe, unsubscribe?
