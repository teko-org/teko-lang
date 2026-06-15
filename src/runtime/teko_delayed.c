#include "teko_delayed.h"
#include <stdlib.h>

// Phase 14 (14.C) — delayed (timed) channel runtime. See teko_delayed.h for the model. A
// bounded set of pending messages, each with a delivery time; recv releases the earliest one
// whose delivery time the logical clock has reached. Pure C (fixed array + a u64 clock), no
// scheduler/threads dependency, so it is the one source of truth for native + the wasm reactor.

typedef struct {
    int32_t  value;
    uint64_t deliver_at; // logical time at which this message becomes receivable
    uint64_t seq;        // send order, to break delivery-time ties (stable FIFO)
} TekoDelayedMsg;

struct TekoDelayed {
    TekoDelayedMsg msg[TEKO_DELAYED_MAX_CAP];
    uint32_t count;
    uint32_t cap;
    uint64_t now;     // logical clock
    uint64_t seq_next;
    int      closed;
};

TekoDelayed* teko_delayed_open(uint32_t capacity) {
    TekoDelayed* d = (TekoDelayed*)malloc(sizeof(TekoDelayed));
    if (!d) return NULL;
    d->count = 0;
    d->cap = (capacity == 0) ? 1u : (capacity > TEKO_DELAYED_MAX_CAP ? TEKO_DELAYED_MAX_CAP : capacity);
    d->now = 0;
    d->seq_next = 0;
    d->closed = 0;
    return d;
}

void teko_delayed_free(TekoDelayed* d) { free(d); }

TekoDelayedStatus teko_delayed_send(TekoDelayed* d, int32_t value, uint32_t delay) {
    if (!d) return TEKO_DLY_BADARG;
    if (d->closed) return TEKO_DLY_CLOSED;
    if (d->count >= d->cap) return TEKO_DLY_FULL;
    TekoDelayedMsg* m = &d->msg[d->count++];
    m->value = value;
    m->deliver_at = d->now + (uint64_t)delay;
    m->seq = d->seq_next++;
    return TEKO_DLY_OK;
}

uint64_t teko_delayed_advance(TekoDelayed* d, uint32_t dt) {
    if (!d) return 0;
    d->now += (uint64_t)dt;
    return d->now;
}

// Find the index of the earliest-due message (deliver_at <= now), ties broken by send order.
// Returns -1 if none is due.
static int earliest_due_index(const TekoDelayed* d) {
    int best = -1;
    for (uint32_t i = 0; i < d->count; i++) {
        if (d->msg[i].deliver_at > d->now) continue;
        if (best < 0 ||
            d->msg[i].deliver_at < d->msg[best].deliver_at ||
            (d->msg[i].deliver_at == d->msg[best].deliver_at && d->msg[i].seq < d->msg[best].seq)) {
            best = (int)i;
        }
    }
    return best;
}

TekoDelayedStatus teko_delayed_recv(TekoDelayed* d, int32_t* out_value) {
    if (!d || !out_value) return TEKO_DLY_BADARG;
    int idx = earliest_due_index(d);
    if (idx >= 0) {
        *out_value = d->msg[idx].value;
        // Remove by shifting down (keeps the small buffer compact + send order stable).
        for (uint32_t i = (uint32_t)idx + 1; i < d->count; i++) d->msg[i - 1] = d->msg[i];
        d->count--;
        return TEKO_DLY_OK;
    }
    if (d->count > 0) return TEKO_DLY_NOT_READY;           // buffered but not yet due
    return d->closed ? TEKO_DLY_CLOSED : TEKO_DLY_EMPTY;   // drained
}

TekoDelayedStatus teko_delayed_poll(const TekoDelayed* d) {
    if (!d) return TEKO_DLY_BADARG;
    if (earliest_due_index(d) >= 0) return TEKO_DLY_OK;
    if (d->count > 0) return TEKO_DLY_NOT_READY;
    return d->closed ? TEKO_DLY_CLOSED : TEKO_DLY_EMPTY;
}

uint64_t teko_delayed_now(const TekoDelayed* d) { return d ? d->now : 0; }

void teko_delayed_close(TekoDelayed* d) { if (d) d->closed = 1; }
