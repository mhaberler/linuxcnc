#include <signal.h>		// signal(), SIGINT
#include <sys/wait.h>		// waitpid(), WNOHANG, WIFEXITED

#include "emc_nml.hh"
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "emcglb.h"		// EMC_INIFILE,NMLFILE, EMC_TASK_CYCLE_TIME
#include "rcs_print.hh"
#include "timer.hh"
#include "task.hh"		// emcTaskCommand etc
#include "motion.h"             // EMCMOT_ORIENT_*

static EMC_TASK_PLAN_SYNCH taskPlanSynchCmd;
static int steppedLine = 0;

// from emctaskmain.cc:
extern int emcAuxInputWaitType;
extern int emcAuxInputWaitIndex;
// delay counter
extern double taskExecDelayTimeout;

/*
  STEPPING_CHECK() is a macro that prefaces a switch-case with a check
  for stepping. If stepping is active, it waits until the step has been
  given, then falls through to the rest of the case statement.
*/

#define STEPPING_CHECK()                                                   \
if (stepping) {                                                            \
  if (! steppingWait) {                                                    \
    steppingWait = 1;                                                      \
    steppedLine = emcStatus->task.currentLine;                             \
  }                                                                        \
  else {                                                                   \
    if (emcStatus->task.currentLine != steppedLine) {                      \
      break;                                                               \
    }                                                                      \
  }                                                                        \
}

// executor function
int emcTaskExecute(EMC_STAT *emcStatus)
{
    int retval = 0;
    int status;			// status of child from EMC_SYSTEM_CMD
    pid_t pid;			// pid returned from waitpid()

    // first check for an abandoned system command and abort it
    if (get_SystemCmdPid() != 0 &&
	emcStatus->task.execState !=
	EMC_TASK_EXEC_WAITING_FOR_SYSTEM_CMD) {
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print("emcSystemCmd: abandoning process %d\n",
		      get_SystemCmdPid());
	}
	kill(get_SystemCmdPid(), SIGINT);
	set_SystemCmdPid(0);
    }

    switch (emcStatus->task.execState) {
    case EMC_TASK_EXEC_ERROR:

	/*! \todo FIXME-- duplicate code for abort,
	   also near end of main, when aborting on subordinate errors,
	   and in emcTaskIssueCommand() */

	// abort everything
	emcTaskAbort();
        emcIoAbort(EMC_ABORT_TASK_EXEC_ERROR);
        emcSpindleAbort();
	mdi_execute_abort();

	// without emcTaskPlanClose(), a new run command resumes at
	// aborted line-- feature that may be considered later
	{
	    int was_open = taskplanopen;
	    emcTaskPlanClose();
	    if (emc_debug & EMC_DEBUG_INTERP && was_open) {
		rcs_print("emcTaskPlanClose() called at %s:%d\n", __FILE__,
			  __LINE__);
	    }
	}

	// clear out pending command
	emcTaskCommand = 0;
	interp_list.clear();
	emcAbortCleanup(EMC_ABORT_TASK_EXEC_ERROR);
        emcStatus->task.currentLine = 0;

	// clear out the interpreter state
	emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
	emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	stepping = 0;
	steppingWait = 0;

	// now queue up command to resynch interpreter
	emcTaskQueueCommand(&taskPlanSynchCmd);

	retval = -1;
	break;

    case EMC_TASK_EXEC_DONE:
	STEPPING_CHECK();
	if (!emcStatus->motion.traj.queueFull &&
	    emcStatus->task.interpState != EMC_TASK_INTERP_PAUSED) {
	    if (0 == emcTaskCommand) {
		// need a new command
		emcTaskCommand = interp_list.get();
		// interp_list now has line number associated with this-- get
		// it
		if (0 != emcTaskCommand) {
		    set_eager(true);
		    emcStatus->task.currentLine =
			interp_list.get_line_number();
		    // and set it for all subsystems which use queued ids
		    emcTrajSetMotionId(emcStatus->task.currentLine);
		    if (emcStatus->motion.traj.queueFull) {
			emcStatus->task.execState =
			    EMC_TASK_EXEC_WAITING_FOR_MOTION_QUEUE;
		    } else {
			emcStatus->task.execState =
			    (enum EMC_TASK_EXEC_ENUM)
			    emcTaskCheckPreconditions(emcTaskCommand);
		    }
		}
	    } else {
		// have an outstanding command
		if (0 != emcTaskIssueCommand(emcTaskCommand)) {
		    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
		    retval = -1;
		} else {
		    emcStatus->task.execState = (enum EMC_TASK_EXEC_ENUM)
			emcTaskCheckPostconditions(emcTaskCommand);
		    set_eager(true);
		}
		emcTaskCommand = 0;	// reset it
	    }
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_MOTION_QUEUE:
	STEPPING_CHECK();
	if (!emcStatus->motion.traj.queueFull) {
	    if (0 != emcTaskCommand) {
		emcStatus->task.execState = (enum EMC_TASK_EXEC_ENUM)
		    emcTaskCheckPreconditions(emcTaskCommand);
		set_eager(true);
	    } else {
		emcStatus->task.execState = EMC_TASK_EXEC_DONE;
		set_eager(true);
	    }
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_MOTION:
	STEPPING_CHECK();
	if (emcStatus->motion.status == RCS_ERROR) {
	    // emcOperatorError(0, "error in motion controller");
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	} else if (emcStatus->motion.status == RCS_DONE) {
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    set_eager(true);
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_IO:
	STEPPING_CHECK();
	if (emcStatus->io.status == RCS_ERROR) {
	    // emcOperatorError(0, "error in IO controller");
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	} else if (emcStatus->io.status == RCS_DONE) {
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    set_eager(true);
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_MOTION_AND_IO:
	STEPPING_CHECK();
	if (emcStatus->motion.status == RCS_ERROR) {
	    // emcOperatorError(0, "error in motion controller");
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	} else if (emcStatus->io.status == RCS_ERROR) {
	    // emcOperatorError(0, "error in IO controller");
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	} else if (emcStatus->motion.status == RCS_DONE &&
		   emcStatus->io.status == RCS_DONE) {
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    set_eager(true);
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_SPINDLE_ORIENTED:
	STEPPING_CHECK(); // not sure
	switch (emcStatus->motion.spindle.orient_state) {
	case EMCMOT_ORIENT_NONE:
	case EMCMOT_ORIENT_COMPLETE:
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    emcStatus->task.delayLeft = 0;
	    set_eager(true);
	    rcs_print("wait for orient complete: nothing to do\n");
	    break;

	case EMCMOT_ORIENT_IN_PROGRESS:
	    emcStatus->task.delayLeft = taskExecDelayTimeout - etime();
	    if (etime() >= taskExecDelayTimeout) {
		emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
		emcStatus->task.delayLeft = 0;
		set_eager(true);
		emcOperatorError(0, "wait for orient complete: TIMED OUT");
	    }
	    break;

	case EMCMOT_ORIENT_FAULTED:
	    // actually the code in main() should trap this before we get here
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	    emcStatus->task.delayLeft = 0;
	    set_eager(true);
	    emcOperatorError(0, "wait for orient complete: FAULTED code=%d",
			     emcStatus->motion.spindle.orient_fault);
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_DELAY:
	STEPPING_CHECK();
	// check if delay has passed
	emcStatus->task.delayLeft = taskExecDelayTimeout - etime();
	if (etime() >= taskExecDelayTimeout) {
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    emcStatus->task.delayLeft = 0;
	    if (emcStatus->task.input_timeout != 0)
		emcStatus->task.input_timeout = 1; // timeout occured
	    set_eager(true);
	}
	// delay can be also be because we wait for an input
	// if the index is set (not -1)
	if (emcAuxInputWaitIndex >= 0) {
	    switch (emcAuxInputWaitType) {
		case WAIT_MODE_HIGH:
		    if (emcStatus->motion.synch_di[emcAuxInputWaitIndex] != 0) {
			emcStatus->task.input_timeout = 0; // clear timeout flag
			emcAuxInputWaitIndex = -1;
			emcStatus->task.execState = EMC_TASK_EXEC_DONE;
			emcStatus->task.delayLeft = 0;
		    }
		    break;

		case WAIT_MODE_RISE:
		    if (emcStatus->motion.synch_di[emcAuxInputWaitIndex] == 0) {
			emcAuxInputWaitType = WAIT_MODE_HIGH;
		    }
		    break;

		case WAIT_MODE_LOW:
		    if (emcStatus->motion.synch_di[emcAuxInputWaitIndex] == 0) {
			emcStatus->task.input_timeout = 0; // clear timeout flag
			emcAuxInputWaitIndex = -1;
			emcStatus->task.execState = EMC_TASK_EXEC_DONE;
			emcStatus->task.delayLeft = 0;
		    }
		    break;

		case WAIT_MODE_FALL: //FIXME: implement different fall mode if needed
		    if (emcStatus->motion.synch_di[emcAuxInputWaitIndex] != 0) {
			emcAuxInputWaitType = WAIT_MODE_LOW;
		    }
		    break;

		case WAIT_MODE_IMMEDIATE:
		    emcStatus->task.input_timeout = 0; // clear timeout flag
		    emcAuxInputWaitIndex = -1;
		    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
		    emcStatus->task.delayLeft = 0;
		    break;

		default:
		    emcOperatorError(0, "Unknown Wait Mode");
	    }
	}
	break;

    case EMC_TASK_EXEC_WAITING_FOR_SYSTEM_CMD:
	STEPPING_CHECK();

	// if we got here without a system command pending, say we're done
	if (0 == get_SystemCmdPid()) {
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    break;
	}
	// check the status of the system command
	pid = waitpid(get_SystemCmdPid(), &status, WNOHANG);

	if (0 == pid) {
	    // child is still executing
	    break;
	}

	if (-1 == pid) {
	    // execution error
	    if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
		rcs_print("emcSystemCmd: error waiting for %d\n",
			   get_SystemCmdPid());
	    }
	    set_SystemCmdPid(0);
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	    break;
	}

	if ( get_SystemCmdPid() != pid) {
	    // somehow some other child finished, which is a coding error
	    if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
		rcs_print
		    ("emcSystemCmd: error waiting for system command %d, we got %d\n",
		      get_SystemCmdPid(), pid);
	    }
	    set_SystemCmdPid(0);
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	    break;
	}
	// else child has finished
	if (WIFEXITED(status)) {
	    if (0 == WEXITSTATUS(status)) {
		// child exited normally
		set_SystemCmdPid(0);
		emcStatus->task.execState = EMC_TASK_EXEC_DONE;
		set_eager(true);
	    } else {
		// child exited with non-zero status
		if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
		    rcs_print
			("emcSystemCmd: system command %d exited abnormally with value %d\n",
			  get_SystemCmdPid(), WEXITSTATUS(status));
		}
		set_SystemCmdPid(0);
		emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	    }
	} else if (WIFSIGNALED(status)) {
	    // child exited with an uncaught signal
	    if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
		rcs_print("system command %d terminated with signal %d\n",
			   get_SystemCmdPid(), WTERMSIG(status));
	    }
	    set_SystemCmdPid(0);
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	} else if (WIFSTOPPED(status)) {
	    // child is currently being traced, so keep waiting
	} else {
	    // some other status, we'll call this an error
	    set_SystemCmdPid(0);
	    emcStatus->task.execState = EMC_TASK_EXEC_ERROR;
	}
	break;

    default:
	// coding error
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print_error("invalid execState");
	}
	retval = -1;
	break;
    }
    return retval;
}
