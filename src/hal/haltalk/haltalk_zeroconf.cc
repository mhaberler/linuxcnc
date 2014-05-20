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

// zeroconf-register haltalk services

int
zeroconf_announce(htself_t *self)
{
    char name[100];
    char buf[40];
    uuid_unparse(self->uuid, buf);

    self->zsgroup = {0};
    snprintf(name,sizeof(name), "HAL Group service on %s pid %d", self->cfg->ipaddr, getpid());
    self->zsgroup.name = strdup(name);
    self->zsgroup.proto =  AVAHI_PROTO_INET;
    self->zsgroup.interface =  AVAHI_IF_UNSPEC;
    self->zsgroup.type =  MACHINEKIT_DNSSD_SERVICE_TYPE;
    self->zsgroup.port = self->z_group_port;
    self->zsgroup.txt = avahi_string_list_add_printf(self->zsgroup.txt,
						      "dsn=%s",self->z_group_status_dsn);
    self->zsgroup.txt = avahi_string_list_add_printf(self->zsgroup.txt,
						      "uuid=%s",self->cfg->service_uuid);
    self->zsgroup.txt = avahi_string_list_add_printf(self->zsgroup.txt,
						      "instance=%s",buf);
    self->zsgroup.txt = avahi_string_list_add_printf(self->zsgroup.txt, "service=halgroup");
    self->zsgroup.subtypes = avahi_string_list_add(self->zsgroup.subtypes,
						    HALGROUP_DNSSD_SUBTYPE
						    MACHINEKIT_DNSSD_SERVICE_TYPE);

    if ((self->group_publisher = mk_zeroconf_register(&self->zsgroup,
						      self->av_loop)) == NULL)
	return -1;

    self->zsrcomp = {0};
    snprintf(name,sizeof(name), "HAL Rcomp service on %s pid %d", self->cfg->ipaddr, getpid());
    self->zsrcomp.name = strdup(name);
    self->zsrcomp.proto =  AVAHI_PROTO_INET;
    self->zsrcomp.interface = AVAHI_IF_UNSPEC;
    self->zsrcomp.type =  MACHINEKIT_DNSSD_SERVICE_TYPE;
    self->zsrcomp.port = self->z_rcomp_port;
    self->zsrcomp.txt = avahi_string_list_add_printf(self->zsrcomp.txt,
						      "dsn=%s",self->z_rcomp_status_dsn);
    self->zsrcomp.txt = avahi_string_list_add_printf(self->zsrcomp.txt,
						      "uuid=%s",self->cfg->service_uuid);
    self->zsrcomp.txt = avahi_string_list_add_printf(self->zsrcomp.txt,
						      "instance=%s",buf);
    self->zsrcomp.txt = avahi_string_list_add_printf(self->zsrcomp.txt, "service=halrcomp");
    self->zsrcomp.subtypes = avahi_string_list_add(self->zsrcomp.subtypes,
						    HALRCOMP_DNSSD_SUBTYPE
						    MACHINEKIT_DNSSD_SERVICE_TYPE);

    if ((self->rcomp_publisher = mk_zeroconf_register(&self->zsrcomp,
						      self->av_loop)) == NULL)
	return -1;

    self->zscommand = {0};
    snprintf(name,sizeof(name), "HAL Rcommand service on %s pid %d", self->cfg->ipaddr, getpid());
    self->zscommand.name = strdup(name);
    self->zscommand.proto =  AVAHI_PROTO_INET;
    self->zscommand.interface = AVAHI_IF_UNSPEC;
    self->zscommand.type =  MACHINEKIT_DNSSD_SERVICE_TYPE;
    self->zscommand.port = self->z_command_port;
    self->zscommand.txt = avahi_string_list_add_printf(self->zscommand.txt,
							"dsn=%s",self->z_command_dsn);
    self->zscommand.txt = avahi_string_list_add_printf(self->zscommand.txt,
							"uuid=%s",self->cfg->service_uuid);
    self->zscommand.txt = avahi_string_list_add_printf(self->zscommand.txt, "instance=%s",buf);
    self->zscommand.txt = avahi_string_list_add_printf(self->zscommand.txt, "service=halrcmd");
    self->zscommand.subtypes = avahi_string_list_add(self->zscommand.subtypes,
						      HALRCMD_DNSSD_SUBTYPE
						      MACHINEKIT_DNSSD_SERVICE_TYPE);

    if ((self->command_publisher = mk_zeroconf_register(&self->zscommand,
							self->av_loop)) == NULL)
	return -1;

    return 0;
}

int
zeroconf_withdraw(htself_t *self)
{
    if (self->group_publisher)
	mk_zeroconf_unregister(self->group_publisher);
    if (self->rcomp_publisher)
	mk_zeroconf_unregister(self->rcomp_publisher);
    if (self->command_publisher)
	mk_zeroconf_unregister(self->command_publisher);

    // deregister poll adapter
    if (self->av_loop)
        avahi_czmq_poll_free(self->av_loop);
    return 0;
}
