#include "halcmd_rtapiapp.h"

#include <czmq.h>
#include <string.h>
#include <sdiscover.h>


#include <middleware/generated/message.pb.h>
using namespace google::protobuf;

static pb::Container command, reply;

static zctx_t *z_context;
static void *z_command;
static int timeout = 1000;
static  sdreq_t *sd;
static std::string errormsg;

int rtapi_rpc(void *socket, pb::Container &tx, pb::Container &rx)
{
    zframe_t *request = zframe_new (NULL, tx.ByteSize());
    assert(request);
    assert(tx.SerializeWithCachedSizesToArray(zframe_data (request)));

    assert (zframe_send (&request, socket, 0) == 0);
    zframe_t *reply = zframe_recv (socket);
    if (reply == NULL) {
	errormsg =  "rtapi_rpc(): reply timeout";
	return -1;
    }
    int retval =  rx.ParseFromArray(zframe_data (reply),
				    zframe_size (reply)) ? 0 : -1;
    //    assert(retval == 0);
    zframe_destroy(&reply);
    if (rx.note_size())
	errormsg = rx.note(0); // simplify - one line only
    else
	errormsg = "";
    return retval;
}

static int rtapi_loadop(pb::ContainerType type, int instance, const char *modname, const char **args)
{
    pb::RTAPICommand *cmd;
    command.Clear();
    command.set_type(type);
    cmd = command.mutable_rtapicmd();
    cmd->set_modname(modname);
    cmd->set_instance(instance);

    int argc = 0;
    if (args)
	while(args[argc] && *args[argc]) {
	    cmd->add_argv(args[argc]);
	    argc++;
	}
    int retval = rtapi_rpc(z_command, command, reply);
    if (retval)
	return retval;
    return reply.retcode();
}

int rtapi_loadrt(int instance, const char *modname, const char **args)
{
    return rtapi_loadop(pb::MT_RTAPI_APP_LOADRT, instance, modname, args);
}

int rtapi_unloadrt(int instance, const char *modname)
{
    return rtapi_loadop(pb::MT_RTAPI_APP_UNLOADRT, instance, modname, NULL);
}

int rtapi_shutdown(int instance)
{
    pb::RTAPICommand *cmd;

    command.Clear();
    command.set_type(pb::MT_RTAPI_APP_EXIT);
    cmd = command.mutable_rtapicmd();
    cmd->set_instance(instance);

    int retval = rtapi_rpc(z_command, command, reply);
    if (retval)
	return retval;
    return reply.retcode();
}


int rtapi_ping(int instance)
{
    pb::RTAPICommand *cmd;
    command.Clear();
    command.set_type(pb::MT_RTAPI_APP_PING);
    cmd = command.mutable_rtapicmd();
    cmd->set_instance(instance);

    int retval = rtapi_rpc(z_command, command, reply);
    if (retval)
	return retval;
    return reply.retcode();
}

int rtapi_newthread(int instance, const char *name, int period, int cpu, bool use_fp)
{
    pb::RTAPICommand *cmd;
    command.Clear();
    command.set_type(pb::MT_RTAPI_APP_NEWTHREAD);
    cmd = command.mutable_rtapicmd();
    cmd->set_instance(instance);
    cmd->set_threadname(name);
    cmd->set_threadperiod(period);
    cmd->set_cpu(cpu);
    cmd->set_use_fp(use_fp);

    int retval = rtapi_rpc(z_command, command, reply);
    if (retval)
	return retval;
    return reply.retcode();
}

int rtapi_delthread(int instance, const char *name)
{
    pb::RTAPICommand *cmd;
    command.Clear();
    command.set_type(pb::MT_RTAPI_APP_DELTHREAD);
    cmd = command.mutable_rtapicmd();
    cmd->set_instance(instance);
    cmd->set_threadname(name);
    int retval = rtapi_rpc(z_command, command, reply);
    if (retval)
	return retval;
    return reply.retcode();
}

const char *rtapi_rpcerror(void)
{
    return errormsg.c_str();
}

int rtapi_connect(int instance, const char *uri)
{
    int retval;

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if (uri == NULL) {
	// use service discovery to retrieve rtapi_app command URI
	sd = sd_new(0, instance);
	assert(sd);
	retval = sd_add(sd,  pb::ST_RTAPI_COMMAND,
			0,   pb::SA_ZMQ_PROTOBUF);
	assert(retval == 0);
	retval = sd_query(sd, 3000);

	if (retval) {
	    fprintf(stderr,
		    "halcmd: service discovery failed - cant retrieve rtapi_app command uri: %d\n",
		    retval );
	    return retval;
	}
	uri = sd_uri(sd, pb::ST_RTAPI_COMMAND);
	if (uri == NULL) {
	    fprintf(stderr, "halcmd: BUG - service discovery retrieved invalid URI\n");
	    sd_dump("after query: ", sd);
	    return -ENOENT;
	}
    }

    z_context = zctx_new ();
    assert(z_context);
    z_command = zsocket_new (z_context, ZMQ_DEALER);
    assert(z_command);

    char z_ident[30];
    snprintf(z_ident, sizeof(z_ident), "halcmd%d",getpid());

    zsocket_set_identity(z_command, z_ident);
    zsocket_set_linger(z_command, 0);
    if (zsocket_connect(z_command, uri))
	return -EINVAL;
    zsocket_set_rcvtimeo (z_command, timeout * ZMQ_POLL_MSEC);

    return rtapi_ping(instance);
}
