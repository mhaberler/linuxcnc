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
#include "nmlsetup.hh"             // EMCMOT_ORIENT_*

// NML channels
RCS_CMD_CHANNEL *emcCommandBuffer = 0;
RCS_STAT_CHANNEL *emcStatusBuffer = 0;
NML *emcErrorBuffer = 0;

extern int done; // main
extern int emcTaskNoDelay; //main
extern RCS_TIMER *timer; //main
extern int shutdown_zmq(void);

// called to allocate and init resources
int emctask_startup()
{
    double end;
    int good;

#define RETRY_TIME 10.0		// seconds to wait for subsystems to come up
#define RETRY_INTERVAL 1.0	// seconds between wait tries for a subsystem

    // moved up so it can be exposed in taskmodule at init timed
    // // get our status data structure
    // emcStatus = new EMC_STAT;

    // get the NML command buffer
    if (!(emc_debug & EMC_DEBUG_NML)) {
	set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
	// messages
    }
    end = RETRY_TIME;
    good = 0;
    do {
	if (NULL != emcCommandBuffer) {
	    delete emcCommandBuffer;
	}
	emcCommandBuffer =
	    new RCS_CMD_CHANNEL(emcFormat, "emcCommand", "emc",
				emc_nmlfile);
	if (emcCommandBuffer->valid()) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// restore diag
    // messages
    if (!good) {
	rcs_print_error("can't get emcCommand buffer\n");
	return -1;
    }
    // get our command data structure
    // emcCommand = emcCommandBuffer->get_address();

    // get the NML status buffer
    if (!(emc_debug & EMC_DEBUG_NML)) {
	set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
	// messages
    }
    end = RETRY_TIME;
    good = 0;
    do {
	if (NULL != emcStatusBuffer) {
	    delete emcStatusBuffer;
	}
	emcStatusBuffer =
	    new RCS_STAT_CHANNEL(emcFormat, "emcStatus", "emc",
				 emc_nmlfile);
	if (emcStatusBuffer->valid()) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// restore diag
    // messages
    if (!good) {
	rcs_print_error("can't get emcStatus buffer\n");
	return -1;
    }

    if (!(emc_debug & EMC_DEBUG_NML)) {
	set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
	// messages
    }
    end = RETRY_TIME;
    good = 0;
    do {
	if (NULL != emcErrorBuffer) {
	    delete emcErrorBuffer;
	}
	emcErrorBuffer =
	    new NML(nmlErrorFormat, "emcError", "emc", emc_nmlfile);
	if (emcErrorBuffer->valid()) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// restore diag
    // messages
    if (!good) {
	rcs_print_error("can't get emcError buffer\n");
	return -1;
    }
    // get the timer
    if (!emcTaskNoDelay) {
	timer = new RCS_TIMER(emc_task_cycle_time, "", "");
    }
    // initialize the subsystems

    // IO first

    if (!(emc_debug & EMC_DEBUG_NML)) {
	set_rcs_print_destination(RCS_PRINT_TO_NULL);	// inhibit diag
	// messages
    }
    end = RETRY_TIME;
    good = 0;
    do {
	if (0 == emcIoInit()) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    set_rcs_print_destination(RCS_PRINT_TO_STDOUT);	// restore diag
    // messages
    if (!good) {
	rcs_print_error("can't initialize IO\n");
	return -1;
    }

    end = RETRY_TIME;
    good = 0;
    do {
	if (0 == emcIoUpdate(&emcStatus->io)) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    if (!good) {
	rcs_print_error("can't read IO status\n");
	return -1;
    }


    // now motion

    end = RETRY_TIME;
    good = 0;
    do {
	if (0 == emcMotionInit()) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    if (!good) {
	rcs_print_error("can't initialize motion\n");
	return -1;
    }

    end = RETRY_TIME;
    good = 0;
    do {
	if (0 == emcMotionUpdate(&emcStatus->motion)) {
	    good = 1;
	    break;
	}
	esleep(RETRY_INTERVAL);
	end -= RETRY_INTERVAL;
	if (done) {
	    emctask_shutdown();
	    exit(1);
	}
    } while (end > 0.0);
    if (!good) {
	rcs_print_error("can't read motion status\n");
	return -1;
    }
    // now the interpreter

    if (0 != emcTaskPlanInit()) {
	rcs_print_error("can't initialize interpreter\n");
	return -1;
    }

    if (done ) {
	emctask_shutdown();
	exit(1);
    }

    // now task
    if (0 != emcTaskInit()) {
	rcs_print_error("can't initialize task\n");
	return -1;
    }
    emcTaskUpdate(&emcStatus->task);

    return 0;
}

// called to deallocate resources
int emctask_shutdown(void)
{
    // shut down the subsystems
    if (0 != emcStatus) {
	emcTaskHalt();
	emcTaskPlanExit();
	emcMotionHalt();
	emcIoHalt();
    }
    // delete the timer
    if (0 != timer) {
	delete timer;
	timer = 0;
    }
    // delete the NML channels

    if (0 != emcErrorBuffer) {
	delete emcErrorBuffer;
	emcErrorBuffer = 0;
    }

    if (0 != emcStatusBuffer) {
	delete emcStatusBuffer;
	emcStatusBuffer = 0;
	emcStatus = 0;
    }

    if (0 != emcCommandBuffer) {
	delete emcCommandBuffer;
	emcCommandBuffer = 0;
	//emcCommand = 0;
    }

    if (0 != emcStatus) {
	delete emcStatus;
	emcStatus = 0;
    }

    shutdown_zmq();
    return 0;
}
