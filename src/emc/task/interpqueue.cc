#include "emc_nml.hh"
#include "interpl.hh"		// NML_INTERP_LIST, interp_list

#include "interpqueue.hh"		// emcTaskQueueCommand

// puts command on interp list
int emcTaskQueueCommand(NMLmsg * cmd)
{
    if (0 == cmd) {
	return 0;
    }

    interp_list.append(cmd);

    return 0;
}

int emcTaskQueueSynchCmd()
{
    static EMC_TASK_PLAN_SYNCH taskPlanSynchCmd;
    emcTaskQueueCommand(&taskPlanSynchCmd);
    return 0;
}

int emcTaskQueueRunCmd(int line)
{
    static EMC_TASK_PLAN_RUN taskPlanRunCmd;
    taskPlanRunCmd.line = line;
    emcTaskQueueCommand(&taskPlanRunCmd);
    return 0;
}
