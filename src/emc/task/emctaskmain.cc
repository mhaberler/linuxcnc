/********************************************************************
* Description: emctaskmain.cc
*   Main program for EMC task level
*
*   Derived from a work by Fred Proctor & Will Shackleford
*
* Author:
* License: GPL Version 2
* System: Linux
*    
* Copyright (c) 2004 All rights reserved.
*
********************************************************************/
/*
  Principles of operation:

  1.  The main program calls emcTaskPlan() and emcTaskExecute() cyclically.

  2.  emcTaskPlan() reads the new command, and decides what to do with
  it based on the mode (manual, auto, mdi) or state (estop, on) of the
  machine. Many of the commands just go out immediately to the
  subsystems (motion and IO). In auto mode, the interpreter is called
  and as a result the interp_list is appended with NML commands.

  3.  emcTaskExecute() executes a big switch on execState. If it's done,
  it gets the next item off the interp_list, and sets execState to the
  preconditions for that. These preconditions include waiting for motion,
  waiting for IO, etc. Once they are satisfied, it issues the command, and
  sets execState to the postconditions. Once those are satisfied, it gets
  the next item off the interp_list, and so on.

  4.  preconditions and postconditions are only looked at in conjunction
  with commands on the interp_list. Immediate commands won't have any
  pre- or postconditions associated with them looked at.

  5.  At this point, nothing in this file adds anything to the interp_list.
  This could change, for example, when defining pre- and postconditions for
  jog or home commands. If this is done, make sure that the corresponding
  abort command clears out the interp_list.

  6. Single-stepping is handled in checkPreconditions() as the first
  condition. If we're in single-stepping mode, as indicated by the
  variable 'stepping', we set the state to waiting-for-step. This
  polls on the variable 'steppingWait' which is reset to zero when a
  step command is received, and set to one when the command is
  issued.
  */

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


#if 0
// Enable this to niftily trap floating point exceptions for debugging
#include <fpu_control.h>
fpu_control_t __fpu_control = _FPU_IEEE & ~(_FPU_MASK_IM | _FPU_MASK_ZM | _FPU_MASK_OM);
#endif


#include <google/protobuf/text_format.h>
#include <google/protobuf/message_lite.h>

#include <protobuf/generated/types.pb.h>
#include <protobuf/generated/task.pb.h>
#include <protobuf/generated/message.pb.h>

using namespace google::protobuf;

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
#include "iniload.hh"
#include "nmlsetup.hh"
#include "inihal.hh"

#include "czmq.h"

/* time after which the user interface is declared dead
 * because it would'nt read any more messages
 */
#define DEFAULT_EMC_UI_TIMEOUT 5.0


// command line args-- global so that other modules can access 
int Argc;
char **Argv;

// NML channels
// see nmlsetup.hh
// RCS_CMD_CHANNEL *emcCommandBuffer = 0;
// RCS_STAT_CHANNEL *emcStatusBuffer = 0;
// NML *emcErrorBuffer = 0;

// NML command channel data pointer
//static RCS_CMD_MSG *emcCommand = 0;

// global EMC status
EMC_STAT *emcStatus = 0;

// timer stuff
RCS_TIMER *timer = 0;

// flag signifying that ini file [TASK] CYCLE_TIME is <= 0.0, so
// we should not delay at all between cycles. This means also that
// the EMC_TASK_CYCLE_TIME global will be set to the measured cycle
// time each cycle, in case other code references this.
int emcTaskNoDelay = 0;
// flag signifying that on the next loop, there should be no delay.
// this is set when transferring trajectory data from userspace to kernel
// space, annd reset otherwise.
// static int emcTaskEager = 0;

int no_force_homing = 0; // forces the user to home first before allowing MDI and Program run
//can be overriden by [TRAJ]NO_FORCE_HOMING=1

double EMC_TASK_CYCLE_TIME_ORIG = 0.0;


// emcTaskIssueCommand issues command immediately
// moved to task.h static int emcTaskIssueCommand(NMLmsg * cmd);

// pending command to be sent out by emcTaskExecute()
NMLmsg *emcTaskCommand = 0;

// signal handling code to stop main loop
int done;
//static int emctask_shutdown(void);
extern void backtrace(int signo);
int _task = 1; // control preview behaviour when remapping

// for operator display on iocontrol signalling a toolchanger fault if io.fault is set
// %d receives io.reason
const char *io_error = "toolchanger error %d";

extern void setup_signal_handlers(); // backtrace, gdb-in-new-window supportx

zctx_t *zmq_ctx;
void  *cmd, *completion;
static const char *cmdsocket = "tcp://127.0.0.1:5556";
static const char *cmdid = "task";

// each request gets assigned a ticket number in the reply
// the ticket number is passed to the client in the submit reply
// and used for completion updates
static int ticket = 1;

// the command originator ID
// initial commands are self-generated, so tag as 'task'
string origin = "task";

// track emcStatus->status for zmq-injected commands
static int prev_status = UNINITIALIZED_STATUS;

// current command symbol
const char *current_cmd = "";

static const char *completion_socket = "tcp://127.0.0.1:5557";

static int zdebug;

static int setup_zmq(void)
{
    int major, minor, patch, rc;

    if (zmq_ctx)
	return 0;
    zdebug = (getenv("ZDEBUG") != NULL);

    zmq_version (&major, &minor, &patch);
    fprintf(stderr, "Current 0MQ version is %d.%d.%d\n",major, minor, patch);

    zmq_ctx = zctx_new ();
    cmd = zsocket_new (zmq_ctx, ZMQ_ROUTER);

    // shutdown socket immediately if unsent messages
    zsocket_set_linger (cmd, 0);
    zsocket_set_identity (cmd, (char *) cmdid);

    rc = zsocket_bind(cmd, cmdsocket);
    assert (rc != 0);

    completion = zsocket_new (zmq_ctx, ZMQ_PUB);
    rc = zsocket_bind(completion, completion_socket);
    assert (rc != 0);
    return 0;
}

int shutdown_zmq(void)
{
    zctx_destroy (&zmq_ctx);
    return 0;
}

int publish_ticket_update(int state, int ticket, string origin, string text = "")
{
    pb::Container update;
    update.set_type(pb::MT_TICKET_UPDATE);

    pb::TicketUpdate *tu = update.mutable_ticket_update();
    tu->set_cticket(ticket);
    tu->set_status((pb::RCS_STATUS)state);
    if (text.size()) {
	tu->set_text(text);
    }
    size_t pb_update_size = update.ByteSize();
    zframe_t *pb_update_frame = zframe_new (NULL, pb_update_size);
    assert(update.SerializeToArray(zframe_data (pb_update_frame),
				   zframe_size (pb_update_frame)));
    zmsg_t *msg = zmsg_new ();
    zmsg_pushstr (msg, origin.c_str());
    zmsg_add (msg, pb_update_frame);
    zmsg_send (&msg, completion);
    return 0;
}

// static int all_homed(void) {
//     for(int i=0; i<9; i++) {
//         unsigned int mask = 1<<i;
//         if((emcStatus->motion.traj.axis_mask & mask) && !emcStatus->motion.axis[i].homed)
//             return 0;
//     }
//     return 1;
// }

void emctask_quit(int sig)
{
    // set main's done flag
    done = 1;
    // restore signal handler
    signal(sig, emctask_quit);
}

/* make sure at least space bytes are available on
 * error channel; wait a bit to drain if needed
 */
int emcErrorBufferOKtoWrite(int space, const char *caller)
{
    // check channel for validity
    if (emcErrorBuffer == NULL)
	return -1;
    if (!emcErrorBuffer->valid())
	return -1;

    double send_errorchan_timout = etime() + DEFAULT_EMC_UI_TIMEOUT;

    while (etime() < send_errorchan_timout) {
	if (emcErrorBuffer->get_space_available() < space) {
	    esleep(0.01);
	    continue;
	} else {
	    break;
	}
    }
    if (etime() >= send_errorchan_timout) {
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print("timeout waiting for error channel to drain, caller=`%s' request=%d\n", caller,space);
	}
	return -1;
    } else {
	// printf("--- %d bytes available after %f seconds\n", space, etime() - send_errorchan_timout + DEFAULT_EMC_UI_TIMEOUT);
    }
    return 0;
}


// implementation of EMC error logger
int emcOperatorError(int id, const char *fmt, ...)
{
    EMC_OPERATOR_ERROR error_msg;
    va_list ap;

    if ( emcErrorBufferOKtoWrite(sizeof(error_msg) * 2, "emcOperatorError"))
	return -1;

    if (NULL == fmt) {
	return -1;
    }
    if (0 == *fmt) {
	return -1;
    }
    // prepend error code, leave off 0 ad-hoc code
    error_msg.error[0] = 0;
    if (0 != id) {
	snprintf(error_msg.error, sizeof(error_msg.error), "[%d] ", id);
    }
    // append error string
    va_start(ap, fmt);
    vsnprintf(&error_msg.error[strlen(error_msg.error)], 
	      sizeof(error_msg.error) - strlen(error_msg.error), fmt, ap);
    va_end(ap);

    // force a NULL at the end for safety
    error_msg.error[LINELEN - 1] = 0;

    // write it
    rcs_print("%s\n", error_msg.error);
    return emcErrorBuffer->write(error_msg);
}

int emcOperatorText(int id, const char *fmt, ...)
{
    EMC_OPERATOR_TEXT text_msg;
    va_list ap;

    if ( emcErrorBufferOKtoWrite(sizeof(text_msg) * 2, "emcOperatorText"))
	return -1;

    // write args to NML message (ignore int text code)
    va_start(ap, fmt);
    vsnprintf(text_msg.text, sizeof(text_msg.text), fmt, ap);
    va_end(ap);

    // force a NULL at the end for safety
    text_msg.text[LINELEN - 1] = 0;

    // write it
    return emcErrorBuffer->write(text_msg);
}

int emcOperatorDisplay(int id, const char *fmt, ...)
{
    EMC_OPERATOR_DISPLAY display_msg;
    va_list ap;

    if ( emcErrorBufferOKtoWrite(sizeof(display_msg) * 2, "emcOperatorDisplay"))
	return -1;

    // write args to NML message (ignore int display code)
    va_start(ap, fmt);
    vsnprintf(display_msg.display, sizeof(display_msg.display), fmt, ap);
    va_end(ap);

    // force a NULL at the end for safety
    display_msg.display[LINELEN - 1] = 0;

    // write it
    return emcErrorBuffer->write(display_msg);
}


// made visible for execute.cc
int emcAuxInputWaitType = 0;
int emcAuxInputWaitIndex = -1;
// delay counter
double taskExecDelayTimeout = 0.0;

// commands we compose here
static EMC_TASK_PLAN_RUN taskPlanRunCmd;	// 16-Aug-1999 FMP
static EMC_TASK_PLAN_INIT taskPlanInitCmd;
static EMC_TASK_PLAN_SYNCH taskPlanSynchCmd;

//static int interpResumeState = EMC_TASK_INTERP_IDLE;
int programStartLine = 0;	// which line to run program from
// how long the interp list can be

int stepping = 0;
int steppingWait = 0;
//static int steppedLine = 0;

// Variables to handle MDI call interrupts
// Depth of call level before interrupted MDI call
extern int mdi_execute_level;
// Schedule execute(0) command
extern int mdi_execute_next;
// Wait after interrupted command
extern int mdi_execute_wait;

// Side queue to store MDI commands
NML_INTERP_LIST mdi_execute_queue;

// MDI input queue
static NML_INTERP_LIST mdi_input_queue;
#define  MAX_MDI_QUEUE 10
int max_mdi_queued_commands = MAX_MDI_QUEUE;

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
extern int emcTaskMopup();

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
				emcTaskQueueCommand(&taskPlanSynchCmd);
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
		emcTaskQueueCommand(&taskPlanSynchCmd);
	    } else {
		emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
	    }
	    emcStatus->task.readLine = 0;
	} else {
	    // still executing
        }
}

// puts command on interp list
int emcTaskQueueCommand(NMLmsg * cmd)
{
    if (0 == cmd) {
	return 0;
    }

    interp_list.append(cmd);

    return 0;
}


/*
  syntax: a.out {-d -ini <inifile>} {-nml <nmlfile>} {-shm <key>}
*/
int main(int argc, char *argv[])
{
    int taskPlanError = 0;
    int taskExecuteError = 0;
    double startTime, endTime, deltaTime;
    double minTime, maxTime;

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    bindtextdomain("linuxcnc", EMC2_PO_DIR);
    setlocale(LC_MESSAGES,"");
    setlocale(LC_CTYPE,"");
    textdomain("linuxcnc");

    // copy command line args
    Argc = argc;
    Argv = argv;

    // loop until done
    done = 0;
    // trap ^C
    signal(SIGINT, emctask_quit);
    // and SIGTERM (used by runscript to shut down)
    signal(SIGTERM, emctask_quit);

    // create a backtrace on stderr
    signal(SIGSEGV, backtrace);
    signal(SIGFPE, backtrace);
    signal(SIGUSR1, backtrace);

    // set print destination to stdout, for console apps
    set_rcs_print_destination(RCS_PRINT_TO_STDOUT);
    // process command line args
    if (0 != emcGetArgs(argc, argv)) {
	rcs_print_error("error in argument list\n");
	exit(1);
    }

    if (done) {
	emctask_shutdown();
	exit(1);
    }

    if (setup_zmq()) {
	emctask_shutdown();
	exit(1);
    }

    // initialize globals
    emcInitGlobals();

    if (done) {
	emctask_shutdown();
	exit(1);
    }
    // get configuration information
    iniLoad(emc_inifile);

    if (done) {
	emctask_shutdown();
	exit(1);
    }



    // get our status data structure
    // moved up from emc_startup so we can expose it in Python right away
    emcStatus = new EMC_STAT;
    emcStatus->ticket = ticket;

    // get the Python plugin going

    // inistantiate task methods object, too
    emcTaskOnce(emc_inifile);
    if (task_methods == NULL) {
	set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// restore diag
	rcs_print_error("can't initialize Task methods\n");
	emctask_shutdown();
	exit(1);
    }

    // this is the place to run any post-HAL-creation halcmd files
    emcRunHalFiles(emc_inifile);

    // initialize everything
    if (0 != emctask_startup()) {
	emctask_shutdown();
	exit(1);
    }
    // set the default startup modes
    emcTaskSetState(EMC_TASK_STATE_ESTOP);
    emcTaskSetMode(EMC_TASK_MODE_MANUAL);

    // cause the interpreter's starting offset to be reflected
    emcTaskPlanInit();
    // reflect the initial value of EMC_DEBUG in emcStatus->debug
    emcStatus->debug = emc_debug;

    startTime = etime();	// set start time before entering loop;
    // it will be set at end of loop from now on
    minTime = DBL_MAX;		// set to value that can never be exceeded
    maxTime = 0.0;		// set to value that can never be underset

    pb::Container request;
    string wrapped_nml;

    // get our command data structure
    RCS_CMD_MSG *emcCommand  = emcCommandBuffer->get_address();

    while (!done) {
 check_ini_hal_items();
	if (zsocket_poll (cmd, emcTaskEager||emcTaskNoDelay ? 0: 10)) {
            zmsg_t *msg = zmsg_recv (cmd);
            if (!msg)
                continue;          //  Interrupted
            if (zdebug) {
                zclock_log ("task: received message:");
                zmsg_dump (msg);
            }
	    // first frame: originating socket identity
	    char *o = zmsg_popstr (msg);
	    origin = string(o);
	    free(o);

	    // second frame: protobof-encoded NML message
	    // see protobuf/proto/message.proto Container/legacy_nml
            zframe_t *pb_req  = zmsg_pop (msg);

	    if (request.ParseFromArray(zframe_data(pb_req),zframe_size(pb_req))) {
		switch (request.type()) {
		case pb::MT_LEGACY_NML:
		    // tunneled case, a wrapped NML message
		    assert (request.has_legacy_nml());
		    wrapped_nml = request.legacy_nml();
		    emcCommand = (RCS_CMD_MSG *) wrapped_nml.c_str();
		    current_cmd = emcSymbolLookup(emcCommand->type);
		    break;

		case pb:: MT_EMC_TASK_ABORT:
		    // non-tunneled case, a native protobuf replacement for NML
		    fprintf(stderr, "--- NML-free zone not reached yet\n");
		    continue;
		    break;

		default:
		    abort();
		}
		ticket += 1;
		emcStatus->ticket = ticket;

		// kick taskplan into action
		emcCommand->serial_number = ticket;

		// mark as executing a new command to assure a transition
		prev_status = RCS_RECEIVED;

		pb::Container answer;
		pb::TaskReply *task_reply;

		answer.set_type(pb::MT_TASK_REPLY);
		task_reply = answer.mutable_task_reply();
		task_reply->set_ticket(ticket);

		zmsg_t *reply = zmsg_new ();
		zmsg_pushstr(reply, origin.c_str());

		size_t pb_reply_size = answer.ByteSize();
		zframe_t *pb_reply_frame = zframe_new (NULL, pb_reply_size);
		assert(answer.SerializeToArray(zframe_data (pb_reply_frame),
					       zframe_size (pb_reply_frame)));
		zmsg_add (reply, pb_reply_frame);
		if (zdebug) {
		    fprintf(stderr, "task: received %s ticket %d\n",
			    emcSymbolLookup(emcCommand->type),
			    emcStatus->ticket);
		    zclock_log ("task: reply message:");
		    zmsg_dump (reply);
		}
		assert(zmsg_send (&reply, cmd) == 0);
		assert (reply == NULL);
	    }
	} else {

	    // nothing there, normal NML transport
	    emcCommand = emcCommandBuffer->get_address();
	    // read command
	    if (0 != emcCommandBuffer->peek()) {
		current_cmd = emcSymbolLookup(emcCommand->type);
		// got a new command, so clear out errors
		taskPlanError = 0;
		taskExecuteError = 0;
		ticket += 1;
		emcStatus->ticket = ticket;
		origin = "legacynml";

		// mark as executing a new command to assure a transition
		prev_status = RCS_RECEIVED;
	    }
	}
	// run control cycle
	if (0 != emcTaskPlan(emcCommand, emcStatus)) {
	    taskPlanError = 1;
	}
	if (0 != emcTaskExecute(emcStatus)) {
	    taskExecuteError = 1;
	}
	// update subordinate status

	emcIoUpdate(&emcStatus->io);
	emcMotionUpdate(&emcStatus->motion);
	// synchronize subordinate states
	if (emcStatus->io.aux.estop) {
	    if (emcStatus->motion.traj.enabled) {
		emcTrajDisable();
		emcTaskAbort();
		emcIoAbort(EMC_ABORT_AUX_ESTOP);
		emcSpindleAbort();
		emcAxisUnhome(-2); // only those joints which are volatile_home
		mdi_execute_abort();
		emcAbortCleanup(EMC_ABORT_AUX_ESTOP);
		emcTaskPlanSynch();
	    }
	    if (emcStatus->io.coolant.mist) {
		emcCoolantMistOff();
	    }
	    if (emcStatus->io.coolant.flood) {
		emcCoolantFloodOff();
	    }
	    if (emcStatus->io.lube.on) {
		emcLubeOff();
	    }
	    if (emcStatus->motion.spindle.enabled) {
		emcSpindleOff();
	    }
	}

	// toolchanger indicated fault code > 0
	if ((emcStatus->io.status == RCS_ERROR) &&
	    emcStatus->io.fault) {
	    static int reported = -1;
	    if (emcStatus->io.reason > 0) {
		if (reported ^ emcStatus->io.fault) {
		    rcs_print("M6: toolchanger soft fault=%d, reason=%d\n",
			      emcStatus->io.fault, emcStatus->io.reason);
		    reported = emcStatus->io.fault;
		}
		emcStatus->io.status = RCS_DONE; // let program continue
	    } else {
		rcs_print("M6: toolchanger hard fault, reason=%d\n",
			  emcStatus->io.reason);
		// abort since io.status is RCS_ERROR
	    }

	}

	// check for subordinate errors, and halt task if so
	if (emcStatus->motion.status == RCS_ERROR ||
	    ((emcStatus->io.status == RCS_ERROR) &&
	     (emcStatus->io.reason <= 0))) {

	    /*! \todo FIXME-- duplicate code for abort,
	      also in emcTaskExecute()
	      and in emcTaskIssueCommand() */

	    if (emcStatus->io.status == RCS_ERROR) {
		// this is an aborted M6.
		if (emc_debug & EMC_DEBUG_RCS ) {
		    rcs_print("io.status=RCS_ERROR, fault=%d reason=%d\n",
			      emcStatus->io.fault, emcStatus->io.reason);
		}
		if (emcStatus->io.reason < 0) {
		    emcOperatorError(0, io_error, emcStatus->io.reason);
		}
	    }
	    // motion already should have reported this condition (and set RCS_ERROR?)
	    // an M19 orient failed to complete within timeout
	    // if ((emcStatus->motion.status == RCS_ERROR) && 
	    // 	(emcStatus->motion.spindle.orient_state == EMCMOT_ORIENT_FAULTED) &&
	    // 	(emcStatus->motion.spindle.orient_fault != 0)) {
	    // 	emcOperatorError(0, "wait for orient complete timed out");
	    // }

	    // abort everything
	    emcTaskAbort();
	    emcIoAbort(EMC_ABORT_MOTION_OR_IO_RCS_ERROR);
	    emcSpindleAbort();
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

	    emcAbortCleanup(EMC_ABORT_MOTION_OR_IO_RCS_ERROR);

	    // clear out the interpreter state
	    emcStatus->task.interpState = EMC_TASK_INTERP_IDLE;
	    emcStatus->task.execState = EMC_TASK_EXEC_DONE;
	    stepping = 0;
	    steppingWait = 0;

	    // now queue up command to resynch interpreter
	    emcTaskQueueCommand(&taskPlanSynchCmd);
	}

	// update task-specific status
	emcTaskUpdate(&emcStatus->task);

	// handle RCS_STAT_MSG base class members explicitly, since this
	// is not an NML_MODULE and they won't be set automatically

	// do task
	emcStatus->task.command_type = emcCommand->type;
	emcStatus->task.echo_serial_number = emcCommand->serial_number;

	// do top level
	emcStatus->command_type = emcCommand->type;
	emcStatus->echo_serial_number = emcCommand->serial_number;

	if (taskPlanError || taskExecuteError ||
	    emcStatus->task.execState == EMC_TASK_EXEC_ERROR ||
	    emcStatus->motion.status == RCS_ERROR ||
	    emcStatus->io.status == RCS_ERROR) {
	    emcStatus->status = RCS_ERROR;
	    emcStatus->task.status = RCS_ERROR;
	} else if (!taskPlanError && !taskExecuteError &&
		   emcStatus->task.execState == EMC_TASK_EXEC_DONE &&
		   emcStatus->motion.status == RCS_DONE &&
		   emcStatus->io.status == RCS_DONE &&
		   interp_list.len() == 0 &&
		   emcTaskCommand == 0 &&
		   emcStatus->task.interpState == EMC_TASK_INTERP_IDLE) {
	    emcStatus->status = RCS_DONE;
	    emcStatus->task.status = RCS_DONE;
	} else {
	    emcStatus->status = RCS_EXEC;
	    emcStatus->task.status = RCS_EXEC;
	}

	// write it
	// since emcStatus was passed to the WM init functions, it
	// will be updated in the _update() functions above. There's
	// no need to call the individual functions on all WM items.
	emcStatusBuffer->write(emcStatus);


	// detect changes in status and push ticket updates
	if (prev_status ^ emcStatus->status) {
	    fprintf(stderr, "task: update ticket=%d status=%d to origin=%s\n",
		    emcStatus->ticket, emcStatus->status, origin.c_str());

	    publish_ticket_update(emcStatus->status, emcStatus->ticket,
				  origin, current_cmd);

	    prev_status = emcStatus->status;
	}

	// wait on timer cycle, if specified, or calculate actual
	// interval if ini file says to run full out via
	// [TASK] CYCLE_TIME <= 0.0d
	// emcTaskEager = 0;
	if (emcTaskNoDelay) {
	    endTime = etime();
	    deltaTime = endTime - startTime;
	    if (deltaTime < minTime)
		minTime = deltaTime;
	    else if (deltaTime > maxTime)
		maxTime = deltaTime;
	    startTime = endTime;
	}

	if ((emcTaskNoDelay) || (get_eager())) {
	    set_eager(false);
	} else {
	    // timer->wait();  // wait now happens in zmq_poll()
	}
    }
    // end of while (! done)

    // clean up everything
    emctask_shutdown();
    /* debugging */
    if (emcTaskNoDelay) {
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print("cycle times (seconds): %f min, %f max\n", minTime,
		      maxTime);
	}
    }
    // and leave
    exit(0);
}

// encapsulate state access

static int interp_ResumeState = EMC_TASK_INTERP_IDLE;

int  get_interpResumeState(void)
{
    return interp_ResumeState;
}

void set_interpResumeState(int s)
{
    interp_ResumeState = s;
}

// flag signifying that on the next loop, there should be no delay.
// this is set when transferring trajectory data from userspace to kernel
// space, annd reset otherwise.
static bool emc_TaskEager = false;

void set_eager(bool e)
{
    emc_TaskEager = e;
}

bool get_eager(void)
{
    return emc_TaskEager;
}
