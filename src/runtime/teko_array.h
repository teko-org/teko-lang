#ifndef TEKO_ARRAY_H
#define TEKO_ARRAY_H

#include <stdint.h> // intptr_t — element cells must be pointer-width on EVERY ABI (Windows LLP64!)

// Phase 18 (18.E.1) — FIXED-size, CONTIGUOUS, CHECKED array substrate: a handle-backed contiguous
// store of register-width cells backing the `array` surface. This portable C runtime is the SINGLE
// SOURCE OF TRUTH for arrays (native teko_rt_array_* wrappers + the wasm32 reactor), the same
// pattern as the Phase 15 object model (teko_array.c mirrors teko_object.c exactly).
//
// The ONE semantic difference from the object store: array bounds violations are FAIL-LOUD, not
// defensive no-ops. The core get/set RETURN 0 on an out-of-range index (1 on success) so the
// register-width wrappers (teko_rt_array_get/set) can abort the program via the SAME teko_rt_die
// path the checked decimal/cast/parse surface uses (native exit 70 + stderr; WASM __builtin_trap).
//
// Cells are `intptr_t` (POINTER width), NOT `long`/int32: an element may hold a plain integer OR
// another handle (a pointer). `intptr_t` holds either WITHOUT truncation on every ABI — crucially
// on Windows LLP64 where `long` is only 32-bit while pointers are 64-bit (a `long` cell would
// truncate a stored handle). On LP64/wasm32 `intptr_t` == the register width anyway.
//
// Length is O(1) METADATA: the element count is stored in the header (no scan). Allocation is
// calloc — header + every cell zero-initialized (the Windows wild-free lesson: never leave an
// allocation's fields unset).

typedef struct TekoArray TekoArray;

// Allocate an array with `n` zero-initialized cells (n clamped to [0, MAX]). NULL on failure.
TekoArray* teko_array_new(int n);
void       teko_array_free(TekoArray* a);

// O(1) length metadata (the count stored in the header). 0 if NULL.
int        teko_array_len(const TekoArray* a);

// Bounds-RETURNING accessors: 1 on success, 0 on out-of-range/NULL (so the wrapper fails loudly).
// get writes the cell to *out on success (leaves *out untouched on failure).
int        teko_array_get(const TekoArray* a, int i, intptr_t* out);
int        teko_array_set(TekoArray* a, int i, intptr_t v);

#define TEKO_ARRAY_MAX_LEN 65536 // bounded element count (allocation-free upper bound)

#endif // TEKO_ARRAY_H
