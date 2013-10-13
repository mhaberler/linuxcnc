#include "emc_nml.hh"
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "emcglb.h"		// EMC_INIFILE,NMLFILE, EMC_TASK_CYCLE_TIME
#include "interp_internal.hh"	// interpreter private definitions
#include "rcs_print.hh"
#include "task.hh"		// emcTaskCommand etc
#include "readahead.hh"
#include "interpqueue.hh"		// emcTaskCommand etc
#include "issue.hh"
#include "main.hh"

// XXX

/*
  emcTaskPlan()

  Planner for NC code or manual mode operations
  */
int emcTaskPlan(RCS_CMD_MSG *emcCommand, EMC_STAT *emcStatus)
{
    NMLTYPE type;
    int retval = 0;

    // check for new command
    if (emcCommand->serial_number != emcStatus->echo_serial_number) {
	// flag it here locally as a new command
	type = emcCommand->type;
    } else {
	// no new command-- reset local flag
	type = 0;
    }

    // handle any new command
    switch (emcStatus->task.state) {
    case EMC_TASK_STATE_OFF:
    case EMC_TASK_STATE_ESTOP:
    case EMC_TASK_STATE_ESTOP_RESET:

	// now switch on the mode
	switch (emcStatus->task.mode) {
	case EMC_TASK_MODE_MANUAL:
	case EMC_TASK_MODE_AUTO:
	case EMC_TASK_MODE_MDI:

	    // now switch on the command
	    switch (type) {
	    case 0:
	    case EMC_NULL_TYPE:
		// no command
		break;

		// immediate commands
	    case EMC_AXIS_SET_BACKLASH_TYPE:
	    case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
	    case EMC_AXIS_DISABLE_TYPE:
	    case EMC_AXIS_ENABLE_TYPE:
	    case EMC_AXIS_SET_FERROR_TYPE:
	    case EMC_AXIS_SET_MIN_FERROR_TYPE:
	    case EMC_AXIS_ABORT_TYPE:
	    case EMC_AXIS_LOAD_COMP_TYPE:
	    case EMC_AXIS_UNHOME_TYPE:
	    case EMC_TRAJ_SET_SCALE_TYPE:
	    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
	    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
	    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
	    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	    case EMC_TRAJ_SET_VELOCITY_TYPE:
	    case EMC_TRAJ_SET_ACCELERATION_TYPE:
	    case EMC_TASK_INIT_TYPE:
	    case EMC_TASK_SET_MODE_TYPE:
	    case EMC_TASK_SET_STATE_TYPE:
	    case EMC_TASK_PLAN_INIT_TYPE:
	    case EMC_TASK_PLAN_OPEN_TYPE:
	    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
	    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
	    case EMC_TASK_ABORT_TYPE:
	    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
	    case EMC_TRAJ_PROBE_TYPE:
	    case EMC_AUX_INPUT_WAIT_TYPE:
	    case EMC_MOTION_SET_DOUT_TYPE:
	    case EMC_MOTION_ADAPTIVE_TYPE:
	    case EMC_MOTION_SET_AOUT_TYPE:
	    case EMC_TRAJ_RIGID_TAP_TYPE:
	    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE:
	    case EMC_SET_DEBUG_TYPE:
		retval = emcTaskIssueCommand(emcCommand);
		break;

		// one case where we need to be in manual mode
	    case EMC_AXIS_OVERRIDE_LIMITS_TYPE:
		retval = 0;
		if (emcStatus->task.mode == EMC_TASK_MODE_MANUAL) {
		    retval = emcTaskIssueCommand(emcCommand);
		}
		break;

	    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
	    case EMC_TOOL_SET_OFFSET_TYPE:
		// send to IO
		emcTaskQueueCommand(emcCommand);
		// signify no more reading
		emcTaskPlanSetWait();
		// then resynch interpreter
		emcTaskQueueSynchCmd();
		break;

	    case EMC_TOOL_SET_NUMBER_TYPE:
		// send to IO
		emcTaskQueueCommand(emcCommand);
		// then resynch interpreter
		emcTaskQueueSynchCmd();
		break;

	    default:
		emcOperatorError(0,
				 _
				 ("command (%s) cannot be executed until the machine is out of E-stop and turned on"),
				 emc_symbol_lookup(type));
		retval = -1;
		break;

	    }			// switch (type)

	default:
	    // invalid mode
	    break;

	}			// switch (mode)

	break;			// case EMC_TASK_STATE_OFF,ESTOP,ESTOP_RESET

    case EMC_TASK_STATE_ON:
	/* we can do everything (almost) when the machine is on, so let's
	   switch on the execution mode */
	switch (emcStatus->task.mode) {
	case EMC_TASK_MODE_MANUAL:	// ON, MANUAL
	    switch (type) {
	    case 0:
	    case EMC_NULL_TYPE:
		// no command
		break;

		// immediate commands

	    case EMC_AXIS_DISABLE_TYPE:
	    case EMC_AXIS_ENABLE_TYPE:
	    case EMC_AXIS_SET_BACKLASH_TYPE:
	    case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
	    case EMC_AXIS_SET_FERROR_TYPE:
	    case EMC_AXIS_SET_MIN_FERROR_TYPE:
	    case EMC_AXIS_SET_MAX_POSITION_LIMIT_TYPE:
	    case EMC_AXIS_SET_MIN_POSITION_LIMIT_TYPE:
	    case EMC_AXIS_ABORT_TYPE:
	    case EMC_AXIS_HALT_TYPE:
	    case EMC_AXIS_HOME_TYPE:
	    case EMC_AXIS_UNHOME_TYPE:
	    case EMC_AXIS_JOG_TYPE:
	    case EMC_AXIS_INCR_JOG_TYPE:
	    case EMC_AXIS_ABS_JOG_TYPE:
	    case EMC_AXIS_OVERRIDE_LIMITS_TYPE:
	    case EMC_TRAJ_PAUSE_TYPE:
	    case EMC_TRAJ_RESUME_TYPE:
	    case EMC_TRAJ_ABORT_TYPE:
	    case EMC_TRAJ_SET_SCALE_TYPE:
	    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
	    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
	    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
	    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	    case EMC_SPINDLE_SPEED_TYPE:
	    case EMC_SPINDLE_ON_TYPE:
	    case EMC_SPINDLE_OFF_TYPE:
	    case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
	    case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
	    case EMC_SPINDLE_INCREASE_TYPE:
	    case EMC_SPINDLE_DECREASE_TYPE:
	    case EMC_SPINDLE_CONSTANT_TYPE:
	    case EMC_COOLANT_MIST_ON_TYPE:
	    case EMC_COOLANT_MIST_OFF_TYPE:
	    case EMC_COOLANT_FLOOD_ON_TYPE:
	    case EMC_COOLANT_FLOOD_OFF_TYPE:
	    case EMC_LUBE_ON_TYPE:
	    case EMC_LUBE_OFF_TYPE:
	    case EMC_TASK_SET_MODE_TYPE:
	    case EMC_TASK_SET_STATE_TYPE:
	    case EMC_TASK_ABORT_TYPE:
	    case EMC_TASK_PLAN_PAUSE_TYPE:
	    case EMC_TASK_PLAN_RESUME_TYPE:
	    case EMC_TASK_PLAN_INIT_TYPE:
	    case EMC_TASK_PLAN_SYNCH_TYPE:
	    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
	    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
	    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
	    case EMC_TRAJ_PROBE_TYPE:
	    case EMC_AUX_INPUT_WAIT_TYPE:
	    case EMC_MOTION_SET_DOUT_TYPE:
	    case EMC_MOTION_SET_AOUT_TYPE:
	    case EMC_MOTION_ADAPTIVE_TYPE:
	    case EMC_TRAJ_RIGID_TAP_TYPE:
	    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE:
	    case EMC_TRAJ_SET_TELEOP_VECTOR_TYPE:
	    case EMC_SET_DEBUG_TYPE:
		retval = emcTaskIssueCommand(emcCommand);
		break;

		// queued commands

	    case EMC_TASK_PLAN_EXECUTE_TYPE:
		// resynch the interpreter, since we may have moved
		// externally
		emcTaskQueueSynchCmd();
		// and now call for interpreter execute
		retval = emcTaskIssueCommand(emcCommand);
		break;

	    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
	    case EMC_TOOL_SET_OFFSET_TYPE:
		// send to IO
		emcTaskQueueCommand(emcCommand);
		// signify no more reading
		emcTaskPlanSetWait();
		// then resynch interpreter
		emcTaskQueueSynchCmd();
		break;

	    case EMC_TOOL_SET_NUMBER_TYPE:
		// send to IO
		emcTaskQueueCommand(emcCommand);
		// then resynch interpreter
		emcTaskQueueSynchCmd();
		break;

		// otherwise we can't handle it

	    default:
		emcOperatorError(0, _("can't do that (%s:%d) in manual mode"),
				 emc_symbol_lookup(type),(int) type);
		retval = -1;
		break;

	    }			// switch (type) in ON, MANUAL

	    break;		// case EMC_TASK_MODE_MANUAL

	case EMC_TASK_MODE_AUTO:	// ON, AUTO
	    switch (emcStatus->task.interpState) {
	    case EMC_TASK_INTERP_IDLE:	// ON, AUTO, IDLE
		switch (type) {
		case 0:
		case EMC_NULL_TYPE:
		    // no command
		    break;

		    // immediate commands

		case EMC_AXIS_SET_BACKLASH_TYPE:
		case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
		case EMC_AXIS_SET_FERROR_TYPE:
		case EMC_AXIS_SET_MIN_FERROR_TYPE:
		case EMC_AXIS_UNHOME_TYPE:
		case EMC_TRAJ_PAUSE_TYPE:
		case EMC_TRAJ_RESUME_TYPE:
		case EMC_TRAJ_ABORT_TYPE:
		case EMC_TRAJ_SET_SCALE_TYPE:
		case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
		case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
		case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	        case EMC_TRAJ_SET_FH_ENABLE_TYPE:
		case EMC_TRAJ_SET_SO_ENABLE_TYPE:
		case EMC_SPINDLE_SPEED_TYPE:
		case EMC_SPINDLE_ORIENT_TYPE:
		case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
		case EMC_SPINDLE_ON_TYPE:
		case EMC_SPINDLE_OFF_TYPE:
		case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
		case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
		case EMC_SPINDLE_INCREASE_TYPE:
		case EMC_SPINDLE_DECREASE_TYPE:
		case EMC_SPINDLE_CONSTANT_TYPE:
		case EMC_COOLANT_MIST_ON_TYPE:
		case EMC_COOLANT_MIST_OFF_TYPE:
		case EMC_COOLANT_FLOOD_ON_TYPE:
		case EMC_COOLANT_FLOOD_OFF_TYPE:
		case EMC_LUBE_ON_TYPE:
		case EMC_LUBE_OFF_TYPE:
		case EMC_TASK_SET_MODE_TYPE:
		case EMC_TASK_SET_STATE_TYPE:
		case EMC_TASK_ABORT_TYPE:
		case EMC_TASK_PLAN_INIT_TYPE:
		case EMC_TASK_PLAN_OPEN_TYPE:
		case EMC_TASK_PLAN_RUN_TYPE:
		case EMC_TASK_PLAN_EXECUTE_TYPE:
		case EMC_TASK_PLAN_PAUSE_TYPE:
		case EMC_TASK_PLAN_RESUME_TYPE:
		case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
		case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
		case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
		case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
		case EMC_TRAJ_PROBE_TYPE:
		case EMC_AUX_INPUT_WAIT_TYPE:
		case EMC_TRAJ_RIGID_TAP_TYPE:
		case EMC_SET_DEBUG_TYPE:
		    retval = emcTaskIssueCommand(emcCommand);
		    break;

		case EMC_TASK_PLAN_STEP_TYPE:
		    // handles case where first action is to step the program
		    taskPlanRunCmd.line = 1;	// run from start
		    /*! \todo FIXME-- can have GUI set this; send a run instead of a
		       step */
		    emcTaskQueueRunCmd(1);	// run from start

		    if(retval != 0) break;
		    emcTrajPause();
		    if (emcStatus->task.interpState != EMC_TASK_INTERP_PAUSED) {
			set_interpResumeState(emcStatus->task.interpState);
		    }
		    emcStatus->task.interpState = EMC_TASK_INTERP_PAUSED;
		    emcStatus->task.task_paused = 1;
		    retval = 0;
		    break;

		case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
		case EMC_TOOL_SET_OFFSET_TYPE:
		    // send to IO
		    emcTaskQueueCommand(emcCommand);
		    // signify no more reading
		    emcTaskPlanSetWait();
		    // then resynch interpreter
		    emcTaskQueueSynchCmd();

		    break;

		    // otherwise we can't handle it
		default:
		    emcOperatorError(0, _
			    ("can't do that (%s) in auto mode with the interpreter idle"),
			    emc_symbol_lookup(type));
		    retval = -1;
		    break;

		}		// switch (type) in ON, AUTO, IDLE

		break;		// EMC_TASK_INTERP_IDLE

	    case EMC_TASK_INTERP_READING:	// ON, AUTO, READING
		switch (type) {
		case 0:
		case EMC_NULL_TYPE:
		    // no command
		    break;

		    // immediate commands

		case EMC_AXIS_SET_BACKLASH_TYPE:
		case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
		case EMC_AXIS_SET_FERROR_TYPE:
		case EMC_AXIS_SET_MIN_FERROR_TYPE:
		case EMC_AXIS_UNHOME_TYPE:
		case EMC_TRAJ_PAUSE_TYPE:
		case EMC_TRAJ_RESUME_TYPE:
		case EMC_TRAJ_ABORT_TYPE:
		case EMC_TRAJ_SET_SCALE_TYPE:
                case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
		case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
		case EMC_TRAJ_SET_FO_ENABLE_TYPE:
		case EMC_TRAJ_SET_FH_ENABLE_TYPE:
		case EMC_TRAJ_SET_SO_ENABLE_TYPE:
		case EMC_SPINDLE_INCREASE_TYPE:
		case EMC_SPINDLE_DECREASE_TYPE:
		case EMC_SPINDLE_CONSTANT_TYPE:
		case EMC_TASK_PLAN_PAUSE_TYPE:
		case EMC_TASK_PLAN_RESUME_TYPE:
		case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
		case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
		case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
		case EMC_TASK_SET_MODE_TYPE:
		case EMC_TASK_SET_STATE_TYPE:
		case EMC_TASK_ABORT_TYPE:
		case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
		case EMC_TRAJ_PROBE_TYPE:
		case EMC_AUX_INPUT_WAIT_TYPE:
		case EMC_TRAJ_RIGID_TAP_TYPE:
		case EMC_SET_DEBUG_TYPE:
                case EMC_COOLANT_MIST_ON_TYPE:
                case EMC_COOLANT_MIST_OFF_TYPE:
                case EMC_COOLANT_FLOOD_ON_TYPE:
                case EMC_COOLANT_FLOOD_OFF_TYPE:
                case EMC_LUBE_ON_TYPE:
                case EMC_LUBE_OFF_TYPE:
		    retval = emcTaskIssueCommand(emcCommand);
		    return retval;
		    break;

		case EMC_TASK_PLAN_STEP_TYPE:
		    stepping = 1;	// set stepping mode in case it's not
		    steppingWait = 0;	// clear the wait
		    break;

		    // otherwise we can't handle it
		default:
		    emcOperatorError(0, _
			    ("can't do that (%s) in auto mode with the interpreter reading"),
			    emc_symbol_lookup(type));
		    retval = -1;
		    break;

		}		// switch (type) in ON, AUTO, READING

               // handle interp readahead logic
                readahead_reading();

		break;		// EMC_TASK_INTERP_READING

	    case EMC_TASK_INTERP_PAUSED:	// ON, AUTO, PAUSED
		switch (type) {
		case 0:
		case EMC_NULL_TYPE:
		    // no command
		    break;

		    // immediate commands

		case EMC_AXIS_SET_BACKLASH_TYPE:
		case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
		case EMC_AXIS_SET_FERROR_TYPE:
		case EMC_AXIS_SET_MIN_FERROR_TYPE:
		case EMC_AXIS_UNHOME_TYPE:
		case EMC_TRAJ_PAUSE_TYPE:
		case EMC_TRAJ_RESUME_TYPE:
		case EMC_TRAJ_ABORT_TYPE:
		case EMC_TRAJ_SET_SCALE_TYPE:
		case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
		case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
		case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	        case EMC_TRAJ_SET_FH_ENABLE_TYPE:
		case EMC_TRAJ_SET_SO_ENABLE_TYPE:
		case EMC_SPINDLE_SPEED_TYPE:
		case EMC_SPINDLE_ORIENT_TYPE:
		case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
		case EMC_SPINDLE_ON_TYPE:
		case EMC_SPINDLE_OFF_TYPE:
		case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
		case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
		case EMC_SPINDLE_INCREASE_TYPE:
		case EMC_SPINDLE_DECREASE_TYPE:
		case EMC_SPINDLE_CONSTANT_TYPE:
		case EMC_COOLANT_MIST_ON_TYPE:
		case EMC_COOLANT_MIST_OFF_TYPE:
		case EMC_COOLANT_FLOOD_ON_TYPE:
		case EMC_COOLANT_FLOOD_OFF_TYPE:
		case EMC_LUBE_ON_TYPE:
		case EMC_LUBE_OFF_TYPE:
		case EMC_TASK_SET_MODE_TYPE:
		case EMC_TASK_SET_STATE_TYPE:
		case EMC_TASK_ABORT_TYPE:
		case EMC_TASK_PLAN_EXECUTE_TYPE:
		case EMC_TASK_PLAN_PAUSE_TYPE:
		case EMC_TASK_PLAN_RESUME_TYPE:
		case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
		case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
		case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
		case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
		case EMC_TRAJ_PROBE_TYPE:
		case EMC_AUX_INPUT_WAIT_TYPE:
		case EMC_TRAJ_RIGID_TAP_TYPE:
		case EMC_SET_DEBUG_TYPE:
		    retval = emcTaskIssueCommand(emcCommand);
		    break;

		case EMC_TASK_PLAN_STEP_TYPE:
		    stepping = 1;
		    steppingWait = 0;
		    if (emcStatus->motion.traj.paused &&
			emcStatus->motion.traj.queue > 0) {
			// there are pending motions paused; step them
			emcTrajStep();
		    } else {
			emcStatus->task.interpState = (enum EMC_TASK_INTERP_ENUM)
			    get_interpResumeState();
			// interpResumeState;
		    }
		    emcStatus->task.task_paused = 1;
		    break;

		    // otherwise we can't handle it
		default:
		    emcOperatorError(0, _
			    ("can't do that (%s) in auto mode with the interpreter paused"),
			    emc_symbol_lookup(type));
		    retval = -1;
		    break;

		}		// switch (type) in ON, AUTO, PAUSED

		break;		// EMC_TASK_INTERP_PAUSED

	    case EMC_TASK_INTERP_WAITING:
		// interpreter ran to end
		// handle input commands
		switch (type) {
		case 0:
		case EMC_NULL_TYPE:
		    // no command
		    break;

		    // immediate commands

		case EMC_AXIS_SET_BACKLASH_TYPE:
		case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
		case EMC_AXIS_SET_FERROR_TYPE:
		case EMC_AXIS_SET_MIN_FERROR_TYPE:
		case EMC_AXIS_UNHOME_TYPE:
		case EMC_TRAJ_PAUSE_TYPE:
		case EMC_TRAJ_RESUME_TYPE:
		case EMC_TRAJ_ABORT_TYPE:
		case EMC_TRAJ_SET_SCALE_TYPE:
		case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
		case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
		case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	        case EMC_TRAJ_SET_FH_ENABLE_TYPE:
		case EMC_TRAJ_SET_SO_ENABLE_TYPE:
		case EMC_SPINDLE_INCREASE_TYPE:
		case EMC_SPINDLE_DECREASE_TYPE:
		case EMC_SPINDLE_CONSTANT_TYPE:
		case EMC_TASK_PLAN_EXECUTE_TYPE:
		case EMC_TASK_PLAN_PAUSE_TYPE:
		case EMC_TASK_PLAN_RESUME_TYPE:
		case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
		case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
		case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
		case EMC_TASK_SET_MODE_TYPE:
		case EMC_TASK_SET_STATE_TYPE:
		case EMC_TASK_ABORT_TYPE:
		case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
		case EMC_TRAJ_PROBE_TYPE:
		case EMC_AUX_INPUT_WAIT_TYPE:
	        case EMC_TRAJ_RIGID_TAP_TYPE:
		case EMC_SET_DEBUG_TYPE:
                case EMC_COOLANT_MIST_ON_TYPE:
                case EMC_COOLANT_MIST_OFF_TYPE:
                case EMC_COOLANT_FLOOD_ON_TYPE:
                case EMC_COOLANT_FLOOD_OFF_TYPE:
                case EMC_LUBE_ON_TYPE:
                case EMC_LUBE_OFF_TYPE:
		    retval = emcTaskIssueCommand(emcCommand);
		    break;

		case EMC_TASK_PLAN_STEP_TYPE:
		    stepping = 1;	// set stepping mode in case it's not
		    steppingWait = 0;	// clear the wait
		    break;

		    // otherwise we can't handle it
		default:
		    emcOperatorError(0, _
			    ("can't do that (%s) in auto mode with the interpreter waiting"),
			    emc_symbol_lookup(type));
		    retval = -1;
		    break;

		}		// switch (type) in ON, AUTO, WAITING

                // handle interp readahead logic
                readahead_waiting();

		break;		// end of case EMC_TASK_INTERP_WAITING

	    default:
		// coding error
		rcs_print_error("invalid mode(%d)", emcStatus->task.mode);
		retval = -1;
		break;

	    }			// switch (mode) in ON, AUTO

	    break;		// case EMC_TASK_MODE_AUTO

	case EMC_TASK_MODE_MDI:	// ON, MDI
	    switch (type) {
	    case 0:
	    case EMC_NULL_TYPE:
		// no command
		break;

		// immediate commands

	    case EMC_AXIS_SET_BACKLASH_TYPE:
	    case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
	    case EMC_AXIS_SET_FERROR_TYPE:
	    case EMC_AXIS_SET_MIN_FERROR_TYPE:
	    case EMC_AXIS_UNHOME_TYPE:
	    case EMC_TRAJ_SET_SCALE_TYPE:
	    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
	    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
	    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
	    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	    case EMC_SPINDLE_SPEED_TYPE:
	    case EMC_SPINDLE_ORIENT_TYPE:
	    case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
	    case EMC_SPINDLE_ON_TYPE:
	    case EMC_SPINDLE_OFF_TYPE:
	    case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
	    case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
	    case EMC_SPINDLE_INCREASE_TYPE:
	    case EMC_SPINDLE_DECREASE_TYPE:
	    case EMC_SPINDLE_CONSTANT_TYPE:
	    case EMC_COOLANT_MIST_ON_TYPE:
	    case EMC_COOLANT_MIST_OFF_TYPE:
	    case EMC_COOLANT_FLOOD_ON_TYPE:
	    case EMC_COOLANT_FLOOD_OFF_TYPE:
	    case EMC_LUBE_ON_TYPE:
	    case EMC_LUBE_OFF_TYPE:
	    case EMC_TASK_SET_MODE_TYPE:
	    case EMC_TASK_SET_STATE_TYPE:
	    case EMC_TASK_PLAN_INIT_TYPE:
	    case EMC_TASK_PLAN_OPEN_TYPE:
	    case EMC_TASK_PLAN_PAUSE_TYPE:
	    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
	    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
	    case EMC_TASK_PLAN_RESUME_TYPE:
	    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	    case EMC_TASK_ABORT_TYPE:
	    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
	    case EMC_TRAJ_PROBE_TYPE:
	    case EMC_AUX_INPUT_WAIT_TYPE:
	    case EMC_MOTION_SET_DOUT_TYPE:
	    case EMC_MOTION_SET_AOUT_TYPE:
	    case EMC_MOTION_ADAPTIVE_TYPE:
	    case EMC_TRAJ_RIGID_TAP_TYPE:
	    case EMC_SET_DEBUG_TYPE:
		retval = emcTaskIssueCommand(emcCommand);
		break;

            case EMC_TASK_PLAN_EXECUTE_TYPE:
                // If there are no queued MDI commands and no commands
                // in interp_list, then this new incoming MDI command
                // can just be issued directly.  Otherwise we need to
                // queue it and deal with it later.
                if (
                    (mdi_execute_queue.len() == 0)
                    && (interp_list.len() == 0)
                    && (emcTaskCommand == NULL)
                ) {
                    retval = emcTaskIssueCommand(emcCommand);
                } else {
                    mdi_execute_queue.append(emcCommand);
                    retval = 0;
                }
                break;
	    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
	    case EMC_TOOL_SET_OFFSET_TYPE:
		// send to IO
		emcTaskQueueCommand(emcCommand);
		// signify no more reading
		emcTaskPlanSetWait();
		// then resynch interpreter
		emcTaskQueueSynchCmd();
		break;

		// otherwise we can't handle it
	    default:
		emcOperatorError(0, _("can't do that (%s:%d) in MDI mode"),
			emc_symbol_lookup(type),(int) type);

		retval = -1;
		break;

	    }			// switch (type) in ON, MDI
	    mdi_execute_hook();

	    break;		// case EMC_TASK_MODE_MDI

	default:
	    break;

	}			// switch (mode)

	break;			// case EMC_TASK_STATE_ON

    default:
	break;

    }				// switch (task.state)

    return retval;
}
