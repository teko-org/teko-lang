/* Freestanding <string.h> shim for the wasm32 crypto reactor build (no wasi-sdk).
 * Declares only what the crypto sources use; implementations live in libc_shim.c. */
#ifndef TEKO_WASM_SHIM_STRING_H
#define TEKO_WASM_SHIM_STRING_H
#include <stddef.h>
void* memcpy(void* dst, const void* src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int   memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);
#endif
