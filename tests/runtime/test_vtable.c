#include "unity.h"
#include "../../src/runtime/teko_vtable.h"

// Phase 15 (15.B) — static vtable runtime (source of truth). Validates compile-time population +
// O(1) dispatch lookup, the -1 "unset" sentinel, override (last write wins), and the defensive
// out-of-range boundary the compiler never hits but the runtime must survive.

void test_teko_vtable_set_get(void) {
    teko_vtable_reset();
    // type 0 implements method 0 -> slot 7, method 1 -> slot 9
    teko_vtable_set(0, 0, 7);
    teko_vtable_set(0, 1, 9);
    // type 1 implements method 0 -> slot 12 (a different impl of the same trait method)
    teko_vtable_set(1, 0, 12);
    TEST_ASSERT_EQUAL_INT32(7,  (int)teko_vtable_get(0, 0));
    TEST_ASSERT_EQUAL_INT32(9,  (int)teko_vtable_get(0, 1));
    TEST_ASSERT_EQUAL_INT32(12, (int)teko_vtable_get(1, 0)); // same method_id, different type -> different slot
}

void test_teko_vtable_unset_is_minus_one(void) {
    teko_vtable_reset();
    teko_vtable_set(3, 2, 5);
    TEST_ASSERT_EQUAL_INT32(5,  (int)teko_vtable_get(3, 2));
    TEST_ASSERT_EQUAL_INT32(-1, (int)teko_vtable_get(3, 0)); // (3,0) never set
    TEST_ASSERT_EQUAL_INT32(-1, (int)teko_vtable_get(9, 9)); // wholly unset
    // slot 0 is a VALID routine slot, so the sentinel must be -1, not 0.
    teko_vtable_set(4, 0, 0);
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_vtable_get(4, 0));
}

void test_teko_vtable_override(void) {
    teko_vtable_reset();
    teko_vtable_set(2, 1, 10);
    teko_vtable_set(2, 1, 20); // an overriding class re-points the same (type,method) -> last wins
    TEST_ASSERT_EQUAL_INT32(20, (int)teko_vtable_get(2, 1));
}

void test_teko_vtable_bounds(void) {
    teko_vtable_reset();
    teko_vtable_set(-1, 0, 5);   // out-of-range: no-op
    teko_vtable_set(0, -1, 5);
    teko_vtable_set(TEKO_VT_MAX_TYPES, 0, 5);
    teko_vtable_set(0, TEKO_VT_MAX_METHODS, 5);
    TEST_ASSERT_EQUAL_INT32(-1, (int)teko_vtable_get(-1, 0));
    TEST_ASSERT_EQUAL_INT32(-1, (int)teko_vtable_get(0, TEKO_VT_MAX_METHODS));
    TEST_ASSERT_EQUAL_INT32(-1, (int)teko_vtable_get(0, 0)); // unaffected by the OOB writes
}
