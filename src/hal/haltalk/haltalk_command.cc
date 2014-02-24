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
#include "pbutil.hh"
#include "rtapi_hexdump.h"

#include <google/protobuf/text_format.h>
using namespace google::protobuf;

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
    return send_reply(from, self->tx, pb::MT_PING_ACKNOWLEDGE, socket,
		      NULL, &self->instance_uuid);
}

static rcomp_t *
create_rcomp(htself_t *self, const char *from, const char *cname, void *socket)
{
    int arg1 = 0, arg2 = 0, retval;
    rcomp_t *rc = new rcomp_t();
    int comp_id;
    char text[100];
    halitem_t *hi;

    rc->timer_id = -1;

    // extract scan timer
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

	send_reply(from, self->tx, pb::MT_HALRCOMP_ERROR, socket,
		   text, &self->instance_uuid);
	return NULL;
    }

    // create the pins
    for (int i = 0; i < self->rx.comp().pin_size(); i++) {
	const pb::Pin &p = self->rx.comp().pin(i);
	hi = new halitem_t();
	assert(hi != NULL);
	hi->ptr =  hal_malloc(sizeof(void *));
	assert(hi->ptr != NULL);
	hi->type = HAL_PIN;
	const char *pname = p.name().c_str();
	retval = hal_pin_new(pname,
			     (hal_type_t) p.type(),
			     (hal_pin_dir_t) p.dir(),
			     (void **) hi->ptr,
			     comp_id);
	// FIXME fail on retval < 0
	hi->o.pin = find_pin_by_name(pname);
	assert(hi->o.pin != NULL);

	// extract handle, insert into handlemap
	// (*items)[name] = pinitem;
	// hi->hal_type = p.type();
	// hi->dir.pin_dir = p.dir();

    }

    // halitem pinitem;

    // if (type < HAL_BIT || type > HAL_U32) {
    // 	PyErr_Format(PyExc_RuntimeError,
    // 		     "Invalid pin type %d", type);
    // 	throw boost::python::error_already_set();
    // }

    // pinitem.is_pin = true;
    // pinitem.ptr =  hal_malloc(sizeof(void *));
    // if (!pinitem.ptr)
    // 	throw std::runtime_error("hal_malloc failed");

    // result = hal_pin_new(name, (hal_type_t) type, (hal_pin_dir_t) dir,
    // 			 (void **) pinitem.ptr, comp->comp_id);
    // if (result < 0) {
    // 	PyErr_Format(PyExc_RuntimeError,
    // 		     "hal_pin_new(%s, %d) failed: %s",
    // 		     name, type, strerror(-result));
    // 	throw boost::python::error_already_set();
    // }

    // pinitem.pp.pin = halpr_find_pin_by_name(name);
    // assert(pinitem.pp.pin != NULL);
    // (*items)[name] = pinitem;

                // for s in self.rx.pin:
                //    rcomp.newpin(str(s.name), s.type, s.dir)
                // rcomp.ready()
                // rcomp.acquire()
                // rcomp.bind()
                // print >> self.rtapi, "%s created remote comp: %s" % (client,name)
    // compile the component
             //    rcomp.ready()
             //    rcomp.acquire()
             //    rcomp.bind()
             //    print >> self.rtapi, "%s created remote comp: %s" % (client,name)

             //    # add to in-service dict
             //    self.rcomp[name] = rcomp
             //    self.tx.type = MT_HALRCOMP_BIND_CONFIRM
             // except Exception,s:
             //    print >> self.rtapi, "%s: %s create failed: %s" % (client,str(self.rx.comp.name), str(s))
             //    self.tx.type = MT_HALRCOMP_BIND_REJECT
             //    self.tx.note = str(s)

    return rc;
}

static int
process_rcomp_bind(const char *from,  htself_t *self, void *socket)
{
    int retval = 0;
    zframe_t *reply_frame;
    char text[100];
    const char *cname = NULL;
    rcomp_t *rc;

    // extract component name
    // fail if comp not present
    if (!self->rx.has_comp()) {
	snprintf(text,sizeof(text),"request %d from %s: no Component submessage",
		 self->rx.type(), from ? from : "NULL");
	return send_reply(from, self->tx, pb::MT_HALRCOMP_ERROR,
			  socket, text);
    }
    // fail if comp.name not present
    if (self->rx.comp().has_name()) {
	snprintf(text,sizeof(text),"request %d from %s: no name in Component submessage",
		 self->rx.type(), from ? from : "NULL");
	return send_reply(from, self->tx, pb::MT_HALRCOMP_ERROR,
			  socket, text);
    }
    cname = self->rx.comp().name().c_str();

    // validate pinlist attributes if pins are present -
    // to create a pin, it must have, name, type, direction
    for (int i = 0; i < self->rx.comp().pin_size(); i++) {
	const pb::Pin &p = self->rx.comp().pin(i);
	if (!(p.has_name() && p.has_type() && p.has_dir())) {
	    // TODO if (type < HAL_BIT || type > HAL_U32)
	    std::string s;
	    TextFormat::PrintToString(p, &s);
	    std::string err = "request from " + std::string(cname) + ": invalid pin: " + s;
	    return send_reply(from, self->tx, pb::MT_HALRCOMP_ERROR,
			      socket, text);
	}
    }

    // see if component already exists
    if (self->rcomps.count(cname) == 0) {
	// no, new component being created remotely
	rc = create_rcomp(self, from, cname, socket);


    } else {
	// component exists
	rc = self->rcomps[cname];
	// validate request against existing comp

    }



    self->tx.set_type(pb::MT_PING_ACKNOWLEDGE);
    reply_frame = zframe_new(NULL, self->tx.ByteSize());
    assert(reply_frame != 0);
    self->tx.SerializeWithCachedSizesToArray(zframe_data(reply_frame));
    retval = zstr_sendm (socket, from);
    assert(retval == 0);
    retval = zframe_send(&reply_frame, socket, 0);
    assert(retval == 0);
    self->tx.Clear();
    return 0;
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
