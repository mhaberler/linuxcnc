#include <string.h>		// strcpy()
#include <stdlib.h>		// exit()
#include <sys/types.h>		// pid_t
#include <unistd.h>		// fork()
#include <ctype.h>		// isspace()

#include "emc_nml.hh"
#include "rcs_print.hh"

static pid_t emcSystemCmdPid = 0;

void set_SystemCmdPid(pid_t pid)
{
    emcSystemCmdPid = pid;
}

pid_t get_SystemCmdPid(void)
{
    return emcSystemCmdPid;
}

/*
  handling of EMC_SYSTEM_CMD
 */

/* convert string to arg/argv set */

static int argvize(const char *src, char *dst, char *argv[], int len)
{
    char *bufptr;
    int argvix;
    char inquote;
    char looking;

    strncpy(dst, src, len);
    dst[len - 1] = 0;
    bufptr = dst;
    inquote = 0;
    argvix = 0;
    looking = 1;

    while (0 != *bufptr) {
	if (*bufptr == '"') {
	    *bufptr = 0;
	    if (inquote) {
		inquote = 0;
		looking = 1;
	    } else {
		inquote = 1;
	    }
	} else if (isspace(*bufptr) && !inquote) {
	    looking = 1;
	    *bufptr = 0;
	} else if (looking) {
	    looking = 0;
	    argv[argvix] = bufptr;
	    argvix++;
	}
	bufptr++;
    }

    argv[argvix] = 0;		// null-terminate the argv list

    return argvix;
}

int emcSystemCmd(char *s)
{
    char buffer[EMC_SYSTEM_CMD_LEN];
    char *argv[EMC_SYSTEM_CMD_LEN / 2 + 1];

    if (0 != emcSystemCmdPid) {
	// something's already running, and we can only handle one
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print
		("emcSystemCmd: abandoning process %d, running ``%s''\n",
		 emcSystemCmdPid, s);
	}
    }

    emcSystemCmdPid = fork();

    if (-1 == emcSystemCmdPid) {
	// we're still the parent, with no child created
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print("system command ``%s'' can't be executed\n", s);
	}
	return -1;
    }

    if (0 == emcSystemCmdPid) {
	// we're the child
	// convert string to argc/argv
	argvize(s, buffer, argv, EMC_SYSTEM_CMD_LEN);
	// drop any setuid privileges
	setuid(getuid());
	execvp(argv[0], argv);
	// if we get here, we didn't exec
	if (emc_debug & EMC_DEBUG_TASK_ISSUE) {
	    rcs_print("emcSystemCmd: can't execute ``%s''\n", s);
	}
	exit(-1);
    }
    // else we're the parent
    return 0;
}
