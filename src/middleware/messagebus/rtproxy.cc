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
#include "multiframe_flag.h"

// inproc variant for comms with RT proxy threads
// defined in messagbus.cc
extern const char *proxy_cmd_uri;
extern const char *proxy_response_uri;

#if 0
static int test_decode(zframe_t *f, const pb_field_t *fields);
static zframe_t *test_encode(const void *msg, const pb_field_t *fields);
#endif

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
	if ((retval = hal_ring_attach(self->to_rt_name, &self->to_rt_ring,
				       &flags))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_attach(%s) failed - %d\n",
			    progname, self->to_rt_name, retval);
	    return;
	}
	self->to_rt_ring.header->writer = comp_id;
    }
    msgbuffer_init(&self->to_rt_mframe, &self->to_rt_ring);

    if (self->from_rt_name) {
	unsigned flags;

	if ((retval = hal_ring_attach(self->from_rt_name, &self->from_rt_ring,
				      &flags))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_attach(%s) failed - %d\n",
			    progname, self->from_rt_name, retval);
	    return;
	}
	self->from_rt_ring.header->reader = comp_id;
    }
    msgbuffer_init(&self->from_rt_mframe, &self->from_rt_ring);

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
	    zmsg_t *to_rt = zmsg_recv(cmdsocket);

	    //	    zmsg_fprint (to_rt,stderr);

	    zframe_t *f;
	    int i;
	    pb_Container rx;

	    msg_write_abort(&self->to_rt_mframe);

	    for (i = 0,f = zmsg_first (to_rt);
		 f != NULL;
		 f = zmsg_next(to_rt),i++) {

		mflag_t flags = { .u = 0 };
		void *data;
		size_t size;

	    	switch (i) {
	    	case 0:
		    flags.f.frametype = MF_TARGET;
		    data = zframe_data(f);
		    size = zframe_size(f);
		    break;

	    	case 1:
		    flags.f.frametype = MF_ORIGINATOR;
		    data = zframe_data(f);
		    size = zframe_size(f);
		    break;

	    	default:
		    if (!(self->flags & DESERIALIZE_TO_RT)) {
			// as is, no attempt at decoding
			flags.f.frametype = MF_UNSPECIFIED;
			flags.f.npbtype = NPB_UNSPECIFIED;
			flags.f.count = 1;
			data = zframe_data(f);
			size = zframe_size(f);
		    } else {
			// try to deserialize as Container:
			pb_istream_t stream = pb_istream_from_buffer(zframe_data(f),
								     zframe_size(f));
			if (pb_decode(&stream, pb_Container_fields, &rx)) {
			    // good, a Container. Check type:
			    switch (rx.type) {

			    case pb_ContainerType_MT_RTMESSAGE:
				// send only the deserialized RTMessage array
				data = (void *) &rx.rtmessage;
				size = sizeof(pb_RTMessage) * rx.rtmessage_count;
				flags.f.frametype = MF_NPB_CSTRUCT;
				flags.f.npbtype = NPB_RTMESSAGE;
				flags.f.count = rx.rtmessage_count;
				rtapi_print_msg(RTAPI_MSG_DBG,
						"%s: sending as deserialized"
						" RTMessage, count=%d\n",
						progname, rx.rtmessage_count);
				break;

			    default:
				// leave Container as-is
				data = (void *) &rx;
				size = sizeof(rx);
				flags.f.frametype = MF_NPB_CSTRUCT;
				flags.f.npbtype = NPB_CONTAINER;
				flags.f.count = 1;
				rtapi_print_msg(RTAPI_MSG_DBG,
						"%s: sending as deserialized"
						" Container\n",
						progname);
				break;
			    }
			} else {
			    // hm, something else.
			    rtapi_print_msg(RTAPI_MSG_DBG,
					    "%s: pb_decode(Container) failed: '%s'\n",
					progname, PB_GET_ERROR(&stream));
			    flags.f.frametype = MF_UNSPECIFIED;
			    flags.f.npbtype = NPB_UNSPECIFIED;
			    flags.f.count = 1;
			    data = zframe_data(f);
			    size = zframe_size(f);
			}
		    }
		    break;
		}
		retval = frame_write(&self->to_rt_mframe, data, size, flags.u);
		if (retval)
		    rtapi_print_msg(RTAPI_MSG_ERR, "frame_write: %s",
				    strerror(retval));
		if (self->flags & ACTOR_TRACE)
			rtapi_print_hex_dump(RTAPI_MSG_ERR, RTAPI_DUMP_PREFIX_OFFSET,
					     16,1, data, size, 1,
					     "%s->%s t=%d c=%d: ",
					     self->name, self->to_rt_name,
					     flags.f.frametype, flags.f.npbtype);
	    }
	    msg_write_flush(&self->to_rt_mframe);
	    zmsg_destroy(&to_rt);

	    self->state = WAIT_FOR_RT_RESPONSE;
	    self->current_delay = self->min_delay;

	    zmsg_t *from_rt = zmsg_new();
	    msg_read_abort(&self->from_rt_mframe);
	    i = 0;
	    while (1) {
		zpoller_wait (delay, self->current_delay);
		if ( zpoller_terminated (delay) ) {
		    rtapi_print_msg(RTAPI_MSG_ERR, "%s: wait interrupted",
				    self->from_rt_name);
		}
		const void *data;
		size_t size;
		mflag_t flags;
		if (frame_read(&self->from_rt_mframe,
			       &data, &size, &flags.u) == 0) {
		    if (self->flags & ACTOR_TRACE)
			rtapi_print_hex_dump(RTAPI_MSG_ERR, RTAPI_DUMP_PREFIX_OFFSET,
					     16,1, data, size, 1,
					     "%s->%s t=%d c=%d r=%d: ",
					     self->from_rt_name,self->name,
					     flags.f.frametype, flags.f.npbtype, flags.f.count);


		    pb_ostream_t sstream, ostream;
		    const pb_field_t *fields;

		    // decide what to do based on flags
		    switch (flags.f.frametype) {

		    case MF_NPB_CSTRUCT:

			switch (flags.f.npbtype) {
			case NPB_CONTAINER:
			case NPB_RTMESSAGE:
			    fields = (flags.f.npbtype == NPB_CONTAINER) ?
				pb_Container_fields : pb_RTMessage_fields;
			    // determine size
			    sstream = PB_OSTREAM_SIZING;
			    if (!pb_encode(&sstream, fields, data)) {
				rtapi_print_msg(RTAPI_MSG_ERR,
						"%s: from_rt sizing failed %s type=%d written=%zu\n",
						progname, PB_GET_ERROR(&sstream), flags.f.npbtype,
						sstream.bytes_written);
				// send as-is
				f = zframe_new (data, size);
				zmsg_append (from_rt, &f);
				break;
			    }
			    f = zframe_new (NULL,  sstream.bytes_written);
			    ostream = pb_ostream_from_buffer(zframe_data(f), zframe_size(f));
			    if (!pb_encode(&ostream, fields, data)) {
				rtapi_print_msg(RTAPI_MSG_ERR,
						"%s: from_rt encoding failed %s type=%d written=%zu\n",
						progname, PB_GET_ERROR(&sstream),flags.f.npbtype,
						ostream.bytes_written);
				// send as-is
				f = zframe_new (data, size);
				zmsg_append (from_rt, &f);
				break;
			    }
			    zmsg_append (from_rt, &f);
			    break;

			default:
			    rtapi_print_msg(RTAPI_MSG_ERR,
					    "%s: invalid npbtype from RT: %d\n",
					    progname, flags.f.npbtype);
			    zframe_t *f = zframe_new (data, size);
			    zmsg_append (from_rt, &f);
			}
			break;

		    case MF_UNSPECIFIED:
		    case MF_ORIGINATOR:
		    case MF_TARGET:
		    case MF_PROTOBUF:
			// leave as is
			// zframe_new copies the data
			zframe_t *f = zframe_new (data, size);
			zmsg_append (from_rt, &f);
		    }
			frame_shift(&self->from_rt_mframe);

		    //    if (!(self->flags & SERIALIZE_FROM_RT)) {
			// leave as is
			// zframe_new copies the data
			// zframe_t *f = zframe_new (data, size);
			// zmsg_append (from_rt, &f);
			// frame_shift(&self->from_rt_mframe);
		    // } else {
		    // 	// switch (flags.f.frametype) {
		    // 	// case MF_TARGET:
		    // 	// case MF_ORIGINATOR:
		
// 			    // determine size
// 			    if (!pb_encode(&sstream, pb_Container_fields, data)) {
// 				rtapi_print_msg(RTAPI_MSG_ERR,
// 						"%s: sizing pb_encode(): %s written=%zu\n",
// 						progname, PB_GET_ERROR(&sstream), sstream.bytes_written);
// 			    }

// ) {
// 			    // good, a Container. Check type:	    

		    // 	// }

		    // }

		} else
		    break;

		// exponential backoff
		self->current_delay <<= 1;
		self->current_delay = MIN(self->current_delay, self->max_delay);
	    }
	    msg_read_flush(&self->from_rt_mframe);
	    zmsg_send (&from_rt, self->proxy_response);
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
	if ((retval = hal_ring_detach(self->to_rt_name, &self->to_rt_ring))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_detach(%s) failed - %d\n",
			    progname, self->to_rt_name, retval);
	    return;
	}
	self->to_rt_ring.header->writer = 0;
    }

    if (self->from_rt_name) {
	if ((retval = hal_ring_detach(self->from_rt_name, &self->from_rt_ring))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "%s: hal_ring_detach(%s) failed - %d\n",
			    progname, self->from_rt_name, retval);
	    return;
	}
	self->from_rt_ring.header->reader = 0;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "%s: %s exit", progname, self->name);
}

#if 0
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
#endif
