#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_ring.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("ringbuffer multiplexer");
MODULE_LICENSE("GPL");

RTAPI_TAG(HAL,HC_INSTANTIABLE);

#define MAX_INPUTS 8
static char *input[MAX_INPUTS] = {0,};
RTAPI_IP_ARRAY_STRING(input, MAX_INPUTS, "input ringbuffers");
static char *output = "";
RTAPI_IP_STRING(output, "destination ringbuffer");

static int comp_id;
static char *compname = "ringmux";

struct inst_data {
    int count;                  // of input ringbuffers
    hal_bit_t   *forward;       // in: enable message forwarding
                                // from current input to output ringbuffer
    hal_u32_t   *select;        // in: desired ringbuffer
    ringbuffer_t out;           // output ringbuffer
    ringbuffer_t in[0];         // the input ringbuffers
};


static int update(void *arg, const hal_funct_args_t *fa)
{
    struct inst_data *ip = (struct inst_data *) arg;

    if (!*(ip->forward))
	return 0;  // forwarding disabled

    if (*(ip->select) > (ip->count - 1))
	return 0;  // invalid rb index

    ringbuffer_t *current_input = &ip->in[*(ip->select)];
    void *data;
    size_t size;

    while (record_read(current_input, (const void**)&data, &size) == 0) {
	if (record_write(&ip->out, data, size) == 0) {
	    // write succeeeded, so consume record
	    record_shift(current_input);
	} else {
	    // downstream full, stop
	    break;
	}
    }
    return 0;
}


static int instantiate_rmux(const char *name,
			    const int argc,
			    const char**argv)
{
    struct inst_data *ip;
    int inst_id, i, n;
    unsigned flags;

    for (n = 0; input[n] != NULL; n++);

    if ((inst_id = hal_inst_create(name, comp_id,
				   sizeof(struct inst_data) +
				   sizeof(ringbuffer_t) * n,
				   (void **)&ip)) < 0)
	return -1;

    ip->count = n;
    for (i = 0; i < n; i++) {
	if (!hal_ring_attachf(&ip->in[i], &flags,  input[i])) {
	    if ((flags & RINGTYPE_MASK) == RINGTYPE_STREAM) {
		HALERR("ring %s: stream rings not suppported",name);
		return -EINVAL;
	    }
	    ip->in[i].header->reader = inst_id;
	}
    }
    if (!hal_ring_attachf(&ip->out, &flags, output)) {
	if ((flags & RINGTYPE_MASK) == RINGTYPE_STREAM) {
	    HALERR("ring %s: stream rings not suppported", name);
	    return -EINVAL;
	}
	ip->out.header->writer = inst_id;
    }

    if (hal_pin_bit_newf(HAL_IN, &(ip->forward), inst_id, "%s.forward", name) ||
	hal_pin_u32_newf(HAL_IN, &(ip->select), inst_id, "%s.select", name))
	return -1;

    hal_export_xfunct_args_t xfunct_args = {
        .type = FS_XTHREADFUNC,
        .funct.x = update,
        .arg = ip,
        .uses_fp = 0,
        .reentrant = 0,
        .owner_id = inst_id
    };
    return hal_export_xfunctf(&xfunct_args, "%s.update", name);
}

static int delete_rmux(const char *name, void *inst, const int inst_size)
{

    struct inst_data *ip = (struct inst_data *) inst;
    int i;

    for (i = 0; i < ip->count; i++) {
	// FIXME detach in rb's
	ip->in[i].header->reader = 0;
    }
    // FIXME detach out rb
    ip->out.header->reader = 0;
    return 0;
}

int rtapi_app_main(void)
{
    comp_id = hal_xinit(TYPE_RT, 0, 0,
			instantiate_rmux,
			delete_rmux,
			compname);
    if (comp_id < 0)
	return comp_id;
    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
