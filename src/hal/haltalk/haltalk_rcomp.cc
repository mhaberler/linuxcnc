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

#include "haltalk.h"

static int collect_unbound_comps(hal_compstate_t *cs,  void *cb_data);
static int comp_report_cb(int phase,  hal_compiled_comp_t *cc,
			  hal_pin_t *pin,
			  int handle,
			  hal_data_u *vp,
			  void *cb_data);
static int handle_rcomp_command(htself_t *self, zmsg_t *msg);


// handle timer event for a rcomp
// report if one is due
int
handle_rcomp_timer(zloop_t *loop, int timer_id, void *arg)
{
    rcomp_t *rc = (rcomp_t *) arg;

    if (hal_ccomp_match(rc->cc) ||  (rc->flags & RCOMP_REPORT_FULL)) {
	hal_ccomp_report(rc->cc, comp_report_cb, rc,
			 (rc->flags & RCOMP_REPORT_FULL));
    }
    return 0;
}

// handle message input on the XPUB channel, these would be:
//    subscribe events (\001<topic>), for every subscribe
//    unsubscribe events (\001<topic>), for the last unsubscribe
//    other - any commands sent to the XPUB - dubious how useful this is
int
handle_rcomp_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
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

	    // scan for, and adopt any recently created rcomps
	    // so subscribe to them works (after haltalk was started)
	    scan_comps(self);

	    if (self->rcomps.count(topic) == 0) {
		rtapi_print_msg(RTAPI_MSG_ERR, "%s: subscribe - no comp '%s'",
				    self->cfg->progname, topic);

		// not found, publish an error message on this topic
		self->tx.set_type(pb::MT_HALRCOMP_ERROR);
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
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: start scanning comp %s, tid=%d %d mS",
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
		    rtapi_print_msg(RTAPI_MSG_DBG, "%s: stop scanning comp %s, tid=%d",
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


int
scan_comps(htself_t *self)
{
    int retval;
    int nfail = 0;

    // this needs to be done in two steps due to HAL locking:
    // 1. collect remote component names and populate dict keys
    // 2. acquire and compile remote components
    hal_retrieve_compstate(NULL, collect_unbound_comps, self);

    for (compmap_iterator c = self->rcomps.begin();
	 c != self->rcomps.end(); c++) {

	if (c->second != NULL) // already compiled
	    continue;

	const char *name = c->first.c_str();

	if ((retval = hal_acquire(name, self->pid)) < 0) {
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

int release_comps(htself_t *self)
{
    int nfail = 0, retval;

    for (compmap_iterator c = self->rcomps.begin();
	 c != self->rcomps.end(); c++) {

	const char *name = c->first.c_str();
	rcomp_t *rc = c->second;
	hal_comp_t *comp = rc->cc->comp;

	// unbind all comps owned by us:
	if (comp->state == COMP_BOUND) {
	    if (comp->pid == self->pid) {
		retval = hal_unbind(name);
		if (retval < 0) {
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "%s: hal_unbind(%s) failed: %s",
				    self->cfg->progname,
				    name, strerror(-retval));
		    nfail++;
		} else
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "%s: unbound component '%s'",
				    self->cfg->progname, name);
	    } else {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"%s: BUG - comp %s bound but not by haltalk: %d/%d",
				self->cfg->progname, name, self->pid, comp->pid);
		nfail++;
	    }
	}
	int retval = hal_release(name);
	if (retval < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_release(%s) failed: %s",
			    self->cfg->progname,
			    name, strerror(-retval));
	    nfail++;
	} else
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: released component '%s'",
			    self->cfg->progname, name);
    }
    return -nfail;
}

// ----- end of public functions ----

static int
collect_unbound_comps(hal_compstate_t *cs,  void *cb_data)
{
    htself_t *self = (htself_t *) cb_data;;

    // collect any unbound, un-aquired remote comps
    // which we dont know about yet
    if ((cs->type == TYPE_REMOTE) &&
	(cs->pid == 0) &&
	(cs->state == COMP_UNBOUND) &&
	(self->rcomps.count(cs->name) == 0)) {

	self->rcomps[cs->name] = NULL;

	rtapi_print_msg(RTAPI_MSG_DBG, "%s: found unbound remote comp '%s'",
			self->cfg->progname, cs->name);
    }
    return 0;
}


static
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
	    self->tx.set_type(pb::MT_HALRCOMP_FULL_UPDATE);
	    // enable clients to detect a restart
	    self->tx.set_uuid(self->instance_uuid, sizeof(self->instance_uuid));
	} else
	    self->tx.set_type(pb::MT_HALRCOMP_INCREMENTAL_UPDATE);

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
	zframe_destroy(&f);
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

    case pb::MT_HALRCOMMAND_SET:
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
