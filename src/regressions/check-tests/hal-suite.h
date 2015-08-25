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

static int id;

void hal_setup(void)
{
    ck_assert_ptr_ne(hal_data, NULL); // realtime must be running
    id = hal_init("testme");
    ck_assert_int_ge(id, 0);
    hal_ready(id);
}

void hal_teardown(void)
{
    if (id > 0) {
	int rc = hal_exit(id);
	id = 0;
	ck_assert_int_eq(rc, 0);
    }
}

START_TEST(test_hal_comp)
{

    bit_pin_ptr out = halx_pin_bit_newf(HAL_OUT, id, "testme.out");
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

    //ck_assert_int_eq(1, 0);
}
END_TEST

Suite * hal_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("HAL");

    tc_core = tcase_create("hal");

    // runs hal_init/hal_exit in the parent process
    tcase_add_unchecked_fixture(tc_core, hal_setup, hal_teardown);

    tcase_add_test(tc_core, test_hal_comp);
    suite_add_tcase(s, tc_core);

    return s;
}


#endif /* _TEST_HAL_SUITE */
