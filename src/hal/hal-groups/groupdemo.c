// demo for HAL group matching and reporting
//
// run:
//   $ halcmd -f nested.hal
// this defines several groups, some of them nested
//
//   $ groupdemo test1
//
// now change a signal which is member of the reported group, eg.
//   $ halcmd sets bar 1.2 # causes a report
//   $ halcmd sets bar 1.3 # no report, since epsilon 0.2 not exceeded
//   $ halcmd sets bar 2.2 # again causes a report

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

#include "rtapi.h"
#include "hal.h"
#include "hal_group.h"

const char *progname = "";
static int done;

static void quit(int sig)
{
    done = 1;
}

static char *data_value2(int type, void *valptr)
{
    char *value_str;
    static char buf[15];

    switch (type) {
    case HAL_BIT:
	if (*((char *) valptr) == 0)
	    value_str = "FALSE";
	else
	    value_str = "TRUE";
	break;
    case HAL_FLOAT:
	snprintf(buf, 14, "%.7g", (double)*((hal_float_t *) valptr));
	value_str = buf;
	break;
    case HAL_S32:
	snprintf(buf, 14, "%ld", (long)*((hal_s32_t *) valptr));
	value_str = buf;
	break;
    case HAL_U32:
	snprintf(buf, 14, "%ld", (unsigned long)*((hal_u32_t *) valptr));
	value_str = buf;
	break;
    default:
	/* Shouldn't get here, but just in case... */
	value_str = "unknown_type";
    }
    return value_str;
}

const char *strhal(int type,void *data)
{
    return data_value2(type, data);
}

int report_callback(int phase, hal_compiled_group_t *cgroup, int handle,
		    hal_sig_t *sig, void *cb_data)
{
    switch (phase) {

    case REPORT_BEGIN:
	// any report initialisation
	printf("report begin for %s\n",cgroup->group->name);
	break;

    case REPORT_SIGNAL:
	// per-reported-signal action
	printf("\tsignal %s  %s\n",sig->name,strhal(sig->type, SHMPTR(sig->data_ptr)));
	break;

    case REPORT_END:
	// finalise & send it off
	printf("report end\n");
	break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int retval;
    hal_compiled_group_t *cgroup;
    char *groupname;
    int msec;
    struct timespec ts;

    progname = argv[0];
    if (argc < 2) {
	fprintf(stderr, "usage: %s groupname\n", progname);
	exit(1);
    }
    groupname = argv[1];

    signal(SIGINT, quit);
    signal(SIGTERM, quit);

    if ((retval = hal_group_compile(groupname, &cgroup))) {
	fprintf(stderr, "hal_group_compile(%s) failed: %d\n", groupname, retval);
	exit(1);
    }
    // the group is now 'referenced'
    // $ halcmd show group test1
    // Group name      Arg1       Arg2       Refs
    // test1           0x000003e8 0x00000001 1
    // --------------------------------------^
    //..

    // a group with a nonzero reference count cannot be changed or deleted:
    // $ halcmd delg test1
    // HAL:0 ERROR: cannot delete group 'test1' (still used: 1)

    // extract update period
    msec = hal_cgroup_timer(cgroup);
    if (msec <= 0) {
	fprintf(stderr, "group %s: invalid period %d, using 1sec\n", groupname, msec);
	msec = 1000;
    }
    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec - ts.tv_sec * 1000) * 1000;

    // an initial full report:
    hal_cgroup_report(cgroup, report_callback, NULL, 1);

    // report/monitor changes periodically:
    while (!done) {
	if (hal_cgroup_match(cgroup)) {
	    retval = hal_cgroup_report(cgroup, report_callback, NULL, 0);
	}
	fprintf(stderr, ".");
	nanosleep(&ts, NULL);
    }

    // this decreases the group reference count by one
    // once the refcount reaches zero, the group may be
    // changed or deleted again:
    hal_unref_group(groupname);
    exit(0);
}
