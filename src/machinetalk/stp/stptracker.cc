#define _DEBUG

#include <stdio.h>
#include <string>

#include<arpa/inet.h>
#include<sys/socket.h>

#include <google/protobuf/text_format.h>
#include <machinetalk/generated/message.pb.h>
#include <stptracker.h>
#include <stptracker-private.h>

namespace gpb = google::protobuf;

extern int stp_debug;

mvar_t *stmon_double(const char *name, double *dref)
{
    _mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::HAL_FLOAT;
    self->value.f = dref;
    self->var_updates = 0;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->serial = -1;
    return self;
}

mvar_t *stmon_s32(const char *name, int *sref)
{
    mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::HAL_S32;
    self->value.s = sref;
    self->var_updates = 0;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->serial = -1;
    return self;
}

mvar_t *stmon_u32(const char *name, int *uref)
{
    mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::HAL_U32;
    self->value.u = uref;
    self->var_updates = 0;
    self->serial = -1;
    return self;
}

mvar_t *stmon_bool(const char *name, bool *bref)
{
    mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::HAL_BIT;
    self->value.b = bref;
    self->var_updates = 0;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->serial = -1;
    return self;
}

mvar_t *stmon_string(const char *name, const void **sref)
{
    mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::STRING;
    self->bsize = 0;
    self->value.blob.bref = sref;
    self->value.blob.bsize = &self->bsize;
    self->var_updates = 0;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->serial = -1;
    return self;
}

mvar_t *stmon_blob(const char *name, const void **bref, size_t *bsize)
{
    mvar_t *self = new _mvar_t;
    assert (self);
    self->name = name;
    self->type = pb::BYTES;
    self->value.blob.bref = bref;
    self->value.blob.bsize = bsize;
    self->var_updates = 0;
    self->callback = NULL;
    self->callback_arg = NULL;
    self->serial = -1;
    return self;
}

//-------- groups ----

mgroup_t *stmon_group_new(const char *name,
			  group_update_complete_cb callback,
			  void *callback_arg)
{
    _mgroup_t *self = new _mgroup_t;
    assert (self);
    self->name = name;
    self->callback = callback;
    self->callback_arg = callback_arg;
    self->group_full_updates = 0;
    self->group_incr_updates = 0;
    self->serial = -1;
    return self;
}

int stmon_add_var(_mgroup_t *self, _mvar_t *v)
{
    // should check for dups
    self->vars.push_back(v);
    self->byname[std::string(v->name)] = v;
    return 0;
}


//-------- source ----
msource_t *stmon_source_new(_mtracker_t *t)
{
    _msource_t *self = new _msource_t;
    assert (self);
    assert (t);
    self->socket = zsocket_new (t->ctx, ZMQ_XSUB);
    assert(self->socket);
    self->tracker = t;
    return self;
}

int stmon_source_add_origin(msource_t *self, const char *uri)
{
    assert(self);
    assert(uri);
    self->origin = std::string(uri);
    return zsocket_connect (self->socket, uri);
}

int stmon_source_add_group(msource_t *self, _mgroup_t *group)
{
    self->groups[group->name] = group;
    return 0;
}

//-------- tracker ----
mtracker_t *stmon_tracker_new(zctx_t *ctx)
{
    _mtracker_t *self = new _mtracker_t;
    assert (self);
    if (ctx)
	self->ctx = ctx;
    else
	self->ctx = zctx_new();
    return self;
}

static void s_tracker_task (void *args, zctx_t *ctx, void *pipe);

int stmon_tracker_run(_mtracker_t *self)
{
    assert(self->ctx);

    //  Start background agent
    self->pipe = zthread_fork (self->ctx, s_tracker_task, self);
    assert(self->pipe);

    //  and wait for it to initialize
    return retcode(self->pipe);
}

int stmon_tracker_stop(_mtracker_t *self)
{
    zstr_send (self->pipe, "EXIT");
    return retcode(self->pipe);
}

int stmon_tracker_stats(_mtracker_t *self)
{
    zstr_send (self->pipe, "STATS");
    return retcode(self->pipe);
}

int stmon_tracker_add_source(_mtracker_t *self, _msource_t *src)
{
    self->sources.push_back(src);
    return 0;
}

void stmon_set_change_callback(mvar_t *self, var_change_cb callback, void *arg)
{
    assert(self);
    self->callback = callback;
    self->callback_arg = arg;
}
//-------- internals ----

// executed by tracking task who owns the data structures
static int s_tracker_stats(_mtracker_t *self)
{
    int ngfail = 0, nvfail = 0;

    for (source_list::iterator sit = self->sources.begin();
	 sit != self->sources.end(); ++sit) {

	stp_info("origin %s:\n", (*sit)->origin.c_str());

	for (group_map::iterator it = (*sit)->groups.begin();
	     it != (*sit)->groups.end(); ++it) {
	    _mgroup_t *grp = (*it).second;

	    if (grp->group_full_updates == 0)
		ngfail++;
	    stp_info("\tgroup %s:\t serial %d full %d incr %d\n",
		     grp->name, grp->serial, grp->group_full_updates,
		     grp->group_incr_updates);

	    for (var_list::iterator vit = grp->vars.begin();
		 vit != grp->vars.end(); ++vit) {
		_mvar_t *v = (*vit);
		stp_info("\t\t%s:\t serial %d updates %d\n",
			 v->name, v->serial, v->var_updates);
		if (v->var_updates == 0)
		    nvfail++;
	    }
	}
    }
    if (ngfail)
	stp_info("warning: %d group(s) never updated\n",ngfail);

    if (nvfail)
	stp_info("warning: %d variable(s) never updated\n",nvfail);
    return 0;
}

static inline int apply_update(pb::Container &rx, zframe_t *f, _mgroup_t *grp)
{
    if (!rx.ParseFromArray(zframe_data(f), zframe_size(f))) {
	rx.Clear();
	char *s = zframe_strhex(f);
	stp_err("%s: cant decode '%s'\n", __func__, s);
	free(s);
	return -1;
    }

    if (false) {
	std::string text;
	if (TextFormat::PrintToString(rx, &text))
	    fprintf(stderr, "update: %s\n", text.c_str());
    }
    switch (rx.type()) {
    case pb::MT_STP_UPDATE_FULL:
    case pb::MT_STP_UPDATE:
	{
	    for (int i = 0; i < rx.signal_size(); i++) {
		pb::Signal const &s = rx.signal(i);
		_mvar_t *v = NULL;
		assert(s.has_handle());
		if (rx.type() ==  pb::MT_STP_UPDATE_FULL) {
		    assert(s.has_name());
		    name_iterator ni = grp->byname.find(s.name());
		    if (ni == grp->byname.end()) {
			stp_debug("group %s variable %s not tracked\n",
				  grp->name, s.name().c_str());
			continue;
		    }
		    v = ni->second;
		    grp->byhandle[s.handle()] = v;
		} else {
		    // incremental update.
		    handle_iterator hi = grp->byhandle.find(s.handle());
		    if (hi == grp->byhandle.end()) {
			stp_err("group %s : no variable with handle %d\n",
				grp->name, s.handle());
			continue; // not tracked.
		    }
		    v = hi->second;
		}
		if (v->type != s.type()) {
		    stp_err("group %s : variable %s: type mismatch %d/%d\n",
			    grp->name, v->name, v->type, s.type());
			continue;
		}
		switch (v->type) {
		default:
		    stp_err("group %s : variable %s: invalid signal type %d\n",
			    grp->name, v->name, v->type);
		    continue;
		case pb::HAL_BIT:
		    *(v->value.b) = s.halbit();
		    break;
		case pb::HAL_FLOAT:
		    *(v->value.f) = s.halfloat();
		    break;
		case pb::HAL_S32:
		    *(v->value.s) = s.hals32();
		    break;
		case pb::HAL_U32:
		    *(v->value.u) = s.halu32();
		    break;

		case pb::STRING:
		case pb::BYTES:
		    {
			stpblob_t *bp = &v->value.blob;
			size_t nbsize;
			const void *src;
			if (v->type == pb::BYTES) {
			    nbsize = s.blob().size();
			    src = s.blob().c_str();
			} else {
			    nbsize = s.strval().size() + 1; // trailing zero
			    src = s.strval().c_str();
			}
			if (*bp->bsize < nbsize) {
			    // resize
			    void *old = *bp->bsize > 0 ?
				(void *)*(bp->bref) : NULL;
			    *(bp->bref) = realloc(old, nbsize);
			    if (*(bp->bref) == NULL) {
				stp_err("%s/%s: realloc %d failure - out of memory\n",
					 grp->name, v->name, nbsize);
				*bp->bsize = 0;
			    } else {
				memcpy((void *)*(bp->bref), src, nbsize);
				*bp->bsize = nbsize;
			    }
			} else { // large enough
			    memcpy((void *)*(bp->bref), src, nbsize);
			    *bp->bsize = nbsize;
			}
		    }
		    break;

		}
		v->serial = rx.serial();
		v->var_updates++;
		if (v->callback)
		    v->callback(v, v->callback_arg);
	    }
	    if (rx.type() == pb::MT_STP_UPDATE_FULL)
		grp->group_full_updates++;
	    else
		grp->group_incr_updates++;
	    grp->serial = rx.serial();
	    if (grp->callback)
		grp->callback(grp, grp->callback_arg);
	}
	break;

    case pb::MT_STP_NOGROUP: // note set
	stp_err("group %s subscribe error:\n", grp->name);
	for (int i = 0; i < rx.note_size(); i++)
	    stp_err("group %s subscribe error: %s\n",
		    grp->name,
		    rx.note(i).c_str());
	break;

    default:
	stp_err("group %s: bad message type %d: %s\n", grp->name, rx.type());

	// bad message type
	;
    }
    rx.Clear();
    return 0;
}


static int
s_source_recv(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    _msource_t *src =  (_msource_t *) arg;
    zmsg_t *m;
    zframe_t *f;
    struct _mgroup *grp;

    m = zmsg_recv(item->socket);
    assert(m);
    char *topic = zmsg_popstr(m);
    assert(topic);

    f = zmsg_pop (m);
    assert(f);

    grp = src->groups[topic];
    if (grp)
	apply_update(src->tracker->update, f, grp);
    else
	stp_err("%s: no such group: %s\n", __func__, topic);
    free(topic);
    zframe_destroy(&f);
    zmsg_destroy(&m);
    return 0;
}

static int
cmdpipe_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    _mtracker_t *self = (_mtracker_t *) arg;
    int retval = 0;
    char *cmd_str = zstr_recv (item->socket);
    assert(cmd_str);

    if (!strcmp(cmd_str,"EXIT")) {
	retval = -1; // exit reactor
    }
    if (!strcmp(cmd_str,"STATS")) {
	zstr_sendf (item->socket, "%d",s_tracker_stats(self));
    }
    zstr_free(&cmd_str);
    return retval;
}

static void s_tracker_task (void *args, zctx_t *ctx, void *pipe)
{
    _mtracker_t *self = (_mtracker_t *) args;
    zloop_t *loop =  zloop_new();

    // watch pipe for API commands
    zmq_pollitem_t cmditem =  { pipe, 0, ZMQ_POLLIN, 0 };
    assert(zloop_poller (loop, &cmditem, cmdpipe_readable, self) == 0);

    for (source_list::iterator sit = self->sources.begin();
	 sit != self->sources.end(); ++sit) {
	(*sit)->pollitem = { (*sit)->socket, 0, ZMQ_POLLIN, 0 };
	assert(zloop_poller (loop, &(*sit)->pollitem, s_source_recv, (*sit)) == 0);

	for (group_map::iterator it = (*sit)->groups.begin();
	     it != (*sit)->groups.end(); ++it) {
	    const char *topic = it->first.c_str();
	    size_t len = strlen(topic);
	    zframe_t *f = zframe_new(NULL, len + 1);
	    char *fdata = (char *)zframe_data(f);
	    strncpy(fdata + 1 , topic, len);
	    *fdata = '\001';
	    assert(zframe_send (&f, (*sit)->socket,  0) == 0);
	}
    }
    // good to go
    zstr_send (pipe, "0");

    zloop_start (loop);

    zstr_send (pipe, "0");
}
