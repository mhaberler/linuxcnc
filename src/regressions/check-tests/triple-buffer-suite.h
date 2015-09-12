#ifndef _TEST_TRIPLE_BUFFER_SUITE
#define _TEST_TRIPLE_BUFFER_SUITE

#include <triple-buffer.h>


// see if this works for us:
// http://remis-thoughts.blogspot.co.at/2012/01/triple-buffering-as-concurrency_30.html

START_TEST(test_triple_buffer)
{
    TRIPLE_BUFFER_TYPE(TestStruct, int);
    TRIPLE_BUFFER_NEW(it, TestStruct);

    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 0);

    /* Test 1 */
    *TRIPLE_BUFFER_WRITE_PTR(it) = 3;
    TRIPLE_BUFFER_FLIP_WRITER(it);

    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 0);

    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 3);

    /* Test 2 */

    *TRIPLE_BUFFER_WRITE_PTR(it) = 4;
    TRIPLE_BUFFER_FLIP_WRITER(it);
    *TRIPLE_BUFFER_WRITE_PTR(it) = 5;

    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 4);
    TRIPLE_BUFFER_FLIP_WRITER(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 4);
    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 5);

    TRIPLE_BUFFER_FLIP_WRITER(it);
    *TRIPLE_BUFFER_WRITE_PTR(it) = 6;
    TRIPLE_BUFFER_FLIP_WRITER(it);

    TRIPLE_BUFFER_NEW_SNAP(it);

    *TRIPLE_BUFFER_WRITE_PTR(it) = 7;
    TRIPLE_BUFFER_FLIP_WRITER(it);
    *TRIPLE_BUFFER_WRITE_PTR(it) = 8;
    TRIPLE_BUFFER_FLIP_WRITER(it);

    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 6);

    *TRIPLE_BUFFER_WRITE_PTR(it) = 7;
    TRIPLE_BUFFER_FLIP_WRITER(it);
    *TRIPLE_BUFFER_WRITE_PTR(it) = 8;
    TRIPLE_BUFFER_FLIP_WRITER(it);

    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it), 8);
    TRIPLE_BUFFER_NEW_SNAP(it);
    ck_assert_int_eq(*TRIPLE_BUFFER_SNAP_PTR(it),  8);
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
