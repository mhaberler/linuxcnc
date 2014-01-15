#include <stdio.h>
#include <string>

#include<arpa/inet.h>
#include<sys/socket.h>

#include <google/protobuf/text_format.h>
#include <middleware/generated/message.pb.h>
#include <stptracker.h>
#include <stptracker-private.h>

namespace gpb = google::protobuf;


// static void s_talker_task (void *args, zctx_t *ctx, void *pipe);
// static int retcode(void *pipe);
// static inline int report_group(sttalker_t *self, stgroup_t *g, bool full);
// static inline int pack_and_send(sttalker_t *self, const char *topic);

extern int stp_debug;

mvar_t *stmon_double(const char *name, double *dref)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_FLOAT;
    self->value.f = dref;
    return self;
}

mvar_t *stmon_s32(const char *name, int *sref)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_S32;
    self->value.s = sref;
    return self;
}

mvar_t *stmon_u32(const char *name, int *uref)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_U32;
    self->value.u = uref;
    return self;
}

mvar_t *stmon_bool(const char *name, bool *bref)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_BIT;
    self->value.b = bref;
    return self;
}

mvar_t *stmon_string(const char *name, const void **sref)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::STRING;
    self->value.blob.bref = sref;
    self->value.blob.bsize = 0;
    return self;
}

mvar_t *stmon_blob(const char *name, const void **bref, size_t *bsize)
{
    mvar_t *self = (mvar_t *) zmalloc (sizeof (mvar_t));
    assert (self);
    self->name = name;
    self->type = pb::BYTES;
    self->value.blob.bref = bref;
    self->value.blob.bsize = bsize;
    return self;
}

//-------- groups ----

mgroup_t *stmon_group_new(const char *name, group_update_complete_cb callback, void *callback_arg)
{
    // _mgroup_t *self = (_mgroup_t *) zmalloc (sizeof (_mgroup_t));
    _mgroup_t *self = new _mgroup_t;
    assert (self);
    self->name = name;
    self->callback = callback;
    self->callback_arg = callback_arg;
    // self->vars = value_list();
    // self->byhandle = value_map();
    self->serial = self->group_updates = 0;
    return self;
}

int stmon_add_var(_mgroup_t *self, _mvar_t *v)
{
    // should check for dups
    self->vars.push_back(v);
    return 0;
}


//-------- source ----
msource_t *stmon_source_new(_mtracker_t *t)
{
    _msource_t *self = (_msource_t *) zmalloc (sizeof (_msource_t));
    assert (self);
    assert (t);
    self->socket = zsocket_new (t->ctx, ZMQ_SUB);
    assert(self->socket);
    self->groups = group_map();
    //    self->groups = zlist_new ();
    self->tracker = t;
    return self;
}

int stmon_source_add_origin(msource_t *self, const char *uri)
{
    assert(self);
    assert(uri);
    return zsocket_connect (self->socket, uri);
}

int stmon_source_add_group(msource_t *self, _mgroup_t *group)
{
    self->groups[group->name] = group;
    // return zlist_append (self->groups, group);
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
    // self->sources = zlist_new ();
    self->update =  new pb::Container();
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

int stmon_tracker_add_source(_mtracker_t *self, _msource_t *src)
{
    self->sources.push_back(src);
    return 0;
}


//-------- internals ----
// update: type: MT_STP_UPDATE_FULL
// signal {
//   type: HAL_FLOAT
//   name: "x"
//   handle: 158184224
//   halfloat: 0
// }
// signal {
//   type: HAL_FLOAT
//   name: "y"
//   handle: 158178280
//   halfloat: 0
// }
// signal {
//   type: HAL_FLOAT
//   name: "z"
//   handle: 158178192
//   halfloat: 0
// }
// serial: 21

static int apply_update(zframe_t *f, _mgroup_t *grp)
{
    fprintf(stderr,"%s: %p %s\n", __func__, f, grp->name);
    pb::Container rx;
    if (!rx.ParseFromArray(zframe_data(f), zframe_size(f)))
	return -1;

    std::string text;
    if (TextFormat::PrintToString(rx, &text))
	fprintf(stderr, "update: %s\n", text.c_str());

    switch (rx.type()) {
    case pb::MT_STP_UPDATE_FULL:
	// names and handles

	// check all vars in group set
	break;
    case pb::MT_STP_UPDATE:
	// handles only
	break;

    case pb::MT_STP_NOGROUP: // note set
	break;

    default:
	// bad message type
	;
    }
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

    fprintf(stderr,"%s: topic=%s\n", __func__, topic);

    f = zmsg_pop (m);
    assert(f);

    grp = src->groups[topic];
    if (grp)
	apply_update(f, grp);
    else
	fprintf(stderr,"%s: no such group: %s\n", __func__, topic);
    free(topic);
    zframe_destroy(&f);
    zmsg_destroy(&m);
    return 0;
}

static int
cmdpipe_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    int retval = 0;

    char *cmd_str = zstr_recv (item->socket);
    assert (cmd_str);

    fprintf(stderr,"%s: %s\n", __func__, cmd_str);

    if (!strcmp(cmd_str,"EXIT")) {
	retval = -1; // exit reactor
    }
    zstr_free(&cmd_str);
    return retval;
}

static void s_tracker_task (void *args, zctx_t *ctx, void *pipe)
{
    _mtracker_t *self = (_mtracker_t *) args;
    zloop_t *loop =  zloop_new();

    // watch command pipe for EXIT command
    zmq_pollitem_t cmditem =  { pipe, 0, ZMQ_POLLIN, 0 };
    assert(zloop_poller (loop, &cmditem, cmdpipe_readable, self) == 0);

    for (source_list::iterator sit = self->sources.begin();
	 sit != self->sources.end(); ++sit) {
	(*sit)->pollitem = { (*sit)->socket, 0, ZMQ_POLLIN, 0 };
	assert(zloop_poller (loop, &(*sit)->pollitem, s_source_recv, (*sit)) == 0);

	for (group_map::iterator it = (*sit)->groups.begin();
	     it != (*sit)->groups.end(); ++it) {
	    zsocket_set_subscribe ((*sit)->socket, it->first.c_str());
	}
    }
    // good to go
    zstr_send (pipe, "0");
    zloop_start (loop);
    zstr_send (pipe, "0");
}




#if 0
//-------- TODO ----

mservice_t *stmon_service_new(int port)
{
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    mservice_t *self = (mservice_t *) zmalloc (sizeof (mservice_t));
    assert (self);

    self->groups = zlist_new ();
    self->update =  new pb::Container();
    return self;
}

stgroup_t * strack_find_group(sttalker_t *self, const char *groupname)
{
    assert (self);
    assert (groupname);
    for (stgroup_t *g = (stgroup_t *) zlist_first (self->groups);
	 g != NULL;
	 g = (stgroup_t *) zlist_next (self->groups)) {
	if (!strcmp(groupname, g->name))
	    return g;
    }
    return NULL;
}

sttalker_t *strack_talker_new(void)
{
    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    sttalker_t *self = (sttalker_t *) zmalloc (sizeof (sttalker_t));
    assert (self);
    self->groups = zlist_new ();
    self->epsilon = STP_DEFAULT_EPSILON;
    self->update =  new pb::Container();
    self->empty_updates = true;
    self->serial = 0;
    return self;
}

int strack_talker_add(sttalker_t *self, stgroup_t *g)
{
    zlist_append (self->groups, g);
    return 0;
}

int strack_talker_run(sttalker_t *self,
		      zctx_t *ctx, const char *uri,
		      int interval, int beacon_port,
		      subscribe_cb *callback)
{
    assert (ctx);
    self->interval = interval;
    self->uri = uri;
    self->beacon_port = beacon_port;
    self->subscribe_callback = callback;

    //  Start background agent
    self->pipe = zthread_fork (ctx, s_talker_task, self);
    assert(self->pipe);

    //  and wait for it to initialize
    return retcode(self->pipe);
}

int strack_talker_update(sttalker_t *self, const char *groupname, bool full)
{
    assert (self);
    bool match_group = (groupname != NULL) && (strlen(groupname) > 0);

    if (match_group) {
	// report single group
	stgroup_t *g = strack_find_group(self, groupname);
	if (g == NULL) {
	    // no such group - reply with error message on topic
	    self->update->set_type(pb::MT_STP_NOGROUP);
	    self->update->set_note("no such group: " + std::string(groupname));
	    return pack_and_send(self, groupname);
	}
	return report_group(self, g, full);
    } else {
	// report all groups
	for (stgroup_t *g = (stgroup_t *) zlist_first (self->groups);
	     g != NULL;
	     g = (stgroup_t *) zlist_next (self->groups)) {
	    report_group(self, g, full);
	}
    }
    return 0;
}

int strack_talker_exit(sttalker_t *self)
{
    assert (self);
    zstr_send (self->pipe, "EXIT");
    return retcode(self->pipe);
}


//---- internal routines --

static inline int pack_and_send(sttalker_t *self, const char *topic)
{
    zmsg_t *msg = zmsg_new();
    zmsg_pushstr(msg, topic);
    zframe_t *update_frame = zframe_new(NULL, self->update->ByteSize());
    assert(self->update->SerializeWithCachedSizesToArray(zframe_data(update_frame)));
    zmsg_add(msg, update_frame);
    assert(zmsg_send (&msg, self->update_socket) == 0);
    assert(msg == NULL);
    self->update->Clear();
    return 0;
}

static inline int report_group(sttalker_t *self, stgroup_t *g, bool full)
{
    self->update->set_type(full ? pb::MT_STP_UPDATE_FULL :
			   pb::MT_STP_UPDATE);

    for (stvar_t *v = (stvar_t *) zlist_first (g->vars);
	 v != NULL;
	 v = (stvar_t *) zlist_next (g->vars)) {
	if (full) {
	    pb::Signal *signal = self->update->add_signal();
	    switch (v->type) {
	    default:
		assert("invalid signal type" == NULL);
	    case pb::HAL_BIT:
		signal->set_halbit(*v->value.b);
		break;
	    case pb::HAL_FLOAT:
		signal->set_halfloat(*v->value.f);
		break;
	    case pb::HAL_S32:
		signal->set_hals32(*v->value.s);
		break;
	    case pb::HAL_U32:
		signal->set_halu32(*v->value.u);
		break;
	    case pb::STRING:
		signal->set_strval((const char *)*v->value.blob.bref);
		break;
	    case pb::BYTES:
		signal->set_blob(*v->value.blob.bref,
				 *v->value.blob.bsize);
		break;
	    }
	    signal->set_name(v->name);
	    signal->set_type((pb::ValueType)v->type);
	    signal->set_handle((uint32) v);
	} else {
	    switch (v->type) {
	    default:
		assert("invalid signal type" == NULL);
	    case pb::HAL_BIT:
		if (*v->value.b != v->track.b) {
		    v->track.b = *v->value.b;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halbit(*v->value.b);
		    signal->set_handle((uint32) v);
		}
		break;
	    case pb::HAL_FLOAT:
		if (*v->value.f != v->track.f) {
		    v->track.f = *v->value.f;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halfloat(*v->value.f);
		    signal->set_handle((uint32) v);
		}
		break;
	    case pb::HAL_S32:
		if (*v->value.s != v->track.s) {
		    v->track.s = *v->value.s;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_hals32(*v->value.s);
		    signal->set_handle((uint32) v);
		}
		break;
	    case pb::HAL_U32:
		if (*v->value.u != v->track.u) {
		    v->track.u = *v->value.u;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halu32(*v->value.u);
		    signal->set_handle((uint32) v);
		}
		break;
	    case pb::STRING:
		if (v->track.changed) {
		    v->track.changed = false;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_strval((const char *)*v->value.blob.bref);
		    signal->set_handle((uint32) v);
		}
		break;
	    case pb::BYTES:
		if (v->track.changed) {
		    v->track.changed = false;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_blob(*v->value.blob.bref,
				     *v->value.blob.bsize);
		    signal->set_handle((uint32) v);
		}
		break;
	    }
	}
    }
    if (self->empty_updates || (self->update->signal_size() > 0)) {
	self->update->set_serial(self->serial++);
	return pack_and_send(self, g->name);
    } else {
	self->update->Clear();
    }
    return 0;
}

// deal with service discovery probes
static void s_beacon_recv (sttalker_t * self)
{
    char buffer[8192];
    pb::Container rx;
    struct sockaddr_in remaddr = {0};
    socklen_t addrlen = sizeof(remaddr);

    if (stp_debug) fprintf(stderr, "beacon recv\n");

    size_t recvlen = recvfrom(self->beacon_fd, buffer,
			   sizeof(buffer), 0,
			   (struct sockaddr *)&remaddr, &addrlen);
    if (stp_debug) fprintf(stderr, "got %d from %s\n",
			   recvlen, inet_ntoa(remaddr.sin_addr));

    if (rx.ParseFromArray(buffer, recvlen)) {
	std::string text;
	if (stp_debug && TextFormat::PrintToString(rx, &text))
	    fprintf(stderr, "discovery request: %s\n", text.c_str());

	if (rx.type() ==  pb::MT_SERVICE_PROBE) {
	    pb::ServiceAnnouncement *sa;

	    self->update->set_type(pb::MT_SERVICE_ANNOUNCEMENT);
	    sa = self->update->add_service_announcement();
	    sa->set_instance(0);
	    sa->set_stype(pb::ST_STP);
	    sa->set_version(1);
	    sa->set_update_port(self->port);

	    size_t txlen = self->update->ByteSize();
	    assert(txlen < sizeof(buffer));
	    self->update->SerializeWithCachedSizesToArray((uint8 *)buffer);

	    struct sockaddr_in destaddr = {0};
	    destaddr.sin_port = htons(self->beacon_port);
	    destaddr.sin_family = AF_INET;
	    destaddr.sin_addr = remaddr.sin_addr;

	    if (sendto(self->beacon_fd, buffer, txlen, 0,
		       (struct sockaddr *)&destaddr, sizeof(destaddr)) < 0) {
		perror("sendto failed");
	    }
	    if (stp_debug && TextFormat::PrintToString(*self->update, &text))
		fprintf(stderr, "response: %s\n", text.c_str());
	    self->update->Clear();
	}
    } else
	if (stp_debug)
	    fprintf(stderr, "cant protobuf parse request\n");
}


static void s_subscribe_recv (sttalker_t * self)
{
    zframe_t *subscribe_msg =  zframe_recv (self->update_socket);
    unsigned char *s = zframe_data (subscribe_msg);

    if (self->subscribe_callback)
	self->subscribe_callback(self, subscribe_msg);
    else if (*s)
	strack_talker_update(self , (const char *)s+1, true);
    zframe_destroy (&subscribe_msg);
}

//  Background task:
//     command interpreter
//     service discovery
//     subscribe/unsubscribe detection
//     optional timer handling
static void s_talker_task (void *args, zctx_t *ctx, void *pipe)
{
    sttalker_t *self = (sttalker_t *) args;
    int n_items = 2;

    self->update_socket = zsocket_new (ctx, ZMQ_XPUB);
    zsocket_set_linger (self->update_socket, 0);
    zsocket_set_xpub_verbose (self->update_socket, 1);
    self->port = zsocket_bind(self->update_socket, self->uri);

    if (self->beacon_port) {
	// setup service discovery listener
	if ((self->beacon_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
	    perror("socket");
	    zstr_send (pipe, "%d", self->beacon_fd);
	    return;
	}
	struct sockaddr_in sd_addr =  {0};
	sd_addr.sin_family = AF_INET;
	sd_addr.sin_port = htons(self->beacon_port);
	sd_addr.sin_addr.s_addr = INADDR_BROADCAST;

	int on = 1;
	//  since there might be several servers on a host,
	//  allow multiple owners to bind to socket; incoming
	//  messages will replicate to each owner
	if (setsockopt (self->beacon_fd, SOL_SOCKET, SO_REUSEADDR,
			(char *) &on, sizeof (on)) == -1) {
	    perror("setsockopt (SO_REUSEADDR)");
	    zstr_send (pipe, "-1");
	    return;
	}
#if defined (SO_REUSEPORT)
	//
	// On some platforms we have to ask to reuse the port
	// as of linux 3.9 - ignore failure for earlier kernels
	setsockopt (self->beacon_fd, SOL_SOCKET, SO_REUSEPORT,
		    (char *) &on, sizeof (on));
#endif
	// setsockopt(SO_BROADCAST) not needed since we're using directed replies
	if (bind(self->beacon_fd, (struct sockaddr*)&sd_addr, sizeof(sd_addr) ) == -1) {
	    perror("bind");
	    zstr_send (pipe, "-1");
	    return;
	}
	n_items = 3;
    }
    if (self->port < 0) {
	zstr_send (pipe, "%d", self->port);
	return;
    }
    // good to go
    zstr_send (pipe, "0");

    int retval;
    while (!zctx_interrupted) {
        //  Poll on API pipe, update socket,
	//  and beacon socket if beacon_port given
        zmq_pollitem_t pollitems [] = {
            { pipe, 0, ZMQ_POLLIN, 0 },
            { self->update_socket, 0, ZMQ_POLLIN, 0 },
	    { NULL, self->beacon_fd, ZMQ_POLLIN, 0 }
        };
        retval = zmq_poll (pollitems, n_items, self->interval * ZMQ_POLL_MSEC);
	if (retval == 0) {
	    // timer expired - incremental update of all groups
	    strack_talker_update(self, NULL, false);
	}
	if (retval == -1)
	    break;

        if (pollitems [0].revents & ZMQ_POLLIN) {
	    char *cmd = zstr_recv (pipe);
	    if (stp_debug)
		fprintf(stderr, "cmd=%s\n", cmd);
	    if (strcmp(cmd, "EXIT")) {
		zstr_free (&cmd);
		zstr_send (pipe, "0");
		return;
	    }
	    zstr_free (&cmd);
	    zstr_send (pipe, "0");
	}
        if (pollitems [1].revents & ZMQ_POLLIN)
            s_subscribe_recv (self);
        if (pollitems [2].revents & ZMQ_POLLIN)
            s_beacon_recv (self);
     }
}
#endif
