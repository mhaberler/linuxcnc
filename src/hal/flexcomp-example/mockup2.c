
#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"

MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("mockup for instantiation HAL API");
MODULE_LICENSE("GPL");

static int comp_id;
static char *compname = "ufdemo";

static int npins;
RTAPI_INSTP_INT(npins, "the npins instance parameter");

struct inst_data {
    int npins;
    hal_s32_t *pin[0];
};

// thread function.
static void funct(void *arg, long period)
{
}

static int instantiate(const char *name)
{
    int i;
    struct intst_data *ip = NULL;


    int inst_id = hal_inst_create(name, comp_id,
			      sizeof(struct inst_data) + sizeof(hal_s32_t *)*npins,
			      (void **)&ip);
    if ((inst_id < 0) || (ip == NULL))
	return -1;

    ip->npins = npins;
    for (i = 0; i < npins; i++) {
	if (hal_pin_s32_newf(HAL_IN, &(ip->pin[i)), compId, "%s.pin%d", name, i))
	    return -1;
    }
    if (hal_export_funct("%s.funct", funct, ip, 0, 0, comp_id))
	return -1;
    return 0;
}

static int delete(const char *name, void *inst)
{
    struct intst_data *ip = inst;
    int i;

    free_funct_struct(hal_funct_t * funct); // FIND FUNCT!!
    for (i = 0; i < npins; i++) {
	// FIND PINDESC for  &(ip->pin[i)
	unlink_pin(pindesc);
	delete pin;
    }
    hal_inst_delete(name);
    // hal_free(ip);
}


static int answer = 42;
RTAPI_MP_INT(answer, "a random module parameter");

int rtapi_app_main(void)
{

    comp_id = hal_init_inst(compname, ip, instantiate, delete);
    if (comp_id < 0)
	return -1;

    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id); // calls delete() on all insts
}
