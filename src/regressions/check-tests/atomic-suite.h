#ifndef _TEST_ATOMIC_SUITE
#define _TEST_ATOMIC_SUITE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <ck_pr.h>
#include <rtapi.h>

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

Suite * atomic_suite(void)
{
    Suite *s;
    TCase *tc;
    s = suite_create("smp");


    tc = tcase_create("increment");
    tcase_set_timeout(tc, 30.0);


    tcase_add_test(tc, test_smp_increment);
    suite_add_tcase(s, tc);

    return s;
}
#endif /* _TEST_ATOMIC_SUITE */
