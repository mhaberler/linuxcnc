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

    tcase_add_test(tc_core, test_hal_comp);
    suite_add_tcase(s, tc_core);

    return s;
}


#endif /* _TEST_HAL_SUITE */
