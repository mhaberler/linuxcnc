// remotely track hal groups

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <getopt.h>

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

#include <inifile.hh>
#include <inifile.h>
#include <czmq.h>
#include <pthread.h>
#include <hal.h>  // hal_bit_t etc
#include <hal_priv.h>  // hal_data_u
#include <rcs.hh>  // RCS_DONE etc

#include <google/protobuf/text_format.h>
#include <protobuf/generated/message.pb.h>

typedef std::vector<std::string> string_array;

typedef struct halvalue {
    int type;
    hal_data_u value;
    void *ref;
} value_t;

typedef std::unordered_map<int,value_t> value_map;
typedef std::unordered_map<std::string,int> name_map;
typedef std::unordered_map<std::string,int>::iterator name_map_iterator;
typedef int (*update_cb)(void *self);

typedef struct htconf {
    const char *progname;
    const char *inifile;
    const char *section;
    int debug;
    int pid;
    string_array sockets;
    string_array topics;
} htconf_t;

typedef struct htself {
    htconf_t *cfg;
    zctx_t *z_context;
    void *z_status;
    zloop_t *z_loop;
    pb::Container update;
    pthread_t listener_thread;
    value_map values;
    name_map names;
    update_cb callback;
} htself_t;


using namespace google::protobuf;
using namespace std;

static const char *option_string = "hI:S:d";
static struct option long_options[] = {
    {"help", no_argument, 0, 'h'},
    {"ini", required_argument, 0, 'I'},     // default: getenv(INI_FILE_NAME)
    {"section", required_argument, 0, 'S'},
    {"debug", required_argument, 0, 'd'},
    {0,0,0,0}
};

htconf_t conf = {
    "haltracker",
    NULL,
    "HALTRACKER",
    0,
    0,
    {},
    {}
};

static void listener_cleanup(void *arg)
{
    htself_t *self = (htself_t *)arg;

    if (self->cfg->debug)
	fprintf(stderr, "listener_cleanup() called\n");
    zsocket_destroy (self->z_context, self->z_status);
    zctx_destroy (&self->z_context);
}

static void *
listener (void *arg)
{
    htself_t *self = (htself_t *)arg;
    int rc;
    unsigned i;
    int ticket = 0;
    int status = RCS_DONE;

    self->z_context = zctx_new ();
    self->z_status = zsocket_new (self->z_context, ZMQ_SUB);
    zsocket_set_linger (self->z_status, 0);
    pthread_cleanup_push(listener_cleanup, self);

    for (i = 0; i < self->cfg->sockets.size(); i++) {
	if (self->cfg->debug)
	    fprintf(stderr, "connect: '%s'\n", self->cfg->sockets[i].c_str());
	rc = zsocket_connect(self->z_status, self->cfg->sockets[i].c_str());
	assert (rc == 0);
    }
    for (i = 0; i < self->cfg->topics.size(); i++) {
	if (self->cfg->debug)
	    fprintf(stderr, "topic: '%s'\n", self->cfg->topics[i].c_str());
	// silly ini parser doesnt support zero-length strings
	if (self->cfg->topics[i] == "*")
	    zsocket_set_subscribe (self->z_status, "");
	else
	    zsocket_set_subscribe (self->z_status, self->cfg->topics[i].c_str());
    }

    do {
	zmsg_t *m_update = zmsg_recv (self->z_status);
	pb::Container update;
        char *dest = zmsg_popstr (m_update);
	zframe_t *pb_update  = zmsg_pop (m_update);

	assert(update.ParseFromArray(zframe_data(pb_update),
				     zframe_size(pb_update)));

	if (self->cfg->debug > 5) {
	    string buffer;
	    if (TextFormat::PrintToString(update, &buffer)) {
		fprintf(stderr, "msg: %s\n",buffer.c_str());
	    }
	}

	switch (update.type()) {
	case pb::MT_HALUPDATE_FULL:
	case pb::MT_HALUPDATE:
	    {
		for (int i = 0; i < update.signal_size(); i++) {
		    pb::Signal const &s = update.signal(i);
		    value_t v;
		    if (update.type() ==  pb::MT_HALUPDATE_FULL) {
			self->names[s.name()] = s.handle();
			v.type = s.type();
			v.ref = NULL;
			if (self->cfg->debug ) {
			    fprintf(stderr, "newsig: %s %d\n", s.name().c_str(), s.type());
			}
		    } else
			v = self->values[s.handle()];

		    switch (v.type) {
		    default:
			assert("invalid signal type" == NULL);
		    case HAL_BIT:
			v.value.b = s.halbit();
			if (v.ref)
			    *(bool *)v.ref = v.value.b;
			break;
		    case HAL_FLOAT:
			v.value.f = s.halfloat();
			if (v.ref)
			    *(double *)v.ref = v.value.f;
			break;
		    case HAL_S32:
			v.value.s = s.hals32();
			if (v.ref)
			    *(int *)v.ref = v.value.s;
			break;
		    case HAL_U32:
			v.value.u = s.halu32();
			if (v.ref)
			    *(unsigned *)v.ref = v.value.u;
			break;
		    }
		    self->values[s.handle()] = v;
		}
	    }
	    break;
	case pb::MT_TICKET_UPDATE:
	    if (update.has_ticket_update()) {
		// track  non-closed tickets
		if ((update.ticket_update().cticket() >  ticket) &&
		    (status != RCS_DONE)) {
		    fprintf(stderr,
			    "----- old ticket %d status %d new ticket %d owner '%s'\n",
			    ticket, status, update.ticket_update().cticket(),
			    dest);
		}

		ticket = update.ticket_update().cticket(),
		status = update.ticket_update().status(),
		fprintf(stderr,"ticket %d %d owner '%s'\n",
			ticket, status, dest);
	    }
	    break;
	default:
	    break;
	}
	if (self->callback)
	    self->callback(self);

	zmsg_destroy(&m_update);
	free(dest);
    } while (!zctx_interrupted);
    static int cleanup_pop_arg;
    pthread_cleanup_pop(cleanup_pop_arg);
    return NULL;
}

int track( htself_t *self, int type, const char *signame, void *ref)
{
    name_map_iterator it = self->names.find(signame);
    if (it == self->names.end()) {
	fprintf(stderr,"no such signal: '%s'\n", signame);
	return -ENOENT;
    }
    int handle = (*it).second;

    value_t v = self->values[handle];
    if (v.type != type) {
	fprintf(stderr,"track(%s): type mismatch %d != %d\n",
		signame, type, v.type);
	return -1;
    }
    v.ref = ref;
    self->values[handle]  = v;
    switch (v.type) {
    default:
	assert("invalid signal type" == NULL);
    case HAL_BIT:
	*(bool *)v.ref = v.value.b;
	break;
    case HAL_FLOAT:
	*(double *)v.ref = v.value.f;
	break;
    case HAL_S32:
	*(int *)v.ref = v.value.s;
	break;
    case HAL_U32:
	*(unsigned *)v.ref = v.value.u;
	break;
    }
    return 0;
}

const char *iniFind(FILE *fp, const char *tag, const char *section, int num)
{
    IniFile f(false, fp);

    return(f.Find(tag, section, num, NULL));
}

static int read_config(htconf_t *conf)
{
    const char *s;
    FILE *inifp;

    if (!conf->inifile) {
	fprintf(stderr,
		"%s: need an inifile - either --ini <inifile>"
		" or env INI_FILE_NAME=<inifile>\n",
		conf->progname);
	return -1;
    }
    if ((inifp = fopen(conf->inifile,"r")) == NULL) {
	fprintf(stderr, "%s: cant open inifile '%s'\n",
		conf->progname, conf->inifile);
	return -1;
    }
    int count = 1;
    while ((s = iniFind(inifp, "SOCKET", conf->section, count)) != NULL) {
	conf->sockets.push_back(s);
	count++;
    }
    count = 1;
    while ((s = iniFind(inifp, "TOPIC",  conf->section, count)) != NULL) {
	conf->topics.push_back(s);
	count++;
    }
    if (count == 1) // no TOPIC given - subscribe all topics
	conf->topics.push_back("*");

    fclose(inifp);
    return 0;
}

static void usage(void) {
    printf("Usage:  haltracker [options]\n");
    printf("This is a userspace HAL program, typically loaded "
	   "using the halcmd \"loadusr\" command:\n"
	   "    loadusr haltalker [options]\n"
	   "Options are:\n"
	   "-I or --ini <inifile>\n"
	   "    Use <inifile> (default: take ini filename from environment"
	   " variable INI_FILE_NAME)\n"
	   "-S or --section <section-name> (default 8)\n"
	   "    Read parameters from <section_name> (default 'VFS11')\n"
	   "-d <level> or --debug <level>\n"
	   "    Turn on event debugging messages.\n");
}

double xpos,ypos, zpos;

int update_callback(htself_t *self)
{
    fprintf(stderr, "%f %f %f\n", xpos,ypos, zpos);
    return 0;
}

int main (int argc, char *argv[ ])
{
    int opt;

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    conf.progname = argv[0];
    conf.inifile = getenv("INI_FILE_NAME");
    while ((opt = getopt_long(argc, argv, option_string,
			      long_options, NULL)) != -1) {
	switch(opt) {
	case 'd':
	    conf.debug = atoi(optarg);
	    break;
	case 'S':
	    conf.section = optarg;
	    break;
	case 'I':
	    conf.inifile = optarg;
	    break;
	case 'h':
	default:
	    usage();
	    exit(0);
	}
    }
    conf.pid = getpid();

    if (read_config(&conf))
	exit(1);

    htself_t self = {0};
    self.cfg = &conf;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    assert(pthread_create(&self.listener_thread, &attr, listener, &self) == 0);
    pthread_attr_destroy(&attr);

    sleep(1);

    track(&self, HAL_FLOAT, "halui_axis_0_pos-commanded", &xpos);
    track(&self, HAL_FLOAT, "halui_axis_1_pos-commanded", &ypos);
    track(&self, HAL_FLOAT, "halui_axis_2_pos-commanded", &zpos);
    update_callback(&self);  // show values post-registration
    self.callback = (update_cb) update_callback;

    sleep(100);

    exit(0);
}
