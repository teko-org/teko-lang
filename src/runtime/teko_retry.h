#ifndef TEKO_RETRY_H
#define TEKO_RETRY_H

#include <stdint.h>

// Phase 14 (14.F) — resilience policy: `retry` (with exponential/logarithmic backoff, bounded by
// `attempts` and/or a global `timeout`, falling through to `fallback`) and a `circuit` breaker.
// This portable C runtime is the SINGLE SOURCE OF TRUTH for the policy decisions (backoff
// schedule, the attempts+timeout→fallback rule, breaker state transitions) — the deterministic,
// KAT-tested core. The `retry { } fallback { }` control-flow lowering that DRIVES this policy
// from real source is the follow-up frontend increment (see docs/HANDOFF_PHASE14.md). Like the
// channel/delayed runtimes it is clock-source-agnostic: time (`elapsed`, `now`) is passed in, so
// the policy is deterministic and testable; a real scheduler supplies a monotonic ms clock.

typedef enum {
    TEKO_BACKOFF_EXPONENTIAL = 0, // delay = base * 2^attempt
    TEKO_BACKOFF_LOGARITHMIC = 1  // delay = base * (1 + floor(log2(attempt+1)))  (grows slowly)
} TekoBackoffMode;

typedef enum {
    TEKO_CIRCUIT_CLOSED    = 0, // calls flow through
    TEKO_CIRCUIT_OPEN      = 1, // calls are short-circuited (failing fast) until the cooldown
    TEKO_CIRCUIT_HALF_OPEN = 2  // a single trial call is allowed to probe recovery
} TekoCircuitState;

typedef struct TekoRetry   TekoRetry;
typedef struct TekoCircuit TekoCircuit;

// --- retry policy ---------------------------------------------------------------
// attempts: max attempts (0 = unlimited by count). timeout: global budget in the same time unit
// as `elapsed` (0 = no time limit). mode: backoff curve. base: base delay unit.
TekoRetry* teko_retry_new(int attempts, uint64_t timeout, TekoBackoffMode mode, uint64_t base);
void       teko_retry_free(TekoRetry* r);

// The backoff delay to wait BEFORE the given 0-based attempt index (attempt 0 is the first
// retry after the initial try; callers may also query attempt 0 for the first wait).
uint64_t teko_retry_next_delay(const TekoRetry* r, int attempt);

// Should another attempt be made? 0 → give up and branch to `fallback`. Stops when the attempt
// count is exhausted, OR (when a timeout is set) when the projected next wait would overrun the
// budget: elapsed + next_delay(attempt) > timeout — the incremental-relative-time rule from
// docs/plan.md (branch straight to fallback rather than start a doomed attempt).
int teko_retry_should_continue(const TekoRetry* r, int attempt, uint64_t elapsed);

// Phase 14 (real-time clock): the surface drives the timeout off the REAL monotonic clock.
// teko_retry_mark_start records the policy's start instant (same unit as `timeout` — the wrappers
// pass ms derived from teko_rt_now_ns); teko_retry_should_continue_rt computes elapsed = now-start
// and applies the same rule. The explicit-elapsed teko_retry_should_continue above stays for the
// deterministic KATs.
void teko_retry_mark_start(TekoRetry* r, uint64_t start);
int  teko_retry_should_continue_rt(const TekoRetry* r, int attempt, uint64_t now);

// --- circuit breaker ------------------------------------------------------------
// threshold: consecutive failures that trip CLOSED→OPEN. cooldown: time (same unit as `now`)
// the breaker stays OPEN before allowing a HALF_OPEN trial.
TekoCircuit* teko_circuit_new(int threshold, uint64_t cooldown);
void         teko_circuit_free(TekoCircuit* c);

// May a call proceed now? Transitions OPEN→HALF_OPEN once the cooldown has elapsed. 1=allow.
int teko_circuit_allow(TekoCircuit* c, uint64_t now);

// Record a call outcome (ok != 0 = success). Drives CLOSED/OPEN/HALF_OPEN transitions.
void teko_circuit_record(TekoCircuit* c, int ok, uint64_t now);

TekoCircuitState teko_circuit_state(const TekoCircuit* c);

#endif // TEKO_RETRY_H
