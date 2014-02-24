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

// start the SDP listener thread
int
service_discovery_start(htself_t *self)
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
		    HAL_GROUP_STATUS_VERSION, // version
		    NULL, // ip
		    0, // port
		    self->z_group_status_dsn, // uri
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "HAL group STP");  // descr
    assert(retval == 0);

    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_STP_HALRCOMP, //type
		    HAL_RCOMP_STATUS_VERSION, // version
		    NULL, // ip
		    0, // port
		    self->z_rcomp_status_dsn, // uri
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "HAL RComp");  // descr
    assert(retval == 0);


    retval = sp_add(self->sd_publisher,
		    (int) pb::ST_HAL_RCOMMAND, //type
		    HAL_RCOMMAND_VERSION, // version
		    NULL, // ip
		    0, // port
		    self->z_command_dsn, // uri
		    (int) pb::SA_ZMQ_PROTOBUF, // api
		    "HAL Rcommand");  // descr
    assert(retval == 0);


    retval = sp_start(self->sd_publisher);
    assert(retval == 0);
    return 0;
}

int
service_discovery_stop(htself_t *self)
{
    // stop  the service announcement responder
    return sp_destroy(&self->sd_publisher);
}
