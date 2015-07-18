// HAL thread API

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_internal.h"

#ifdef RTAPI

/** 'thread_task()' is a function that is invoked as a realtime task.
    It implements a thread, by running down the thread's function list
    and calling each function in turn.
*/
static void thread_task(void *arg)
{
    hal_thread_t *thread = arg;
    hal_funct_entry_t *funct_root, *funct_entry;
    long long int end_time;

    // thread execution times collected here, doubles as
    // param struct for xthread functs
    hal_funct_args_t fa = {
	.thread = thread,
	.argc = 0,
	.argv = NULL,
    };

    while (1) {
	if (hal_data->threads_running > 0) {
	    /* point at first function on function list */
	    funct_root = (hal_funct_entry_t *) & (thread->funct_list);
	    funct_entry = SHMPTR(funct_root->links.next);
	    /* execution time logging */
	    fa.start_time = rtapi_get_clocks();
	    end_time = fa.start_time;
	    fa.thread_start_time = fa.start_time;

	    /* run thru function list */
	    while (funct_entry != funct_root) {
		/* point to function structure */
		fa.funct = SHMPTR(funct_entry->funct_ptr);

		/* call the function */
		switch (funct_entry->type) {
		case FS_LEGACY_THREADFUNC:
		    funct_entry->funct.l(funct_entry->arg, thread->period);
		    break;
		case FS_XTHREADFUNC:
		    funct_entry->funct.x(funct_entry->arg, &fa);
		    break;
		default:
		    // bad - a mistyped funct
		    ;
		}
		/* capture execution time */
		end_time = rtapi_get_clocks();
		/* update execution time data */
		*(fa.funct->runtime) = (hal_s32_t)(end_time - fa.start_time);
		if ( *(fa.funct->runtime) > fa.funct->maxtime) {
		    fa.funct->maxtime = *(fa.funct->runtime);
		    fa.funct->maxtime_increased = 1;
		} else {
		    fa.funct->maxtime_increased = 0;
		}
		/* point to next next entry in list */
		funct_entry = SHMPTR(funct_entry->links.next);
		/* prepare to measure time for next funct */
		fa.start_time = end_time;
	    }
	    /* update thread execution time */
	    thread->runtime = (hal_s32_t)(end_time - fa.thread_start_time);
	    if (thread->runtime > thread->maxtime) {
		thread->maxtime = thread->runtime;
	    }
	}
	/* wait until next period */
	rtapi_wait();
    }
}

// HAL threads - public API

int hal_create_thread(const char *name, unsigned long period_nsec,
		      int uses_fp, int cpu_id)
{
    int prev_priority;
    int retval, n;
    hal_thread_t *new, *tptr;
    long prev_period, curr_period;

    CHECK_STRLEN(name, HAL_NAME_LEN);
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_CONFIG);
    HALDBG("creating thread %s, %ld nsec", name, period_nsec);

    if (period_nsec == 0) {
	hal_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: create_thread called "
			"with period of zero");
	return -EINVAL;
    }
    {
	WITH_HAL_MUTEX();

	if (halg_find_object_by_name(0, HAL_THREAD, name).thread) {
	    HALERR("duplicate thread name %s", name);
	    return -EINVAL;
	}

	// allocate thread descriptor
	if ((new = halg_create_object(0, sizeof(hal_thread_t),
				       HAL_THREAD, 0, name)) == NULL)
	    return -ENOMEM;

	hh_init_hdrf(&new->hdr, HAL_THREAD, 0, "%s", name);
	dlist_init_entry(&(new->funct_list));

	/* initialize the structure */
	new->uses_fp = uses_fp;
	new->cpu_id = cpu_id;

	/* have to create and start a task to run the thread */
	if (dlist_empty(&hal_data->threads)) {

	    /* this is the first thread created */
	    /* is timer started? if so, what period? */
	    curr_period = rtapi_clock_set_period(0);
	    if (curr_period == 0) {
		/* not running, start it */
		curr_period = rtapi_clock_set_period(period_nsec);
		if (curr_period < 0) {
		    HALERR("clock_set_period returned %ld",
				    curr_period);
		    return -EINVAL;
		}
	    }
	    /* make sure period <= desired period (allow 1% roundoff error) */
	    if (curr_period > (period_nsec + (period_nsec / 100))) {
		HALERR("clock period too long: %ld", curr_period);
		return -EINVAL;
	    }
	    if(hal_data->exact_base_period) {
		hal_data->base_period = period_nsec;
	    } else {
		hal_data->base_period = curr_period;
	    }
	    /* reserve the highest priority (maybe for a watchdog?) */
	    prev_priority = rtapi_prio_highest();
	    /* no previous period to worry about */
	    prev_period = 0;
	} else {
	    /* there are other threads, slowest (and lowest
	       priority) is at head of list */

	    tptr = dlist_first_entry(&hal_data->threads, hal_thread_t, thread);
	    // tptr = SHMPTR(hal_data->thread_list_ptr);
	    prev_period = tptr->period;
	    prev_priority = tptr->priority;
	}

	if ( period_nsec < hal_data->base_period) {
	    HALERR("new thread period %ld is less than clock period %ld",
		   period_nsec, hal_data->base_period);
	    return -EINVAL;
	}
	/* make period an integer multiple of the timer period */
	n = (period_nsec + hal_data->base_period / 2) / hal_data->base_period;
	new->period = hal_data->base_period * n;
	if ( new->period < prev_period ) {
	    HALERR("new thread period %ld is less than existing thread period %ld",
			    period_nsec, prev_period);
	    return -EINVAL;
	}
	/* make priority one lower than previous */
	new->priority = rtapi_prio_next_lower(prev_priority);
	/* create task - owned by library module, not caller */
	retval = rtapi_task_new(thread_task,
				new,
				new->priority,
				lib_module_id,
				global_data->hal_thread_stack_size,
				uses_fp,
				(char *)ho_name(new),
				new->cpu_id);
	if (retval < 0) {
	    HALERR("could not create task for thread %s", name);
	    return -EINVAL;
	}
	new->task_id = retval;

	/* init time logging variables */
	new->runtime = 0;
	new->maxtime = 0;

	/* start task */
	retval = rtapi_task_start(new->task_id, new->period);
	if (retval < 0) {
	    HALERR("could not start task for thread %s: %d", name, retval);
	    return -EINVAL;
	}
	/* insert new structure at head of list */
	dlist_add_before(&new->thread, &hal_data->threads);

	// make it visible
	halg_add_object(false, (hal_object_ptr)new);

    } // exit block protected by scoped lock


    HALDBG("thread %s id %d created prio=%d",
	   name, new->task_id, new->priority);
    return 0;
}

static int delete_thread_cb(hal_object_ptr o, foreach_args_t *args)
{
    free_thread_struct(o.thread);
    return 0;
}

// delete a named thread, or all threads if name == NULL
int halg_exit_thread(const int use_hal_mutex, const char *name)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_RUN);

    hal_data->threads_running = 0;
    {
	WITH_HAL_MUTEX_IF(use_hal_mutex);

	foreach_args_t args =  {
	    .type = HAL_THREAD,
	    .name = (char *)name
	};
	int ret = halg_foreach(0, &args, delete_thread_cb);
	if (name && (ret == 0)) {
	    HALERR("thread '%s' not found",   name);
	    return -EINVAL;
	}
	HALDBG("%d thread%s exited", ret, ret == 1 ? "":"s");
	// all threads stopped & deleted
    }
    return 0;
}

extern int hal_thread_delete(const char *name)
{
    CHECK_STR(name);
    HALDBG("deleting thread '%s'", name);
    return halg_exit_thread(1, name);
}

#endif /* RTAPI */


int hal_start_threads(void)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_RUN);

    HALDBG("starting threads");
    hal_data->threads_running = 1;
    return 0;
}

int hal_stop_threads(void)
{
    CHECK_HALDATA();
    CHECK_LOCK(HAL_LOCK_RUN);

    hal_data->threads_running = 0;
    HALDBG("threads stopped");
    return 0;
}

#ifdef RTAPI

void free_thread_struct(hal_thread_t * thread)
{
    hal_funct_entry_t *funct_entry;
    hal_list_t *list_root, *list_entry;

    /* if we're deleting a thread, we need to stop all threads */
    hal_data->threads_running = 0;

    /* and stop the task associated with this thread */
    rtapi_task_pause(thread->task_id);
    rtapi_task_delete(thread->task_id);

    /* clear the function entry list */
    list_root = &(thread->funct_list);
    list_entry = dlist_next(list_root);
    while (list_entry != list_root) {
	/* entry found, save pointer to it */
	funct_entry = (hal_funct_entry_t *) list_entry;
	/* unlink it, point to the next one */
	list_entry = dlist_remove_entry(list_entry);
	/* free the removed entry */
	free_funct_entry_struct(funct_entry);
    }
    // remove from priority list
    dlist_remove_entry(&thread->thread);
    halg_free_object(false, (hal_object_ptr) thread);
}
#endif /* RTAPI */
