#include "emc_nml.hh"
#include "interpl.hh"		// NML_INTERP_LIST, interp_list
#include "interp_return.hh"	// public interpreter return values
#include "interp_internal.hh"	// interpreter private definitions
#include "rcs_print.hh"
#include "task.hh"		// emcTaskCommand etc
#include "main.hh"
#include "taskparams.hh"
#include "interpqueue.hh"		// emcTaskCommand etc

extern EMC_STAT *emcStatus;

// MDI input queue
static NML_INTERP_LIST mdi_input_queue;
int max_mdi_queued_commands = MAX_MDI_QUEUE;

static int checkInterpList(NML_INTERP_LIST * il, EMC_STAT * stat);

// main.cc
extern int programStartLine;	// which line to run program from
// issue.cc
// Variables to handle MDI call interrupts
// Depth of call level before interrupted MDI call
extern int mdi_execute_level;
// Schedule execute(0) command
extern int mdi_execute_next;
// Wait after interrupted command
extern int mdi_execute_wait;

// Side queue to store MDI commands
NML_INTERP_LIST mdi_execute_queue;

void readahead_reading(void)
{
    int readRetval;
    int execRetval;

		if (interp_list.len() <= emc_task_interp_max_len) {
                    int count = 0;
interpret_again:
		    if (emcTaskPlanIsWait()) {
			// delay reading of next line until all is done
			if (interp_list.len() == 0 &&
			    emcTaskCommand == 0 &&
			    emcStatus->task.execState ==
			    EMC_TASK_EXEC_DONE) {
			    emcTaskPlanClearWait();
			 }
		    } else {
			readRetval = emcTaskPlanRead();
			/*! \todo MGS FIXME
			   This if() actually evaluates to if (readRetval != INTERP_OK)...
			   *** Need to look at all calls to things that return INTERP_xxx values! ***
			   MGS */
			if (readRetval > INTERP_MIN_ERROR
				|| readRetval == INTERP_ENDFILE
				|| readRetval == INTERP_EXIT
				|| readRetval == INTERP_EXECUTE_FINISH) {
			    /* emcTaskPlanRead retval != INTERP_OK
			       Signal to the rest of the system that that the interp
			       is now in a paused state. */
			    /*! \todo FIXME The above test *should* be reduced to:
			       readRetVal != INTERP_OK
			       (N.B. Watch for negative error codes.) */
			    emcStatus->task.interpState =
				EMC_TASK_INTERP_WAITING;
			} else {
			    // got a good line
			    // record the line number and command
			    emcStatus->task.readLine = emcTaskPlanLine();

			    emcTaskPlanCommand((char *) &emcStatus->task.
					       command);
			    // and execute it
			    execRetval = emcTaskPlanExecute(0);
			    if (execRetval > INTERP_MIN_ERROR) {
				emcStatus->task.interpState =
				    EMC_TASK_INTERP_WAITING;
				interp_list.clear();
				emcAbortCleanup(EMC_ABORT_INTERPRETER_ERROR,
						"interpreter error");
			    } else if (execRetval == -1
				    || execRetval == INTERP_EXIT ) {
				emcStatus->task.interpState =
				    EMC_TASK_INTERP_WAITING;
			    } else if (execRetval == INTERP_EXECUTE_FINISH) {
				// INTERP_EXECUTE_FINISH signifies
				// that no more reading should be done until
				// everything
				// outstanding is completed
				emcTaskPlanSetWait();
				// and resynch interp WM
				emcTaskQueueSynchCmd();

			    } else if (execRetval != 0) {
				// end of file
				emcStatus->task.interpState =
				    EMC_TASK_INTERP_WAITING;
                                emcStatus->task.motionLine = 0;
                                emcStatus->task.readLine = 0;
			    } else {

				// executed a good line
			    }

			    // throw the results away if we're supposed to
			    // read
			    // through it
			    if (programStartLine < 0 ||
				emcStatus->task.readLine <
				programStartLine) {
				// we're stepping over lines, so check them
				// for
				// limits, etc. and clear then out
				if (0 != checkInterpList(&interp_list,
							 emcStatus)) {
				    // problem with actions, so do same as we
				    // did
				    // for a bad read from emcTaskPlanRead()
				    // above
				    emcStatus->task.interpState =
					EMC_TASK_INTERP_WAITING;
				}
				// and clear it regardless
				interp_list.clear();
			    }

			    if (emcStatus->task.readLine < programStartLine) {

				//update the position with our current position, as the other positions are only skipped through
				CANON_UPDATE_END_POINT(emcStatus->motion.traj.actualPosition.tran.x,
						       emcStatus->motion.traj.actualPosition.tran.y,
						       emcStatus->motion.traj.actualPosition.tran.z,
						       emcStatus->motion.traj.actualPosition.a,
						       emcStatus->motion.traj.actualPosition.b,
						       emcStatus->motion.traj.actualPosition.c,
						       emcStatus->motion.traj.actualPosition.u,
						       emcStatus->motion.traj.actualPosition.v,
						       emcStatus->motion.traj.actualPosition.w);

				if ((emcStatus->task.readLine + 1 == programStartLine)  &&
				    (emcTaskPlanLevel() == 0))  {

				    emcTaskPlanSynch();

                                    // reset programStartLine so we don't fall into our stepping routines
                                    // if we happen to execute lines before the current point later (due to subroutines).
                                    programStartLine = 0;
                                }
			    }

                            if (count++ < emc_task_interp_max_len
                                    && emcStatus->task.interpState == EMC_TASK_INTERP_READING
                                    && interp_list.len() <= emc_task_interp_max_len * 2/3) {
                                goto interpret_again;
                            }

			}	// else read was OK, so execute
		    }		// else not emcTaskPlanIsWait
		}		// if interp len is less than max
}

void mdi_execute_abort(void)
{
    // XXX: Reset needed?
    if (mdi_execute_wait || mdi_execute_next)
        emcTaskPlanReset();
    mdi_execute_level = -1;
    mdi_execute_wait = 0;
    mdi_execute_next = 0;

    mdi_execute_queue.clear();
    emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
}

void mdi_execute_hook(void)
{
    if (mdi_execute_wait && emcTaskPlanIsWait()) {
	// delay reading of next line until all is done
	if (interp_list.len() == 0 &&
	    emcTaskCommand == 0 &&
	    emcStatus->task.execState ==
	    EMC_TASK_EXEC_DONE) {
	    emcTaskPlanClearWait();
	    mdi_execute_wait = 0;
	    mdi_execute_hook();
	}
	return;
    }

    if (
        (mdi_execute_level < 0)
        && (mdi_execute_wait == 0)
        && (mdi_execute_queue.len() > 0)
        && (interp_list.len() == 0)
        && (emcTaskCommand == NULL)
    ) {
	interp_list.append(mdi_execute_queue.get());
	return;
    }

    // determine when a MDI command actually finishes normally.
    if (interp_list.len() == 0 &&
	emcTaskCommand == 0 &&
	emcStatus->task.execState ==  EMC_TASK_EXEC_DONE &&
	emcStatus->task.interpState != EMC_TASK_INTERP_IDLE &&
	emcStatus->motion.traj.queue == 0 &&
	emcStatus->io.status == RCS_DONE &&
	!mdi_execute_wait &&
	!mdi_execute_next) {

	// finished. Check for dequeuing of queued MDI command is done in emcTaskPlan().
	if (emc_debug & EMC_DEBUG_TASK_ISSUE)
	    rcs_print("mdi_execute_hook: MDI command '%s' done (remaining: %d)\n",
		      emcStatus->task.command, mdi_input_queue.len());
	emcStatus->task.command[0] = 0;
	emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
    }

    if (!mdi_execute_next) return;

    if (interp_list.len() > emc_task_interp_max_len) return;

    mdi_execute_next = 0;

    EMC_TASK_PLAN_EXECUTE msg;
    msg.command[0] = (char) 0xff;

    interp_list.append(msg);
}

void readahead_waiting(void)
{
	// now handle call logic
	// check for subsystems done
	if (interp_list.len() == 0 &&
	    emcTaskCommand == 0 &&
	    emcStatus->motion.traj.queue == 0 &&
	    emcStatus->io.status == RCS_DONE)
	    // finished
	{
	    int was_open = taskplanopen;
	    if (was_open) {
		emcTaskPlanClose();
		if (emc_debug & EMC_DEBUG_INTERP && was_open) {
		    rcs_print
			("emcTaskPlanClose() called at %s:%d\n",
			 __FILE__, __LINE__);
		}
		// then resynch interpreter
		emcTaskQueueSynchCmd();
	    } else {
		emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
	    }
	    emcStatus->task.readLine = 0;
	} else {
	    // still executing
        }
}

/*
  checkInterpList(NML_INTERP_LIST *il, EMC_STAT *stat) takes a pointer
  to an interpreter list and a pointer to the EMC status, pops each NML
  message off the list, and checks it against limits, resource availability,
  etc. in the status.

  It returns 0 if all messages check out, -1 if any of them fail. If one
  fails, the rest of the list is not checked.
 */
static int checkInterpList(NML_INTERP_LIST * il, EMC_STAT * stat)
{
    NMLmsg *cmd = 0;
    // let's create some shortcuts to casts at compile time
#define operator_error_msg ((EMC_OPERATOR_ERROR *) cmd)
#define linear_move ((EMC_TRAJ_LINEAR_MOVE *) cmd)
#define circular_move ((EMC_TRAJ_CIRCULAR_MOVE *) cmd)

    while (il->len() > 0) {
	cmd = il->get();

	switch (cmd->type) {

	case EMC_OPERATOR_ERROR_TYPE:
	    emcOperatorError(operator_error_msg->id, "%s",
			     operator_error_msg->error);
	    break;

	case EMC_TRAJ_LINEAR_MOVE_TYPE:
	    if (linear_move->end.tran.x >
		stat->motion.axis[0].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +X limit"), stat->task.command);
		return -1;
	    }
	    if (linear_move->end.tran.y >
		stat->motion.axis[1].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +Y limit"), stat->task.command);
		return -1;
	    }
	    if (linear_move->end.tran.z >
		stat->motion.axis[2].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +Z limit"), stat->task.command);
		return -1;
	    }
	    if (linear_move->end.tran.x <
		stat->motion.axis[0].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -X limit"), stat->task.command);
		return -1;
	    }
	    if (linear_move->end.tran.y <
		stat->motion.axis[1].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -Y limit"), stat->task.command);
		return -1;
	    }
	    if (linear_move->end.tran.z <
		stat->motion.axis[2].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -Z limit"), stat->task.command);
		return -1;
	    }
	    break;

	case EMC_TRAJ_CIRCULAR_MOVE_TYPE:
	    if (circular_move->end.tran.x >
		stat->motion.axis[0].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +X limit"), stat->task.command);
		return -1;
	    }
	    if (circular_move->end.tran.y >
		stat->motion.axis[1].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +Y limit"), stat->task.command);
		return -1;
	    }
	    if (circular_move->end.tran.z >
		stat->motion.axis[2].maxPositionLimit) {
		emcOperatorError(0, _("%s exceeds +Z limit"), stat->task.command);
		return -1;
	    }
	    if (circular_move->end.tran.x <
		stat->motion.axis[0].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -X limit"), stat->task.command);
		return -1;
	    }
	    if (circular_move->end.tran.y <
		stat->motion.axis[1].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -Y limit"), stat->task.command);
		return -1;
	    }
	    if (circular_move->end.tran.z <
		stat->motion.axis[2].minPositionLimit) {
		emcOperatorError(0, _("%s exceeds -Z limit"), stat->task.command);
		return -1;
	    }
	    break;

	default:
	    break;
	}
    }

    return 0;

    // get rid of the compile-time cast shortcuts
#undef circular_move_msg
#undef linear_move_msg
#undef operator_error_msg
}
