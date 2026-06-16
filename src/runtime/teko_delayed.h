#ifndef TEKO_DELAYED_H
#define TEKO_DELAYED_H

#include <stdint.h>

// Phase 14 (14.C) — delayed (timed) channel: each message is stamped with an absolute delivery
// DEADLINE in nanoseconds (now + delay) and only becomes receivable once real time reaches it;
// messages are released in deadline order (the Timer Queue from docs/plan.md). This portable C
// runtime is the SINGLE SOURCE OF TRUTH for the `delayed.*` surface (native teko_rt_delayed_* + the
// wasm32 reactor), same pattern as the crypto/duplex runtimes.
//
// The time base is the REAL MONOTONIC nanosecond clock (owner decision — replaced the former
// logical clock + explicit `advance`). The runtime itself stays clock-source-agnostic and
// KAT-deterministic: `now_ns` is PASSED IN to send/recv/poll, so the wrappers supply the real
// clock (teko_rt_now_ns / env.teko_now_ns) while the unit tests pass explicit timestamps. recv is
// non-blocking: it returns NOT_READY (nothing due yet) so a cooperative caller yields/retries.

typedef enum {
    TEKO_DLY_OK        = 0, // a due message was delivered
    TEKO_DLY_NOT_READY = 1, // messages buffered but none due yet at now_ns (yield/retry)
    TEKO_DLY_EMPTY     = 2, // no messages buffered at all
    TEKO_DLY_FULL      = 3, // capacity reached
    TEKO_DLY_CLOSED    = 4, // closed + nothing due/buffered (structured end-of-stream)
    TEKO_DLY_BADARG    = 5
} TekoDelayedStatus;

typedef struct TekoDelayed TekoDelayed;

TekoDelayed* teko_delayed_open(uint32_t capacity);
void         teko_delayed_free(TekoDelayed* d);

// Schedule `value` for delivery at the absolute deadline (now_ns + delay_ns). FULL if at capacity,
// CLOSED if closed, BADARG on NULL.
TekoDelayedStatus teko_delayed_send(TekoDelayed* d, int32_t value, uint64_t delay_ns, uint64_t now_ns);

// Deliver the earliest message whose deadline has been reached at now_ns (ties broken by send
// order). OK + *out set, or NOT_READY / EMPTY / CLOSED. BADARG on NULL.
TekoDelayedStatus teko_delayed_recv(TekoDelayed* d, int32_t* out_value, uint64_t now_ns);

// Non-consuming probe at now_ns: OK if a message is due, else NOT_READY / EMPTY / CLOSED.
TekoDelayedStatus teko_delayed_poll(const TekoDelayed* d, uint64_t now_ns);

void teko_delayed_close(TekoDelayed* d);

#define TEKO_DELAYED_MAX_CAP 64u // bounded buffer (allocation-free growth)

#endif // TEKO_DELAYED_H
