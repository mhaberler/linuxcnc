
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("test component for HAL instantiation API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "icomp";

struct inst_data {
    hal_float_t *out; // pins
    hal_float_t *in;
    hal_s32_t    iter; // a param

    // instance-level pins
    // for nosetests/unittest_icomp.py to verify the instance
    // parameter passing API works
    // echos the values of the int instance parameter,
    // and the length of the instance string parameter
    hal_s32_t *repeat_value; // unittest
    hal_s32_t *prefix_len; // unittest
};


static int answer = 42;
RTAPI_MP_INT(answer, "a module-level int parameter");

static char *text = "abcdef";
RTAPI_MP_STRING(text, "a module-level string parameter");

static int repeat = 1;
RTAPI_IP_INT(repeat, "a counter passed as instance parameter");

static char *prefix = "bar";
RTAPI_IP_STRING(prefix, "an instance string parameter");

// module-level pins
// for nosetests/unittest_icomp.py to verify the comp
// parameter passing API works
// echos the values of the int module parameter,
// and the length of the module string parameter
struct comp_data {
    hal_s32_t *answer_value;
    hal_s32_t *text_len;
};
static struct comp_data *cd;
static struct comp_data *export_observer_pins(int owner_id, const char *name);


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

    // unittest observer pins, per instance
    if (hal_pin_s32_newf(HAL_OUT, &ip->repeat_value, owner_id, "%s.repeat_value", name))
	return -1;
    if (hal_pin_s32_newf(HAL_OUT, &ip->prefix_len, owner_id, "%s.prefix_len", name))
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
    hal_print_msg(RTAPI_MSG_ERR,"%s: inst=%s argc=%d\n",__FUNCTION__, name, argc);

    hal_print_msg(RTAPI_MSG_ERR,"%s: instance parms: repeat=%d prefix='%s'",__FUNCTION__, repeat, prefix);
    hal_print_msg(RTAPI_MSG_ERR,"%s: module parms: answer=%d text='%s'",__FUNCTION__, answer, text);

    // these pins/params/functs will be owned by the instance, and can be separately exited
    int retval = export_halobjs(ip, inst_id, name);

    // unittest: echo instance parameters into observer pins
    if (!retval) {
	*(ip->repeat_value) = repeat;
	*(ip->prefix_len) = strlen(prefix);
    }
    return retval;
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

    hal_print_msg(RTAPI_MSG_ERR,"%s: instance parms: repeat=%d prefix='%s'",__FUNCTION__, repeat, prefix);
    hal_print_msg(RTAPI_MSG_ERR,"%s: module parms: answer=%d text='%s'",__FUNCTION__, answer, text);

    return 0;
}

int rtapi_app_main(void)
{

    hal_print_msg(RTAPI_MSG_ERR,"%s: instance parms: repeat=%d prefix='%s'",__FUNCTION__, repeat, prefix);
    hal_print_msg(RTAPI_MSG_ERR,"%s: module parms: answer=%d text='%s'",__FUNCTION__, answer, text);

    // to use default destructor, use NULL instead of delete
    comp_id = hal_xinit(TYPE_RT, 0, 0, instantiate, delete, compname);
    if (comp_id < 0)
	return -1;
#if 0
    struct inst_data *ip = hal_malloc(sizeof(struct inst_data));

    // traditional behavior: these pins/params/functs will be owned by the component
    // NB: this 'instance' cannot be exited
    if (export_halobjs(ip, comp_id, "foo"))
	return -1;
#endif
    // unittest only
    if ((cd = export_observer_pins(comp_id, compname)) == NULL)
	return -1;

    hal_ready(comp_id);

    // unittest only: echo module params into observer pins
    // (cant set pins to strings, so just echo the string length)
    *(cd->answer_value) = answer;
    *(cd->text_len) = strlen(text);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}

// export unittest observer pins
static struct comp_data *export_observer_pins(int owner_id, const char *name)
{
    struct comp_data *cd = hal_malloc(sizeof(struct comp_data));

    if (hal_pin_s32_newf(HAL_OUT, &cd->answer_value, owner_id, "%s.answer_value", name))
	return NULL;
    if (hal_pin_s32_newf(HAL_OUT, &cd->text_len, owner_id, "%s.text_len", name))
	return NULL;
    return cd;
}
