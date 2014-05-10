// service discovery - client side API

// MAX_SERVICES must be larger than the largets enum ServiceType
// value in machinetalk/proto/types.proto

#define MAX_SERVICES 30


#define RETRY_TIME   500 //msec

#include <stdio.h>
#include <errno.h>
#include <string>
#include <sys/time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/poll.h>

#include <google/protobuf/text_format.h>
#include <machinetalk/generated/message.pb.h>

#include <sdiscover.h>

namespace gpb = google::protobuf;


typedef struct {
    unsigned stype;
    unsigned api;
    unsigned version;
    int port;
    char *ipaddress;
    char *uri;
    char *description;
    bool found;
} sdquery_t;

typedef struct _sdreq {
    int instance;
    int port;
    unsigned char *probe;
    size_t probe_len;
    sdquery_t query[MAX_SERVICES];
    bool trace;
    int outstanding;
    int retry_time;
    int sock;
} _sdreq_t;

static int s_register_sockets(sdreq_t *self);
static int s_send_probe(sdreq_t *self);

sdreq_t *sd_new(int port, int instance)
{
    int retval;


    _sdreq_t *self = (_sdreq_t *) zmalloc(sizeof(_sdreq_t));
    assert (self);
    self->port = port > 0 ? port : SERVICE_DISCOVERY_PORT;
    self->retry_time = RETRY_TIME;
    self->instance = instance;

    if ((retval = s_register_sockets(self))) {
	free(self);
	return NULL;
    }
    // ready the probe frame one - it might be reused
    pb::Container p;
    p.set_type(pb::MT_SERVICE_PROBE);
    self->probe_len = p.ByteSize();
    self->probe = (unsigned char *) zmalloc(self->probe_len);
    assert(self->probe != NULL);
    p.SerializeWithCachedSizesToArray(self->probe);

    return self;
}

int sd_socket(sdreq_t *self)
{
    assert (self);
    return self->sock;
}

void sd_log(sdreq_t *self, int trace)
{
    assert (self);
    self->trace = (trace != 0);
}

int sd_add(sdreq_t *self,
	   unsigned stype,
	   unsigned version,
	   unsigned api)
{
    assert (self);
    assert(stype >= 0);
    assert(stype < MAX_SERVICES);
    self->query[stype].stype = stype;
    self->query[stype].api = api;
    self->query[stype].version = version;
    self->query[stype].found = false;
    self->outstanding++;
    return 0;
}

void sd_dump(const char *tag, sdreq_t *self)
{
    assert (self);
    fprintf(stderr, "%s instance=%d outstanding=%d\n", tag ? tag : "",
	    self->instance, self->outstanding);
    for (int i = 0; i < MAX_SERVICES; i++) {
	sdquery_t *sq = &self->query[i];
	if (sq->found) {
	    fprintf(stderr, "stype=%d api=%d version=%d port=%d ",
		    sq->stype, sq->api, sq->version, sq->port);
	    if (sq->ipaddress) fprintf(stderr, "ipaddress=%s ", sq->ipaddress);
	    if (sq->uri) fprintf(stderr, "uri=%s ", sq->uri);
	    if (sq->description) fprintf(stderr, "description=%s ", sq->description);
	    fprintf(stderr, "\n");
	}
    }
}

void sd_destroy(sdreq_t **arg)
{
    if (arg == NULL || *arg == NULL)
	return;

    sdreq_t *self = *arg;
    for (int i = 0; i < MAX_SERVICES; i++) {
	sdquery_t *sq = &self->query[i];
	if (sq->ipaddress) free(sq->ipaddress);
	if (sq->uri) free(sq->uri);
	if (sq->description) free(sq->description);
    }
    if (self->probe) free(self->probe);
    free(self);
    *arg = NULL;
}

int sd_send_probe(sdreq_t *self)
{
    return s_send_probe(self);
}

static int s_msec_since(struct timeval *start);
static int s_find_matches(sdreq_t *self, unsigned char *buffer, size_t len);

int sd_query(sdreq_t *self, int timeoutms)
{
    unsigned char buffer[8192];
    struct timeval start;
    int retval;

    assert (self);
    struct pollfd pfd = { self->sock, POLLIN, 0 };
    gettimeofday(&start, NULL);

    if ((retval = s_send_probe(self)))
	return retval;

    while (self->outstanding > 0) {
	if (s_msec_since(&start) > timeoutms)
	    return -ETIMEDOUT;

	pb::Container rx;
	struct sockaddr_in remote_addr = {0};
	socklen_t addrlen = sizeof(remote_addr);

	int ready = poll(&pfd, 1, self->retry_time);
	if (ready > 0) {
	    if (pfd.revents & POLLIN) {
		size_t len = recvfrom(self->sock, buffer, sizeof(buffer), 0,
				      (struct sockaddr *)&remote_addr, &addrlen);

		if (self->trace)
		    fprintf(stderr, "sd_query: received size %zu from %s:%d:\n",
			    len, inet_ntoa(remote_addr.sin_addr),
			    ntohs(remote_addr.sin_port));
		self->outstanding -= s_find_matches(self, buffer, len);
	    }
	} else {
	    // timeout, resend probe broadcast
	    if (self->trace)
		fprintf(stderr, "sd_query: resend probe broadcast\n");
	    if ((retval = s_send_probe(self)))
		return retval;
	}
    }
    return 0;
}

// --- end of public API ---
static int s_find_matches(sdreq_t *self, unsigned char *buffer, size_t len)
{
    pb::Container reply;
    unsigned stype;
    int matches = 0;

    if (reply.ParseFromArray((const void *) buffer, len)) {
	if (self->trace) {
	    std::string s;
	    if (gpb::TextFormat::PrintToString(reply, &s))
		fprintf(stderr, "reply:\n%s",s.c_str());
	}
	if (reply.type() ==  pb::MT_SERVICE_ANNOUNCEMENT) {

	    // loop over announcments
	    for (int i = 0; i < reply.service_announcement_size(); i++) {
		pb::ServiceAnnouncement const &sa = reply.service_announcement(i);
		stype = sa.stype();

		if (stype < 0 || stype > MAX_SERVICES-1) {
		    fprintf(stderr, "s_resolve_reply: stype out of range: %d\n",stype);
		    return -1;
		}
		// are we interested in this one?
		sdquery_t *sq = &self->query[stype];
		if (!sq->found &&
		    (stype == sq->stype) &&
		    (sa.version() >= sq->version) &&
		    (sa.api() >= sq->api)) {

		    sq->found = true; // first matching responder wins
		    sq->uri = strdup(sa.uri().c_str());
		    if (sa.has_description())
			sq->description = strdup(sa.description().c_str());
		    sq->api = sa.api();
		    matches++;
		}
	    }
	    return matches;
	}
    }
    return 0;
}

static int s_send_probe(sdreq_t *self)
{
    int retval;
    struct sockaddr_in destaddr = {0};

    // broadcast the query frame
    destaddr.sin_port = htons(self->port);
    destaddr.sin_family = AF_INET;
    destaddr.sin_addr.s_addr  = INADDR_BROADCAST;
    retval = sendto(self->sock, self->probe, self->probe_len, 0,
		    (struct sockaddr *)&destaddr, sizeof(destaddr));
    if (retval < 0) {
	fprintf(stderr, "s_send_probe: sendto(%d) failed: %s %d\n",
		self->probe_len,strerror(errno), retval);
	return retval;
    } else if (self->trace)
	     fprintf(stderr, "s_send_probe: sent size %zu to %s:%d:\n",
		     self->probe_len, inet_ntoa(destaddr.sin_addr),
			    ntohs(destaddr.sin_port));
    return 0;
}


static int s_msec_since(struct timeval *start)
{
    struct timeval end;
    int startms, endms;

    gettimeofday(&end, NULL);
    startms = start->tv_sec * 1000 + start->tv_usec/1000;
    endms = end.tv_sec * 1000 + end.tv_usec/1000;
    return  endms - startms;
}

static int s_register_sockets(sdreq_t *self)
{
    self->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (self->sock < 0) {
	fprintf(stderr,	"s_register_socket: socket() failed: %s",
			strerror(errno));
	return -1;
    }
    int on = 1;

    struct sockaddr_in sd_addr =  {0};
    sd_addr.sin_family = AF_INET;
    // bind to ephemeral local port
    sd_addr.sin_addr.s_addr = INADDR_ANY;

    //  since there might be several servers on a host,
    //  allow multiple owners to bind to socket; incoming
    //  messages will replicate to each owner
    if (setsockopt (self->sock, SOL_SOCKET, SO_REUSEADDR,
		    (char *) &on, sizeof (on)) == -1) {
	fprintf(stderr,"s_register_socket: setsockopt (SO_REUSEADDR) failed: %s",
			strerror(errno));
	return -1;
    }
#if defined (SO_REUSEPORT)
    // On some platforms we have to ask to reuse the port
    // as of linux 3.9 - ignore failure for earlier kernels
    setsockopt (self->sock, SOL_SOCKET, SO_REUSEPORT,
		(char *) &on, sizeof (on));
#endif
    setsockopt (self->sock, SOL_SOCKET, SO_BROADCAST,
		(char *) &on, sizeof (on));

    if (::bind(self->sock, (struct sockaddr*)&sd_addr, sizeof(sd_addr) ) == -1) {
	fprintf(stderr, "s_register_socket: bind() failed: %s",
		strerror(errno));
	return -1;
    }
    return 0;
}

static sdquery_t *find_service(sdreq_t *self, unsigned stype)
{
    if (self == NULL) return NULL;
    for (int i = 0; i < MAX_SERVICES; i++) {
	sdquery_t *sq = &self->query[i];
	if (sq->found &&  (sq->stype == stype))
	    return sq;
    }
    return NULL;
}


int sd_version(sdreq_t *self, int stype)
{
    sdquery_t *s = find_service(self, stype);
    if (s == NULL) return -1;
    return s->version;
}

const char *sd_uri(sdreq_t *self, int stype)
{
    sdquery_t *s = find_service(self, stype);
    if (s == NULL) return NULL;
    return s->uri;
}

const char *sd_description(sdreq_t *self, int stype)
{
    sdquery_t *s = find_service(self, stype);
    if (s == NULL) return NULL;
    return s->description;
}

#ifdef TEST
int main(int argc, char **argv)
{
    int rc;

    sdreq_t *sd = sd_new(0, 0);
    assert(sd);

    rc = sd_add(sd,  pb::ST_HAL_RCOMMAND,
		0,  pb::SA_ZMQ_PROTOBUF);
    assert(rc == 0);
    rc = sd_add(sd,  (int) pb::ST_STP_HALGROUP,
		0,  pb::SA_ZMQ_PROTOBUF);
    assert(rc == 0);

    sd_dump("before query: ", sd);
    sd_log(sd, argc - 1);

    sd_query(sd, 3000);

    sd_dump("after query: ", sd);

    sd_destroy(&sd);
    return 0;

}
#endif
