#ifndef _HAL_OBJECT_SELECTORS_H
#define _HAL_OBJECT_SELECTORS_H


// retrieving a HAL object descriptor, or any field therein, is a two-step
// process:
// 1. define a standard object selection.
//   (a) selecting the object type (optional):
//     .type = <desired type>, // one of enum hal_object_type,
//                             // or 0 for any object type
//
//   (b) selecting the actual object (optional):
//     .id = <object ID>,      // by ID, OR
//     .name = <object name>   // by name
//
// 2. once the object(s) are selected,
//    apply the callback function on each member of the result set,
//    optionally passing selector-specific arguments and
//    retrieving selector-specific return values
//    through the foreach_args struct.
//    there are two user-definable int params (user_arg1 and user_arg2)
//    plus two user-definable void pointers (user_ptr1 and user_ptr2).
//    these can be extended as needed.

//--------------------------------------------------------------------
// use this selector to retrieve a pointer to a HAL object descriptor
// selected by the standard selection (type, object ID/object name):
//
// selector-specific arguments:
//
// returned HAL object descriptor or NULL if not found
//     .user_ptr1 = NULL,  // holy water - init to zero
// };
int yield_match(hal_object_ptr o, foreach_args_t *args);

//--------------------------------------------------------------------
// use this selector to count the number of objects matching
// the standard selection (type, object ID/object name):
//
// selector-specific arguments:
//
// returned HAL object descriptor or NULL if not found
//     .user_ptr1 = NULL,  // holy water - init to zero
// };
int yield_count(hal_object_ptr o, foreach_args_t *args);

//--------------------------------------------------------------------
// use this selector to retrieve a pointer to a HAL vtable descriptor
// selected by standard selection AND having a particular version:
//     .type = HAL_VTABLE,  // type MUST be present
//     .name = name,        // if selecting by name AND/OR
//     .id = <object ID>,   // by vtable object ID
//
// required argument:
//     .user_arg1 = <version>,
//
// returned vtable descriptor or NULL if not found
//     .user_ptr1 = NULL,
// };
int yield_versioned_vtable_object(hal_object_ptr o, foreach_args_t *args);


// use this selector to count the number of objects
// selected by standard selection AND subordinate to a different object
//
// examles:
//   number of pins owned by a comp
//   number of params owned by a comp
//   number of vtables exported by a comp
//   number of pins owned by an instance
//
// required argument:
//     .user_arg1 = <owning object id>,
// returned count
//     .user_arg2 = 0,
int count_subordinate_objects(hal_object_ptr o, foreach_args_t *args);


#endif
