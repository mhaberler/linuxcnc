/** HAL stands for Hardware Abstraction Layer, and is used by EMC to
    transfer realtime data to and from I/O devices and other low-level
    modules.
*/
/********************************************************************
* Description:  hal_libc.c
*               This file, 'hal_lib.c', implements the HAL API, for 
*               both user space and realtime modules.  It uses the 
*               RTAPI and ULAPI #define symbols to determine which 
*               version to compile.
*
* Author: John Kasunich
* License: LGPL Version 2
*    
* Copyright (c) 2003 All rights reserved.
*
* Last change: 
********************************************************************/

/** Copyright (C) 2003 John Kasunich
                       <jmkasunich AT users DOT sourceforge DOT net>

    Other contributors:
                       Paul Fox
                       <pgf AT foxharp DOT boston DOT ma DOT us>
		       Alex Joni
		       <alex_joni AT users DOT sourceforge DOT net>

    extensively reworked for the 'new RTOS' support and unified build
                       Michael Haberler
                       <git AT mah DOT priv DOT at>
*/

/** This library is free software; you can redistribute it and/or
    modify it under the terms of version 2.1 of the GNU Lesser General
    Public License as published by the Free Software Foundation.
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111 USA

    THE AUTHORS OF THIS LIBRARY ACCEPT ABSOLUTELY NO LIABILITY FOR
    ANY HARM OR LOSS RESULTING FROM ITS USE.  IT IS _EXTREMELY_ UNWISE
    TO RELY ON SOFTWARE ALONE FOR SAFETY.  Any machinery capable of
    harming persons must have provisions for completely removing power
    from all motors, etc, before persons enter any danger area.  All
    machinery must be designed to comply with local and national safety
    codes, and the authors of this software can not, and do not, take
    any responsibility for such compliance.

    This code was written as part of the EMC HAL project.  For more
    information, go to www.linuxcnc.org.

*/

#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"		/* HAL private decls */
#include "hal_ring.h"		/* HAL ringbuffer decls */
#include "hal_group.h"		/* HAL ringbuffer decls */

#include "rtapi_string.h"
#ifdef RTAPI
#include "rtapi_app.h"
/* module information */
MODULE_AUTHOR("John Kasunich");
MODULE_DESCRIPTION("Hardware Abstraction Layer for EMC");
MODULE_LICENSE("GPL");
#define assert(e)
#endif /* RTAPI */


#if defined(ULAPI)
// if loading the ulapi shared object fails, we dont have RTAPI
// calls available for error messages, so fallback to stderr
// in this case.
#include <stdio.h>
#include <sys/types.h>		/* pid_t */
#include <unistd.h>		/* getpid() */
#include <dlfcn.h>              /* for dlopen/dlsym ulapi-$THREADSTYLE.so */
#include <assert.h>
#include <time.h>               /* remote comp bind/unbind/update timestamps */
#include <limits.h>             /* PATH_MAX */
#include <stdlib.h>		/* exit() */
#include "rtapi/shmdrv/shmdrv.h"
#endif

// avoid pulling in math.h
#define FABS(a) ((a) > 0.0 ? (a) : -(a))	/* floating absolute value */

char *hal_shmem_base = 0;
hal_data_t *hal_data = 0;
static int lib_module_id = -1;	/* RTAPI module ID for library module */
static int lib_mem_id = -1;	/* RTAPI shmem ID for library module */

// the global data segment contains vital instance information and
// the error message ringbuffer, so all HAL entities attach it
// it is initialized and populated by rtapi_msgd which is assumed
// to run before any HAL/RTAPI activity starts, and until after it ends.
// RTAPI cannot execute without the global segment existing and attached.

// depending on ULAP/ vs RTAPI and thread system (userland vs kthreads)
// the way how
// pointer to the global data segment, initialized by calling the
// the ulapi.so ulapi_main() method (ULAPI) or by external reference
// to the instance module (kernel modes)


// defined(BUILD_SYS_KBUILD) && defined(RTAPI)
// defined & attached in, and exported from rtapi_module.c

// defined(BUILD_SYS_USER_DSO) && defined(RTAPI)
// defined & attached in, and exported from rtapi_main.c

// ULAPI: defined & attached in, & exported from ulapi_autoload.c
extern global_data_t *global_data;

// this is the pointer through which _all_ RTAPI/ULAPI references go:
extern rtapi_switch_t *rtapi_switch;

extern void  rtapi_printall(void);

/***********************************************************************
*                  LOCAL FUNCTION DECLARATIONS                         *
************************************************************************/

/* These functions are used internally by this file.  The code is at
   the end of the file.  */

/** init_hal_data() initializes the entire HAL data structure, only
    if the structure has not already been initialized.  (The init
    is done by the first HAL component to be loaded.
*/
static int init_hal_data(void);

/** The 'shmalloc_xx()' functions allocate blocks of shared memory.
    Each function allocates a block that is 'size' bytes long.
    If 'size' is 3 or more, the block is aligned on a 4 byte
    boundary.  If 'size' is 2, it is aligned on a 2 byte boundary,
    and if 'size' is 1, it is unaligned.
    These functions do not test a mutex - they are called from
    within the hal library by code that already has the mutex.
    (The public function 'hal_malloc()' is a wrapper that gets the
    mutex and then calls 'shmalloc_up()'.)
    The only difference between the two functions is the location
    of the allocated memory.  'shmalloc_up()' allocates from the
    base of shared memory and works upward, while 'shmalloc_dn()'
    starts at the top and works down.
    This is done to improve realtime performance.  'shmalloc_up()'
    is used to allocate data that will be accessed by realtime
    code, while 'shmalloc_dn()' is used to allocate the much
    larger structures that are accessed only occaisionally during
    init.  This groups all the realtime data together, inproving
    cache performance.
*/
static void *shmalloc_up(long int size);
static void *shmalloc_dn(long int size);

/** The alloc_xxx_struct() functions allocate a structure of the
    appropriate type and return a pointer to it, or 0 if they fail.
    They attempt to re-use freed structs first, if none are
    available, then they call hal_malloc() to create a new one.
    The free_xxx_struct() functions add the structure at 'p' to
    the appropriate free list, for potential re-use later.
    All of these functions assume that the caller has already
    grabbed the hal_data mutex.
*/
hal_comp_t *halpr_alloc_comp_struct(void);
static hal_pin_t *alloc_pin_struct(void);
static hal_sig_t *alloc_sig_struct(void);
static hal_param_t *alloc_param_struct(void);
static hal_group_t *alloc_group_struct(void);
static hal_member_t *alloc_member_struct(void);
static hal_oldname_t *halpr_alloc_oldname_struct(void);
#ifdef RTAPI
static hal_funct_t *alloc_funct_struct(void);
#endif /* RTAPI */
static hal_funct_entry_t *alloc_funct_entry_struct(void);
#ifdef RTAPI
static hal_thread_t *alloc_thread_struct(void);
#endif /* RTAPI */

static void free_comp_struct(hal_comp_t * comp);
static void unlink_pin(hal_pin_t * pin);
static void free_pin_struct(hal_pin_t * pin);
static void free_sig_struct(hal_sig_t * sig);
static void free_param_struct(hal_param_t * param);
static void free_oldname_struct(hal_oldname_t * oldname);
#ifdef RTAPI
static void free_funct_struct(hal_funct_t * funct);
#endif /* RTAPI */
static void free_funct_entry_struct(hal_funct_entry_t * funct_entry);
#ifdef RTAPI
static void free_thread_struct(hal_thread_t * thread);
#endif /* RTAPI */
static void free_group_struct(hal_group_t * group);
static void free_member_struct(hal_member_t * member);
static hal_group_t *find_group_of_member(const char *member);

// rings are never freed.
static hal_ring_t *alloc_ring_struct(void);
static hal_ring_t *find_ring_by_name(hal_data_t *hd, const char *name);

static hal_ring_attachment_t *alloc_ring_attachment_struct(void);
static void        free_ring_attachment_struct(hal_ring_attachment_t *ra);
static hal_ring_attachment_t *find_ring_attachment_by_name(hal_data_t *hd, const char *name);


// FIXME replace by rtapi size_aligned?
static ring_size_t size_aligned(ring_size_t x);

static void free_context_struct(hal_context_t * p);
static hal_context_t *alloc_context_struct(int pid);
static int context_pid();

#ifdef RTAPI
/** 'thread_task()' is a function that is invoked as a realtime task.
    It implements a thread, by running down the thread's function list
    and calling each function in turn.
*/
static void thread_task(void *arg);
#endif /* RTAPI */

// the phases where startup-related events happen:
// ULAPI: HAL shared library load/unload
// RTAPI: beginning or end of rtapi_app_main()
//        or rtapi_app_exit()
enum phase_t  {
    SHLIB_LOADED,
    SHLIB_UNLOADED,
    MAIN_START,
    MAIN_END,
    EXIT_START,
    EXIT_END
};

static int context_pid()
{
#if defined(BUILD_SYS_USER_DSO)
    return getpid();
#else
#if defined(RTAPI)
    return 0; // meaning 'in kernel'
#else
    return getpid();
#endif
#endif
}

/***********************************************************************
*                  PUBLIC (API) FUNCTION CODE                          *
************************************************************************/
int hal_init_mode(const char *name, int type)
{
    int comp_id;
    char rtapi_name[RTAPI_NAME_LEN + 1];
    char hal_name[HAL_NAME_LEN + 1];
    hal_comp_t *comp;

    hal_context_t *context;
    int pid = context_pid();

    // tag message origin field
    rtapi_set_logtag("hal_lib");

    if (name == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: no component name\n");
	return -EINVAL;
    }
    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component name '%s' is too long\n", name);
	return -EINVAL;
    }
    // rtapi initialisation already done
    // since this happens through the constructor
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: initializing component '%s'\n",
		    name);
    /* copy name to local vars, truncating if needed */
    rtapi_snprintf(rtapi_name, RTAPI_NAME_LEN, "HAL_%s", name);
    rtapi_snprintf(hal_name, sizeof(hal_name), "%s", name);
    /* do RTAPI init */
    comp_id = rtapi_init(rtapi_name);
    if (comp_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: rtapi init failed\n");
	return -EINVAL;
    }
    // tag message origin field since ulapi autoload re-tagged them
    rtapi_set_logtag("hal_lib");
#ifdef ULAPI
    hal_rtapi_attach();
#endif
    /* get mutex before manipulating the shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* make sure name is unique in the system */
    if (halpr_find_comp_by_name(hal_name) != 0) {
	/* a component with this name already exists */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: duplicate component name '%s'\n", hal_name);
	rtapi_exit(comp_id);
	return -EINVAL;
    }
    // refer to context
    if ((context = THIS_CONTEXT()) == NULL) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: BUG: HAL context NULL: %s\n", name);
	rtapi_exit(comp_id);
	return -EINVAL;
    }
    /* allocate a new component structure */
    comp = halpr_alloc_comp_struct();
    if (comp == 0) {
	/* couldn't allocate structure */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for component '%s'\n", hal_name);
	rtapi_exit(comp_id);
	return -ENOMEM;
    }

    // update context
    context->refcount++;  // comps in this context

    /* initialize the comp structure */

    // back reference to enclosing context
    comp->context_ptr = SHMOFF(context);

    comp->comp_id = comp_id;
    comp->type = type;
#ifdef RTAPI
    comp->pid = pid;
#else /* ULAPI */
    // a remote component starts out disowned
    comp->pid = comp->type == TYPE_REMOTE ? 0 : pid;
#endif
    comp->state = COMP_INITIALIZING;
    comp->last_update = 0;
    comp->last_bound = 0;
    comp->last_unbound = 0;
    //comp->shmem_base = hal_shmem_base;
    comp->insmod_args = 0;
    rtapi_snprintf(comp->name, sizeof(comp->name), "%s", hal_name);
    /* insert new structure at head of list */
    comp->next_ptr = hal_data->comp_list_ptr;
    hal_data->comp_list_ptr = SHMOFF(comp);

    // synch local namespaces with what was told us by hal_data
    hal_namespace_sync();

    /* done with list, release mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    /* done */
    rtapi_print_msg(RTAPI_MSG_DBG,
	"HAL: component '%s' initialized, ID = %02d\n", hal_name, comp_id);
    return comp_id;
}


#if defined(ULAPI)
int hal_bind(const char *comp_name)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_name(comp_name);

    if (comp == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_bind(): no such component '%s'\n", comp_name);
	return -EINVAL;
    }
    if (comp->type != TYPE_REMOTE) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_bind(%s): not a remote componet (%d)\n",
			comp_name, comp->type);
	return -EINVAL;
    }
    if (comp->state != COMP_UNBOUND) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_bind(%s): state not unbound (%d)\n",
			comp_name, comp->state);
	return -EINVAL;
    }
    comp->state = COMP_BOUND;
    comp->last_bound = (long int) time(NULL);; // XXX ugly
    return 0;
}

int hal_unbind(const char *comp_name)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_name(comp_name);

    if (comp == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_unbind(): no such component '%s'\n",
			comp_name);
	return -EINVAL;
    }
    if (comp->type != TYPE_REMOTE) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_unbind(%s): not a remote componet (%d)\n",
			comp_name, comp->type);
	return -EINVAL;
    }
    if (comp->state != COMP_BOUND) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_unbind(%s): state not bound (%d)\n",
			comp_name, comp->state);
	return -EINVAL;
    }
    comp->state = COMP_UNBOUND;
    comp->last_unbound = (long int) time(NULL);; // XXX ugly
    return 0;
}

int hal_acquire(const char *comp_name, int pid)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_name(comp_name);

    if (comp == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_acquire(): no such component '%s'\n",
			comp_name);
	return -EINVAL;
    }
    if (comp->type != TYPE_REMOTE) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_acquire(%s): not a remote componet (%d)\n",
			comp_name, comp->type);
	return -EINVAL;
    }
    if (comp->state == COMP_BOUND) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_acquire(%s): cant reown a bound component (%d)\n",
			comp_name, comp->state);
	return -EINVAL;
    }
    // let a comp be 'adopted away' from the RT environment
    // this is a tad hacky, should separate owner pid from RT/user distinction
    if ((comp->pid !=0) &&
	(comp->pid != global_data->rtapi_app_pid))

	{
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_acquire(%s): component already owned by pid %d\n",
			comp_name, comp->pid);
	return -EINVAL;
    }
    comp->pid = pid;
    return comp->comp_id;
}

int hal_release(const char *comp_name)
{
    hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_name(comp_name);

    if (comp == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_release(): no such component '%s'\n",
			comp_name);
	return -EINVAL;
    }
    if (comp->type != TYPE_REMOTE) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_release(%s): not a remote componet (%d)\n",
			comp_name, comp->type);
	return -EINVAL;
    }
    if (comp->pid == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: hal_release(%s): component already disowned\n",
			comp_name);
	return -EINVAL;
    }

    if (comp->pid != getpid()) {
	rtapi_print_msg(RTAPI_MSG_WARN,
			"HAL: hal_release(%s): component owned by pid %d\n",
			comp_name, comp->pid);
	// return -EINVAL;
    }
    comp->pid = 0;
    return 0;
}

// introspection support for remote components.

int hal_retrieve_compstate(const char *comp_name,
			   hal_retrieve_compstate_callback_t callback,
			   void *cb_data)
{
    int next;
    int nvisited = 0;
    int result;
    hal_compstate_t state;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: hal_retrieve_compstate called before ULAPI init\n");
	return -EINVAL;
    }
    {
	hal_comp_t *comp  __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* search for the comp */
	next = hal_data->comp_list_ptr;
	while (next != 0) {
	    comp = SHMPTR(next);
	    if (!comp_name || (strcmp(comp->name, comp_name)) == 0) {
		nvisited++;
		/* this is the right comp */
		if (callback) {
		    // fill in the details:
		    state.type = comp->type;
		    state.state = comp->state;
		    state.last_update = comp->last_update;
		    state.last_bound = comp->last_bound;
		    state.last_unbound = comp->last_unbound;
		    state.pid = comp->pid;
		    state.insmod_args = comp->insmod_args;
		    strncpy(state.name, comp->name, sizeof(comp->name));

		    result = callback(&state, cb_data);
		    if (result < 0) {
			// callback signaled an error, pass that back up.
			return result;
		    } else if (result > 0) {
			// callback signaled 'stop iterating'.
			// pass back the number of visited comps so far.
			return nvisited;
		    } else {
			// callback signaled 'OK to continue'
			// fall through
		    }
		} else {
		    // null callback passed in,
		    // just count comps
		    // nvisited already bumped above.
		}
	    }
	    /* no match, try the next one */
	    next = comp->next_ptr;
	}
	rtapi_print_msg(RTAPI_MSG_DBG,
			"HAL: hal_retrieve_compstate: visited %d comps\n", nvisited);
	/* if we get here, we ran through all the comps, so return count */
	return nvisited;
    }
}

int hal_retrieve_pinstate(const char *comp_name,
			  hal_retrieve_pins_callback_t callback,
			  void *cb_data)
{
    int next;
    int nvisited = 0;
    int result;
    hal_comp_t *comp = NULL;
    hal_comp_t *owner;
    hal_pinstate_t pinstate;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: hal_retrieve_pinstate called before ULAPI init\n");
	return -EINVAL;
    }
    {
	hal_pin_t *pin __attribute__((cleanup(halpr_autorelease_mutex)));

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	if (comp_name != NULL) {
	    comp = halpr_find_comp_by_name(comp_name);
	    if (comp == NULL) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: hal_retrieve_pinstate: component '%s' not found\n", comp_name);
		return -EINVAL;
	    }
	}
	// either comp == NULL, so visit all pins
	// or comp != NULL, in which case visit only this
	// component's pins

	// walk the pinlist
	next = hal_data->pin_list_ptr;
	while (next != 0) {
	    pin = SHMPTR(next);
	    owner = SHMPTR(pin->owner_ptr);
	    if (!comp_name || (owner->comp_id == comp->comp_id)) {
		nvisited++;
		/* this is the right comp */
		if (callback) {
		    // fill in the details:
		    pinstate.signal_inst = pin->signal_inst;
		    // NB: cover remote link case!
		    pinstate.value = SHMPTR(pin->data_ptr_addr);
		    pinstate.type = pin->type;
		    pinstate.dir = pin->dir;
		    pinstate.epsilon = pin->epsilon;
		    pinstate.flags = pin->flags;
		    strncpy(pinstate.name, pin->name, sizeof(pin->name));
		    strncpy(pinstate.owner_name, owner->name, sizeof(owner->name));

		    result = callback(&pinstate, cb_data);
		    if (result < 0) {
			// callback signaled an error, pass that back up.
			return result;
		    } else if (result > 0) {
			// callback signaled 'stop iterating'.
			// pass back the number of visited pins so far.
			return nvisited;
		    } else {
			// callback signaled 'OK to continue'
			// fall through
		    }
		} else {
		    // null callback passed in,
		    // just count pins
		    // nvisited already bumped above.
		}
	    }
	    /* no match, try the next one */
	    next = pin->next_ptr;
	}
	rtapi_print_msg(RTAPI_MSG_DBG,
			"HAL: hal_retrieve_pinstate: visited %d pins\n", nvisited);
	/* if we get here, we ran through all the pins, so return count */
	return nvisited;
    }
}

// component reporting support

int hal_compile_comp(const char *name, hal_compiled_comp_t **ccomp)
{
   hal_compiled_comp_t *tc;
   hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));

   if (!name) {
       rtapi_print_msg(RTAPI_MSG_ERR,
		       "HAL:%d ERROR: hal_compile_comp() called with NULL name\n",
		       rtapi_instance);
	return -EINVAL;
   }
   {
       hal_comp_t *comp __attribute__((cleanup(halpr_autorelease_mutex)));
       int next, n;
       hal_comp_t *owner;
       hal_pin_t *pin;

       rtapi_mutex_get(&(hal_data->mutex));

       if ((comp = halpr_find_comp_by_name(name)) == NULL) {
	   rtapi_print_msg(RTAPI_MSG_ERR,
		       "HAL:%d ERROR: hal_comp_compile(%s): no such comp\n",
			   rtapi_instance, name);
	   return -EINVAL;
       }

       // array sizing: count pins owned by this component
       next = hal_data->pin_list_ptr;
       n = 0;
       while (next != 0) {
	    pin = SHMPTR(next);
	    owner = SHMPTR(pin->owner_ptr);
	    if (owner->comp_id == comp->comp_id)
		n++;
	    next = pin->next_ptr;
       }
       if (n == 0) {
	   rtapi_print_msg(RTAPI_MSG_ERR,
			   "HAL:%d ERROR: component %s has no pins to watch for changes\n",
			   rtapi_instance, name);
	   return -EINVAL;
       }
       // a compiled comp is a userland/per process memory object
       if ((tc = malloc(sizeof(hal_compiled_comp_t))) == NULL)
	   return -ENOMEM;

       memset(tc, 0, sizeof(hal_compiled_comp_t));
       tc->comp = comp;
       tc->n_pins = n;

       // alloc pin array
       if ((tc->pin = malloc(sizeof(hal_pin_t *) * tc->n_pins)) == NULL)
	   return -ENOMEM;
       // alloc tracking value array
       if ((tc->tracking =
	    malloc(sizeof(hal_data_u) * tc->n_pins )) == NULL)
	   return -ENOMEM;
       // alloc change bitmap
       if ((tc->changed =
	    malloc(RTAPI_BITMAP_SIZE(tc->n_pins))) == NULL)
	    return -ENOMEM;

       memset(tc->pin, 0, sizeof(hal_pin_t *) * tc->n_pins);
       memset(tc->tracking, 0, sizeof(hal_data_u) * tc->n_pins);
       RTAPI_ZERO_BITMAP(tc->changed,tc->n_pins);

       // fill in pin array
       n = 0;
       next = hal_data->pin_list_ptr;
       while (next != 0) {
	   pin = SHMPTR(next);
	   owner = SHMPTR(pin->owner_ptr);
	   if (owner->comp_id == comp->comp_id)
	       tc->pin[n++] = pin;
	   next = pin->next_ptr;
       }
       assert(n == tc->n_pins);
       tc->magic = CCOMP_MAGIC;
       *ccomp = tc;
   }
   return 0;
}

int hal_ccomp_apply(hal_compiled_comp_t *cc, int handle, hal_type_t type, hal_data_u value)
{
    hal_data_t *sig_hal_data;
    hal_pin_t *pin;
    hal_sig_t *sig;
    hal_data_u *dp;
    hal_context_t *ctx = THIS_CONTEXT();

    assert(cc->magic ==  CCOMP_MAGIC);

    // handles (value references) run from 0..cc->n_pin-1
    if ((handle < 0) || (handle > cc->n_pins-1))
	return -ERANGE;
    pin = cc->pin[handle];
    if (pin->type != type)
	return -EINVAL;
    if (pin->signal != 0) {
	// linked to a signal; cover crosslink too
	sig_hal_data = ctx->namespaces[pin->signal_inst].haldata;
	sig = SHMPTR_IN(sig_hal_data, pin->signal);
	dp = SHMPTR_IN(sig_hal_data, sig->data_ptr);
    } else {
	dp = (hal_data_u *)(hal_shmem_base + SHMOFF(&(pin->dummysig)));
    }
    switch (type) {
    case HAL_TYPE_UNSPECIFIED:
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: hal_ccomp_apply(): invalid HAL type %d\n",
			   rtapi_instance, type);
	   return -EINVAL;
    case HAL_BIT:
	dp->b = value.b;
	break;
    case HAL_FLOAT:
	dp->f = value.f;
	break;
    case HAL_S32:
	dp->s = value.s;
	break;
    case HAL_U32:
	dp->u = value.u;
	break;
    }
    return 0;
}

int hal_ccomp_match(hal_compiled_comp_t *cc)
{
    hal_context_t *ctx = THIS_CONTEXT();
    int i, nchanged = 0;
    hal_bit_t halbit;
    hal_s32_t hals32;
    hal_s32_t halu32;
    hal_float_t halfloat,delta;
    hal_data_t *sig_hal_data;
    hal_pin_t *pin;
    hal_sig_t *sig;
    void *data_ptr;

    assert(cc->magic ==  CCOMP_MAGIC);
    RTAPI_ZERO_BITMAP(cc->changed, cc->n_pins);

    for (i = 0; i < cc->n_pins; i++) {
	pin = cc->pin[i];
	if (pin->signal != 0) {
	    // linked to a signal; cover crosslink too
	    sig_hal_data = ctx->namespaces[pin->signal_inst].haldata;
	    sig = SHMPTR_IN(sig_hal_data, pin->signal);
	    data_ptr = SHMPTR_IN(sig_hal_data, sig->data_ptr);
	} else {
	    data_ptr = hal_shmem_base + SHMOFF(&(pin->dummysig));
	}

	switch (pin->type) {
	case HAL_BIT:
	    halbit = *((char *) data_ptr);
	    if (cc->tracking[i].b != halbit) {
		nchanged++;
		RTAPI_BIT_SET(cc->changed, i);
		cc->tracking[i].b = halbit;
	    }
	    break;
	case HAL_FLOAT:
	    halfloat = *((hal_float_t *) data_ptr);
	    delta = FABS(halfloat - cc->tracking[i].f);
	    if (delta > pin->epsilon) {
		nchanged++;
		RTAPI_BIT_SET(cc->changed, i);
		cc->tracking[i].f = halfloat;
	    }
	    break;
	case HAL_S32:
	    hals32 =  *((hal_s32_t *) data_ptr);
	    if (cc->tracking[i].s != hals32) {
		nchanged++;
		RTAPI_BIT_SET(cc->changed, i);
		cc->tracking[i].s = hals32;
	    }
	    break;
	case HAL_U32:
	    halu32 =  *((hal_u32_t *) data_ptr);
	    if (cc->tracking[i].u != halu32) {
		nchanged++;
		RTAPI_BIT_SET(cc->changed, i);
		cc->tracking[i].u = halu32;
	    }
	    break;
	default:
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d BUG: hal_ccomp_match(%s): invalid type for pin %s: %d\n",
			    rtapi_instance, cc->comp->name, pin->name, pin->type);
	    return -EINVAL;
	}
    }
    return nchanged;
}

int hal_ccomp_report(hal_compiled_comp_t *cc,
		     comp_report_callback_t report_cb,
		     void *cb_data, int report_all)
{
    int retval, i;
    hal_context_t *ctx = THIS_CONTEXT();
    void *data_ptr;
    hal_data_t *sig_hal_data;
    hal_pin_t *pin;
    hal_sig_t *sig;

    if (!report_cb)
	return 0;
    if ((retval = report_cb(REPORT_BEGIN, cc, NULL,  -1, NULL, cb_data)) < 0)
	return retval;

    for (i = 0; i < cc->n_pins; i++) {
	if (report_all || RTAPI_BIT_TEST(cc->changed, i)) {
	    pin = cc->pin[i];
	    if (pin->signal != 0) {
		// take care of crosslinked pins
		sig_hal_data = ctx->namespaces[pin->signal_inst].haldata;
		sig = SHMPTR_IN(sig_hal_data, pin->signal);
		data_ptr = SHMPTR_IN(sig_hal_data, sig->data_ptr);
	    } else {
		data_ptr = hal_shmem_base + SHMOFF(&(pin->dummysig));
	    }
	    if ((retval = report_cb(REPORT_PIN, cc, pin, i,
				    data_ptr, cb_data)) < 0)
		return retval;
	}
    }
    return report_cb(REPORT_END, cc, NULL, -1, NULL, cb_data);
}

int hal_ccomp_free(hal_compiled_comp_t *cc)
{
    if (cc == NULL)
	return 0;
    assert(cc->magic ==  CCOMP_MAGIC);
    if (cc->tracking)
	free(cc->tracking);
    if (cc->changed)
	free(cc->changed);  // BAD
    if (cc->pin)
	free(cc->pin);
    free(cc);
    return 0;
}
#endif
int hal_exit(int comp_id)
{
    int *prev, next;
    hal_comp_t *comp;
    char name[HAL_NAME_LEN + 1];
    hal_context_t *context;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: exit called before init\n");
	return -EINVAL;
    }
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: removing component %02d\n", comp_id);
    /* grab mutex before manipulating list */
    rtapi_mutex_get(&(hal_data->mutex));
    /* search component list for 'comp_id' */
    prev = &(hal_data->comp_list_ptr);
    next = *prev;
    if (next == 0) {
	/* list is empty - should never happen, but... */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }
    comp = SHMPTR(next);
    while (comp->comp_id != comp_id) {
	/* not a match, try the next one */
	prev = &(comp->next_ptr);
	next = *prev;
	if (next == 0) {
	    /* reached end of list without finding component */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: component %d not found\n", comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
    }
    // lookup enclosing context
    if ((context = THIS_CONTEXT()) == NULL) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: BUG: hal_exit(%d,%s): no enclosing context!\n",
			comp_id, comp->name);
	return -EINVAL;
    }
    context->refcount--;
    /* found our component, unlink it from the list */
    *prev = comp->next_ptr;
    /* save component name for later */
    rtapi_snprintf(name, sizeof(name), "%s", comp->name);
    /* get rid of the component */
    free_comp_struct(comp);
/*! \todo Another #if 0 */
#if 0
    /*! \todo FIXME - this is the beginning of a two pronged approach to managing
       shared memory.  Prong 1 - re-init the shared memory allocator whenever 
       it is known to be safe.  Prong 2 - make a better allocator that can
       reclaim memory allocated by components when those components are
       removed. To be finished later. */
    /* was that the last component? */
    if (hal_data->comp_list_ptr == 0) {
	/* yes, are there any signals or threads defined? */
	if ((hal_data->sig_list_ptr == 0) && (hal_data->thread_list_ptr == 0)) {
	    /* no, invalidate "magic" number so shmem will be re-inited when
	       a new component is loaded */
	    hal_data->magic = 0;
	}
    }
#endif
    /* release mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    // the RTAPI resources are now released
    // on hal_lib shared library unload
    rtapi_exit(comp_id);
    /* done */
    rtapi_print_msg(RTAPI_MSG_DBG,
	"HAL: component %02d removed, name = '%s'\n", comp_id, name);

    return 0;
}

void *hal_malloc(long int size)
{
    void *retval;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: hal_malloc called before init\n");
	return 0;
    }
    /* get the mutex */
    rtapi_mutex_get(&(hal_data->mutex));
    /* allocate memory */
    retval = shmalloc_up(size);
    /* release the mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    /* check return value */
    if (retval == 0) {
	rtapi_print_msg(RTAPI_MSG_DBG,
	    "HAL: hal_malloc() can't allocate %ld bytes\n", size);
    }
    return retval;
}

#ifdef RTAPI
int hal_set_constructor(int comp_id, constructor make) {
    int next;
    hal_comp_t *comp;

    rtapi_mutex_get(&(hal_data->mutex));

    /* search component list for 'comp_id' */
    next = hal_data->comp_list_ptr;
    if (next == 0) {
	/* list is empty - should never happen, but... */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }

    comp = SHMPTR(next);
    while (comp->comp_id != comp_id) {
	/* not a match, try the next one */
	next = comp->next_ptr;
	if (next == 0) {
	    /* reached end of list without finding component */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: component %d not found\n", comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
    }
    
    comp->make = make;

    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}
#endif

int hal_ready(int comp_id) {
    int next;
    hal_comp_t *comp;

    rtapi_mutex_get(&(hal_data->mutex));

    /* search component list for 'comp_id' */
    next = hal_data->comp_list_ptr;
    if (next == 0) {
	/* list is empty - should never happen, but... */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }

    comp = SHMPTR(next);
    while (comp->comp_id != comp_id) {
	/* not a match, try the next one */
	next = comp->next_ptr;
	if (next == 0) {
	    /* reached end of list without finding component */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: component %d not found\n", comp_id);
	    return -EINVAL;
	}
	comp = SHMPTR(next);
    }
    if(comp->state > COMP_INITIALIZING) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: Component '%s' already ready (%d)\n",
			comp->name, comp->state);
        rtapi_mutex_give(&(hal_data->mutex));
        return -EINVAL;
    }
    comp->state = (comp->type == TYPE_REMOTE ?  COMP_UNBOUND : COMP_READY);
    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}

char *hal_comp_name(int comp_id)
{
    hal_comp_t *comp;
    char *result = NULL;
    rtapi_mutex_get(&(hal_data->mutex));
    comp = halpr_find_comp_by_id(comp_id);
    if(comp) result = comp->name;
    rtapi_mutex_give(&(hal_data->mutex));
    return result;
}

/***********************************************************************
*                      "LOCKING" FUNCTIONS                             *
************************************************************************/
/** The 'hal_set_lock()' function sets locking based on one of the 
    locking types defined in hal.h
*/
int hal_set_lock(unsigned char lock_type) {
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: set_lock called before init\n");
	return -EINVAL;
    }
    hal_data->lock = lock_type;
    return 0;
}

/** The 'hal_get_lock()' function returns the current locking level 
    locking types defined in hal.h
*/

unsigned char hal_get_lock() {
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: get_lock called before init\n");
	return -EINVAL;
    }
    return hal_data->lock;
}


/***********************************************************************
*                        "PIN" FUNCTIONS                               *
************************************************************************/

/* wrapper functs for typed pins - these call the generic funct below */

int hal_pin_bit_new(const char *name, hal_pin_dir_t dir,
    hal_bit_t ** data_ptr_addr, int comp_id)
{
    return hal_pin_new(name, HAL_BIT, dir, (void **) data_ptr_addr, comp_id);
}

int hal_pin_float_new(const char *name, hal_pin_dir_t dir,
    hal_float_t ** data_ptr_addr, int comp_id)
{
    return hal_pin_new(name, HAL_FLOAT, dir, (void **) data_ptr_addr,
	comp_id);
}

int hal_pin_u32_new(const char *name, hal_pin_dir_t dir,
    hal_u32_t ** data_ptr_addr, int comp_id)
{
    return hal_pin_new(name, HAL_U32, dir, (void **) data_ptr_addr, comp_id);
}

int hal_pin_s32_new(const char *name, hal_pin_dir_t dir,
    hal_s32_t ** data_ptr_addr, int comp_id)
{
    return hal_pin_new(name, HAL_S32, dir, (void **) data_ptr_addr, comp_id);
}

static int hal_pin_newfv(hal_type_t type, hal_pin_dir_t dir,
    void ** data_ptr_addr, int comp_id, const char *fmt, va_list ap)
{
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        rtapi_print_msg(RTAPI_MSG_ERR,
	    "hal_pin_newfv: length %d too long for name starting '%s'\n",
	    sz, name);
        return -ENOMEM;
    }
    return hal_pin_new(name, type, dir, data_ptr_addr, comp_id);
}

int hal_pin_bit_newf(hal_pin_dir_t dir,
    hal_bit_t ** data_ptr_addr, int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_BIT, dir, (void**)data_ptr_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_float_newf(hal_pin_dir_t dir,
    hal_float_t ** data_ptr_addr, int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_FLOAT, dir, (void**)data_ptr_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_u32_newf(hal_pin_dir_t dir,
    hal_u32_t ** data_ptr_addr, int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_U32, dir, (void**)data_ptr_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_pin_s32_newf(hal_pin_dir_t dir,
    hal_s32_t ** data_ptr_addr, int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_pin_newfv(HAL_S32, dir, (void**)data_ptr_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}


/* this is a generic function that does the majority of the work. */

int hal_pin_new(const char *name, hal_type_t type, hal_pin_dir_t dir,
    void **data_ptr_addr, int comp_id)
{
    int *prev, next, cmp;
    hal_pin_t *new, *ptr;
    hal_comp_t *comp;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin_new called before init\n");
	return -EINVAL;
    }

    if(*data_ptr_addr) 
    {
        rtapi_print_msg(RTAPI_MSG_ERR,
            "HAL: ERROR: pin_new(%s) called with already-initialized memory\n",
            name);
    }
    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U32\n");
	return -EINVAL;
    }
    
    if(dir != HAL_IN && dir != HAL_OUT && dir != HAL_IO) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin direction not one of HAL_IN, HAL_OUT, or HAL_IO\n");
	return -EINVAL;
    }
    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin_new called while HAL locked\n");
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: creating pin '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* validate comp_id */
    comp = halpr_find_comp_by_id(comp_id);
    if (comp == 0) {
	/* bad comp_id */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }
    /* validate passed in pointer - must point to HAL shmem */
    if (! SHMCHK(data_ptr_addr)) {
	/* bad pointer */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: data_ptr_addr not in shared memory\n");
	return -EINVAL;
    }
    if(comp->state > COMP_INITIALIZING) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: pin_new called after hal_ready (%d)\n", comp->state);
	return -EINVAL;
    }
    /* allocate a new variable structure */
    new = alloc_pin_struct();
    if (new == 0) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for pin '%s'\n", name);
	return -ENOMEM;
    }
    /* initialize the structure */
    new->data_ptr_addr = SHMOFF(data_ptr_addr);
    new->signal_inst = rtapi_instance;
    new->owner_ptr = SHMOFF(comp);
    new->type = type;
    new->dir = dir;
    new->signal = 0;
    memset(&new->dummysig, 0, sizeof(hal_data_u));
    rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
    /* make 'data_ptr' point to dummy signal */
    *data_ptr_addr = hal_shmem_base + SHMOFF(&(new->dummysig));
    /* search list for 'name' and insert new structure */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	ptr = SHMPTR(next);
	cmp = strcmp(ptr->name, new->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	if (cmp == 0) {
	    /* name already in list, can't insert */
	    free_pin_struct(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: duplicate variable '%s'\n", name);
	    return -EINVAL;
	}
	/* didn't find it yet, look at next one */
	prev = &(ptr->next_ptr);
	next = *prev;
    }
}

int hal_pin_alias(const char *pin_name, const char *alias)
{
    int *prev, next, cmp;
    hal_pin_t *pin, *ptr;
    hal_oldname_t *oldname;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin_alias called before init\n");
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin_alias called while HAL locked\n");
	return -EPERM;
    }
    if (alias != NULL ) {
	if (strlen(alias) > HAL_NAME_LEN) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
	        "HAL: ERROR: alias name '%s' is too long\n", alias);
	    return -EINVAL;
	}
    }
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    if (alias != NULL ) {
	pin = halpr_find_pin_by_name(alias);
	if ( pin != NULL ) {
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
	        "HAL: ERROR: duplicate pin/alias name '%s'\n", alias);
	    return -EINVAL;
	}
    }
    /* once we unlink the pin from the list, we don't want to have to
       abort the change and repair things.  So we allocate an oldname
       struct here, then free it (which puts it on the free list).  This
       allocation might fail, in which case we abort the command.  But
       if we actually need the struct later, the next alloc is guaranteed
       to succeed since at least one struct is on the free list. */
    oldname = halpr_alloc_oldname_struct();
    if ( oldname == NULL ) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for pin_alias\n");
	return -EINVAL;
    }
    free_oldname_struct(oldname);
    /* find the pin and unlink it from pin list */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, not found */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: pin '%s' not found\n", pin_name);
	    return -EINVAL;
	}
	pin = SHMPTR(next);
	if ( strcmp(pin->name, pin_name) == 0 ) {
	    /* found it, unlink from list */
	    *prev = pin->next_ptr;
	    break;
	}
	if (pin->oldname != 0 ) {
	    oldname = SHMPTR(pin->oldname);
	    if (strcmp(oldname->name, pin_name) == 0) {
		/* found it, unlink from list */
		*prev = pin->next_ptr;
		break;
	    }
	}
	/* didn't find it yet, look at next one */
	prev = &(pin->next_ptr);
	next = *prev;
    }
    if ( alias != NULL ) {
	/* adding a new alias */
	if ( pin->oldname == 0 ) {
	    /* save old name (only if not already saved) */
	    oldname = halpr_alloc_oldname_struct();
	    pin->oldname = SHMOFF(oldname);
	    rtapi_snprintf(oldname->name, sizeof(oldname->name), "%s", pin->name);
	}
	/* change pin's name to 'alias' */
	rtapi_snprintf(pin->name, sizeof(pin->name), "%s", alias);
    } else {
	/* removing an alias */
	if ( pin->oldname != 0 ) {
	    /* restore old name (only if pin is aliased) */
	    oldname = SHMPTR(pin->oldname);
	    rtapi_snprintf(pin->name, sizeof(pin->name), "%s", oldname->name);
	    pin->oldname = 0;
	    free_oldname_struct(oldname);
	}
    }
    /* insert pin back into list in proper place */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    pin->next_ptr = next;
	    *prev = SHMOFF(pin);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	ptr = SHMPTR(next);
	cmp = strcmp(ptr->name, pin->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    pin->next_ptr = next;
	    *prev = SHMOFF(pin);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* didn't find it yet, look at next one */
	prev = &(ptr->next_ptr);
	next = *prev;
    }
}

/***********************************************************************
*                      "SIGNAL" FUNCTIONS                              *
************************************************************************/

int hal_signal_new(const char *name, hal_type_t type)
{

    int *prev, next, cmp;
    hal_sig_t *new, *ptr;
    void *data_addr;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: signal_new called before init\n");
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: signal name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: signal_new called while HAL is locked\n");
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: creating signal '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* check for an existing signal with the same name */
    if (halpr_find_sig_by_name(name) != 0) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: duplicate signal '%s'\n", name);
	return -EINVAL;
    }
    /* allocate memory for the signal value */
    switch (type) {
    case HAL_BIT:
	data_addr = shmalloc_up(sizeof(hal_bit_t));
	break;
    case HAL_S32:
	data_addr = shmalloc_up(sizeof(hal_u32_t));
	break;
    case HAL_U32:
	data_addr = shmalloc_up(sizeof(hal_s32_t));
	break;
    case HAL_FLOAT:
	data_addr = shmalloc_up(sizeof(hal_float_t));
	break;
    default:
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: illegal signal type %d'\n", type);
	return -EINVAL;
	break;
    }
    /* allocate a new signal structure */
    new = alloc_sig_struct();
    if ((new == 0) || (data_addr == 0)) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for signal '%s'\n", name);
	return -ENOMEM;
    }
    /* initialize the signal value */
    switch (type) {
    case HAL_BIT:
	*((char *) data_addr) = 0;
	break;
    case HAL_S32:
	*((hal_s32_t *) data_addr) = 0;
        break;
    case HAL_U32:
	*((hal_u32_t *) data_addr) = 0;
        break;
    case HAL_FLOAT:
	*((hal_float_t *) data_addr) = 0.0;
	break;
    default:
	break;
    }
    /* initialize the structure */
    new->data_ptr = SHMOFF(data_addr);
    new->type = type;
    new->readers = 0;
    new->writers = 0;
    new->bidirs = 0;
    rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
    /* search list for 'name' and insert new structure */
    prev = &(hal_data->sig_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	ptr = SHMPTR(next);
	cmp = strcmp(ptr->name, new->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* didn't find it yet, look at next one */
	prev = &(ptr->next_ptr);
	next = *prev;
    }
}

int hal_signal_delete(const char *name)
{
    hal_sig_t *sig;
    int *prev, next;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: signal_delete called before init\n");
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: signal_delete called while HAL locked\n");
	return -EPERM;
    }
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: deleting signal '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* search for the signal */
    prev = &(hal_data->sig_list_ptr);
    next = *prev;
    while (next != 0) {
	sig = SHMPTR(next);
	if (strcmp(sig->name, name) == 0) {
	    hal_group_t *grp = find_group_of_member(name);
	    if (grp) {
		rtapi_mutex_give(&(hal_data->mutex));
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: cannot delete signal '%s' since it is member of group '%s'\n",
				name, grp->name);
		return -EINVAL;
	    }
	    /* this is the right signal, unlink from list */
	    *prev = sig->next_ptr;
	    /* and delete it */
	    free_sig_struct(sig);
	    /* done */
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* no match, try the next one */
	prev = &(sig->next_ptr);
	next = *prev;
    }
    /* if we get here, we didn't find a match */
    rtapi_mutex_give(&(hal_data->mutex));
    rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: signal '%s' not found\n",
	name);
    return -EINVAL;
}

// given a remote NS prefix, determine if the remote NS exists,
// is reqested and attached, and return its instance id or -EINVAL
// NB: hal_mutex is expected to be held
static int match_remote(const char*name)
{
    int i;
    hal_context_t *ctx = THIS_CONTEXT();

    for (i = 0; i < MAX_INSTANCES; i++) {
	if (RTAPI_BIT_TEST(hal_data->requested_namespaces,i) &&
	    !strcmp(name, hal_data->nsdesc[i].name)) {
	    // namespace is requested; check if mapped in
	    // this context
	    if (RTAPI_BIT_TEST(ctx->visible_namespaces,i))
		return i;
	    else {
		rtapi_print_msg(RTAPI_MSG_DBG,
				"HAL:%d remote namespace %s not visible in this context\n",
				rtapi_instance, name);
		return -EINVAL;
	    }
	}
    }
    return -EINVAL;
}

// objects which can be linked to remotely
enum remote_objects {
    OBJ_SIGNAL,
    OBJ_RING
};

enum name_property {
    NAME_LOCAL=0,
    NAME_REMOTE=1,
    NAME_INVALID=2,
};

static char *nameprop[] = {
    "local", "remote", "invalid"
};

// investigate a name, expecting a particular type (pin or ring)
// ok if all prerequisites ticked
// hal_data must be locked when calling remote_name
static int remote_name(int type, const char *name, int *remote, char *basename, char *prefix)
{
    hal_sig_t *sig;
    int instance, retval, retval2;
    char tmpname[HAL_NAME_LEN+1];
    char *s,  *localname;
    hal_data_t *remote_hal_data;
    hal_context_t *ctx = THIS_CONTEXT();
    hal_ring_t *ring;

    if (name == NULL) {
	return NAME_INVALID;
    }
    strcpy(tmpname, name);
    s = strchr(tmpname,':');
    if (s) {
	// a remote name
	*s = '\0';
	localname = s+1;
    } else {
	// a local name
	*remote = rtapi_instance;
	if (basename)
	    strcpy(basename, name);
	return NAME_LOCAL;
    }

    rtapi_mutex_get(&(hal_data->mutex));
    instance = match_remote(tmpname);
    if (instance < 0) {
	rtapi_mutex_give(&(hal_data->mutex));
	return instance;
    }
    // valid instance, requested and mapped in this context:
    remote_hal_data = ctx->namespaces[instance].haldata;

    rtapi_mutex_give(&(hal_data->mutex));

    // lock both hal_data segments in instance order
    if ((retval = halpr_lock_ordered(rtapi_instance, instance)) != 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: failed to lock %d/%d\n",
			rtapi_instance, rtapi_instance, instance);
	return retval;
    }
    // at this point, both HAL segment's mutexes are held by us
    // investigate the remote HAL segment for the requested object type
    switch (type) {

    case OBJ_SIGNAL:
	if ((sig = halpr_remote_find_sig_by_name(remote_hal_data,
						 localname)) == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: remote signal %s does not exist in namespace %s/%d\n",
			    rtapi_instance,localname, tmpname, instance);
	    retval =  NAME_INVALID;
	    goto unlock;
	}
	retval =  NAME_REMOTE;
	if (remote)
	    *remote = instance;
	if (basename)
	    strcpy(basename, localname);
	if (prefix)
	    strcpy(prefix, tmpname);
	break;

    case OBJ_RING:
	if ((ring = find_ring_by_name(remote_hal_data,
				      localname)) == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: remote ring %s does not exist in namespace %s/%d\n",
			    rtapi_instance, localname, tmpname, instance);
	    retval =  NAME_INVALID;
	    goto unlock;
	}
	retval = NAME_REMOTE;
	if (remote)
	    *remote = instance;
	if (basename)
	    strcpy(basename, localname);
	if (prefix)
	    strcpy(prefix, tmpname);
	break;

    default:
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: invalid remote object type %d for %s\n",
			rtapi_instance, type, name);
	retval =  NAME_INVALID;
    }
 unlock:
    retval2 = halpr_unlock_ordered(rtapi_instance, instance);
    if (retval2) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: failed to unlock %d/%d\n",
			rtapi_instance, rtapi_instance, instance);
	return retval2;
    }
    return retval;
}

int hal_link(const char *pin_name, const char *sig_name)
{
    hal_pin_t *pin;
    hal_sig_t *sig;
    hal_comp_t *comp;
    void **data_ptr_addr, *data_addr;
    int status;
    hal_data_t *my_remote_hal_data, *comp_remote_hal_data;
    hal_context_t *comp_ctx;
    hal_context_t *my_ctx = THIS_CONTEXT();
    char localname[HAL_NAME_LEN+1];
    char prefix[HAL_NAME_LEN+1];

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: link called before init\n");
	return -EINVAL;
    }
    {   // purpose of this block:
	// cleanup mutex(es) on scope exit regardless if one,
	// or both hal_data segments were locked
	int remote_instance
	    __attribute__((cleanup(halpr_autorelease_ordered))) = rtapi_instance;

	// get local mutex before accessing data structures
	rtapi_mutex_get(&(hal_data->mutex));

	if (hal_data->lock & HAL_LOCK_CONFIG)  {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: link called while HAL locked\n");
	    return -EPERM;
	}
	/* make sure we were given a pin name */
	if (pin_name == 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: pin name not given\n");
	    return -EINVAL;
	}
	/* make sure we were given a signal name */
	if (sig_name == 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: signal name not given\n");
	    return -EINVAL;
	}
	rtapi_print_msg(RTAPI_MSG_DBG,
			"HAL: linking pin '%s' to '%s'\n", pin_name, sig_name);

	// check if remote signal, and preconditions set
	if ((status = remote_name(OBJ_SIGNAL, sig_name,
				  &remote_instance, localname, prefix)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR, "HAL:%d remote namespace for '%s' not associated\n",
			    rtapi_instance, sig_name);
	    return status;
	}
	rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d name=%s instance=%d\n",
			rtapi_instance, nameprop[status], remote_instance);

	/* locate the pin */
	pin = halpr_find_pin_by_name(pin_name);
	if (pin == 0) {
	    /* not found */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: pin '%s' not found\n", pin_name);
	    return -EINVAL;
	}
	// need to lookup context as seen by owning component
	comp = SHMPTR(pin->owner_ptr);
	comp_ctx = SHMPTR(comp->context_ptr);
	comp_remote_hal_data = comp_ctx->namespaces[remote_instance].haldata;
	my_remote_hal_data   = my_ctx->namespaces[remote_instance].haldata;

	// if signal is in a different instance, upgrade locks in sequence
	if (remote_instance != rtapi_instance) {
	    // but to avoid a circular deadlock we need to
	    // re-lock in instance order
	    rtapi_mutex_give(&(hal_data->mutex));
	    halpr_lock_ordered(rtapi_instance, remote_instance);
	}

	// at this point, good to manipulate objects in either hal_data segment

	/* locate the signal */
	if (status == NAME_LOCAL) { // local link
	    sig = halpr_find_sig_by_name(sig_name);
	} else {
	    // remote link
	    sig = halpr_remote_find_sig_by_name(my_remote_hal_data, localname);
	}
	if (sig == 0) {
	    /* not found */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: signal '%s' not found\n", sig_name);
	    return -EINVAL;
	}

	/* found both pin and signal, are they already connected? */
	if (SHMPTR_IN(my_remote_hal_data, pin->signal) == sig) {
	    rtapi_print_msg(RTAPI_MSG_WARN,
			    "HAL: Warning: pin '%s' already linked to '%s'\n", pin_name, sig_name);
	    return 0;
	}
	/* is the pin connected to something else? */
	if(pin->signal) {
	    sig = SHMPTR_IN(my_remote_hal_data, pin->signal);
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: pin '%s' is linked to '%s', cannot link to '%s'\n",
			    pin_name, sig->name, sig_name);
	    return -EINVAL;
	}
	/* check types */
	if (pin->type != sig->type) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: type mismatch '%s' <- '%s'\n", pin_name, sig_name);
	    return -EINVAL;
	}
	/* linking output pin to sig that already has output or I/O pins? */
	if ((pin->dir == HAL_OUT) && ((sig->writers > 0) || (sig->bidirs > 0 ))) {
	    /* yes, can't do that */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: signal '%s' already has output or I/O pin(s)\n", sig_name);
	    return -EINVAL;
	}
	/* linking bidir pin to sig that already has output pin? */
	if ((pin->dir == HAL_IO) && (sig->writers > 0)) {
	    /* yes, can't do that */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: signal '%s' already has output pin\n", sig_name);
	    return -EINVAL;
	}
	/* everything is OK, make the new link */
	data_ptr_addr = SHMPTR(pin->data_ptr_addr);

	// bend into remote segment as needed
	data_addr = SHMPTR_IN(comp_remote_hal_data, sig->data_ptr);
	*data_ptr_addr = data_addr;

	/* update the signal's reader/writer/bidir counts */
	if ((pin->dir & HAL_IN) != 0) {
	    sig->readers++;
	}
	if (pin->dir == HAL_OUT) {
	    sig->writers++;
	}
	if (pin->dir == HAL_IO) {
	    sig->bidirs++;
	}
	/* and update the pin */
	pin->signal = SHMOFF_IN(my_remote_hal_data, sig);
	pin->signal_inst = remote_instance;

	// the cleanup attribute on remote_instance will unlock
	// the local, or both hal_data segments in order as needed
	return 0;
    }
}

int hal_unlink(const char *pin_name)
{
    hal_pin_t *pin;

    // preliminary checks are done before locking
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: unlink called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: unlink called while HAL locked\n");
	return -EPERM;
    }
    /* make sure we were given a pin name */
    if (pin_name == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: pin name not given\n");
	return -EINVAL;
    }
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL: unlinking pin '%s'\n", pin_name);

    {	// cleanup mutex(es) regardless if one,
	// or both hal_data segments were locked
	// doing this in a separate block avoids a spurious error message by
	// halpr_autorelease_ordered if one of the tests above happens
	// to fail
	int remote_instance
	    __attribute__((cleanup(halpr_autorelease_ordered))) = rtapi_instance;

	/* get mutex before accessing data structures */
	rtapi_mutex_get(&(hal_data->mutex));

	/* locate the pin */
	pin = halpr_find_pin_by_name(pin_name);

	if (pin == 0) {
	    /* not found */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: pin '%s' not found\n", pin_name);
	    return -EINVAL;
	}

	// if this is a cross-link..
	remote_instance = pin->signal_inst;

	// we need to lock remote hal_data segment too
	if (remote_instance != rtapi_instance) {
	    // but to avoid a circular deadlock we need to
	    // re-lock in instance order
	    rtapi_mutex_give(&(hal_data->mutex));
	    halpr_lock_ordered(rtapi_instance, remote_instance);
	}

	/* found pin, unlink it */
	unlink_pin(pin);

	// done, mutex(es) will be released on scope exit automagically
	return 0;
    }
}

/***********************************************************************
*                       "PARAM" FUNCTIONS                              *
************************************************************************/

/* wrapper functs for typed params - these call the generic funct below */

int hal_param_bit_new(const char *name, hal_param_dir_t dir, hal_bit_t * data_addr,
    int comp_id)
{
    return hal_param_new(name, HAL_BIT, dir, (void *) data_addr, comp_id);
}

int hal_param_float_new(const char *name, hal_param_dir_t dir, hal_float_t * data_addr,
    int comp_id)
{
    return hal_param_new(name, HAL_FLOAT, dir, (void *) data_addr, comp_id);
}

int hal_param_u32_new(const char *name, hal_param_dir_t dir, hal_u32_t * data_addr,
    int comp_id)
{
    return hal_param_new(name, HAL_U32, dir, (void *) data_addr, comp_id);
}

int hal_param_s32_new(const char *name, hal_param_dir_t dir, hal_s32_t * data_addr,
    int comp_id)
{
    return hal_param_new(name, HAL_S32, dir, (void *) data_addr, comp_id);
}

static int hal_param_newfv(hal_type_t type, hal_param_dir_t dir,
	void *data_addr, int comp_id, const char *fmt, va_list ap) {
    char name[HAL_NAME_LEN + 1];
    int sz;
    sz = rtapi_vsnprintf(name, sizeof(name), fmt, ap);
    if(sz == -1 || sz > HAL_NAME_LEN) {
        rtapi_print_msg(RTAPI_MSG_ERR,
	    "hal_param_newfv: length %d too long for name starting '%s'\n",
	    sz, name);
	return -ENOMEM;
    }
    return hal_param_new(name, type, dir, (void *) data_addr, comp_id);
}

int hal_param_bit_newf(hal_param_dir_t dir, hal_bit_t * data_addr,
    int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_BIT, dir, (void*)data_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_float_newf(hal_param_dir_t dir, hal_float_t * data_addr,
    int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_FLOAT, dir, (void*)data_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_u32_newf(hal_param_dir_t dir, hal_u32_t * data_addr,
    int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_U32, dir, (void*)data_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}

int hal_param_s32_newf(hal_param_dir_t dir, hal_s32_t * data_addr,
    int comp_id, const char *fmt, ...)
{
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = hal_param_newfv(HAL_S32, dir, (void*)data_addr, comp_id, fmt, ap);
    va_end(ap);
    return ret;
}


/* this is a generic function that does the majority of the work. */

int hal_param_new(const char *name, hal_type_t type, hal_param_dir_t dir, void *data_addr,
    int comp_id)
{
    int *prev, next, cmp;
    hal_param_t *new, *ptr;
    hal_comp_t *comp;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_new called before init\n");
	return -EINVAL;
    }

    if (type != HAL_BIT && type != HAL_FLOAT && type != HAL_S32 && type != HAL_U32) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: pin type not one of HAL_BIT, HAL_FLOAT, HAL_S32 or HAL_U32\n");
	return -EINVAL;
    }

    if(dir != HAL_RO && dir != HAL_RW) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param direction not one of HAL_RO, or HAL_RW\n");
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: parameter name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_new called while HAL locked\n");
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: creating parameter '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* validate comp_id */
    comp = halpr_find_comp_by_id(comp_id);
    if (comp == 0) {
	/* bad comp_id */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }
    /* validate passed in pointer - must point to HAL shmem */
    if (! SHMCHK(data_addr)) {
	/* bad pointer */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: data_addr not in shared memory\n");
	return -EINVAL;
    }
    if(comp->state > COMP_INITIALIZING) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_new called after hal_ready\n");
	return -EINVAL;
    }
    /* allocate a new parameter structure */
    new = alloc_param_struct();
    if (new == 0) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for parameter '%s'\n", name);
	return -ENOMEM;
    }
    /* initialize the structure */
    new->owner_ptr = SHMOFF(comp);
    new->data_ptr = SHMOFF(data_addr);
    new->type = type;
    new->dir = dir;
    rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
    /* search list for 'name' and insert new structure */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	ptr = SHMPTR(next);
	cmp = strcmp(ptr->name, new->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	if (cmp == 0) {
	    /* name already in list, can't insert */
	    free_param_struct(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: duplicate parameter '%s'\n", name);
	    return -EINVAL;
	}
	/* didn't find it yet, look at next one */
	prev = &(ptr->next_ptr);
	next = *prev;
    }
}

/* wrapper functs for typed params - these call the generic funct below */

int hal_param_bit_set(const char *name, int value)
{
    return hal_param_set(name, HAL_BIT, &value);
}

int hal_param_float_set(const char *name, double value)
{
    return hal_param_set(name, HAL_FLOAT, &value);
}

int hal_param_u32_set(const char *name, unsigned long value)
{
    return hal_param_set(name, HAL_U32, &value);
}

int hal_param_s32_set(const char *name, signed long value)
{
    return hal_param_set(name, HAL_S32, &value);
}

/* this is a generic function that does the majority of the work */

int hal_param_set(const char *name, hal_type_t type, void *value_addr)
{
    hal_param_t *param;
    void *d_ptr;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_set called before init\n");
	return -EINVAL;
    }
    
    if (hal_data->lock & HAL_LOCK_PARAMS)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_set called while HAL locked\n");
	return -EPERM;
    }
    
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: setting parameter '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));

    /* search param list for name */
    param = halpr_find_param_by_name(name);
    if (param == 0) {
	/* parameter not found */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: parameter '%s' not found\n", name);
	return -EINVAL;
    }
    /* found it, is type compatible? */
    if (param->type != type) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: type mismatch setting param '%s'\n", name);
	return -EINVAL;
    }
    /* is it read only? */
    if (param->dir == HAL_RO) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param '%s' is not writable\n", name);
	return -EINVAL;
    }
    /* everything is OK, set the value */
    d_ptr = SHMPTR(param->data_ptr);
    switch (param->type) {
    case HAL_BIT:
	if (*((int *) value_addr) == 0) {
	    *(hal_bit_t *) (d_ptr) = 0;
	} else {
	    *(hal_bit_t *) (d_ptr) = 1;
	}
	break;
    case HAL_FLOAT:
	*((hal_float_t *) (d_ptr)) = *((double *) (value_addr));
	break;
    case HAL_S32:
	*((hal_s32_t *) (d_ptr)) = *((signed long *) (value_addr));
	break;
    case HAL_U32:
	*((hal_u32_t *) (d_ptr)) = *((unsigned long *) (value_addr));
	break;
    default:
	/* Shouldn't get here, but just in case... */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: bad type %d setting param\n", param->type);
	return -EINVAL;
    }
    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}

int hal_param_alias(const char *param_name, const char *alias)
{
    int *prev, next, cmp;
    hal_param_t *param, *ptr;
    hal_oldname_t *oldname;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_alias called before init\n");
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: param_alias called while HAL locked\n");
	return -EPERM;
    }
    if (alias != NULL ) {
	if (strlen(alias) > HAL_NAME_LEN) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
	        "HAL: ERROR: alias name '%s' is too long\n", alias);
	    return -EINVAL;
	}
    }
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    if (alias != NULL ) {
	param = halpr_find_param_by_name(alias);
	if ( param != NULL ) {
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
	        "HAL: ERROR: duplicate pin/alias name '%s'\n", alias);
	    return -EINVAL;
	}
    }
    /* once we unlink the param from the list, we don't want to have to
       abort the change and repair things.  So we allocate an oldname
       struct here, then free it (which puts it on the free list).  This
       allocation might fail, in which case we abort the command.  But
       if we actually need the struct later, the next alloc is guaranteed
       to succeed since at least one struct is on the free list. */
    oldname = halpr_alloc_oldname_struct();
    if ( oldname == NULL ) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for param_alias\n");
	return -EINVAL;
    }
    free_oldname_struct(oldname);
    /* find the param and unlink it from pin list */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, not found */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: param '%s' not found\n", param_name);
	    return -EINVAL;
	}
	param = SHMPTR(next);
	if ( strcmp(param->name, param_name) == 0 ) {
	    /* found it, unlink from list */
	    *prev = param->next_ptr;
	    break;
	}
	if (param->oldname != 0 ) {
	    oldname = SHMPTR(param->oldname);
	    if (strcmp(oldname->name, param_name) == 0) {
		/* found it, unlink from list */
		*prev = param->next_ptr;
		break;
	    }
	}
	/* didn't find it yet, look at next one */
	prev = &(param->next_ptr);
	next = *prev;
    }
    if ( alias != NULL ) {
	/* adding a new alias */
	if ( param->oldname == 0 ) {
	    /* save old name (only if not already saved) */
	    oldname = halpr_alloc_oldname_struct();
	    param->oldname = SHMOFF(oldname);
	    rtapi_snprintf(oldname->name, sizeof(oldname->name), "%s", param->name);
	}
	/* change param's name to 'alias' */
	rtapi_snprintf(param->name, sizeof(param->name), "%s", alias);
    } else {
	/* removing an alias */
	if ( param->oldname != 0 ) {
	    /* restore old name (only if param is aliased) */
	    oldname = SHMPTR(param->oldname);
	    rtapi_snprintf(param->name, sizeof(param->name), "%s", oldname->name);
	    param->oldname = 0;
	    free_oldname_struct(oldname);
	}
    }
    /* insert param back into list in proper place */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    param->next_ptr = next;
	    *prev = SHMOFF(param);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	ptr = SHMPTR(next);
	cmp = strcmp(ptr->name, param->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    param->next_ptr = next;
	    *prev = SHMOFF(param);
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* didn't find it yet, look at next one */
	prev = &(ptr->next_ptr);
	next = *prev;
    }
}

/***********************************************************************
*                   EXECUTION RELATED FUNCTIONS                        *
************************************************************************/

#ifdef RTAPI

int hal_export_funct(const char *name, void (*funct) (void *, long),
    void *arg, int uses_fp, int reentrant, int comp_id)
{
    int *prev, next, cmp;
    hal_funct_t *new, *fptr;
    hal_comp_t *comp;
    char buf[HAL_NAME_LEN + 1];

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: export_funct called before init\n");
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: export_funct called while HAL locked\n");
	return -EPERM;
    }
    
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: exporting function '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* validate comp_id */
    comp = halpr_find_comp_by_id(comp_id);
    if (comp == 0) {
	/* bad comp_id */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d not found\n", comp_id);
	return -EINVAL;
    }
    if (comp->type == TYPE_USER) {
	/* not a realtime component */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: component %d is not realtime (%d)\n",
			comp_id, comp->type);
	return -EINVAL;
    }
    if(comp->state > COMP_INITIALIZING) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: export_funct called after hal_ready\n");
	return -EINVAL;
    }
    /* allocate a new function structure */
    new = alloc_funct_struct();
    if (new == 0) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for function '%s'\n", name);
	return -ENOMEM;
    }
    /* initialize the structure */
    new->uses_fp = uses_fp;
    new->owner_ptr = SHMOFF(comp);
    new->reentrant = reentrant;
    new->users = 0;
    new->arg = arg;
    new->funct = funct;
    rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
    /* search list for 'name' and insert new structure */
    prev = &(hal_data->funct_list_ptr);
    next = *prev;
    while (1) {
	if (next == 0) {
	    /* reached end of list, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    /* break out of loop and init the new function */
	    break;
	}
	fptr = SHMPTR(next);
	cmp = strcmp(fptr->name, new->name);
	if (cmp > 0) {
	    /* found the right place for it, insert here */
	    new->next_ptr = next;
	    *prev = SHMOFF(new);
	    /* break out of loop and init the new function */
	    break;
	}
	if (cmp == 0) {
	    /* name already in list, can't insert */
	    free_funct_struct(new);
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: duplicate function '%s'\n", name);
	    return -EINVAL;
	}
	/* didn't find it yet, look at next one */
	prev = &(fptr->next_ptr);
	next = *prev;
    }
    /* at this point we have a new function and can yield the mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    /* init time logging variables */
    new->runtime = 0;
    new->maxtime = 0;
    /* note that failure to successfully create the following params
       does not cause the "export_funct()" call to fail - they are
       for debugging and testing use only */
    /* create a parameter with the function's runtime in it */
    rtapi_snprintf(buf, sizeof(buf), "%s.time", name);
    hal_param_s32_new(buf, HAL_RO, &(new->runtime), comp_id);
    /* create a parameter with the function's maximum runtime in it */
    rtapi_snprintf(buf, sizeof(buf), "%s.tmax", name);
    hal_param_s32_new(buf, HAL_RW, &(new->maxtime), comp_id);
    return 0;
}

int hal_create_thread(const char *name, unsigned long period_nsec, int uses_fp, int cpu_id)
{
    int next, cmp, prev_priority;
    int retval, n;
    hal_thread_t *new, *tptr;
    long prev_period, curr_period;
/*! \todo Another #if 0 */
#if 0
    char buf[HAL_NAME_LEN + 1];
#endif

    rtapi_print_msg(RTAPI_MSG_DBG,
	"HAL: creating thread %s, %ld nsec\n", name, period_nsec);
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: create_thread called before init\n");
	return -EINVAL;
    }
    if (period_nsec == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: create_thread called with period of zero\n");
	return -EINVAL;
    }

    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: thread name '%s' is too long\n", name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: create_thread called while HAL is locked\n");
	return -EPERM;
    }

    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* make sure name is unique on thread list */
    next = hal_data->thread_list_ptr;
    while (next != 0) {
	tptr = SHMPTR(next);
	cmp = strcmp(tptr->name, name);
	if (cmp == 0) {
	    /* name already in list, can't insert */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: duplicate thread name %s\n", name);
	    return -EINVAL;
	}
	/* didn't find it yet, look at next one */
	next = tptr->next_ptr;
    }
    /* allocate a new thread structure */
    new = alloc_thread_struct();
    if (new == 0) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory to create thread\n");
	return -ENOMEM;
    }
    /* initialize the structure */
    new->uses_fp = uses_fp;
    new->cpu_id = cpu_id;
    rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
    /* have to create and start a task to run the thread */
    if (hal_data->thread_list_ptr == 0) {
	/* this is the first thread created */
	/* is timer started? if so, what period? */
	curr_period = rtapi_clock_set_period(0);
	if (curr_period == 0) {
	    /* not running, start it */
	    curr_period = rtapi_clock_set_period(period_nsec);
	    if (curr_period < 0) {
		rtapi_mutex_give(&(hal_data->mutex));
		rtapi_print_msg(RTAPI_MSG_ERR,
		    "HAL_LIB: ERROR: clock_set_period returned %ld\n",
		    curr_period);
		return -EINVAL;
	    }
	}
	/* make sure period <= desired period (allow 1% roundoff error) */
	if (curr_period > (period_nsec + (period_nsec / 100))) {
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL_LIB: ERROR: clock period too long: %ld\n", curr_period);
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
	tptr = SHMPTR(hal_data->thread_list_ptr);
	prev_period = tptr->period;
	prev_priority = tptr->priority;
    }
    if ( period_nsec < hal_data->base_period) { 
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL_LIB: ERROR: new thread period %ld is less than clock period %ld\n",
	     period_nsec, hal_data->base_period);
	return -EINVAL;
    }
    /* make period an integer multiple of the timer period */
    n = (period_nsec + hal_data->base_period / 2) / hal_data->base_period;
    new->period = hal_data->base_period * n;
    if ( new->period < prev_period ) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL_LIB: ERROR: new thread period %ld is less than existing thread period %ld\n",
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
			    new->name, new->cpu_id);
    if (retval < 0) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL_LIB: could not create task for thread %s\n", name);
	return -EINVAL;
    }
    new->task_id = retval;
    /* start task */
    retval = rtapi_task_start(new->task_id, new->period);
    if (retval < 0) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL_LIB: could not start task for thread %s: %d\n", name, retval);
	return -EINVAL;
    }
    /* insert new structure at head of list */
    new->next_ptr = hal_data->thread_list_ptr;
    hal_data->thread_list_ptr = SHMOFF(new);
    /* done, release mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    /* init time logging variables */
    new->runtime = 0;
    new->maxtime = 0;
/*! \todo Another #if 0 */
#if 0
/* These params need to be re-visited when I refactor HAL.  Right
   now they cause problems - they can no longer be owned by the calling
   component, and they can't be owned by the hal_lib because it isn't
   actually a component.
*/
    /* create a parameter with the thread's runtime in it */
    rtapi_snprintf(buf, sizeof(buf), "%s.time", name);
    hal_param_s32_new(buf, HAL_RO, &(new->runtime), lib_module_id);
    /* create a parameter with the thread's maximum runtime in it */
    rtapi_snprintf(buf, sizeof(buf), "%s.tmax", name);
    hal_param_s32_new(buf, HAL_RW, &(new->maxtime), lib_module_id);
#endif
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: thread %s created prio=%d\n", name, new->priority);
    return 0;
}

extern int hal_thread_delete(const char *name)
{
    hal_thread_t *thread;
    int *prev, next;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: thread_delete called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: thread_delete called while HAL is locked\n");
	return -EPERM;
    }
    
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: deleting thread '%s'\n", name);
    /* get mutex before accessing shared data */
    rtapi_mutex_get(&(hal_data->mutex));
    /* search for the signal */
    prev = &(hal_data->thread_list_ptr);
    next = *prev;
    while (next != 0) {
	thread = SHMPTR(next);
	if (strcmp(thread->name, name) == 0) {
	    /* this is the right thread, unlink from list */
	    *prev = thread->next_ptr;
	    /* and delete it */
	    free_thread_struct(thread);
	    /* done */
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* no match, try the next one */
	prev = &(thread->next_ptr);
	next = *prev;
    }
    /* if we get here, we didn't find a match */
    rtapi_mutex_give(&(hal_data->mutex));
    rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: thread '%s' not found\n",
	name);
    return -EINVAL;
}

#endif /* RTAPI */

int hal_add_funct_to_thread(const char *funct_name, const char *thread_name, int position)
{
    hal_thread_t *thread;
    hal_funct_t *funct;
    hal_list_t *list_root, *list_entry;
    int n;
    hal_funct_entry_t *funct_entry;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: add_funct called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: add_funct_to_thread called while HAL is locked\n");
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG,
	"HAL: adding function '%s' to thread '%s'\n",
	funct_name, thread_name);
    /* get mutex before accessing data structures */
    rtapi_mutex_get(&(hal_data->mutex));
    /* make sure position is valid */
    if (position == 0) {
	/* zero is not allowed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: bad position: 0\n");
	return -EINVAL;
    }
    /* make sure we were given a function name */
    if (funct_name == 0) {
	/* no name supplied */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: missing function name\n");
	return -EINVAL;
    }
    /* make sure we were given a thread name */
    if (thread_name == 0) {
	/* no name supplied */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: missing thread name\n");
	return -EINVAL;
    }
    /* search function list for the function */
    funct = halpr_find_funct_by_name(funct_name);
    if (funct == 0) {
	/* function not found */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function '%s' not found\n", funct_name);
	return -EINVAL;
    }
    /* found the function, is it available? */
    if ((funct->users > 0) && (funct->reentrant == 0)) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function '%s' may only be added to one thread\n", funct_name);
	return -EINVAL;
    }
    /* search thread list for thread_name */
    thread = halpr_find_thread_by_name(thread_name);
    if (thread == 0) {
	/* thread not found */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: thread '%s' not found\n", thread_name);
	return -EINVAL;
    }
    /* ok, we have thread and function, are they compatible? */
    if ((funct->uses_fp) && (!thread->uses_fp)) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function '%s' needs FP\n", funct_name);
	return -EINVAL;
    }
    /* find insertion point */
    list_root = &(thread->funct_list);
    list_entry = list_root;
    n = 0;
    if (position > 0) {
	/* insertion is relative to start of list */
	while (++n < position) {
	    /* move further into list */
	    list_entry = list_next(list_entry);
	    if (list_entry == list_root) {
		/* reached end of list */
		rtapi_mutex_give(&(hal_data->mutex));
		rtapi_print_msg(RTAPI_MSG_ERR,
		    "HAL: ERROR: position '%d' is too high\n", position);
		return -EINVAL;
	    }
	}
    } else {
	/* insertion is relative to end of list */
	while (--n > position) {
	    /* move further into list */
	    list_entry = list_prev(list_entry);
	    if (list_entry == list_root) {
		/* reached end of list */
		rtapi_mutex_give(&(hal_data->mutex));
		rtapi_print_msg(RTAPI_MSG_ERR,
		    "HAL: ERROR: position '%d' is too low\n", position);
		return -EINVAL;
	    }
	}
	/* want to insert before list_entry, so back up one more step */
	list_entry = list_prev(list_entry);
    }
    /* allocate a funct entry structure */
    funct_entry = alloc_funct_entry_struct();
    if (funct_entry == 0) {
	/* alloc failed */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: insufficient memory for thread->function link\n");
	return -ENOMEM;
    }
    /* init struct contents */
    funct_entry->funct_ptr = SHMOFF(funct);
    funct_entry->arg = funct->arg;
    funct_entry->funct = funct->funct;
    /* add the entry to the list */
    list_add_after((hal_list_t *) funct_entry, list_entry);
    /* update the function usage count */
    funct->users++;
    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}

int hal_del_funct_from_thread(const char *funct_name, const char *thread_name)
{
    hal_thread_t *thread;
    hal_funct_t *funct;
    hal_list_t *list_root, *list_entry;
    hal_funct_entry_t *funct_entry;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: del_funct called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: del_funct_from_thread called while HAL is locked\n");
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG,
	"HAL: removing function '%s' from thread '%s'\n",
	funct_name, thread_name);
    /* get mutex before accessing data structures */
    rtapi_mutex_get(&(hal_data->mutex));
    /* make sure we were given a function name */
    if (funct_name == 0) {
	/* no name supplied */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: missing function name\n");
	return -EINVAL;
    }
    /* make sure we were given a thread name */
    if (thread_name == 0) {
	/* no name supplied */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL: ERROR: missing thread name\n");
	return -EINVAL;
    }
    /* search function list for the function */
    funct = halpr_find_funct_by_name(funct_name);
    if (funct == 0) {
	/* function not found */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function '%s' not found\n", funct_name);
	return -EINVAL;
    }
    /* found the function, is it in use? */
    if (funct->users == 0) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: function '%s' is not in use\n", funct_name);
	return -EINVAL;
    }
    /* search thread list for thread_name */
    thread = halpr_find_thread_by_name(thread_name);
    if (thread == 0) {
	/* thread not found */
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: thread '%s' not found\n", thread_name);
	return -EINVAL;
    }
    /* ok, we have thread and function, does thread use funct? */
    list_root = &(thread->funct_list);
    list_entry = list_next(list_root);
    while (1) {
	if (list_entry == list_root) {
	    /* reached end of list, funct not found */
	    rtapi_mutex_give(&(hal_data->mutex));
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: thread '%s' doesn't use %s\n", thread_name,
		funct_name);
	    return -EINVAL;
	}
	funct_entry = (hal_funct_entry_t *) list_entry;
	if (SHMPTR(funct_entry->funct_ptr) == funct) {
	    /* this funct entry points to our funct, unlink */
	    list_remove_entry(list_entry);
	    /* and delete it */
	    free_funct_entry_struct(funct_entry);
	    /* done */
	    rtapi_mutex_give(&(hal_data->mutex));
	    return 0;
	}
	/* try next one */
	list_entry = list_next(list_entry);
    }
}

int hal_start_threads(void)
{
    /* a trivial function for a change! */
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: start_threads called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_RUN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: start_threads called while HAL is locked\n");
	return -EPERM;
    }


    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: starting threads\n");
    hal_data->threads_running = 1;
    return 0;
}

int hal_stop_threads(void)
{
    /* wow, two in a row! */
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: stop_threads called before init\n");
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_RUN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: stop_threads called while HAL is locked\n");
	return -EPERM;
    }

    hal_data->threads_running = 0;
    rtapi_print_msg(RTAPI_MSG_DBG, "HAL: threads stopped\n");
    return 0;
}

/***********************************************************************
*                    PRIVATE FUNCTION CODE                             *
************************************************************************/

hal_list_t *list_prev(hal_list_t * entry)
{
    /* this function is only needed because of memory mapping */
    return SHMPTR(entry->prev);
}

hal_list_t *list_next(hal_list_t * entry)
{
    /* this function is only needed because of memory mapping */
    return SHMPTR(entry->next);
}

void list_init_entry(hal_list_t * entry)
{
    int entry_n;

    entry_n = SHMOFF(entry);
    entry->next = entry_n;
    entry->prev = entry_n;
}

void list_add_after(hal_list_t * entry, hal_list_t * prev)
{
    int entry_n, prev_n, next_n;
    hal_list_t *next;

    /* messiness needed because of memory mapping */
    entry_n = SHMOFF(entry);
    prev_n = SHMOFF(prev);
    next_n = prev->next;
    next = SHMPTR(next_n);
    /* insert the entry */
    entry->next = next_n;
    entry->prev = prev_n;
    prev->next = entry_n;
    next->prev = entry_n;
}

void list_add_before(hal_list_t * entry, hal_list_t * next)
{
    int entry_n, prev_n, next_n;
    hal_list_t *prev;

    /* messiness needed because of memory mapping */
    entry_n = SHMOFF(entry);
    next_n = SHMOFF(next);
    prev_n = next->prev;
    prev = SHMPTR(prev_n);
    /* insert the entry */
    entry->next = next_n;
    entry->prev = prev_n;
    prev->next = entry_n;
    next->prev = entry_n;
}

hal_list_t *list_remove_entry(hal_list_t * entry)
{
    int entry_n;
    hal_list_t *prev, *next;

    /* messiness needed because of memory mapping */
    entry_n = SHMOFF(entry);
    prev = SHMPTR(entry->prev);
    next = SHMPTR(entry->next);
    /* remove the entry */
    prev->next = entry->next;
    next->prev = entry->prev;
    entry->next = entry_n;
    entry->prev = entry_n;
    return next;
}

hal_comp_t *halpr_find_comp_by_name(const char *name)
{
    int next;
    hal_comp_t *comp;

    /* search component list for 'name' */
    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = SHMPTR(next);
	if (strcmp(comp->name, name) == 0) {
	    /* found a match */
	    return comp;
	}
	/* didn't find it yet, look at next one */
	next = comp->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_pin_t *halpr_remote_find_pin_by_name(const hal_data_t *hal_data, const char *name)
{
    int next;
    hal_pin_t *pin;
    hal_oldname_t *oldname;

    /* search pin list for 'name' */
    next = hal_data->pin_list_ptr;
    while (next != 0) {
	pin = SHMPTR(next);
	if (strcmp(pin->name, name) == 0) {
	    /* found a match */
	    return pin;
	}
	if (pin->oldname != 0 ) {
	    oldname = SHMPTR(pin->oldname);
	    if (strcmp(oldname->name, name) == 0) {
		/* found a match */
		return pin;
	    }
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_pin_t *halpr_find_pin_by_name(const char *name)
{
    return halpr_remote_find_pin_by_name(hal_data,name);
}

hal_sig_t *halpr_remote_find_sig_by_name(const hal_data_t *hal_data, const char *name)
{
    int next;
    hal_sig_t *sig;

    /* search signal list for 'name' */
    next = hal_data->sig_list_ptr;
    while (next != 0) {
	sig = SHMPTR_IN(hal_data, next);
	if (strcmp(sig->name, name) == 0) {
	    /* found a match */
	    return sig;
	}
	/* didn't find it yet, look at next one */
	next = sig->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_sig_t *halpr_find_sig_by_name(const char *name)
{
    return halpr_remote_find_sig_by_name(hal_data, name);
}

hal_param_t *halpr_find_param_by_name(const char *name)
{
    int next;
    hal_param_t *param;
    hal_oldname_t *oldname;

    /* search parameter list for 'name' */
    next = hal_data->param_list_ptr;
    while (next != 0) {
	param = SHMPTR(next);
	if (strcmp(param->name, name) == 0) {
	    /* found a match */
	    return param;
	}
	if (param->oldname != 0 ) {
	    oldname = SHMPTR(param->oldname);
	    if (strcmp(oldname->name, name) == 0) {
		/* found a match */
		return param;
	    }
	}
	/* didn't find it yet, look at next one */
	next = param->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_thread_t *halpr_find_thread_by_name(const char *name)
{
    int next;
    hal_thread_t *thread;

    /* search thread list for 'name' */
    next = hal_data->thread_list_ptr;
    while (next != 0) {
	thread = SHMPTR(next);
	if (strcmp(thread->name, name) == 0) {
	    /* found a match */
	    return thread;
	}
	/* didn't find it yet, look at next one */
	next = thread->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_funct_t *halpr_find_funct_by_name(const char *name)
{
    int next;
    hal_funct_t *funct;

    /* search function list for 'name' */
    next = hal_data->funct_list_ptr;
    while (next != 0) {
	funct = SHMPTR(next);
	if (strcmp(funct->name, name) == 0) {
	    /* found a match */
	    return funct;
	}
	/* didn't find it yet, look at next one */
	next = funct->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

hal_comp_t *halpr_find_comp_by_id(int id)
{
    int next;
    hal_comp_t *comp;

    /* search list for 'comp_id' */
    next = hal_data->comp_list_ptr;
    while (next != 0) {
	comp = SHMPTR(next);
	if (comp->comp_id == id) {
	    /* found a match */
	    return comp;
	}
	/* didn't find it yet, look at next one */
	next = comp->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_pin_t *halpr_find_pin_by_owner(hal_comp_t * owner, hal_pin_t * start)
{
    int owner_ptr, next;
    hal_pin_t *pin;

    /* get offset of 'owner' component */
    owner_ptr = SHMOFF(owner);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of pin list */
	next = hal_data->pin_list_ptr;
    } else {
	/* no, start at next pin */
	next = start->next_ptr;
    }
    while (next != 0) {
	pin = SHMPTR(next);
	if (pin->owner_ptr == owner_ptr) {
	    /* found a match */
	    return pin;
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_param_t *halpr_find_param_by_owner(hal_comp_t * owner,
    hal_param_t * start)
{
    int owner_ptr, next;
    hal_param_t *param;

    /* get offset of 'owner' component */
    owner_ptr = SHMOFF(owner);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of param list */
	next = hal_data->param_list_ptr;
    } else {
	/* no, start at next param */
	next = start->next_ptr;
    }
    while (next != 0) {
	param = SHMPTR(next);
	if (param->owner_ptr == owner_ptr) {
	    /* found a match */
	    return param;
	}
	/* didn't find it yet, look at next one */
	next = param->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_funct_t *halpr_find_funct_by_owner(hal_comp_t * owner,
    hal_funct_t * start)
{
    int owner_ptr, next;
    hal_funct_t *funct;

    /* get offset of 'owner' component */
    owner_ptr = SHMOFF(owner);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of function list */
	next = hal_data->funct_list_ptr;
    } else {
	/* no, start at next function */
	next = start->next_ptr;
    }
    while (next != 0) {
	funct = SHMPTR(next);
	if (funct->owner_ptr == owner_ptr) {
	    /* found a match */
	    return funct;
	}
	/* didn't find it yet, look at next one */
	next = funct->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}

hal_pin_t *halpr_remote_find_pin_by_sig(hal_data_t *hal_data,
					hal_sig_t * sig, hal_pin_t * start)
{
    int sig_ptr, next;
    hal_pin_t *pin;

    /* get offset of 'sig' component */
    sig_ptr = SHMOFF(sig);
    /* is this the first call? */
    if (start == 0) {
	/* yes, start at beginning of pin list */
	next = hal_data->pin_list_ptr;
    } else {
	/* no, start at next pin */
	next = start->next_ptr;
    }
    while (next != 0) {
	pin = SHMPTR_IN(hal_data, next);
	if (pin->signal == sig_ptr) {
	    /* found a match */
	    return pin;
	}
	/* didn't find it yet, look at next one */
	next = pin->next_ptr;
    }
    /* if loop terminates, we reached end of list without finding a match */
    return 0;
}
hal_pin_t *halpr_find_pin_by_sig(hal_sig_t * sig, hal_pin_t * start)
{
    return halpr_remote_find_pin_by_sig(hal_data, sig, start);
}

// ----------------- Group support code ------------
int hal_group_new(const char *name, int arg1, int arg2)
{
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_new called before init\n"
			, rtapi_instance);
	return -EINVAL;
    }
    if (!name) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_new() called with NULL name\n",
			rtapi_instance);
    }
    if (strlen(name) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group name '%s' is too long\n",
			rtapi_instance, name);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_new called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d creating group '%s'\n",
		    rtapi_instance, name);

    {
	hal_group_t *new, *chan;
	hal_group_t *ptr   __attribute__((cleanup(halpr_autorelease_mutex)));
	int *prev, next, cmp;

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* validate group name */
	chan = halpr_find_group_by_name(name);
	if (chan != 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: group '%s' already defined\n",
			    rtapi_instance, name);
	    return -EINVAL;
	}
	/* allocate a new group structure */
	new = alloc_group_struct();
	if (new == 0) {
	    /* alloc failed */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: insufficient memory for group '%s'\n",
			    rtapi_instance, name);
	    return -ENOMEM;
	}
	/* initialize the structure */
	new->userarg1 = arg1;
	new->userarg2 = arg2;
	rtapi_snprintf(new->name, sizeof(new->name), "%s", name);
	/* search list for 'name' and insert new structure */
	prev = &(hal_data->group_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		new->next_ptr = next;
		*prev = SHMOFF(new);
		return 0;
	    }
	    ptr = SHMPTR(next);
	    cmp = strcmp(ptr->name, new->name);
	    if (cmp > 0) {
		/* found the right place for it, insert here */
		new->next_ptr = next;
		*prev = SHMOFF(new);
		return 0;
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
	return 0;
    }
}

int hal_group_delete(const char *name)
{
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_delete called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (name == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_delete: name is NULL\n",
			rtapi_instance);
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_delete called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d deleting group '%s'\n",
		    rtapi_instance, name);

    // this block is protected by hal_data->mutex with automatic
    // onlock on scope exit
    {
	hal_group_t *group __attribute__((cleanup(halpr_autorelease_mutex)));
	int next,*prev;

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));
	/* search for the group */
	prev = &(hal_data->group_list_ptr);
	next = *prev;
	while (next != 0) {
	    group = SHMPTR(next);
	    if (strcmp(group->name, name) == 0) {
		/* this is the right group */
		// verify it is unreferenced, and fail if not:
		if (group->refcount) {
		    rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: cannot delete group '%s' (still used: %d)\n",
				    rtapi_instance, name, group->refcount);
		    return -EBUSY;
		}
		/* unlink from list */
		*prev = group->next_ptr;
		/* and delete it, linking it on the free list */
		//NB: freeing member list is done in free_group_struct
		free_group_struct(group);
		/* done */
		return 0;
	    }
	    /* no match, try the next one */
	    prev = &(group->next_ptr);
	    next = *prev;
	}
	/* if we get here, we didn't find a match */
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: group_delete: no such group '%s'\n",
			rtapi_instance, name);
	return -EINVAL;
    }
}

int hal_ref_group(const char *name)
{
    hal_group_t *group __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));

    group = halpr_find_group_by_name(name);
    if (group == NULL)
	return -ENOENT;
    group->refcount += 1;
    return 0;
}

int hal_unref_group(const char *name)
{
    hal_group_t *group __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));

    group = halpr_find_group_by_name(name);
    if (group == NULL)
	return -ENOENT;
    group->refcount -= 1;
    return 0;
}

// does NOT lock hal_data!
int halpr_foreach_group(const char *groupname,
			hal_group_callback_t callback, void *cb_data)
{
    hal_group_t *group;
    int next;
    int nvisited = 0;
    int result;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_group called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_group called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }
#if 0
    if (groupname == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_group(): name is NULL\n",
			rtapi_instance);
	return -EINVAL;
    }
#endif
    /* search for the group */
    next = hal_data->group_list_ptr;
    while (next != 0) {
	group = SHMPTR(next);
	if (!groupname || (strcmp(group->name, groupname)) == 0) {
	    nvisited++;
	    /* this is the right group */
	    if (callback) {
		result = callback(group, cb_data);
		if (result < 0) {
		    // callback signaled an error, pass that back up.
		    return result;
		} else if (result > 0) {
		    // callback signaled 'stop iterating'.
		    // pass back the number of visited groups.
		    return nvisited;
		} else {
		    // callback signaled 'OK to continue'
		    // fall through
		}
	    } else {
		// null callback passed in,
		// just count groups
		// nvisited already bumped above.
	    }
	}
	/* no match, try the next one */
	next = group->next_ptr;
    }
    /* if we get here, we ran through all the groups, so return count */
    return nvisited;
}

// run through member list, recursively expanding nested groups depth-first
static int resolve_members( int *nvisited, int level, hal_group_t **groups,
			   hal_member_callback_t callback, void *cb_data,
			   int flags)
{
    hal_member_t *member;
    hal_group_t *nestedgroup;
    int result = 0, mptr = groups[level]->member_ptr;

    while (mptr) {
	member = SHMPTR(mptr);
	if (member->sig_member_ptr) {
	    (*nvisited)++;
	    if (callback)
		result = callback(level, groups, member, cb_data);
	} else if (member->group_member_ptr) {
	    if (flags & RESOLVE_NESTED_GROUPS) {
		if (level >= MAX_NESTED_GROUPS) {
		    // dump stack & bail
		    rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: maximum group nesting exceeded for '%s'\n",
				    rtapi_instance, groups[0]->name);
		    while (level) {
			rtapi_print_msg(RTAPI_MSG_ERR,
					"\t%d: %s\n",
					level, groups[level]->name);
			level--;
		    }
		    return -EINVAL;
		}
		nestedgroup = SHMPTR(member->group_member_ptr);
		groups[level+1] = nestedgroup;
		result = resolve_members(nvisited, level+1, groups,
					 callback, cb_data, flags);
	    } else {
		if (callback)
		    result = callback(level, groups, member, cb_data);
		(*nvisited)++;
	    }
	}
	if (result < 0) {
	    // callback signaled an error, pass that back up.
	    return result;
	} else if (result > 0) {
	    // callback signaled 'stop iterating'.
	    // pass back the number of visited members.
	    return *nvisited;
	} else {
	    // callback signaled 'OK to continue'
	    //fall through
	}
	mptr = member->next_ptr;
    }
    return result;
}

// does NOT lock hal_data!
int halpr_foreach_member(const char *groupname,
			 hal_member_callback_t callback, void *cb_data, int flags)
{
    int nvisited = 0;
    int level = 0;
    int result;
    int next;
    hal_group_t *groupstack[MAX_NESTED_GROUPS+1], *grp;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_member called before init\n",
			rtapi_instance);
	return -EINVAL;
    }

    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_member called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    if (groupname == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: halpr_foreach_member(): name is NULL\n",
			rtapi_instance);
	return -EINVAL;
    }

    /* search for the group */
    next = hal_data->group_list_ptr;
    while (next != 0) {
	grp = SHMPTR(next);
	groupstack[0] = grp;

	if (!groupname ||
	    (strcmp(grp->name, groupname)) == 0) {
	    // go through members
	    result = resolve_members(&nvisited, level, groupstack,
				     callback,  cb_data, flags);
	    if (result < 0) {
		// callback signaled an error, pass that back up.
		return result;
	    } else if (result > 0) {
		// callback signaled 'stop iterating'.
		// pass back the number of visited members.
		return nvisited;
	    } else {
		// callback signaled 'OK to continue'
		// fall through
	    }
	} // group match
	/* no match, try the next one */
	next = grp->next_ptr;
    } // forall groups
    /* if we get here, we ran through all the groups, so return count */
    return nvisited;
}

hal_group_t *halpr_find_group_by_name(const char *name)
{
    int next;
    hal_group_t *group;

    /* search group list for 'name' */
    next = hal_data->group_list_ptr;
    while (next != 0) {
	group = SHMPTR(next);
	if (strcmp(group->name, name) == 0) {
	    /* found a match */
	    return group;
	}
	/* didn't find it yet, look at next one */
	next = group->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

static hal_group_t *find_group_of_member(const char *name)
{
    int nextg, nextm;
    hal_group_t *group, *tgrp;
    hal_member_t *member;
    hal_sig_t *sig;

    nextg = hal_data->group_list_ptr;
    while (nextg != 0) {
	group = SHMPTR(nextg);
	nextm = group->member_ptr;
	while (nextm != 0) {
	    member = SHMPTR(nextm);
	    if (member->sig_member_ptr) { // a signal member

		sig = SHMPTR(member->sig_member_ptr);
		if (strcmp(name, sig->name) == 0) {
		    rtapi_print_msg(RTAPI_MSG_DBG,
				    "HAL:%d  find_group_of_member(%s): found signal in group '%s'\n",
				    rtapi_instance, name, group->name);
		    return group;
		}
		nextm = member->next_ptr;
	    }
	    if (member->group_member_ptr) { // a nested group
		tgrp = SHMPTR(member->group_member_ptr);
		if (strcmp(name, tgrp->name) == 0) {
		    rtapi_print_msg(RTAPI_MSG_DBG,
				    "HAL:%d  find_group_of_member(%s): found group in group '%s'\n",
				    rtapi_instance, name, group->name);
		    return group;
		}
		nextm = member->next_ptr;
	    }
	}
	nextg = group->next_ptr;
    }
    return group;
}

int hal_member_new(const char *group, const char *member,
		     int arg1, double epsilon)
{
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (!group) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new() called with NULL group\n",
			rtapi_instance);
    }
    if (!member) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new() called with NULL member\n",
			rtapi_instance);
    }
    if (strlen(member) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member name '%s' is too long\n",
			rtapi_instance, member);
	return -EINVAL;
    }
    if (!strcmp(member, group)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new(): cannot nest group '%s' as member '%s'\n",
			rtapi_instance, group, member);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d creating member '%s'\n",
		    rtapi_instance, member);

    {
	hal_group_t *grp __attribute__((cleanup(halpr_autorelease_mutex)));
	hal_member_t *new, *ptr;
	hal_group_t *mgrp = NULL;
	hal_sig_t *sig = NULL;
	int *prev, next;

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* validate group name */
	grp = halpr_find_group_by_name(group);
	if (!grp) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: member_new(): undefined group '%s'\n",
			    rtapi_instance, group);
	    return -EINVAL;
	}

	// TBD: handle remote signal case
	if ((sig = halpr_find_sig_by_name(member)) != NULL) {
	    rtapi_print_msg(RTAPI_MSG_DBG,"HAL:%d adding signal '%s' to group '%s'\n",
			    rtapi_instance, member, group);
	    goto found;
	}
	if ((mgrp = halpr_find_group_by_name(member)) != NULL) {
	    rtapi_print_msg(RTAPI_MSG_DBG,"HAL:%d adding nested group '%s' to group '%s'\n",
			    rtapi_instance, member, group);
	    goto found;
	}
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_new(): undefined member '%s'\n",
			rtapi_instance, member);
	return -EINVAL;

    found:
	/* allocate a new member structure */
	new = alloc_member_struct();
	if (new == 0) {
	    /* alloc failed */
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: insufficient memory for member '%s'\n",
			    rtapi_instance, member);
	    return -ENOMEM;
	}
	/* initialize the structure */
	new->userarg1 = arg1;
	new->epsilon = epsilon;
	if (sig) {
	    new->sig_member_ptr =  SHMOFF(sig);
	    new->signal_inst = rtapi_instance; // TBD: correct for remote signal
	    new->group_member_ptr = 0;
	} else if (mgrp) {
	    new->group_member_ptr =  SHMOFF(mgrp);
	    new->sig_member_ptr = 0;
	}
	/* insert new structure */
	/* NB: ordering is by insertion sequence */
	prev = &(grp->member_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		new->next_ptr = next;
		*prev = SHMOFF(new);
		return 0;
	    }
	    ptr = SHMPTR(next);

	    if (ptr->sig_member_ptr) {
		sig = SHMPTR(ptr->sig_member_ptr);
		if (strcmp(member, sig->name) == 0) {
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "HAL:%d ERROR: member_new(): group '%s' already has signal member '%s'\n",
				    rtapi_instance, group, sig->name);
		    return -EINVAL;
		}
	    }
	    if (ptr->group_member_ptr) {
		mgrp = SHMPTR(ptr->group_member_ptr);
		if (strcmp(member, mgrp->name) == 0) {
		    rtapi_print_msg(RTAPI_MSG_ERR,
				    "HAL:%d ERROR: member_new(): group '%s' already has group member '%s'\n",
				    rtapi_instance, group, mgrp->name);
		    return -EINVAL;
		}
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
	return 0;
    }
}

int hal_member_delete(const char *group, const char *member)
{
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_delete called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if(!group) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_delete() called with NULL group\n",
			rtapi_instance);
    }
    if(!member) {
        rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_delete() called with NULL member\n",
			rtapi_instance);
    }
    if (strlen(member) > HAL_NAME_LEN) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member name '%s' is too long\n",
			rtapi_instance, member);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_LOAD)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_delete called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }
    {
	hal_group_t *grp __attribute__((cleanup(halpr_autorelease_mutex)));
	hal_member_t  *mptr;
	hal_sig_t *sig;
	int *prev, next;

	/* get mutex before accessing shared data */
	rtapi_mutex_get(&(hal_data->mutex));

	/* validate group name */
	grp = halpr_find_group_by_name(group);
	if (!grp) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: member_new(): undefined group '%s'\n",
			    rtapi_instance, group);
	    return -EINVAL;
	}

	sig = halpr_find_sig_by_name(member);
	if (!sig) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: member_delete(): undefined member '%s'\n",
			    rtapi_instance, member);
	    return -EINVAL;
	}
	rtapi_print_msg(RTAPI_MSG_DBG,"HAL:%d deleting signal '%s' from group '%s'\n",
			rtapi_instance,  member, group);

	/* delete member structure */
	prev = &(grp->member_ptr);
	/* search for the member */
	next = *prev;
	while (next != 0) {
	    mptr = SHMPTR(next);
	    if (strcmp(member, sig->name) == 0) {
		/* this is the right member, unlink from list */
		*prev = mptr->next_ptr;
		/* and delete it */
		free_member_struct(mptr);
		/* done */
		return 0;
	    }
	    /* no match, try the next one */
	    prev = &(mptr->next_ptr);
	    next = *prev;
	}
	// pin or signal did exist but was not a group member */
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: member_delete(): signal '%s' exists but not member of '%s'\n",
			rtapi_instance, member, group);
	return 0;
    }
}

static int cgroup_size_cb(int level, hal_group_t **groups, hal_member_t *member,
			  void *cb_data)
{
    hal_compiled_group_t *tc = cb_data;
    tc->n_members++;
    if ((member->userarg1 & MEMBER_MONITOR_CHANGE) ||
	// the toplevel group flags are relevant, no the nested ones:
	(groups[0]->userarg2 & GROUP_MONITOR_ALL_MEMBERS))
	tc->n_monitored++;
    return 0;
}

static int cgroup_init_members_cb(int level, hal_group_t **groups, hal_member_t *member,
				  void *cb_data)
{
    hal_compiled_group_t *tc = cb_data;
    hal_sig_t *sig;

    sig = SHMPTR(member->sig_member_ptr);
#if 0
    if ((sig->writers + sig->bidirs) == 0)
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d WARNING: group '%s': member signal '%s' has no updater\n",
			rtapi_instance, groups[0]->name, sig->name);
#endif
    tc->member[tc->mbr_index] = member;
    tc->mbr_index++;
    if ((member->userarg1 & MEMBER_MONITOR_CHANGE) ||
	(groups[0]->userarg2 & GROUP_MONITOR_ALL_MEMBERS)) {
	tc->mon_index++;
    }
    return 0;
}

// group generic change detection & reporting support

int hal_group_compile(const char *name, hal_compiled_group_t **cgroup)
{
    int result;
    hal_compiled_group_t *tc;
    hal_group_t *grp __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));

    if ((grp = halpr_find_group_by_name(name)) == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_group_compile(%s): no such group\n",
			rtapi_instance, name);
	return -EINVAL;
    }

    // a compiled group is a userland memory object
    if ((tc = malloc(sizeof(hal_compiled_group_t))) == NULL)
	return -ENOMEM;

    memset(tc, 0, sizeof(hal_compiled_group_t));

    // first pass: determine sizes
    // this fills sets the n_members and n_monitored fields
    result = halpr_foreach_member(name, cgroup_size_cb,
				  tc, RESOLVE_NESTED_GROUPS);
    /* rtapi_print_msg(RTAPI_MSG_DBG, */
    /* 		    "HAL:%d hal_group_compile(%s): %d signals %d monitored\n", */
    /* 		    rtapi_instance, name, tc->n_members, tc->n_monitored ); */
    if ((tc->member =
	 malloc(sizeof(hal_member_t  *) * tc->n_members )) == NULL)
	return -ENOMEM;

    tc->mbr_index = 0;
    tc->mon_index = 0;

    // second pass: fill in references
    result = halpr_foreach_member(name, cgroup_init_members_cb,
				  tc, RESOLVE_NESTED_GROUPS);
    assert(tc->n_monitored == tc->mon_index);
    assert(tc->n_members == tc->mbr_index);

    // this attribute combination doesn make sense - such a group
    // definition will never trigger a report:
    if ((grp->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS)) &&
	(tc->n_monitored  == 0)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
		 "HAL:%d hal_group_compile(%s): changed-monitored group with no members to check\n",
			rtapi_instance, name);
	return -EINVAL;
    }

    // set up value tracking if either the whole group is to be monitored for changes
    // to cause a report, or some members should be included in a periodic
    if ((grp->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS)) ||
	(tc->n_monitored  > 0)) {
	if ((tc->tracking =
	     malloc(sizeof(hal_data_u) * tc->n_monitored )) == NULL)
	    return -ENOMEM;
	memset(tc->tracking, 0, sizeof(hal_data_u) * tc->n_monitored);
	if ((tc->changed =
	     malloc(RTAPI_BITMAP_SIZE(tc->n_members))) == NULL)
	    return -ENOMEM;
	RTAPI_ZERO_BITMAP(tc->changed, tc->n_members);
    } else {
	// nothing to track
	tc->n_monitored = 0;
	tc->tracking = NULL;
	tc->changed = NULL;
    }

    tc->magic = CGROUP_MAGIC;
    tc->group = grp;
    grp->refcount++;
    *cgroup = tc;

    return 0;
}

int hal_cgroup_apply(hal_compiled_group_t *cg, int handle, hal_type_t type, hal_data_u value)
{
    hal_data_t *sig_hal_data;
    hal_sig_t *sig;
    hal_data_u *dp;
    hal_member_t *member;
    hal_context_t *ctx = THIS_CONTEXT();

    assert(cg->magic ==  CGROUP_MAGIC);
    // handles (value references) run from 0..cc->n_pin-1
    if ((handle < 0) || (handle > cg->n_members-1))
	return -ERANGE;

    member = cg->member[handle];
    sig_hal_data = ctx->namespaces[member->signal_inst].haldata;
    sig = SHMPTR_IN(sig_hal_data, member->sig_member_ptr);
    dp = SHMPTR_IN(sig_hal_data, sig->data_ptr);

    if (sig->writers > 0)
	rtapi_print_msg(RTAPI_MSG_WARN,
			"HAL:%d WARNING: group '%s': member signal '%s' already has updater\n",
			rtapi_instance, cg->group->name, sig->name);

    switch (type) {
    case HAL_TYPE_UNSPECIFIED:
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: hal_cgroup_apply(): invalid HAL type %d\n",
			rtapi_instance, type);
	return -EINVAL;
    case HAL_BIT:
	dp->b = value.b;
	break;
    case HAL_FLOAT:
	dp->f = value.f;
	break;
    case HAL_S32:
	dp->s = value.s;
	break;
    case HAL_U32:
	dp->u = value.u;
	break;
    }
    return 0;
}


int hal_cgroup_match(hal_compiled_group_t *cg)
{
    int i, monitor, nchanged = 0, m = 0;
    hal_sig_t *sig;
    hal_bit_t halbit;
    hal_s32_t hals32;
    hal_s32_t halu32;
    hal_float_t halfloat,delta;

    assert(cg->magic ==  CGROUP_MAGIC);

    // scan for changes if needed
    monitor = (cg->group->userarg2 & (GROUP_REPORT_ON_CHANGE|GROUP_REPORT_CHANGED_MEMBERS))
	|| (cg->n_monitored > 0);

    // walk the group if either the whole group is to be monitored for changes
    // to cause a report, or only changed members should be included in a periodic
    // report.
    if (monitor) {
	RTAPI_ZERO_BITMAP(cg->changed, cg->n_members);
	for (i = 0; i < cg->n_members; i++) {
	    sig = SHMPTR(cg->member[i]->sig_member_ptr);
	    /* if (!cg->member[i]->userarg1 &  MEMBER_MONITOR_CHANGE) */
	    /* 	continue; */
	    switch (sig->type) {
	    case HAL_BIT:
		halbit = *((char *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].b != halbit) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].b = halbit;
		}
		break;
	    case HAL_FLOAT:
		halfloat = *((hal_float_t *) SHMPTR(sig->data_ptr));
		delta = FABS(halfloat - cg->tracking[m].f);
		if (delta > cg->member[i]->epsilon) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].f = halfloat;
		}
		break;
	    case HAL_S32:
		hals32 =  *((hal_s32_t *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].s != hals32) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].s = hals32;
		}
		break;
	    case HAL_U32:
		halu32 =  *((hal_u32_t *) SHMPTR(sig->data_ptr));
		if (cg->tracking[m].u != halu32) {
		    nchanged++;
		    RTAPI_BIT_SET(cg->changed, i);
		    cg->tracking[m].u = halu32;
		}
		break;
	    default:
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL:%d BUG: detect_changes(%s): invalid type for signal %s: %d\n",
				rtapi_instance, cg->group->name, sig->name, sig->type);
		return -EINVAL;
	    }
	    m++;
	}
	return nchanged;
    } else
	return 1;
}


int hal_cgroup_report(hal_compiled_group_t *cg, group_report_callback_t report_cb,
		      void *cb_data, int force_all)
{
    int retval, i, reportall;

    if (!report_cb)
	return 0;
    if ((retval = report_cb(REPORT_BEGIN, cg,-1, NULL,  cb_data)) < 0)
	return retval;

    // report all members if forced, there are no members with change detect in the group,
    // or the group doesnt have the 'report changed members only' bit set
    reportall = force_all || (cg->n_monitored == 0) ||
	!(cg->group->userarg2 & GROUP_REPORT_CHANGED_MEMBERS);

    for (i = 0; i < cg->n_members; i++) {
	if (reportall || RTAPI_BIT_TEST(cg->changed, i))
	    if ((retval = report_cb(REPORT_SIGNAL, cg,i,
				    SHMPTR(cg->member[i]->sig_member_ptr), cb_data)) < 0)
		return retval;
    }
    return report_cb(REPORT_END, cg, -1, NULL,  cb_data);
}

int hal_cgroup_free(hal_compiled_group_t *cgroup)
{
    if (cgroup == NULL)
	return -ENOENT;
    if (cgroup->tracking)
	free(cgroup->tracking);
    if (cgroup->changed)
	free(cgroup->changed);
    if (cgroup->member)
	free(cgroup->member);
    free(cgroup);
    return 0;
}
//--- end group support code ---

/***********************************************************************
*         Scope exit unlock helpers                                    *
*         see hal_priv.h for usage hints                               *
************************************************************************/
void halpr_autorelease_mutex(void *variable)
{
    if (hal_data != NULL)
	rtapi_mutex_give(&(hal_data->mutex));
    else
	// programming error
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: halpr_autorelease_mutex called before hal_data inited\n",
			rtapi_instance);
}

void halpr_autorelease_ordered(int *remote_instance)
{
    if (hal_data == NULL) { 	// programming error
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: halpr_autorelease_ordered called before hal_data inited\n",
			rtapi_instance);
    }

    if ((remote_instance == NULL) ||
	!hal_valid_instance(*remote_instance)) { // programming error
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: halpr_autorelease_ordered called with invalid instance %p\n",
			rtapi_instance, remote_instance);
	return;
    }
    halpr_unlock_ordered(rtapi_instance, *remote_instance);
}

/***********************************************************************
*                    Internal HAL ring support functions               *
************************************************************************/
static hal_ring_t *find_ring_by_name(hal_data_t *hd, const char *name)
{
    int next;
    hal_ring_t *ring;

    /* search ring list for 'name' */
    next = hd->ring_list_ptr;
    while (next != 0) {
	ring = SHMPTR_IN(hd, next);
	if (strcmp(ring->name, name) == 0) {
	    /* found a match */
	    return ring;
	}
	/* didn't find it yet, look at next one */
	next = ring->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}


static hal_ring_attachment_t *find_ring_attachment_by_name(hal_data_t *hd, const char *name)
{
    int next;
    hal_ring_attachment_t *ring;

    /* search ring attachment list for 'name' */
    next = hd->ring_attachment_list_ptr;
    while (next != 0) {
	ring = SHMPTR_IN(hd, next);
	if (strcmp(ring->name, name) == 0) {
	    /* found a match */
	    return ring;
	}
	/* didn't find it yet, look at next one */
	next = ring->next_ptr;
    }
    /* if loop terminates, we reached end of list with no match */
    return 0;
}

// we manage ring shm segments through a bitmap visible in hal_data
// instead of going through separate (and rather useless) RTAPI methods
// this removes both reliance on a visible RTAPI data segment, which the
// userland flavors dont have, and simplifies RTAPI.
// the shared memory key of a given ring id and instance is defined as
// OS_KEY(RTAPI_RING_SHM_KEY+id, instance).
static int next_ring_id(void)
{
    int i, ring_shmkey;
    for (i = 0; i < HAL_MAX_RINGS; i++) {
	if (!RTAPI_BIT_TEST(hal_data->rings,i)) {  // unused
#if 1  // paranoia
	    ring_shmkey = OS_KEY(HAL_KEY, rtapi_instance);
	    // test if foreign instance exists
	    if (!rtapi_shmem_exists(ring_shmkey)) {
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL_LIB:%d BUG: next_ring_id(%d) - shm segment exists (%x)\n",
				rtapi_instance, i, ring_shmkey);
		return -EEXIST;
	    }
#endif
	    RTAPI_BIT_SET(hal_data->rings,i);      // allocate
	    return i;
	}
    }
    return -EBUSY; // no more slots available
}

#if 0 // for now we never free rings and hence ring id's - no need so far
static int free_ring_id(int id)
{
    if ((id < 0) || (id > HAL_MAX_RINGS-1))
	return -EINVAL;

    if  (!_BIT_TEST(hal_data->rings,id)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d BUG: free_ring_id(%d): not allocated\n",
			rtapi_instance, id);
	return -EINVAL;
    }
    _BIT_CLEAR(hal_data->rings,id);
    return 0;
}
#endif

/***********************************************************************
*                     Public HAL ring functions                        *
************************************************************************/
int hal_ring_new(const char *name, int size, int sp_size, int module_id, int flags)
{
    hal_ring_t *rbdesc;
    int *prev, next, cmp, retval;
    int ring_id;
    ringheader_t *rhptr;

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_new called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    {
	hal_ring_t *ptr __attribute__((cleanup(halpr_autorelease_mutex)));

	rtapi_mutex_get(&(hal_data->mutex));

	if (!name) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: hal_ring_new() called with NULL name\n",
			    rtapi_instance);
	}
	if (strlen(name) > HAL_NAME_LEN) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: ring name '%s' is too long\n",
			    rtapi_instance, name);
	    return -EINVAL;
	}
	if (hal_data->lock & HAL_LOCK_LOAD)  {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: hal_ring_new called while HAL locked\n",
			    rtapi_instance);
	    return -EPERM;
	}

	// make sure no such ring name already exists
	if ((ptr = find_ring_by_name(hal_data, name)) != 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: ring '%s' already exists\n",
			    rtapi_instance, name);
	    return -EEXIST;
	}
	// allocate a new ring id - needed since we dont track ring shm
	// segments in RTAPI
	if ((ring_id = next_ring_id()) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d cant allocate new ring id for '%s'\n",
			    rtapi_instance, name);
	    return -ENOMEM;
	}

	// allocate a new ring descriptor
	if ((rbdesc = alloc_ring_struct()) == 0) {
	    // alloc failed
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: insufficient memory for ring '%s'\n", name);
	    return -ENOMEM;
	}

	rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d creating ring '%s'\n",
			rtapi_instance, name);

	// make total allocation fit ringheader, ringbuffer and scratchpad
	rbdesc->total_size = ring_memsize( flags, size, sp_size);

	// allocate shared memory segment for ring and init
	rbdesc->ring_shmkey = OS_KEY((RTAPI_RING_SHM_KEY + ring_id), rtapi_instance);

	// allocate an RTAPI shm segment owned by the allocating module
	if ((rbdesc->ring_shmid = rtapi_shmem_new(rbdesc->ring_shmkey, module_id,
						  rbdesc->total_size)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_new: rtapi_shmem_new(0x%8.8x,%d,%zu) failed: %d\n",
			    rtapi_instance,
			    rbdesc->ring_shmkey, module_id,
			    rbdesc->total_size, rbdesc->ring_shmid);
	    return  -ENOMEM;
	}

	// map the segment now so we can fill in the ringheader details
	if ((retval = rtapi_shmem_getptr(rbdesc->ring_shmid,
					 (void **)&rhptr)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_new: rtapi_shmem_getptr for %d failed %d\n",
			    rtapi_instance, rbdesc->ring_shmid, retval);
	    return -ENOMEM;
	}

	ringheader_init(rhptr, flags, size, sp_size);
	rhptr->refcount = 0; // on hal_ring_attach: increase; on hal_ring_detach: decrease
	rtapi_snprintf(rbdesc->name, sizeof(rbdesc->name), "%s", name);
	rbdesc->next_ptr = 0;
	rbdesc->owner = module_id;

	// search list for 'name' and insert new structure
	prev = &(hal_data->ring_list_ptr);
	next = *prev;
	while (1) {
	    if (next == 0) {
		/* reached end of list, insert here */
		rbdesc->next_ptr = next;
		*prev = SHMOFF(rbdesc);
		return 0;
	    }
	    ptr = SHMPTR(next);
	    cmp = strcmp(ptr->name, rbdesc->name);
	    if (cmp > 0) {
		/* found the right place for it, insert here */
		rbdesc->next_ptr = next;
		*prev = SHMOFF(rbdesc);
		return 0;
	    }
	    /* didn't find it yet, look at next one */
	    prev = &(ptr->next_ptr);
	    next = *prev;
	}
    }
    // automatic unlock by scope exit
}

int hal_ring_attach(const char *name, ringbuffer_t *rbptr, int module_id)
{
    hal_ring_t *rbdesc;
    hal_ring_attachment_t *radesc;
    int retval, status, shmid, other_instance;
    hal_data_t *hd;
    hal_context_t *ctx = THIS_CONTEXT();
    ringheader_t *rhptr;
    char localname[HAL_NAME_LEN+1];
    char prefix[HAL_NAME_LEN+1];

    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL: ERROR: hal_ring_attach called before init\n");
	return -EINVAL;
    }
    // remote_name() does its own locking
    if ((status = remote_name(OBJ_RING, name, &other_instance,
			      localname, prefix)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d remote namespace for '%s' not associated\n",
			rtapi_instance, name);
	return status;
    }
    if (status == NAME_INVALID) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL:%d hal_ring_attach: invalid name '%s'\n",
			rtapi_instance, name);
	return -EINVAL;
    }
    // no mutex(es) held at this point
    {
	int remote_instance
	    __attribute__((cleanup(halpr_autorelease_ordered))) = other_instance;

	// lock our, or both hal_data segment(s) in instance order
	if ((retval = halpr_lock_ordered(rtapi_instance, remote_instance)) != 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: failed to lock %d/%d\n",
			    rtapi_instance, rtapi_instance, remote_instance);
	    return retval;
	}

	// at this point, good to manipulate objects in either hal_data segment
	// as both mutexes are held
	hd = ctx->namespaces[remote_instance].haldata;

	if ((rbdesc = find_ring_by_name(hd, localname)) == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach: no such ring '%s'\n",
			    rtapi_instance, name);
	    return -EINVAL;
	}

	// check for self-reference here (instX:foo where instX==us)
	if ((status == NAME_REMOTE) &&
	    !strcmp(prefix, hal_data->nsdesc[rtapi_instance].name)) {
	    rtapi_print_msg(RTAPI_MSG_DBG,
			    "HAL:%d hal_ring_attach: ignoring self-reference in '%s'\n",
			    rtapi_instance, name);
	    status = NAME_LOCAL; // local afterall
	}

	// map in the shm segment
	if ((shmid = rtapi_shmem_new_inst(rbdesc->ring_shmkey,
					  remote_instance, module_id, 0)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach(%s): rtapi_shmem_new_inst() failed %d\n",
			    rtapi_instance,name, shmid);
	    return -EINVAL;
	}
	// make it accessible
	if ((retval = rtapi_shmem_getptr(shmid, (void **)&rhptr))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_attach: rtapi_shmem_getptr %d failed %d\n",
			    rtapi_instance, rbdesc->ring_shmid, retval);
	    return -ENOMEM;
	}
	// allocate a new ring attachment descriptor
	if ((radesc = alloc_ring_attachment_struct()) == 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: insufficient memory for attching ring '%s'\n",
			    rtapi_instance, name);
	    return -ENOMEM;
	}

	// record usage in ringheader
	rhptr->refcount++;
	// fill in ringbuffer_t
	ringbuffer_init(rhptr, rbptr);

	// record attachment details
	strncpy(radesc->name, name, HAL_NAME_LEN);
	radesc->ring_shmid = shmid;
	radesc->ring_inst = remote_instance;
	radesc->owner = module_id;

	// record attachment descriptor
	radesc->next_ptr = hal_data->ring_attachment_list_ptr;
	hal_data->ring_attachment_list_ptr = SHMOFF(radesc);

	// unlocking happens automatically on scope exit
	return 0;
    }
}

int hal_ring_detach(const char *name, ringbuffer_t *rbptr)
{
    hal_ring_attachment_t *radesc, *raptr;
    ringheader_t *rhptr;
    int next,*prev, status, other_instance;
    int retval;
    char localname[HAL_NAME_LEN+1];
    char prefix[HAL_NAME_LEN+1];

    if ((rbptr == NULL) || (rbptr->magic != RINGBUFFER_MAGIC)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach: invalid ringbuffer\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (hal_data == 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach called before init\n",
			rtapi_instance);
	return -EINVAL;
    }
    if (hal_data->lock & HAL_LOCK_CONFIG)  {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL:%d ERROR: hal_ring_detach called while HAL locked\n",
			rtapi_instance);
	return -EPERM;
    }

    rtapi_print_msg(RTAPI_MSG_DBG, "HAL:%d removing ring attachment '%s'\n",
		    rtapi_instance, name);

    // remote_name() does its own locking
    if ((status = remote_name(OBJ_RING, name, &other_instance,
			      localname, prefix)) < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL:%d remote namespace for '%s' not associated\n",
			rtapi_instance, name);
	return status;
    }
    if (status == NAME_INVALID) {
	rtapi_print_msg(RTAPI_MSG_ERR, "HAL:%d hal_ring_detach: invalid name '%s'\n",
			rtapi_instance, name);
	return -EINVAL;
    }
    // no mutex(es) held here
    {
	int remote_instance
	    __attribute__((cleanup(halpr_autorelease_ordered))) = other_instance;

	if ((retval = halpr_lock_ordered(rtapi_instance, remote_instance))) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d ERROR: failed to lock %d/%d\n",
			    rtapi_instance, rtapi_instance, remote_instance);
	    return retval;
	}
	if ((radesc = find_ring_attachment_by_name(hal_data, name)) == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_detach: no such ring attachment '%s'\n",
			    rtapi_instance, name);
	    return -EINVAL;
	}

	// check for self-reference (instX:foo where instX==us)
	if ((status == NAME_REMOTE) &&
	    !strcmp(prefix, hal_data->nsdesc[rtapi_instance].name)) {
	    rtapi_print_msg(RTAPI_MSG_DBG,
			    "HAL:%d hal_ring_detach: ignoring self-reference in '%s'\n",
			    rtapi_instance, name);
	    status = NAME_LOCAL;
	}

	// retrieve ringheader address to decrease refcount
	if ((retval = rtapi_shmem_getptr(radesc->ring_shmid, (void **)&rhptr)) < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL:%d hal_ring_detach: rtapi_shmem_getptr %d failed %d\n",
			    rtapi_instance, radesc->ring_shmid, retval);
	    return -ENOMEM;
	}
	rhptr->refcount--;
	rbptr->magic = 0;  // invalidate

	// delete the attachment record
	prev = &(hal_data->ring_attachment_list_ptr);
	next = *prev;
	while (next != 0) {
	    raptr = SHMPTR(next);
	    if (radesc == raptr) {
		*prev = radesc->next_ptr;
		free_ring_attachment_struct(radesc);
		break;
	    }
	    prev = &(raptr->next_ptr);
	    next = *prev;
	}
    }
    // unlocking happens automatically on scope exit
    return 0;
}
/***********************************************************************
*                     LOCAL FUNCTION CODE                              *
************************************************************************/

#ifdef RTAPI
/* these functions are called when the hal_lib module is insmod'ed
   or rmmod'ed.
*/
#if defined(BUILD_SYS_USER_DSO)
#undef CONFIG_PROC_FS
#endif

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
extern struct proc_dir_entry *rtapi_dir;
static struct proc_dir_entry *hal_dir = 0;
static struct proc_dir_entry *hal_newinst_file = 0;

static int proc_write_newinst(struct file *file,
        const char *buffer, unsigned long count, void *data)
{
    if(hal_data->pending_constructor) {
        rtapi_print_msg(RTAPI_MSG_DBG,
                "HAL: running constructor for %s %s\n",
                hal_data->constructor_prefix,
                hal_data->constructor_arg);
        hal_data->pending_constructor(hal_data->constructor_prefix,
                hal_data->constructor_arg);
        hal_data->pending_constructor = 0;
    }
    return count;
}

static void hal_proc_clean(void) {
    if(hal_newinst_file)
        remove_proc_entry("newinst", hal_dir);
    if(hal_dir)
        remove_proc_entry("hal", rtapi_dir);
    hal_newinst_file = hal_dir = 0;
}
static int hal_proc_init(void) {
    if(!rtapi_dir) return 0;
    hal_dir = create_proc_entry("hal", S_IFDIR, rtapi_dir);
    if(!hal_dir) { hal_proc_clean(); return -1; }
    hal_newinst_file = create_proc_entry("newinst", 0666, hal_dir);
    if(!hal_newinst_file) { hal_proc_clean(); return -1; }
    hal_newinst_file->data = NULL;
    hal_newinst_file->read_proc = NULL;
    hal_newinst_file->write_proc = proc_write_newinst;
    return 0;
}
#else
static int hal_proc_clean(void) { return 0; }
static int hal_proc_init(void) { return 0; }
#endif

hal_context_t *rt_context;

int rtapi_app_main(void)
{
    int retval;
    void *mem;
    hal_namespace_t *np;

    rtapi_switch = rtapi_get_handle();
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL_LIB:%d loading RT support gd=%pp\n",rtapi_instance,global_data);

    /* do RTAPI init */
    lib_module_id = rtapi_init("HAL_LIB");
    if (lib_module_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d ERROR: rtapi init failed\n",
			rtapi_instance);
	return -EINVAL;
    }

    // paranoia
    if (global_data == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d ERROR: global_data == NULL\n",
			rtapi_instance);
	return -EINVAL;
    }

    /* get HAL shared memory block from RTAPI */
    lib_mem_id = rtapi_shmem_new(HAL_KEY, lib_module_id, global_data->hal_size);

    if (lib_mem_id < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d ERROR: could not open shared memory\n",
			rtapi_instance);
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }
    /* get address of shared memory area */
    retval = rtapi_shmem_getptr(lib_mem_id, &mem);

    if (retval < 0) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d ERROR: could not access shared memory\n",
			rtapi_instance);
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }
    /* set up internal pointers to shared mem and data structure */
    hal_shmem_base = (char *) mem;
    hal_data = (hal_data_t *) mem;
    /* perform a global init if needed */
    retval = init_hal_data();

    if ( retval ) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d ERROR: could not init shared memory\n",
			rtapi_instance);
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }
    if (rt_context != NULL) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: BUG: rt_context != NULL - rtapi_app_main() called twice?\n");
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }

    // allocate the RT context
    if ((rt_context = alloc_context_struct(context_pid())) == NULL) {
	rtapi_mutex_give(&(hal_data->mutex));
	rtapi_print_msg(RTAPI_MSG_ERR,
	    "HAL: ERROR: cannot allocate RT HAL context\n");
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }
    // export what we're 'seeing'
    // we can fill this in permanently for RT
    // for ULAPI it's more involved due to the RTAI bug workaround
    // requiring halcmd to detach and reattach when running module_helper
    np = &rt_context->namespaces[rtapi_instance];
    RTAPI_BIT_SET(rt_context->visible_namespaces, rtapi_instance);
    np->haldata = hal_data;

    retval = hal_proc_init();

    if ( retval ) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB: ERROR:%d could not init /proc files\n",
			rtapi_instance);
	rtapi_exit(lib_module_id);
	return -EINVAL;
    }
    //rtapi_printall();

    /* done */
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL_LIB:%d kernel lib installed successfully\n",
		    rtapi_instance);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_thread_t *thread;
	int retval;

    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL_LIB:%d removing RT support\n",rtapi_instance);
    hal_proc_clean();
    /* grab mutex before manipulating list */
    rtapi_mutex_get(&(hal_data->mutex));
    /* must remove all threads before unloading this module */
    while (hal_data->thread_list_ptr != 0) {
	/* point to a thread */
	thread = SHMPTR(hal_data->thread_list_ptr);
	/* unlink from list */
	hal_data->thread_list_ptr = thread->next_ptr;
	/* and delete it */
	free_thread_struct(thread);
    }
    // release HAL context
    if (rt_context->refcount != 0) {
	rtapi_print_msg(RTAPI_MSG_INFO,
			"HAL: rtapi_app_exit: %d comps didnt hal_exit()\n",
			rt_context->refcount);
    }
    free_context_struct(rt_context);
    rt_context = NULL;
    /* release mutex */
    rtapi_mutex_give(&(hal_data->mutex));

    /* release RTAPI resources */
    rtapi_shmem_delete(lib_mem_id, lib_module_id);
    rtapi_exit(lib_module_id);

    retval = rtapi_exit(lib_module_id);
    if (retval) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d rtapi_exit(%d) failed: %d\n",
			rtapi_instance, lib_module_id, retval);
    }
    /* done */
    rtapi_print_msg(RTAPI_MSG_DBG,
		    "HAL_LIB:%d RT support removed successfully\n",
		    rtapi_instance);
}

/* this is the task function that implements threads in realtime */

static void thread_task(void *arg)
{
    hal_thread_t *thread;
    hal_funct_t *funct;
    hal_funct_entry_t *funct_root, *funct_entry;
    long long int start_time, end_time;
    long long int thread_start_time;

    thread = arg;
    while (1) {
	if (hal_data->threads_running > 0) {
	    /* point at first function on function list */
	    funct_root = (hal_funct_entry_t *) & (thread->funct_list);
	    funct_entry = SHMPTR(funct_root->links.next);
	    /* execution time logging */
	    start_time = rtapi_get_clocks();
	    end_time = start_time;
	    thread_start_time = start_time;
	    /* run thru function list */
	    while (funct_entry != funct_root) {
		/* call the function */
		funct_entry->funct(funct_entry->arg, thread->period);
		/* capture execution time */
		end_time = rtapi_get_clocks();
		/* point to function structure */
		funct = SHMPTR(funct_entry->funct_ptr);
		/* update execution time data */
		funct->runtime = (hal_s32_t)(end_time - start_time);
		if (funct->runtime > funct->maxtime) {
		    funct->maxtime = funct->runtime;
		}
		/* point to next next entry in list */
		funct_entry = SHMPTR(funct_entry->links.next);
		/* prepare to measure time for next funct */
		start_time = end_time;
	    }
	    /* update thread execution time */
	    thread->runtime = (hal_s32_t)(end_time - thread_start_time);
	    if (thread->runtime > thread->maxtime) {
		thread->maxtime = thread->runtime;
	    }
	}
	/* wait until next period */
	rtapi_wait();
    }
}
#endif /* RTAPI */

/* see the declarations of these functions (near top of file) for
   a description of what they do.
*/

static int init_hal_data(void)
{

    /* has the block already been initialized? */
    if (hal_data->version != 0) {
	/* yes, verify version code */
	if (hal_data->version == HAL_VER) {
	    return 0;
	} else {
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: version code mismatch\n");
	    return -1;
	}
    }
    /* no, we need to init it, grab the mutex unconditionally */
    rtapi_mutex_try(&(hal_data->mutex));

    // some heaps contain garbage, like xenomai
    memset(hal_data, 0, global_data->hal_size);

    /* set version code so nobody else init's the block */
    hal_data->version = HAL_VER;

    // namespace support:
    // how many _remote_ namespaces reference this HAL segment?
    hal_data->refcount = 0;

    /* initialize everything */
    hal_data->comp_list_ptr = 0;
    hal_data->pin_list_ptr = 0;
    hal_data->sig_list_ptr = 0;
    hal_data->param_list_ptr = 0;
    hal_data->funct_list_ptr = 0;
    hal_data->thread_list_ptr = 0;
    hal_data->base_period = 0;
    hal_data->threads_running = 0;
    hal_data->oldname_free_ptr = 0;
    hal_data->comp_free_ptr = 0;
    hal_data->pin_free_ptr = 0;
    hal_data->sig_free_ptr = 0;
    hal_data->param_free_ptr = 0;
    hal_data->funct_free_ptr = 0;
    hal_data->pending_constructor = 0;
    hal_data->constructor_prefix[0] = 0;
    list_init_entry(&(hal_data->funct_entry_free));
    hal_data->thread_free_ptr = 0;
    hal_data->exact_base_period = 0;

    hal_data->group_list_ptr = 0;
    hal_data->member_list_ptr = 0;
    hal_data->ring_list_ptr = 0;
    hal_data->context_list_ptr = 0;
    hal_data->ring_attachment_list_ptr = 0;

    hal_data->group_free_ptr = 0;
    hal_data->member_free_ptr = 0;
    hal_data->ring_free_ptr = 0;
    hal_data->context_free_ptr = 0;
    hal_data->ring_attachment_free_ptr = 0;

    RTAPI_ZERO_BITMAP(&hal_data->rings, HAL_MAX_RINGS);
    // silly 1-based shm segment id allocation
    // yeah, 'user friendly', how could one possibly think zero might be a valid id
    RTAPI_BIT_SET(hal_data->rings,0);

    RTAPI_BIT_SET(hal_data->requested_namespaces,rtapi_instance);
    strncpy(hal_data->nsdesc[rtapi_instance].name,
	    global_data->instance_name, HAL_NAME_LEN);
    /* set up for shmalloc_xx() */
    hal_data->shmem_bot = sizeof(hal_data_t);
    hal_data->shmem_top = global_data->hal_size;
    hal_data->lock = HAL_LOCK_NONE;
    /* done, release mutex */
    rtapi_mutex_give(&(hal_data->mutex));
    return 0;
}

static void *shmalloc_up(long int size)
{
    long int tmp_bot;
    void *retval;

    /* deal with alignment requirements */
    tmp_bot = hal_data->shmem_bot;
    if (size >= 8) {
	/* align on 8 byte boundary */
	tmp_bot = (tmp_bot + 7) & (~7);
    } else if (size >= 4) {
	/* align on 4 byte boundary */
	tmp_bot = (tmp_bot + 3) & (~3);
    } else if (size == 2) {
	/* align on 2 byte boundary */
	tmp_bot = (tmp_bot + 1) & (~1);
    }
    /* is there enough memory available? */
    if ((hal_data->shmem_top - tmp_bot) < size) {
	/* no */
	return 0;
    }
    /* memory is available, allocate it */
    retval = SHMPTR(tmp_bot);
    hal_data->shmem_bot = tmp_bot + size;
    hal_data->shmem_avail = hal_data->shmem_top - hal_data->shmem_bot;
    return retval;
}

static void *shmalloc_dn(long int size)
{
    long int tmp_top;
    void *retval;

    /* tentatively allocate memory */
    tmp_top = hal_data->shmem_top - size;
    /* deal with alignment requirements */
    if (size >= 8) {
	/* align on 8 byte boundary */
	tmp_top &= (~7);
    } else if (size >= 4) {
	/* align on 4 byte boundary */
	tmp_top &= (~3);
    } else if (size == 2) {
	/* align on 2 byte boundary */
	tmp_top &= (~1);
    }
    /* is there enough memory available? */
    if (tmp_top < hal_data->shmem_bot) {
	/* no */
	return 0;
    }
    /* memory is available, allocate it */
    retval = SHMPTR(tmp_top);
    hal_data->shmem_top = tmp_top;
    hal_data->shmem_avail = hal_data->shmem_top - hal_data->shmem_bot;
    return retval;
}

hal_comp_t *halpr_alloc_comp_struct(void)
{
    hal_comp_t *p;

    /* check the free list */
    if (hal_data->comp_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->comp_free_ptr);
	/* unlink it from the free list */
	hal_data->comp_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_comp_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->comp_id = 0;
	p->mem_id = 0;
	p->type = TYPE_INVALID;
	p->state = COMP_INVALID;
	// p->shmem_base = 0;
	p->name[0] = '\0';
    }
    return p;
}

static hal_pin_t *alloc_pin_struct(void)
{
    hal_pin_t *p;

    /* check the free list */
    if (hal_data->pin_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->pin_free_ptr);
	/* unlink it from the free list */
	hal_data->pin_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_pin_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->data_ptr_addr = 0;
	p->signal_inst = -1; // not referring to a valid instance
	p->owner_ptr = 0;
	p->type = 0;
	p->dir = 0;
	p->signal = 0;
	memset(&p->dummysig, 0, sizeof(hal_data_u));
	p->name[0] = '\0';
#ifdef USE_PIN_USER_ATTRIBUTES
	p->epsilon = 0.0;
	p->flags = 0;
#endif
    }
    return p;
}

static hal_sig_t *alloc_sig_struct(void)
{
    hal_sig_t *p;

    /* check the free list */
    if (hal_data->sig_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->sig_free_ptr);
	/* unlink it from the free list */
	hal_data->sig_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_sig_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->data_ptr = 0;
	p->type = 0;
	p->readers = 0;
	p->writers = 0;
	p->bidirs = 0;
	p->name[0] = '\0';
    }
    return p;
}

static hal_param_t *alloc_param_struct(void)
{
    hal_param_t *p;

    /* check the free list */
    if (hal_data->param_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->param_free_ptr);
	/* unlink it from the free list */
	hal_data->param_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_param_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->data_ptr = 0;
	p->owner_ptr = 0;
	p->type = 0;
	p->name[0] = '\0';
    }
    return p;
}

static hal_oldname_t *halpr_alloc_oldname_struct(void)
{
    hal_oldname_t *p;

    /* check the free list */
    if (hal_data->oldname_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->oldname_free_ptr);
	/* unlink it from the free list */
	hal_data->oldname_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_oldname_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->name[0] = '\0';
    }
    return p;
}

static hal_group_t *alloc_group_struct(void)
{
    hal_group_t *p;

    /* check the free list */
    if (hal_data->group_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->group_free_ptr);
	/* unlink it from the free list */
	hal_data->group_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_group_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->refcount = 0;
	p->userarg1 = 0;
	p->userarg2 = 0;
	p->member_ptr = 0;
	p->name[0] = '\0';
    }
    return p;
}


static hal_member_t *alloc_member_struct(void)
{
    hal_member_t *p;

    /* check the free list */
    if (hal_data->member_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->member_free_ptr);
	/* unlink it from the free list */
	hal_data->member_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_member_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->sig_member_ptr = 0;
	p->signal_inst = rtapi_instance; // default to local
	p->group_member_ptr = 0;
	p->userarg1 = 0;
	p->epsilon = CHANGE_DETECT_EPSILON;
    }
    return p;
}

#ifdef RTAPI
static hal_funct_t *alloc_funct_struct(void)
{
    hal_funct_t *p;

    /* check the free list */
    if (hal_data->funct_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->funct_free_ptr);
	/* unlink it from the free list */
	hal_data->funct_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_funct_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->uses_fp = 0;
	p->owner_ptr = 0;
	p->reentrant = 0;
	p->users = 0;
	p->arg = 0;
	p->funct = 0;
	p->name[0] = '\0';
    }
    return p;
}
#endif /* RTAPI */

static hal_funct_entry_t *alloc_funct_entry_struct(void)
{
    hal_list_t *freelist, *l;
    hal_funct_entry_t *p;

    /* check the free list */
    freelist = &(hal_data->funct_entry_free);
    l = list_next(freelist);
    if (l != freelist) {
	/* found a free structure, unlink from the free list */
	list_remove_entry(l);
	p = (hal_funct_entry_t *) l;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_funct_entry_t));
	l = (hal_list_t *) p;
	list_init_entry(l);
    }
    if (p) {
	/* make sure it's empty */
	p->funct_ptr = 0;
	p->arg = 0;
	p->funct = 0;
    }
    return p;
}

#ifdef RTAPI
static hal_thread_t *alloc_thread_struct(void)
{
    hal_thread_t *p;

    /* check the free list */
    if (hal_data->thread_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->thread_free_ptr);
	/* unlink it from the free list */
	hal_data->thread_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_thread_t));
    }
    if (p) {
	/* make sure it's empty */
	p->next_ptr = 0;
	p->uses_fp = 0;
	p->period = 0;
	p->priority = 0;
	p->task_id = 0;
	list_init_entry(&(p->funct_list));
	p->name[0] = '\0';
    }
    return p;
}
#endif /* RTAPI */

static void free_comp_struct(hal_comp_t * comp)
{
    int *prev, next;
#ifdef RTAPI
    hal_funct_t *funct;
#endif /* RTAPI */
    hal_pin_t *pin;
    hal_param_t *param;

    /* can't delete the component until we delete its "stuff" */
    /* need to check for functs only if a realtime component */
#ifdef RTAPI
    /* search the function list for this component's functs */
    prev = &(hal_data->funct_list_ptr);
    next = *prev;
    while (next != 0) {
	funct = SHMPTR(next);
	if (SHMPTR(funct->owner_ptr) == comp) {
	    /* this function belongs to our component, unlink from list */
	    *prev = funct->next_ptr;
	    /* and delete it */
	    free_funct_struct(funct);
	} else {
	    /* no match, try the next one */
	    prev = &(funct->next_ptr);
	}
	next = *prev;
    }
#endif /* RTAPI */
    /* search the pin list for this component's pins */
    prev = &(hal_data->pin_list_ptr);
    next = *prev;
    while (next != 0) {
	pin = SHMPTR(next);
	if (SHMPTR(pin->owner_ptr) == comp) {
	    /* this pin belongs to our component, unlink from list */
	    *prev = pin->next_ptr;
	    /* and delete it */
	    free_pin_struct(pin);
	} else {
	    /* no match, try the next one */
	    prev = &(pin->next_ptr);
	}
	next = *prev;
    }
    /* search the parameter list for this component's parameters */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (next != 0) {
	param = SHMPTR(next);
	if (SHMPTR(param->owner_ptr) == comp) {
	    /* this param belongs to our component, unlink from list */
	    *prev = param->next_ptr;
	    /* and delete it */
	    free_param_struct(param);
	} else {
	    /* no match, try the next one */
	    prev = &(param->next_ptr);
	}
	next = *prev;
    }
    /* now we can delete the component itself */
    /* clear contents of struct */
    comp->comp_id = 0;
    comp->mem_id = 0;
    comp->type = TYPE_INVALID;
    comp->state = COMP_INVALID;
    comp->last_bound = 0;
    comp->last_unbound = 0;
    comp->last_update = 0;
    // comp->shmem_base = 0;
    comp->name[0] = '\0';
    /* add it to free list */
    comp->next_ptr = hal_data->comp_free_ptr;
    hal_data->comp_free_ptr = SHMOFF(comp);
}

static void unlink_pin(hal_pin_t * pin)
{
    hal_sig_t *sig;
    hal_comp_t *comp;
    void *dummy_addr, **data_ptr_addr;
    int inst = pin->signal_inst;
    hal_context_t *ctx = THIS_CONTEXT();
    hal_data_t *remote_hal_data = ctx->namespaces[inst].haldata;

    /* is this pin linked to a signal? */
    if (pin->signal != 0) {
	/* yes, need to unlink it */
	sig = SHMPTR_IN(remote_hal_data, pin->signal);
	/* make pin's 'data_ptr' point to its dummy signal */
	data_ptr_addr = SHMPTR(pin->data_ptr_addr);
	comp = SHMPTR(pin->owner_ptr);
	dummy_addr = hal_shmem_base + SHMOFF(&(pin->dummysig));
	*data_ptr_addr = dummy_addr;
	/* update the signal's reader/writer counts */
	if ((pin->dir & HAL_IN) != 0) {
	    sig->readers--;
	}
	if (pin->dir == HAL_OUT) {
	    sig->writers--;
	}
	if (pin->dir == HAL_IO) {
	    sig->bidirs--;
	}
	/* mark pin as unlinked */
	pin->signal = 0;
	pin->signal_inst = rtapi_instance;
    }
}

static void free_pin_struct(hal_pin_t * pin)
{

    unlink_pin(pin);
    /* clear contents of struct */
    if ( pin->oldname != 0 ) free_oldname_struct(SHMPTR(pin->oldname));
    pin->data_ptr_addr = 0;
    pin->owner_ptr = 0;
    pin->type = 0;
    pin->dir = 0;
    pin->signal = 0;
    memset(&pin->dummysig, 0, sizeof(hal_data_u));
    pin->name[0] = '\0';
#ifdef USE_PIN_USER_ATTRIBUTES
    pin->epsilon = 0.0;
    pin->flags = 0;
#endif
    /* add it to free list */
    pin->next_ptr = hal_data->pin_free_ptr;
    hal_data->pin_free_ptr = SHMOFF(pin);
}

static void free_sig_struct(hal_sig_t * sig)
{
    hal_pin_t *pin;

    /* look for pins linked to this signal */
    pin = halpr_find_pin_by_sig(sig, 0);
    while (pin != 0) {
	/* found one, unlink it */
	unlink_pin(pin);
	/* check for another pin linked to the signal */
	pin = halpr_find_pin_by_sig(sig, pin);
    }
    /* clear contents of struct */
    sig->data_ptr = 0;
    sig->type = 0;
    sig->readers = 0;
    sig->writers = 0;
    sig->bidirs = 0;
    sig->name[0] = '\0';
    /* add it to free list */
    sig->next_ptr = hal_data->sig_free_ptr;
    hal_data->sig_free_ptr = SHMOFF(sig);
}

static void free_param_struct(hal_param_t * p)
{
    /* clear contents of struct */
    if ( p->oldname != 0 ) free_oldname_struct(SHMPTR(p->oldname));
    p->data_ptr = 0;
    p->owner_ptr = 0;
    p->type = 0;
    p->name[0] = '\0';
    /* add it to free list (params use the same struct as src vars) */
    p->next_ptr = hal_data->param_free_ptr;
    hal_data->param_free_ptr = SHMOFF(p);
}

static void free_oldname_struct(hal_oldname_t * oldname)
{
    /* clear contents of struct */
    oldname->name[0] = '\0';
    /* add it to free list */
    oldname->next_ptr = hal_data->oldname_free_ptr;
    hal_data->oldname_free_ptr = SHMOFF(oldname);
}

static void free_member_struct(hal_member_t * member)
{
    /* clear contents of struct */
    member->group_member_ptr = 0;
    member->sig_member_ptr = 0;
    member->userarg1 = 0;

    /* add it to free list */
    member->next_ptr = hal_data->member_free_ptr;
    hal_data->member_free_ptr = SHMOFF(member);
}

static void free_group_struct(hal_group_t * group)
{
    int nextm;
    hal_member_t * member;

    /* clear contents of struct */
    group->next_ptr = 0;
    group->refcount = 0;
    group->userarg1 = 0;
    group->userarg2 = 0;
    group->name[0] = '\0';
    nextm = group->member_ptr;
    // free all linked member structs
    while (nextm != 0) {
	member = SHMPTR(nextm);
	nextm = member->next_ptr;
	free_member_struct(member);
    }
    group->member_ptr = 0;

    /* add it to free list */
    group->next_ptr = hal_data->group_free_ptr;
    hal_data->group_free_ptr = SHMOFF(group);
}

static hal_ring_t *alloc_ring_struct(void)
{
    hal_ring_t *p;

    /* check the free list */
    if (hal_data->ring_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->ring_free_ptr);
	/* unlink it from the free list */
	hal_data->ring_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_ring_t));
    }
    return p;
}

#if 0 // for now we never free rings - no need
static void free_ring_struct(hal_ring_t * p)
{
    /* add it to free list */
    p->next_ptr = hal_data->ring_free_ptr;
    hal_data->ring_free_ptr = SHMOFF(p);
}
#endif

// attach
static hal_ring_attachment_t *alloc_ring_attachment_struct(void)
{
    hal_ring_attachment_t *p;

    /* check the free list */
    if (hal_data->ring_attachment_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->ring_attachment_free_ptr);
	/* unlink it from the free list */
	hal_data->ring_attachment_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_ring_attachment_t));
    }
    return p;
}

static void free_ring_attachment_struct(hal_ring_attachment_t * p)
{
    /* add it to free list */
    p->next_ptr = hal_data->ring_attachment_free_ptr;
    hal_data->ring_attachment_free_ptr = SHMOFF(p);
}


static void free_context_struct(hal_context_t * p)
{
    hal_context_t *context;
    int next, *prev;

    // search in list of active contexts
    prev = &(hal_data->context_list_ptr);
    next = *prev;
    while (next != 0) {
	context = SHMPTR(next);
	if (context->pid  == p->pid) {
	    // unlink from active list
	    *prev = context->next_ptr;
	    // add it to free list
	    p->type =  CONTEXT_INVALID;
	    p->next_ptr = hal_data->context_free_ptr;
	    hal_data->context_free_ptr = SHMOFF(p);
	    return;
	} else {
	    prev = &(context->next_ptr);
	}
	next = *prev;
    }
}

#if 0
// lookup a context
static hal_context_t *find_context(int pid)
{
    int next;
    hal_context_t *context;

    /* search context list for 'name' */
    next = hal_data->context_list_ptr;
    while (next != 0) {
	context = SHMPTR(next);
	if (pid == context->pid) {
	    return context;
	}
	next = context->next_ptr;
    }
    return 0;
}
#endif

// create a new context for a given pid
static hal_context_t *alloc_context_struct(int pid)
{
    hal_context_t *p;

    /* check the free list */
    if (hal_data->context_free_ptr != 0) {
	/* found a free structure, point to it */
	p = SHMPTR(hal_data->context_free_ptr);
	/* unlink it from the free list */
	hal_data->context_free_ptr = p->next_ptr;
	p->next_ptr = 0;
    } else {
	/* nothing on free list, allocate a brand new one */
	p = shmalloc_dn(sizeof(hal_context_t));
    }
    if (p == NULL)
	return p;

    memset(p, 0, sizeof(hal_context_t));
    p->pid = pid;
#ifdef RTAPI
    p->type = CONTEXT_RT;
#else // ULAPI
    p->type = CONTEXT_USERLAND;
#endif
    // link it into the list of active contexts
    p->next_ptr = hal_data->context_list_ptr;
    hal_data->context_list_ptr = SHMOFF(p);
    return p;
}

#ifdef RTAPI
static void free_funct_struct(hal_funct_t * funct)
{
    int next_thread;
    hal_thread_t *thread;
    hal_list_t *list_root, *list_entry;
    hal_funct_entry_t *funct_entry;

/*  int next_thread, next_entry;*/

    if (funct->users > 0) {
	/* We can't casually delete the function, there are thread(s) which
	   will call it.  So we must check all the threads and remove any
	   funct_entrys that call this function */
	/* start at root of thread list */
	next_thread = hal_data->thread_list_ptr;
	/* run through thread list */
	while (next_thread != 0) {
	    /* point to thread */
	    thread = SHMPTR(next_thread);
	    /* start at root of funct_entry list */
	    list_root = &(thread->funct_list);
	    list_entry = list_next(list_root);
	    /* run thru funct_entry list */
	    while (list_entry != list_root) {
		/* point to funct entry */
		funct_entry = (hal_funct_entry_t *) list_entry;
		/* test it */
		if (SHMPTR(funct_entry->funct_ptr) == funct) {
		    /* this funct entry points to our funct, unlink */
		    list_entry = list_remove_entry(list_entry);
		    /* and delete it */
		    free_funct_entry_struct(funct_entry);
		} else {
		    /* no match, try the next one */
		    list_entry = list_next(list_entry);
		}
	    }
	    /* move on to the next thread */
	    next_thread = thread->next_ptr;
	}
    }
    /* clear contents of struct */
    funct->uses_fp = 0;
    funct->owner_ptr = 0;
    funct->reentrant = 0;
    funct->users = 0;
    funct->arg = 0;
    funct->funct = 0;
    funct->name[0] = '\0';
    /* add it to free list */
    funct->next_ptr = hal_data->funct_free_ptr;
    hal_data->funct_free_ptr = SHMOFF(funct);
}
#endif /* RTAPI */

static void free_funct_entry_struct(hal_funct_entry_t * funct_entry)
{
    hal_funct_t *funct;

    if (funct_entry->funct_ptr > 0) {
	/* entry points to a function, update the function struct */
	funct = SHMPTR(funct_entry->funct_ptr);
	funct->users--;
    }
    /* clear contents of struct */
    funct_entry->funct_ptr = 0;
    funct_entry->arg = 0;
    funct_entry->funct = 0;
    /* add it to free list */
    list_add_after((hal_list_t *) funct_entry, &(hal_data->funct_entry_free));
}

#ifdef RTAPI
static void free_thread_struct(hal_thread_t * thread)
{
    hal_funct_entry_t *funct_entry;
    hal_list_t *list_root, *list_entry;
/*! \todo Another #if 0 */
#if 0
    int *prev, next;
    char time[HAL_NAME_LEN + 1], tmax[HAL_NAME_LEN + 1];
    hal_param_t *param;
#endif

    /* if we're deleting a thread, we need to stop all threads */
    hal_data->threads_running = 0;
    /* and stop the task associated with this thread */
    rtapi_task_pause(thread->task_id);
    rtapi_task_delete(thread->task_id);
    /* clear contents of struct */
    thread->uses_fp = 0;
    thread->period = 0;
    thread->priority = 0;
    thread->task_id = 0;
    /* clear the function entry list */
    list_root = &(thread->funct_list);
    list_entry = list_next(list_root);
    while (list_entry != list_root) {
	/* entry found, save pointer to it */
	funct_entry = (hal_funct_entry_t *) list_entry;
	/* unlink it, point to the next one */
	list_entry = list_remove_entry(list_entry);
	/* free the removed entry */
	free_funct_entry_struct(funct_entry);
    }
/*! \todo Another #if 0 */
#if 0
/* Currently these don't get created, so we don't have to worry
   about deleting them.  They will come back when the HAL refactor
   is done, at that time this code or something like it will be
   needed.
*/
    /* need to delete <thread>.time and <thread>.tmax params */
    rtapi_snprintf(time, sizeof(time), "%s.time", thread->name);
    rtapi_snprintf(tmax, sizeof(tmax), "%s.tmax", thread->name);
    /* search the parameter list for those parameters */
    prev = &(hal_data->param_list_ptr);
    next = *prev;
    while (next != 0) {
	param = SHMPTR(next);
	/* does this param match either name? */
	if ((strcmp(param->name, time) == 0)
	    || (strcmp(param->name, tmax) == 0)) {
	    /* yes, unlink from list */
	    *prev = param->next_ptr;
	    /* and delete it */
	    free_param_struct(param);
	} else {
	    /* no match, try the next one */
	    prev = &(param->next_ptr);
	}
	next = *prev;
    }
#endif
    thread->name[0] = '\0';
    /* add thread to free list */
    thread->next_ptr = hal_data->thread_free_ptr;
    hal_data->thread_free_ptr = SHMOFF(thread);
}
#endif /* RTAPI */

/***********************************************************************
*                     HAL namespace support                            *
************************************************************************/
// outline
// - all foreign HAL data segments must be preattached or cannot be linked into
//
// - all 'HAL environments' (RT env + each userland HAL comp process) exports
// the mappings it 'sees' at the component level, in the HAL data segment
// this is a table of:
// {pid, flag, [instance, local address of haldata[instance]]}
//
// the linking flow is now for 'net foo:signame comp.pin'
// determine if foreign namespace attached, fail if not
// determine if foreign signal exists, record type and offset
// determine owning comp of pin
// check if comp has foreign namespace attached, fail if not
// obtain base address of haldata[instance] as seen by comp
// adjust pin address to haldata[instance] + offset(foo:signame)


// is there an aliasing problem (different comps see pin at different addresses?
// ---> API change for halpr_find_pin
//   take into account pin might point into remote HAL namespace
//   rename funcs, map on demand

// main loop function for userland HAL comps: track mappings
// -- in haldata keep bitmap of mapped NS's
// -- compare with local map of NS's
// -- attach missing
// -- update own set of mapped NS'es
//    (how to make them visible? bitmap in comp structure)
// -- in hal_link(), update bitmap in hal_data
//    wait for bitmap in comp to mirror this bitmap
//    fail with programming hint to update mainloop with function


// flow for halcmd linking a userland pin into a remote NS:
// -- execute RT command: attach 23 foo
// -- wait for haldata->mapped_NSes bitmap to include 23
// -- resolve owing component
// -- wait for owning comp to have 23 mapped in comp structure
// -- fail with programming note if not present
// -- execute hal_link() such that comp's pin points to remote HAL ns


// shutdown
// for all RT comps:
//    unlink pins pointing to remote NS'es
//    update bitmap in hal_data
// for all Userland components:
// if hal_remote_pins(comp):
//     unlink this pin
//
// superclean:
// all unlinked: update mapping to own NS only
// wait for all comps to have updated maps
// shutdown

// mapping of rings:
// extend _rtapi_ring_attach() by instance param
// record instance in ringbuffer descriptor
// attaching comp needs to check whether targe instance attached


#ifdef ULAPI
#if 0

enum context_type {
    CONTEXT_INVALID = 0,
    CONTEXT_RT = 1,
    CONTEXT_USERLAND = 2,
};

// visible in the per-namespace HAL data segment:
typedef struct {
    unsigned long flags;
    int refcount;  // number of objects referenced in this namespace
    void *haldata; // RT space attach address of that instance
 } hal_namespace_t;

typedef struct {
    int next_ptr;       // next contex in list
    unsigned long type; // context_type
    int pid;
    // we use a bitmap because it's going to be tested frequently
    // conceptually it belongs into namespaces[] as a bool attribute
    _DECLARE_BITMAP(visible_namespaces,MAX_INSTANCES);
    // the map of attached HAL namespaces of this context
    hal_namespace_t namespaces[MAX_INSTANCES];
    int refcount;  // the number of active comps in this context
    // any rtapi or otherwise information needed to attach/detach the segment
} hal_context_t;

typedef struct {
    char name[HAL_NAME_LEN + 1];
} hal_namespace_descriptor_t;

hal_data:
// each component is expected to mirror this
_DECLARE_BITMAP(required_namespaces,MAX_INSTANCES);

// the symbolic names for foreign namespaces - only once HAL
hal_namespace_descriptor_t nsdesc[MAX_INSTANCES];
int refcount;

#endif


// this ULAPI function adds a foreign ns to the requested namespaces map
// in hal_data, and gives it a name, which is a strictly local operation
//
// it does not actually attach a foreign HAL data segment, which is a
// an operation local to a context and differs according to context type
//
// used: by halcmd
int hal_namespace_add(int instance, const char *name)
{
    int halkey __attribute__((cleanup(halpr_autorelease_mutex)));

    rtapi_mutex_get(&(hal_data->mutex));
    if (name == NULL) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d no name parameter for instance id %d\n",
			rtapi_instance, instance);
	return -EINVAL;
    }
    if (instance == rtapi_instance) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d cannot attach self (instance %d)\n",
			rtapi_instance, instance);
	return -EINVAL;
    }
    if (!hal_valid_instance(instance)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d invalid instance id %d\n", rtapi_instance, instance);
	return -EINVAL;
    }
    // test if in required map already, return if yes
    if (RTAPI_BIT_TEST(hal_data->requested_namespaces,instance)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d instance %d alredy requested as %s\n",
			rtapi_instance,instance,
			hal_data->nsdesc[instance].name);
	return -EEXIST;

    }
    halkey = OS_KEY(HAL_KEY, instance);

    // test if foreign instance exists
    if (!rtapi_shmem_exists(halkey)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d instance %d/%s does not exist (HAL key 0x%8.8x)\n",
			rtapi_instance, instance, name, halkey);
	return -ENOENT;
    }
    // mark as requested
    RTAPI_BIT_SET(hal_data->requested_namespaces,instance);
    strncpy(hal_data->nsdesc[instance].name, name, HAL_NAME_LEN);
    return 0;
}


int halpr_pin_crossrefs(unsigned long *bitmap, hal_pin_sig_xref_cb pin_xlink_callback)
{
    hal_context_t *ctx = THIS_CONTEXT();
    hal_pin_t *pin;
    hal_sig_t *sig;
    int i, next, retval;
    hal_data_t *pin_hal_data, *sig_hal_data;
    extern hal_context_t *ul_context;
    char *pin_prefix, *sig_prefix;
    int sum = 0;

    for (i = 0; i < MAX_INSTANCES; i++) {
	if (RTAPI_BIT_TEST(bitmap,i) &&
	    RTAPI_BIT_TEST(ctx->visible_namespaces,i)) {
	    pin_hal_data = ctx->namespaces[i].haldata;
	    pin_prefix = hal_data->nsdesc[i].name;
	    rtapi_mutex_get(&(pin_hal_data->mutex));

	    next = pin_hal_data->pin_list_ptr;
	    while (next != 0) {
		pin = SHMPTR_IN(pin_hal_data, next);
		if (pin->signal && // linked
		    (pin->signal_inst != i)) { // pointing outside
		    sig_hal_data = ctx->namespaces[pin->signal_inst].haldata;
		    rtapi_mutex_get(&(sig_hal_data->mutex));

		    sig_prefix = sig_hal_data->nsdesc[pin->signal_inst].name;
		    sig = SHMPTR_IN(sig_hal_data, pin->signal);

		    retval = pin_xlink_callback(i, pin_prefix, sig_prefix,
						pin, sig);
		    if (retval >= 0)
			sum += retval;
		    else
			i = MAX_INSTANCES; // break while unlocking properly
		    rtapi_mutex_give(&(sig_hal_data->mutex));
		}
		next = pin->next_ptr;
	    }
	    rtapi_mutex_give(&(pin_hal_data->mutex));
	}
    }
    return sum;
}

// trivial callback to count ring crosslinks
static int count_ring_xref(int ra_inst, char *ra_prefix, char *ring_prefix,
			  hal_ring_attachment_t *ra)
{
    return 1;
}
// detect inter-instance ring buffers
// those exist if:
// a local ring attachment refers to a remote instance (ring_inst != rtapi_instance)
// a remote ring attachment refers to the local instance
int halpr_ring_crossrefs(unsigned long *bitmap, hal_ring_xref_cb ring_xlink_callback)
{
    hal_data_t *ra_hal_data, *ring_hal_data;
    hal_context_t *ctx = THIS_CONTEXT();
    char *ra_prefix, *ring_prefix;
    int i, next, retval;
    hal_ring_attachment_t *hra;
    int sum = 0;

    for (i = 0; i < MAX_INSTANCES; i++) {
	if (RTAPI_BIT_TEST(bitmap,i) &&
	    RTAPI_BIT_TEST(ctx->visible_namespaces,i)) {

	    ra_hal_data = ctx->namespaces[i].haldata;
	    rtapi_mutex_get(&(ra_hal_data->mutex));

	    ra_prefix = ra_hal_data->nsdesc[i].name;
	    next = ra_hal_data->ring_attachment_list_ptr;
	    while (next != 0) {
		hra = SHMPTR_IN(ra_hal_data, next);
		if (hra->ring_inst != i) {
		    // a cross-linked ring attachment
		    ring_hal_data = ctx->namespaces[hra->ring_inst].haldata;
		    rtapi_mutex_get(&(ring_hal_data->mutex));
		    ring_prefix = ring_hal_data->nsdesc[hra->ring_inst].name;

		    retval = ring_xlink_callback(i, ra_prefix, ring_prefix, hra);
		    rtapi_mutex_give(&(ring_hal_data->mutex));

		    if (retval >= 0)
			sum += retval;
		    else
			i = MAX_INSTANCES; // break while unlocking properly
		}
		next = hra->next_ptr;
	    }
	    rtapi_mutex_give(&(ra_hal_data->mutex));
	}
    }
    return sum;
}

// trivial callback to count pin crosslinks
static int count_pin_xref(int pin_inst, char *pin_prefix, char *sig_prefix,
		      hal_pin_t *pin, hal_sig_t *sig)
{
    return 1;
}

// break the association between instance and us (rtapi_instance)
// before we can do so check for crossrefs, which are:
// - pins in the other ns linking to sigs in our ns
// - pins in our ns linking to sigs in the other ns
// - any rings cross-referenced from remote to us
// - any rings cross-referenced from us into remote
int hal_namespace_disassociate(int remote_instance)
{
    int pin_xrefs, ring_xrefs;
    hal_context_t  *ctx = THIS_CONTEXT();
    hal_data_t *remote_hal_data;
    RTAPI_DECLARE_BITMAP(testmap,MAX_INSTANCES);

    if (!hal_valid_instance(remote_instance)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d hal_namespace_disassociate(): invalid instance %d\n",
			rtapi_instance, remote_instance);
	return -EINVAL;
    }

    // check for crossrefs between us and the remote instance
    RTAPI_ZERO_BITMAP(testmap,MAX_INSTANCES);
    RTAPI_BIT_SET(testmap, rtapi_instance);
    RTAPI_BIT_SET(testmap, remote_instance);

    pin_xrefs = halpr_pin_crossrefs(testmap,count_pin_xref);
    ring_xrefs = halpr_ring_crossrefs(testmap,count_ring_xref);

    if (pin_xrefs) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d cannot disassociate instance %d - %d pin%s crosslinked\n",
			rtapi_instance, remote_instance,
			pin_xrefs, pin_xrefs == 1 ? "" : "s");
    }
    if (ring_xrefs) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d cannot disassociate instance %d - %d ring%s cross-attached\n",
			rtapi_instance, remote_instance,
			ring_xrefs, ring_xrefs == 1 ? "" : "s");
    }
    if (pin_xrefs || ring_xrefs)
	return -EBUSY;

    // break mutual association of namespaces:
    remote_hal_data = ctx->namespaces[remote_instance].haldata;

    rtapi_mutex_get(&(remote_hal_data->mutex));
    // unmark our instance in the remote ns
    RTAPI_BIT_CLEAR(remote_hal_data->requested_namespaces,rtapi_instance);
    // clear our instance name in remote ns map
    remote_hal_data->nsdesc[rtapi_instance].name[0] = '\0';
    rtapi_mutex_give(&(remote_hal_data->mutex));

    rtapi_mutex_get(&(hal_data->mutex));
    // unmark remote instance in our ns
    RTAPI_BIT_CLEAR(hal_data->requested_namespaces,remote_instance);
    hal_data->nsdesc[remote_instance].name[0] = '\0';
    rtapi_mutex_give(&(hal_data->mutex));

    // now sync, this should unmap the ns
    hal_namespace_sync();

    return 0;
}

// this ULAPI function associates a foreign ns
// it attaches its HAL segment as needed, and sets the requested ns bits
// in the local and remote ns'es
// it will sync the namespaces in the local context, and set the prefix name
// in the local ns from the remote
// it pass the remote prefix if non-NULL; space must be allocated
// used: by halcmd
int hal_namespace_associate(int instance, char *prefix)
{
    int halkey __attribute__((cleanup(halpr_autorelease_mutex)));
    hal_context_t  *ctx = THIS_CONTEXT();
    hal_data_t *remote_hal_data;
    const char *remote_name;

    rtapi_mutex_get(&(hal_data->mutex));
    if (instance == rtapi_instance) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d cannot associate self (instance %d)\n",
			rtapi_instance, instance);
	return -EINVAL;
    }
    if (!hal_valid_instance(instance)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d invalid instance id %d\n", rtapi_instance, instance);
	return -EINVAL;
    }
    // test if in required map already, return if yes
    if (RTAPI_BIT_TEST(hal_data->requested_namespaces,instance)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d instance %d alredy requested as %s\n",
			rtapi_instance,instance,
			hal_data->nsdesc[instance].name);
	return -EEXIST;

    }
    // test if foreign instance exists
    halkey = OS_KEY(HAL_KEY, instance);
    if (!rtapi_shmem_exists(halkey)) {
	rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d instance %d does not exist (HAL key 0x%8.8x)\n",
			rtapi_instance, instance, halkey);
	return -ENOENT;
    }
    // mark as requested
    RTAPI_BIT_SET(hal_data->requested_namespaces,instance);
    // now sync, this should map in the ns locally
    hal_namespace_sync();
    remote_hal_data = ctx->namespaces[instance].haldata;

    // mark our instance as requested in the remote ns
    RTAPI_BIT_SET(remote_hal_data->requested_namespaces,rtapi_instance);

    // fetch the foreign instance name
    remote_name = remote_hal_data->nsdesc[instance].name;

    // record foreign name locally
    strncpy(hal_data->nsdesc[instance].name, remote_name, HAL_NAME_LEN);

    // record our name in remote ns
    strncpy(remote_hal_data->nsdesc[rtapi_instance].name,
	    hal_data->nsdesc[rtapi_instance].name, HAL_NAME_LEN);

    if (prefix)
	strcpy(prefix, remote_name);
    return 0;
}


#if 0
// to prevent dangling pins into detached namespaces,
static int check_unreferenced(int instance)
{
    return 0;
}
#endif
int hal_namespace_delete(const char *name)
{
    int halkey __attribute__((cleanup(halpr_autorelease_mutex)));
    int i;

    rtapi_mutex_get(&(hal_data->mutex));
    for (i = 0; i < MAX_INSTANCES; i++) {
	if (RTAPI_BIT_TEST(hal_data->requested_namespaces,i) &&
	    !strcmp(name, hal_data->nsdesc[i].name)) {

	    // test for not deleting self - not good.
	    if (i == rtapi_instance)
		 rtapi_print_msg(RTAPI_MSG_ERR,
				 "HAL_LIB:%d cannot delete own namespace %s\n",
				 rtapi_instance, name);
	    else
		RTAPI_BIT_CLEAR(hal_data->requested_namespaces,i);
	    return 0;
	}
    }
    rtapi_print_msg(RTAPI_MSG_ERR,
			"HAL_LIB:%d hal_namespace_delete: no such namespace %s\n",
			rtapi_instance, name);
    return -ENOENT;
}


// bump refcount 'over there' under remote lock
// usage iterator - retrieve remote references in pins, signals
// list of parts needed:

#endif // ULAPI

int hal_namespace_sync(void)
{
    hal_context_t *ctx = THIS_CONTEXT();
    hal_namespace_t *ns;
    int i;

    // compare bitmaps - NB: assume < 32 instances!
    if (hal_data->requested_namespaces != ctx->visible_namespaces) {
	for (i = 0; i < MAX_INSTANCES; i++) {
	    if (RTAPI_BIT_TEST(hal_data->requested_namespaces,i) ^
		RTAPI_BIT_TEST(ctx->visible_namespaces,i)) {
		ns = &ctx->namespaces[i];

		if (RTAPI_BIT_TEST(hal_data->requested_namespaces,i) && //requested
		    ns->shmem_id >= 0) {  // not marked as dead
		    // use the HAL library module ID as owner reference for the foreign HAL segment
		    if ((ns->shmem_id = rtapi_shmem_new_inst(HAL_KEY,i, lib_module_id, 0)) < 0) {
			rtapi_print_msg(RTAPI_MSG_ERR,
					"HAL:%d hal_namespace_sync: failed to attach HAL segment of instance %d\n",
					rtapi_instance, i+1);
			// ns->shmem_id < 0 means 'marked as dead'
			// FIXME: set ns-->shmem_id to 0 in namespace_detach/attach so retry is possible
		    } else {
			// mark as visible in this context
			if (rtapi_shmem_getptr_inst(ns->shmem_id, i, &ns->haldata)) {
			    rtapi_print_msg(RTAPI_MSG_ERR,
					"HAL:%d hal_namespace_sync: failed to getptr HAL segment of instance %d\n",
					rtapi_instance, i+1);
			} else
			    RTAPI_BIT_SET(ctx->visible_namespaces,i);
		    }
		} else {
		    rtapi_print_msg(RTAPI_MSG_DBG,
				    "sync:%d need to detach %d/%s\n",getpid(),
				    i+1, hal_data->nsdesc[i].name);
		    if (rtapi_shmem_delete_inst(ns->shmem_id, i, lib_module_id)) {
			rtapi_print_msg(RTAPI_MSG_ERR,
					"HAL:%d hal_namespace_sync: failed to detach HAL segment of instance %d\n",
					rtapi_instance, i+1);

		    }
		    // FIXME mark ns-->shmem_id as 'bad'
		    // mark as invisible in this context anyway
		    ns->haldata = 0;
		    RTAPI_BIT_CLEAR(ctx->visible_namespaces,i);
		}
	    }
	}
    }
    return 0;
}
#ifdef RTAPI
EXPORT_SYMBOL(hal_namespace_sync);
#endif

// this needs eyeballs: circular deadlock prevention
// always lock hal_data segments in instance order
int halpr_lock_ordered(int inst1, int inst2)
{
    hal_data_t *hd1,*hd2;
    hal_context_t *ctx = THIS_CONTEXT();

    if (!hal_valid_instance(inst1) ||
	!hal_valid_instance(inst2) ||
	!RTAPI_BIT_TEST(hal_data->requested_namespaces,inst1) ||
	!RTAPI_BIT_TEST(hal_data->requested_namespaces,inst2))
	return -EINVAL;
    hd1 = ctx->namespaces[inst1].haldata;
    hd2 = ctx->namespaces[inst2].haldata;
    if (hd1 == hd2) {
	// special case: self reference (inst1 == inst2)
	// so lock once only
	rtapi_mutex_get(&(hd1->mutex));
    } else {
	if (inst2 > inst2) {
	    rtapi_mutex_get(&(hd2->mutex));
	    rtapi_mutex_get(&(hd1->mutex));
	} else {
	    rtapi_mutex_get(&(hd1->mutex));
	    rtapi_mutex_get(&(hd2->mutex));
	}
    }
    return 0;
}

// and unlock in instance order, while properly
// dealing with the case inst1 == inst2
int halpr_unlock_ordered(int inst1, int inst2)
{
    hal_data_t *hd1,*hd2;
    hal_context_t *ctx = THIS_CONTEXT();

    if (!hal_valid_instance(inst1) ||
	!hal_valid_instance(inst2) ||
	!RTAPI_BIT_TEST(hal_data->requested_namespaces,inst1) ||
	!RTAPI_BIT_TEST(hal_data->requested_namespaces,inst2))
	return -EINVAL;
    hd1 = ctx->namespaces[inst1].haldata;
    hd2 = ctx->namespaces[inst2].haldata;
    if (hd1 == hd2) {
	// special case: self reference (inst1 == inst2)
	// so unlock once only
	rtapi_mutex_give(&(hd1->mutex));
    } else {
	if (inst2 > inst2) {
	    rtapi_mutex_give(&(hd2->mutex));
	    rtapi_mutex_give(&(hd1->mutex));
	} else {
	    rtapi_mutex_give(&(hd1->mutex));
	    rtapi_mutex_give(&(hd2->mutex));
	}
    }
    return 0;
}

/***********************************************************************
*                     HAL data segment attach & detach                 *
*                                                                      *
* in place to work around a purported RTAI issue with shared memory    *
************************************************************************/

#ifdef ULAPI

// this is now delayed to first hal_init() in this process
int hal_rtapi_attach()
{
    int retval;
    void *mem;
    char rtapi_name[RTAPI_NAME_LEN + 1];
    hal_namespace_t *np;

    if (lib_mem_id < 0) {
	rtapi_print_msg(RTAPI_MSG_DBG, "HAL: initializing hal_lib\n");
	rtapi_snprintf(rtapi_name, RTAPI_NAME_LEN, "HAL_LIB_%d", (int)getpid());
	lib_module_id = rtapi_init(rtapi_name);
	if (lib_module_id < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: could not not initialize RTAPI - realtime not started?\n");
	    return -EINVAL;
	}

	if (global_data == NULL) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
			    "HAL: ERROR: RTAPI shutting down - exiting\n");
	    exit(1);
	}

	/* get HAL shared memory block from RTAPI */
	lib_mem_id = rtapi_shmem_new(HAL_KEY, lib_module_id, global_data->hal_size);
	if (lib_mem_id < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: could not open shared memory\n");
	    rtapi_exit(lib_module_id);
	    return -EINVAL;
	}
	/* get address of shared memory area */
	retval = rtapi_shmem_getptr(lib_mem_id, &mem);
	if (retval < 0) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: could not access shared memory\n");
	    rtapi_exit(lib_module_id);
	    return -EINVAL;
	}
	/* set up internal pointers to shared mem and data structure */
        hal_shmem_base = (char *) mem;
        hal_data = (hal_data_t *) mem;
	/* perform a global init if needed */
	retval = init_hal_data();
	if ( retval ) {
	    rtapi_print_msg(RTAPI_MSG_ERR,
		"HAL: ERROR: could not init shared memory\n");
	    rtapi_exit(lib_module_id);
	    return -EINVAL;
	}

	rtapi_mutex_get(&(hal_data->mutex));
	// allocate the userland context
	if (ul_context == NULL) { // not yet inited in this process context
	    if ((ul_context = alloc_context_struct(context_pid())) == NULL) {
		rtapi_mutex_give(&(hal_data->mutex));
		rtapi_print_msg(RTAPI_MSG_ERR,
				"HAL: ERROR: cannot allocate userland HAL context for pid %d\n",
				context_pid());
		rtapi_exit(lib_module_id);
		return -EINVAL;
	    }
	}
	if (ul_context->refcount != 0) { // strange..
	    rtapi_print_msg(RTAPI_MSG_INFO,
			    "HAL: ulapi_hal_lib_init: %d comps already active\n",
			    ul_context->refcount);
	}
	// NB: initialisation of ul_context really happens in hal_rtapi_attach()
	// export what we're 'seeing', and where
	RTAPI_BIT_SET(ul_context->visible_namespaces, rtapi_instance);
	np = &ul_context->namespaces[rtapi_instance];
	np->haldata = hal_data;
	rtapi_mutex_give(&(hal_data->mutex));

	rtapi_print_msg(RTAPI_MSG_DBG,
		"HAL: hal_rtapi_attach(): HAL shm segment attached\n");
    }
    return 0;
}

int hal_rtapi_detach()
{
    /* release RTAPI resources */
    if (lib_mem_id > -1) {
	// if they were actually initialized
	rtapi_shmem_delete(lib_mem_id, lib_module_id);
	rtapi_exit(lib_module_id);

	lib_mem_id = 0;
	lib_module_id = -1;
	hal_shmem_base = NULL;
	hal_data = NULL;

	// disable local mappings
	//hal_mappings[rtapi_instance].shmbase = NULL;
	//hal_mappings[rtapi_instance].hal_data = NULL;

	rtapi_print_msg(RTAPI_MSG_DBG,
			"HAL: hal_rtapi_detach(): HAL shm segment detached\n");
    }
    return 0;
}

// ULAPI-side cleanup. Called at shared library unload time as
// a destructor.
static void  __attribute__ ((destructor))  ulapi_hal_lib_cleanup(void)
{
    // detach the HAL data segment
    hal_rtapi_detach();
    // shut down ULAPI
    ulapi_cleanup();
}
#endif


#ifdef RTAPI
/* only export symbols when we're building a kernel module */

EXPORT_SYMBOL(hal_init);
EXPORT_SYMBOL(hal_init_mode);
EXPORT_SYMBOL(hal_ready);
EXPORT_SYMBOL(hal_exit);
EXPORT_SYMBOL(hal_malloc);
EXPORT_SYMBOL(hal_comp_name);

EXPORT_SYMBOL(hal_pin_bit_new);
EXPORT_SYMBOL(hal_pin_float_new);
EXPORT_SYMBOL(hal_pin_u32_new);
EXPORT_SYMBOL(hal_pin_s32_new);
EXPORT_SYMBOL(hal_pin_new);

EXPORT_SYMBOL(hal_pin_bit_newf);
EXPORT_SYMBOL(hal_pin_float_newf);
EXPORT_SYMBOL(hal_pin_u32_newf);
EXPORT_SYMBOL(hal_pin_s32_newf);


EXPORT_SYMBOL(hal_signal_new);
EXPORT_SYMBOL(hal_signal_delete);
EXPORT_SYMBOL(hal_link);
EXPORT_SYMBOL(hal_unlink);

EXPORT_SYMBOL(hal_param_bit_new);
EXPORT_SYMBOL(hal_param_float_new);
EXPORT_SYMBOL(hal_param_u32_new);
EXPORT_SYMBOL(hal_param_s32_new);
EXPORT_SYMBOL(hal_param_new);

EXPORT_SYMBOL(hal_param_bit_newf);
EXPORT_SYMBOL(hal_param_float_newf);
EXPORT_SYMBOL(hal_param_u32_newf);
EXPORT_SYMBOL(hal_param_s32_newf);

EXPORT_SYMBOL(hal_param_bit_set);
EXPORT_SYMBOL(hal_param_float_set);
EXPORT_SYMBOL(hal_param_u32_set);
EXPORT_SYMBOL(hal_param_s32_set);
EXPORT_SYMBOL(hal_param_set);

EXPORT_SYMBOL(hal_set_constructor);

EXPORT_SYMBOL(hal_export_funct);

EXPORT_SYMBOL(hal_create_thread);

EXPORT_SYMBOL(hal_add_funct_to_thread);
EXPORT_SYMBOL(hal_del_funct_from_thread);

EXPORT_SYMBOL(hal_start_threads);
EXPORT_SYMBOL(hal_stop_threads);

EXPORT_SYMBOL(hal_shmem_base);
EXPORT_SYMBOL(halpr_find_comp_by_name);
EXPORT_SYMBOL(halpr_find_pin_by_name);
EXPORT_SYMBOL(halpr_find_sig_by_name);
EXPORT_SYMBOL(halpr_find_param_by_name);
EXPORT_SYMBOL(halpr_find_thread_by_name);
EXPORT_SYMBOL(halpr_find_funct_by_name);
EXPORT_SYMBOL(halpr_find_comp_by_id);

EXPORT_SYMBOL(halpr_find_pin_by_owner);
EXPORT_SYMBOL(halpr_find_param_by_owner);
EXPORT_SYMBOL(halpr_find_funct_by_owner);

EXPORT_SYMBOL(halpr_find_pin_by_sig);
EXPORT_SYMBOL(hal_ring_new);
EXPORT_SYMBOL(hal_ring_detach);
EXPORT_SYMBOL(hal_ring_attach);

EXPORT_SYMBOL(halpr_lock_ordered);
EXPORT_SYMBOL(halpr_unlock_ordered);

#endif /* rtapi */


