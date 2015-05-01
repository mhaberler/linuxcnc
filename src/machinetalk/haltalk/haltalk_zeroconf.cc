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
ht_zeroconf_announce_services(htself_t *self)
{
    char name[LINELEN];
    char uri[PATH_MAX];

    // use mDNS addressing if running over TCP:
    // construct a URI of the form 'tcp://<hostname>.local.:<portnumber>'

    if (self->cfg->remote)
	snprintf(uri,sizeof(uri), "tcp://%s.local.:%d", self->hostname, self->z_group_port);

    snprintf(name,sizeof(name), "HAL Group service on %s.local pid %d", self->hostname, getpid());
    self->halgroup_publisher = zeroconf_service_announce(name,
							 MACHINEKIT_DNSSD_SERVICE_TYPE,
							 HALGROUP_DNSSD_SUBTYPE,
							 self->z_group_port,
							 self->cfg->remote ? uri :
							 (char *)self->cfg->halgroup,
							 self->cfg->service_uuid,
							 self->puuid,
							 "halgroup", NULL,
							 self->av_loop);
    if (self->halgroup_publisher == NULL) {
	syslog_async(LOG_ERR, "%s: failed to start zeroconf HAL Group publisher\n",
		     self->cfg->progname);
	return -1;
    }

    snprintf(name,sizeof(name), "HAL Rcomp service on %s.local pid %d", self->hostname, getpid());

    if (self->cfg->remote)
	snprintf(uri,sizeof(uri), "tcp://%s.local.:%d",self->hostname, self->z_rcomp_port);

    self->halrcomp_publisher = zeroconf_service_announce(name,
							 MACHINEKIT_DNSSD_SERVICE_TYPE,
							 HALRCOMP_DNSSD_SUBTYPE,
							 self->z_rcomp_port,
							 self->cfg->remote ? uri :
							 (char *)self->cfg->halrcomp,
							 self->cfg->service_uuid,
							 self->puuid,
							 "halrcomp", NULL,
							 self->av_loop);
    if (self->halrcomp_publisher == NULL) {
	syslog_async(LOG_ERR, "%s: failed to start zeroconf HAL Rcomp publisher\n",
		     self->cfg->progname);
	return -1;
    }

    snprintf(name,sizeof(name),  "HAL Rcommand service on %s.local pid %d", self->hostname, getpid());
    if (self->cfg->remote)
	snprintf(uri,sizeof(uri), "tcp://%s.local.:%d",self->hostname, self->z_rcomp_port);
    self->halrcmd_publisher = zeroconf_service_announce(name,
							MACHINEKIT_DNSSD_SERVICE_TYPE,
							HALRCMD_DNSSD_SUBTYPE,
							self->z_halrcmd_port,
							self->cfg->remote ? uri :
							(char *)self->cfg->command,
							self->cfg->service_uuid,
							self->puuid,
							"halrcmd", NULL,
							self->av_loop);
    if (self->halrcmd_publisher == NULL) {
	syslog_async(LOG_ERR, "%s: failed to start zeroconf HAL Rcomp publisher\n",
		     self->cfg->progname);
	return -1;
    }


    snprintf(name,sizeof(name),  "HAL Xpub service on %s.local pid %d", self->hostname, getpid());
    if (self->cfg->remote)
	snprintf(uri,sizeof(uri), "tcp://%s.local.:%d",self->hostname, self->z_ring_xpub_port);
    self->ring_xpub_publisher = zeroconf_service_announce(name,
							MACHINEKIT_DNSSD_SERVICE_TYPE,
							RINGXPUB_DNSSD_SUBTYPE,
							self->z_halrcmd_port,
							self->cfg->remote ? uri :
							(char *)self->cfg->xpub,
							self->cfg->service_uuid,
							self->puuid,
							"ring_xpub", NULL,
							self->av_loop);
    if (self->ring_xpub_publisher == NULL) {
	syslog_async(LOG_ERR, "%s: failed to start zeroconf HAL Xpub publisher\n",
		     self->cfg->progname);
	return -1;
    }


    snprintf(name,sizeof(name),  "HAL Router service on %s.local pid %d", self->hostname, getpid());
    if (self->cfg->remote)
	snprintf(uri,sizeof(uri), "tcp://%s.local.:%d",self->hostname, self->z_ring_router_port);
    self->ring_router_publisher = zeroconf_service_announce(name,
							MACHINEKIT_DNSSD_SERVICE_TYPE,
							RINGROUTER_DNSSD_SUBTYPE,
							self->z_halrcmd_port,
							self->cfg->remote ? uri :
							(char *)self->cfg->router,
							self->cfg->service_uuid,
							self->puuid,
							"ring_router", NULL,
							self->av_loop);
    if (self->ring_router_publisher == NULL) {
	syslog_async(LOG_ERR, "%s: failed to start zeroconf HAL Router publisher\n",
		     self->cfg->progname);
	return -1;
    }

    return 0;
}

int
ht_zeroconf_withdraw_services(htself_t *self)
{
    zeroconf_service_withdraw(self->halgroup_publisher);
    zeroconf_service_withdraw(self->halrcomp_publisher);
    zeroconf_service_withdraw(self->halrcmd_publisher);
    zeroconf_service_withdraw(self->ring_xpub_publisher);
    zeroconf_service_withdraw(self->ring_router_publisher);

    // deregister poll adapter
    if (self->av_loop)
        avahi_czmq_poll_free(self->av_loop);
    return 0;
}

