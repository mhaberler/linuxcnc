#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_accessor.h"

/* lran2.h
 * by Wolfram Gloger 1996.
 *
 * A small, portable pseudo-random number generator.
 */

#define LRAN2_MAX 714025l /* constants for portable */
#define IA	  1366l	  /* random number generator */
#define IC	  150889l /* (see e.g. `Numerical Recipes') */

struct lran2_st {
    long x, y, v[97];
};

static void
lran2_init(struct lran2_st* d, long seed)
{
    long x;
    int j;

    x = (IC - seed) % LRAN2_MAX;
    if(x < 0) x = -x;
    for(j=0; j<97; j++) {
	x = (IA*x + IC) % LRAN2_MAX;
	d->v[j] = x;
    }
    d->x = (IA*x + IC) % LRAN2_MAX;
    d->y = d->x;
}

static long
lran2(struct lran2_st* d)
{
    int j = (d->y % 97);

    d->y = d->v[j];
    d->x = (IA*d->x + IC) % LRAN2_MAX;
    d->v[j] = d->x;
    return d->y;
}


MODULE_AUTHOR("Michael Haberler");
MODULE_DESCRIPTION("pseudorandom number generator");
MODULE_LICENSE("GPL");
RTAPI_TAG(HAL,HC_INSTANTIABLE);

static int low = 0;
RTAPI_IP_INT(low, "lower end of range");

static int high = 0;
RTAPI_IP_INT(high, "upper end of range");

static int type = 0;
RTAPI_IP_INT(type, "output type: 0:s32, 1: float");

static int comp_id;
static char *compname = "random";

struct inst_data {
    int wantfloat;
    int low, high;
    bool scaled;
    struct lran2_st status;
    s32_pin_ptr sout;
    float_pin_ptr fout;
};

static int random(void *arg, const hal_funct_args_t *fa)
{
    struct inst_data *ip = arg;
    long r = lran2(&ip->status);

    if (ip->scaled) {
    }
    if (ip->wantfloat)
	set_float_pin(ip->fout, (hal_float_t)r);
    else
	set_s32_pin(ip->sout, r);

    return 0;
}

static int instantiate_random(const char *name,
			      const int argc,
			      const char**argv)
{
    struct inst_data *ip;
    int inst_id;

    if ((inst_id = hal_inst_create(name, comp_id,
				   sizeof(struct inst_data),
				   (void **)&ip)) < 0)
	return -1;

    ip->wantfloat = (type != 0);
    ip->low = low;
    ip->high = high;
    ip->scaled = (high !=0)||(low != 0);

    if (ip->wantfloat) {
	ip->fout = halx_pin_float_newf(HAL_OUT, inst_id, "%s.out", name);
	if (float_pin_null(ip->fout))
	    return _halerrno;
    } else {
	ip->sout = halx_pin_s32_newf(HAL_OUT, inst_id, "%s.out", name);
	if (s32_pin_null(ip->sout))
	    return _halerrno;
    }
    lran2_init(&ip->status, (long) rtapi_get_clocks());
    hal_export_xfunct_args_t xfunct_args = {
        .type = FS_XTHREADFUNC,
        .funct.x = random,
        .arg = ip,
        .uses_fp = 0,
        .reentrant = 0,
        .owner_id = inst_id
    };
    return hal_export_xfunctf(&xfunct_args, "%s.funct", name);
}


int rtapi_app_main(void)
{
    comp_id = hal_xinit(TYPE_RT, 0, 0,
			instantiate_random,
			NULL, compname);
    if (comp_id < 0)
	return comp_id;
    hal_ready(comp_id);
    return 0;
}

void rtapi_app_exit(void)
{
    hal_exit(comp_id);
}
