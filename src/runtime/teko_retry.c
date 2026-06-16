#include "teko_retry.h"
#include <stdlib.h>

// Phase 14 (14.F) — resilience policy runtime. See teko_retry.h. Pure C (no atomics/threads),
// deterministic (time is passed in), the single source of truth for retry/circuit decisions.

struct TekoRetry {
    int             attempts;
    uint64_t        timeout;
    TekoBackoffMode mode;
    uint64_t        base;
    uint64_t        start; // Phase 14 (real-time): start instant for the real-clock timeout
};

// floor(log2(n)) for n >= 1.
static uint32_t ilog2_u64(uint64_t n) {
    uint32_t r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

TekoRetry* teko_retry_new(int attempts, uint64_t timeout, TekoBackoffMode mode, uint64_t base) {
    TekoRetry* r = (TekoRetry*)malloc(sizeof(TekoRetry));
    if (!r) return NULL;
    r->attempts = attempts;
    r->timeout = timeout;
    r->mode = (mode == TEKO_BACKOFF_LOGARITHMIC) ? TEKO_BACKOFF_LOGARITHMIC : TEKO_BACKOFF_EXPONENTIAL;
    r->base = base;
    r->start = 0;
    return r;
}

void teko_retry_free(TekoRetry* r) { free(r); }

// Phase 14 (real-time clock): record the start instant, and decide via real elapsed (now-start).
void teko_retry_mark_start(TekoRetry* r, uint64_t start) { if (r) r->start = start; }
int teko_retry_should_continue_rt(const TekoRetry* r, int attempt, uint64_t now) {
    if (!r) return 0;
    uint64_t elapsed = (now >= r->start) ? (now - r->start) : 0;
    return teko_retry_should_continue(r, attempt, elapsed);
}

uint64_t teko_retry_next_delay(const TekoRetry* r, int attempt) {
    if (!r) return 0;
    if (attempt < 0) attempt = 0;
    if (r->mode == TEKO_BACKOFF_LOGARITHMIC) {
        // base * (1 + floor(log2(attempt+1))): 0->1x, 1->2x, 2..3->3x, 4..7->4x, ... (slow growth)
        return r->base * (uint64_t)(1u + ilog2_u64((uint64_t)attempt + 1u));
    }
    // exponential: base * 2^attempt (cap the shift so it never overflows the multiply).
    int shift = attempt > 40 ? 40 : attempt;
    return r->base * ((uint64_t)1u << shift);
}

int teko_retry_should_continue(const TekoRetry* r, int attempt, uint64_t elapsed) {
    if (!r) return 0;
    if (r->attempts > 0 && attempt >= r->attempts) return 0; // attempt count exhausted
    if (r->timeout > 0) {
        // Incremental-relative-time rule: if starting this attempt's wait would overrun the
        // global budget, branch straight to fallback instead of beginning a doomed attempt.
        uint64_t next = teko_retry_next_delay(r, attempt);
        if (elapsed >= r->timeout) return 0;
        if (next > r->timeout - elapsed) return 0;
    }
    return 1;
}

struct TekoCircuit {
    int              threshold;
    uint64_t         cooldown;
    TekoCircuitState state;
    int              fail_count;  // consecutive failures while CLOSED
    uint64_t         opened_at;   // when the breaker last tripped OPEN
};

TekoCircuit* teko_circuit_new(int threshold, uint64_t cooldown) {
    TekoCircuit* c = (TekoCircuit*)malloc(sizeof(TekoCircuit));
    if (!c) return NULL;
    c->threshold = threshold > 0 ? threshold : 1;
    c->cooldown = cooldown;
    c->state = TEKO_CIRCUIT_CLOSED;
    c->fail_count = 0;
    c->opened_at = 0;
    return c;
}

void teko_circuit_free(TekoCircuit* c) { free(c); }

int teko_circuit_allow(TekoCircuit* c, uint64_t now) {
    if (!c) return 0;
    if (c->state == TEKO_CIRCUIT_OPEN) {
        if (now - c->opened_at >= c->cooldown) {
            c->state = TEKO_CIRCUIT_HALF_OPEN; // cooldown elapsed — allow one trial
            return 1;
        }
        return 0; // still cooling down: fail fast
    }
    return 1; // CLOSED or HALF_OPEN: allow
}

void teko_circuit_record(TekoCircuit* c, int ok, uint64_t now) {
    if (!c) return;
    if (ok) {
        // Any success closes the breaker and clears the failure streak.
        c->state = TEKO_CIRCUIT_CLOSED;
        c->fail_count = 0;
        return;
    }
    if (c->state == TEKO_CIRCUIT_HALF_OPEN) {
        c->state = TEKO_CIRCUIT_OPEN; // trial failed — re-open
        c->opened_at = now;
        return;
    }
    if (++c->fail_count >= c->threshold) {
        c->state = TEKO_CIRCUIT_OPEN;
        c->opened_at = now;
    }
}

TekoCircuitState teko_circuit_state(const TekoCircuit* c) {
    return c ? c->state : TEKO_CIRCUIT_OPEN;
}
