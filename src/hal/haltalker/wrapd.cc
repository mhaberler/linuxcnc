// test nanopb autowrap/unwrap

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <sys/signalfd.h>
#include <errno.h>
#include <getopt.h>

#include <string>

#include <czmq.h>
#include <middleware/include/pb-linuxcnc.h>
#include <middleware/nanopb/pb_decode.h>
#include <middleware/nanopb/pb_encode.h>
#include <middleware/generated/message.npb.h>
//using namespace pb;

zctx_t *z_context;
void *z_command;
zloop_t *z_loop;
const char *uri = "tcp://127.0.0.1:10042";
int debug = 1;
pb_Container rx;

static void format_pose(char *buf, unsigned long int size,  pb_EmcPose *p)
{
    unsigned sz = size;
    char *b = buf; // FIXME guard against buffer overflow
    int n;

    pb_PmCartesian *c = &p->tran;
    if (c->has_x) { n = snprintf(b, sz, "x=%f ", c->x); b += n; }
    if (c->has_y) { n = snprintf(b, sz, "y=%f ", c->y); b += n; }
    if (c->has_z) { n = snprintf(b, sz, "z=%f ", c->z); b += n; }
    if (p->has_a) { n = snprintf(b, sz, "a=%f ", p->a); b += n; }
    if (p->has_b) { n = snprintf(b, sz, "b=%f ", p->b); b += n; }
    if (p->has_c) { n = snprintf(b, sz, "c=%f ", p->c); b += n; }
    if (p->has_u) { n = snprintf(b, sz, "u=%f ", p->u); b += n; }
    if (p->has_v) { n = snprintf(b, sz, "v=%f ", p->v); b += n; }
    if (p->has_w) { n = snprintf(b, sz, "w=%f ", p->w); b += n; }
}
static int handle_command(zloop_t *loop, zmq_pollitem_t *poller, void *arg)
{

    zmsg_t *m_cmd = zmsg_recv(poller->socket);
    //pb::Container update;
    char *src = zmsg_popstr (m_cmd);
    zframe_t *pb_cmd  = zmsg_pop (m_cmd);

    printf("command: got '%s', %d\n",src, zframe_size(pb_cmd));

    pb_istream_t stream = pb_istream_from_buffer((uint8_t *) zframe_data(pb_cmd),
						 zframe_size(pb_cmd));
    if (!pb_decode(&stream, pb_Container_fields, &rx)) {

	printf( "pb_decode(Container) failed: '%s'\n",
		PB_GET_ERROR(&stream));
	return -1;
    }
    if (rx.has_test1) {
	char buf[200];
	format_pose(buf, sizeof(buf), &rx.test1.end);

	printf("--test1.op = %d end=%s\n", rx.test1.op, buf);
    }
    stream = pb_istream_from_buffer((uint8_t *) zframe_data(pb_cmd),
				    zframe_size(pb_cmd));

    // wiretype=0 (varint) tag=1 (ContainerType field #)
    // type=5001 field value
    // decode the type tag
    uint32_t tag;
    pb_wire_type_t wiretype;
    bool eof;
    if (!pb_decode_tag(&stream, &wiretype, &tag, &eof))  {
	printf("Parsing tag#1 failed: %s\n", PB_GET_ERROR(&stream));
	return 0;
    }
    uint64_t type;
    if (!pb_decode_varint(&stream, &type)) {
	printf("Parsing required type field#1 failed: %s\n", PB_GET_ERROR(&stream));
	return 0;
    }
    printf("wiretype=%d tag=%d eof=%s type=%llu\n",
	   wiretype, tag, eof ? "TRUE":"FALSE", type);


    if (!pb_decode_tag(&stream, &wiretype, &tag, &eof))  {
	printf("Parsing tag#2 failed: %s\n", PB_GET_ERROR(&stream));
    }
    // if wt==2: varint=len
    uint64_t msglen;
    if (!pb_decode_varint(&stream, &msglen)) {
	printf("Parsing required type field#1 failed: %s\n", PB_GET_ERROR(&stream));
	return 0;
    }
    printf("wiretype=%d tag=%d eof=%s msglen=%llu\n",
	   wiretype, tag, eof ? "TRUE":"FALSE", msglen);

// --test1.op = 10 end=x=1.000000 y=2.000000 z=3.000000 a=3.141500
// wiretype=0 tag=1 eof=FALSE type=5001
// wiretype=2 tag=5001 eof=FALSE msgtype=46

    return 0;
}

static int mainloop()
{
    int retval;

    zmq_pollitem_t command_poller = { z_command, 0, ZMQ_POLLIN };

    z_loop = zloop_new();
    assert (z_loop);
    zloop_set_verbose (z_loop, debug);
    zloop_poller(z_loop, &command_poller, handle_command, NULL);

    // handle signals && profiling properly
    do {
	retval = zloop_start(z_loop);
    } while  (!(retval || zctx_interrupted));
    return 0;
}

static int setup_zmq()
{
    // fprintf(stderr, "czmq version: %d.%d.%d\n",
    // 	    CZMQ_VERSION_MAJOR, CZMQ_VERSION_MINOR,CZMQ_VERSION_PATCH);

    int rc;

    z_context = zctx_new ();
    z_command = zsocket_new (z_context, ZMQ_ROUTER);
    zsocket_set_linger(z_command, 0);
    rc = zsocket_bind(z_command, uri);
    assert (rc != 0);
    return 0;
}

int main (int argc, char *argv[ ])
{


    if (! setup_zmq())
    mainloop();

    exit(0);
}
