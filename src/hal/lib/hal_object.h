#ifndef HAL_OBJECT_H
#define HAL_OBJECT_H

#include "rtapi_int.h"
#include "hal_priv.h"
#include "hal_list.h"


// type tags of HAL objects. See also protobuf/proto/types.proto/enum ObjectType
// which must match:
typedef enum {
    HAL_OBJECT_INVALID = 0,
    HAL_PIN           = 1,
    HAL_SIGNAL        = 2,
    HAL_PARAM         = 3,
    HAL_THREAD        = 4,
    HAL_FUNCT         = 5,
    HAL_COMPONENT     = 6,
    HAL_VTABLE        = 7,
    HAL_INST          = 8,
    HAL_RING          = 9,
    HAL_GROUP         = 10,
    HAL_MEMBER        = 11,
} hal_object_type;

// common header for all HAL objects
// this MUST be the first field in any named object descriptor,
// so any named object can be cast to a halobj_t *
// access/modify fields by accessors ONLY please.
// prefix any additional accessors with hh_.

typedef struct halhdr {
    hal_list_t list;                  // NB: leave as first member
    __s32    _id;
    __s32    _owner_id;
    __u32    _type :  5;               // enum hal_object_type
    __u32    _valid : 1;               // marks as active/unreferenced object
    __u32    _spare : 2;               //
    char   _name[HAL_NAME_LEN + 1];    // component name
} halhdr_t;

#define OBJECTLIST (&hal_data->halobjects)  // head of all named HAL objects

// accessors for common HAL object attributes
// no locking - caller is expected to aquire the HAL mutex with WITH_HAL_MUTEX()

static inline void *hh_get_next(halhdr_t *o)
{
    return (void *) dlist_next(&o->list);
}
static inline void  hh_set_next(halhdr_t *o, void *next)   { dlist_add_after(&o->list, (hal_list_t *)next); }

static inline int   hh_get_id(const halhdr_t *o)  { return o->_id; }
static inline void  hh_set_id(halhdr_t *o, int id)    { o->_id = id; }

static inline int   hh_get_owner_id(const halhdr_t *o){ return o->_owner_id; }
static inline void  hh_set_owner_id(halhdr_t *o, int owner) { o->_owner_id = owner; }

static inline __u32   hh_get_type(const halhdr_t *o)    { return o->_type; }
static inline void  hh_set_type(halhdr_t *o, __u32 type){ o->_type = type; }
const char *hh_get_typestr(const halhdr_t *hh);

// determine if an object is first-class or dependent on some other object
static inline bool hh_is_toplevel(__u32 type) {
    switch (type) {
    case HAL_PIN:
    case HAL_PARAM:
    case HAL_FUNCT:
    case HAL_INST:
    case HAL_MEMBER:
	return false;
    }
    return true;
}

// this enables us to eventually drop the name from the header
// and move to a string table
static inline const char *hh_get_name(const halhdr_t *o)    { return o->_name; }

int hh_set_namefv(halhdr_t *o, const char *fmt, va_list ap);
int hh_set_namef(halhdr_t *hh, const char *fmt, ...);

static inline void hh_clear_name(halhdr_t *o)         { o->_name[0] = '\0'; }

static inline hal_bool hh_is_valid(const halhdr_t *o)       { return (o->_valid != 0); }
static inline void hh_set_valid(halhdr_t *o)          { o->_valid = 1; }
static inline void hh_set_invalid(halhdr_t *o)        { o->_valid = 0; }

// shorthands macros
#define ho_id(h)  hh_get_id(&(h)->hdr)
#define ho_owner_id(h)  hh_get_owner_id(&(h)->hdr)
#define ho_name(h)  hh_get_name(&(h)->hdr)
#define ho_type(h)  hh_get_type(&(h)->hdr)
#define ho_typestr(h)  hh_get_typestr(&(h)->hdr)

// print common HAL object header to a sized buffer.
// returns number of chars used or -1 for 'too small buffer'
int hh_snprintf(char *buf, size_t size, const halhdr_t *hh);

// adds a HAL object into the object list with partial ordering:
// all objects of the same type will be kept sorted by name.
void halg_add_object(const bool use_hal_mutex,  hal_object_ptr o);

// free a HAL object
// invalidates and removes the object, and frees the descriptor.
void halg_free_object(const bool use_hal_mutex, hal_object_ptr o);


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

// the foreach_args_t struct serves a dual purpose:
// - it drives object matching in the halg_foreach iterator.
// - it has some user_* fields to pass values to/from the callback routine.
//
// the user_* fields have no bearing on matching - they are strictly
// for passing values to/from the callback routine.

// only the following foreach_args_t fields drive matching:
// name type id  owner_id owning_comp
//
// examples:
//
// foreach_args_t args = { .type = HAL_PIN } will match all pins
//
// foreach_args_t args = { .type = HAL_PIN, .owner_id = 123 } will
// match pins owned by comp with id 123, OR instance with id 123
//
// foreach_args_t args = { .type = HAL_PIN, .owning_comp = 453 } will
// match pins owned by comp with id 123 either direcly or through an
// instance (whose ID does not matter)

// foreach_args_t args = { .type = HAL_PIN, .name = "foo" } will
// match all pins whose name starts with 'foo'
//
// foreach_args_t args = { .type = HAL_PIN, .id = 789 } will
// match exactly zero or one times depending if pin with id 789 exists or not
//
// foreach_args_t args = { .type = HAL_PIN, .owning_comp = 453, .id = 789 }
// will match exactly zero or one times depending if pin with id 789 exists
// or not, but only if owned by comp with id 453 directly or indirectly

// foreach_args_t args = { .name = "bar"  } will match all objects whose name begins with "bar"

typedef struct foreach_args {
    // standard selection parameters - in only:
    int type;         // one of hal_object_type or 0
    int id;           // search by object ID

    // use a match on owner_id for direct ownership only:
    // for instance, to find the pins owned by an instance,
    // set owner_id to the instance id.

    // using a comp id to match an owner_id will only retrieve
    // objects directly owned by a comp (legacy case).
    // to cover the new semantics, use owning_comp below.
    int owner_id;     // search by owner object ID as stored in hdr

    // pins, params and functs may be owner either by a comp (legacy)
    // or an instance (instantiable comps).
    // An instance is in turn owned by a comp.
    // searching by 'owning_comp' covers both cases - it will match
    // the comp id in the legacy as well as the instantiable case.
    // see halpr_find_owning_comp() for details.

    // to find all objects eventually (directly or through an instance)
    // owned by a comp (and only a comp), match with owning_comp.
    int owning_comp;  // pins, param,search by owner object ID

    char *name;       // search name prefix or NULL

    // generic in/out parameters to/from the callback function:
    // used to pass selection criteria, and return specific values
    // (either int or void *)
    int user_arg1;          // opaque int arguments
    int user_arg2;
    void *user_ptr1;        // opaque user pointer arguments
    void *user_ptr2;
    void *user_ptr3;
} foreach_args_t;


int halg_foreach(bool use_hal_mutex,
		 foreach_args_t *args,
		 const hal_object_callback_t callback); // optional callback

#include "hal_object_selectors.h"

#endif // HAL_OBJECT_H
