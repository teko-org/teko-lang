#include "unity.h"
#include <stdint.h>
#include "../../src/runtime/teko_object.h"

// Phase 15 (15.A) — object model runtime (source of truth). Validates the handle-based
// field-cell store: allocation + nfields, set/get by compile-time index, zero-initialization,
// register-width cells (a field can hold another object's handle), and the defensive OOB/NULL
// boundary the compiler never actually hits but the runtime must survive.

void test_teko_object_new_zeroed(void) {
    TekoObject* o = teko_object_new(3);
    TEST_ASSERT_NOT_NULL(o);
    TEST_ASSERT_EQUAL_INT32(3, teko_object_nfields(o));
    // calloc zero-init: every field reads 0 before any set.
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, 0));
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, 1));
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, 2));
    teko_object_free(o);
}

void test_teko_object_set_get(void) {
    TekoObject* o = teko_object_new(4);
    teko_object_set(o, 0, 42);
    teko_object_set(o, 1, -7);
    teko_object_set(o, 3, 1000000);
    TEST_ASSERT_EQUAL_INT32(42, (int)teko_object_get(o, 0));
    TEST_ASSERT_EQUAL_INT32(-7, (int)teko_object_get(o, 1));
    TEST_ASSERT_EQUAL_INT32(0,  (int)teko_object_get(o, 2)); // untouched stays 0
    TEST_ASSERT_EQUAL_INT32(1000000, (int)teko_object_get(o, 3));
    // overwrite
    teko_object_set(o, 0, 99);
    TEST_ASSERT_EQUAL_INT32(99, (int)teko_object_get(o, 0));
    teko_object_free(o);
}

void test_teko_object_holds_handle(void) {
    // A field cell is register-width, so it can hold another object's handle (a pointer). This
    // is how the compiler lowers a field whose type is itself a class (composition).
    TekoObject* inner = teko_object_new(1);
    teko_object_set(inner, 0, 314);
    TekoObject* outer = teko_object_new(2);
    teko_object_set(outer, 0, 5);
    teko_object_set(outer, 1, (intptr_t)inner); // store the inner handle in a field (no truncation)
    TekoObject* got = (TekoObject*)teko_object_get(outer, 1);
    TEST_ASSERT_EQUAL_PTR(inner, got);
    TEST_ASSERT_EQUAL_INT32(314, (int)teko_object_get(got, 0));
    teko_object_free(inner);
    teko_object_free(outer);
}

void test_teko_object_bounds_and_null(void) {
    TekoObject* o = teko_object_new(2);
    // OOB set is a no-op (no crash, no neighbor corruption); OOB/NULL get is 0.
    teko_object_set(o, 5, 123);
    teko_object_set(o, -1, 123);
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, 5));
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, -1));
    teko_object_set(NULL, 0, 1);                       // no-op, must not crash
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(NULL, 0));
    TEST_ASSERT_EQUAL_INT32(0, teko_object_nfields(NULL));
    teko_object_free(o);
    teko_object_free(NULL);                            // free(NULL) is safe
}

void test_teko_object_zero_fields(void) {
    // A field-less class (pure behavior / marker) allocates cleanly.
    TekoObject* o = teko_object_new(0);
    TEST_ASSERT_NOT_NULL(o);
    TEST_ASSERT_EQUAL_INT32(0, teko_object_nfields(o));
    TEST_ASSERT_EQUAL_INT32(0, (int)teko_object_get(o, 0)); // OOB on an empty object → 0
    teko_object_free(o);
}
