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

/* static inline const char *item_name(halitem_t *hi) */
/* { */
/*     if (hi == NULL) return "NULL"; */
/*     switch (hi-> type) { */
/*      HAL_PIN           = 1, */
/*     HAL_SIGNAL        = 2, */
/*     HAL_PARAM         = 3, */
/*     HAL_THREAD        = 4, */
/*     HAL_FUNCT         = 5, */
/*     HAL_ALIAS         = 6, */
/*     HAL_COMP_RT       = 7, */
/*     HAL_COMP_USER     = 8, */
/*     HAL_COMP_REMOTE   = 9, */
/*     HAL_RING          = 10, */
/*     HAL_GROUP         = 11, */
/*     HAL_MEMBER_SIGNAL = 12, */
/*     HAL_MEMBER_GROUP  = 13, */
/*     HAL_MEMBER_PIN    = 14, */

/*     RING_RECORD       = 15, */
/*     RING_STREAM       = 16, */
/* 	case  */
/*     default: */
/* 	return "invalid item type"; */
/*     } */

/* } */
