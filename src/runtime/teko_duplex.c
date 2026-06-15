#include "teko_duplex.h"
#include <stdlib.h>

// Phase 14 (14.B) — duplex channel runtime. See teko_duplex.h for the model. Two fixed-cap
// circular rings (one per direction) + a state machine. No scheduler/threads dependency and
// no blocking: callers drive cooperation via the returned status. Pure C (malloc + indices)
// so it is the one source of truth for both the native runner and the wasm32 reactor.

typedef struct {
    int32_t  buf[TEKO_DUPLEX_MAX_CAP];
    uint32_t cap;  // usable capacity (<= TEKO_DUPLEX_MAX_CAP)
    uint32_t size; // number of buffered items
    uint32_t head; // read index
    uint32_t tail; // write index
} TekoDuplexRing;

struct TekoDuplex {
    TekoDuplexRing ring[2]; // ring[0]: 0->1, ring[1]: 1->0
    TekoDuplexState state;
};

static void ring_init(TekoDuplexRing* r, uint32_t cap) {
    r->cap = (cap == 0) ? 1u : (cap > TEKO_DUPLEX_MAX_CAP ? TEKO_DUPLEX_MAX_CAP : cap);
    r->size = 0;
    r->head = 0;
    r->tail = 0;
}

TekoDuplex* teko_duplex_open(uint32_t capacity) {
    TekoDuplex* d = (TekoDuplex*)malloc(sizeof(TekoDuplex));
    if (!d) return NULL;
    ring_init(&d->ring[0], capacity);
    ring_init(&d->ring[1], capacity);
    d->state = TEKO_DUPLEX_OPEN;
    return d;
}

void teko_duplex_free(TekoDuplex* d) {
    free(d);
}

// Map the channel state to the structured status returned when no buffered data is available.
static TekoDuplexStatus status_for_empty(TekoDuplexState s) {
    switch (s) {
        case TEKO_DUPLEX_CLOSED:  return TEKO_DX_CLOSED;
        case TEKO_DUPLEX_DROPPED: return TEKO_DX_DROPPED;
        default:                  return TEKO_DX_EMPTY;
    }
}

TekoDuplexStatus teko_duplex_send(TekoDuplex* d, int endpoint, int32_t value) {
    if (!d || (endpoint != 0 && endpoint != 1)) return TEKO_DX_BADARG;
    // A non-OPEN channel rejects sends with a structured error (no silent loss, no hang).
    if (d->state == TEKO_DUPLEX_CLOSED)  return TEKO_DX_CLOSED;
    if (d->state == TEKO_DUPLEX_DROPPED) return TEKO_DX_DROPPED;
    TekoDuplexRing* r = &d->ring[endpoint]; // send(ep) writes the ring the peer reads
    if (r->size >= r->cap) return TEKO_DX_FULL;
    r->buf[r->tail] = value;
    r->tail = (r->tail + 1) % r->cap;
    r->size++;
    return TEKO_DX_OK;
}

TekoDuplexStatus teko_duplex_recv(TekoDuplex* d, int endpoint, int32_t* out_value) {
    if (!d || !out_value || (endpoint != 0 && endpoint != 1)) return TEKO_DX_BADARG;
    TekoDuplexRing* r = &d->ring[endpoint ? 0 : 1]; // recv(ep) reads the opposite direction
    if (r->size > 0) {
        // Always drain buffered data first — even after close — for a graceful end-of-stream.
        *out_value = r->buf[r->head];
        r->head = (r->head + 1) % r->cap;
        r->size--;
        return TEKO_DX_OK;
    }
    return status_for_empty(d->state);
}

TekoDuplexStatus teko_duplex_poll(const TekoDuplex* d, int endpoint) {
    if (!d || (endpoint != 0 && endpoint != 1)) return TEKO_DX_BADARG;
    const TekoDuplexRing* r = &d->ring[endpoint ? 0 : 1]; // same ring recv(ep) would read
    if (r->size > 0) return TEKO_DX_OK;
    return status_for_empty(d->state);
}

void teko_duplex_close(TekoDuplex* d) {
    // A drop is terminal and outranks a later graceful close.
    if (d && d->state == TEKO_DUPLEX_OPEN) d->state = TEKO_DUPLEX_CLOSED;
}

void teko_duplex_drop(TekoDuplex* d) {
    if (d) d->state = TEKO_DUPLEX_DROPPED;
}

TekoDuplexState teko_duplex_state(const TekoDuplex* d) {
    return d ? d->state : TEKO_DUPLEX_DROPPED;
}
