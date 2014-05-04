#include <stdio.h>
#include <unistd.h>

#include "config.h"
#include "mk-zeroconf.hh"
#include "syslog_async.h"


zservice_t zs;


int main(int argc, char **argv)
{
    openlog_async(argv[0], LOG_NDELAY, LOG_LOCAL1);
    setlogmask_async(LOG_UPTO(LOG_DEBUG));

    void *avahi;

    zs.name = (char *) "foo service on host bar";
    zs.ipv6 = 0;
    zs.port = 4711;
    zs.type = MACHINEKIT_DNS_SERVICE_TYPE;
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
