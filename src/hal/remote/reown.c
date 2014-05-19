// demo to show what a process servicing a remote
// component would do:
//
// startup: hal_aquire() the component
//
// peer connected:  hal_bind()
//
//     receive updates, push to HAL pins
//     detect changed pins and send reports
//
// peer disconnects: hal_unbind()
//
// serving process exits: hal_release() the component

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include "rtapi.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_rcomp.h"
#include "hal_group.h"

const char *progname = "";
const char *modname = NULL;
int my_pid;
int hal_comp_id;

static int done;

static void quit(int sig)
{
    int retval;
    done = 1;

    fprintf(stderr, "\n");

    // 'remote disconnected'
    retval = hal_unbind(modname);
    if (retval) {
	fprintf(stderr, "hal_unbind(%s) failed: %d\n",
		modname, retval);
	hal_release(modname);
	exit(1);
    } else
	fprintf(stderr, "component '%s' unbound\n", modname);

    // 'serving process' exiting
    retval = hal_release(modname);
    if (retval) {
	fprintf(stderr, "hal_release(%s) failed: %d\n",
		modname, retval);
	exit(1);
    }
    fprintf(stderr, "component '%s' disowned\n", modname);
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

int comp_state_cb(hal_compstate_t *cs,  void *cb_data)
{
    printf("comp='%s', type=%d state=%d pid=%d ",
	   cs->name, cs->type,cs->state, cs->pid);
    printf("last_bound=%ld last_unbound=%ld last_update=%ld\n",
	   cs->last_bound, cs->last_unbound, cs->last_update);
    return 0; // continue iterating
}

int pin_state_cb(hal_pinstate_t *ps,  void *cb_data)
{
    printf("pin='%s', type=%d dir=%d ownedby='%s' ",
	   ps->name, ps->type,ps->dir, ps->owner_name);
    printf("epsilon=%f flags=%d ", ps->epsilon, ps->flags);
    printf("pin location ptr=%p\n", ps->value);
    return 0; // continue iterating
}

int report_callback(int phase, hal_compiled_comp_t *cc, hal_pin_t *pin,
		     hal_data_u *value, void *cb_data)
{
    switch (phase) {

    case REPORT_BEGIN:
	// any report initialisation
	printf("report begin for %s\n",cc->comp->name);
	break;

    case REPORT_PIN:
	// per-reported-pin action
	printf("\tpin %s  %s\n",pin->name,strhal(pin->type,value));
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
    hal_compiled_comp_t *cc;
    int proxycomp;

    progname = argv[0];
    if (argc < 2) {
	fprintf(stderr, "usage: %s compname\n", progname);
	exit(1);
    }

    modname = argv[1];
    signal(SIGINT, quit);
    signal(SIGTERM, quit);
    if ((proxycomp = hal_init("reown-rtapi")) < 0) {
	fprintf(stderr, " rtapi_init() failed: %d\n", proxycomp);
	exit(1);
    }
    hal_ready(proxycomp);
    if ((retval = hal_compile_comp(modname, &cc))) {
	fprintf(stderr, "hal_comp_compile(%s) failed: %d\n", modname, retval);
	exit(1);
    }

    // component iterator usage:

    // just count components
    // this might be useful e.g. for memory allocation
    retval = hal_retrieve_compstate(NULL, // visit all comps
				    NULL, //  we're just counting components
				    NULL);  // not used
    fprintf(stderr, "%d components found\n", retval);

    // visit components via callback function:
    retval = hal_retrieve_compstate(NULL, // visit all comps
				    comp_state_cb, // our callback function
				    NULL);  // not used
    fprintf(stderr, "hal_retrieve_compstate(): visited %d components\n", retval);


    // pin iterator usage:

    // just count pins
    retval = hal_retrieve_pinstate(modname, // visit only this comp
				   NULL,    // we're just counting pins
				   NULL);   // not used

    fprintf(stderr, "component '%s' has %d pins\n", modname,retval);

    // visit pins via callback function:
    retval = hal_retrieve_pinstate(modname, // visit only this comp
				   pin_state_cb, // our callback function
				   NULL);  // not used

    fprintf(stderr, "hal_retrieve_pinstate(): visited %d pins\n", retval);


    // takeover the remote hal component
    my_pid = getpid();

    // this says 'this remote component now has a
    // matching serving process'
    hal_comp_id = hal_acquire(modname, my_pid );

    if ((hal_comp_id < 0) || done) {
        fprintf(stderr, "%s: ERROR: hal_aquire(%s,%d) failed\n",
		progname, modname, my_pid);
	exit(1);
    }
    fprintf(stderr, "component '%s' reowned\n", modname);

    // this says:
    // 'connected and receiving/sending updates'

    retval = hal_bind(modname);
    if (retval) {
	fprintf(stderr, "hal_bind(%s) failed: %d\n",
		modname, retval);
	hal_release(modname);
	exit(1);
    }
    fprintf(stderr, "component '%s' bound\n", modname);

    // an initial full report:
    hal_ccomp_report(cc, report_callback, NULL, 1);

    // report/monitor changes periodically:
    while (!done) {
	if (hal_ccomp_match(cc)) {
	    retval = hal_ccomp_report(cc, report_callback, NULL, 0);
	}
	fprintf(stderr, ".");
	sleep(1);
    }
    exit(0);
}
