/*
 * Copyright (C) 2013-2014 Michael Haberler <license@mah.priv.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <getopt.h>
#include <uuid/uuid.h>
#include <czmq.h>

#include <string>
#include <unordered_map>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_group.h>
#include <hal_rcomp.h>
#include <sdpublish.h>
#include <inifile.h>
#include <redirect_log.h>


#include <middleware/generated/message.pb.h>
using namespace google::protobuf;

// announced protocol versions
#define HAL_GROUP_STATUS_VERSION 1
#define HAL_RCOMP_STATUS_VERSION 1
#define HAL_RCOMMAND_VERSION     1

#if JSON_TIMING
#include <middleware/json2pb/json2pb.h>
#include <jansson.h>
#endif

typedef enum {
    GROUP_REPORT_FULL = 1,
} group_flags_t;

typedef enum {
    RCOMP_REPORT_FULL = 1,
} rcomp_flags_t;

typedef struct htself htself_t;

typedef struct {
    hal_compiled_group_t *cg;
    int serial; // must be unique per active group
    unsigned flags;
    htself_t *self;
    int timer_id; // > -1: scan timer active - subscribers present
    int msec;
} group_t;

typedef struct {
    hal_compiled_comp_t *cc;
    int serial; // must be unique per active group
    unsigned flags;
    htself_t *self;
    int timer_id;
    int msec;
} rcomp_t;

typedef std::unordered_map<std::string, group_t *> groupmap_t;
typedef groupmap_t::iterator groupmap_iterator;

typedef std::unordered_map<std::string, rcomp_t *> compmap_t;
typedef compmap_t::iterator compmap_iterator;

typedef struct htconf {
    const char *progname;
    const char *inifile;
    const char *section;
    const char *modname;
    const char *group_status;
    const char *rcomp_status;
    const char *command;

    int debug;
    int sddebug;
    int default_group_timer; // msec
    int default_rcomp_timer; // msec
    int sd_port;
} htconf_t;

typedef struct htself {
    htconf_t *cfg;
    uuid_t instance_uuid;
    int comp_id;
    int signal_fd;
    bool interrupted;
    pid_t pid;

    pb::Container rx; // any ParseFrom.. function does a Clear() first
    pb::Container tx; // tx must be Clear()'d after or before use

    zctx_t *z_context;
    zloop_t *z_loop;

    groupmap_t groups;
    void *z_status;
    const char *z_status_dsn;

    spub_t *sd_publisher;

    void *z_rcomp_status;
    const char *z_rcomp_status_dsn;
    compmap_t rcomps;

    void *z_command;
    const char *z_command_dsn;
} htself_t;


// haltalk_group.cc:
int scan_groups(htself_t *self);
int release_groups(htself_t *self);
int handle_group_timer(zloop_t *loop, int timer_id, void *arg);
int handle_group_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg);

// haltalk_rcomp.cc:
int scan_comps(htself_t *self);
int release_comps(htself_t *self);
int handle_rcomp_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg);
int handle_rcomp_timer(zloop_t *loop, int timer_id, void *arg);

// haltalk_sdiscover.cc:
int service_discovery_start(htself_t *self);
int service_discovery_stop(htself_t *self);
