/********************************************************************
* Description:  xenomai.h
*               This file defines the differences specific to the
*               the Xenomai user land thread system
********************************************************************/

#define FLAVOR_FLAGS XENOMAI_FLAVOR_FLAGS // see rtapi_compat.h

/* rtapi_proc */
#ifdef RTAPI
#define HAVE_RTAPI_READ_STATUS_HOOK
#endif


/* rtapi_task.c */
// Xenomai rt_task priorities are 0: lowest .. 99: highest
#define PRIO_LOWEST 0
#define PRIO_HIGHEST 99

#define HAVE_RTAPI_TASK_DELETE_HOOK
#define HAVE_RTAPI_TASK_STOP_HOOK
#define HAVE_RTAPI_TASK_PAUSE_HOOK
#define HAVE_RTAPI_WAIT_HOOK
#define HAVE_RTAPI_TASK_RESUME_HOOK
#define HAVE_RTAPI_TASK_SELF_HOOK
#define HAVE_RTAPI_TASK_UPDATE_STATS_HOOK

/* rtapi_time.c */
#define RTAPI_TIME_NO_CLOCK_MONOTONIC  // Xenomai has its own time features
#define HAVE_RTAPI_GET_TIME_HOOK
#define HAVE_RTAPI_GET_CLOCKS_HOOK

/* rtapi_main.c */
#define HAVE_RTAPI_MODULE_INIT_HOOK   // arm SGXCPU handler
#define HAVE_RTAPI_MODULE_EXIT_HOOK   // disarm SGXCPU handler

/* misc */

