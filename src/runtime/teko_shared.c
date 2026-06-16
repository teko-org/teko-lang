#include "teko_shared.h"
#include <stdlib.h>

// Phase 14 (14.E) — shared-memory runtime. See teko_shared.h. Portable atomic primitives
// (no <stdatomic.h>, for MSVC safety): clang/gcc __atomic builtins elsewhere (incl. wasm32),
// MSVC Interlocked intrinsics on Windows.

#if defined(_MSC_VER)
#include <intrin.h>
// _InterlockedExchangeAdd64 returns the PREVIOUS value; add delta back for fetch-add-new.
#define TK_ADD64(p, d) (_InterlockedExchangeAdd64((volatile long long*)(p), (long long)(d)) + (long long)(d))
#define TK_LOAD64(p)   (_InterlockedExchangeAdd64((volatile long long*)(p), 0))
#define TK_STORE64(p, v) ((void)_InterlockedExchange64((volatile long long*)(p), (long long)(v)))
#define TK_TAS8(p)     (_InterlockedExchange8((volatile char*)(p), 1)) // returns previous (0=free)
#define TK_CLR8(p)     ((void)_InterlockedExchange8((volatile char*)(p), 0))
#else
#define TK_ADD64(p, d) (__atomic_add_fetch((p), (d), __ATOMIC_SEQ_CST))
#define TK_LOAD64(p)   (__atomic_load_n((p), __ATOMIC_SEQ_CST))
#define TK_STORE64(p, v) (__atomic_store_n((p), (v), __ATOMIC_SEQ_CST))
#define TK_TAS8(p)     (__atomic_test_and_set((p), __ATOMIC_SEQ_CST)) // returns previous (false=free)
#define TK_CLR8(p)     (__atomic_clear((p), __ATOMIC_SEQ_CST))
#endif

struct TekoAtomicCell {
    long long value;
};

// Single global coarse lock guarding all `shared { … }` blocks (coarsest correct MVP).
static volatile char g_shared_lock = 0;

void teko_shared_enter(void) {
    // Spin until we flip the flag 0 -> 1. Uncontended single-threaded: acquires immediately.
    while (TK_TAS8(&g_shared_lock)) { /* spin */ }
}

void teko_shared_leave(void) {
    TK_CLR8(&g_shared_lock);
}

TekoAtomicCell* teko_atomic_cell(long initial) {
    TekoAtomicCell* c = (TekoAtomicCell*)malloc(sizeof(TekoAtomicCell));
    if (!c) return NULL;
    c->value = (long long)initial;
    return c;
}

void teko_atomic_free(TekoAtomicCell* c) { free(c); }

long teko_atomic_add(TekoAtomicCell* c, long delta) {
    if (!c) return 0;
    return (long)TK_ADD64(&c->value, (long long)delta);
}

long teko_atomic_load(TekoAtomicCell* c) {
    if (!c) return 0;
    return (long)TK_LOAD64(&c->value);
}

void teko_atomic_store(TekoAtomicCell* c, long value) {
    if (!c) return;
    TK_STORE64(&c->value, (long long)value);
}
