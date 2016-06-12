#ifndef _PASSFD_PROTO_H_INCLUDED
#define _PASSFD_PROTO_H_INCLUDED

// private header for passfd functions

// when fetching/passing filedescriptors from/to HAL (rtapi_app),
// the fd has a HAL-side name - it is held in a HAL_S32 out pin
// these structs convey the operation desired and the HAL name
// they are used in the mt_fetch_fd/mt_send_fd functions which talk
// to rtapi_app

#define PASSFD_SOCKET  "Xpassfd_socket"

typedef enum {
    PFD_GET,   // retrieve an fd from rtapi_app
    PFD_PUT,   // pass an fd to rtapi_app, or set existing fd to new one
    PFD_CLOSE, // close an fd in rtapi_app
} passfd_op_t;


// request header passed by client to rtapi_app
struct passfd_request {
    passfd_op_t op; // desired operation
    char name[100]; // of the HAL named file descriptor
};


// response header by rtapi_app
// on success, followed by the actual fd transfer via ancil_send_fd()
struct passfd_response {
    int hal_rc;            // return code of the hal_get_fd()/hal_set_fd() call
    char msgtext[100];     // informational - for log
};

#endif
