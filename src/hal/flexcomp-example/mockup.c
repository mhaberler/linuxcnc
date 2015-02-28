
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("mockup for instantiation HAL API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "ufdemo";

static hal_instance_params ip = {
    name, type, unionv

};

static int instantiate()
{

}

static int delete()
{

}


int rtapi_app_main(void)
{

    comp_id = hal_init_class(compname, ip, instantiate, delete);
    if (comp_id < 0)
	return -1;

    // exporting a legacy thread function - as per 'man hal_export_funct':
    if (hal_export_funct("ufdemo.legacy-funct", legacy_funct,
			 "l-instance-data", 0, 0, comp_id))
	return -1;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
