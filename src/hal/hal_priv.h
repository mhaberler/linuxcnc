#ifndef HAL_PRIV_H
#define HAL_PRIV_H

/** HAL stands for Hardware Abstraction Layer, and is used by EMC to
    transfer realtime data to and from I/O devices and other low-level
    modules.
*/
/********************************************************************
* Description:  hal_priv.h
*               This file, 'hal_priv.h', contains declarations of 
*               most of the internal data structures used by the HAL.  
*               It is NOT needed by most HAL components.  However, 
*               some components that interact more closely with the 
*               HAL internals, such as "halcmd", need to include this 
*               file.
*
* Author: John Kasunich
* License: GPL Version 2
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
*/

/** This library is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General
    Public License as published by the Free Software Foundation.
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU General Public
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

/***********************************************************************
*                       GENERAL INFORMATION                            *
************************************************************************/

/** The following data structures are used by the HAL but are not part
    of the HAL API and should not be visible to code that uses the HAL.
*/

/** At runtime, the HAL consists of a pile of interconnected data
    structures in a block of shared memory.  There are linked lists
    of components, pins, signals, parameters, functions, and threads.
    Most of the lists are sorted by name, and each of these lists is
    cross linked to the others.  All pins, parameters, functions, and
    threads are linked to the component that created them.  In addition,
    pins are linked to signals, and functions are linked to threads.
    On top of that, when items are deleted, they are stored in (you
    guessed it) linked lists for possible reuse later.

    As a result, the pointer manipulation needed to add or remove
    an item from the HAL gets a little complex and hard to follow.
    Sorry about that...  tread carefully, especially in the
    free_xxx_struct functions.  And mind your mutexes.

    And just to make it more fun: shared memory is mapped differently
    for each process and for the kernel, so you can't just pass pointers
    to shared memory objects from one process to another.  So we use
    integer offsets from the start of shared memory instead of pointers.
    In addition to adding overhead, this makes the code even harder to
    read.  Very annoying!  Like near/far pointers in the old days!
    In areas where speed is critical, we sometimes use both offsets
    (for general access) and pointers (for fast access by the owning
    component only).  The macros below are an attempt to neaten things
    up a little.
*/

#include <rtapi.h>
#include <rtapi_global.h>

#ifdef ULAPI
#include <rtapi_compat.h>
#endif

#include "rtapi_mbarrier.h"	/* memory barrier primitves */

#if defined(BUILD_SYS_USER_DSO)
#include <stdbool.h>
#else // kernel thread styles
#if defined(RTAPI)
#include <linux/types.h>
#else // ULAPI
#include <stdbool.h>
#include <time.h>               /* remote comp timestamps */
#endif
#endif
#include "hal_ring.h"
RTAPI_BEGIN_DECLS

/* SHMPTR(offset) converts 'offset' to a void pointer.
 * the _IN() variants of these macros are for accessing offsets in a
 * remote hal_data segment when using instances.
 */
#define SHMPTR(offset)  ( (void *)( hal_shmem_base + (offset) ) )
#define SHMPTR_IN(hal, offset)  ( (void *)(((char *)hal) + (offset) ) )

/* SHMOFF(ptr) converts 'ptr' to an offset from shared mem base.  */
#define SHMOFF(ptr)     ( ((char *)(ptr)) - hal_shmem_base )
#define SHMOFF_IN(hal, ptr)  (((char *)(ptr)) -   (((char *)hal)))

/* SHMCHK(ptr) verifies that a pointer actually points to a
   location that is part of the HAL shared memory block. */

#define SHMCHK(ptr)  ( ((char *)(ptr)) > (hal_shmem_base) && \
                       ((char *)(ptr)) < (hal_shmem_base + global_data->hal_size) )

/** The good news is that none of this linked list complexity is
    visible to the components that use this API.  Complexity here
    is a small price to pay for simplicity later.
*/

#ifndef MAX
#define MAX(x, y) (((x) > (y))?(x):(y))
#endif


// sizing
#define HAL_NGROUPS              32

// extending this beyond 255 might require adapting rtapi_shmkeys.h
#define HAL_MAX_RINGS	        255
/***********************************************************************
*            PRIVATE HAL DATA STRUCTURES AND DECLARATIONS              *
************************************************************************/

/** HAL "data union" structure
 ** This structure may hold any type of hal data
*/
typedef union {
    hal_bit_t b;
    hal_s32_t s;
    hal_s32_t u;
    hal_float_t f;
} hal_data_u;

/** HAL "list element" data structure.
    This structure is used to implement generic double linked circular
    lists.  Such lists have the following characteristics:
    1) One "dummy" element that serves as the root of the list.
    2) 'next' and 'previous' pointers are never NULL.
    3) Insertion and removal of elements is clean and fast.
    4) No special case code to deal with empty lists, etc.
    5) Easy traversal of the list in either direction.
    This structure has no data, only links.  To use it, include it
    inside a larger structure.
*/
typedef struct {
    int next;			/* next element in list */
    int prev;			/* previous element in list */
} hal_list_t;

/** HAL "oldname" data structure.
    When a pin or parameter gets an alias, this structure is used to
    store the original name.
*/
typedef struct {
    int next_ptr;		/* next struct (used for free list only) */
    char name[HAL_NAME_LEN + 1];	/* the original name */
} hal_oldname_t;


// visible in the per-instance HAL data segment as an array
// indexed by instance number:
typedef struct {
    unsigned long flags;
    int shmem_id;
    int refcount;  // number of objects referenced in this namespace
    void *haldata; // address as seen from this context
 } hal_namespace_t;

// per-process/kernel mappings
// indexed by rtapi_instance of mapped namespace
//  extern hal_namespace_map_t hal_mappings[];

// the hal_context_t structure describes a HAL context.
//
// (each) RT space is a context (like rtapi_app or the kernel)
// as well as each userland process owning one or several HAL components.
//
// the context carries information about which hal namespaces it sees
// attach/detach operations happen at the context level
// each component belongs to exactly one context
// The pid is the unique key to a context.
//
// a context is created by the first component in a given context.
// the relation of contexts to components is 1:n.
// any component lives in exactly one context and refers back to it by the context_ptr.

enum context_type {
    CONTEXT_INVALID = 0,
    CONTEXT_RT = 1,
    CONTEXT_USERLAND = 2,
};
typedef struct {
    int next_ptr;       // next context in list
    unsigned long type; // context_type
    int pid;

    // we use a bitmap because it's going to be tested frequently
    // conceptually it belongs into namespaces[] as a bool attribute
    RTAPI_DECLARE_BITMAP(visible_namespaces,MAX_INSTANCES);
    // the map of attached HAL namespaces of this context
    hal_namespace_t namespaces[MAX_INSTANCES];
    int refcount;  // the number of active comps in this context
    // any rtapi or otherwise information needed to attach/detach the segment
} hal_context_t;

typedef struct {
    char name[HAL_NAME_LEN + 1];
} hal_namespace_descriptor_t;

#ifdef RTAPI
hal_context_t *rt_context;
#define THIS_CONTEXT() rt_context
#else
hal_context_t *ul_context;
#define THIS_CONTEXT() ul_context
#endif

extern int hal_namespace_sync(void);
typedef int (*hal_namespace_sync_t)(void);

#ifdef ULAPI
extern int hal_namespace_associate(int instance, char *prefix);
extern int hal_namespace_disassociate(int instance);
#endif

/* Master HAL data structure
   There is a single instance of this structure in the machine.
   It resides at the base of the HAL shared memory block, where it
   can be accessed by both realtime and non-realtime versions of
   hal_lib.c.  It contains pointers (offsets) to other data items
   in the area, as well as some housekeeping data.  It is the root
   structure for all data in the HAL.
*/
typedef struct {
    int version;		/* version code for structs, etc */
    unsigned long mutex;	/* protection for linked lists, etc. */

    // each context is expected to attach these and make the fact
    // visible in its contex descriptor
    RTAPI_DECLARE_BITMAP(requested_namespaces,MAX_INSTANCES);

    // the symbolic names for foreign namespaces - only once HAL
    hal_namespace_descriptor_t nsdesc[MAX_INSTANCES];

    // every other HAL namespace referencing this namspace is expected to
    // increase this on attach, and decrease this on detach - so we know
    // it is unsafe to destroy the shm segment
    int refcount;

    hal_s32_t shmem_avail;	/* amount of shmem left free */
    constructor pending_constructor;
			/* pointer to the pending constructor function */
    char constructor_prefix[HAL_NAME_LEN+1];
			        /* prefix of name for new instance */
    char constructor_arg[HAL_NAME_LEN+1];
			        /* prefix of name for new instance */
    int shmem_bot;		/* bottom of free shmem (first free byte) */
    int shmem_top;		/* top of free shmem (1 past last free) */
    int comp_list_ptr;		/* root of linked list of components */
    int pin_list_ptr;		/* root of linked list of pins */
    int sig_list_ptr;		/* root of linked list of signals */
    int param_list_ptr;		/* root of linked list of parameters */
    int funct_list_ptr;		/* root of linked list of functions */
    int thread_list_ptr;	/* root of linked list of threads */
    long base_period;		/* timer period for realtime tasks */
    int threads_running;	/* non-zero if threads are started */
    int oldname_free_ptr;	/* list of free oldname structs */
    int comp_free_ptr;		/* list of free component structs */
    int pin_free_ptr;		/* list of free pin structs */
    int sig_free_ptr;		/* list of free signal structs */
    int param_free_ptr;		/* list of free parameter structs */
    int funct_free_ptr;		/* list of free function structs */
    hal_list_t funct_entry_free;	/* list of free funct entry structs */
    int thread_free_ptr;	/* list of free thread structs */
    int exact_base_period;      /* if set, pretend that rtapi satisfied our
				   period request exactly */
    unsigned char lock;         /* hal locking, can be one of the HAL_LOCK_* types */

    // since rings are realy just glorified named shm segments, allocate by map
    // this gets around the unexposed rtapi_data segment in userland flavors
    RTAPI_DECLARE_BITMAP(rings, HAL_MAX_RINGS);

    int group_list_ptr;	        /* list of group structs */
    int group_free_ptr;	        /* list of free group structs */

    int ring_list_ptr;          /* list of ring structs */
    int ring_free_ptr;          /* list of free ring structs */

    int ring_attachment_list_ptr;   /* list of ring attachment structs */
    int ring_attachment_free_ptr;   /* list of free ring attachment structs */

    int member_list_ptr;	/* list of member structs */
    int member_free_ptr;	/* list of free member structs */

    int context_list_ptr;       /* list of active contexts */
    int context_free_ptr;       /* list of free context structs */
} hal_data_t;


/** HAL 'component' data structure.
    This structure contains information that is unique to a HAL component.
    An instance of this structure is added to a linked list when the
    component calls hal_init().
*/
typedef struct {
    int next_ptr;		/* next component in the list */
    int comp_id;		/* component ID (RTAPI module id) */
    int mem_id;			/* RTAPI shmem ID used by this comp */
    int type;			/* one of: TYPE_RT, TYPE_USER, TYPE_INSTANCE, TYPE_REMOTE */
    int state;                  /* one of: COMP_INITIALIZING, COMP_UNBOUND, */
                                /* COMP_BOUND, COMP_READY */
    // the next two should be time_t, but kernel wont like them
    // so fudge it as long int
    long int last_update;            /* timestamp of last remote update */
    long int last_bound;             /* timestamp of last bind operation */
    long int last_unbound;           /* timestamp of last unbind operation */
    int pid;			     /* PID of component (user components only) */
    char name[HAL_NAME_LEN + 1];     /* component name */
    constructor make;
    int insmod_args;		/* args passed to insmod when loaded */
    int context_ptr;		/* reference to enclosing context */
} hal_comp_t;

/** HAL 'pin' data structure.
    This structure contains information about a 'pin' object.
*/
typedef struct {
    int next_ptr;		/* next pin in linked list */
    int data_ptr_addr;		/* address of pin data pointer */
    int signal_inst;            /* the instance the signal resides in */
    int owner_ptr;		/* component that owns this pin */
    int signal;			/* signal to which pin is linked */
    hal_data_u dummysig;	/* if unlinked, data_ptr points here */
    int oldname;		/* old name if aliased, else zero */
    hal_type_t type;		/* data type */
    hal_pin_dir_t dir;		/* pin direction */
    char name[HAL_NAME_LEN + 1];	/* pin name */
#ifdef USE_PIN_USER_ATTRIBUTES
    double epsilon;
    int flags;
#endif
} hal_pin_t;

/** HAL 'signal' data structure.
    This structure contains information about a 'signal' object.
*/
typedef struct {
    int next_ptr;		/* next signal in linked list */
    int data_ptr;		/* offset of signal value */
    hal_type_t type;		/* data type */
    int readers;		/* number of input pins linked */
    int writers;		/* number of output pins linked */
    int bidirs;			/* number of I/O pins linked */
    char name[HAL_NAME_LEN + 1];	/* signal name */
} hal_sig_t;

/** HAL 'parameter' data structure.
    This structure contains information about a 'parameter' object.
*/
typedef struct {
    int next_ptr;		/* next parameter in linked list */
    int data_ptr;		/* offset of parameter value */
    int owner_ptr;		/* component that owns this signal */
    int oldname;		/* old name if aliased, else zero */
    hal_type_t type;		/* data type */
    hal_param_dir_t dir;	/* data direction */
    char name[HAL_NAME_LEN + 1];	/* parameter name */
} hal_param_t;

/** the HAL uses functions and threads to handle synchronization of
    code.  In general, most control systems need to read inputs,
    perform control calculations, and write outputs, in that order.
    A given component may perform one, two, or all three of those
    functions, but usually code from several components will be
    needed.  Components make that code available by exporting
    functions, then threads are used to run the functions in the
    correct order and at the appropriate rate.

    The following structures implement the function/thread portion
    of the HAL API.  There are two linked lists, one of functions,
    sorted by name, and one of threads, sorted by execution freqency.
    Each thread has a linked list of 'function entries', structs
    that identify the functions connected to that thread.
*/

typedef struct {
    int next_ptr;		/* next function in linked list */
    int uses_fp;		/* floating point flag */
    int owner_ptr;		/* component that added this funct */
    int reentrant;		/* non-zero if function is re-entrant */
    int users;			/* number of threads using function */
    void *arg;			/* argument for function */
    void (*funct) (void *, long);	/* ptr to function code */
    hal_s32_t runtime;		/* duration of last run, in nsec */
    hal_s32_t maxtime;		/* duration of longest run, in nsec */
    char name[HAL_NAME_LEN + 1];	/* function name */
} hal_funct_t;

typedef struct {
    hal_list_t links;		/* linked list data */
    void *arg;			/* argument for function */
    void (*funct) (void *, long);	/* ptr to function code */
    int funct_ptr;		/* pointer to function */
} hal_funct_entry_t;

typedef struct {
    int next_ptr;		/* next thread in linked list */
    int uses_fp;		/* floating point flag */
    long int period;		/* period of the thread, in nsec */
    int priority;		/* priority of the thread */
    int task_id;		/* ID of the task that runs this thread */
    hal_s32_t runtime;		/* duration of last run, in nsec */
    hal_s32_t maxtime;		/* duration of longest run, in nsec */
    hal_list_t funct_list;	/* list of functions to run */
    char name[HAL_NAME_LEN + 1];	/* thread name */
    int cpu_id;                 /* cpu to bind on, or -1 */
} hal_thread_t;

typedef struct {
    int next_ptr;		/* next member in linked list */
    int sig_member_ptr;          /* offset of hal_signal_t  */
    int signal_inst;             // if signal in foreign instance
    int group_member_ptr;        /* offset of hal_group_t (nested) */
    int userarg1;                /* interpreted by using layer */
    double epsilon;
} hal_member_t;

typedef struct {
    int next_ptr;		/* next group in free list */
    int refcount;               /* advisory by using code */
    int userarg1;	        /* interpreted by using layer */
    int userarg2;	        /* interpreted by using layer */
    //  int serial;                 /* incremented each time a signal is added/deleted*/
    char name[HAL_NAME_LEN + 1];	/* group name */
    int member_ptr;             /* list of group members */
} hal_group_t;

/* IMPORTANT:  If any of the structures in this file are changed, the
   version code (HAL_VER) must be incremented, to ensure that 
   incompatible utilities, etc, aren't used to manipulate data in
   shared memory.
*/

/* Historical note: in versions 2.0.0 and 2.0.1 of EMC, the key was
   0x48414C21, and instead of the structure starting with a version
   number, it started with a fixed magic number.  Mixing binaries or
   kernel modules from those releases with newer versions will result
   in two shmem regions being open, and really strange results (but 
   should _not_ result in segfaults or other crash type problems).
   This is unfortunate, but I can't retroactively make the old code
   detect version mismatches.  The alternative is worse: if the new
   code used the same shmem key, the result would be segfaults or
   kernel oopses.

   The use of version codes  means that any subsequent changes to
   the structs will be fully protected, with a clean shutdown and
   meaningfull error messages in case of a mismatch.
*/
#include "rtapi_shmkeys.h"
#define HAL_VER   0x0000000C	/* version code */
//#define HAL_SIZE  262000

/* These pointers are set by hal_init() to point to the shmem block
   and to the master data structure. All access should use these
   pointers, they takes into account the mapping of shared memory
   into either kernel or user space.  (The HAL kernel module and
   each HAL user process have their own copy of these vars,
   initialized to match that process's memory mapping.)
*/

extern char *hal_shmem_base;
extern hal_data_t *hal_data;

/***********************************************************************
*            PRIVATE HAL FUNCTIONS - NOT PART OF THE API               *
************************************************************************/

/** None of these functions get or release any mutex.  They all assume
    that the mutex has already been obtained.  Calling them without
    having the mutex may give incorrect results if other processes are
    accessing the data structures at the same time.
*/

/** These functions are used to manipulate double-linked circular lists.
    Every list entry has pointers to the next and previous entries.
    The pointers are never NULL.  If an entry is not in a list its
    pointers point back to itself (which effectively makes it a list
    with only one entry)

    'list_init_entry()' sets the pointers in the list entry to point
    to itself - making it a legal list with only one entry. It should
    be called when a list entry is first allocated.

    'list_prev()' and 'list_next()' simply return a pointer to the
    list entry that precedes or follows 'entry' in the list. If there
    is only one element in the list, they return 'entry'.

    'list_add_after()' adds 'entry' immediately after 'prev'.
    Entry must be a single entry, not in a larger list.

    'list_add_before()' adds 'entry' immediately before 'next'.
    Entry must be a single entry, not in a larger list.

    'list_remove_entry()' removes 'entry' from any list it may be in.
    It returns a pointer to the next entry in the list.  If 'entry'
    was the only entry in the list, it returns 'entry'.
*/
void list_init_entry(hal_list_t * entry);
hal_list_t *list_prev(hal_list_t * entry);
hal_list_t *list_next(hal_list_t * entry);
void list_add_after(hal_list_t * entry, hal_list_t * prev);
void list_add_before(hal_list_t * entry, hal_list_t * next);
hal_list_t *list_remove_entry(hal_list_t * entry);

/** The 'find_xxx_by_name()' functions search the appropriate list for
    an object that matches 'name'.  They return a pointer to the object,
    or NULL if no matching object is found.
*/
extern hal_comp_t *halpr_find_comp_by_name(const char *name);
extern hal_pin_t *halpr_find_pin_by_name(const char *name);
extern hal_sig_t *halpr_find_sig_by_name(const char *name);
extern hal_param_t *halpr_find_param_by_name(const char *name);
extern hal_thread_t *halpr_find_thread_by_name(const char *name);
extern hal_funct_t *halpr_find_funct_by_name(const char *name);
// variants to search in a remote hal_data segment
// halpr_find_pin_by_name() is defined as the local case of this function
extern hal_pin_t *halpr_remote_find_pin_by_name(const hal_data_t *hal_data,
						const char *name);
// halpr_find_sig_by_name() is defined as the local case of this function
extern hal_sig_t *halpr_remote_find_sig_by_name(const hal_data_t *hal_data,
						const char *name);

/** Allocates a HAL component structure */
extern hal_comp_t *halpr_alloc_comp_struct(void);

/** 'find_comp_by_id()' searches the component list for an object whose
    component ID matches 'id'.  It returns a pointer to that component,
    or NULL if no match is found.
*/
extern hal_comp_t *halpr_find_comp_by_id(int id);

/** The 'find_xxx_by_owner()' functions find objects owned by a specific
    component.  If 'start' is NULL, they start at the beginning of the
    appropriate list, and return the first item owned by 'comp'.
    Otherwise they assume that 'start' is the value returned by a prior
    call, and return the next matching item.  If no match is found, they
    return NULL.
*/
extern hal_pin_t *halpr_find_pin_by_owner(hal_comp_t * owner,
    hal_pin_t * start);
extern hal_param_t *halpr_find_param_by_owner(hal_comp_t * owner,
    hal_param_t * start);
extern hal_funct_t *halpr_find_funct_by_owner(hal_comp_t * owner,
    hal_funct_t * start);

/** 'find_pin_by_sig()' finds pin(s) that are linked to a specific signal.
    If 'start' is NULL, it starts at the beginning of the pin list, and
    returns the first pin that is linked to 'sig'.  Otherwise it assumes
    that 'start' is the value returned by a previous call, and it returns
    the next matching pin.  If no match is found, it returns NULL
*/
extern hal_pin_t *halpr_find_pin_by_sig(hal_sig_t * sig, hal_pin_t * start);
extern hal_pin_t *halpr_remote_find_pin_by_sig(hal_data_t *hal_data,
					       hal_sig_t * sig, hal_pin_t * start);


// search for crosslinks between instances
typedef  int (* hal_pin_sig_xref_cb)(int, char *, char*, hal_pin_t *, hal_sig_t *);
extern int halpr_pin_crossrefs(unsigned long *bitmap, hal_pin_sig_xref_cb ps_callback);

typedef  int (* hal_ring_xref_cb)(int, char *, char*, hal_ring_attachment_t *);
extern int halpr_ring_crossrefs(unsigned long *bitmap, hal_ring_xref_cb ps_callback);

extern int halpr_lock_ordered(int inst1, int inst2);
extern int halpr_unlock_ordered(int inst1, int inst2);

static inline int hal_valid_instance(int instance)
{
    return (instance >= 0) && (instance < MAX_INSTANCES);
}

// automatically release the local hal_data->mutex on scope exit.
// if a local variable is declared like so:
//
// int foo  __attribute__((cleanup(halpr_autorelease_mutex)));
//
// then leaving foo's scope will cause halpr_release_lock() to be called
// see http://git.mah.priv.at/gitweb?p=emc2-dev.git;a=shortlog;h=refs/heads/hal-lock-unlock
// NB: make sure the mutex is actually held in the using code when leaving scope!
void halpr_autorelease_mutex(void *variable);

// automatically release locks on the local, and any remote hal_data (if so
// indicated by *remote_instance != rtapi_instance) on scope exit.
//
// To be used with a declaration like so:
// int remote_instance
//	__attribute__((cleanup(halpr_autorelease_ordered))) = rtapi_instance;
//
// if remote_instance == rtapi_instance, only the local hal_data segment
// will be unlocked; else unlock happens in instance order (deadlock prevention).
void halpr_autorelease_ordered(int *remote_instance);


#define CCOMP_MAGIC  0xbeef0815
typedef struct {
    int magic;
    hal_comp_t *comp;
    int n_pins;
    hal_pin_t  **pin;           // all members (nesting resolved)
    unsigned long *changed;     // bitmap
    hal_data_u    *tracking;    // tracking values of monitored pins
} hal_compiled_comp_t;


typedef int(*comp_report_callback_t)(int,  hal_compiled_comp_t *,
				     hal_pin_t *pin,
				     int handle,
				     void *data,
				     void *cb_data);

extern int hal_compile_comp(const char *name, hal_compiled_comp_t **ccomp);
extern int hal_ccomp_match(hal_compiled_comp_t *ccomp);
extern int hal_ccomp_report(hal_compiled_comp_t *ccomp,
			    comp_report_callback_t report_cb,
			    void *cb_data, int report_all);
extern int hal_ccomp_free(hal_compiled_comp_t *ccomp);
RTAPI_END_DECLS
#endif /* HAL_PRIV_H */
