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
#include "rtapi_hexdump.h"

static int
process_container(const char *from,  htself_t *self, void *socket);

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
	    process_container(origin, self, poller->socket);
	}
	zframe_destroy(&f);
    }
    free(origin);
    zmsg_destroy(&msg);
    return retval;
}

// ----- end of public functions ----

static int
process_container(const char *from,  htself_t *self, void *socket)
{
    int retval = 0;
    zframe_t *reply_frame;

    switch (self->rx.type()) {

    case pb::MT_PING:
	self->tx.set_type(pb::MT_PING_ACKNOWLEDGE);
	reply_frame = zframe_new(NULL, self->tx.ByteSize());
	self->tx.SerializeWithCachedSizesToArray(zframe_data(reply_frame));
	zstr_sendm (socket, from);
	retval = zframe_send (&reply_frame, socket, 0);
	assert(retval == 0);
	self->tx.Clear();
	break;

    default:
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: rcommand from %s : unhandled type %d",
			self->cfg->progname, from, (int) self->rx.type());
	retval = -1;
    }
    return retval;
}
