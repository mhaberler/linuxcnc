#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <uuid/uuid.h>

#include "syslog_async.h"
#include "config.h"
#include "mk-zeroconf.hh"
#include "select_interface.h"


static int timeout(zloop_t *loop, int timer_id, void *arg)
{
    mk_zeroconf_unregister(arg);
    return -1; // cancel loop
}


int main(int argc, const char **argv)
{
    openlog_async(argv[0], LOG_NDELAY, LOG_LOCAL1);
    setlogmask_async(LOG_UPTO(LOG_DEBUG));
    uuid_t uuid;
    uuid_generate_time(uuid);
    char uubuf[40];
    uuid_unparse(uuid, uubuf);

    zctx_t *context = zctx_new ();
    assert(context);
    zloop_t *loop = zloop_new();
    assert(loop);
    zloop_set_verbose (loop, argc > 1);

    char ifname[100], ipv4[100];
    unsigned int ifindex = AVAHI_IF_UNSPEC;
    int retval;

    if (argc > 2) {
	memset(ifname, 0, 100);
	memset(ipv4, 0, 100);
	int retval =  select_interface(argc-2, &argv[2],ifname, ipv4, &ifindex);
	printf("rc=%d if %s ip %s ifindex=%d\n",retval, ifname, ipv4, ifindex);
    }

    AvahiCzmqPoll *av_loop;

    // register poll adapter
    if (!(av_loop = avahi_czmq_poll_new(loop))) {
        syslog_async(LOG_ERR, "zeroconf: Failed to create avahi event loop object.\n");
	return -1;
    }

    zservice_t zs  = {0};
    zs.name = (char *) "foo main on host bar";
    zs.proto =  AVAHI_PROTO_INET;
    zs.interface = ifindex;
    zs.type = "_machinekit._tcp";
    zs.port = 815;
    zs.txt = avahi_string_list_add_printf(zs.txt, "uuid=%s",uubuf);
    zs.subtypes = avahi_string_list_add(zs.subtypes, "_haltalk._sub._machinekit._tcp");
    zs.subtypes = avahi_string_list_add(zs.subtypes, "_rtcmd._sub._machinekit._tcp");

    void *avahi;
    if ((avahi = mk_zeroconf_register(&zs, av_loop)) == NULL)
	return -1;

    zloop_timer(loop, 15000, 1, timeout, avahi);


    do {
	retval = zloop_start(loop);
    } while  (!(retval || zctx_interrupted));

    // deregister poll adapter
    if (av_loop)
        avahi_czmq_poll_free(av_loop);

    return 0;
}
