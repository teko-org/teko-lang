#include "teko_delayed.h"
#include <stdlib.h>

// Phase 14 (14.C) — delayed (timed) channel runtime. See teko_delayed.h for the model. A bounded
// set of pending messages, each with an absolute delivery DEADLINE in ns; recv releases the
// earliest one whose deadline real time (now_ns, passed in) has reached. Pure C (fixed array), no
// scheduler/threads/clock dependency, so it is the one source of truth for native + the wasm
// reactor. The real monotonic clock is supplied by the caller (teko_rt_now_ns / env.teko_now_ns).

typedef struct {
    int32_t  value;
    uint64_t deadline_ns; // absolute time (ns) at which this message becomes receivable
    uint64_t seq;         // send order, to break deadline ties (stable FIFO)
} TekoDelayedMsg;

struct TekoDelayed {
    TekoDelayedMsg msg[TEKO_DELAYED_MAX_CAP];
    uint32_t count;
    uint32_t cap;
    uint64_t seq_next;
    int      closed;
};

TekoDelayed* teko_delayed_open(uint32_t capacity) {
    TekoDelayed* d = (TekoDelayed*)malloc(sizeof(TekoDelayed));
    if (!d) return NULL;
    d->count = 0;
    d->cap = (capacity == 0) ? 1u : (capacity > TEKO_DELAYED_MAX_CAP ? TEKO_DELAYED_MAX_CAP : capacity);
    d->seq_next = 0;
    d->closed = 0;
    return d;
}

void teko_delayed_free(TekoDelayed* d) { free(d); }

TekoDelayedStatus teko_delayed_send(TekoDelayed* d, int32_t value, uint64_t delay_ns, uint64_t now_ns) {
    if (!d) return TEKO_DLY_BADARG;
    if (d->closed) return TEKO_DLY_CLOSED;
    if (d->count >= d->cap) return TEKO_DLY_FULL;
    TekoDelayedMsg* m = &d->msg[d->count++];
    m->value = value;
    m->deadline_ns = now_ns + delay_ns;
    m->seq = d->seq_next++;
    return TEKO_DLY_OK;
}

// Index of the earliest message due at now_ns (deadline_ns <= now_ns), ties broken by send order.
// Returns -1 if none is due.
static int earliest_due_index(const TekoDelayed* d, uint64_t now_ns) {
    int best = -1;
    for (uint32_t i = 0; i < d->count; i++) {
        if (d->msg[i].deadline_ns > now_ns) continue;
        if (best < 0 ||
            d->msg[i].deadline_ns < d->msg[best].deadline_ns ||
            (d->msg[i].deadline_ns == d->msg[best].deadline_ns && d->msg[i].seq < d->msg[best].seq)) {
            best = (int)i;
        }
    }
    return best;
}

TekoDelayedStatus teko_delayed_recv(TekoDelayed* d, int32_t* out_value, uint64_t now_ns) {
    if (!d || !out_value) return TEKO_DLY_BADARG;
    int idx = earliest_due_index(d, now_ns);
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

TekoDelayedStatus teko_delayed_poll(const TekoDelayed* d, uint64_t now_ns) {
    if (!d) return TEKO_DLY_BADARG;
    if (earliest_due_index(d, now_ns) >= 0) return TEKO_DLY_OK;
    if (d->count > 0) return TEKO_DLY_NOT_READY;
    return d->closed ? TEKO_DLY_CLOSED : TEKO_DLY_EMPTY;
}

void teko_delayed_close(TekoDelayed* d) { if (d) d->closed = 1; }
