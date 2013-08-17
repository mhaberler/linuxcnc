#ifndef _HAL_RCOMP_H
#define _HAL_RCOMP_H

// compiled component support
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

#endif
