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
#include <ring.h>


// some primes to give alignment a workout
#define RSIZE 5843
#define SPSIZE 1889

#define MAXRING 6
hal_ring_t *r[MAXRING];
ringbuffer_t rb[MAXRING];


START_TEST(test_ring_alloc)
{
    unsigned flags;

    hal_ring_deletef("stream.shm");
    hal_ring_deletef("stream.hal");
    hal_ring_deletef("record.shm");
    hal_ring_deletef("record.hal");
    hal_ring_deletef("multi.shm");
    hal_ring_deletef("multi.hal");

    r[0] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_STREAM,
			  "stream.shm");
    ck_assert_ptr_ne(r[0], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[0], &flags, "stream.shm"));
    ck_assert_int_eq(RINGTYPE_STREAM, flags & RINGTYPE_MASK);

    r[1] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_STREAM|ALLOC_HALMEM,
			  "stream.hal");
    ck_assert_ptr_ne(r[1], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[1], &flags, "stream.hal"));
    ck_assert_int_eq(RINGTYPE_STREAM, flags & RINGTYPE_MASK);
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);


    r[2] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_RECORD,
			  "record.shm");
    ck_assert_ptr_ne(r[2], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[2], &flags, "record.shm"));
    ck_assert_int_eq(RINGTYPE_RECORD, flags & RINGTYPE_MASK);

    r[3] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_RECORD|ALLOC_HALMEM,
			  "record.hal");
    ck_assert_ptr_ne(r[3], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[3], &flags, "record.hal"));
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);
    ck_assert_int_eq(RINGTYPE_RECORD, flags & RINGTYPE_MASK);

    r[4] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_MULTIPART,
			  "multi.shm");
    ck_assert_ptr_ne(r[4], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[4], &flags, "multi.shm"));
    ck_assert_int_eq(RINGTYPE_MULTIPART, flags & RINGTYPE_MASK);

    r[5] = halg_ring_newf(1, RSIZE, SPSIZE,
			  RINGTYPE_MULTIPART|ALLOC_HALMEM,
			  "multi.hal");
    ck_assert_ptr_ne(r[5], NULL);
    ck_assert_int_eq(0,  hal_ring_attachf(&rb[5], &flags, "multi.hal"));
    ck_assert_int_eq(ALLOC_HALMEM, flags & ALLOC_HALMEM);
    ck_assert_int_eq(RINGTYPE_MULTIPART, flags & RINGTYPE_MASK);

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
    suite_add_tcase(s, tc);

    return s;
}
#endif /* _TEST_RING_SUITE */
