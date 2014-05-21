/* zeroconf announce/withdraw tailored for machinekit purposes
 */

#include "mk-zeroconf.hh"


register_context_t *zeroconf_service_announce(const char *name,
					      const char *subtype,
					      int port,
					      char *dsn,
					      const char *service_uuid,
					      uuid_t process_uuid,
					      const char *tag,
					      AvahiCzmqPoll *av_loop)
{
    zservice_t *zs = (zservice_t *) calloc(sizeof(zservice_t), 1);
    zs->name = name;
    zs->proto =  AVAHI_PROTO_INET;
    zs->interface = AVAHI_IF_UNSPEC;
    zs->type =  MACHINEKIT_DNSSD_SERVICE_TYPE;
    zs->port = port;
    zs->txt = avahi_string_list_add_printf(zs->txt, "dsn=%s", dsn);
    zs->txt = avahi_string_list_add_printf(zs->txt, "uuid=%s", service_uuid);
    char buf[40];
    uuid_unparse(process_uuid, buf);
    zs->txt = avahi_string_list_add_printf(zs->txt, "instance=%s", buf);
    if (tag)
	zs->txt = avahi_string_list_add_printf(zs->txt, "service=%s", tag);
    if (subtype)
	zs->subtypes = avahi_string_list_add(zs->subtypes, subtype);

    return ll_zeroconf_register(zs, av_loop);
}

int zeroconf_service_withdraw(register_context_t *publisher)
{
    ll_zeroconf_unregister(publisher);

    zservice_t *s = publisher->service;
    if (s) {
    	if (s->subtypes)
    	    avahi_string_list_free(s->subtypes);
    	if (s->txt)
    	    avahi_string_list_free(s->txt);
    	free(publisher->service);
    }

    free(publisher->name);
    free(publisher);
    return 0;
}
