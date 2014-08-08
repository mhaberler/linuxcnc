// implements ZWS
// see https://raw.githubusercontent.com/somdoron/rfc/master/spec_39.txt

// use like so:
// webtalk --plugin

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libwebsockets.h>

#include "webtalk.hh"

struct zws_session {
    zmsg_t *partial;  // accumulate frames from ws with 'more' flag set, send on final
};

static  int zws_policy(wtself_t *self,         // server instance
		       zws_session_t *s,       // session
		       zwscb_type type);       // which callback

// plugin descriptor structure
// exported symbol: proxy_policy
// see webtalk_plugin.cc
zwspolicy_t proxy_policy {
    "zws",
    zws_policy
};


// relay policy handler
static int
zws_policy(wtself_t *self,
	   zws_session_t *wss,
	   zwscb_type type)
{
    zmsg_t *m;
    zframe_t *f;
    struct zws_session *zwss = (struct zws_session *) wss->user_data;

    switch (type) {

    case ZWS_CONNECTING:
	lwsl_notice("%s: ZWS_CONNECTING\n", __func__);

	wss->user_data = calloc(1, sizeof(struct zws_session));
	assert(wss->user_data != NULL);

	// > 1 indicates: run the default policy ZWS_CONNECTING code
	return 1;
	break;

    case ZWS_ESTABLISHED:
	lwsl_notice("%s: ZWS_ESTABLISHED\n", __func__);
	break;

    case ZWS_CLOSE:

	lwsl_notice("%s: ZWS_CLOSE\n", __func__);

	if (zwss->partial != NULL) {
	    lwsl_zws("%s: FRAMES LOST:  partial message pending on close, %d frames, socket type=%d\n",
		     __func__, zmsg_size(zwss->partial), wss->socket_type);
	    zmsg_destroy(&zwss->partial);
	}
	free(wss->user_data);
	break;

    case ZWS_FROM_WS:
	{
	    // a frame was received from the websocket client
	    switch (wss->socket_type) {
	    case ZMQ_SUB:
		{
		    char *data = (char *) wss->buffer;
		    switch (*data) {
		    case '0': // a final frame
			data++;
			switch (*data) {
			case '1': // subscribe command:
			    {
				std::string s(data + 1, data + wss->length-2);
				zsocket_set_subscribe (wss->socket, s.c_str());
				lwsl_zws("%s: SUB subscribe '%s'\n", __func__, s.c_str());
			    }
			    break;
			case '0': // unsubscribe command:
			    {
				std::string s(data + 1, data + wss->length-2);
				zsocket_set_unsubscribe (wss->socket, s.c_str());
				lwsl_zws("%s: SUB unsubscribe '%s'\n", __func__, s.c_str());
			    }
			    break;
			default:
			    lwsl_err("%s: invalid code on SUB in ws frame, c='%c' (0x%x)\n",
				       __func__, *data, *data);
			    return -1;
			}
		    }
		    break;
		}
		break;
	    case ZMQ_XSUB:
		{
		    char *data = (char *) wss->buffer;
		    switch (*data) {
		    case '0': // a final frame
			data++;
			switch (*data) {

			case '1': // subscribe command:
			    {
				lwsl_zws("%s: XSUB subscribe '%.*s'\n", __func__,
					 wss->length - 2, data + 1);

				*data = '\001';
				std::string s(data, data + wss->length - 1);
				f = zframe_new (s.c_str(), wss->length - 1);
				return zframe_send(&f, wss->socket, 0);
			    }
			    break;

			case '0': // unsubscribe command:
			    {
				*data = '\000';
				std::string s(data, data + wss->length - 1);
				lwsl_zws("%s: XSUB unsubscribe '%.*s'\n", __func__,
					 wss->length - 2, data +1);
				f = zframe_new (s.c_str(), wss->length - 1);
				return zframe_send(&f, wss->socket, 0);
			    }
			    break;

			default:
			    lwsl_err("%s: invalid code on XSUB in ws frame, c='%c' (0x%x), socket type=%d\n",
				       __func__, *data, *data, wss->socket_type);
			    return -1;
			}
		    }
		    break;
		}
		break;

	    case ZMQ_DEALER:
		{
		    lwsl_zws("%s: DEALER in '%.*s'\n", __func__, wss->length, wss->buffer);
		    char *data = (char *) wss->buffer;
		    int rc;

		    switch (*data) {

		    case '0': // last frame
			{
			    if (zwss->partial == NULL) {
				// trivial case - single frame message
				f = zframe_new (data + 1, wss->length - 1);
				return zframe_send(&f, wss->socket, 0);
			    };
			    // append this frame
			    rc = zmsg_addmem (zwss->partial, data + 1, wss->length - 1);
			    assert (rc == 0);
			    // send it off - this sets zwss->partial to NULL
			    rc = zmsg_send(&zwss->partial,  wss->socket);
			    assert (rc == 0);
			}
			break;

		    case '1': // more to come
			{
			    if (zwss->partial == NULL) {
				lwsl_err("%s: DEALER - start a multiframe message\n",  __func__);
				zwss->partial = zmsg_new();
			    }

			    // append current frame
			    rc = zmsg_addmem (zwss->partial, data + 1, wss->length - 1);
			    lwsl_err("%s: DEALER - add frame count=%d\n",
				     __func__, zmsg_size(zwss->partial));
			}
			break;

		    default:
			lwsl_err("%s: invalid code on DEALER in ws frame, c='%c' (0x%x), socket type=%d\n",
				       __func__, *data, *data, wss->socket_type);
			return -1;
			break;
		    }
		}

	    default:
		lwsl_err("%s: socket type=%d not implemented yet\n",
			   __func__, wss->socket_type);
		return -1;
	    }
	}
	break;

    case ZWS_TO_WS:
	{
	    // a message was received from the zeroMQ socket

	    // ZWS: prepend non-final frames with '1', final frame with '0'

	    m = zmsg_recv(wss->socket);
	    size_t nf = zmsg_size (m);

	    while ((f = zmsg_pop (m)) != NULL) {
		wss->zmq_bytes += zframe_size(f);
		wss->zmq_msgs++;
		nf--;
		size_t nsize = zframe_size(f) + 1;

		zframe_t *nframe = zframe_new (NULL, nsize);
		char *data = (char *) zframe_data(nframe);

		// prepend more/final flag
		*data++ = (nf > 0) ? '1' : '0';
		memcpy(data, zframe_data(f), zframe_size(f));
		lwsl_tows("%s: '%c' %d:'%.*s'\n", __func__,
			  (nf > 0) ? '1' : '0',
			  zframe_size(f), zframe_size(f), zframe_data(f));
		zframe_send(&nframe, wss->wsq_out, 0);
		zframe_destroy(&f);
	    }
	    zmsg_destroy(&m);
	}
	break;

    default:
	lwsl_err("%s: unhandled type: %d\n", __func__, type);
	break;
    }
    return 0;
}
