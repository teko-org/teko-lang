#ifndef TEKO_SHARED_H
#define TEKO_SHARED_H

// Phase 14 (14.E) — automated shared memory: the `shared { … }` block + `atomic` control. The
// compiler injects a COARSE lock around a `shared` block (owner decision: one whole-block lock,
// not per-field) so the enclosed statements run as a critical section; `atomic.*` cells provide
// lock-free read-modify-write counters. This portable C runtime is the SINGLE SOURCE OF TRUTH
// for the surface (native teko_rt_* + the wasm32 reactor), same pattern as the channel runtimes.
//
// Portability (this TU is compiled into teko_core on every host incl. Windows MSVC, into the
// native teko_rt archive, and into the wasm32 reactor): the atomics use clang/gcc __atomic
// builtins (header-free; on wasm32 without the threads feature they lower to correct
// single-threaded ops) and the MSVC Interlocked intrinsics — NOT <stdatomic.h> (MSVC's support
// is version-dependent). The coarse lock is a single global spinlock (the coarsest correct MVP;
// per-block locks are a future refinement). Nested `shared` blocks are not supported in the MVP.

typedef struct TekoAtomicCell TekoAtomicCell;

// Enter / leave the global coarse critical section (injected around a `shared { … }` block).
// Balanced calls; acquires a real lock where threads exist, a fence/no-op single-threaded.
void teko_shared_enter(void);
void teko_shared_leave(void);

// Atomic integer cell (handle-based, like the channel runtimes).
TekoAtomicCell* teko_atomic_cell(long initial);
void            teko_atomic_free(TekoAtomicCell* c);
long            teko_atomic_add(TekoAtomicCell* c, long delta); // atomic fetch-add; returns NEW value
long            teko_atomic_load(TekoAtomicCell* c);
void            teko_atomic_store(TekoAtomicCell* c, long value);

#endif // TEKO_SHARED_H
