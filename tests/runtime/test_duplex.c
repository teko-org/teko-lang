#include "unity.h"
#include "../../src/runtime/teko_duplex.h"

// Phase 14 (14.B) — duplex channel runtime (source of truth). Validates the symmetric
// bidirectional rings, capacity/FULL, the close/drop state machine, drain-after-close, and
// the structured statuses (no hang on a closed/dropped peer).

void test_teko_duplex_bidirectional_roundtrip(void) {
    TekoDuplex* d = teko_duplex_open(4);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DUPLEX_OPEN, teko_duplex_state(d));

    int32_t v = 0;
    // 0 -> 1
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_send(d, 0, 111));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_recv(d, 1, &v));
    TEST_ASSERT_EQUAL_INT32(111, v);
    // 1 -> 0 (the other, isolated direction)
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_send(d, 1, 222));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_recv(d, 0, &v));
    TEST_ASSERT_EQUAL_INT32(222, v);

    // Directions are isolated: a send on 0->1 is NOT visible to recv(1) (which reads 0->1)...
    // i.e. recv(ep=0) reads 1->0 only. With both rings drained, both ends read EMPTY.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_EMPTY, teko_duplex_recv(d, 0, &v));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_EMPTY, teko_duplex_recv(d, 1, &v));
    teko_duplex_free(d);
}

void test_teko_duplex_capacity_full(void) {
    TekoDuplex* d = teko_duplex_open(2);
    TEST_ASSERT_NOT_NULL(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK,   teko_duplex_send(d, 0, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK,   teko_duplex_send(d, 0, 2));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_FULL, teko_duplex_send(d, 0, 3)); // ring 0->1 is full
    // The opposite direction is independent and still has space.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK,   teko_duplex_send(d, 1, 9));
    teko_duplex_free(d);
}

void test_teko_duplex_close_drains_then_signals(void) {
    TekoDuplex* d = teko_duplex_open(4);
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_send(d, 0, 7));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_send(d, 0, 8));
    teko_duplex_close(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DUPLEX_CLOSED, teko_duplex_state(d));
    // poll is non-consuming: still OK while buffered data remains, even after close.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_poll(d, 1));
    // Buffered data is still drained after close (graceful end-of-stream)...
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_recv(d, 1, &v)); TEST_ASSERT_EQUAL_INT32(7, v);
    TEST_ASSERT_EQUAL_INT(TEKO_DX_OK, teko_duplex_recv(d, 1, &v)); TEST_ASSERT_EQUAL_INT32(8, v);
    // ...then poll/recv report CLOSED (structured end-of-stream — no hang) instead of EMPTY.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_CLOSED, teko_duplex_poll(d, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_CLOSED, teko_duplex_recv(d, 1, &v));
    // A send on a closed channel is rejected with a structured error.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_CLOSED, teko_duplex_send(d, 0, 99));
    teko_duplex_free(d);
}

void test_teko_duplex_drop_signals_peer(void) {
    TekoDuplex* d = teko_duplex_open(4);
    int32_t v = 0;
    teko_duplex_drop(d); // simulate a producer panic/drop
    TEST_ASSERT_EQUAL_INT(TEKO_DUPLEX_DROPPED, teko_duplex_state(d));
    // A peer blocked on recv is woken with a DROPPED status rather than hanging.
    TEST_ASSERT_EQUAL_INT(TEKO_DX_DROPPED, teko_duplex_recv(d, 0, &v));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_DROPPED, teko_duplex_send(d, 1, 5));
    // Drop is terminal: a later close does not downgrade it.
    teko_duplex_close(d);
    TEST_ASSERT_EQUAL_INT(TEKO_DUPLEX_DROPPED, teko_duplex_state(d));
    teko_duplex_free(d);
}

void test_teko_duplex_badarg(void) {
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(TEKO_DX_BADARG, teko_duplex_send(NULL, 0, 1));
    TEST_ASSERT_EQUAL_INT(TEKO_DX_BADARG, teko_duplex_recv(NULL, 0, &v));
    TekoDuplex* d = teko_duplex_open(4);
    TEST_ASSERT_EQUAL_INT(TEKO_DX_BADARG, teko_duplex_send(d, 2, 1));   // bad endpoint
    TEST_ASSERT_EQUAL_INT(TEKO_DX_BADARG, teko_duplex_recv(d, -1, &v)); // bad endpoint
    teko_duplex_free(d);
}
