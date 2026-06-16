#include "teko_vtable.h"

// Phase 15 (15.B) — static vtable runtime. See teko_vtable.h. Pure C (a fixed 2-D array + index),
// no allocation, no scheduler/threads dependency, so it is the one source of truth for both the
// native runner (teko_rt_vtable_* wrappers) and the wasm32 reactor. Single-threaded population +
// reads (the compiler emits the population at $main start, before any dynamic dispatch).

static long g_vtable[TEKO_VT_MAX_TYPES * TEKO_VT_MAX_METHODS];
static int  g_vtable_ready = 0; // 0 until the first reset zero-fills with the -1 sentinel

void teko_vtable_reset(void) {
    for (int i = 0; i < TEKO_VT_MAX_TYPES * TEKO_VT_MAX_METHODS; i++) g_vtable[i] = -1;
    g_vtable_ready = 1;
}

void teko_vtable_set(long type_id, long method_id, long slot) {
    if (!g_vtable_ready) teko_vtable_reset(); // self-initialize if the compiler skipped an explicit reset
    if (type_id < 0 || type_id >= TEKO_VT_MAX_TYPES) return;
    if (method_id < 0 || method_id >= TEKO_VT_MAX_METHODS) return;
    g_vtable[type_id * TEKO_VT_MAX_METHODS + method_id] = slot;
}

long teko_vtable_get(long type_id, long method_id) {
    if (!g_vtable_ready) return -1;
    if (type_id < 0 || type_id >= TEKO_VT_MAX_TYPES) return -1;
    if (method_id < 0 || method_id >= TEKO_VT_MAX_METHODS) return -1;
    return g_vtable[type_id * TEKO_VT_MAX_METHODS + method_id];
}
