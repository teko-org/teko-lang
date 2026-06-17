#include "teko_array.h"
#include <stdlib.h>

// Phase 18 (18.E.1) — fixed-size contiguous array runtime. See teko_array.h. Pure C (calloc + an
// index into a register-width cell vector), no scheduler/threads dependency, so it is the one
// source of truth for both the native runner (teko_rt_array_* wrappers) and the wasm32 reactor.
// Mirrors teko_object.c; the ONLY difference is that get/set RETURN a success flag (0 = OOB) so the
// surface wrappers fail loudly on a bad index rather than silently no-op'ing.

struct TekoArray {
    int      n;       // O(1) length metadata (the element count)
    intptr_t cells[]; // flexible array: n zero-initialized POINTER-width cells (Windows-safe)
};

TekoArray* teko_array_new(int n) {
    if (n < 0) n = 0;
    if (n > TEKO_ARRAY_MAX_LEN) n = TEKO_ARRAY_MAX_LEN;
    // calloc zero-initializes the header + every cell (the Windows wild-free lesson: never leave
    // an allocation's fields unset).
    TekoArray* a = (TekoArray*)calloc(1, sizeof(TekoArray) + (size_t)n * sizeof(intptr_t));
    if (!a) return NULL;
    a->n = n;
    return a;
}

void teko_array_free(TekoArray* a) {
    free(a);
}

int teko_array_len(const TekoArray* a) {
    return a ? a->n : 0;
}

int teko_array_get(const TekoArray* a, int i, intptr_t* out) {
    if (!a || i < 0 || i >= a->n) return 0; // out-of-range -> caller fails loudly
    if (out) *out = a->cells[i];
    return 1;
}

int teko_array_set(TekoArray* a, int i, intptr_t v) {
    if (!a || i < 0 || i >= a->n) return 0; // out-of-range -> caller fails loudly
    a->cells[i] = v;
    return 1;
}
