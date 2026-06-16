#include "unity.h"
#include "../../src/runtime/teko_delayed.h"

// Phase 14 (14.C) — delayed (timed) channel runtime (source of truth). Validates timestamped
// scheduling against an absolute ns deadline, delivery-deadline-ordered release as REAL time
// (now_ns, passed in) advances, the non-blocking statuses, capacity, and close. The runtime is
// clock-source-agnostic + KAT-deterministic: these tests pass explicit `now` values (the wrappers
// supply the real monotonic clock at runtime). Values below are plain integers used as ns ticks.

void test_teko_delayed_releases_in_time_order(void) {
    TekoDelayed* d = teko_delayed_open(8);
    TEST_ASSERT_NOT_NULL(d);
    int32_t v = 0;
    // Send at now=0 with delays 30/10/20 -> deadlines 30/10/20 (out of order).
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 30, 30, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 10, 10, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 20, 20, 0));

    // Nothing due at now=0: NOT_READY, not EMPTY.
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_poll(d, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_recv(d, &v, 0));

    // now=15: only the @10 message is due.
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 15)); TEST_ASSERT_EQUAL_INT32(10, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_recv(d, &v, 15)); // @20/@30 not yet

    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 25)); TEST_ASSERT_EQUAL_INT32(20, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 35)); TEST_ASSERT_EQUAL_INT32(30, v);

    TEST_ASSERT_EQUAL_INT(TEKO_DLY_EMPTY, teko_delayed_recv(d, &v, 35)); // drained
    teko_delayed_free(d);
}

void test_teko_delayed_same_time_is_fifo(void) {
    TekoDelayed* d = teko_delayed_open(8);
    int32_t v = 0;
    teko_delayed_send(d, 1, 5, 0);
    teko_delayed_send(d, 2, 5, 0); // same deadline — released in send order
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 5)); TEST_ASSERT_EQUAL_INT32(1, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 5)); TEST_ASSERT_EQUAL_INT32(2, v);
    teko_delayed_free(d);
}

void test_teko_delayed_capacity_and_close(void) {
    TekoDelayed* d = teko_delayed_open(2);
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK,   teko_delayed_send(d, 1, 1, 0)); // deadline 1
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK,   teko_delayed_send(d, 2, 1, 0)); // deadline 1
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_FULL, teko_delayed_send(d, 3, 1, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 1)); TEST_ASSERT_EQUAL_INT32(1, v); // frees a slot
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 4, 0, 1)); // deadline 1
    // Close: a send is rejected; recv drains the due ones then reports CLOSED.
    teko_delayed_close(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_CLOSED, teko_delayed_send(d, 5, 0, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 1)); TEST_ASSERT_EQUAL_INT32(2, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v, 1)); TEST_ASSERT_EQUAL_INT32(4, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_CLOSED, teko_delayed_recv(d, &v, 1));
    teko_delayed_free(d);
}

void test_teko_delayed_badarg(void) {
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_send(NULL, 1, 1, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_recv(NULL, &v, 0));
    TekoDelayed* d = teko_delayed_open(4);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_recv(d, NULL, 0));
    teko_delayed_free(d);
}
