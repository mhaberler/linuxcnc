
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("mockup for HAL instantiation API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "fcomp";

//static int npins;
//RTAPI_INSTP_INT(npins, "the npins instance parameter");

struct inst_data {
    hal_s32_t *pin;
};

// thread function.
static void funct(void *arg, long period)
{
}

static int instantiate(const char *name, const int argc, const char**argv)
{
    struct inst_data *ip;

    int inst_id = hal_inst_create(name, comp_id,
				  sizeof(struct inst_data),
				  (void **)&ip);
    if (inst_id < 0)
	return -1;

    hal_print_msg(RTAPI_MSG_ERR,"ip=%p hal_shmem_base=%p\n", ip, hal_shmem_base);

    if (halinst_pin_s32_newf(HAL_IN, &(ip->pin), comp_id, inst_id, "%s.pin", name))
	return -1;

    if (halinst_export_functf(funct, ip, 0, 0, comp_id, inst_id, "%s.funct", name))
	return -1;
    return 0;
}

static int delete(const char *name, void *inst, const int inst_size)
{
    const int argc = fa_argc(fa);
    const char **argv = fa_argv(fa);

    rtapi_print_msg(RTAPI_MSG_INFO, "%s: userfunct '%s' called, arg='%s' argc=%d\n",
		    compname,  fa_funct_name(fa), (const char *)fa_arg(fa), argc);
    int i;
    for (i = 0; i < argc; i++)
	rtapi_print_msg(RTAPI_MSG_INFO, "    argv[%d] = \"%s\"\n",
			i,argv[i]);
    return argc;
}


static int answer = 42;
RTAPI_MP_INT(answer, "a random module parameter");

int rtapi_app_main(void)
{

    comp_id = hal_xinit(compname, TYPE_RT, 0, 0, instantiate, delete);
    if (comp_id < 0)
	return -1;

    hal_ready(comp_id);

    // fake halcmd
    instantiate("foo", 0, NULL);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}
