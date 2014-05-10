#include "setup_signals.h"
#include <stdio.h>
#include <assert.h>


#if 0
// handler example:
static void handler(int sig, siginfo_t *si, void *uctx)
{
    fprintf(stderr, "signal %d - '%s' received, dumping core (current dir=%s)",
	    sig, strsignal(sig), get_current_dir_name());
    closelog(); // let syslog drain
    sleep(1);
    // and drop core
    signal(SIGABRT, SIG_DFL);
    abort();
    // not reached
}
#endif

int setup_signals(const sa_sigaction_t handler)
{
    // SIGSEGV delivered via sigaction if handler given
    if (handler != NULL) {
	struct sigaction sig_act;
	sigemptyset( &sig_act.sa_mask );
	sig_act.sa_sigaction = handler;
	sig_act.sa_flags   = SA_SIGINFO;
	sigaction(SIGSEGV, &sig_act, (struct sigaction *) NULL);
    } // else fail miserably, the default way

    sigset_t sigmask;
    sigfillset(&sigmask);

    // since we're using signalfd(),
    // block all signal delivery through normal signal handler
    // except SIGSEGV,
    sigdelset(&sigmask, SIGSEGV);
    if (sigprocmask(SIG_SETMASK, &sigmask, NULL) == -1)
	perror("sigprocmask");

    // now explicitly turn on the signals delivered via  signalfd()

    // sigset of all the signals that we're interested in
    // these we want delivered via signalfd()
    int retval;
    retval = sigemptyset(&sigmask);        assert(retval == 0);
    retval = sigaddset(&sigmask, SIGINT);  assert(retval == 0);
    retval = sigaddset(&sigmask, SIGQUIT); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGKILL); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGTERM); assert(retval == 0);
    retval = sigaddset(&sigmask, SIGFPE);  assert(retval == 0);
    retval = sigaddset(&sigmask, SIGBUS);  assert(retval == 0);
    retval = sigaddset(&sigmask, SIGILL);  assert(retval == 0);

    return signalfd(-1, &sigmask, 0);
}
