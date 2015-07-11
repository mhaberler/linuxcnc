#ifndef HAL_OBJECT_H
#define HAL_OBJECT_H

#include "rtapi_int.h"
#include "hal_priv.h"
#include "hal_list.h"

// common header for all HAL objects
// this MUST be the first field in any named object descriptor,
// so any named object can be cast to a halobj_t *
// access/modify fields by accessors ONLY please.
// prefix any additional accessors with hh_.

typedef struct halhdr {
    hal_list_t links;                  // NB: leave as first member
    __s32    _id;
    __s32    _owner_id;
    __u32    _type :  5;               // enum hal_object_type
    __u32    _valid : 1;               // marks as active/unreferenced object
    __u32    _spare : 2;               //
    char   _name[HAL_NAME_LEN + 1];    // component name
} halhdr_t;

#define OBJECTLIST (&hal_data->halobjects)  // list of all named HAL objects

// accessors for common HAL object attributes
// no locking - caller is expected to aquire the HAL mutex with WITH_HAL_MUTEX()

static inline void *hh_get_next(halhdr_t *o)
{
    return (void *) dlist_next(&o->links);
}
static inline void  hh_set_next(halhdr_t *o, void *next)   { dlist_add_after(&o->links, (hal_list_t *)next); }

static inline int   hh_get_id(const halhdr_t *o)      { return o->_id; }
static inline void  hh_set_id(halhdr_t *o, int id)    { o->_id = id; }

static inline int   hh_get_owner_id(const halhdr_t *o){ return o->_owner_id; }
static inline void  hh_set_owner_id(halhdr_t *o, int owner) { o->_owner_id = owner; }

static inline __u32   hh_get_type(const halhdr_t *o)    { return o->_type; }
static inline void  hh_set_type(halhdr_t *o, __u32 type){ o->_type = type; }

// this enables us to eventually drop the name from the header
// and move to a string table
static inline const char *hh_get_name(halhdr_t *o)    { return o->_name; }
int hh_set_namefv(halhdr_t *o, const char *fmt, va_list ap);
static inline void hh_clear_name(halhdr_t *o)    { o->_name[0] = '\0'; }

static inline hal_bool hh_is_valid(halhdr_t *o)           { return (o->_valid != 0); }
static inline void hh_set_valid(halhdr_t *o)          { o->_valid = 1; }
static inline void hh_set_invalid(halhdr_t *o)        { o->_valid = 0; }

// initialize a HAL object header with unique ID and name,
// optionally an owner id for dependent objects (e.g. hal_pin_t, hal_inst_t)
// use 0 as owner_id for top-level objects:

int  hh_init_hdrf(halhdr_t *o,
		  const hal_object_type type,
		  const int owner_id,
		  const char *fmt, ...);

int  hh_init_hdrfv(halhdr_t *o,
		   const hal_object_type type,
		   const int owner_id,
		   const char *fmt, va_list ap);

// invalidate a hal object header
int hh_clear_hdr(halhdr_t *o);


// halg_foreach: generic HAL object iterator
// selects by type (if set), then by name (if set)
// set to replace the gazillion of type-specific iterators
//
// callback return values drive behavior like so:
// 0  - signal 'continue iterating'
// >0 - stop iterating and return number of visited objects
// <0 - stop iterating and return that value (typically error code)
struct foreach_args;
typedef struct foreach_args foreach_args_t;
typedef int (*hal_object_callback_t)  (hal_object_ptr object,
				       foreach_args_t *args);
typedef struct foreach_args {
    // standard selection parameters - in only:
    const int type;         // one of hal_object_type or 0
    const int id;           // search by object ID
    const char *name;       // search name prefix or NULL

    // generic in/out parameters to/from the callback function:
    // used to pass selection criteria, and return specific values
    // (either int or void *)
    int user_arg1;          // opaque int arguments
    int user_arg2;
    void *user_ptr1;        // opaque user pointer arguments
    void *user_ptr2;
} foreach_args_t;


int halg_foreach(bool use_hal_mutex,
		 foreach_args_t *args,
		 const hal_object_callback_t callback); // optional callback

#include "hal_object_selectors.h"

#endif // HAL_OBJECT_H
