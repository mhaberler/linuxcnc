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

#include "haltalk.hh"
#include "halutil.hh"
#include "halpb.h"
#include "pbutil.hh"
#include "rtapi_hexdump.h"

#include <google/protobuf/text_format.h>

static int dispatch_request(htself_t *self, const char *from, void *socket);
static int process_rcmd_get(htself_t *self, const char *from, void *socket);
static int process_rcmd_set(htself_t *self, const char *from, void *socket);

int
handle_command_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    int retval = 0;

    htself_t *self = (htself_t *) arg;
    zmsg_t *msg = zmsg_recv(poller->socket);

    if (self->cfg->debug)
	zmsg_dump(msg);

    char *origin = zmsg_popstr (msg);
    size_t nframes = zmsg_size( msg);

    for(size_t i = 0; i < nframes; i++) {
	zframe_t *f = zmsg_pop (msg);

	if (!self->rx.ParseFromArray(zframe_data(f), zframe_size(f))) {
	    rtapi_print_hex_dump(RTAPI_MSG_ALL, RTAPI_DUMP_PREFIX_OFFSET,
				 16, 1, zframe_data(f), zframe_size(f), true,
				 "%s: invalid pb ", origin);
	} else {
	    // a valid protobuf. Interpret and reply as needed.
	    dispatch_request(self, origin, poller->socket);
	}
	zframe_destroy(&f);
    }
    free(origin);
    zmsg_destroy(&msg);
    return retval;
}

// ----- end of public functions ---

// hal mutex scope-locked version of halpr_find_pin_by_name()
hal_pin_t *
find_pin_by_name(const char *name)
{
    hal_pin_t *p __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));
    p = halpr_find_pin_by_name(name);
    return p;
}

static int
process_ping(htself_t *self, const char *from,  void *socket)
{
    self->tx.set_type( pb::MT_PING_ACKNOWLEDGE);
    self->tx.set_uuid(&self->instance_uuid, sizeof(uuid_t));
    return send_pbcontainer(from, self->tx, socket);
}

// validate name, number, type and direction of pins and params
// of the existing HAL component 'name' against the component described in
// pb::Component c.
// any errors are added as c.note strings.
// Returns the number of notes added (= errors).
// Acquires the HAL mutex.
int
validate_component(const char *name, const pb::Component *pbcomp, pb::Container &e)
{
    hal_pin_t *hp __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));

    hal_comp_t *hc = halpr_find_comp_by_name(name);
    if (hc == NULL) {
	note_printf(e, "HAL component '%s' does not exist", name);
	return e.note_size();
    }

    int npins = halpr_pin_count(name);
    int nparams  = halpr_param_count(name);
    int npbpins = pbcomp->pin_size();
    int npbparams = pbcomp->param_size();
    std::string s;

    if (!pbcomp->has_name())
	note_printf(e, "pb component has no name");

    if (npbpins != npins)
	note_printf(e, "pin count mismatch:pb comp=%d hal comp=%d",
		    npbpins, npins);

    if (npbparams != nparams)
	note_printf(e, "param count mismatch:pb comp=%d hal comp=%d",
		    npbparams, nparams);

    for (int i = 0; i < npbpins; i++) {

	const pb::Pin &p = pbcomp->pin().Get(i);;

	// basic syntax - required attributes
	if (!p.has_name()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "pin withtout name: %s", s.c_str());
	    continue;
	}
	if (!p.has_type()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "pin withtout type: %s", s.c_str());
	    continue;
	}
	if (!p.has_dir()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "pin withtout dir: %s", s.c_str());
	    continue;
	}

	// each pb pin must match an existing HAL pin
	hal_pin_t *hp = halpr_find_pin_by_name(p.name().c_str());
	if (hp == NULL) {
	    note_printf(e, "HAL pin '%s' does not exist", p.name().c_str());
	} else {
	    // HAL pin name exists, match attributes
	    if (hp->type != (hal_type_t) p.type())
		note_printf(e, "HAL pin '%s' type mismatch: hal=%d pb=%d",
			    hp->type, p.type());

	    if (hp->dir != (hal_pin_dir_t) p.dir())
		note_printf(e, "HAL pin '%s' direction mismatch: hal=%d pb=%d",
			    hp->dir, p.dir());
	}
    }
    // same for params:
    for (int i = 0; i < npbparams; i++) {

	const pb::Param &p = pbcomp->param().Get(i);;

	// basic syntax - required attributes
	if (!p.has_name()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "param withtout name: %s", s.c_str());
	    continue;
	}
	if (!p.has_type()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "param withtout type: %s", s.c_str());
	    continue;
	}
	if (!p.has_dir()) {
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(e, "param withtout direction: %s", s.c_str());
	    continue;
	}

	// each pb param must match an existing HAL param
	hal_param_t *hp = halpr_find_param_by_name(p.name().c_str());
	if (hp == NULL) {
	    note_printf(e, "HAL param '%s' does not exist", p.name().c_str());
	} else {
	    // HAL param name exists, match attributes
	    if (hp->type != (hal_type_t) p.type())
		note_printf(e, "HAL param '%s' type mismatch: hal=%d pb=%d",
			    hp->type, p.type());

	    if (hp->dir != (hal_param_dir_t) p.dir())
		note_printf(e, "HAL param '%s' direction mismatch: hal=%d pb=%d",
			    hp->dir, p.dir());
	}
    }
    // this matching on pb objects only will not explicitly
    // enumerate HAL pins and params which are not in the pb request,
    // but the balance mismatch will have already been recorded
    return e.note_size();
}

// create a remote comp as per MT_HALRCOMP_BIND request message contents
// The Component submessage is assumed to exist and carry all required fields.
// compile and return a handle to the rcomp descriptor
// the rcomp will be taken into service once its name is subscribed to.
// accumulate any errors in self->tx.note.
static rcomp_t *
create_rcomp(htself_t *self,  const pb::Component *pbcomp,
	     const char *from, void *socket)
{
    int arg1 = 0, arg2 = 0, retval;
    rcomp_t *rc = new rcomp_t();
    int comp_id = 0;
    halitem_t *hi = NULL;
    const char *cname = pbcomp->name().c_str();

    rc->self = self;
    rc->timer_id = -1;
    rc->serial = 0;
    rc->flags = 0;
    rc->cc = NULL;

    // extract timer and userargs if set
    if (pbcomp->has_timer())
	rc->msec = pbcomp->timer();
    else
	rc->msec = self->cfg->default_rcomp_timer;

    if (pbcomp->has_userarg1()) arg1 = pbcomp->userarg1();
    if (pbcomp->has_userarg2()) arg2 = pbcomp->userarg2();

    // create the remote component
    comp_id = hal_init_mode(cname, TYPE_REMOTE, arg1, arg2);
    if (comp_id < 0) {
	note_printf(self->tx, "hal_init_mode(%s): %s",
		    cname, strerror(-comp_id));
	goto ERROR;
    }

    // create the pins
    for (int i = 0; i < pbcomp->pin_size(); i++) {
	const pb::Pin &p = pbcomp->pin(i);
	hi = new halitem_t();

	if (hi == NULL) {
	    note_printf(self->tx, "new halitem_t() failed");
	    goto EXIT_COMP;
	}
	hi->ptr = hal_malloc(sizeof(void *));
	if (hi->ptr == NULL) {
	    note_printf(self->tx,"hal_malloc() failed");
	    goto EXIT_COMP;
	}
	hi->type = HAL_PIN;
	retval = hal_pin_new(p.name().c_str(),
			     (hal_type_t) p.type(),
			     (hal_pin_dir_t) p.dir(),
			     (void **) hi->ptr,
			     comp_id);
	if (retval < 0) {
	    note_printf(self->tx, "hal_pin_new() failed");
	    goto EXIT_COMP;
	}
	hi->o.pin = find_pin_by_name(p.name().c_str());
	if (hi->o.pin == NULL) {
	    note_printf(self->tx, "hal_find_pin_by_name() failed");
	    goto EXIT_COMP;
	}
	// add to items sparse array - needed for quick
	// lookup when updates by handle are received
	self->items[hi->o.pin->handle] = hi;
    }
    hal_ready(comp_id); // XXX check return value

    // compile the component
    hal_compiled_comp_t *cc;
    if ((retval = hal_compile_comp(cname, &cc))) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: create_rcomp:hal_compile_comp(%s)"
			" failed - skipping component: %s",
			self->cfg->progname,
			cname, strerror(-retval));
	note_printf(self->tx, "hal_compile_comp() failed");
	goto EXIT_COMP;
    }
    rc->cc = cc;
    return rc;

 EXIT_COMP:
    if (hi) delete hi;
    if (rc->cc)
	hal_ccomp_free(cc);
    if (rc) delete rc;
    if (comp_id > 0)
	hal_exit(comp_id);
 ERROR:
    return NULL;
}

static int
process_rcomp_bind(htself_t *self, const char *from,
		   const pb::Component *pbcomp, void *socket)
{
    int retval = 0;
    const char *cname = NULL;
    rcomp_t *rc;
    std::string s;

    // assume failure until proven otherwise
    self->tx.set_type( pb::MT_HALRCOMP_BIND_REJECT);
    self->tx.set_uuid(&self->instance_uuid, sizeof(uuid_t));

    // fail if comp.name not present
    if (!pbcomp->has_name()) {
	note_printf(self->tx, "request %d from %s: no name in Component submessage",
		    self->rx.type(), from ? from : "NULL");
	return send_pbcontainer(from, self->tx, socket);
    }
    cname = pbcomp->name().c_str();

    // validate pinlist attributes if pins are present -
    // to create a pin, it must have, name, type, direction
    for (int i = 0; i < pbcomp->pin_size(); i++) {
	const pb::Pin &p = pbcomp->pin(i);
	if (!(p.has_name() &&
	      p.has_type() &&
	      p.has_dir())) {

	    // TODO if (type < HAL_BIT || type > HAL_U32)
	    gpb::TextFormat::PrintToString(p, &s);
	    note_printf(self->tx,
			"request %d from %s: invalid pin - name, type or dir missing: Pin=(%s)",
			self->rx.type(), from ? from : "NULL", s.c_str());
	}
    }
    // reply if any bad news so far
    if (self->tx.note_size() > 0)
	return send_pbcontainer(from, self->tx, socket);

    // see if component already exists
    if (self->rcomps.count(cname) == 0) {
	// no, new component being created remotely
	// any errors accumulate in self->tx.note
	rc = create_rcomp(self, pbcomp, from, socket);
	if (rc) {
	    self->rcomps[cname] = rc;
	    // acquire and bind happens during subscribe
	}
    } else {
	// component exists
	rc = self->rcomps[cname];
	// validate request against existing comp
	retval = validate_component(cname, pbcomp, self->tx);
	if (retval) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: bind request from %s:"
			    " mismatch against existing HAL component",
			    self->cfg->progname, from);
	    return send_pbcontainer(from, self->tx, socket);
	}
    }
    // all good.
    if (rc) {
	// a valid component, either existing or new.

	pb::Component *c  __attribute__((cleanup(halpr_autorelease_mutex))) = self->tx.add_comp();
	rtapi_mutex_get(&(hal_data->mutex));
	hal_comp_t *comp = halpr_find_comp_by_name(cname);
	assert(comp != NULL);
	self->tx.set_type(pb::MT_HALRCOMP_BIND_CONFIRM);
	self->tx.set_uuid(&self->instance_uuid, sizeof(uuid_t));
	retval = halpr_describe_component(comp, c);
	assert(retval == 0);
    }
    return send_pbcontainer(from, self->tx, socket);
}

static int
dispatch_request(htself_t *self, const char *from,  void *socket)
{
    int retval = 0;

    switch (self->rx.type()) {

    case pb::MT_PING:
	retval = process_ping(self, from, socket);
	break;

    case pb::MT_HALRCOMP_BIND:
	// check for component submessages, and fail if none present
	if (self->rx.comp_size() == 0) {
	    note_printf(self->tx, "request %d from %s: no Component submessage",
			self->rx.type(), from ? from : "NULL");
	    return send_pbcontainer(from, self->tx, socket);
	}
	// bind them all
	for (int i = 0; i < self->rx.comp_size(); i++) {
	    const pb::Component *pbcomp = &self->rx.comp(i);
	    retval = process_rcomp_bind(self, from, pbcomp,  socket);
	}
	break;

    // HAL object set/get ops
    case pb::MT_HALRCOMMAND_SET:
	// pin
	// signal
	// param
	retval = process_rcmd_set(self, from, socket);
	break;

    case pb::MT_HALRCOMMAND_GET:
	// pin
	// signal
	// param
	retval = process_rcmd_get(self, from, socket);
	break;

    case pb::MT_HALRCOMMAND_DESCRIBE:

	retval = process_describe(self, from, socket);
	break;

	// NIY - fall through:
    case pb::MT_HALRCOMMAND_CREATE:
	// signal
	// member
	// group
    case pb::MT_HALRCOMMAND_DELETE:
    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rcommand from %s : unhandled type %d",
			self->cfg->progname, from, (int) self->rx.type());
	retval = -1;
    }
    return retval;
}

static int
process_rcmd_set(htself_t *self, const char *from,  void *socket)
{
    std::string s;

    // work the pins
    for (int i = 0; i < self->rx.pin_size(); i++) {
	const pb::Pin &p = self->rx.pin(i);

	// try fast path via item dict first
	if (p.has_handle() && p.has_type() &&
	    (p.has_halfloat() ||
	     p.has_halbit() ||
	     p.has_halu32() ||
	     p.has_hals32())) {

	    int handle = p.handle();
	    if (self->items.count(handle)) {
		// found via items dict
		halitem_t *hi = self->items[handle];
		if (hi->type != HAL_PIN) {
		     note_printf(self->tx,
				"handle type mismatch - not a pin: handle=%d type=%d",
				 handle, hi->type);
		     continue;
		}
		hal_pin_t *hp = hi->o.pin;
		assert(hp != NULL);

		if (hp->dir == HAL_IN) {
		    note_printf(self->tx,
				"cant write a HAL_IN pin: handle=%d name=%s",
				handle, hp->name);
		    continue;
		}
		if (hp->type != (hal_type_t) p.type()) {
		    note_printf(self->tx,
				"pin type mismatch: pb=%d/hal=%d, handle=%d name=%s",
				p.type(), hp->type, handle, hp->name);
		    continue;
		}
		// set value
		hal_data_u *vp = (hal_data_u *) hal_pin2u(hp);
		assert(vp != NULL);

		switch (hp->type) {
		default:
		    assert("invalid pin type" == NULL);
		    break;
		case HAL_BIT:
		    vp->b = p.halbit();
		    break;
		case HAL_FLOAT:
		    vp->f = p.halfloat();
		    break;
		case HAL_S32:
		    vp->s = p.hals32();
		    break;
		case HAL_U32:
		    vp->u = p.halu32();
		    break;
		}
		continue;
	    }
	    // record handle lookup failure
	    note_printf(self->tx, "no such handle: %d",handle);

	    continue;
	}
	// later: no handle given, try slow path via name, and add item.
	// reply with handle binding.
	// if (p.has_name() ) {
	// }
    }
    if (self->tx.note_size()) {
	self->tx.set_type(pb::MT_HALRCOMP_SET_REJECT);
	return send_pbcontainer(from, self->tx, socket);
    }

    // otherwise reply only if explicitly required:
    if (self->rx.has_reply_required() && self->rx.reply_required()) {
	self->tx.set_type(pb::MT_HALRCOMMAND_ACK);
	return send_pbcontainer(from, self->tx, socket);
    }
    return 0;
}

static int
process_rcmd_get(htself_t *self, const char *from,  void *socket)
{

    return 0;
}
