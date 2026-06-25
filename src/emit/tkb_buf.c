// src/emit/tkb_buf.c
#include "tkb_buf.h"
#include <string.h>

tk_bytes tk_write_u8(tk_bytes b, tk_byte x) { return tk_bytes_push(b, x); }

tk_bytes tk_write_u32(tk_bytes b, uint32_t x) {
    b = tk_bytes_push(b, (tk_byte)(x      ));
    b = tk_bytes_push(b, (tk_byte)(x >>  8));
    b = tk_bytes_push(b, (tk_byte)(x >> 16));
    b = tk_bytes_push(b, (tk_byte)(x >> 24));
    return b;
}

// LE u64 = low u32 then high u32 — the exact inverse of tk_read_u64 (lo | hi<<32).
tk_bytes tk_write_u64(tk_bytes b, uint64_t x) {
    b = tk_write_u32(b, (uint32_t)(x & 0xFFFFFFFFu));
    b = tk_write_u32(b, (uint32_t)(x >> 32));
    return b;
}

tk_bytes tk_write_i64(tk_bytes b, int64_t x) {
    uint64_t bits = (uint64_t)x;                 // reinterpret two's-complement bits
    for (int k = 0; k < 8; k += 1) { b = tk_bytes_push(b, (tk_byte)(bits & 0xFF)); bits >>= 8; }
    return b;
}

tk_bytes tk_write_bytes(tk_bytes b, tk_str s) {
    b = tk_write_u32(b, (uint32_t)s.len);
    for (size_t i = 0; i < s.len; i += 1) b = tk_bytes_push(b, s.ptr[i]);
    return b;
}

tk_strtable tk_st_empty(void) { return (tk_strtable){ NULL, 0, 0 }; }

static bool str_eq(tk_str a, tk_str b) { return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0; }

uint32_t tk_st_find(tk_strtable t, tk_str s) {
    for (size_t i = 0; i < t.len; i += 1) if (str_eq(t.ptr[i], s)) return (uint32_t)i;
    return 0xFFFFFFFFu;
}

uint32_t tk_st_intern(tk_strtable *t, tk_str s) {
    uint32_t f = tk_st_find(*t, s);
    if (f != 0xFFFFFFFFu) return f;
    if (t->len == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 8;
        tk_str *np = realloc(t->ptr, nc * sizeof *np); if (!np) abort();
        t->ptr = np; t->cap = nc;
    }
    t->ptr[t->len] = s;
    return (uint32_t)(t->len++);
}

tk_bytes tk_write_strtable(tk_bytes b, tk_strtable t) {
    b = tk_write_u32(b, (uint32_t)t.len);
    for (size_t i = 0; i < t.len; i += 1) b = tk_write_bytes(b, t.ptr[i]);
    return b;
}

uint64_t tk_fnv1a(const tk_byte *data, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; i += 1) { h ^= (uint64_t)data[i]; h *= 0x100000001B3ull; }
    return h;
}
