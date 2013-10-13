#include <string.h>		// strcpy()
#include <stdlib.h>		// exit()

#include "inifile.hh"		// INIFILE
#include "emcglb.h"		// EMC_INIFILE,NMLFILE, EMC_TASK_CYCLE_TIME
#include "rcs_print.hh"

// eventually:

// typedef struct task_params {
//     double EMC_TASK_CYCLE_TIME_ORIG;
//     emcTaskNoDelay
//     no_force_homing
//     io_error
//     max_mdi_queued_command
// } task_params_t;

extern double EMC_TASK_CYCLE_TIME_ORIG;
extern int emcTaskNoDelay;
extern int no_force_homing;
extern const char *io_error;
extern int max_mdi_queued_commands;

int iniLoad(const char *filename)
{
    IniFile inifile;
    const char *inistring;
    char version[LINELEN], machine[LINELEN];
    double saveDouble;
    int saveInt;

    // open it
    if (inifile.Open(filename) == false) {
	return -1;
    }

    if (NULL != (inistring = inifile.Find("DEBUG", "EMC"))) {
	// copy to global
	if (1 != sscanf(inistring, "%i", &emc_debug)) {
	    emc_debug = 0;
	}
    } else {
	// not found, use default
	emc_debug = 0;
    }
    if (emc_debug & EMC_DEBUG_RCS) {
	// set_rcs_print_flag(PRINT_EVERYTHING);
	max_rcs_errors_to_print = -1;
    }

    if (emc_debug & EMC_DEBUG_VERSIONS) {
	if (NULL != (inistring = inifile.Find("VERSION", "EMC"))) {
	    if(sscanf(inistring, "$Revision: %s", version) != 1) {
		strncpy(version, "unknown", LINELEN-1);
	    }
	} else {
	    strncpy(version, "unknown", LINELEN-1);
	}

	if (NULL != (inistring = inifile.Find("MACHINE", "EMC"))) {
	    strncpy(machine, inistring, LINELEN-1);
	} else {
	    strncpy(machine, "unknown", LINELEN-1);
	}
	rcs_print("task: machine: '%s'  version '%s'\n", machine, version);
    }

    if (NULL != (inistring = inifile.Find("NML_FILE", "EMC"))) {
	// copy to global
	strcpy(emc_nmlfile, inistring);
    } else {
	// not found, use default
    }

    saveInt = emc_task_interp_max_len; //remember default or previously set value
    if (NULL != (inistring = inifile.Find("INTERP_MAX_LEN", "TASK"))) {
	if (1 == sscanf(inistring, "%d", &emc_task_interp_max_len)) {
	    if (emc_task_interp_max_len <= 0) {
		emc_task_interp_max_len = saveInt;
	    }
	} else {
	    emc_task_interp_max_len = saveInt;
	}
    }

    if (NULL != (inistring = inifile.Find("RS274NGC_STARTUP_CODE", "EMC"))) {
	// copy to global
	strcpy(rs274ngc_startup_code, inistring);
    } else {
	if (NULL != (inistring = inifile.Find("RS274NGC_STARTUP_CODE", "RS274NGC"))) {
	    // copy to global
	    strcpy(rs274ngc_startup_code, inistring);
	} else {
	// not found, use default
	}
    }
    saveDouble = emc_task_cycle_time;
    EMC_TASK_CYCLE_TIME_ORIG = emc_task_cycle_time;
    emcTaskNoDelay = 0;
    if (NULL != (inistring = inifile.Find("CYCLE_TIME", "TASK"))) {
	if (1 == sscanf(inistring, "%lf", &emc_task_cycle_time)) {
	    // found it
	    // if it's <= 0.0, then flag that we don't want to
	    // wait at all, which will set the EMC_TASK_CYCLE_TIME
	    // global to the actual time deltas
	    if (emc_task_cycle_time <= 0.0) {
		emcTaskNoDelay = 1;
	    }
	} else {
	    // found, but invalid
	    emc_task_cycle_time = saveDouble;
	    rcs_print
		("invalid [TASK] CYCLE_TIME in %s (%s); using default %f\n",
		 filename, inistring, emc_task_cycle_time);
	}
    } else {
	// not found, using default
	rcs_print("[TASK] CYCLE_TIME not found in %s; using default %f\n",
		  filename, emc_task_cycle_time);
    }


    if (NULL != (inistring = inifile.Find("NO_FORCE_HOMING", "TRAJ"))) {
	if (1 == sscanf(inistring, "%d", &no_force_homing)) {
	    // found it
	    // if it's <= 0.0, then set it 0 so that homing is required before MDI or Auto
	    if (no_force_homing <= 0) {
		no_force_homing = 0;
	    }
	} else {
	    // found, but invalid
	    no_force_homing = 0;
	    rcs_print
		("invalid [TRAJ] NO_FORCE_HOMING in %s (%s); using default %d\n",
		 filename, inistring, no_force_homing);
	}
    } else {
	// not found, using default
	no_force_homing = 0;
    }

    // configurable template for iocontrol reason display
    if (NULL != (inistring = inifile.Find("IO_ERROR", "TASK"))) {
	io_error = strdup(inistring);
    }

    // max number of queued MDI commands
    if (NULL != (inistring = inifile.Find("MDI_QUEUED_COMMANDS", "TASK"))) {
	max_mdi_queued_commands = atoi(inistring);
    }

    // close it
    inifile.Close();

    return 0;
}
