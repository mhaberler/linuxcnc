#include <hal.h>

/* union paramunion { */
/*     hal_bit_t b; */
/*     hal_u32_t u32; */
/*     hal_s32_t s32; */
/*     hal_float_t f; */
/* }; */

/* union pinunion { */
/*     void *v; */
/*     hal_bit_t *b; */
/*     hal_u32_t *u32; */
/*     hal_s32_t *s32; */
/*     hal_float_t *f; */
/* }; */

/* union sigunion { */
/*     void *v; */
/*     hal_bit_t *b; */
/*     hal_u32_t *u32; */
/*     hal_s32_t *s32; */
/*     hal_float_t *f; */
/* }; */


/* union haldirunion { */
/*     hal_pin_dir_t pindir; */
/*     hal_param_dir_t paramdir; */
/* }; */

union halobject_union {
    hal_pin_t *pin;
    hal_param_t *param;
    hal_sig_t *signal;
    hal_comp_t *comp;
    hal_group_t *group;
    hal_member_t *member;
};

typedef struct  {
    hal_object_type type;
    union halobject_union o;
    void *ptr;
} halitem_t;
