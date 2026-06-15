/* Freestanding libc for the wasm32 crypto reactor (Phase 13 Sub-phase C, "big step").
 *
 * The crypto C runtime (src/runtime/teko_crypto_*.c) is the single source of truth for
 * every primitive. To make it run on the WASM target we compile it to a wasm32 "reactor"
 * module (crypto.wasm) instead of re-implementing each algorithm in WAT. That build is
 * freestanding (no wasi-sdk): clang's compiler headers provide <stdint.h>/<stddef.h>, and
 * THIS file provides the only libc the sources touch — mem* + a bump malloc.
 *
 * Memory model: the reactor SHARES one linear memory with the Teko-emitted module. The
 * link places all of the reactor's image (rodata + shadow stack) at/above 64 KiB
 * (--global-base=65536, --no-stack-first), and this malloc bumps upward from the
 * linker-provided __heap_base (just past that image), growing the memory as needed. Teko's
 * own allocators live entirely in [0..65536), so the two never alias. free() is a no-op:
 * a crypto CLI program makes a handful of single-shot calls and exits, so a monotonic bump
 * is correct and simplest (no fragmentation, no reuse needed). */
#include <stddef.h>
#include <stdint.h>

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void* memset(void* dst, int c, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}
void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    if (d == s || n == 0) return dst;
    if (d < s) { for (size_t i = 0; i < n; i++) d[i] = s[i]; }
    else { for (size_t i = n; i > 0; i--) d[i - 1] = s[i - 1]; }
    return dst;
}
int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    for (size_t i = 0; i < n; i++) { if (x[i] != y[i]) return (int)x[i] - (int)y[i]; }
    return 0;
}
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }

extern unsigned char __heap_base;       /* linker-defined: top of rodata+stack image */
static uintptr_t g_brk = 0;

void* malloc(size_t n) {
    if (g_brk == 0) g_brk = (uintptr_t)&__heap_base;
    n = (n + 15u) & ~(size_t)15u;        /* 16-byte align */
    uintptr_t p = g_brk;
    g_brk += n;
    /* Grow linear memory if the bump pointer passed the current size. */
    size_t cur_pages = (size_t)__builtin_wasm_memory_size(0);
    size_t need_pages = (size_t)((g_brk + 65535u) / 65536u);
    if (need_pages > cur_pages) {
        (void)__builtin_wasm_memory_grow(0, need_pages - cur_pages);
    }
    return (void*)p;
}
void  free(void* p) { (void)p; }
void* calloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void* p = malloc(total);
    memset(p, 0, total);
    return p;
}
void* realloc(void* p, size_t n) { (void)p; return malloc(n); }
