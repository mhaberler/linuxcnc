#ifndef _TEST_RING_SUITE

#define _TEST_RING_SUITE
#include "rtapi.h"

#include "rtapi_app.h"
#include "hal.h"
#include "hal_priv.h"
#include "hal_ring.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <ck_pr.h>
#include <rtapi.h>
#include "check-util.h"


#define PLOW 977  // primes
#define PHIGH 62869

// random sizes to give alignment a workout
size_t next_size(void)
{
    unsigned r;
    do {
	r = random() % PHIGH;
    }
    while (r < PLOW);
    // printf( "nextsize: %d\n", r);
    return r;
}

#define MAXRING 6

hal_ring_t *r[MAXRING];
ringbuffer_t rb[MAXRING];
#define TESTRING_SIZE 10240
#define MAX_WSIZE 1213  // some prime
ringbuffer_t testrb;


enum {
    SPSC_READ,
    SPSC_WRITE,
    SPSC_VREAD,
    SPSC_VWRITE,
};
static bool start;
#define NMSG 100000
struct rtest {
    int value;
    int count;
    int traffic;
    int op;
    ringbuffer_t *rb;
    char *name;
};

static void *test_ringop(void * arg)
{
    struct rtest *t = arg;
    int rx;
    const void *data;
    ringsize_t size;

    if (delta) aff_iterate(&a);
    while (!start);
    {
	WITH_THREAD_CPUTIME_N(t->name, t->count, RES_NS);
	while (t->value < t->count) {
	    switch (t->op) {
	    case SPSC_READ:
		while (record_read(t->rb, &data, &size));
		ck_assert_ptr_ne(data, NULL);
		rx = *(int *)data;
		ck_assert_int_eq(rx, t->value);
		ck_assert_int_eq(size, sizeof(t->value));
		t->value++;
		record_shift(t->rb);
		break;
	    case SPSC_VREAD:
		while (record_read(t->rb, &data, &size));
		ck_assert_ptr_ne(data, NULL);
		rx = *(int *)data;
		ck_assert_int_eq(rx, t->value);
		t->value++;
		t->traffic += size;
		record_shift(t->rb);
		break;
	    case SPSC_WRITE:
		while (record_write(t->rb, &t->value, sizeof(t->value)));
		t->value++;
		break;
	    case SPSC_VWRITE:
		size = random() % MAX_WSIZE +  sizeof(t->value);
		while (record_write(t->rb, &t->value, size));
		t->value++;
		t->traffic += size;
		break;
	    }
	    if (hop && (t->value % hop == 0))
		aff_iterate(&a);
	}
    }
    return NULL;
}

START_TEST(test_spsc_variable)
{
    int i;
    ringbuffer_t *b = &rb[2];
    struct rtest ta[] = {
	{.value = 0,
	 .traffic = 0,
	 .count = NMSG,
	 .op = SPSC_VREAD,
	 .rb = b,
	 .name = "reader"
	},
	{.value = 0,
	 .traffic = 0,
	 .count = NMSG,
	 .op = SPSC_VWRITE,
	 .rb = b,
	 .name = "writer"
	},
    };

    pthread_t tids[2];
    start = false;
    record_flush(b);
    for(i = 0; i < 2; i++){
	pthread_create(&(tids[i]), NULL, test_ringop, &ta[i]);
    }
    {
	WITH_PROCESS_CPUTIME_N("spsc variable", NMSG, RES_NS);
	start = true;
	for(i = 0; i < 2; i++){
	    pthread_join(tids[i], NULL);
	}
    }
    ck_assert_int_eq(NMSG, ta[0].value);
    ck_assert_int_eq(NMSG, ta[1].value);
    ck_assert_int_eq(ta[0].traffic, ta[1].traffic);
}
END_TEST

START_TEST(test_spsc_fixed)
{
    int i;
    ringbuffer_t *b = &rb[2];
    struct rtest ta[] = {
	{.value = 0,
	 .count = NMSG,
	 .op = SPSC_READ,
	 .rb = b,
	 .name = "reader"
	},
	{.value = 0,
	 .count = NMSG,
	 .op = SPSC_WRITE,
	 .rb = b,
	 .name = "writer"
	},
    };
    record_flush(b);

    pthread_t tids[2];
    start = false;

    for(i = 0; i < 2; i++){
	pthread_create(&(tids[i]), NULL, test_ringop, &ta[i]);
    }

    {
	WITH_PROCESS_CPUTIME_N("spsc fixed", NMSG, RES_NS);
	start = true;

	for(i = 0; i < 2; i++){
	    pthread_join(tids[i], NULL);
	}
    }
    ck_assert_int_eq(NMSG, ta[0].value);
    ck_assert_int_eq(NMSG, ta[1].value);
}
END_TEST


START_TEST(test_ring_alloc)
{
    unsigned flags;

    hal_ring_deletef("stream.shm");
    hal_ring_deletef("stream.hal");
    hal_ring_deletef("record.shm");
    hal_ring_deletef("record.hal");
    hal_ring_deletef("multi.shm");
    hal_ring_deletef("multi.hal");
    hal_ring_deletef("test.shm");

    r[0] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_STREAM,
			  "stream.shm");
    ck_assert_ptr_ne(r[0], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[0], &flags, "stream.shm"));
    ck_assert_int_eq(RINGTYPE_STREAM, flags & RINGTYPE_MASK);

    r[1] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_STREAM|ALLOC_HALMEM,
			  "stream.hal");
    ck_assert_ptr_ne(r[1], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[1], &flags, "stream.hal"));
    ck_assert_int_eq(RINGTYPE_STREAM, flags & RINGTYPE_MASK);
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);


    r[2] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_RECORD,
			  "record.shm");
    ck_assert_ptr_ne(r[2], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[2], &flags, "record.shm"));
    ck_assert_int_eq(RINGTYPE_RECORD, flags & RINGTYPE_MASK);

    r[3] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_RECORD|ALLOC_HALMEM,
			  "record.hal");
    ck_assert_ptr_ne(r[3], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[3], &flags, "record.hal"));
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);
    ck_assert_int_eq(RINGTYPE_RECORD, flags & RINGTYPE_MASK);

    r[4] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_MULTIPART,
			  "multi.shm");
    ck_assert_ptr_ne(r[4], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[4], &flags, "multi.shm"));
    ck_assert_int_eq(RINGTYPE_MULTIPART, flags & RINGTYPE_MASK);

    r[5] = halg_ring_newf(1, next_size(), next_size(),
			  RINGTYPE_MULTIPART|ALLOC_HALMEM,
			  "multi.hal");
    ck_assert_ptr_ne(r[5], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[5], &flags, "multi.hal"));
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);
    ck_assert_int_eq(RINGTYPE_MULTIPART, flags & RINGTYPE_MASK);

    hal_ring_t *tr = halg_ring_newf(1, TESTRING_SIZE, 0,
			  RINGTYPE_RECORD,
			  "test.shm");
    ck_assert_ptr_ne(tr, NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&testrb, &flags, "test.shm"));
    ck_assert_int_eq(RINGTYPE_RECORD, flags & RINGTYPE_MASK);
    ck_assert_int_eq(0, hal_ring_detach(&testrb));

}
END_TEST


START_TEST(test_ring_alignment)
{

    for (int i = 0; i < 6; i++) {
	// investigate alignments - basic structs, buffer
	ck_assert_int_eq(0, RTAPI_ALIGNED(rb[i].header,
					  RTAPI_CACHELINE));
	ck_assert_int_eq(0, RTAPI_ALIGNED(rb[i].trailer,
					  RTAPI_CACHELINE));
	ck_assert_int_eq(0, RTAPI_ALIGNED(rb[i].buf,
					  RTAPI_CACHELINE));
	ck_assert_int_eq(0, RTAPI_ALIGNED(rb[i].scratchpad,
					  RTAPI_CACHELINE));
	// the head index
	ck_assert_int_eq(0, RTAPI_ALIGNED(&rb[i].header->head,
					  RTAPI_CACHELINE));
	// the tail index
	ck_assert_int_eq(0, RTAPI_ALIGNED(&rb[i].trailer->tail,
					  RTAPI_CACHELINE));
	// ring storage is cache aligned (= all space due to alignment
	// has been used)
	ck_assert_int_eq(0, RTAPI_ALIGNED(rb[i].header->size,
					  RTAPI_CACHELINE));

    }

    for (int i = 0; i < 6; i++)
	ck_assert_int_eq(0, hal_ring_detach(&rb[i]));

}
END_TEST


Suite * ring_suite(void)
{
    Suite *s;
    TCase *tc;
    s = suite_create("ringbuffer");
    tc = tcase_create("alignment");

    tcase_add_test(tc, test_ring_alloc);
    tcase_add_test(tc, test_ring_alignment);
    tcase_add_test(tc, test_spsc_fixed);
    tcase_add_test(tc, test_spsc_variable);
    tcase_set_timeout(tc, 10.0);
    suite_add_tcase(s, tc);

    return s;
}
#endif /* _TEST_RING_SUITE */
