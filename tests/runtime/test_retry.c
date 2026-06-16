#include "unity.h"
#include "../../src/runtime/teko_retry.h"

// Phase 14 (14.F) — resilience policy runtime (source of truth). Validates the backoff schedules,
// the attempts + attempts+timeout→fallback rules, and the circuit-breaker state machine.

void test_teko_retry_exponential_backoff(void) {
    TekoRetry* r = teko_retry_new(0, 0, TEKO_BACKOFF_EXPONENTIAL, 10);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_UINT64(10,  teko_retry_next_delay(r, 0));
    TEST_ASSERT_EQUAL_UINT64(20,  teko_retry_next_delay(r, 1));
    TEST_ASSERT_EQUAL_UINT64(40,  teko_retry_next_delay(r, 2));
    TEST_ASSERT_EQUAL_UINT64(80,  teko_retry_next_delay(r, 3));
    teko_retry_free(r);
}

void test_teko_retry_logarithmic_backoff(void) {
    TekoRetry* r = teko_retry_new(0, 0, TEKO_BACKOFF_LOGARITHMIC, 10);
    // base * (1 + floor(log2(attempt+1))): grows much slower than exponential.
    TEST_ASSERT_EQUAL_UINT64(10, teko_retry_next_delay(r, 0)); // 1+floor(log2(1))=1 -> 1x
    TEST_ASSERT_EQUAL_UINT64(20, teko_retry_next_delay(r, 1)); // 1+floor(log2(2))=2 -> 2x
    TEST_ASSERT_EQUAL_UINT64(20, teko_retry_next_delay(r, 2)); // 1+floor(log2(3))=2 -> 2x
    TEST_ASSERT_EQUAL_UINT64(30, teko_retry_next_delay(r, 3)); // 1+floor(log2(4))=3 -> 3x
    TEST_ASSERT_EQUAL_UINT64(40, teko_retry_next_delay(r, 7)); // 1+floor(log2(8))=4 -> 4x
    teko_retry_free(r);
}

void test_teko_retry_attempts_limit(void) {
    TekoRetry* r = teko_retry_new(3, 0, TEKO_BACKOFF_EXPONENTIAL, 1);
    TEST_ASSERT_EQUAL_INT(1, teko_retry_should_continue(r, 0, 0));
    TEST_ASSERT_EQUAL_INT(1, teko_retry_should_continue(r, 2, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_retry_should_continue(r, 3, 0)); // exhausted → fallback
    teko_retry_free(r);
}

void test_teko_retry_timeout_branches_to_fallback(void) {
    // attempts large, but a 100-unit budget with exponential base 10: delays 10,20,40,80,160...
    // elapsed grows; once elapsed + next_delay > 100 we must branch to fallback BEFORE attempting.
    TekoRetry* r = teko_retry_new(100, 100, TEKO_BACKOFF_EXPONENTIAL, 10);
    TEST_ASSERT_EQUAL_INT(1, teko_retry_should_continue(r, 0, 0));   // 0 + 10 <= 100
    TEST_ASSERT_EQUAL_INT(1, teko_retry_should_continue(r, 2, 30));  // 30 + 40 = 70 <= 100
    TEST_ASSERT_EQUAL_INT(0, teko_retry_should_continue(r, 3, 70));  // 70 + 80 = 150 > 100 → fallback
    TEST_ASSERT_EQUAL_INT(0, teko_retry_should_continue(r, 0, 100)); // budget already spent
    teko_retry_free(r);
}

void test_teko_circuit_breaker_transitions(void) {
    TekoCircuit* c = teko_circuit_new(2, 50); // open after 2 fails, cool down 50
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_CLOSED, teko_circuit_state(c));
    TEST_ASSERT_EQUAL_INT(1, teko_circuit_allow(c, 0));
    teko_circuit_record(c, 0, 0);                 // fail 1
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_CLOSED, teko_circuit_state(c));
    teko_circuit_record(c, 0, 10);                // fail 2 → OPEN
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_OPEN, teko_circuit_state(c));
    TEST_ASSERT_EQUAL_INT(0, teko_circuit_allow(c, 20));  // still cooling (20-10 < 50)
    TEST_ASSERT_EQUAL_INT(1, teko_circuit_allow(c, 70));  // 70-10 >= 50 → HALF_OPEN, allow a trial
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_HALF_OPEN, teko_circuit_state(c));
    teko_circuit_record(c, 0, 70);                // trial fails → OPEN again
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_OPEN, teko_circuit_state(c));
    TEST_ASSERT_EQUAL_INT(1, teko_circuit_allow(c, 130)); // cooled → HALF_OPEN
    teko_circuit_record(c, 1, 130);               // trial succeeds → CLOSED
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_CLOSED, teko_circuit_state(c));
    teko_circuit_free(c);
}

void test_teko_retry_badarg(void) {
    TEST_ASSERT_EQUAL_INT(0, teko_retry_should_continue(NULL, 0, 0));
    TEST_ASSERT_EQUAL_UINT64(0, teko_retry_next_delay(NULL, 0));
    TEST_ASSERT_EQUAL_INT(0, teko_circuit_allow(NULL, 0));
    TEST_ASSERT_EQUAL_INT(TEKO_CIRCUIT_OPEN, teko_circuit_state(NULL)); // fail safe
}
