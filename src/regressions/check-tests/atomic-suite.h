#ifndef _TEST_ATOMIC_SUITE
#define _TEST_ATOMIC_SUITE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <ck_pr.h>
#include <rtapi.h>
#include <rtapi_atomics.h>

#define OP_COUNT 10000
#define MAXTHREADS 4

static bool start;

struct test {
    int value;
    int op;
    int thcnt;
    int count;
};


enum {
    INCR,
    INCR_MB,
    CK_ATOMIC_INCR,
    CK_ATOMIC_CAS,
    GCC_ATOMIC_INCR,
    GCC_ATOMIC_CAS,
};
typedef void *(func)(void *);

static void *test_increment(void * arg)
{
    struct test *t = arg;
    int i, t1;

    while (!start);

    for(i = 0; i < t->count; i++){
	switch (t->op) {
	case INCR:
	    t->value++;
	    break;
	case INCR_MB:
	    rtapi_smp_rmb();
	    t->value++;
	    rtapi_smp_wmb();
	    break;
	case CK_ATOMIC_INCR:
	    ck_pr_add_int(&t->value, 1);
	    break;
	case GCC_ATOMIC_INCR:
	    __atomic_add_fetch(&t->value, 1, RTAPI_MEMORY_MODEL);
	    break;

	case CK_ATOMIC_CAS:
	    do {
		ck_pr_fence_load();
		t1 = ck_pr_load_int(&t->value);
	    } while (!ck_pr_cas_int_value(&t->value,
					  t1,
					  t1 + 1,
					  &t1));
	    break;
	case GCC_ATOMIC_CAS:
	    t1 = __atomic_load_n(&t->value, RTAPI_MEMORY_MODEL);
	    while(!__atomic_compare_exchange_n(&t->value,
					       &t1,
					       t1 + 1,
					       false,
					       RTAPI_MEMORY_MODEL,
					       RTAPI_MEMORY_MODEL));

	    break;
	}
    }
    return NULL;
}

void testrun(char *text, func what, struct test *t)
{
    int i;

    t->value = 0;
    start = false;
    pthread_t tids[MAXTHREADS];

    for(i = 0; i < t->thcnt; i++){
        pthread_create(&(tids[i]), NULL, what, t);
    }
    start = true;
    for(i = 0; i < t->thcnt; i++){
        pthread_join(tids[i], NULL);
    }
    fprintf(stderr, "%s: value == %d, expected %d\n",
	    text, t->value, t->count * t->thcnt);
}

START_TEST(test_smp_increment)
{
    struct test t;
    t.value = 0;
    t.thcnt = MAXTHREADS;
    t.count = OP_COUNT;
    fprintf(stderr, "%s:%d: %s()\n", __FILE__, __LINE__, __FUNCTION__);
    {
	WITH_PROCESS_CPUTIME_N("legacy IO pin incr", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = INCR;
	testrun("legacy IO pin increment", test_increment, &t);
    }
    {
	WITH_PROCESS_CPUTIME_N("incr + barriers", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = INCR_MB;
	testrun("increment + read/write barriers", test_increment, &t);
    }
    {
	WITH_PROCESS_CPUTIME_N("ck atomic incr", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = CK_ATOMIC_INCR;
	testrun("concurrencykit atomic increment", test_increment, &t);
	ck_assert_int_eq(t.value,  t.count * t.thcnt);
    }
    {
	WITH_PROCESS_CPUTIME_N("gcc atomic incr", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = GCC_ATOMIC_INCR;
	testrun("gcc intrinsics atomic increment", test_increment, &t);
	ck_assert_int_eq(t.value,  t.count * t.thcnt);
    }
    {
	WITH_PROCESS_CPUTIME_N("ck CAS loop", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = CK_ATOMIC_CAS;
	testrun("concurrencykit CAS loop", test_increment, &t);
	ck_assert_int_eq(t.value,  t.count * t.thcnt);
    }
    {
	WITH_PROCESS_CPUTIME_N("gcc CAS loop", OP_COUNT *MAXTHREADS, RES_NS);
	t.op = GCC_ATOMIC_CAS;
	testrun("gcc intrinsics CAS loop", test_increment, &t);
	ck_assert_int_eq(t.value,  t.count * t.thcnt);
    }
    fprintf(stderr, "\n");

}
END_TEST


START_TEST(test_ck_ops)
{
    // test my understanding of the ck pointer swap operation
    // the manpage is a tad fuzzy: http://www.concurrencykit.org/doc/ck_pr_fas.html
    int a = 10;
    int b = 20;
    int *ap = &a;
    char *s = "1234";
    char *q = NULL;

#ifdef HAVE_CK
    ck_pr_fas_ptr(&ap, &b);
    ck_assert_int_eq(*ap, b);

    int p = -123;
    p = ck_pr_load_ptr(&b);
    ck_assert_int_eq(p, b);


    q = ck_pr_load_ptr((void **)&s);
    ck_assert_int_eq(strcmp(q,s), 0);
#endif



    q = "foo";

    __atomic_load(&s, &q, RTAPI_MEMORY_MODEL);
    ck_assert_int_eq(strcmp(q,s), 0);
}
END_TEST

START_TEST(test_rtapi_atomics)
{
    // _very_ basic confidence tests for the rtapi_* wrappers
    __s32 s = -4711;
    __s32 stmp  = 0;

    stmp = rtapi_load_s32(&s);
    ck_assert_int_eq(s, stmp);
    stmp  = 0;
    rtapi_store_s32(&stmp, 314);
    ck_assert_int_eq(stmp, 314);

    rtapi_add_s32(&s, 10);
    ck_assert_int_eq(s, -4701);

    __u32 u = 815;
    __u32 utmp  = 0;
    utmp = rtapi_load_u32(&u);
    ck_assert_int_eq(u, utmp);
    utmp  = 0;
    rtapi_store_u32(&utmp, 12345);
    ck_assert_int_eq(utmp, 12345);

    char *str = "1234";
    char *q = NULL;
    q = rtapi_load_ptr((void **)&str);
    ck_assert_int_eq(strcmp(q,str), 0);

    char *blah = "blah";
    q = NULL;
    rtapi_store_ptr((void **)&q, blah);
    ck_assert_ptr_eq(q, blah);
    ck_assert_int_eq(strcmp(q,blah), 0);

    __u64 v = 0x1234567800112233;
    __u64 dest = 0;

    dest =  rtapi_load_u64((uint64_t*)&v);
    ck_assert_int_eq(dest, v);

    rtapi_inc_u64((uint64_t*)&v);
    ck_assert_int_eq(dest+1, v);

    __s64 sv = 0x1234567800112233;
    __s64 sdest = 0;

    sdest =  rtapi_load_s64((int64_t*)&sv);
    ck_assert_int_eq(sdest, sv);

    u = 815;
    utmp  = 0;
    ck_assert(rtapi_cas_u32(&utmp, 4711, 815) == false); // fail - not equal
    ck_assert_int_eq(utmp, 0);
    ck_assert(rtapi_cas_u32(&utmp, 0, 815) == true); // succeed
    ck_assert_int_eq(utmp, u);

    s = 21718;
    stmp = 123;
    ck_assert(rtapi_cas_s32(&stmp, 0, 815) == false);
    ck_assert_int_eq(stmp, 123);
    ck_assert(rtapi_cas_s32(&stmp, 123, 815) == true);
    ck_assert_int_eq(stmp, 815);

    v = 0x1234567800112233;
    dest = 0;
    ck_assert(rtapi_cas_u64((uint64_t*)&dest, 4711, v) == false); // fail - not equal
    ck_assert_int_eq(dest, 0);
    ck_assert(rtapi_cas_u64((uint64_t*)&dest, 0, v) == true);
    ck_assert_int_eq(dest, v);
}
END_TEST

Suite * atomic_suite(void)
{
    Suite *s;
    TCase *tc;
    s = suite_create("smp");


    tc = tcase_create("increment");
    tcase_set_timeout(tc, 30.0);

    tcase_add_test(tc, test_smp_increment);
    tcase_add_test(tc, test_ck_ops);
    tcase_add_test(tc, test_rtapi_atomics);
    suite_add_tcase(s, tc);

    return s;
}
#endif /* _TEST_ATOMIC_SUITE */
