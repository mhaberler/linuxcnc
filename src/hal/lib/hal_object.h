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
static inline void hh_set_invalid(halhdr_t *o)        { o->_valid = 1; }

// initialize a HAL object header with unique ID and name,
// optionally an owner id for dependent objects (e.g. hal_pin_t, hal_inst_t)
// use 0 as owner_id for top-level objects:

int  hh_init_hdrf(halhdr_t *o,
		  const int owner_id,
		  const char *fmt, ...);

int  hh_init_hdrfv(halhdr_t *o,
		   const int owner_id,
		   const char *fmt, va_list ap);

// invalidate a hal object header
int hh_clear_hdr(halhdr_t *o);


// generic typed and optionally named HAL object iterator
// selects by type, then by name
// set to replace the gazillion of type-specific iterators

// callback return values drive behavior like so:
// 0  - signal 'continue iterating'
// >0 - stop iterating and return number of visited objects
// <0 - stop iterating and return that value (typically error code)

typedef int (*hal_object_callback_t)  (hal_object_ptr object, void *arg);
int halg_foreach(bool use_hal_mutex,
		 int type,         // one of hal_object_type or 0
		 const char *name, // name prefix or NULL
		 hal_object_callback_t callback,
		 void *arg);


#endif // HAL_OBJECT_H
