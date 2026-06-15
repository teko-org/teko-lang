#ifndef TEKO_DELAYED_H
#define TEKO_DELAYED_H

#include <stdint.h>

// Phase 14 (14.C) — delayed (timed) channel: each message is stamped with a delivery time
// (now + delay) and only becomes receivable once the channel's clock reaches it; messages are
// released in delivery-time order (the Timer Queue from docs/plan.md). This portable C runtime
// is the SINGLE SOURCE OF TRUTH for the `delayed.*` surface (native teko_rt_delayed_* + the
// wasm32 reactor), same pattern as the crypto/duplex runtimes.
//
// The clock is LOGICAL and advanced explicitly via teko_delayed_advance (a virtual "timer
// tick"): this keeps the runtime deterministic and unit-testable (no wall-clock flakiness),
// and is clock-source-agnostic — a real scheduler advances it from a monotonic ms source, a
// test advances it by fixed steps. recv is non-blocking: it returns NOT_READY (with nothing
// due yet) so a cooperative caller yields/advances and retries instead of hanging.

typedef enum {
    TEKO_DLY_OK        = 0, // a due message was delivered
    TEKO_DLY_NOT_READY = 1, // messages buffered but none due yet (advance the clock / retry)
    TEKO_DLY_EMPTY     = 2, // no messages buffered at all
    TEKO_DLY_FULL      = 3, // capacity reached
    TEKO_DLY_CLOSED    = 4, // closed + nothing due/buffered (structured end-of-stream)
    TEKO_DLY_BADARG    = 5
} TekoDelayedStatus;

typedef struct TekoDelayed TekoDelayed;

TekoDelayed* teko_delayed_open(uint32_t capacity);
void         teko_delayed_free(TekoDelayed* d);

// Schedule `value` for delivery at (current logical time + delay). FULL if at capacity,
// CLOSED if closed, BADARG on NULL.
TekoDelayedStatus teko_delayed_send(TekoDelayed* d, int32_t value, uint32_t delay);

// Advance the logical clock by dt (a timer tick); messages whose delivery time is now reached
// become receivable. Returns the new logical time.
uint64_t teko_delayed_advance(TekoDelayed* d, uint32_t dt);

// Deliver the earliest message whose delivery time has been reached (ties broken by send
// order). OK + *out set, or NOT_READY / EMPTY / CLOSED. BADARG on NULL.
TekoDelayedStatus teko_delayed_recv(TekoDelayed* d, int32_t* out_value);

// Non-consuming probe: OK if a message is due now, else NOT_READY / EMPTY / CLOSED.
TekoDelayedStatus teko_delayed_poll(const TekoDelayed* d);

uint64_t teko_delayed_now(const TekoDelayed* d); // current logical time
void     teko_delayed_close(TekoDelayed* d);

#define TEKO_DELAYED_MAX_CAP 64u // bounded buffer (allocation-free growth)

#endif // TEKO_DELAYED_H
