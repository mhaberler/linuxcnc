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
#include "halpb.h"
#include "pbutil.hh"
#include "rtapi_hexdump.h"

#include <google/protobuf/text_format.h>

static int dispatch_request(const char *from,  htself_t *self, void *socket);


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
	    dispatch_request(origin, self, poller->socket);
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
process_ping(const char *from,  htself_t *self, void *socket)
{
    self->tx.set_type( pb::MT_PING_ACKNOWLEDGE);
    self->tx.set_uuid(&self->instance_uuid, sizeof(uuid_t));
    return send_pbcontainer(from, self->tx, socket);
}


// transfrom a HAL component into a Component protobuf.
// Aquires the HAL mutex.
int hal_describe_component(const char *name, pb::Component *c)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));
    hal_comp_t *owner;
    hal_pin_t *pin;

    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_name(name);
    if (comp == 0)
	return -ENOENT;

    c->set_name(comp->name);
    c->set_comp_id(comp->comp_id);
    c->set_type(comp->type);
    c->set_state(comp->state);
    c->set_last_update(comp->last_update);
    c->set_last_bound(comp->last_bound);
    c->set_last_unbound(comp->last_unbound);
    c->set_pid(comp->pid);
    if (comp->insmod_args)
	c->set_args((const char *)SHMPTR(comp->insmod_args));
    c->set_userarg1(comp->userarg1);
    c->set_userarg2(comp->userarg2);

    int next = hal_data->pin_list_ptr;
    while (next != 0) {
	pin = (hal_pin_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(pin->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pb::Pin *p = c->add_pin();
	    p->set_type((pb::ValueType) pin->type);
	    p->set_dir((pb::HalPinDirection) pin->dir);
	    p->set_handle(pin->handle);
	    p->set_name(pin->name);
	    p->set_linked(pin->signal != 0);
	    assert(hal_pin2pb(pin, p) == 0);
#ifdef USE_PIN_USER_ATTRIBUTES
	    p->set_flags(pin->flags);
	    if (pin->type == HAL_FLOAT)
		p->set_epsilon(pin->epsilon);
#endif
	}
	next = pin->next_ptr;
    }
    next = hal_data->param_list_ptr;
    while (next != 0) {
	hal_param_t *param = (hal_param_t *)SHMPTR(next);
	owner = (hal_comp_t *) SHMPTR(param->owner_ptr);
	if (owner->comp_id == comp->comp_id) {
	    pb::Param *p = c->add_param();
	    p->set_name(param->name);
	    p->set_type((pb::ValueType) param->type);
	    p->set_pdir((pb::HalParamDirection) param->dir);
	    p->set_handle(param->handle);
	    assert(hal_param2pb(param, p) == 0);
	}
	next = param->next_ptr;
    }
    return 0;
}


// create a remote comp as per MT_HALRCOMP_BIND request message contents
// compile and return a handle to the rcomp descriptor
// the rcomp will be taken into service once its name is subscribed to.
static rcomp_t *
create_rcomp(htself_t *self, const char *from, const char *cname, void *socket)
{
    int arg1 = 0, arg2 = 0, retval;
    rcomp_t *rc = new rcomp_t();
    int comp_id = 0;
    char text[100];
    halitem_t *hi = NULL;
    std::string errmsg;
    pb::Container err;

    rc->self = self;
    rc->timer_id = -1;
    rc->serial = 0;
    rc->flags = 0;
    rc->cc = NULL;

    // extract timer and userargs if set
    if (self->rx.comp().has_timer())
	rc->msec = self->rx.comp().timer();
    else
	rc->msec = self->cfg->default_rcomp_timer;

    if (self->rx.comp().has_userarg1()) arg1 = self->rx.comp().userarg1();
    if (self->rx.comp().has_userarg2()) arg2 = self->rx.comp().userarg2();

    // create the remote component
    comp_id = hal_init_mode(cname, TYPE_REMOTE, arg1, arg2);
    if (comp_id < 0) {
	snprintf(text, sizeof(text),"hal_init_mode(%s): %s",
		 cname, strerror(-comp_id));
	err.add_note(text);
	goto ERROR_REPLY;
    }

    // create the pins
    for (int i = 0; i < self->rx.comp().pin_size(); i++) {
	const pb::Pin &p = self->rx.comp().pin(i);
	hi = new halitem_t();

	if (hi == NULL) {
	    err.add_note("new halitem_t() failed");
	    goto EXIT_COMP;
	}
	hi->ptr =  hal_malloc(sizeof(void *));
	if (hi->ptr == NULL) {
	    err.add_note("hal_malloc() failed");
	    goto EXIT_COMP;
	}
	hi->type = HAL_PIN;
	retval = hal_pin_new(p.name().c_str(),
			     (hal_type_t) p.type(),
			     (hal_pin_dir_t) p.dir(),
			     (void **) hi->ptr,
			     comp_id);
	if (retval < 0) {
	    err.add_note("hal_pin_new() failed");
	    goto EXIT_COMP;
	}
	hi->o.pin = find_pin_by_name(p.name().c_str());
	if (hi->o.pin == NULL) {
	    err.add_note("hal_find_pin_by_name() failed");
	    goto EXIT_COMP;
	}
	// add to items sparse array
	self->items[hi->o.pin->handle] = hi;
    }
    hal_ready(comp_id);

    // compile the component
    hal_compiled_comp_t *cc;
    if ((retval = hal_compile_comp(cname, &cc))) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: create_rcomp:hal_compile_comp(%s)"
			" failed - skipping component: %s",
			self->cfg->progname,
			cname, strerror(-retval));
	err.add_note("hal_compile_comp() failed");
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
 ERROR_REPLY:
    err.set_type( pb::MT_HALRCOMP_ERROR);
    err.set_uuid(&self->instance_uuid, sizeof(uuid_t));
    err.add_note(errmsg);
    send_pbcontainer(from, err, socket);
    return NULL;
}

static int
process_rcomp_bind(const char *from,  htself_t *self, void *socket)
{
    int retval = 0;
    char text[100];
    const char *cname = NULL;
    rcomp_t *rc;

    // once - in case we need it later
    pb::Container err;
    err.set_type( pb::MT_HALRCOMP_ERROR);
    err.set_uuid(&self->instance_uuid, sizeof(uuid_t));

    // extract component name
    // fail if comp not present
    if (!self->rx.has_comp()) {

	snprintf(text, sizeof(text),
		 "request %d from %s: no Component submessage",
		 self->rx.type(), from ? from : "NULL");
	err.add_note(text);
	return send_pbcontainer(from, err, socket);
    }
    // fail if comp.name not present
    if (!self->rx.comp().has_name()) {
	snprintf(text,sizeof(text),"request %d from %s: no name in Component submessage",
		 self->rx.type(), from ? from : "NULL");
	err.add_note(text);
	return send_pbcontainer(from, err, socket);
    }
    cname = self->rx.comp().name().c_str();

    // validate pinlist attributes if pins are present -
    // to create a pin, it must have, name, type, direction
    for (int i = 0; i < self->rx.comp().pin_size(); i++) {
	const pb::Pin &p = self->rx.comp().pin(i);
	if (!(p.has_name() && p.has_type() && p.has_dir())) {
	    // TODO if (type < HAL_BIT || type > HAL_U32)
	    std::string s;
	    gpb::TextFormat::PrintToString(p, &s);
	    std::string errormsg = "request from " + std::string(cname) + ": invalid pin: " + s;
	    err.add_note(errormsg);
	}
    }
    // reply if any bad news
    if (err.note_size() > 0)
	return send_pbcontainer(from, err, socket);

    // see if component already exists
    if (self->rcomps.count(cname) == 0) {
	// no, new component being created remotely
	rc = create_rcomp(self, from, cname, socket);
	if (rc) {
	    self->rcomps[cname] = rc;
	    // acquire and bind happens during subscribe
	}
    } else {
	// component exists
	rc = self->rcomps[cname];
	// TBD: validate request against existing comp
    }

    if (rc) {
	// a valid component, either existing or new.
	self->tx.set_type(pb::MT_HALRCOMP_BIND_CONFIRM);
	self->tx.set_uuid(&self->instance_uuid, sizeof(uuid_t));
	pb::Component *c = self->tx.mutable_comp();
	retval = hal_describe_component(cname, c);
	assert(retval == 0);
	return send_pbcontainer(from, self->tx, socket);

    } else {
	return send_pbcontainer(from, err, socket);
    }
}

    // def validate(self, comp, pins):
    //     ''' validate pins of an existing remote component against
    //     requested pinlist '''

    //     np_exist = len(comp.pins())
    //     np_requested = len(pins)
    //     if np_exist != np_requested:
    //         raise ValidateError, "pin count mismatch: requested=%d have=%d" % (np_exist,np_requested)
    //     pd = pindict(comp)
    //     for p in pins:
    //         if not pd.has_key(str(p.name)):
    //             raise ValidateError, "pin " + p.name + "does not exist"

    //         pin = pd[str(p.name)]
    //         if p.type != pin.type:
    //             raise ValidateError, "pin %s type mismatch: %d/%d" % (p.name, p.type,pin.type)
    //         if p.dir != pin.dir:
    //             raise ValidateError, "pin %s direction mismatch: %d/%d" % (p.name, p.dir,pin.dir)
    //     # all is well

static int
dispatch_request(const char *from,  htself_t *self, void *socket)
{
    int retval = 0;

    switch (self->rx.type()) {

    case pb::MT_PING:
	retval = process_ping(from, self, socket);
	break;

    case pb::MT_HALRCOMP_BIND:
	retval = process_rcomp_bind(from, self, socket);
	break;

    // HAL object set/get ops
    case pb::MT_HALRCOMMAND_SET:
	// pin
	// signal
	// param
	break;

    case pb::MT_HALRCOMMAND_GET:
	// pin
	// signal
	// param
	break;

    case pb::MT_HALRCOMMAND_CREATE:
	// signal
	// param
	break;

    case pb::MT_HALRCOMMAND_DELETE:
	break;

    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rcommand from %s : unhandled type %d",
			self->cfg->progname, from, (int) self->rx.type());
	retval = -1;
    }
    return retval;
}
