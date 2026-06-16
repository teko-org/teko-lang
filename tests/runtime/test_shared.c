#include "unity.h"
#include "../../src/runtime/teko_shared.h"

// Phase 14 (14.E) — shared-memory runtime (source of truth). Validates the atomic cell RMW and
// the coarse lock enter/leave (balance + use inside a critical section). Single-threaded (the
// executable proofs are single-threaded); the atomics are real RMW so the contract holds when
// threads are added.

void test_teko_atomic_cell_rmw(void) {
    TekoAtomicCell* c = teko_atomic_cell(0);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_atomic_load(c));
    TEST_ASSERT_EQUAL_INT32(5, (int)teko_atomic_add(c, 5));   // fetch-add returns NEW value
    TEST_ASSERT_EQUAL_INT32(8, (int)teko_atomic_add(c, 3));
    TEST_ASSERT_EQUAL_INT32(8, (int)teko_atomic_load(c));
    teko_atomic_store(c, 42);
    TEST_ASSERT_EQUAL_INT32(42, (int)teko_atomic_load(c));
    TEST_ASSERT_EQUAL_INT32(40, (int)teko_atomic_add(c, -2));
    teko_atomic_free(c);
}

void test_teko_shared_critical_section(void) {
    // enter/leave are balanced and a non-recursive lock is released between sections.
    TekoAtomicCell* c = teko_atomic_cell(0);
    teko_shared_enter();
    teko_atomic_add(c, 1);
    teko_atomic_add(c, 1);
    teko_shared_leave();
    teko_shared_enter();          // re-acquirable after release (would deadlock if leave failed)
    teko_atomic_add(c, 1);
    teko_shared_leave();
    TEST_ASSERT_EQUAL_INT32(3, (int)teko_atomic_load(c));
    teko_atomic_free(c);
}

void test_teko_atomic_badarg(void) {
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_atomic_add(NULL, 1));
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_atomic_load(NULL));
    teko_atomic_store(NULL, 5); // no-op, must not crash
}
