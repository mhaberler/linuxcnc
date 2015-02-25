
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("demo component for userland functs");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "ufdemo";

static int demo(const hal_funct_args_t *fa)
{

    return 0;
}

int rtapi_app_main(void)
{
    comp_id = hal_init(compname);
    if (comp_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "%s: ERROR: hal_init() failed - %d\n",
			compname, comp_id);
	return -1;
    }

    hal_xfunct_t xf = {
	.type = FS_USERLAND,
	.funct.u = demo,
	.arg = "demo-argument",
	.comp_id = comp_id
    };
    int retval = hal_export_xfunct("demofunct", &xf);
    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: ERROR: hal_export_xfunct) failed: %d\n",
			compname, retval);
	hal_exit(comp_id);
	return retval;
    }
    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
