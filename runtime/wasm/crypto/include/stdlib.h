/* Freestanding <stdlib.h> shim for the wasm32 crypto reactor build (no wasi-sdk).
 * malloc/free/calloc/realloc are implemented as a bump allocator in libc_shim.c. */
#ifndef TEKO_WASM_SHIM_STDLIB_H
#define TEKO_WASM_SHIM_STDLIB_H
#include <stddef.h>
void* malloc(size_t n);
void  free(void* p);
void* calloc(size_t n, size_t sz);
void* realloc(void* p, size_t n);
#endif
