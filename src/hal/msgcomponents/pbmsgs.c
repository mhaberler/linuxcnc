// this component provides support for other RT components
// it exports protobuf message definitions in nanopb format
// as automatically generated in protobuf/Submakefile
// from protobuf/proto/*.proto definitions

#include "config.h"		/* GIT_VERSION */
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "rtapi_app.h"		/* RTAPI realtime module decls */
#include "hal.h"		/* HAL API */

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("Nanopb protobuf message definitions");
MODULE_LICENSE("GPL");

#include "pbmsgs.h"

static int comp_id;
static const char *name = "pbmsgs";

int rtapi_app_main(void)
{
    if ((comp_id = hal_init(name)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"%s: ERROR: hal_init() failed: %d\n",
			name, comp_id);
	return -1;
    }
    hal_ready(comp_id);
    rtapi_print_msg(RTAPI_MSG_DBG, "%s " VERSION " loaded\n", name);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}

#define PB_MESSAGE(symbol) EXPORT_SYMBOL(pb_ ## symbol ## _fields)

// FIXME many lacking - autogenerate

PB_MESSAGE(Container);
PB_MESSAGE(Telegram);
PB_MESSAGE(Object);
PB_MESSAGE(MotionCommand);
PB_MESSAGE(MotionStatus);
PB_MESSAGE(Pm_Cartesian);
PB_MESSAGE(Emc_Pose);
PB_MESSAGE(HalUpdate);
//PB_MESSAGE(LegacyEmcPose);
PB_MESSAGE(RTAPI_Message);
PB_MESSAGE(LogMessage);
PB_MESSAGE(Test1);
PB_MESSAGE(Test2);
PB_MESSAGE(Test3);
