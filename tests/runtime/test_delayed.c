#include "unity.h"
#include "../../src/runtime/teko_delayed.h"

// Phase 14 (14.C) — delayed (timed) channel runtime (source of truth). Validates timestamped
// scheduling, delivery-time-ordered release as the logical clock advances, the non-blocking
// statuses, capacity, and close.

void test_teko_delayed_releases_in_time_order(void) {
    TekoDelayed* d = teko_delayed_open(8);
    TEST_ASSERT_NOT_NULL(d);
    int32_t v = 0;
    // Send out of time order: value 30 due@30, 10 due@10, 20 due@20.
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 30, 30));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 10, 10));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 20, 20));

    // Nothing due yet (clock at 0): NOT_READY, not EMPTY.
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_poll(d));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_recv(d, &v));

    teko_delayed_advance(d, 15);                       // clock = 15: only the @10 message is due
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(10, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_NOT_READY, teko_delayed_recv(d, &v)); // @20/@30 not yet

    teko_delayed_advance(d, 10);                       // clock = 25: the @20 message is now due
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(20, v);

    teko_delayed_advance(d, 10);                       // clock = 35: the @30 message is due
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(30, v);

    TEST_ASSERT_EQUAL_INT(TEKO_DLY_EMPTY, teko_delayed_recv(d, &v)); // drained
    TEST_ASSERT_EQUAL_UINT64(35, teko_delayed_now(d));
    teko_delayed_free(d);
}

void test_teko_delayed_same_time_is_fifo(void) {
    TekoDelayed* d = teko_delayed_open(8);
    int32_t v = 0;
    teko_delayed_send(d, 1, 5);
    teko_delayed_send(d, 2, 5); // same delivery time — released in send order
    teko_delayed_advance(d, 5);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(1, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(2, v);
    teko_delayed_free(d);
}

void test_teko_delayed_capacity_and_close(void) {
    TekoDelayed* d = teko_delayed_open(2);
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK,   teko_delayed_send(d, 1, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK,   teko_delayed_send(d, 2, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_FULL, teko_delayed_send(d, 3, 1));
    teko_delayed_advance(d, 1);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); // frees a slot
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_send(d, 4, 0));
    // Close: a send is rejected; recv drains the due ones then reports CLOSED.
    teko_delayed_close(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_CLOSED, teko_delayed_send(d, 5, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(2, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_OK, teko_delayed_recv(d, &v)); TEST_ASSERT_EQUAL_INT32(4, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_CLOSED, teko_delayed_recv(d, &v));
    teko_delayed_free(d);
}

void test_teko_delayed_badarg(void) {
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_send(NULL, 1, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_recv(NULL, &v));
    TekoDelayed* d = teko_delayed_open(4);
    TEST_ASSERT_EQUAL_INT(TEKO_DLY_BADARG, teko_delayed_recv(d, NULL));
    teko_delayed_free(d);
}
