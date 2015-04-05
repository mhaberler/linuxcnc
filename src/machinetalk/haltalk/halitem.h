#include <hal.h>
#include <hal_ring.h>

union halobject_union {
    hal_pin_t *pin;
    hal_param_t *param;
    hal_sig_t *signal;
    hal_comp_t *comp;
    hal_group_t *group;
    hal_member_t *member;
    hal_ring_t *ring;
};

union hal_value_union {
    hal_data_u *sigptr;
    hal_data_u **pinptr;
    ringbuffer_t *rbptr;
};

typedef struct  {
    hal_object_type type;
    union halobject_union o;
    union hal_value_union v;
    //void *ptr; // points to raw value if there's one (usually hal_data_u *)
} halitem_t;
