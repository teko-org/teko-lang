#ifndef TEKO_IARRAY_H
#define TEKO_IARRAY_H

#include <stdint.h> // int32_t — packed 32-bit element cells (the SIMD substrate)

// Phase 18 (18.E.2) — TYPED `i32[]` PACKED numeric array: a handle-backed CONTIGUOUS store of
// PACKED 32-bit signed integers (`int32_t` cells, NOT pointer-width). A SEPARATE collection from the
// 18.E.1 i64-cell `array`: the packed layout is the SIMD substrate (a later phase can vectorize a run
// of contiguous int32 cells). This portable C runtime is the SINGLE SOURCE OF TRUTH for typed i32
// arrays (native teko_rt_iarray_* wrappers + the wasm32 reactor), mirroring teko_array.c EXACTLY —
// the ONLY difference is the cell type (int32_t vs intptr_t).
//
// Bounds violations are FAIL-LOUD: the core get/set RETURN 0 on an out-of-range index (1 on success)
// so the register-width wrappers (teko_rt_iarray_get/set) abort via teko_rt_die (native exit 70 +
// stderr "iarray: index out of bounds"; wasm32 reactor __builtin_trap) — the SAME checked posture as
// the array/decimal/cast/parse surface.
//
// Cells are `int32_t` (packed 32-bit) — these are NUMERIC values, never handles, so the packed
// 32-bit width is correct on every ABI (including Windows LLP64). Length is O(1) metadata (the count
// stored in the header, no scan). Allocation is calloc — header + every cell zero-initialized.

typedef struct TekoIArray TekoIArray;

// Allocate a typed i32 array with `n` zero-initialized packed int32 cells (n clamped to [0, MAX]).
// NULL on failure.
TekoIArray* teko_iarray_new(int n);
void        teko_iarray_free(TekoIArray* a);

// O(1) length metadata (the count stored in the header). 0 if NULL.
int         teko_iarray_len(const TekoIArray* a);

// Bounds-RETURNING accessors: 1 on success, 0 on out-of-range/NULL (so the wrapper fails loudly).
// get writes the packed int32 cell to *out on success (leaves *out untouched on failure).
int         teko_iarray_get(const TekoIArray* a, int i, int32_t* out);
int         teko_iarray_set(TekoIArray* a, int i, int32_t v);

// Phase 18 (18.E.4) — SIMD substrate access + scalar reference. teko_iarray_data returns the
// pointer to the PACKED contiguous int32 cell buffer (the run a SIMD kernel walks); NULL if the
// array is NULL or empty. teko_iarray_sum is the SCALAR reference reduction (plain `for` loop) —
// it is BOTH the honest scalar fallback for non-vector targets AND the in-program self-check
// oracle the `simd.sum` proof asserts the vectorized result against. Pure C, NO intrinsics
// (portable: the vectorization lives in the BACKEND emitters, never here).
int32_t*    teko_iarray_data(TekoIArray* a);
int         teko_iarray_sum(const TekoIArray* a);

#define TEKO_IARRAY_MAX_LEN 65536 // bounded element count (allocation-free upper bound)

#endif // TEKO_IARRAY_H
