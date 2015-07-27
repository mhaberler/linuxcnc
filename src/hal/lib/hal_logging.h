#ifndef HAL_LOGGING_H
#define HAL_LOGGING_H

#include <rtapi.h>
RTAPI_BEGIN_DECLS

void hal_print_loc(const int level,
		     const char *func,
		     const int line,
		     const char *topic,
		     const char *fmt, ...)
    __attribute__((format(printf,5,6)));

// checking & logging shorthands
#define HALERR(fmt, ...)					\
    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__,__LINE__,	\
		    "HAL error:", fmt, ## __VA_ARGS__)
#define HALBUG(fmt, ...)					\
    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__,__LINE__,	\
		    "HAL BUG:", fmt, ## __VA_ARGS__)
#define HALWARN(fmt, ...)					\
    hal_print_loc(RTAPI_MSG_WARN,__FUNCTION__,__LINE__,	\
		    "HAL WARNING:", fmt, ## __VA_ARGS__)
#define HALINFO(fmt, ...)					\
    hal_print_loc(RTAPI_MSG_INFO,__FUNCTION__,__LINE__,	\
		    "HAL info:", fmt, ## __VA_ARGS__)

#define HALDBG(fmt, ...)					\
    hal_print_loc(RTAPI_MSG_DBG,__FUNCTION__,__LINE__,	\
		    "HAL:", fmt, ## __VA_ARGS__)

#define HAL_ASSERT(x)						\
    do {							\
	if (!(x)) {						\
	    hal_print_loc(RTAPI_MSG_ERR,			\
			    __FUNCTION__,__LINE__,		\
			    "HAL error:",			\
			    "ASSERTION VIOLATED: '%s'", #x);	\
	}							\
    } while(0)


#define CHECK_HALDATA()					\
    do {						\
	if (hal_data == 0) {				\
	    hal_print_loc(RTAPI_MSG_ERR,		\
			    __FUNCTION__,__LINE__,	\
			    "HAL error:",		\
			    "called before init");	\
	    return -EINVAL;				\
	}						\
    } while (0)

#define PCHECK_HALDATA()					\
    do {						\
	if (hal_data == 0) {				\
	    hal_print_loc(RTAPI_MSG_ERR,		\
			    __FUNCTION__,__LINE__,	\
			    "HAL error:",		\
			    "called before init");	\
	    _halerrno = -EINVAL;			\
	    return NULL;				\
	}						\
    } while (0)

#define CHECK_NULL(p)						\
    do {							\
	if (p == NULL) {					\
	    hal_print_loc(RTAPI_MSG_ERR,			\
			    __FUNCTION__,__LINE__,"HAL error:",	\
			    #p  " is NULL");			\
	    return -EINVAL;					\
	}							\
    } while (0)

#define PCHECK_NULL(p)						\
    do {							\
	if (p == NULL) {					\
	    hal_print_loc(RTAPI_MSG_ERR,			\
			    __FUNCTION__,__LINE__,"HAL error:",	\
			    #p  " is NULL");			\
	    _halerrno = -EINVAL;				\
	    return NULL;					\
	}							\
    } while (0)

#define CHECK_LOCK(ll)							\
    do {								\
	if (hal_data->lock & ll) {					\
	    hal_print_loc(RTAPI_MSG_ERR,				\
			    __FUNCTION__, __LINE__,"HAL error:",	\
			    "called while HAL is locked (%d)",		\
			    ll);					\
	    return -EPERM;						\
	}								\
    } while(0)

#define PCHECK_LOCK(ll)							\
    do {								\
	if (hal_data->lock & ll) {					\
	    hal_print_loc(RTAPI_MSG_ERR,				\
			    __FUNCTION__, __LINE__,"HAL error:",	\
			    "called while HAL is locked (%d)",		\
			    ll);					\
	    _halerrno = -EPERM;						\
	    return NULL;						\
	}								\
    } while(0)


#define CHECK_STR(name)							\
    do {								\
	if ((name) == NULL) {						\
	    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__, __LINE__,"HAL error:", \
			    "argument '" # name  "' is NULL");		\
	    return -EINVAL;						\
	}								\
    } while(0)

#define PCHECK_STR(name)							\
    do {								\
	if ((name) == NULL) {						\
	    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__, __LINE__,"HAL error:", \
			    "argument '" # name  "' is NULL");		\
	    _halerrno = -EINVAL;					\
	    return NULL;						\
	}								\
    } while(0)


#define CHECK_STRLEN(name, len)						\
    do {								\
	CHECK_STR(name);						\
	if (strlen(name) > len) {					\
	    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__, __LINE__,"HAL error:", \
			    "argument '%s' too long (%zu/%d)",		\
			    name, strlen(name), len);			\
	    return -EINVAL;						\
	}								\
    } while(0)

#define PCHECK_STRLEN(name, len)						\
    do {								\
	PCHECK_STR(name);						\
	if (strlen(name) > len) {					\
	    hal_print_loc(RTAPI_MSG_ERR,__FUNCTION__, __LINE__,"HAL error:", \
			    "argument '%s' too long (%zu/%d)",		\
			    name, strlen(name), len);			\
	    _halerrno = -EINVAL;					\
	    return NULL;						\
	}								\
    } while(0)

#define NOMEM(fmt, ...)						\
    do {							\
	hal_print_loc(RTAPI_MSG_ERR,				\
			__FUNCTION__, __LINE__,"HAL error:",	\
			" insufficient memory for: "  fmt,	\
			## __VA_ARGS__);			\
	return -ENOMEM;						\
    } while(0)

RTAPI_END_DECLS

#endif // HAL_LOGGING_H
