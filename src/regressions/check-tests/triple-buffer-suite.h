#ifndef _TEST_TRIPLE_BUFFER_SUITE
#define _TEST_TRIPLE_BUFFER_SUITE


// see if this works for us:
// http://remis-thoughts.blogspot.co.at/2012/01/triple-buffering-as-concurrency_30.html
#include <triple-buffer.h>

TB_FLAG_FAST(tb);
int tb_buf[3];

START_TEST(test_triple_buffer)
{
    rtapi_tb_init(&tb);

    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 0);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);  // no new data

    /* Test 1 */
    tb_buf[rtapi_tb_write(&tb)] = 3;
    rtapi_tb_flip_writer(&tb);   // commit 3

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // flipped - new data available
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 3);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);     // no new data

    /* Test 2 */
    tb_buf[rtapi_tb_write(&tb)] = 4;
    rtapi_tb_flip_writer(&tb);                           // commit 4
    tb_buf[rtapi_tb_write(&tb)] = 5;                     // 5 not committed

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 4);     // equals last committed, 4
    rtapi_tb_flip_writer(&tb);                           // commit 5
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 4);     // still 4 since no new snap

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 5);     // must be 5 now since new snap

    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 6;                     // 6 but not committed
    rtapi_tb_flip_writer(&tb);
    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // must be new data
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 6);     // must be 6 now since new snap

    tb_buf[rtapi_tb_write(&tb)] = 7;
    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 8;
    rtapi_tb_flip_writer(&tb);
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 6); // must be still 6 since no new snap

    tb_buf[rtapi_tb_write(&tb)] = 7;
    rtapi_tb_flip_writer(&tb);
    tb_buf[rtapi_tb_write(&tb)] = 8;
    rtapi_tb_flip_writer(&tb);

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), true);      // must be new data, 8
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 8);

    ck_assert_int_eq(rtapi_tb_new_snap(&tb), false);      // no new data, 8
    ck_assert_int_eq(tb_buf[rtapi_tb_snap(&tb)], 8);
}
END_TEST

Suite * triple_buffer_suite(void)
{
    Suite *s;
    TCase *tc_core;
    s = suite_create("triple buffer exploration");

    tc_core = tcase_create("triple buffer");
    tcase_add_test(tc_core, test_triple_buffer);
    suite_add_tcase(s, tc_core);

    return s;
}

#endif
