#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <hal.h>
#include <hal_priv.h>
#include <hal_group.h>
#include <hal_ring.h>
#include <rtapi_heap.h>
#include <rtapi_heap_private.h>
#include <emc/motion/motion.h>
#include <emc/motion/motion_debug.h>

#include <machinetalk/generated/test.npb.h>
#include <machinetalk/generated/value.npb.h>
#include <machinetalk/generated/object.npb.h>
#include <machinetalk/generated/message.npb.h>
#include <machinetalk/generated/motcmds.npb.h>


int main()
{
    printf("int = %zu\n", sizeof(int));
    printf("size_t = %zu\n", sizeof(size_t));
    printf("void* = %zu\n", sizeof(void *));
    printf("ptrdiff_t = %zu\n", sizeof(ptrdiff_t));
    printf("halhdr_t = %zu\n", sizeof(halhdr_t));
    printf("halhdr_t details:\n");
    printf("\tid\t%zu\n", offsetof(halhdr_t,_id));
    printf("\towner_id\t%zu\n", offsetof(halhdr_t,_owner_id));
    printf("\tname_ptr\t%zu\n", offsetof(halhdr_t,_name_ptr));

    printf("heap:\trtapi_malloc_hdr_t\t%zu\n", sizeof(rtapi_malloc_hdr_t));

    printf("hal_comp_t = %zu\n", sizeof(hal_comp_t));
    printf("hal_inst_t = %zu\n", sizeof(hal_inst_t));
    printf("hal_pin_t = %zu\n", sizeof(hal_pin_t));
    printf("hal_param_t = %zu\n", sizeof(hal_param_t));
    printf("hal_sig_t = %zu\n", sizeof(hal_sig_t));
    printf("\ttype\t%zu\n", offsetof(hal_sig_t,type));
    printf("\tvalue\t%zu\n", offsetof(hal_sig_t,value));
    printf("\treaders\t%zu\n", offsetof(hal_sig_t,readers));

    printf("hal_group_t = %zu\n", sizeof(hal_group_t));
    printf("hal_member_t = %zu\n", sizeof(hal_member_t));
    printf("hal_funct_t = %zu\n", sizeof(hal_funct_t));
    printf("hal_thread_t = %zu\n", sizeof(hal_thread_t));
    printf("hal_vtable_t = %zu\n", sizeof(hal_vtable_t));
    printf("hal_ring_t = %zu\n", sizeof(hal_ring_t));
    printf("hal_plug_t = %zu\n", sizeof(hal_plug_t));

    printf("ringheader_t = %zu (without storage)\n", sizeof(ringheader_t));
    printf("ringbuffer_t = %zu\n", sizeof(ringbuffer_t));
    printf("msgbuffer_t = %zu\n", sizeof(msgbuffer_t));

    printf("emcmot_joint_t = %zu\n", sizeof(emcmot_joint_t));
    printf("emcmot_joint_status_t = %zu\n", sizeof(emcmot_joint_status_t));
    printf("emcmot_command_t = %zu\n", sizeof(emcmot_command_t));
    printf("spindle_status = %zu\n", sizeof(spindle_status));
    printf("emcmot_status_t = %zu\n", sizeof(emcmot_status_t));
    printf("emcmot_config_t = %zu\n", sizeof(emcmot_config_t));
    printf("emcmot_debug_t = %zu\n", sizeof(emcmot_debug_t));

    printf("npb Container = %zu\n", sizeof(pb_Container));
    printf("npb Test1 = %zu\n", sizeof(pb_Test1));
    printf("npb Value = %zu\n", sizeof(pb_Value));

    printf("npb MotionCommand = %zu\n", sizeof(pb_MotionCommand));
    printf("npb MotionStatus = %zu\n", sizeof(pb_MotionStatus));
    pb_Container c[1];
    //  printf("npb Task cmds = %zu\n", c->has_traj_set_g5x-c->has_tpexecute);
    printf("npb Canon cmds = %zu\n", (void *)&c[1]- (void *)&(c->has_traj_set_g5x));


    return 0;
}
