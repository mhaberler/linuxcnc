#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <assert.h>
#include <errno.h>
#include <czmq.h>

#ifndef ULAPI
#error This is intended as a userspace component only.
#endif

#include <rtapi.h>
#include <rtapi_hexdump.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_ring.h>

#include <middleware/include/pb-linuxcnc.h>
#include <middleware/nanopb/pb_decode.h>
#include <middleware/nanopb/pb_encode.h>
#include <middleware/include/container.h>

#include "messagebus.hh"
#include "rtproxy.hh"

// inproc variant for comms with RT proxy threads
// defined in messagbus.cc
extern const char *proxy_cmd_uri;
extern const char *proxy_response_uri;

static int test_decode(zframe_t *f, const pb_field_t *fields);
static zframe_t *test_encode(const void *msg, const pb_field_t *fields);

int
send_subscribe(void *socket, const char *topic)
{
    size_t topiclen = strlen(topic);
    zframe_t *f = zframe_new (NULL, topiclen + 1 );
    assert(f);

    unsigned char *data = zframe_data(f);
    *data++ = '\001';
    memcpy(data, topic, topiclen);
    return zframe_send (&f, socket, 0);
}

void
rtproxy_thread(void *arg, zctx_t *ctx, void *pipe)
{
    rtproxy_t *self = (rtproxy_t *) arg;
    int retval;

    self->proxy_cmd = zsocket_new (ctx, ZMQ_XSUB);
    retval = zsocket_connect(self->proxy_cmd, proxy_cmd_uri);
    assert(retval == 0);

    self->proxy_response = zsocket_new (ctx, ZMQ_XSUB);
    assert(zsocket_connect(self->proxy_response, proxy_response_uri) == 0);

    if (self->flags & (ACTOR_RESPONDER|ACTOR_ECHO)) {
	retval = send_subscribe(self->proxy_cmd, self->name);
	assert(retval == 0);
    }

    if (self->flags & ACTOR_INJECTOR) {
	retval = send_subscribe(self->proxy_response, self->name);
	assert(retval == 0);
    }

    if (self->to_rt_name) {
	unsigned flags;
	if ((retval = hal_ring_attach(self->to_rt_name, &self->to_rt,
				       &flags))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_attach(%s) failed - %d\n",
			    progname, self->to_rt_name, retval);
	    return;
	}
	self->to_rt.header->writer = comp_id;
    }

    if (self->from_rt_name) {
	unsigned flags;

	if ((retval = hal_ring_attach(self->from_rt_name, &self->from_rt,
				      &flags))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_attach(%s) failed - %d\n",
			    progname, self->from_rt_name, retval);
	    return;
	}
	self->from_rt.header->reader = comp_id;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s startup", progname, self->name);

    if (self->flags & ACTOR_ECHO) {
	while (1) {
	    zmsg_t *msg = zmsg_recv(self->proxy_cmd);
	    if (msg == NULL)
		break;
	    if (self->flags & ACTOR_TRACE)
		zmsg_dump_to_stream (msg, stderr);
	    zmsg_send(&msg, self->proxy_response);
	}
	rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s exit", progname, self->name);
    }

    if (self->flags & ACTOR_RESPONDER) {
	zpoller_t *cmdpoller = zpoller_new (self->proxy_cmd, NULL);
	zpoller_t *delay = zpoller_new(NULL);

	while (1) {
	    self->state = WAIT_FOR_COMMAND;
	    void *cmdsocket = zpoller_wait (cmdpoller, -1);
	    if (cmdsocket == NULL)
		goto DONE;
	    zmsg_t *msg = zmsg_recv(cmdsocket);
	    char *me = zmsg_popstr (msg);
	    char *from = zmsg_popstr (msg);
	    zframe_t *f = zmsg_pop(msg);
	    if (f == NULL)
		break;

	    test_decode(f, pb_Container_fields);

	     // preallocate memory in ringbuffer
	    void *buffer;
	    size_t size = zframe_size(f);
	    if ((retval = record_write_begin(&self->to_rt,
				     (void **)&buffer, size)) != 0) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"%s: %s/%s:record_write_begin(%d) failed: %d\n",
				progname, self->name, self->to_rt_name, size, retval);
		break; // FIXME retry/timed ?
	    }
	    memcpy(buffer, zframe_data(f), size);
	    if ((retval = record_write_end(&self->to_rt, buffer, size))) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"%s: record_write_end(%d) failed: %d\n",
				progname,  size, retval);
		break;  // FIXME
	    }
	    if (self->flags & ACTOR_TRACE) {
		rtapi_print_hex_dump(RTAPI_MSG_ERR, RTAPI_DUMP_PREFIX_OFFSET,
				     16,1, zframe_data(f), zframe_size(f),1,
				     "%s->%s: ", self->name, self->to_rt_name);
	    }

	    zframe_destroy(&f);

	    self->state = WAIT_FOR_RT_RESPONSE;
	    self->current_delay = self->min_delay;
	    ring_size_t response_size;
	    while (1) {
		zpoller_wait (delay, self->current_delay);
		if ( zpoller_terminated (delay) ) {
		    rtapi_print_msg(RTAPI_MSG_ERR, "%s: wait interrupted",
				    self->from_rt_name);

		}
		response_size = record_next_size(&self->from_rt);
		if (response_size > -1) // zero-size frames are ok
		    break;
		// exponential backoff
		self->current_delay <<= 1;
		self->current_delay = MIN(self->current_delay, self->max_delay);
	    }


	    const void *response = record_next(&self->from_rt);
	    zframe_t *r = zframe_new(response, response_size);

	    if (self->flags & ACTOR_TRACE) {
		rtapi_print_hex_dump(RTAPI_MSG_ERR, RTAPI_DUMP_PREFIX_OFFSET,
				     16,1, zframe_data(r), zframe_size(r),1,
				     "%s->%s: ", self->from_rt_name,self->name);
	    }
	    zstr_sendm(self->proxy_response, me);
	    zstr_sendm(self->proxy_response, from);
	    zframe_send (&r, self->proxy_response, 0);
	    record_shift(&self->from_rt);
	    free(me);
	    free(from);
	}
    DONE:
	zpoller_destroy(&cmdpoller);
	zpoller_destroy(&delay);
    }

#if 0
    zpoller_t *poller = zpoller_new (pipe, self->proxy_cmd, NULL);
    assert (poller);
	void *which = zpoller_wait (poller, -1);
	if (which == pipe) {
	    char *cmd = zstr_recv(pipe);
	    if (cmd && (strcmp(cmd, "EXIT") == 0)) {
		rtapi_print_msg(RTAPI_MSG_ERR, "%s: %s - EXIT command received\n",
				progname, self->name);
		free(cmd);
		break;
	    }
	} else {
	    zmsg_t *msg = zmsg_recv(self->proxy_cmd);
	    if (msg == NULL) {
		rtapi_print_msg(RTAPI_MSG_ERR, "%s: %s - context terminated\n",
				progname, self->name);
		break;
	    }
	    if (debug)
		zmsg_dump(msg);
	    char *me = zmsg_popstr (msg);
	    char *from = zmsg_popstr (msg);
	    zmsg_pushstr (msg, from);
	    zmsg_send(&msg, self->proxy_response);
	    free(me);
	    free(from);
	}
    }
    zpoller_destroy (&poller);
#endif
    if (self->to_rt_name) {
	if ((retval = hal_ring_detach(self->to_rt_name, &self->to_rt))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_detach(%s) failed - %d\n",
			    progname, self->to_rt_name, retval);
	    return;
	}
	self->to_rt.header->writer = 0;
    }

    if (self->from_rt_name) {
	if ((retval = hal_ring_detach(self->from_rt_name, &self->from_rt))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_detach(%s) failed - %d\n",
			    progname, self->from_rt_name, retval);
	    return;
	}
	self->from_rt.header->reader = 0;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s exit", progname, self->name);
}

// exercise Nanopb en/decode in case that's done in the proxy

static int test_decode(zframe_t *f, const pb_field_t *fields)
{
    pb_Container rx;
    uint8_t *buffer = zframe_data(f);
    size_t size = zframe_size(f);
    pb_istream_t stream = pb_istream_from_buffer(buffer, size);

    if (!pb_decode(&stream, fields, &rx)) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: pb_decode(Container) failed: '%s'\n",
			progname, PB_GET_ERROR(&stream));
	return -1;
    }
    return 0;
}

static zframe_t *test_encode(const void *msg, const pb_field_t *fields)
{
    size_t size;

    // determine size
    pb_ostream_t sstream = PB_OSTREAM_SIZING;
    if (!pb_encode(&sstream, fields,  msg)) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: sizing pb_encode(): %s written=%zu\n",
			    progname, PB_GET_ERROR(&sstream), sstream.bytes_written);
	    return NULL;
    }
    size = sstream.bytes_written;
    zframe_t *f = zframe_new(NULL, size);

    // encode directly into the new frame
    pb_ostream_t rstream = pb_ostream_from_buffer(zframe_data(f), size);
    if (!pb_encode(&rstream, fields,  msg)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: pb_encode failed: %s, msgsize=%d written=%zu\n",
			progname, PB_GET_ERROR(&rstream), size, rstream.bytes_written);
	zframe_destroy(&f);
	return NULL;
    }
    return f;
}
