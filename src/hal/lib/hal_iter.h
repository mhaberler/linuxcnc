#ifndef HAL_ITER_H
#define HAL_ITER_H

#include <rtapi.h>
#include <hal.h>
#include <hal_ring.h>
#include <hal_priv.h>

RTAPI_BEGIN_DECLS

// callback return value convention:
//----------------------------------
//
// 0  - signal 'continue iterating'
// >0 - stop iterating and return number of visited objects
// <0 - stop iterating and return that value (typically error code)
typedef int (*hal_pertype_callback_t)  (hal_object_ptr o,  void *arg);

// generic version
int halg_foreach_type(const int use_hal_mutex,
		      const int type,
		      const char *name,
		      hal_pertype_callback_t cb,
		      void *args);

// legacy
typedef int (*hal_comp_callback_t)  (hal_comp_t *comp,  void *arg);
typedef int (*hal_sig_callback_t)   (hal_sig_t *sig,  void *arg);
typedef int (*hal_ring_callback_t)  (hal_ring_t *ring,  void *arg);
typedef int (*hal_funct_callback_t) (hal_funct_t *funct,  void *arg);
typedef int (*hal_thread_callback_t)(hal_thread_t *thread,  void *arg);
typedef int (*hal_pin_callback_t)   (hal_pin_t *pin,  void *arg);

typedef int (*hal_param_callback_t) (hal_param_t *param,  void *arg);
typedef int (*hal_vtable_callback_t)(hal_vtable_t *vtable,  void *arg);
typedef int (*hal_inst_callback_t)  (hal_inst_t *inst,  void *arg);


int halpr_foreach_comp(const char *name,  hal_comp_callback_t callback, void *arg);
//int halpr_foreach_sig(const char *name,   hal_sig_callback_t callback, void *arg);
int halpr_foreach_ring(const char *name,  hal_ring_callback_t callback, void *arg);
int halpr_foreach_thread(const char *name,hal_thread_callback_t callback, void *arg);


// int halpr_foreach_vtable(const char *name,   hal_vtable_callback_t callback, void *arg);


static inline int halpr_foreach_sig(const char *name,
					hal_sig_callback_t callback,
					void *arg)
{
    return halg_foreach_type(0, HAL_SIGNAL, name,
			     (hal_pertype_callback_t) callback,
			     arg);
}

static inline int halpr_foreach_pin(const char *name,
				    hal_pin_callback_t callback,
				    void *arg)
{
    return halg_foreach_type(0, HAL_PIN, name,
			     (hal_pertype_callback_t) callback,
			     arg);
}

static inline int halpr_foreach_param(const char *name,
				      hal_param_callback_t callback,
				      void *arg)
{
    return halg_foreach_type(0, HAL_PARAM, name,
			     (hal_pertype_callback_t) callback,
			     arg);
}

static inline int halpr_foreach_inst(const char *name,
				     hal_inst_callback_t callback,
				     void *arg)
{
    return halg_foreach_type(0, HAL_INST, name,
			     (hal_pertype_callback_t) callback,
			     arg);
}

static inline int halpr_foreach_funct(const char *name,
				      hal_funct_callback_t callback,
				      void *arg)
{
    return halg_foreach_type(0, HAL_FUNCT, name,
			     (hal_pertype_callback_t) callback,
			     arg);
}



// callback return value convention applies
typedef int (*hal_pin_signal_callback_t)(hal_pin_t *pin,
					 hal_sig_t *sig,
					 void *user);
// iterate over pins linked to a signal.
int halg_foreach_pin_by_signal(const int use_hal_mutex,
			       hal_sig_t *sig,
			       hal_pin_signal_callback_t cb,
			       void *user);

RTAPI_END_DECLS

#endif // HAL_ITER_H
