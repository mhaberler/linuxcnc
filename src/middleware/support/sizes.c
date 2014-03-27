#include <stdio.h>
#include <emc/motion/motion.h>
#include <emc/motion/motion_debug.h>

#include <middleware/generated/test.npb.h>
#include <middleware/generated/value.npb.h>
#include <middleware/generated/object.npb.h>
#include <middleware/generated/message.npb.h>
#include <middleware/generated/motcmds.npb.h>


int main()
{
    printf("emcmot_joint_t = %d\n", sizeof(emcmot_joint_t));
    printf("emcmot_joint_status_t = %d\n", sizeof(emcmot_joint_status_t));
    printf("emcmot_command_t = %d\n", sizeof(emcmot_command_t));
    printf("spindle_status = %d\n", sizeof(spindle_status));
    printf("emcmot_status_t = %d\n", sizeof(emcmot_status_t));
    printf("emcmot_config_t = %d\n", sizeof(emcmot_config_t));
    printf("emcmot_debug_t = %d\n", sizeof(emcmot_debug_t));

    printf("npb Container = %d\n", sizeof(pb_Container));
    printf("npb Test1 = %d\n", sizeof(pb_Test1));
    printf("npb Value = %d\n", sizeof(pb_Value));

    printf("npb MotionCommand = %d\n", sizeof(pb_MotionCommand));


    return 0;
}
