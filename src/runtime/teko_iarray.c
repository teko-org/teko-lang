#include "teko_iarray.h"
#include <stdlib.h>

// Phase 18 (18.E.2) — TYPED `i32[]` PACKED numeric array runtime. See teko_iarray.h. Pure C (calloc +
// an index into a PACKED int32_t cell vector), no scheduler/threads dependency, so it is the one
// source of truth for both the native runner (teko_rt_iarray_* wrappers) and the wasm32 reactor.
// Mirrors teko_array.c EXACTLY — the ONLY difference is the cell type (int32_t packed cells vs
// intptr_t register-width cells): the packed 32-bit layout is the SIMD substrate.

struct TekoIArray {
    int     n;        // O(1) length metadata (the element count)
    int32_t cells[];  // flexible array: n zero-initialized PACKED 32-bit cells (contiguous, SIMD-able)
};

TekoIArray* teko_iarray_new(int n) {
    if (n < 0) n = 0;
    if (n > TEKO_IARRAY_MAX_LEN) n = TEKO_IARRAY_MAX_LEN;
    // calloc zero-initializes the header + every cell (the Windows wild-free lesson: never leave an
    // allocation's fields unset).
    TekoIArray* a = (TekoIArray*)calloc(1, sizeof(TekoIArray) + (size_t)n * sizeof(int32_t));
    if (!a) return NULL;
    a->n = n;
    return a;
}

void teko_iarray_free(TekoIArray* a) {
    free(a);
}

int teko_iarray_len(const TekoIArray* a) {
    return a ? a->n : 0;
}

int teko_iarray_get(const TekoIArray* a, int i, int32_t* out) {
    if (!a || i < 0 || i >= a->n) return 0; // out-of-range -> caller fails loudly
    if (out) *out = a->cells[i];
    return 1;
}

int teko_iarray_set(TekoIArray* a, int i, int32_t v) {
    if (!a || i < 0 || i >= a->n) return 0; // out-of-range -> caller fails loudly
    a->cells[i] = v;
    return 1;
}

// Phase 18 (18.E.4) — the SIMD substrate accessor: the pointer to the contiguous packed int32 cell
// buffer the backend's vector kernel walks. NULL for a NULL/empty array (the kernel sees length 0
// and reduces to 0). The cells are guaranteed contiguous (flexible array member) and 4-byte packed.
int32_t* teko_iarray_data(TekoIArray* a) {
    if (!a || a->n <= 0) return NULL;
    return a->cells;
}

// Phase 18 (18.E.4) — the SCALAR reference reduction: a plain `for` loop summing the run. This is
// the honest fallback for any target WITHOUT a vector unit (the 16 freestanding emitters, riscv
// without RVV) AND the self-check oracle the in-program `simd.sum` proof asserts the vectorized
// result against. NO intrinsics — portable across every ABI (Windows LLP64 int32 cells are fine).
// Accumulates in a 32-bit int (matches the vector kernels' i32x4 accumulate + i32 collapse).
int teko_iarray_sum(const TekoIArray* a) {
    int s = 0;
    if (a) {
        for (int i = 0; i < a->n; i++) s += a->cells[i];
    }
    return s;
}
