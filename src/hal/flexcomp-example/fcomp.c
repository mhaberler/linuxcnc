
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("mockup for HAL instantiation API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "fcomp";

struct inst_data {
    hal_s32_t *pin;
};

// thread function.
static void funct(void *arg, long period)
{
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

    hal_print_msg(RTAPI_MSG_ERR,"%s inst=%s argc=%d\n",__FUNCTION__, name, argc);

    // init HAL objects in the instance data area returned
    if (halinst_pin_s32_newf(HAL_IN, &(ip->pin), comp_id, inst_id, "%s.pin", name))
	return -1;
    // export a thread function
    if (halinst_export_functf(funct, ip, 0, 0, comp_id, inst_id, "%s.funct", name))
	return -1;
    return 0;
}

// custom destructor - normally not needed
// pins, params, and functs are automatically deallocated when
// deleting an instance.
// some objects like vtables, rings, threads are not owned by a component
// so such objects may require a custom destructor for
// cleanup actions
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

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}
