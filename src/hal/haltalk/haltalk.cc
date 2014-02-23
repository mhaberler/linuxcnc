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

// haltalk:
//   1. reports the status of HAL signals aggregated into
//      HAL groups via the Status Tracking protocol.
//
//   2. implements the HALRcomp protocol for remote HAL components.

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
#include <inifile.h>
#include <sdpublish.h>  // for UDP service discovery
#include <redirect_log.h>


#include <middleware/generated/message.pb.h>
using namespace google::protobuf;

// announced protocol versions
#define STP_VERSION 1
#define HAL_RCOMP_VERSION 1

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


static const char *option_string = "hI:S:dt:Du:r:T:";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {"sddebug", required_argument, 0, 'D'},
    {"gtimer", required_argument, 0, 't'},
    {"ctimer", required_argument, 0, 'T'},
    {"stpuri", required_argument, 0, 'u'},
    {"rcompuri", required_argument, 0, 'r'},
    {0,0,0,0}
};

typedef struct htconf {
    const char *progname;
    const char *inifile;
    const char *section;
    const char *modname;
    const char *status;
    const char *rcomp_status;
    int debug;
    int sddebug;
    int pid;
    int default_group_timer; // msec
    int default_rcomp_timer; // msec
    int sd_port;
} htconf_t;

static htconf_t conf = {
    "",
    NULL,
    "HALTALK",
    "haltalk",
    "tcp://127.0.0.1:*", // localhost, use ephemeral port
    "tcp://127.0.0.1:*",
    0,
    0,
    100,
};

typedef struct htself {
    htconf_t *cfg;
    uuid_t instance_uuid;
    int comp_id;
    groupmap_t groups;
    zctx_t *z_context;
    void *z_status;
    const char *z_status_dsn;
    int signal_fd;
    zloop_t *z_loop;
    pb::Container rx; // any ParseFrom.. function does a Clear() first
    pb::Container tx; // tx must be Clear()'d after or before use
    spub_t *sd_publisher;
    bool interrupted;

    void *z_rcomp_status;
    const char *z_rcomp_status_dsn;
    compmap_t rcomps;
} htself_t;

static int
handle_signal(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *)arg;
    struct signalfd_siginfo fdsi;
    ssize_t s;

    s = read(self->signal_fd, &fdsi, sizeof(struct signalfd_siginfo));
    if (s != sizeof(struct signalfd_siginfo)) {
	perror("read");
    }
    switch (fdsi.ssi_signo) {
    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signal %d - '%s' received\n",
			self->cfg->progname, fdsi.ssi_signo, strsignal(fdsi.ssi_signo));
    }
    self->interrupted = true;
    return -1; // exit reactor with -1
}


static int
group_report_cb(int phase, hal_compiled_group_t *cgroup, int handle,
		hal_sig_t *sig, void *cb_data)
{
    group_t *grp = (group_t *) cb_data;
    htself_t *self = grp->self;
    hal_data_u *vp;
    pb::Signal *signal;
    zmsg_t *msg;
    int retval;

    switch (phase) {

    case REPORT_BEGIN:	// report initialisation
	// this enables a new subscriber to easily detect she's receiving
	// a full status snapshot, not just a change tracking update
	if (grp->flags & GROUP_REPORT_FULL) {
	    self->tx.set_type(pb::MT_STP_UPDATE_FULL);
	    // enable clients to detect a restart
	    self->tx.set_uuid(self->instance_uuid, sizeof(self->instance_uuid));
	} else
	    self->tx.set_type(pb::MT_STP_UPDATE);

	// the serial enables detection of lost updates
	// for a client to recover from a lost update:
	// unsubscribe + re-subscribe which will cause
	// a full state dump to be sent
	self->tx.set_serial(grp->serial++);
	break;

    case REPORT_SIGNAL: // per-reported-signal action
	signal = self->tx.add_signal();
	vp = (hal_data_u *) SHMPTR(sig->data_ptr);
	switch (sig->type) {
	default:
	    assert("invalid signal type" == NULL);
	case HAL_BIT:
	    signal->set_halbit(vp->b);
	    break;
	case HAL_FLOAT:
	    signal->set_halfloat(vp->f);
	    break;
	case HAL_S32:
	    signal->set_hals32(vp->s);
	    break;
	case HAL_U32:
	    signal->set_halu32(vp->u);
	    break;
	}
	if (grp->flags & GROUP_REPORT_FULL) {
	    signal->set_name(sig->name);
	    signal->set_type((pb::ValueType)sig->type);
	}
	signal->set_handle(sig->handle);
	break;

    case REPORT_END: // finalize & send
	msg = zmsg_new();
	zmsg_pushstr(msg, cgroup->group->name);
	zframe_t *update_frame = zframe_new(NULL, self->tx.ByteSize());
	self->tx.SerializeWithCachedSizesToArray(zframe_data(update_frame));
	zmsg_add(msg, update_frame);
	retval = zmsg_send (&msg, self->z_status);
	assert(retval == 0);
	assert(msg == NULL);
	grp->flags &= ~GROUP_REPORT_FULL;

#if JSON_TIMING
	// timing test:
	try {
	    std::string json = pb2json(self->tx);
	    zframe_t *z_jsonframe = zframe_new( json.c_str(), json.size());
	    //assert(zframe_send(&z_jsonframe, self->z_status, 0) == 0);
	    zframe_destroy(&z_jsonframe);
	} catch (std::exception &ex) {
	    rtapi_print_msg(RTAPI_MSG_ERR, "to_ws: pb2json exception: %s\n",
		     ex.what());
	    // std::string text;
	    // if (TextFormat::PrintToString(c, &text))
	    // 	fprintf(stderr, "container: %s\n", text.c_str());
	}
#endif // JSON_TIMING

	self->tx.Clear();
	break;
    }
    return 0;
}

static int
handle_group_timer(zloop_t *loop, int timer_id, void *arg)
{
    group_t *g = (group_t *) arg;

    if (hal_cgroup_match(g->cg) ||  (g->flags & GROUP_REPORT_FULL)) {
	hal_cgroup_report(g->cg, group_report_cb, g,
			  (g->flags & GROUP_REPORT_FULL));
    }
    return 0;
}

int comp_report_cb(int phase,  hal_compiled_comp_t *cc,
		   hal_pin_t *pin,
		   int handle,
		   hal_data_u *vp,
		   void *cb_data)
{
    rcomp_t *rc = (rcomp_t *) cb_data;
    htself_t *self =  rc->self;
    pb::Pin *p;
    zmsg_t *msg;
    int retval;

    switch (phase) {

    case REPORT_BEGIN:	// report initialisation
	// this enables a new subscriber to easily detect she's receiving
	// a full status snapshot, not just a change tracking update
	if (rc->flags & RCOMP_REPORT_FULL) {
	    self->tx.set_type(pb::MT_HALRCOMP_STATUS);
	    // enable clients to detect a restart
	    self->tx.set_uuid(self->instance_uuid, sizeof(self->instance_uuid));
	} else
	    self->tx.set_type(pb::MT_HALRCOMP_PIN_CHANGE);

	self->tx.set_serial(rc->serial++);
	break;

    case REPORT_PIN: // per-reported-pin action
	p = self->tx.add_pin();
	switch (pin->type) {
	default:
	    assert("invalid signal type" == NULL);
	case HAL_BIT:
	    p->set_halbit(vp->b);
	    break;
	case HAL_FLOAT:
	    p->set_halfloat(vp->f);
	    break;
	case HAL_S32:
	    p->set_hals32(vp->s);
	    break;
	case HAL_U32:
	    p->set_halu32(vp->u);
	    break;
	}
	if (rc->flags & RCOMP_REPORT_FULL) {
	    p->set_name(pin->name);
	    p->set_type((pb::ValueType)pin->type);
	    p->set_linked(pin->signal != 0);
	}
	p->set_handle(pin->handle);
	break;

    case REPORT_END: // finalize & send
	msg = zmsg_new();
	zmsg_pushstr(msg, cc->comp->name);
	zframe_t *update_frame = zframe_new(NULL, self->tx.ByteSize());
	self->tx.SerializeWithCachedSizesToArray(zframe_data(update_frame));
	zmsg_add(msg, update_frame);
	retval = zmsg_send (&msg, self->z_rcomp_status);
	assert(retval == 0);
	assert(msg == NULL);
	rc->flags &= ~GROUP_REPORT_FULL;
	self->tx.Clear();
	break;
    }
    return 0;
}

static int
handle_rcomp_timer(zloop_t *loop, int timer_id, void *arg)
{
    rcomp_t *rc = (rcomp_t *) arg;

    if (hal_ccomp_match(rc->cc) ||  (rc->flags & RCOMP_REPORT_FULL)) {
	hal_ccomp_report(rc->cc, comp_report_cb, rc,
			 (rc->flags & RCOMP_REPORT_FULL));
    }
    return 0;
}

static int
handle_rcomp_command(htself_t *self, zmsg_t *msg)
{
    int retval;
    zframe_t *reply_frame;
    size_t nframes = zmsg_size(msg);

    if (nframes != 2)
	return -1;
    char *cname = zmsg_popstr(msg);
    zframe_t *f = zmsg_pop(msg);

    if (!self->rx.ParseFromArray(zframe_data(f), zframe_size(f))) {
	char *s = zframe_strhex(f);
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rcomp %s command: cant parse %s",
			self->cfg->progname, cname, s);
	free(s);
	free(cname);
	return -1;
    }
    switch (self->rx.type()) {

    case pb::MT_PING:
	self->tx.set_type(pb::MT_PING_ACKNOWLEDGE);
	reply_frame = zframe_new(NULL, self->tx.ByteSize());
	self->tx.SerializeWithCachedSizesToArray(zframe_data(reply_frame));
	zstr_sendm (self->z_rcomp_status, cname);
	retval = zframe_send (&reply_frame, self->z_rcomp_status, 0);
	assert(retval == 0);
	self->tx.Clear();
	break;

    case pb::MT_HALRCOMP_BIND:

	// if (comp exists in rcomp dict) {
	//     if (!validate()) {
	// 	send  MT_HALRCOMP_BIND_REJECT;
	//     } else
	// 	send  MT_HALRCOMP_BIND_CONFIRM;
	// } else {
	//     create comp as per pinlist;
	//     ready the comp;
	//     compile it;
	//     add to self->rcomps[name];
	//     register timer in loop;
	// }
	break;

    case pb::MT_HALRCOMP_SET_PINS:
	// forall (pins) {
	//     lookup pin in handle2pin dict;
	//     set value;
	//     on failure {
	// 	send  MT_HALRCOMP_SET_PINS_REJECT;
	//     }
	// }
	// extension: reply if rsvp is set
	break;

    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rcomp %s command: unhandled type %d",
			self->cfg->progname, cname, (int) self->rx.type());
    }
    zframe_destroy(&f);
    free(cname);
    return 0;
}

static int
handle_rcomp(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *) arg;
    int retval;
    zmsg_t *msg = zmsg_recv(poller->socket);
    size_t nframes = zmsg_size( msg);

    if (nframes == 1) {
	// likely a subscribe/unsubscribe message

	zframe_t *f = zmsg_first(msg); // leaves message intact
	char *data = (char *) zframe_data(f);
	assert(data);
	char *topic = data + 1;

	switch (*data) {

	case '\001':
	    if (self->rcomps.count(topic) == 0) {
		rtapi_print_msg(RTAPI_MSG_ERR, "%s: subscribe - no comp '%s'",
				    self->cfg->progname, topic);

		// not found, publish an error message on this topic
		self->tx.set_type(pb::MT_HALRCOMP_SUBSCRIBE_ERROR);
		std::string error = "component " + std::string(topic) + " does not exist";
		self->tx.set_note(error);
		zframe_t *reply_frame = zframe_new(NULL, self->tx.ByteSize());
		self->tx.SerializeWithCachedSizesToArray(zframe_data(reply_frame));

		zstr_sendm (self->z_rcomp_status, topic);
		retval = zframe_send (&reply_frame, self->z_rcomp_status, 0);
		assert(retval == 0);
		self->tx.Clear();
	    } else {
		// compiled component found, schedule a full update
		rcomp_t *g = self->rcomps[topic];
		g->flags |= RCOMP_REPORT_FULL;

		// first subscriber - activate scanning
		if (g->timer_id < 0) { // not scanning
		    g->timer_id = zloop_timer(self->z_loop, g->msec,
					      0, handle_rcomp_timer, (void *)g);
		    assert(g->timer_id > -1);
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: comp %s scanning, tid=%d %d mS",
				    self->cfg->progname, topic, g->timer_id, g->msec);
		}

		if (g->cc->comp->state == COMP_UNBOUND) {
		    // once only by first subscriber
		    hal_bind(topic);
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s bound, serial=%d",
				    self->cfg->progname, topic, g->serial);
		} else
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s subscribed, serial=%d",
				    self->cfg->progname, topic, g->serial);
	    }
	    break;

	case '\000':
	    // last subscriber went away - unbind the component
	    if (self->rcomps.count(topic) > 0) {
		rcomp_t *g = self->rcomps[topic];

		// stop the scanning timer
		if (g->timer_id > -1) {  // currently scanning
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: comp %s stop scanning, tid=%d",
				    self->cfg->progname, topic, g->timer_id);
		    retval = zloop_timer_end (self->z_loop, g->timer_id);
		    assert(retval == 0);
		    g->timer_id = -1;
		}
		hal_unbind(topic);
		rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s unbound",
				self->cfg->progname, topic);
	    }
	    break;

	default:
	    handle_rcomp_command(self, msg);
	    zmsg_destroy(&msg);
	}
	return 0;
    } else {
	handle_rcomp_command(self, msg);
	zmsg_destroy(&msg);
    }
    return 0;
}

// monitor subscribe events:
//
// a new subscriber will cause the next update to be 'full', i.e. with current
// values and including signal names regardless of any change respective to the last scan
//
// this permits a new subscriber to establish the set of signal names immediately as
// well as retrieve all current values without constantly broadcasting all
// signal names
static int
handle_stp(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    htself_t *self = (htself_t *) arg;
    zframe_t *f_subscribe = zframe_recv(poller->socket);
    const char *s = (const char *) zframe_data(f_subscribe);

    if ((s == NULL) || ((*s != '\000') && (*s != '\001'))) {
	// some random message - ignore
	zframe_destroy(&f_subscribe);
	return 0;
    }
    const char *topic = s+1;

    switch (*s) {
    case '\001':   // non-zero: subscribe event
	if (strlen(topic) == 0) {
	    // this was a subscribe("") - all topics
	    // mark all groups as requiring a full report on next poll
	    for (groupmap_iterator gi = self->groups.begin();
		 gi != self->groups.end(); gi++) {

		group_t *g = gi->second;
		g->flags |= GROUP_REPORT_FULL;

		// if first subscriber: activate scanning
		if (g->timer_id < 0) { // not scanning
		    g->timer_id = zloop_timer(self->z_loop, g->msec,
					      0, handle_group_timer, (void *)g);
		    assert(g->timer_id > -1);
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: group %s scanning, tid=%d %d mS",
				    self->cfg->progname, topic, g->timer_id, g->msec);
		}
		rtapi_print_msg(RTAPI_MSG_DBG, "%s: wildcard subscribe group='%s' serial=%d",
				self->cfg->progname,
				gi->first.c_str(), gi->second->serial);
	    }
	} else {
	    // a selective subscribe
	    groupmap_iterator gi = self->groups.find(topic);
	    if (gi != self->groups.end()) {
		group_t *g = gi->second;
		g->flags |= GROUP_REPORT_FULL;
		rtapi_print_msg(RTAPI_MSG_DBG, "%s: subscribe group='%s' serial=%d",
				self->cfg->progname,
				gi->first.c_str(), gi->second->serial);

		// if first subscriber: activate scanning
		if (g->timer_id < 0) { // not scanning
		    g->timer_id = zloop_timer(self->z_loop, g->msec,
					      0, handle_group_timer, (void *)g);
		    assert(g->timer_id > -1);
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: group %s scanning, tid=%d %d mS",
				    self->cfg->progname, topic, g->timer_id, g->msec);
		}
	    } else {
		// non-existant topic, complain.
		std::string note = "no such group: " + std::string(topic)
		    + ", valid groups are: ";
		for (groupmap_iterator g = self->groups.begin();
		     g != self->groups.end(); g++) {
		    note += g->first;
		    note += " ";
		}
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: subscribe error: %s\n",
				self->cfg->progname, note.c_str());

		self->tx.set_type(pb::MT_STP_NOGROUP);
		self->tx.set_note(note);
		zmsg_t *msg = zmsg_new();
		zmsg_pushstr(msg, topic);
		zframe_t *update_frame = zframe_new(NULL, self->tx.ByteSize());
		self->tx.SerializeWithCachedSizesToArray(zframe_data(update_frame));
		zmsg_add(msg, update_frame);
		int retval = zmsg_send (&msg, self->z_status);
		assert(retval == 0);
		assert(msg == NULL);
		self->tx.Clear();
	    }
	}
	break;

    case '\000':   // last unsubscribe
	if (self->groups.count(topic) > 0) {
	    group_t *g = self->groups[topic];
	    // stop the scanning timer
	    if (g->timer_id > -1) {  // currently scanning
		rtapi_print_msg(RTAPI_MSG_DBG, "%s: group %s stop scanning, tid=%d",
				self->cfg->progname, topic, g->timer_id);
		int retval = zloop_timer_end (self->z_loop, g->timer_id);
		assert(retval == 0);
		g->timer_id = -1;
	    }
	}
	break;

    default:
	break;
    }
    zframe_destroy(&f_subscribe);
    return 0;
}


static int
collect_unbound_comps(hal_compstate_t *cs,  void *cb_data)
{
    htself_t *self = (htself_t *) cb_data;;

    if ((cs->type == TYPE_REMOTE) &&
	(cs->pid == 0) &&
	(cs->state == COMP_UNBOUND)) {
	self->rcomps[cs->name] = NULL;
	rtapi_print_msg(RTAPI_MSG_DBG, "%s: found unbound remote comp '%s'",
			self->cfg->progname, cs->name);
    }
    return 0;
}

static int
prepare_comps(htself_t *self)
{
    int retval;
    int nfail = 0;

    // this needs to be done in two steps due to HAL locking:
    // 1. collect remote component names and populate dict keys
    // 2. acquire and compile remote components
    hal_retrieve_compstate(NULL, collect_unbound_comps, self);

    for (compmap_iterator c = self->rcomps.begin();
	 c != self->rcomps.end(); c++) {

	const char *name = c->first.c_str();
	if ((retval = hal_acquire(name, getpid())) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_acquire(%s) failed: %s",
			    self->cfg->progname,
			    name, strerror(-retval));
	    nfail++;
	    continue;
	}
	rtapi_print_msg(RTAPI_MSG_DBG, "%s: acquired '%s'",
			self->cfg->progname, name);

	hal_compiled_comp_t *cc;
	if ((retval = hal_compile_comp(name, &cc))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_compile_comp(%s) failed - skipping component: %s",
			    self->cfg->progname,
			    name, strerror(-retval));
	    nfail++;
	    self->rcomps.erase(c);
	    continue;
	}

	int arg1, arg2;
	hal_ccomp_args(cc, &arg1, &arg2);
	int msec = arg1 ? arg1 : self->cfg->default_rcomp_timer;

	rcomp_t *rc = new rcomp_t();
	rc->flags = 0;
	rc->self = self;
	rc->cc = cc;
	rc->serial = 0;
	rc->msec = msec;
	rc->timer_id = -1; // invalid

	self->rcomps[name] = rc; // all prepared, timer not yet started

	rtapi_print_msg(RTAPI_MSG_DBG, "%s: component '%s' - using %d mS poll interval",
			self->cfg->progname, name, msec);
    }
    return nfail;
}

static int
prepare_groups(htself_t *self)
{
    size_t msec;
    for (groupmap_iterator g = self->groups.begin();
	 g != self->groups.end(); g++) {
	group_t *grp = g->second;
	msec =  hal_cgroup_timer(grp->cg);
	if (msec == 0)
	    msec = self->cfg->default_group_timer;

	grp->msec = msec;
	grp->timer_id = -1; // not yet scanning

	rtapi_print_msg(RTAPI_MSG_DBG, "%s: group '%s' - using %d mS poll interval",
			self->cfg->progname, g->first.c_str(), msec);

	//	zloop_timer(self->z_loop, msec, 0, handle_group_timer, (void *)grp);
    }
    return 0;
}

static int
mainloop( htself_t *self)
{
    int retval;

    zmq_pollitem_t signal_poller =     { 0, self->signal_fd, ZMQ_POLLIN };
    zmq_pollitem_t stp_poller =  { self->z_status, 0, ZMQ_POLLIN };
    zmq_pollitem_t rcomp_poller =  { self->z_rcomp_status, 0, ZMQ_POLLIN };

    self->z_loop = zloop_new();
    assert (self->z_loop);

    zloop_set_verbose (self->z_loop, self->cfg->debug);
    zloop_poller(self->z_loop, &signal_poller, handle_signal, self);
    zloop_poller(self->z_loop, &stp_poller, handle_stp, self);
    zloop_poller(self->z_loop, &rcomp_poller, handle_rcomp, self);

    prepare_groups(self);

    if ((retval = prepare_comps(self))) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: %d remote components failed to initialize",
			self->cfg->progname, retval);
	return retval;
    }
    do {
	retval = zloop_start(self->z_loop);
    } while  (!(retval || self->interrupted));

    rtapi_print_msg(RTAPI_MSG_INFO,
		    "%s: exiting mainloop (%s)\n",
		    self->cfg->progname,
		    self->interrupted ? "interrupted": "reactor exited");

    return 0;
}

static int
zmq_init(htself_t *self)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    sigset_t sigmask;
    int retval;

    sigemptyset(&sigmask);

    // block all signal delivery through default signal handlers
    // since we're using signalfd()
    sigfillset(&sigmask);
    retval = sigprocmask(SIG_SETMASK, &sigmask, NULL);
    assert(retval == 0);

    // explicitly enable signals we want delivered via signalfd()
    retval = sigemptyset(&sigmask); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGINT);  assert(retval == 0);
    retval = sigaddset(&sigmask, SIGQUIT); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGTERM); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGSEGV); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGFPE);  assert(retval == 0);

    if ((self->signal_fd = signalfd(-1, &sigmask, 0)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: signaldfd() failed: %s\n",
			self->cfg->progname,  strerror(errno));
	return -1;
    }

    // suppress default handling of signals in zctx_new()
    // since we're using signalfd()
    // must happen before zctx_new()
    zsys_handler_set(NULL);

    self->z_context = zctx_new ();
    assert(self->z_context);

    self->z_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_status);
    zsocket_set_linger (self->z_status, 0);
    zsocket_set_xpub_verbose (self->z_status, 1);
    int rc = zsocket_bind(self->z_status, self->cfg->status);
    assert (rc != 0);
    self->z_status_dsn = zsocket_last_endpoint (self->z_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking STP on '%s'",
		    conf.progname, self->z_status_dsn);

    self->z_rcomp_status = zsocket_new (self->z_context, ZMQ_XPUB);
    assert(self->z_rcomp_status);
    zsocket_set_linger (self->z_rcomp_status, 0);
    zsocket_set_xpub_verbose (self->z_rcomp_status, 1);
    rc = zsocket_bind(self->z_rcomp_status, self->cfg->rcomp_status);
    assert (rc != 0);
    self->z_rcomp_status_dsn = zsocket_last_endpoint (self->z_rcomp_status);

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: talking HALRcomp on '%s'",
		    conf.progname, self->z_rcomp_status_dsn);

    usleep(200 *1000); // avoid slow joiner syndrome

    return 0;
}

static int
service_discovery_init(htself_t *self)
{
    int retval;

    if (!self->cfg->sd_port)
	return 0;  // service discovery disabled

    // start the service announcement responder
    self->sd_publisher = sp_new(self->z_context, self->cfg->sd_port,
				rtapi_instance);

    assert(self->sd_publisher != NULL);
    sp_log(self->sd_publisher, self->cfg->sddebug);
    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_STP_HALGROUP, //type
		    STP_VERSION, // version
		    NULL, // ip
		    0, // port
		    self->z_status_dsn, // uri
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "HAL group STP");  // descr
    assert(retval == 0);
    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_HAL_RCOMP, //type
		    HAL_RCOMP_VERSION, // version
		    NULL, // ip
		    0, // port
		    self->z_rcomp_status_dsn, // uri
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "HAL RComp");  // descr
    assert(retval == 0);


    retval = sp_start(self->sd_publisher);
    assert(retval == 0);
    return 0;
}

static int
group_cb(hal_group_t *g, void *cb_data)
{
    htself_t *self = (htself_t *)cb_data;
    hal_compiled_group_t *cgroup;
    int retval;

    if ((retval = halpr_group_compile(g->name, &cgroup))) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"hal_group_compile(%s) failed: %d - skipping group\n",
			g->name, retval);
	return 0;
    }
    group_t *grp = new group_t();
    grp->cg = cgroup;
    grp->serial = 0;
    grp->self = self;
    grp->flags = 0;
    self->groups[g->name] = grp;
    return 0;
}

static int
hal_setup(htself_t *self)
{
    if ((self->comp_id = hal_init(self->cfg->modname)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init(%s) failed: HAL error code=%d\n",
			self->cfg->progname, self->cfg->modname, self->comp_id);
	return -1;
    }
    hal_ready(self->comp_id);

    int major, minor, patch;
    zmq_version (&major, &minor, &patch);

    char buf[40];
    uuid_unparse(self->instance_uuid, buf);

    rtapi_print_msg(RTAPI_MSG_DBG,
		    "%s: startup ØMQ=%d.%d.%d czmq=%d.%d.%d protobuf=%d.%d.%d uuid=%s\n",
		    conf.progname, major, minor, patch,
		    CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH,
		    GOOGLE_PROTOBUF_VERSION / 1000000,
		    (GOOGLE_PROTOBUF_VERSION / 1000) % 1000,
		    GOOGLE_PROTOBUF_VERSION % 1000,
		    buf);

    {   // scoped lock
	int retval __attribute__((cleanup(halpr_autorelease_mutex)));
	rtapi_mutex_get(&(hal_data->mutex));

	// run through all groups, execute a callback for each group found.
	if ((retval = halpr_foreach_group(NULL, group_cb, self)) < 0)
	    return retval;
	rtapi_print_msg(RTAPI_MSG_DBG,"found %d group(s)\n", retval);
    }
    return 0;
}

static int
hal_cleanup(htself_t *self)
{
    int retval;
    pid_t mypid = getpid();

    // release rcomps
    for (compmap_iterator c = self->rcomps.begin();
	 c != self->rcomps.end(); c++) {

	const char *name = c->first.c_str();
	rcomp_t *rc = c->second;
	hal_comp_t *comp = rc->cc->comp;

	// unbind all comps owned by us:
	if (comp->state == COMP_BOUND) {
	    if (comp->pid == mypid) {
		retval = hal_unbind(name);
		if (retval < 0)
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "%s: hal_unbind(%s) failed: %s",
				    self->cfg->progname,
				    name, strerror(-retval));
		else
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "%s: unbound component '%s'",
				    self->cfg->progname, name);
	    } else {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"%s: BUG - comp %s bound but not by haltalk: %d/%d",
				self->cfg->progname, name, mypid, comp->pid);

	    }
	}
	int retval = hal_release(name);
	if (retval < 0)
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_release(%s) failed: %s",
			    self->cfg->progname,
			    name, strerror(-retval));
	else
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: released component '%s'",
			    self->cfg->progname, name);
    }

    // drop group refcount on exit
    for (groupmap_iterator g = self->groups.begin(); g != self->groups.end(); g++) {
	hal_unref_group(g->first.c_str());
	rtapi_print_msg(RTAPI_MSG_DBG,
			"%s: unreferencing group '%s'",
			self->cfg->progname, g->first.c_str());
    }
    if (self->comp_id)
	hal_exit(self->comp_id);
    return 0;
}

static int
read_config(htconf_t *conf)
{
    const char *s;
    FILE *inifp;

    if (!conf->inifile)
	return 0; // use compiled-in defaults

    if ((inifp = fopen(conf->inifile,"r")) == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: cant open inifile '%s'\n",
		conf->progname, conf->inifile);
	return -1;
    }
    if ((s = iniFind(inifp, "GROUP_STATUS_URI", conf->section)))
	conf->status = strdup(s);
    if ((s = iniFind(inifp, "RCOMP_STATUS_URI", conf->section)))
	conf->rcomp_status = strdup(s);

    iniFindInt(inifp, "GROUPTIMER", conf->section, &conf->default_group_timer);
    iniFindInt(inifp, "RCOMPTIMER", conf->section, &conf->default_rcomp_timer);
    iniFindInt(inifp, "DEBUG", conf->section, &conf->debug);
    iniFindInt(inifp, "SDDEBUG", conf->section, &conf->sddebug);
    iniFindInt(inifp, "SDPORT", conf->section, &conf->sd_port);
    fclose(inifp);
    return 0;
}

static void
usage(void)
{
    printf("Usage:  haltalk [options]\n");
    printf("This is a userspace HAL program, typically loaded "
	   "using the halcmd \"loadusr\" command:\n"
	   "    loadusr haltalk [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment"
	   " variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'HALTALK')\n"
	   "-u or --uri <uri>\n"
	   "    zeroMQ URI for status reporting socket\n"
	   "-m or --rtapi-msg-level <level>\n"
	   "    set the RTAPI message level.\n"
	   "-t or --timer <msec>\n"
	   "    set the default group scan timer (100mS).\n"
	   "-d or --debug\n"
	   "    Turn on event debugging messages.\n");
}

int main (int argc, char *argv[])
{
    int opt, retval;

    conf.progname = argv[0];
    conf.inifile = getenv("INI_FILE_NAME");
    while ((opt = getopt_long(argc, argv, option_string,
			      long_options, NULL)) != -1) {
	switch(opt) {
	case 'd':
	    conf.debug = 1;
	    break;
	case 'D':
	    conf.sddebug = 1;
	    break;
	case 'S':
	    conf.section = optarg;
	    break;
	case 'I':
	    conf.inifile = optarg;
	    break;
	case 'u':
	    conf.status = optarg;
	    break;
	case 'r':
	    conf.rcomp_status = optarg;
	    break;
	case 't':
	    conf.default_group_timer = atoi(optarg);
	    break;
	case 'T':
	    conf.default_rcomp_timer = atoi(optarg);
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    conf.pid = getpid();
    conf.sd_port = SERVICE_DISCOVERY_PORT;

    // to_syslog("haltalk> ", &stdout); // redirect stdout to syslog
    // to_syslog("haltalk>> ", &stderr);  // redirect stderr to syslog

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;
    uuid_generate_time(self.instance_uuid);

    retval = hal_setup(&self);
    if (retval) exit(retval);

    retval = zmq_init(&self);
    if (retval) exit(retval);

    retval = service_discovery_init(&self);
    if (retval) exit(retval);

    mainloop(&self);

    // stop  the service announcement responder
    sp_destroy(&self.sd_publisher);

    // shutdown zmq context
    zctx_destroy(&self.z_context);

    hal_cleanup(&self);

    exit(0);
}
