#ifndef TEKO_OBJECT_H
#define TEKO_OBJECT_H

#include <stdint.h> // intptr_t — field cells must be pointer-width on EVERY ABI (Windows LLP64!)

// Phase 15 (15.A) — bare-metal object model: a handle-based field-cell store backing the
// `class` surface. This portable C runtime is the SINGLE SOURCE OF TRUTH for object instances
// (native teko_rt_object_* wrappers + the wasm32 reactor), the same pattern as the Phase 14
// channel runtimes.
//
// ZERO RUNTIME REFLECTION: an object is just a fixed-size vector of register-width cells. There
// is NO name->field lookup, NO type tag, NO attribute bag at runtime — field *indices* are
// resolved at COMPILE TIME from the class declaration, and a method is a plain function that
// takes the object handle. The store only allocates the cells and reads/writes them by index.
//
// Cells are `intptr_t` (POINTER width), NOT `long`/int32: a field may hold a plain integer OR
// another object's handle (a pointer). `intptr_t` holds either WITHOUT truncation on every ABI —
// crucially on Windows LLP64 where `long` is only 32-bit while pointers are 64-bit (a `long` cell
// would truncate a stored handle there). On LP64/wasm32 `intptr_t` == the register width anyway.

typedef struct TekoObject TekoObject;

// Allocate an object with `nfields` zero-initialized cells (nfields clamped to [0, MAX]).
// Returns NULL on allocation failure. Free with teko_object_free.
TekoObject* teko_object_new(int nfields);
void        teko_object_free(TekoObject* o);

// Field access by COMPILE-TIME index. Out-of-range / NULL are defensive no-ops (set) / 0 (get) —
// never crash (the compiler always emits valid indices; this just hardens the boundary).
void        teko_object_set(TekoObject* o, int idx, intptr_t value);
intptr_t    teko_object_get(const TekoObject* o, int idx);
int         teko_object_nfields(const TekoObject* o); // 0 if NULL

#define TEKO_OBJECT_MAX_FIELDS 256 // bounded field count (allocation-free upper bound)

#endif // TEKO_OBJECT_H
