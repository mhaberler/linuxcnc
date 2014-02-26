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

#include "pbutil.hh"
#include <czmq.h>

int
send_pbcontainer(const char *dest, pb::Container &c, void *socket)
{
    int retval = 0;
    zframe_t *f;

    f = zframe_new(NULL, c.ByteSize());
    if (f == NULL)
	return -ENOMEM;

    c.SerializeWithCachedSizesToArray(zframe_data(f));
    if (dest) {
	retval = zstr_sendm (socket, dest);
	if (retval)
	    goto DONE;
    }
    retval = zframe_send(&f, socket, 0);
 DONE:
    c.Clear();
    return retval;
}
