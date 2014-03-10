
#include <stdio.h>
#include <errno.h>
#include <string>
#include <uuid/uuid.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#include <google/protobuf/text_format.h>
#include <middleware/generated/message.pb.h>
#include <sdpublish.h>

namespace gpb = google::protobuf;

static void s_publisher_task (void *args, zctx_t *ctx, void *pipe);
static int s_retcode(void *pipe);

typedef struct _spub {
    int instance;
    int port;
    pb::Container *tx;
    void *pipe;
    zctx_t *ctx;
    bool trace;
    uuid_t uuid;
} _spub_t;


spub_t *
sp_new(zctx_t *ctx, int port, int instance, unsigned char *uuid)
{
    _spub_t *self = (_spub_t *) zmalloc(sizeof(_spub_t));
    assert (self);

    self->ctx = ctx;
    self->port = port > 0 ? port : SERVICE_DISCOVERY_PORT;
    self->instance = instance;
    self->tx = new pb::Container();
    self->tx->set_type(pb::MT_SERVICE_ANNOUNCEMENT);
    if (uuid)
	self->tx->set_uuid(uuid, sizeof(uuid_t));
    return self;
}

int
sp_add(spub_t *self,
       int stype,
       int version,
       const char *ipaddr,
       int port,
       const char *uri,
       int api,
       const char *description)
{
    pb::ServiceAnnouncement *sa;

    assert (self);
    sa = self->tx->add_service_announcement();
    sa->set_instance(self->instance);
    sa->set_stype((pb::ServiceType)stype);
    sa->set_version(version);
    sa->set_uri(uri);
    sa->set_api((pb::ServiceAPI)api);
    if (description)
	sa->set_description(description);
    return 0;
}

// start the service publisher
int
sp_start(spub_t *self)
{
    assert(self);

    //  Start background agent
    self->pipe = zthread_fork (self->ctx, s_publisher_task, self);
    assert(self->pipe);

    //  and wait for it to initialize
    return s_retcode(self->pipe);
}

// turn on/off logging for service requests
void
sp_log(spub_t *self, int trace)
{
    assert(self);
    self->trace = (trace != 0);
}

// terminate the service publisher
int
sp_destroy(_spub_t **arg)
{
    int retval = -ENOENT;

    if (arg == NULL) return 0;
    if (*arg == NULL) return 0;

    _spub_t *self = *arg;
    if (self->pipe) {
	if (self->trace)
	    fprintf(stderr, "sp_destroy: send EXIT\n");
	zstr_send (self->pipe, "EXIT");
	retval =  s_retcode(self->pipe);
	if (self->trace)
	    fprintf(stderr, "sp_destroy: got %d\n", retval);
    }
    if (self->tx)
	delete  self->tx;
    free(self);
    *arg = NULL;
    return retval;
}

// --- end of public API ---

static int s_cmdpipe_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg);
static int s_register_service_discovery(int sd_port);
static int s_socket_readable(zloop_t *loop, zmq_pollitem_t *poller, void *arg);

static int
s_retcode(void *pipe)
{
    char *retval = zstr_recv (pipe);
    int rc = atoi(retval);
    zstr_free(&retval);
    return rc;
}

static void
s_publisher_task (void *args, zctx_t *ctx, void *pipe)
{
    int retval;

    _spub_t *self = (_spub_t *) args;
    int fd;
    if (self->trace)
	fprintf(stderr, "s_publisher_task: startup\n");

    zloop_t *loop =  zloop_new();
    assert(loop != NULL);
    zloop_set_verbose (loop, self->trace);

    if ((fd = s_register_service_discovery(self->port)) < 0) {
	// fail sp_start()
	if (self->trace)
	    fprintf(stderr, "s_publisher_task: s_register_service_discovery FAIL\n");
	zstr_sendf (pipe, "%d", fd);
	return;
    }

    // watch pipe for API commands
    zmq_pollitem_t cmditem =  { pipe, 0, ZMQ_POLLIN, 0 };
    zmq_pollitem_t rxitem =   { 0, fd, ZMQ_POLLIN, 0 };

    assert(zloop_poller (loop, &cmditem, s_cmdpipe_readable, self) == 0);
    assert(zloop_poller (loop, &rxitem,  s_socket_readable, self) == 0);

    // good to go
    zstr_send (pipe, "0"); // makes sp_start() return 0

    if (self->trace)
	fprintf(stderr, "s_publisher_task: loop start\n");
    do {
        retval = zloop_start(loop);
    } while (!(retval || zctx_interrupted));

    if (self->trace)
	fprintf(stderr, "s_publisher_task: loop exit\n");

    shutdown(fd, SHUT_RDWR);
    zstr_send (pipe, "0"); // makes sp_destroy() return 0
}

static int
s_cmdpipe_readable(zloop_t *loop, zmq_pollitem_t *item, void *arg)
{
    _spub_t *self = (_spub_t *) arg;
    int retval = 0;
    char *cmd_str = zstr_recv (item->socket);
    assert(cmd_str);
    if (self->trace)
	fprintf(stderr, "s_cmdpipe_readable: got %s\n", cmd_str);
    if (!strcmp(cmd_str,"EXIT")) {
	retval = -1; // exit reactor
    }
    zstr_free(&cmd_str);
    return retval;
}

static int
s_register_service_discovery(int sd_port)
{
    int sd_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sd_fd == -1) {
	fprintf(stderr,	"s_register_service discovery: socket() failed: %s",
			strerror(errno));
	return -1;
    }

    struct sockaddr_in sd_addr =  {0};
    sd_addr.sin_family = AF_INET;
    sd_addr.sin_port = htons(sd_port);
    sd_addr.sin_addr.s_addr = INADDR_BROADCAST;
    int on = 1;
    //  since there might be several servers on a host,
    //  allow multiple owners to bind to socket; incoming
    //  messages will replicate to each owner
    if (setsockopt (sd_fd, SOL_SOCKET, SO_REUSEADDR,
		    (char *) &on, sizeof (on)) == -1) {
	fprintf(stderr,"service discovery: setsockopt (SO_REUSEADDR) failed: %s",
			strerror(errno));
	return -1;
    }
#if defined (SO_REUSEPORT)
    // On some platforms we have to ask to reuse the port
    // as of linux 3.9 - ignore failure for earlier kernels
    setsockopt (sd_fd, SOL_SOCKET, SO_REUSEPORT,
		(char *) &on, sizeof (on));
#endif
    if (::bind(sd_fd, (struct sockaddr*)&sd_addr, sizeof(sd_addr) ) == -1) {
	fprintf(stderr, "service discovery: bind() failed: %s",
		strerror(errno));
	return -1;
    }
    return sd_fd;
}

static int
s_socket_readable(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{
    _spub_t *self = (_spub_t *) arg;
    unsigned char buffer[8192];
    pb::Container rx;
    struct sockaddr_in remote_addr = {0};
    socklen_t addrlen = sizeof(remote_addr);
    int retval;

    size_t recvlen = recvfrom(poller->fd, buffer,
			      sizeof(buffer), 0,
			      (struct sockaddr *)&remote_addr, &addrlen);
    if (self->trace)
	fprintf(stderr, "s_socket_readable: request size %zu from %s:%d\n",
		recvlen, inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port));

    if (rx.ParseFromArray(buffer, recvlen)) {
	std::string text;
	if (self->trace) {
	    std::string s;
	    if (gpb::TextFormat::PrintToString(rx, &s))
		fprintf(stderr, "%s\n",s.c_str());
	}
	if (rx.type() ==  pb::MT_SERVICE_PROBE) {
	    bool trace = rx.has_trace() && rx.trace();

	    size_t txlen = self->tx->ByteSize();
	    assert(sizeof buffer >= txlen);
	    self->tx->SerializeWithCachedSizesToArray(buffer);
	    if (self->trace || trace) {
		std::string s;
		if (gpb::TextFormat::PrintToString(*self->tx, &s)) {
		    fprintf(stderr, "response:\n%s\n",s.c_str());
		}
	    }
	    struct sockaddr_in destaddr = {0};
	    destaddr.sin_port = remote_addr.sin_port;
	    destaddr.sin_family = AF_INET;
	    destaddr.sin_addr = remote_addr.sin_addr;

	    if ((retval = sendto(poller->fd, buffer, txlen, 0,
				 (struct sockaddr *)&destaddr, sizeof(destaddr))) < 0) {
		fprintf(stderr, "s_socket_readable: sendto(%s) failed: %s\n",
				inet_ntoa(remote_addr.sin_addr),
				strerror(errno));
	    } else
		if (self->trace)
		    fprintf(stderr, "s_socket_readable: sent size %zu to %s:%d:\n",
			    txlen, inet_ntoa(remote_addr.sin_addr),
			    ntohs(destaddr.sin_port));
	}
    } else {
	fprintf(stderr,
		"s_socket_readable: can parse request from %s (size %zu)\n",
		inet_ntoa(remote_addr.sin_addr),recvlen);
    }
    return 0;
}


#ifdef TEST
int main(int argc, char **argv)
{
    int rc;

    zctx_t *ctx = zctx_new();

    spub_t *sp = sp_new(ctx, 0, 0, NULL);
    assert(sp);

    rc = sp_add(sp,
		(int) pb::ST_HAL_RCOMMAND,
		2,
		NULL,
		0,
		"tcp://nowhere.com:64711",
		(int) pb::SA_ZMQ_PROTOBUF,
		"test command socket");
    assert(rc == 0);

   rc = sp_add(sp,
	       (int) pb::ST_STP_HALGROUP,
		2,
		NULL,
		0,
		"tcp://nowhere.com:64712",
		(int) pb::SA_ZMQ_PROTOBUF,
		"test status socket");

    assert(rc == 0);
    sp_log(sp, argc - 1);

    assert(sp_start(sp) == 0);
    sleep(60);
    assert(sp_destroy(&sp) == 0);
    return 0;
}
#endif
