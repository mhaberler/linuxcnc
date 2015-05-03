/*
 * Copyright (C) 2015 Michael Haberler <license@mah.priv.at>
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
#include "halpb.hh"
#include "hal_iter.h"
#include "rtapi_hexdump.h"

#include "pbutil.hh"
#include <machinetalk/generated/types.npb.h>

static int adopt_ring_cb(hal_ring_t *r, void *cb_data);
//static int create_socket(htself_t *self, htring_t *rd, int count);


int handle_xpub_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    HALDBG("");

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

	// adopt any rings defined since startup
	scan_rings(self);

	if (strlen(topic) == 0) {
	    // this was a subscribe("") - all topics

#if 0
	    // describe all groups
	    for (groupmap_iterator gi = self->groups.begin();
		 gi != self->groups.end(); gi++) {

		group_t *g = gi->second;
		self->tx.set_type(pb::MT_HALGROUP_FULL_UPDATE);
		self->tx.set_uuid(self->process_uuid, sizeof(self->process_uuid));
		self->tx.set_serial(g->serial++);
		describe_parameters(self);
		describe_group(self, gi->first.c_str(), gi->first.c_str(), poller->socket);

		// if first subscriber: activate scanning
		if (g->timer_id < 0) { // not scanning
		    g->timer_id = zloop_timer(self->z_loop, g->msec,
					      0, handle_group_timer, (void *)g);
		    assert(g->timer_id > -1);
		    rtapi_print_msg(RTAPI_MSG_DBG,
				    "%s: start scanning group %s, tid=%d, %d mS, %d members, %d monitored",
				    self->cfg->progname, topic, g->timer_id, g->msec,
				    g->cg->n_members, g->cg->n_monitored);
		}
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: wildcard subscribe group='%s' serial=%d",
				self->cfg->progname,
				gi->first.c_str(), gi->second->serial);
	    }
#endif
	} else {
	    // a selective subscribe - describe only the desired ring

#if 0
	    groupmap_iterator gi = self->groups.find(topic);
	    if (gi != self->groups.end()) {
		group_t *g = gi->second;
		self->tx.set_type(pb::MT_HALGROUP_FULL_UPDATE);
		self->tx.set_uuid(self->process_uuid, sizeof(self->process_uuid));
		self->tx.set_serial(g->serial++);
		describe_parameters(self);
		describe_group(self, gi->first.c_str(), gi->first.c_str(), poller->socket);
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: subscribe group='%s' serial=%d",
				self->cfg->progname,
				gi->first.c_str(), gi->second->serial);

		// if first subscriber: activate scanning
		if (g->timer_id < 0) { // not scanning
		    g->timer_id = zloop_timer(self->z_loop, g->msec,
					      0, handle_group_timer, (void *)g);
		    assert(g->timer_id > -1);
		    rtapi_print_msg(RTAPI_MSG_DBG,
				    "%s: start scanning group %s, tid=%d, %d mS, %d members, %d monitored",
				    self->cfg->progname, topic,
				    g->timer_id, g->msec, g->cg->n_members, g->cg->n_monitored);
		}
	    } else {
		// non-existant topic, complain.
		self->tx.set_type(pb::MT_STP_NOGROUP);
		note_printf(self->tx, "no such ring: '%s', currently %d valid groups",
			    topic, self->groups.size());
		if (self->groups.size())
		    note_printf(self->tx, ": ");
		for (groupmap_iterator g = self->groups.begin();
		     g != self->groups.end(); g++) {
		    note_printf(self->tx, "    %s", g->first.c_str());
		}
		int retval = send_pbcontainer(topic, self->tx, self->z_halgroup);
		assert(retval == 0);

	    }
#endif
	}
	break;

    case '\000':   // last unsubscribe

#if 0
	if (self->groups.count(topic) > 0) {
	    group_t *g = self->groups[topic];
	    // stop the scanning timer
	    if (g->timer_id > -1) {  // currently scanning
		rtapi_print_msg(RTAPI_MSG_DBG,
				"%s: group %s stop scanning, tid=%d",
				self->cfg->progname, topic, g->timer_id);
		int retval = zloop_timer_end (self->z_loop, g->timer_id);
		assert(retval == 0);
		g->timer_id = -1;
	    }
	}
#endif
	break;

    default:
	break;
    }
    zframe_destroy(&f_subscribe);
    return 0;
}

int handle_router_input(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    HALDBG("");

    return 0;
}

int handle_ring_timer(zloop_t *loop, int timer_id, void *arg)
{
    htring_t *rd = (htring_t *) arg;
    const void *ptr;
    size_t size;
    ring_size_t rsize;
    int retval;
    zframe_t *d;

    switch (rd->hr_primary->haltalk_zeromq_stype) {

    case pb_socketType_ST_ZMQ_XPUB:

	switch (rd->primary.header->type) {

	case RINGTYPE_RECORD:
	    {
		switch ((retval = record_read(&rd->primary, &ptr, &size))) {
		case 0:
		    HALINFO("%s: RECORD total %d", rd->hr_primary->name, size);

		    rtapi_print_hex_dump(RTAPI_MSG_DBG, RTAPI_DUMP_PREFIX_OFFSET,
					     16, 1, ptr, size, true,
					     NULL, "%6zu ", size);

		    zstr_sendm(rd->self->z_ring_xpub, rd->hr_primary->name);
		    d =  zframe_new (ptr, size);
		    retval = zframe_send (&d, rd->self->z_ring_xpub, 0);
		    assert(retval == 0);
		    record_shift(&rd->primary);
		    break;

		case EAGAIN:
		    // no data available
		    break;

		default:
		    HALERR("record_read(%s): %d - %s",
			   rd->hr_primary->name,
			   retval, strerror(retval));
		}
	    }
	    break;


	case RINGTYPE_STREAM:
	    {
		while ((size = stream_read_space(rd->primary.header)) > 0) {
		    HALINFO("%s: STREAM total %zu", rd->hr_primary->name, size);

		    zstr_sendm(rd->self->z_ring_xpub, rd->hr_primary->name);
		    d =  zframe_new(NULL, size);
		    size_t actual = stream_read(&rd->primary, (char *)zframe_data(d), size);
		    assert(actual == size);

		    rtapi_print_hex_dump(RTAPI_MSG_DBG, RTAPI_DUMP_PREFIX_OFFSET,
					     16, 1, zframe_data(d), size, true,
					     NULL, "%6zu ", size);
		    retval = zframe_send(&d, rd->self->z_ring_xpub, 0);
		    assert(retval == 0);
		}
	    }
	    break;

	case RINGTYPE_MULTIPART:
	    {
		ringvec_t rv;
		// since a multiframe message is wrapped in a record, check if a
		// record is available, then read the multiframes therein

		if ((rsize = record_next_size(&rd->primary)) > -1) {
		    zstr_sendm(rd->self->z_ring_xpub, rd->hr_primary->name);

		    HALINFO("%s: MULTI total %d", rd->hr_primary->name, rsize);

		    while (frame_readv(&rd->mr_primary, &rv) == 0) {
			mflag_t f;
			f.u = rv.rv_flags;

			rtapi_print_msg(RTAPI_MSG_DBG, "flag=%u/0x%x msgid=%d format=%d more=%d eor=%d",
					f.u, f.u, f.f.msgid, f.f.format, f.f.more, f.f.eor);


			rtapi_print_hex_dump(RTAPI_MSG_DBG, RTAPI_DUMP_PREFIX_OFFSET,
					     16, 1, rv.rv_base, rv.rv_len, true,
					     NULL, "%6zu ", rv.rv_len);

			d =  zframe_new(rv.rv_base, rv.rv_len);


			// XXX: how to handle rv.rv_flags here?
			retval = zframe_send(&d, rd->self->z_ring_xpub, 0);
			assert(retval == 0);
			frame_shift(&rd->mr_primary);
		    }
		    msg_read_flush(&rd->mr_primary);
		}
	    }
	    break;
	}
	break;

    case pb_socketType_ST_ZMQ_ROUTER:
	break;

    default: ;
    }
    return 0;
}


// walk HAL rings, looking for rings with haltalk_adopt flag set
// attach them, including any paired ring
// check validity of arguments, and act on socket and announce
// parameters
//
// idempotent - will add new rings as found, and leave already
// adopted ones alone
int scan_rings(htself_t *self)
{
    {
	// all ring inspection, attaching and modifying internals
	// happens with the HAL mutex held
	WITH_HAL_MUTEX();

        int retval;

	if ((retval = halpr_foreach_ring(NULL, adopt_ring_cb, self)) < 0)
	    return retval;
	rtapi_print_msg(RTAPI_MSG_DBG,"scanned %d rings(s)\n", retval);
    }
    return 0;
}

// inspect a single ring object for adoption
static int adopt_ring_cb(hal_ring_t *r, void *cb_data)

{
    htself_t *self = (htself_t *)cb_data;
    int rcount = 0;

    if (!r->haltalk_adopt)
	// adopt flag not set, so ignore.
	return 0;

    if (self->rings.count(r->name) > 0)
	// already adopted
	return 0;

    // that ring is for us.
    htring_t *rd = new htring_t();

    // lookup ring descriptor
    // HAL mutex is held in caller (scan_rings).
    rd->hr_primary = halpr_find_ring_by_name(r->name);

    assert(rd->hr_primary != NULL);
    assert(rd->hr_primary->haltalk_adopt);

    // attach by descriptor
    int handle = halpr_ring_attach_by_desc(rd->hr_primary,
					   &rd->primary, NULL);
    if (handle < 0) {
	HALERR("failed to attach %s", rd->hr_primary->name);
	return handle;
    }

    assert(ringbuffer_attached(&rd->primary));
    rcount = 1;

    // if its a multiframe ring, init accessor structure
    if (rd->primary.header->type == RINGTYPE_MULTIPART) {
	msgbuffer_init(&rd->mr_primary, &rd->primary);
    }
    HALDBG("adopted ring '%s'", r->name);


    // if there's a paired ring, inspect that as well:
    if (rd->hr_primary->paired_handle != 0) {

	// find paired ringbuffer by ID
	rd->hr_paired = halpr_find_ring_by_id(rd->hr_primary->paired_handle);

	assert(rd->hr_paired != NULL);
	assert(rd->hr_paired->paired_handle == rd->hr_primary->handle);

	// and attach it too, by descriptor
	int handle = halpr_ring_attach_by_desc(rd->hr_paired,
					       &rd->paired, NULL);
	if (handle < 0)
	    return handle;

	assert(ringbuffer_attached(&rd->paired));
	rcount += 1;

	// if the secondary is a multiframe ring, init accessor structure as well
	if (rd->paired.header->type == RINGTYPE_MULTIPART) {
	    msgbuffer_init(&rd->mr_paired, &rd->paired);
	}
	HALDBG("adopted paired ring '%s'", rd->hr_paired->name);
    }

    // haltalk ring descriptor complete. Now figure what to do with these rings:
    if (rd->hr_primary->haltalk_writes) {
	// haltalk should write. Any writer yet?
	if (rd->primary.header->writer != 0) {
	    // not good. This ring has a writer already, cop out
	    HALERR("not adopting ring '%s', has writer %d",
		   rd->hr_primary->name, rd->primary.header->writer);
	    goto UNWIND;
	}

	// if there's a paired ring, it better have no reader
	// because haltalk will read it:
	if ((rd->hr_primary->paired_handle != 0) && // paired exists
	    (rd->paired.header->reader != 0)) {     // paired has reader

	    // not good. This ring has a reader already, cop out
	    HALERR("not adopting ring '%s', paired ring '%s' already has reader %d",
		   rd->hr_primary->name,
		   rd->hr_paired->name,
		   rd->primary.header->reader);
	    goto UNWIND;
	}
	// mark ring as served by haltalk write-side and read side, respectively
	rd->primary.header->writer = self->comp_id;
	if (rd->hr_primary->paired_handle != 0)
	    rd->paired.header->reader = self->comp_id;

    } else {
	// haltalk should read on primary. Any reader yet?
	if (rd->primary.header->reader != 0) {
	    // not good. This ring has a reader already, cop out
	    HALINFO("not adopting ring '%s', has reader %d",
		    rd->hr_primary->name, rd->primary.header->reader);
	    goto UNWIND;
	}
	// if there's a paired ring, it better have no writer
	// because haltalk will write it:
	if ((rd->hr_primary->paired_handle != 0) && // paired exists
	    (rd->paired.header->writer != 0)) {     // paired has writer

	    // not good. This ring has a writer already, cop out
	    HALERR("not adopting ring '%s', paired ring '%s' already has writer %d",
		   rd->hr_primary->name,
		   rd->hr_paired->name,
		   rd->primary.header->writer);
	    goto UNWIND;
	}
	// mark ring as served by haltalk read-side and write side, respectively
	rd->primary.header->reader = self->comp_id;
	if (rd->hr_primary->paired_handle != 0)
	    rd->paired.header->writer = self->comp_id;
    }

    // add any non-default timer setting code here
    rd->msec = self->cfg->default_ring_timer;

    rd->timer_id = zloop_timer(self->z_loop, rd->msec, 0,
			       handle_ring_timer, (void *)rd);
    assert(rd->timer_id > -1);
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "%s: start scanning ring %s, tid=%d, %d mS",
		    self->cfg->progname,  rd->hr_primary->name, rd->timer_id, rd->msec);
    rd->self = self;
    self->rings[r->name] = rd;

    return 0;

 UNWIND:
    // undo damage
    if (ringbuffer_attached(&rd->primary))
	hal_ring_detach(&rd->primary);

    if (ringbuffer_attached(&rd->paired))
	hal_ring_detach(&rd->paired);

    // TBD close socket if set

    delete rd;
    self->rings.erase(r->name);
    return -1;
}

#if 0
static int create_socket(htself_t *self, htring_t *rd, int count)
{
    // inspect socket type
    // check against rcount
    // open socket
    switch (rd->hr_primary->haltalk_zeromq_stype) {

    case pb_socketType_ST_ZMQ_PUB:
	// rcount == 1

	// rd->z_ring = zsocket_new (self->z_context, ZMQ_PUB);
	// assert(rd->z_ring);
	// zsocket_set_linger (rd->z_ring, 0);
	break;

    case pb_socketType_ST_ZMQ_ROUTER:
	// rcount == 2
	break;

    case pb_socketType_ST_ZMQ_SUB:
    case pb_socketType_ST_ZMQ_PAIR:
    case pb_socketType_ST_ZMQ_REQ:
    case pb_socketType_ST_ZMQ_REP:
    case pb_socketType_ST_ZMQ_DEALER:
    case pb_socketType_ST_ZMQ_PULL:
    case pb_socketType_ST_ZMQ_PUSH:
    case pb_socketType_ST_ZMQ_XPUB:
    case pb_socketType_ST_ZMQ_XSUB:
    case pb_socketType_ST_ZMQ_STREAM:
    default:
	HALERR("socket type %d not supported",
	       rd->hr_primary->haltalk_zeromq_stype);
	return -ESOCKTNOSUPPORT;
    }
    return 0;
}
#endif
