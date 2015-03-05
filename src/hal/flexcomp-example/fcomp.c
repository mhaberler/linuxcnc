
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("demo component for HAL instantiation API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "fcomp";

struct inst_data {
    hal_float_t *out; // pins
    hal_float_t *in;
    hal_s32_t    iter; // a param
};

// thread function - equivalent to FUNCTION(_) in comp
static void funct(void *arg, long period)
{
    // use the instance pointer passed in halinst_export_funct()
    struct inst_data *ip = arg;

    *(ip->out) = *(ip->in);
    ip->iter++;
}

// init HAL objects
static int export_halobjs(struct inst_data *ip, int owner_id, const char *name)
{

    if (hal_pin_float_newf(HAL_OUT, &ip->out, owner_id, "%s.out", name))
	return -1;
    if (hal_pin_float_newf(HAL_IN,  &ip->in,  owner_id, "%s.in", name))
	return -1;
    if (hal_param_s32_newf(HAL_RO,  &ip->iter,owner_id, "%s.iter", name))
	return -1;

    // export a thread function, passing the pointer to the instance's HAL memory blob
    if (hal_export_functf(funct, ip, 0, 0, owner_id, "%s.funct", name))
	return -1;
    return 0;
}

// constructor - init all HAL pins, params, funct etc here
static int instantiate(const char *name, const int argc, const char**argv)
{
    struct inst_data *ip;

    // allocate a named instance, and some HAL memory for the instance data
    int inst_id = hal_inst_create(name, comp_id,
				  sizeof(struct inst_data),
				  (void **)&ip);
    if (inst_id < 0)
	return -1;

    // here ip is guaranteed to point to a blob of HAL memory of size sizeof(struct inst_data).
    hal_print_msg(RTAPI_MSG_ERR,"%s inst=%s argc=%d\n",__FUNCTION__, name, argc);

    // these pins/params/functs will be owned by the instance, and can be separately exited
    return export_halobjs(ip, inst_id, name);
}

// custom destructor - normally not needed
// pins, params, and functs are automatically deallocated regardless if a
// destructor is used or not (see below)
//
// some objects like vtables, rings, threads are not owned by a component
// interaction with such objects may require a custom destructor for
// cleanup actions

// NB: if a customer destructor is used, it is called
// - after the instance's functs have been removed from their respective threads
//   (so a thread funct call cannot interact with the destructor any more)
// - any pins and params of this instance are still intact when the destructor is
//   called, and they are automatically destroyed by the HAL library once the
//   destructor returns
static int delete(const char *name, void *inst, const int inst_size)
{

    hal_print_msg(RTAPI_MSG_ERR,"%s inst=%s size=%d %p\n",
		  __FUNCTION__, name, inst_size, inst);
    return 0;
}


static int answer = 42;
RTAPI_MP_INT(answer, "a random module parameter");

int rtapi_app_main(void)
{
    // to use default destructor, use NULL instead of delete
    comp_id = hal_xinit(compname, TYPE_RT, 0, 0, instantiate, delete);
    if (comp_id < 0)
	return -1;

    struct inst_data *ip = hal_malloc(sizeof(struct inst_data));

    // traditional behavior: these pins/params/functs will be owned by the component
    // NB: this 'instance' cannot be exited
    if (export_halobjs(ip, comp_id, "foo"))
	return -1;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}
