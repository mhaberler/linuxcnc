#ifndef _TEST_HAL_SUITE
#define _TEST_HAL_SUITE

#include "rtapi.h"
#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

extern int comp_id;
static hal_float_t c = 42.0;

#define SAMPLES 10000

START_TEST(test_rtapi_timing)
{
    // not really HAL, but hile we are here..
    // determine the rtapi_get_clocks/rtapi_get_time scaling factor

    long long int t1,t2,c1,c2;
    t1 = rtapi_get_time();
    c1 = rtapi_get_clocks();
    sleep(1);
    t2 = rtapi_get_time();
    c2 = rtapi_get_clocks();
    double scale = (double)(c2 - c1)/(double)(t2-t1);
    // on VM's rtapi_get_clocks() comes out pretty random
    fprintf(stderr, "rtapi_get_clocks:rtapi_get_time scale: %f\n", scale);
    fprintf(stderr, "cdiff=%lld tdiff=%lld\n", (c2-c1),(t2-t1));

    volatile long long int s;
    {
	int i;
    	WITH_PROCESS_CPUTIME_N("rtapi_get_clocks ns/op", SAMPLES, RES_NS);
	for (i = 0; i < SAMPLES; i++)
	    s = rtapi_get_clocks();
    }
    {
	int i;
    	WITH_PROCESS_CPUTIME_N("rtapi_get_time ns/op", SAMPLES, RES_NS);
	for (i = 0; i < SAMPLES; i++)
	    s = rtapi_get_time();
    }
}
END_TEST

START_TEST(test_hal_comp)
{
    ck_assert_int_ne(comp_id, 0);
    bit_pin_ptr out = halx_pin_bit_newf(HAL_OUT, comp_id, "testme.out");
    ck_assert_int_ne(out._bp, 0);
    hal_pin_t *pin = (hal_pin_t *)hal_ptr(out._bp);
    hal_data_u *u =  (hal_data_u *)hal_ptr(pin->data_ptr);

    ck_assert_int_eq(pin_type(pin), HAL_BIT);
    ck_assert_int_eq(pin_dir(pin), HAL_OUT);

    // assure full hal_data_u zero inited:
    for (int i = 0; i < 8; i++)
	ck_assert_int_eq(u->_bytes[i], 0);
    set_bit_pin(out,true);

    // assure store was byte-wide only
    ck_assert_int_eq(u->_bytes[0], 1);
    for (int i = 1; i < 8; i++)
	ck_assert_int_eq(u->_bytes[i], 0);

    float_pin_ptr fo = halx_pin_float_newf(HAL_OUT, comp_id, "testme.fout");
    set_float_pin(fo, c);
    pin = (hal_pin_t *)hal_ptr(fo._fp);
    u =  (hal_data_u *)hal_ptr(pin->data_ptr);

    ck_assert_msg(abs(u->_f - c) < 1e-9, "float get/setpin do not agree: %f %f", u->_f, c);

    hal_float_t value = get_float_pin(fo);
    ck_assert_msg(abs(value - c) < 1e-9, "float get/setpin do not agree: %f %f", value, c);
}
END_TEST


Suite * hal_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("HAL");

    tc_core = tcase_create("hal");

    tcase_add_test(tc_core, test_rtapi_timing);
    tcase_add_test(tc_core, test_hal_comp);
    suite_add_tcase(s, tc_core);

    return s;
}


#endif /* _TEST_HAL_SUITE */
