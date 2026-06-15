#ifndef TEKO_DUPLEX_H
#define TEKO_DUPLEX_H

#include <stdint.h>

// Phase 14 (14.B) — duplex channel: a symmetric, full-duplex bidirectional channel with two
// *isolated* rings (one per direction) plus a close/drop state machine. This portable C
// runtime is the SINGLE SOURCE OF TRUTH for the language `duplex chan` surface — KAT-style
// unit-tested in the Unity suite, lowered into native binaries via teko_rt_duplex_* wrappers,
// and compiled into the wasm32 runtime reactor for the WASM target (the same pattern as the
// Phase 13 crypto runtime). It is deliberately scheduler-agnostic: send/recv never block —
// they return a structured status, so a cooperative caller yields/retries on EMPTY and gets a
// definite CLOSED/DROPPED instead of hanging when the peer goes away (the "unblock and wake
// threads stuck during a panic, return structured errors" requirement from docs/plan.md).
//
// Endpoints are 0 and 1. A duplex carries two rings:
//   ring[0]: messages 0 -> 1   (send(ep=0) writes it; recv(ep=1) reads it)
//   ring[1]: messages 1 -> 0   (send(ep=1) writes it; recv(ep=0) reads it)
// So send(d, ep, v) enqueues on ring[ep]; recv(d, ep, &v) dequeues from ring[1-ep].

typedef enum {
    TEKO_DUPLEX_OPEN    = 0,
    TEKO_DUPLEX_CLOSED  = 1, // legitimate .close()
    TEKO_DUPLEX_DROPPED = 2  // failure/panic drop
} TekoDuplexState;

typedef enum {
    TEKO_DX_OK      = 0, // value sent / received
    TEKO_DX_EMPTY   = 1, // OPEN + no data buffered: a cooperative caller should yield/retry
    TEKO_DX_FULL    = 2, // OPEN + this direction's ring is full
    TEKO_DX_CLOSED  = 3, // recv: drained + peer closed (structured end-of-stream); send: closed
    TEKO_DX_DROPPED = 4, // peer dropped/panicked
    TEKO_DX_BADARG  = 5  // NULL channel / bad endpoint
} TekoDuplexStatus;

typedef struct TekoDuplex TekoDuplex;

// Allocate an OPEN duplex with `capacity` slots PER DIRECTION (clamped to a sane max).
// Returns NULL on allocation failure. Free with teko_duplex_free.
TekoDuplex* teko_duplex_open(uint32_t capacity);
void        teko_duplex_free(TekoDuplex* d);

// Send `value` from `endpoint` (0/1) to the peer. OPEN+space → OK; OPEN+full → FULL;
// CLOSED → CLOSED; DROPPED → DROPPED; bad args → BADARG.
TekoDuplexStatus teko_duplex_send(TekoDuplex* d, int endpoint, int32_t value);

// Receive into *out_value at `endpoint` (0/1). Buffered data is always drained first
// (OK), even after close. When empty: OPEN → EMPTY, CLOSED → CLOSED, DROPPED → DROPPED.
TekoDuplexStatus teko_duplex_recv(TekoDuplex* d, int endpoint, int32_t* out_value);

// Non-consuming status probe for `endpoint`'s recv direction: OK if data is buffered, else
// the empty-status for the current state (EMPTY/CLOSED/DROPPED). Lets a caller distinguish a
// transient EMPTY (yield/retry) from a terminal CLOSED/DROPPED without an in-band sentinel.
TekoDuplexStatus teko_duplex_poll(const TekoDuplex* d, int endpoint);

void            teko_duplex_close(TekoDuplex* d); // legitimate close (OPEN → CLOSED)
void            teko_duplex_drop(TekoDuplex* d);  // panic/failure (→ DROPPED)
TekoDuplexState teko_duplex_state(const TekoDuplex* d);

#define TEKO_DUPLEX_MAX_CAP 64u // per-direction ring cap (bounded, allocation-free growth)

#endif // TEKO_DUPLEX_H
