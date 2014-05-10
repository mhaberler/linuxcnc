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

#include <machinetalk/generated/message.pb.h>

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
#include "task.hh"
#include "main.hh"
#include "plan.hh"
#include "execute.hh"
#include "taskclass.hh"
#include "motion.h"             // EMCMOT_ORIENT_*
#include "interpqueue.hh"
#include "readahead.hh"
#include "iniload.hh"
#include "nmlsetup.hh"
#include "zmqsupport.hh"


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
// zmq completion pub socket
extern void *completion; // needed for subscription detection


void emctask_quit(int sig)
{
    // set main's done flag
    done = 1;
    // restore signal handler
    signal(sig, emctask_quit);
}



// made visible for execute.cc
int emcAuxInputWaitType = 0;
int emcAuxInputWaitIndex = -1;
// delay counter
double taskExecDelayTimeout = 0.0;

// commands we compose here
//static EMC_TASK_PLAN_INIT taskPlanInitCmd;

int programStartLine = 0;	// which line to run program from
// how long the interp list can be

int stepping = 0;
int steppingWait = 0;

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

	if (zsocket_poll (completion, 0)) {
            zmsg_t *msg = zmsg_recv (completion);
	    char *s = zmsg_popstr (msg);

	    // on subscribe request  publish current ticket status
	    fprintf(stderr, "--- main: %ssubscribe '%s' event\n",
		    *s ? "" : "un", s+1);
	    free(s);
	    zmsg_destroy(&msg);
	    publish_ticket_update(emcStatus->status, emcStatus->ticket, origin);
	}
	if (zsocket_poll (cmd, get_eager()||emcTaskNoDelay ? 0: 10)) {
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
	    emcTaskQueueSynchCmd();

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
