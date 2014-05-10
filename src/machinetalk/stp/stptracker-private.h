#ifndef STPTRACKER_PRIVATE_H
#define STPTRACKER_PRIVATE_H

#include <czmq.h>

#include <stp.h>
#include <machinetalk/generated/message.pb.h>

#include <string>
#include <list>
#include <unordered_map>

using namespace google::protobuf;

typedef struct _mvar {
    const char *name;
    pb::ValueType type;
    stp_valueref_u value;
    size_t bsize; // for strings
    int var_updates;
    var_change_cb *callback;
    void *callback_arg;
    int serial;
} _mvar_t;

//  handle -> variable descriptor
typedef std::unordered_map<int, _mvar_t *> handle_map;
typedef handle_map::iterator handle_iterator;

typedef std::unordered_map<std::string, _mvar_t *> name_map;
typedef name_map::iterator name_iterator;

typedef std::list<_mvar_t *> var_list;
typedef var_list::iterator value_iterator;

typedef struct _mgroup {
    const char *name;
    group_update_complete_cb *callback;
    void *callback_arg;
    var_list vars;
    handle_map byhandle;
    name_map byname;
    int serial;
    int group_full_updates;
    int group_incr_updates;
}  _mgroup_t ;

// topic -> group descriptor
typedef std::unordered_map<std::string, _mgroup_t *> group_map;

typedef struct _msource {
    std::string origin;
    group_map groups;
    void *socket;
    zmq_pollitem_t pollitem;
    mtracker_t *tracker; // backlink for ctx, Container
} _msource_t;
typedef std::list<_msource_t *> source_list;

typedef struct _mtracker {
    zctx_t *ctx;
    source_list sources;
    pb::Container update;
    void *pipe; // command pipe to updater thread
} _mtracker_t;


#endif // STPTRACKER_PRIVATE_H
