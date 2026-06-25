// src/emit/tkb_buf.h — .tkb serialization primitives.
#ifndef TK_EMIT_TKB_BUF_H
#define TK_EMIT_TKB_BUF_H

#include "../core.h"
#include "../text/text.h"

TK_LIST(tk_byte, tk_bytes);        // the growable output buffer

tk_bytes tk_write_u8(tk_bytes b, tk_byte x);
tk_bytes tk_write_u32(tk_bytes b, uint32_t x);
tk_bytes tk_write_u64(tk_bytes b, uint64_t x);   // LE: low u32 then high u32 (inverse of tk_read_u64)
tk_bytes tk_write_i64(tk_bytes b, int64_t x);
tk_bytes tk_write_bytes(tk_bytes b, tk_str s);

typedef struct { tk_str *ptr; size_t len; size_t cap; } tk_strtable;
tk_strtable tk_st_empty(void);
uint32_t    tk_st_find(tk_strtable t, tk_str s);          // 0xFFFFFFFF if absent
uint32_t    tk_st_intern(tk_strtable *t, tk_str s);       // mutates t; returns index
tk_bytes    tk_write_strtable(tk_bytes b, tk_strtable t);

uint64_t tk_fnv1a(const tk_byte *data, size_t n);

#endif // TK_EMIT_TKB_BUF_H
