
#include <stdio.h>		// vsprintf()
#include <string.h>		// strcpy()
#include <stdarg.h>		// va_start()
#include <stdlib.h>		// exit()
#include <signal.h>		// signal(), SIGINT
#include <float.h>		// DBL_MAX
#include <sys/types.h>		// pid_t
#include <unistd.h>		// fork()
#include <sys/wait.h>		// waitpid(), WNOHANG, WIFEXITED
#include <ctype.h>		// isspace()
#include <libintl.h>
#include <locale.h>



#include <google/protobuf/text_format.h>
#include <google/protobuf/message_lite.h>

#include <machinetalk/generated/types.pb.h>
#include <machinetalk/generated/task.pb.h>
#include <machinetalk/generated/message.pb.h>

using namespace google::protobuf;

#include "czmq.h"

#include "zmqsupport.hh"

int zdebug;
static const char *completion_socket = "tcp://127.0.0.1:5557";

static zctx_t *zmq_ctx;
void  *cmd, *completion;
static const char *cmdsocket = "tcp://127.0.0.1:5556";
static const char *cmdid = "task";


int setup_zmq(void)
{
    int major, minor, patch, rc;

    if (zmq_ctx)
	return 0;
    zdebug = (getenv("ZDEBUG") != NULL);

    zmq_version (&major, &minor, &patch);
    fprintf(stderr, "Current 0MQ version is %d.%d.%d\n",major, minor, patch);

    zmq_ctx = zctx_new ();
    cmd = zsocket_new (zmq_ctx, ZMQ_ROUTER);

    // shutdown socket immediately if unsent messages
    zsocket_set_linger (cmd, 0);
    zsocket_set_identity (cmd, (char *) cmdid);

    rc = zsocket_bind(cmd, cmdsocket);
    assert (rc != 0);

    completion = zsocket_new (zmq_ctx, ZMQ_PUB);
    rc = zsocket_bind(completion, completion_socket);
    assert (rc != 0);
    return 0;
}

int shutdown_zmq(void)
{
    zctx_destroy (&zmq_ctx);
    return 0;
}

int publish_ticket_update(int state, int ticket, string origin, string text)
{
    pb::Container update;
    update.set_type(pb::MT_TICKET_UPDATE);

    pb::TicketUpdate *tu = update.mutable_ticket_update();
    tu->set_cticket(ticket);
    tu->set_status((pb::RCS_STATUS)state);
    if (text.size()) {
	tu->set_text(text);
    }
    size_t pb_update_size = update.ByteSize();
    zframe_t *pb_update_frame = zframe_new (NULL, pb_update_size);
    assert(update.SerializeToArray(zframe_data (pb_update_frame),
				   zframe_size (pb_update_frame)));
    zmsg_t *msg = zmsg_new ();
    zmsg_pushstr (msg, origin.c_str());
    zmsg_add (msg, pb_update_frame);
    zmsg_send (&msg, completion);
    return 0;
}
