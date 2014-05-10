#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "syslog_async.h"
#include "config.h"
#include "mk-zeroconf.hh"
#include "select_interface.h"



zservice_t zs;


int main(int argc, const char **argv)
{
    openlog_async(argv[0], LOG_NDELAY, LOG_LOCAL1);
    setlogmask_async(LOG_UPTO(LOG_DEBUG));

    char ifname[100], ipv4[100];
    unsigned int ifindex = AVAHI_IF_UNSPEC;
    int retval;

    if (argc > 1) {
	memset(ifname, 0, 100);
	memset(ipv4, 0, 100);
	int retval =  select_interface(argc-1, &argv[1],ifname, ipv4, &ifindex);
	printf("rc=%d if %s ip %s ifindex=%d\n",retval, ifname, ipv4, ifindex);
    }

    void *avahi;

    zs.name = (char *) "foo service on host bar";
    zs.proto =  AVAHI_PROTO_INET;
    zs.interface = ifindex;
    zs.type = MACHINEKIT_DNS_SERVICE_TYPE;
    zs.port = 4711;

    zs.txt = NULL;
    zs.txt = avahi_string_list_add(zs.txt, "foo=bar");
    zs.txt = avahi_string_list_add(zs.txt, "blah=fasel");
    zs.txt = avahi_string_list_add_pair(zs.txt, "key", "value");
    zs.txt = avahi_string_list_add_printf(zs.txt, "version=%s", GIT_VERSION);

    if ((avahi = mk_zeroconf_register(&zs)) == NULL)
	return -1;
    sleep(10);
    if (mk_zeroconf_unregister(avahi) != 0)
	return -1;
    return 0;
}
