#include "emc_nml.hh"
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "interp_return.hh"	// public interpreter return values
#include "interp_internal.hh"	// _() macro
#include "rcs_print.hh"
#include "timer.hh"
#include "main.hh"		// emcTaskCommand etc
#include "taskclass.hh"

#include "issue.hh"
#include "interpqueue.hh"
#include "readahead.hh"
#include "systemcmd.hh"		// emcTaskCommand etc

#include "nmlsetup.hh"    // emcCommandBuffer
#include "zmqcmds.hh"

// config
extern int no_force_homing;

// from emctaskmain.cc:
extern int emcAuxInputWaitType;
extern int emcAuxInputWaitIndex;
// delay counter
extern double taskExecDelayTimeout;
extern int programStartLine;	// which line to run program from

extern const char *current_cmd;
extern std::string origin;
// Variables to handle MDI call interrupts
// Depth of call level before interrupted MDI call
int mdi_execute_level = -1;
// Schedule execute(0) command
int mdi_execute_next = 0;
// Wait after interrupted command
int mdi_execute_wait = 0;

// shorthand typecasting ptrs
static EMC_AXIS_HALT *axis_halt_msg;
static EMC_AXIS_DISABLE *disable_msg;
static EMC_AXIS_ENABLE *enable_msg;
static EMC_AXIS_HOME *home_msg;
static EMC_AXIS_UNHOME *unhome_msg;
static EMC_AXIS_JOG *jog_msg;
static EMC_AXIS_ABORT *axis_abort_msg;
static EMC_AXIS_INCR_JOG *incr_jog_msg;
static EMC_AXIS_ABS_JOG *abs_jog_msg;
static EMC_AXIS_SET_BACKLASH *set_backlash_msg;
static EMC_AXIS_SET_HOMING_PARAMS *set_homing_params_msg;
static EMC_AXIS_SET_FERROR *set_ferror_msg;
static EMC_AXIS_SET_MIN_FERROR *set_min_ferror_msg;
static EMC_AXIS_SET_MAX_POSITION_LIMIT *set_max_limit_msg;
static EMC_AXIS_SET_MIN_POSITION_LIMIT *set_min_limit_msg;
static EMC_AXIS_OVERRIDE_LIMITS *axis_lim_msg;
//static EMC_AXIS_SET_OUTPUT *axis_output_msg;
static EMC_AXIS_LOAD_COMP *axis_load_comp_msg;
//static EMC_AXIS_SET_STEP_PARAMS *set_step_params_msg;

static EMC_TRAJ_SET_SCALE *emcTrajSetScaleMsg;
static EMC_TRAJ_SET_MAX_VELOCITY *emcTrajSetMaxVelocityMsg;
static EMC_TRAJ_SET_SPINDLE_SCALE *emcTrajSetSpindleScaleMsg;
static EMC_TRAJ_SET_VELOCITY *emcTrajSetVelocityMsg;
static EMC_TRAJ_SET_ACCELERATION *emcTrajSetAccelerationMsg;
static EMC_TRAJ_LINEAR_MOVE *emcTrajLinearMoveMsg;
static EMC_TRAJ_CIRCULAR_MOVE *emcTrajCircularMoveMsg;
static EMC_TRAJ_DELAY *emcTrajDelayMsg;
static EMC_TRAJ_SET_TERM_COND *emcTrajSetTermCondMsg;
static EMC_TRAJ_SET_SPINDLESYNC *emcTrajSetSpindlesyncMsg;

// These classes are commented out because the compiler
// complains that they are "defined but not used".
//static EMC_MOTION_SET_AOUT *emcMotionSetAoutMsg;
//static EMC_MOTION_SET_DOUT *emcMotionSetDoutMsg;

static EMC_SPINDLE_SPEED *spindle_speed_msg;
static EMC_SPINDLE_ORIENT *spindle_orient_msg;
static EMC_SPINDLE_WAIT_ORIENT_COMPLETE *wait_spindle_orient_complete_msg;
static EMC_SPINDLE_ON *spindle_on_msg;
static EMC_TOOL_PREPARE *tool_prepare_msg;
static EMC_TOOL_LOAD_TOOL_TABLE *load_tool_table_msg;
static EMC_TOOL_SET_OFFSET *emc_tool_set_offset_msg;
static EMC_TOOL_SET_NUMBER *emc_tool_set_number_msg;
static EMC_TASK_SET_MODE *mode_msg;
static EMC_TASK_SET_STATE *state_msg;
static EMC_TASK_PLAN_RUN *run_msg;
static EMC_TASK_PLAN_EXECUTE *execute_msg;
static EMC_TASK_PLAN_OPEN *open_msg;
static EMC_TASK_PLAN_SET_OPTIONAL_STOP *os_msg;
static EMC_TASK_PLAN_SET_BLOCK_DELETE *bd_msg;

static EMC_AUX_INPUT_WAIT *emcAuxInputWaitMsg;

static int all_homed(void) {
    for(int i=0; i<9; i++) {
        unsigned int mask = 1<<i;
        if((emcStatus->motion.traj.axis_mask & mask) && !emcStatus->motion.axis[i].homed)
            return 0;
    }
    return 1;
}

// issues command immediately
int emcTaskIssueCommand(NMLmsg * cmd)
{
    int retval = 0;
    int execRetval = 0;

    if (0 == cmd) {
        if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
            rcs_print("emcTaskIssueCommand() null command\n");
        }
	return 0;
    }
    if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	rcs_print("Issuing %s -- \t (%s)\n", emcSymbolLookup(cmd->type),
		  emcCommandBuffer->msg2str(cmd));
    }
    switch (cmd->type) {
	// general commands

    case EMC_OPERATOR_ERROR_TYPE:
	retval = emcOperatorError(((EMC_OPERATOR_ERROR *) cmd)->id,
				  "%s", ((EMC_OPERATOR_ERROR *) cmd)->error);
	break;

    case EMC_OPERATOR_TEXT_TYPE:
	retval = emcOperatorText(((EMC_OPERATOR_TEXT *) cmd)->id,
				 "%s", ((EMC_OPERATOR_TEXT *) cmd)->text);
	break;

    case EMC_OPERATOR_DISPLAY_TYPE:
	retval = emcOperatorDisplay(((EMC_OPERATOR_DISPLAY *) cmd)->id,
				    "%s", ((EMC_OPERATOR_DISPLAY *) cmd)->
				    display);
	break;

    case EMC_SYSTEM_CMD_TYPE:
	retval = emcSystemCmd(((EMC_SYSTEM_CMD *) cmd)->string);
	break;

	// axis commands

    case EMC_AXIS_DISABLE_TYPE:
	disable_msg = (EMC_AXIS_DISABLE *) cmd;
	retval = emcAxisDisable(disable_msg->axis);
	break;

    case EMC_AXIS_ENABLE_TYPE:
	enable_msg = (EMC_AXIS_ENABLE *) cmd;
	retval = emcAxisEnable(enable_msg->axis);
	break;

    case EMC_AXIS_HOME_TYPE:
	home_msg = (EMC_AXIS_HOME *) cmd;
	retval = emcAxisHome(home_msg->axis);
	break;

    case EMC_AXIS_UNHOME_TYPE:
	unhome_msg = (EMC_AXIS_UNHOME *) cmd;
	retval = emcAxisUnhome(unhome_msg->axis);
	break;

    case EMC_AXIS_JOG_TYPE:
	jog_msg = (EMC_AXIS_JOG *) cmd;
	retval = emcAxisJog(jog_msg->axis, jog_msg->vel);
	break;

    case EMC_AXIS_ABORT_TYPE:
	axis_abort_msg = (EMC_AXIS_ABORT *) cmd;
	retval = emcAxisAbort(axis_abort_msg->axis);
	break;

    case EMC_AXIS_INCR_JOG_TYPE:
	incr_jog_msg = (EMC_AXIS_INCR_JOG *) cmd;
	retval = emcAxisIncrJog(incr_jog_msg->axis,
				incr_jog_msg->incr, incr_jog_msg->vel);
	break;

    case EMC_AXIS_ABS_JOG_TYPE:
	abs_jog_msg = (EMC_AXIS_ABS_JOG *) cmd;
	retval = emcAxisAbsJog(abs_jog_msg->axis,
			       abs_jog_msg->pos, abs_jog_msg->vel);
	break;

    case EMC_AXIS_SET_BACKLASH_TYPE:
	set_backlash_msg = (EMC_AXIS_SET_BACKLASH *) cmd;
	retval =
	    emcAxisSetBacklash(set_backlash_msg->axis,
			       set_backlash_msg->backlash);
	break;

    case EMC_AXIS_SET_HOMING_PARAMS_TYPE:
	set_homing_params_msg = (EMC_AXIS_SET_HOMING_PARAMS *) cmd;
	retval = emcAxisSetHomingParams(set_homing_params_msg->axis,
					set_homing_params_msg->home,
					set_homing_params_msg->offset,
					set_homing_params_msg->home_final_vel,
					set_homing_params_msg->search_vel,
					set_homing_params_msg->latch_vel,
					set_homing_params_msg->use_index,
					set_homing_params_msg->ignore_limits,
					set_homing_params_msg->is_shared,
					set_homing_params_msg->home_sequence,
					set_homing_params_msg->volatile_home,
                                        set_homing_params_msg->locking_indexer);
	break;

    case EMC_AXIS_SET_FERROR_TYPE:
	set_ferror_msg = (EMC_AXIS_SET_FERROR *) cmd;
	retval = emcAxisSetFerror(set_ferror_msg->axis,
				  set_ferror_msg->ferror);
	break;

    case EMC_AXIS_SET_MIN_FERROR_TYPE:
	set_min_ferror_msg = (EMC_AXIS_SET_MIN_FERROR *) cmd;
	retval = emcAxisSetMinFerror(set_min_ferror_msg->axis,
				     set_min_ferror_msg->ferror);
	break;

    case EMC_AXIS_SET_MAX_POSITION_LIMIT_TYPE:
	set_max_limit_msg = (EMC_AXIS_SET_MAX_POSITION_LIMIT *) cmd;
	retval = emcAxisSetMaxPositionLimit(set_max_limit_msg->axis,
					    set_max_limit_msg->limit);
	break;

    case EMC_AXIS_SET_MIN_POSITION_LIMIT_TYPE:
	set_min_limit_msg = (EMC_AXIS_SET_MIN_POSITION_LIMIT *) cmd;
	retval = emcAxisSetMinPositionLimit(set_min_limit_msg->axis,
					    set_min_limit_msg->limit);
	break;

    case EMC_AXIS_HALT_TYPE:
	axis_halt_msg = (EMC_AXIS_HALT *) cmd;
	retval = emcAxisHalt(axis_halt_msg->axis);
	break;

    case EMC_AXIS_OVERRIDE_LIMITS_TYPE:
	axis_lim_msg = (EMC_AXIS_OVERRIDE_LIMITS *) cmd;
	retval = emcAxisOverrideLimits(axis_lim_msg->axis);
	break;

    case EMC_AXIS_LOAD_COMP_TYPE:
	axis_load_comp_msg = (EMC_AXIS_LOAD_COMP *) cmd;
	retval = emcAxisLoadComp(axis_load_comp_msg->axis,
				 axis_load_comp_msg->file,
				 axis_load_comp_msg->type);
	break;

	// traj commands

    case EMC_TRAJ_SET_SCALE_TYPE:
	emcTrajSetScaleMsg = (EMC_TRAJ_SET_SCALE *) cmd;
	retval = emcTrajSetScale(emcTrajSetScaleMsg->scale);
	break;

    case EMC_TRAJ_SET_MAX_VELOCITY_TYPE:
	emcTrajSetMaxVelocityMsg = (EMC_TRAJ_SET_MAX_VELOCITY *) cmd;
	retval = emcTrajSetMaxVelocity(emcTrajSetMaxVelocityMsg->velocity);
	break;

    case EMC_TRAJ_SET_SPINDLE_SCALE_TYPE:
	emcTrajSetSpindleScaleMsg = (EMC_TRAJ_SET_SPINDLE_SCALE *) cmd;
	retval = emcTrajSetSpindleScale(emcTrajSetSpindleScaleMsg->scale);
	break;

    case EMC_TRAJ_SET_FO_ENABLE_TYPE:
	retval = emcTrajSetFOEnable(((EMC_TRAJ_SET_FO_ENABLE *) cmd)->mode);  // feed override enable/disable
	break;

    case EMC_TRAJ_SET_FH_ENABLE_TYPE:
	retval = emcTrajSetFHEnable(((EMC_TRAJ_SET_FH_ENABLE *) cmd)->mode); //feed hold enable/disable
	break;

    case EMC_TRAJ_SET_SO_ENABLE_TYPE:
	retval = emcTrajSetSOEnable(((EMC_TRAJ_SET_SO_ENABLE *) cmd)->mode); //spindle speed override enable/disable
	break;

    case EMC_TRAJ_SET_VELOCITY_TYPE:
	emcTrajSetVelocityMsg = (EMC_TRAJ_SET_VELOCITY *) cmd;
	retval = emcTrajSetVelocity(emcTrajSetVelocityMsg->velocity,
			emcTrajSetVelocityMsg->ini_maxvel);
	break;

    case EMC_TRAJ_SET_ACCELERATION_TYPE:
	emcTrajSetAccelerationMsg = (EMC_TRAJ_SET_ACCELERATION *) cmd;
	retval = emcTrajSetAcceleration(emcTrajSetAccelerationMsg->acceleration);
	break;

    case EMC_TRAJ_LINEAR_MOVE_TYPE:
	emcTrajLinearMoveMsg = (EMC_TRAJ_LINEAR_MOVE *) cmd;
        retval = emcTrajLinearMove(emcTrajLinearMoveMsg->end,
                                   emcTrajLinearMoveMsg->type, emcTrajLinearMoveMsg->vel,
                                   emcTrajLinearMoveMsg->ini_maxvel, emcTrajLinearMoveMsg->acc,
                                   emcTrajLinearMoveMsg->indexrotary);
	break;

    case EMC_TRAJ_CIRCULAR_MOVE_TYPE:
	emcTrajCircularMoveMsg = (EMC_TRAJ_CIRCULAR_MOVE *) cmd;
        retval = emcTrajCircularMove(emcTrajCircularMoveMsg->end,
                emcTrajCircularMoveMsg->center, emcTrajCircularMoveMsg->normal,
                emcTrajCircularMoveMsg->turn, emcTrajCircularMoveMsg->type,
                emcTrajCircularMoveMsg->vel,
                emcTrajCircularMoveMsg->ini_maxvel,
                emcTrajCircularMoveMsg->acc);
	break;

    case EMC_TRAJ_PAUSE_TYPE:
	emcStatus->task.task_paused = 1;
	retval = emcTrajPause();
	break;

    case EMC_TRAJ_RESUME_TYPE:
	emcStatus->task.task_paused = 0;
	retval = emcTrajResume();
	break;

    case EMC_TRAJ_ABORT_TYPE:
	retval = emcTrajAbort();
	break;

    case EMC_TRAJ_DELAY_TYPE:
	emcTrajDelayMsg = (EMC_TRAJ_DELAY *) cmd;
	// set the timeout clock to expire at 'now' + delay time
	taskExecDelayTimeout = etime() + emcTrajDelayMsg->delay;
	retval = 0;
	break;

    case EMC_TRAJ_SET_TERM_COND_TYPE:
	emcTrajSetTermCondMsg = (EMC_TRAJ_SET_TERM_COND *) cmd;
	retval = emcTrajSetTermCond(emcTrajSetTermCondMsg->cond, emcTrajSetTermCondMsg->tolerance);
	break;

    case EMC_TRAJ_SET_SPINDLESYNC_TYPE:
        emcTrajSetSpindlesyncMsg = (EMC_TRAJ_SET_SPINDLESYNC *) cmd;
        retval = emcTrajSetSpindleSync(emcTrajSetSpindlesyncMsg->feed_per_revolution, emcTrajSetSpindlesyncMsg->velocity_mode);
        break;

    case EMC_TRAJ_SET_OFFSET_TYPE:
	// update tool offset
	emcStatus->task.toolOffset = ((EMC_TRAJ_SET_OFFSET *) cmd)->offset;
        retval = emcTrajSetOffset(emcStatus->task.toolOffset);
	break;

    case EMC_TRAJ_SET_ROTATION_TYPE:
        emcStatus->task.rotation_xy = ((EMC_TRAJ_SET_ROTATION *) cmd)->rotation;
        retval = 0;
        break;

    case EMC_TRAJ_SET_G5X_TYPE:
	// struct-copy program origin
	emcStatus->task.g5x_offset = ((EMC_TRAJ_SET_G5X *) cmd)->origin;
        emcStatus->task.g5x_index = ((EMC_TRAJ_SET_G5X *) cmd)->g5x_index;
	retval = 0;
	break;
    case EMC_TRAJ_SET_G92_TYPE:
	// struct-copy program origin
	emcStatus->task.g92_offset = ((EMC_TRAJ_SET_G92 *) cmd)->origin;
	retval = 0;
	break;
    case EMC_TRAJ_CLEAR_PROBE_TRIPPED_FLAG_TYPE:
	retval = emcTrajClearProbeTrippedFlag();
	break;

    case EMC_TRAJ_PROBE_TYPE:
	retval = emcTrajProbe(
	    ((EMC_TRAJ_PROBE *) cmd)->pos,
	    ((EMC_TRAJ_PROBE *) cmd)->type,
	    ((EMC_TRAJ_PROBE *) cmd)->vel,
            ((EMC_TRAJ_PROBE *) cmd)->ini_maxvel,
	    ((EMC_TRAJ_PROBE *) cmd)->acc,
            ((EMC_TRAJ_PROBE *) cmd)->probe_type);
	break;

    case EMC_AUX_INPUT_WAIT_TYPE:
	emcAuxInputWaitMsg = (EMC_AUX_INPUT_WAIT *) cmd;
	if (emcAuxInputWaitMsg->timeout == WAIT_MODE_IMMEDIATE) { //nothing to do, CANON will get the needed value when asked by the interp
	    emcStatus->task.input_timeout = 0; // no timeout can occur
	    emcAuxInputWaitIndex = -1;
	    taskExecDelayTimeout = 0.0;
	} else {
	    emcAuxInputWaitType = emcAuxInputWaitMsg->wait_type; // remember what we are waiting for
	    emcAuxInputWaitIndex = emcAuxInputWaitMsg->index; // remember the input to look at
	    emcStatus->task.input_timeout = 2; // set timeout flag, gets cleared if input changes before timeout happens
	    // set the timeout clock to expire at 'now' + delay time
	    taskExecDelayTimeout = etime() + emcAuxInputWaitMsg->timeout;
	}
	break;

    case EMC_SPINDLE_WAIT_ORIENT_COMPLETE_TYPE:
	wait_spindle_orient_complete_msg = (EMC_SPINDLE_WAIT_ORIENT_COMPLETE *) cmd;
	taskExecDelayTimeout = etime() + wait_spindle_orient_complete_msg->timeout;
	break;

    case EMC_TRAJ_RIGID_TAP_TYPE:
	retval = emcTrajRigidTap(((EMC_TRAJ_RIGID_TAP *) cmd)->pos,
	        ((EMC_TRAJ_RIGID_TAP *) cmd)->vel,
		((EMC_TRAJ_RIGID_TAP *) cmd)->ini_maxvel,
		((EMC_TRAJ_RIGID_TAP *) cmd)->acc);
	break;

    case EMC_TRAJ_SET_TELEOP_ENABLE_TYPE:
	if (((EMC_TRAJ_SET_TELEOP_ENABLE *) cmd)->enable) {
	    retval = emcTrajSetMode(EMC_TRAJ_MODE_TELEOP);
	} else {
	    retval = emcTrajSetMode(EMC_TRAJ_MODE_FREE);
	}
	break;

    case EMC_TRAJ_SET_TELEOP_VECTOR_TYPE:
	retval =
	    emcTrajSetTeleopVector(((EMC_TRAJ_SET_TELEOP_VECTOR *) cmd)->
				   vector);
	break;

    case EMC_MOTION_SET_AOUT_TYPE:
	retval = emcMotionSetAout(((EMC_MOTION_SET_AOUT *) cmd)->index,
				  ((EMC_MOTION_SET_AOUT *) cmd)->start,
				  ((EMC_MOTION_SET_AOUT *) cmd)->end,
				  ((EMC_MOTION_SET_AOUT *) cmd)->now);
	break;

    case EMC_MOTION_SET_DOUT_TYPE:
	retval = emcMotionSetDout(((EMC_MOTION_SET_DOUT *) cmd)->index,
				  ((EMC_MOTION_SET_DOUT *) cmd)->start,
				  ((EMC_MOTION_SET_DOUT *) cmd)->end,
				  ((EMC_MOTION_SET_DOUT *) cmd)->now);
	break;

    case EMC_MOTION_ADAPTIVE_TYPE:
	retval = emcTrajSetAFEnable(((EMC_MOTION_ADAPTIVE *) cmd)->status);
	break;

    case EMC_SET_DEBUG_TYPE:
	/* set the debug level here */
	emc_debug = ((EMC_SET_DEBUG *) cmd)->debug;
	/* and in IO and motion */
	emcIoSetDebug(emc_debug);
	emcMotionSetDebug(emc_debug);
	/* and reflect it in the status-- this isn't updated continually */
	emcStatus->debug = emc_debug;
	break;

	// unimplemented ones

	// IO commands

    case EMC_SPINDLE_SPEED_TYPE:
	spindle_speed_msg = (EMC_SPINDLE_SPEED *) cmd;
	retval = emcSpindleSpeed(spindle_speed_msg->speed, spindle_speed_msg->factor, spindle_speed_msg->xoffset);
	break;

    case EMC_SPINDLE_ORIENT_TYPE:
	spindle_orient_msg = (EMC_SPINDLE_ORIENT *) cmd;
	retval = emcSpindleOrient(spindle_orient_msg->orientation, spindle_orient_msg->mode);
	break;

   case EMC_SPINDLE_ON_TYPE:
	spindle_on_msg = (EMC_SPINDLE_ON *) cmd;
	retval = emcSpindleOn(spindle_on_msg->speed, spindle_on_msg->factor, spindle_on_msg->xoffset);
	break;

    case EMC_SPINDLE_OFF_TYPE:
	retval = emcSpindleOff();
	break;

    case EMC_SPINDLE_BRAKE_RELEASE_TYPE:
	retval = emcSpindleBrakeRelease();
	break;

    case EMC_SPINDLE_INCREASE_TYPE:
	retval = emcSpindleIncrease();
	break;

    case EMC_SPINDLE_DECREASE_TYPE:
	retval = emcSpindleDecrease();
	break;

    case EMC_SPINDLE_CONSTANT_TYPE:
	retval = emcSpindleConstant();
	break;

    case EMC_SPINDLE_BRAKE_ENGAGE_TYPE:
	retval = emcSpindleBrakeEngage();
	break;

    case EMC_COOLANT_MIST_ON_TYPE:
	retval = emcCoolantMistOn();
	break;

    case EMC_COOLANT_MIST_OFF_TYPE:
	retval = emcCoolantMistOff();
	break;

    case EMC_COOLANT_FLOOD_ON_TYPE:
	retval = emcCoolantFloodOn();
	break;

    case EMC_COOLANT_FLOOD_OFF_TYPE:
	retval = emcCoolantFloodOff();
	break;

    case EMC_LUBE_ON_TYPE:
	retval = emcLubeOn();
	break;

    case EMC_LUBE_OFF_TYPE:
	retval = emcLubeOff();
	break;

    case EMC_TOOL_PREPARE_TYPE:
	tool_prepare_msg = (EMC_TOOL_PREPARE *) cmd;
	retval = emcToolPrepare(tool_prepare_msg->pocket,tool_prepare_msg->tool);
	break;

    case EMC_TOOL_START_CHANGE_TYPE:
        retval = emcToolStartChange();
	break;

    case EMC_TOOL_LOAD_TYPE:
	retval = emcToolLoad();
	break;

    case EMC_TOOL_UNLOAD_TYPE:
	retval = emcToolUnload();
	break;

    case EMC_TOOL_LOAD_TOOL_TABLE_TYPE:
	load_tool_table_msg = (EMC_TOOL_LOAD_TOOL_TABLE *) cmd;
	retval = emcToolLoadToolTable(load_tool_table_msg->file);
	break;

    case EMC_TOOL_SET_OFFSET_TYPE:
	emc_tool_set_offset_msg = (EMC_TOOL_SET_OFFSET *) cmd;
	retval = emcToolSetOffset(emc_tool_set_offset_msg->pocket,
                                  emc_tool_set_offset_msg->toolno,
                                  emc_tool_set_offset_msg->offset,
                                  emc_tool_set_offset_msg->diameter,
                                  emc_tool_set_offset_msg->frontangle,
                                  emc_tool_set_offset_msg->backangle,
                                  emc_tool_set_offset_msg->orientation);
	break;

    case EMC_TOOL_SET_NUMBER_TYPE:
	emc_tool_set_number_msg = (EMC_TOOL_SET_NUMBER *) cmd;
	retval = emcToolSetNumber(emc_tool_set_number_msg->tool);
	break;

	// task commands

    case EMC_TASK_INIT_TYPE:
	retval = emcTaskInit();
	break;

    case EMC_TASK_ABORT_TYPE:
	// abort everything
	emcTaskAbort();
        emcIoAbort(EMC_ABORT_TASK_ABORT);
        emcSpindleAbort();
	mdi_execute_abort();
	emcAbortCleanup(EMC_ABORT_TASK_ABORT);

	// the interp_list has been cleared
	// and a EMC_TASK_PLAN_SYNCH queued, so size = 1

	// conceptually the ticket issuing the abort is done here
	// since the synch() is immediately executed, need to update
	// the current ticket status here since the completion
	// logic doesnt work in this case:
	publish_ticket_update(RCS_DONE, emcStatus->ticket,
			      origin, current_cmd);
	retval = 0;
	break;

	// mode and state commands

    case EMC_TASK_SET_MODE_TYPE:
	mode_msg = (EMC_TASK_SET_MODE *) cmd;
	if (emcStatus->task.mode == EMC_TASK_MODE_AUTO &&
	    emcStatus->task.interpState != EMC_TASK_INTERP_IDLE &&
	    mode_msg->mode != EMC_TASK_MODE_AUTO) {
	    emcOperatorError(0, _("Can't switch mode while mode is AUTO and interpreter is not IDLE"));
	} else { // we can honour the modeswitch
	    if (mode_msg->mode == EMC_TASK_MODE_MANUAL &&
		emcStatus->task.mode != EMC_TASK_MODE_MANUAL) {
		// leaving auto or mdi mode for manual

		/*! \todo FIXME-- duplicate code for abort,
	        also near end of main, when aborting on subordinate errors,
	        and in emcTaskExecute() */

		// abort motion
		emcTaskAbort();
		mdi_execute_abort();

		// without emcTaskPlanClose(), a new run command resumes at
		// aborted line-- feature that may be considered later
		{
		    int was_open = taskplanopen;
		    emcTaskPlanClose();
		    if (emc_debug & EMC_DEBUG_INTERP && was_open) {
			rcs_print("emcTaskPlanClose() called at %s:%d\n",
			      __FILE__, __LINE__);
		    }
		}

		// clear out the pending command
		emcTaskCommand = 0;
		interp_list.clear();
                emcStatus->task.currentLine = 0;

		// clear out the interpreter state
		emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
		emcStatus->task.execState = EMC_TASK_EXEC_DONE;
		stepping = 0;
		steppingWait = 0;

		// now queue up command to resynch interpreter
		emcTaskQueueSynchCmd();
	    }
	    retval = emcTaskSetMode(mode_msg->mode);
	}
	break;

    case EMC_TASK_SET_STATE_TYPE:
	state_msg = (EMC_TASK_SET_STATE *) cmd;
	retval = emcTaskSetState(state_msg->state);
	break;

	// interpreter commands

    case EMC_TASK_PLAN_OPEN_TYPE:
	open_msg = (EMC_TASK_PLAN_OPEN *) cmd;
	retval = emcTaskPlanOpen(open_msg->file);
	if (retval > INTERP_MIN_ERROR) {
	    retval = -1;
	}
	if (-1 == retval) {
	    emcOperatorError(0, _("can't open %s"), open_msg->file);
	} else {
	    strcpy(emcStatus->task.file, open_msg->file);
	    retval = 0;
	}
	break;

    case EMC_TASK_PLAN_EXECUTE_TYPE:
	stepping = 0;
	steppingWait = 0;
	execute_msg = (EMC_TASK_PLAN_EXECUTE *) cmd;
        if (!all_homed() && !no_force_homing) { //!no_force_homing = force homing before MDI
            emcOperatorError(0, _("Can't issue MDI command when not homed"));
            retval = -1;
            break;
        }
        if (emcStatus->task.mode != EMC_TASK_MODE_MDI) {
            emcOperatorError(0, _("Must be in MDI mode to issue MDI command"));
            retval = -1;
            break;
        }
	// track interpState also during MDI - it might be an oword sub call
	emcStatus->task.interpState = EMC_TASK_INTERP_READING;

	if (execute_msg->command[0] != 0) {
	    char * command = execute_msg->command;
	    if (command[0] == (char) 0xff) {
		// Empty command recieved. Consider it is NULL
		command = NULL;
	    } else {
		// record initial MDI command
		strcpy(emcStatus->task.command, execute_msg->command);
	    }

	    int level = emcTaskPlanLevel();
	    if (emcStatus->task.mode == EMC_TASK_MODE_MDI) {
		if (mdi_execute_level < 0)
		    mdi_execute_level = level;
	    }

	    execRetval = emcTaskPlanExecute(command, 0);

	    level = emcTaskPlanLevel();

	    if (emcStatus->task.mode == EMC_TASK_MODE_MDI) {
		if (mdi_execute_level == level) {
		    mdi_execute_level = -1;
		} else if (level > 0) {
		    // Still insude call. Need another execute(0) call
		    // but only if we didnt encounter an error
		    if (execRetval == INTERP_ERROR) {
			mdi_execute_next = 0;
		    } else {
			mdi_execute_next = 1;
		    }
		}
	    }
	    switch (execRetval) {

	    case INTERP_EXECUTE_FINISH:
		// Flag MDI wait
		mdi_execute_wait = 1;
		// need to flush execution, so signify no more reading
		// until all is done
		emcTaskPlanSetWait();
		// and resynch the interpreter WM
		emcTaskQueueSynchCmd();
		// it's success, so retval really is 0
		retval = 0;
		break;

	    case INTERP_ERROR:
		// emcStatus->task.interpState =  EMC_TASK_INTERP_WAITING;
		interp_list.clear();
		// abort everything
		emcTaskAbort();
		emcIoAbort(EMC_ABORT_INTERPRETER_ERROR_MDI);
		emcSpindleAbort();
		mdi_execute_abort(); // sets emcStatus->task.interpState to  EMC_TASK_INTERP_IDLE
		emcAbortCleanup(EMC_ABORT_INTERPRETER_ERROR_MDI, "interpreter error during MDI");
		retval = -1;
		break;

	    case INTERP_EXIT:
	    case INTERP_ENDFILE:
	    case INTERP_FILE_NOT_OPEN:
		// this caused the error msg on M2 in MDI mode - execRetval == INTERP_EXIT which is would be ok (I think). mah
		retval = -1;
		break;

	    default:
		// other codes are OK
		retval = 0;
	    }
	}
	break;

    case EMC_TASK_PLAN_RUN_TYPE:
        if (!all_homed() && !no_force_homing) { //!no_force_homing = force homing before Auto
            emcOperatorError(0, _("Can't run a program when not homed"));
            retval = -1;
            break;
        }
	stepping = 0;
	steppingWait = 0;
	if (!taskplanopen && emcStatus->task.file[0] != 0) {
	    emcTaskPlanOpen(emcStatus->task.file);
	}
	run_msg = (EMC_TASK_PLAN_RUN *) cmd;
	programStartLine = run_msg->line;
	emcStatus->task.interpState = EMC_TASK_INTERP_READING;
	emcStatus->task.task_paused = 0;
	retval = 0;
	break;

    case EMC_TASK_PLAN_PAUSE_TYPE:
	emcTrajPause();
	if (emcStatus->task.interpState != EMC_TASK_INTERP_PAUSED) {
	    set_interpResumeState(emcStatus->task.interpState);
	}
	emcStatus->task.interpState = EMC_TASK_INTERP_PAUSED;
	emcStatus->task.task_paused = 1;
	retval = 0;
	break;

    case EMC_TASK_PLAN_OPTIONAL_STOP_TYPE:
	if (GET_OPTIONAL_PROGRAM_STOP() == ON) {
	    emcTrajPause();
	    if (emcStatus->task.interpState != EMC_TASK_INTERP_PAUSED) {
		set_interpResumeState(emcStatus->task.interpState);
	    }
	    emcStatus->task.interpState = EMC_TASK_INTERP_PAUSED;
	    emcStatus->task.task_paused = 1;
	}
	retval = 0;
	break;

    case EMC_TASK_PLAN_RESUME_TYPE:
	emcTrajResume();
	emcStatus->task.interpState =
	    (enum EMC_TASK_INTERP_ENUM) get_interpResumeState();
	emcStatus->task.task_paused = 0;
	stepping = 0;
	steppingWait = 0;
	retval = 0;
	break;

    case EMC_TASK_PLAN_END_TYPE:
	retval = 0;
	break;

    case EMC_TASK_PLAN_INIT_TYPE:
	retval = emcTaskPlanInit();
	if (retval > INTERP_MIN_ERROR) {
	    retval = -1;
	}
	break;

    case EMC_TASK_PLAN_SYNCH_TYPE:
	retval = emcTaskPlanSynch();
	if (retval > INTERP_MIN_ERROR) {
	    retval = -1;
	}
	break;

    case EMC_TASK_PLAN_SET_OPTIONAL_STOP_TYPE:
	os_msg = (EMC_TASK_PLAN_SET_OPTIONAL_STOP *) cmd;
	emcTaskPlanSetOptionalStop(os_msg->state);
	retval = 0;
	break;

    case EMC_TASK_PLAN_SET_BLOCK_DELETE_TYPE:
	bd_msg = (EMC_TASK_PLAN_SET_BLOCK_DELETE *) cmd;
	emcTaskPlanSetBlockDelete(bd_msg->state);
	retval = 0;
	break;

    case EMC_EXEC_PLUGIN_CALL_TYPE:
	retval =  emcPluginCall( (EMC_EXEC_PLUGIN_CALL *) cmd);
	break;

    case EMC_IO_PLUGIN_CALL_TYPE:
	retval =  emcIoPluginCall( (EMC_IO_PLUGIN_CALL *) cmd);
	break;

     default:
	// unrecognized command
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print_error("ignoring issue of unknown command %d:%s\n",
			    (int)cmd->type, emc_symbol_lookup(cmd->type));
	}
	retval = 0;		// don't consider this an error
	break;
    }

    if (retval == -1) {
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print_error("error executing command %d:%s\n", (int)cmd->type,
			    emc_symbol_lookup(cmd->type));
	}
    }
    /* debug */
    if ((emc_debug & EMC_DEBUG_TASK_ISSUE) && retval) {
	rcs_print("emcTaskIssueCommand() returning: %d\n", retval);
    }
    return retval;
}
