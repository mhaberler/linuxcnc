#ifndef HAL_OBJECT_H
#define HAL_OBJECT_H

#include "rtapi_int.h"

// common header for all HAL objects
// this MUST be the first field in any named object descriptor,
// so any named object can be cast to a halobj_t *
// access/modify fields by accessors ONLY please.
// prefix any additional accessors with hh_.

typedef struct {
    __s32    _next_ptr;
    __s32    _id;
    __s32    _owner_id;
    __u32    _type :  5;               // enum hal_object_type
    __u32    _valid : 1;               // marks as active/unreferenced object
    __u32    _spare : 2;               //
    char   _name[HAL_NAME_LEN + 1];    // component name
} halobj_t;

// accessors for common HAL object attributes
// no locking - caller is expected to aquire the HAL mutex with WITH_HAL_MUTEX()

static inline int   hh_get_next(const halobj_t *o)    { return o->_next_ptr; }
static inline void  hh_set_next(halobj_t *o, int n)   { o->_next_ptr = n; }

static inline int   hh_get_id(const halobj_t *o)      { return o->_id; }
static inline void  hh_set_id(halobj_t *o, int id)    { o->_id = id; }

static inline int   hh_get_owner_id(const halobj_t *o){ return o->_owner_id; }
static inline void  hh_set_owner_id(halobj_t *o, int owner) { o->_owner_id = owner; }

static inline __u32   hh_get_type(const halobj_t *o)    { return o->_type; }
static inline void  hh_set_type(halobj_t *o, __u32 type){ o->_type = type; }

// this enables us to eventually drop the name from the header
// and move to a string table
static inline const char *hh_get_name(halobj_t *o)    { return o->_name; }
int hh_set_namefv(halobj_t *o, const char *fmt, va_list ap);
static inline void hh_clear_name(halobj_t *o)    { o->_name[0] = '\0'; }

static inline hal_bool hh_is_valid(halobj_t *o)           { return (o->_valid != 0); }
static inline void hh_set_valid(halobj_t *o)          { o->_valid = 1; }
static inline void hh_set_invalid(halobj_t *o)        { o->_valid = 1; }

// initialize a HAL object header with unique ID and name,
// optionally an owner id for dependent objects (e.g. hal_pin_t, hal_inst_t)
// use 0 as owner_id for top-level objects:

int  hh_init_hdrf(halobj_t *o,
		  const int owner_id,
		  const char *fmt, ...);

int  hh_init_hdrfv(halobj_t *o,
		   const int owner_id,
		   const char *fmt, va_list ap);

// invalidate a hal object header
int hh_clear_hdr(halobj_t *o);

#endif // HAL_OBJECT_H
