// src/compress/compress.h — C7.12: deterministic ZIP-STORE writer (teko::compress).
// Mirrors compress.tks exactly. Method 0 (STORE, no compression). Deterministic: no
// timestamps, no host-OS metadata — mod_date is fixed at 2025-01-01 (0x5A21).
#ifndef TK_COMPRESS_H
#define TK_COMPRESS_H

#include "../emit/tkb_buf.h"   // tk_bytes (growable byte buffer), tk_str

// A single ZIP entry: a filename (str) + raw byte payload.
typedef struct {
    tk_str  name;   // filename stored in the ZIP (e.g. "mylib.tkh")
    tk_bytes data;  // the file's bytes
} tk_zip_entry;

// Assemble a complete ZIP-STORE archive from `n` entries and return it as a
// heap-allocated tk_bytes. Caller owns the returned buffer. Never fails (OOM → abort).
tk_bytes tk_write_zip(const tk_zip_entry *entries, size_t n);

// Parse a ZIP-STORE archive (typically a .tkl package). Returns a heap-allocated array of
// entries (caller frees with tk_free0; each entry's name.ptr and data.ptr are owned copies).
// *out_n receives the entry count (0 on error or empty archive). Mirrors compress.tks::read_zip.
tk_zip_entry *tk_read_zip(const tk_byte *data, size_t data_len, size_t *out_n);

#endif // TK_COMPRESS_H
