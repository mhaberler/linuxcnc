#include <stdio.h>
#include <string>
#include <stptalker.h>
#include <stptalker-private.h>

#include<arpa/inet.h>
#include<sys/socket.h>

#include <google/protobuf/text_format.h>
#include <middleware/generated/message.pb.h>

namespace gpb = google::protobuf;


static void s_talker_task (void *args, zctx_t *ctx, void *pipe);
static inline int report_group(sttalker_t *self, stgroup_t *g, bool full);
static inline int pack_and_send(sttalker_t *self, const char *topic);

int stp_debug = 0;
static unsigned next_handle = 1;

stvar_t *strack_double(const char *name, double *dref)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_FLOAT;
    self->value.f = dref;
    self->track.f = *dref;
    self->handle = next_handle++;
    return self;
}

stvar_t *strack_s32(const char *name, int *sref)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_S32;
    self->value.s = sref;
    self->track.s = *sref;
    self->handle = next_handle++;
    return self;
}

stvar_t *strack_u32(const char *name, int *uref)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_U32;
    self->value.u = uref;
    self->track.u = *uref;
    self->handle = next_handle++;
    return self;
}

stvar_t *strack_bool(const char *name, bool *bref)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::HAL_BIT;
    self->value.b = bref;
    self->track.b = *bref;
    self->handle = next_handle++;
    return self;
}

stvar_t *strack_string(const char *name, const void **sref)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::STRING;
    self->value.blob.bref = sref;
    self->value.blob.bsize = 0;
    self->track.changed = false;
    self->handle = next_handle++;
    return self;
}

stvar_t *strack_blob(const char *name, const void **bref, size_t *bsize)
{
    stvar_t *self = (stvar_t *) zmalloc (sizeof (stvar_t));
    assert (self);
    self->name = name;
    self->type = pb::BYTES;
    self->value.blob.bref = bref;
    self->value.blob.bsize = bsize;
    self->track.changed = false;
    self->handle = next_handle++;
    return self;
}

void strack_variable_changed(stvar_t *v)
{
    assert(v);
    assert((v->type == pb::STRING) || (v->type == pb::BYTES));
    v->track.changed = true;
}

stgroup_t *strack_group_new(const char *name, int interval)
{
    stgroup_t *self = (stgroup_t *) zmalloc (sizeof (stgroup_t));
    assert (self);
    self->name = name;
    self->vars = zlist_new ();
    return self;
}

void strack_set_epsilon(sttalker_t *self, double epsilon)
{
    assert (self);
    self->epsilon = epsilon;
}

void strack_set_empty_updates(sttalker_t *self, bool empty)
{
    assert (self);
    self->empty_updates = empty;
}

int strack_group_add(stgroup_t *self, stvar_t *v)
{
    zlist_append (self->vars, v);
    return 0;
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
		      int interval,
		      int beacon_port,
		      int stp_service,
		      subscribe_cb *callback)
{
    assert (ctx);
    self->interval = interval;
    self->uri = uri;
    self->beacon_port = beacon_port;
    self->stp_service = stp_service;
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
	    self->update->add_note("no such group: " + std::string(groupname));
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
	    signal->set_handle(v->handle);
	} else {
	    switch (v->type) {
	    default:
		assert("invalid signal type" == NULL);
	    case pb::HAL_BIT:
		if (*v->value.b != v->track.b) {
		    v->track.b = *v->value.b;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halbit(*v->value.b);
		    signal->set_handle(v->handle);
		}
		break;
	    case pb::HAL_FLOAT:
		if (*v->value.f != v->track.f) {
		    v->track.f = *v->value.f;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halfloat(*v->value.f);
		    signal->set_handle(v->handle);
		}
		break;
	    case pb::HAL_S32:
		if (*v->value.s != v->track.s) {
		    v->track.s = *v->value.s;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_hals32(*v->value.s);
		    signal->set_handle(v->handle);
		}
		break;
	    case pb::HAL_U32:
		if (*v->value.u != v->track.u) {
		    v->track.u = *v->value.u;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_halu32(*v->value.u);
		    signal->set_handle(v->handle);
		}
		break;
	    case pb::STRING:
		if (v->track.changed) {
		    v->track.changed = false;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_strval((const char *)*v->value.blob.bref);
		    signal->set_handle(v->handle);
		}
		break;
	    case pb::BYTES:
		if (v->track.changed) {
		    v->track.changed = false;
		    pb::Signal *signal = self->update->add_signal();
		    signal->set_blob(*v->value.blob.bref,
				     *v->value.blob.bsize);
		    signal->set_handle(v->handle);
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
    if (stp_debug) fprintf(stderr, "got %zu from %s\n",
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
	    sa->set_stype((pb::ServiceType)self->stp_service);
	    sa->set_version(1);
	    sa->set_uri("FIXME stptalker.cc");

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

    usleep(200 *1000); // avoid slow joiner syndrome

    if (self->beacon_port) {
	// setup service discovery listener
	if ((self->beacon_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
	    perror("socket");
	    zstr_sendf (pipe, "%d", self->beacon_fd);
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
	if (::bind(self->beacon_fd, (struct sockaddr*)&sd_addr, sizeof(sd_addr) ) == -1) {
	    perror("bind");
	    zstr_send (pipe, "-1");
	    return;
	}
	n_items = 3;
    }
    if (self->port < 0) {
	zstr_sendf (pipe, "%d", self->port);
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
