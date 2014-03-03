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
#include "hal_iter.h"
#include "halutil.hh"
#include "halpb.h"
#include "pbutil.hh"
#include "rtapi_hexdump.h"

#include <google/protobuf/text_format.h>

static int describe_comp(hal_comp_t *comp,  void *arg);
static int describe_sig(hal_sig_t *sig,  void *arg);
static int describe_funct(hal_funct_t *funct,  void *arg);
static int describe_ring(hal_ring_t *ring,  void *arg);
static int describe_thread(hal_thread_t *thread,  void *arg);
static int describe_group(hal_group_t *group,  void *arg);

// describe the current HAL universe.
int
process_describe(htself_t *self, const char *from,  void *socket)
{
    int retval __attribute__((cleanup(halpr_autorelease_mutex)));
    rtapi_mutex_get(&(hal_data->mutex));

    halpr_foreach_comp(NULL, describe_comp, self);
    halpr_foreach_sig(NULL, describe_sig, self);
    halpr_foreach_group(NULL, describe_group, self);
    halpr_foreach_funct(NULL, describe_funct, self);
    halpr_foreach_ring(NULL, describe_ring, self);
    halpr_foreach_thread(NULL, describe_thread, self);
    return send_pbcontainer(from, self->tx, socket);
}

// ----- end of public functions ---

static int describe_comp(hal_comp_t *comp,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Component *c = self->tx.add_comp();
    halpr_describe_component(comp, c);
    return 0;
}

static int describe_sig(hal_sig_t *sig,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Signal *s = self->tx.add_signal();
    halpr_describe_signal(sig, s);
    return 0;
}

static int describe_member(int level, hal_group_t **groups,
			   hal_member_t *member, void *arg)
{
    hal_sig_t *sig = (hal_sig_t *)SHMPTR(member->sig_member_ptr);

    pb::Group *pbgroup =  (pb::Group *) arg;
    pb::Member *pbmember = pbgroup->add_member();

    pbmember->set_name(sig->name);
    pbmember->set_handle(sig->handle);
    pbmember->set_type((pb::ObjectType)sig->type);
    pbmember->set_userarg1(member->userarg1);
    pbmember->set_epsilon(member->epsilon);
    return 0;
}

static int describe_group(hal_group_t *g,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Group *pbgroup = self->tx.add_group();

    pbgroup->set_name(g->name);
    pbgroup->set_refcount(g->refcount);
    pbgroup->set_userarg1(g->userarg1);
    pbgroup->set_userarg2(g->userarg2);
    pbgroup->set_handle(g->handle);

    halpr_foreach_member(g->name, describe_member, pbgroup, RESOLVE_NESTED_GROUPS);
    return 0;
}

static int describe_funct(hal_funct_t *funct,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Function *f = self->tx.add_function();
    halpr_describe_funct(funct, f);
    return 0;
}

static int describe_ring(hal_ring_t *ring,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Ring *r = self->tx.add_ring();
    halpr_describe_ring(ring, r);
    return 0;
}

static int describe_thread(hal_thread_t *thread,  void *arg)
{
    htself_t *self = (htself_t *) arg;
    pb::Thread *t = self->tx.add_thread();
    halpr_describe_thread(thread, t);
    return 0;
}
