#ifndef _SETUP_SIGNALS_H
#define _SETUP_SIGNALS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <signal.h>
#include <sys/signalfd.h>

typedef void (*sa_sigaction_t)(int sig, siginfo_t *si, void *uctx);

// setup signal disposition via signalfd, and optionally
// a sa_sigaction type handler for SIGSEGV,  which cannot
// be delivered via signalfd
int setup_signals(const sa_sigaction_t handler);

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

#ifdef __cplusplus
}
#endif
#endif //_SETUP_SIGNALS_H
