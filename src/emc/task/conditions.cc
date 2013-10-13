#include <stdio.h>		// vsprintf()
#include <string.h>		// strcpy()
#include <stdarg.h>		// va_start()
#include <stdlib.h>		// exit()
#include <signal.h>		// signal(), SIGINT
#include <float.h>		// DBL_MAX
#include <sys/types.h>		// pid_t
#include <unistd.h>		// fork()
#include <sys/wait.h>		// waitpid(), WNOHANG, WIFEXITED
#include <ctype.h>		// isspace()
#include <libintl.h>
#include <locale.h>

#include "rcs.hh"		// NML classes, nmlErrorFormat()
#include "emc.hh"		// EMC NML
#include "emc_nml.hh"
#include "canon.hh"		// CANON_TOOL_TABLE stuff
#include "inifile.hh"		// INIFILE
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "emcglb.h"		// EMC_INIFILE,NMLFILE, EMC_TASK_CYCLE_TIME
#include "interp_return.hh"	// public interpreter return values
#include "interp_internal.hh"	// interpreter private definitions
#include "rcs_print.hh"
#include "timer.hh"
#include "nml_oi.hh"
#include "task.hh"		// emcTaskCommand etc
#include "taskclass.hh"
#include "motion.h"             // EMCMOT_ORIENT_*

/*
   emcTaskCheckPreconditions() is called for commands on the interp_list.
   Immediate commands, i.e., commands sent from calls to emcTaskIssueCommand()
   in emcTaskPlan() directly, are not handled here.

   The return value is a state for emcTaskExecute() to wait on, e.g.,
   EMC_TASK_EXEC_WAITING_FOR_MOTION, before the command can be sent out.
   */
int emcTaskCheckPreconditions(NMLmsg * cmd)
{
    if (0 == cmd) {
	return EMC_TASK_EXEC_DONE;
    }

    switch (cmd->type) {
	// operator messages, if queued, will go out when everything before
	// them is done
    case EMC_OPERATOR_ERROR_TYPE:
    case EMC_OPERATOR_TEXT_TYPE:
    case EMC_OPERATOR_DISPLAY_TYPE:
    case EMC_SYSTEM_CMD_TYPE:
    case EMC_TRAJ_PROBE_TYPE:	// prevent blending of this
    case EMC_TRAJ_RIGID_TAP_TYPE: //and this
    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:	// and this
    case EMC_AUX_INPUT_WAIT_TYPE:
    case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TRAJ_LINEAR_MOVE_TYPE:
    case EMC_TRAJ_CIRCULAR_MOVE_TYPE:
    case EMC_TRAJ_SET_VELOCITY_TYPE:
    case EMC_TRAJ_SET_ACCELERATION_TYPE:
    case EMC_TRAJ_SET_TERM_COND_TYPE:
    case EMC_TRAJ_SET_SPINDLESYNC_TYPE:
    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_IO;
	break;

    case EMC_TRAJ_SET_OFFSET_TYPE:
	// this applies the tool length offset variable after previous
	// motions
    case EMC_TRAJ_SET_G5X_TYPE:
    case EMC_TRAJ_SET_G92_TYPE:
    case EMC_TRAJ_SET_ROTATION_TYPE:
	// this applies the program origin after previous motions
	return EMC_TASK_EXEC_WAITING_FOR_MOTION;
	break;

    case EMC_TOOL_LOAD_TYPE:
    case EMC_TOOL_UNLOAD_TYPE:
    case EMC_TOOL_START_CHANGE_TYPE:
    case EMC_COOLANT_MIST_ON_TYPE:
    case EMC_COOLANT_MIST_OFF_TYPE:
    case EMC_COOLANT_FLOOD_ON_TYPE:
    case EMC_COOLANT_FLOOD_OFF_TYPE:
    case EMC_SPINDLE_SPEED_TYPE:
    case EMC_SPINDLE_ON_TYPE:
    case EMC_SPINDLE_OFF_TYPE:
    case EMC_SPINDLE_ORIENT_TYPE: // not sure
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TOOL_PREPARE_TYPE:
    case EMC_LUBE_ON_TYPE:
    case EMC_LUBE_OFF_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_IO;
	break;

    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
    case EMC_TOOL_SET_OFFSET_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TOOL_SET_NUMBER_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_IO;
	break;

    case EMC_TASK_PLAN_PAUSE_TYPE:
    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	/* pause on the interp list is queued, so wait until all are done */
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TASK_PLAN_END_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TASK_PLAN_INIT_TYPE:
    case EMC_TASK_PLAN_RUN_TYPE:
    case EMC_TASK_PLAN_SYNCH_TYPE:
    case EMC_TASK_PLAN_EXECUTE_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_TRAJ_DELAY_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO;
	break;

    case EMC_MOTION_SET_AOUT_TYPE:
	if (((EMC_MOTION_SET_AOUT *) cmd)->now) {
	    return EMC_TASK_EXEC_WAITING_FOR_MOTION;
	}
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_MOTION_SET_DOUT_TYPE:
	if (((EMC_MOTION_SET_DOUT *) cmd)->now) {
	    return EMC_TASK_EXEC_WAITING_FOR_MOTION;
	}
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_MOTION_ADAPTIVE_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_MOTION;
	break;

    case EMC_EXEC_PLUGIN_CALL_TYPE:
    case EMC_IO_PLUGIN_CALL_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;


    default:
	// unrecognized command
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print_error("preconditions: unrecognized command %d:%s\n",
			    (int)cmd->type, emc_symbol_lookup(cmd->type));
	}
	return EMC_TASK_EXEC_ERROR;
	break;
    }

    return EMC_TASK_EXEC_DONE;
}


/*
   emcTaskCheckPostconditions() is called for commands on the interp_list.
   Immediate commands, i.e., commands sent from calls to emcTaskIssueCommand()
   in emcTaskPlan() directly, are not handled here.

   The return value is a state for emcTaskExecute() to wait on, e.g.,
   EMC_TASK_EXEC_WAITING_FOR_MOTION, after the command has finished and
   before any other commands can be sent out.
   */
int emcTaskCheckPostconditions(NMLmsg * cmd)
{
    if (0 == cmd) {
	return EMC_TASK_EXEC_DONE;
    }

    switch (cmd->type) {
    case EMC_OPERATOR_ERROR_TYPE:
    case EMC_OPERATOR_TEXT_TYPE:
    case EMC_OPERATOR_DISPLAY_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_SYSTEM_CMD_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_SYSTEM_CMD;
	break;

    case EMC_TRAJ_LINEAR_MOVE_TYPE:
    case EMC_TRAJ_CIRCULAR_MOVE_TYPE:
    case EMC_TRAJ_SET_VELOCITY_TYPE:
    case EMC_TRAJ_SET_ACCELERATION_TYPE:
    case EMC_TRAJ_SET_TERM_COND_TYPE:
    case EMC_TRAJ_SET_SPINDLESYNC_TYPE:
    case EMC_TRAJ_SET_OFFSET_TYPE:
    case EMC_TRAJ_SET_G5X_TYPE:
    case EMC_TRAJ_SET_G92_TYPE:
    case EMC_TRAJ_SET_ROTATION_TYPE:
    case EMC_TRAJ_PROBE_TYPE:
    case EMC_TRAJ_RIGID_TAP_TYPE:
    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE:
    case EMC_TRAJ_SET_TELEOP_VECTOR_TYPE:
    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_TOOL_PREPARE_TYPE:
    case EMC_TOOL_LOAD_TYPE:
    case EMC_TOOL_UNLOAD_TYPE:
    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
    case EMC_TOOL_START_CHANGE_TYPE:
    case EMC_TOOL_SET_OFFSET_TYPE:
    case EMC_TOOL_SET_NUMBER_TYPE:
    case EMC_SPINDLE_SPEED_TYPE:
    case EMC_SPINDLE_ON_TYPE:
    case EMC_SPINDLE_OFF_TYPE:
    case EMC_SPINDLE_ORIENT_TYPE:
    case EMC_COOLANT_MIST_ON_TYPE:
    case EMC_COOLANT_MIST_OFF_TYPE:
    case EMC_COOLANT_FLOOD_ON_TYPE:
    case EMC_COOLANT_FLOOD_OFF_TYPE:
    case EMC_LUBE_ON_TYPE:
    case EMC_LUBE_OFF_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_TASK_PLAN_RUN_TYPE:
    case EMC_TASK_PLAN_PAUSE_TYPE:
    case EMC_TASK_PLAN_END_TYPE:
    case EMC_TASK_PLAN_INIT_TYPE:
    case EMC_TASK_PLAN_SYNCH_TYPE:
    case EMC_TASK_PLAN_EXECUTE_TYPE:
    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_SPINDLE_ORIENTED;
	break;

    case EMC_TRAJ_DELAY_TYPE:
    case EMC_AUX_INPUT_WAIT_TYPE:
	return EMC_TASK_EXEC_WAITING_FOR_DELAY;
	break;

    case EMC_MOTION_SET_AOUT_TYPE:
    case EMC_MOTION_SET_DOUT_TYPE:
    case EMC_MOTION_ADAPTIVE_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    case EMC_EXEC_PLUGIN_CALL_TYPE:
    case EMC_IO_PLUGIN_CALL_TYPE:
	return EMC_TASK_EXEC_DONE;
	break;

    default:
	// unrecognized command
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print_error("postconditions: unrecognized command %d:%s\n",
			    (int)cmd->type, emc_symbol_lookup(cmd->type));
	}
	return EMC_TASK_EXEC_DONE;
	break;
    }
    return EMC_TASK_EXEC_DONE; // unreached
}
