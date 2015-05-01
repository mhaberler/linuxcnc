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
#include "pbutil.hh"
#include <machinetalk/generated/types.npb.h>

static int adopt_ring_cb(hal_ring_t *r, void *cb_data);
static int create_socket(htself_t *self, htring_t *rd, int count);

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
        int retval __attribute__((cleanup(halpr_autorelease_mutex)));

	// all ring inspection, attaching and modifying internals
	// happens with the HAL mutex held
	rtapi_mutex_get(&(hal_data->mutex));

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
	return 0;

    if (self->rings.count(r->name) > 0) // already adopted
	return 0;

    htring_t *rd = new htring_t();

    // lookup ring descriptor
    rd->hr_primary = halpr_find_ring_by_name(r->name);

    assert(rd->hr_primary != NULL);
    assert(rd->hr_primary->haltalk_adopt);

    // attach by descriptor
    int handle = halpr_ring_attach_by_desc(rd->hr_primary,
					   &rd->primary, NULL);
    if (handle < 0)
	return handle;

    assert(ringbuffer_attached(&rd->primary));
    HALDBG("adopted ring '%s'", r->name);
    rcount = 1;

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

	HALDBG("adopted paired ring '%s'", rd->hr_paired->name);
    }

    // primary, and optionally paird ring attached.
    // create the zeroMQ socket.
    if (create_socket(self, rd, rcount) < 0)
	goto UNWIND;

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

    // announcement: TBD

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

static int create_socket(htself_t *self, htring_t *rd, int count)
{
    // inspect socket type
    // check against rcount
    // open socket
    switch (rd->hr_primary->haltalk_zeromq_stype) {

    case pb_socketType_ST_ZMQ_PUB:
	// rcount == 1
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
