// src/compress/compress.c — C7.12: deterministic ZIP-STORE writer (teko::compress). Mirrors compress.tks exactly.
#include "compress.h"
#include <stdint.h>
#include <string.h>

// --- CRC-32 (polynomial 0xEDB88320, reflected / little-endian standard) ---

// Compute one row of the 256-entry CRC-32 table (table-free variant; called per byte).
static uint32_t crc32_row(uint32_t i) {
    uint32_t crc = i;
    for (int k = 0; k < 8; k += 1) {
        if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320u;
        else         crc = (crc >> 1);
    }
    return crc;
}

static uint32_t crc32_of(const tk_byte *data, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i += 1)
        crc = crc32_row((crc ^ data[i]) & 0xFF) ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// --- little-endian write helpers ---

static tk_bytes pu16(tk_bytes b, uint32_t x) {
    b = tk_bytes_push(b, (tk_byte)(x      ));
    b = tk_bytes_push(b, (tk_byte)(x >>  8));
    return b;
}

static tk_bytes pu32(tk_bytes b, uint32_t x) {
    b = tk_bytes_push(b, (tk_byte)(x      ));
    b = tk_bytes_push(b, (tk_byte)(x >>  8));
    b = tk_bytes_push(b, (tk_byte)(x >> 16));
    b = tk_bytes_push(b, (tk_byte)(x >> 24));
    return b;
}

static tk_bytes pstr(tk_bytes b, tk_str s) {
    for (size_t i = 0; i < s.len; i += 1)
        b = tk_bytes_push(b, s.ptr[i]);
    return b;
}

static tk_bytes pbytes(tk_bytes b, const tk_bytes *src) {
    for (size_t i = 0; i < src->len; i += 1)
        b = tk_bytes_push(b, src->ptr[i]);
    return b;
}

// --- per-entry central-directory bookkeeping ---
typedef struct {
    tk_str   name;
    uint32_t crc32;
    uint32_t size;
    uint32_t offset;   // byte offset of local header from archive start
} zip_cd_entry;

TK_LIST(zip_cd_entry, zip_cd_list);

// DOS date for 2025-01-01: year=(2025-1980)=45, month=1, day=1
// Packed: (45 << 9) | (1 << 5) | 1 = 0x5A21
#define ZIP_MOD_DATE 0x5A21u
#define ZIP_VER      20u       // version 2.0

tk_bytes tk_write_zip(const tk_zip_entry *entries, size_t n) {
    tk_bytes buf = tk_bytes_empty();
    zip_cd_list cd = zip_cd_list_empty();

    // --- Phase 1: local file headers + file data ---
    for (size_t i = 0; i < n; i += 1) {
        const tk_zip_entry *e = &entries[i];
        uint32_t crc = crc32_of(e->data.ptr, e->data.len);
        uint32_t sz  = (uint32_t)e->data.len;
        uint32_t off = (uint32_t)buf.len;

        // Local File Header signature: PK\x03\x04
        buf = tk_bytes_push(buf, 0x50); buf = tk_bytes_push(buf, 0x4B);
        buf = tk_bytes_push(buf, 0x03); buf = tk_bytes_push(buf, 0x04);
        buf = pu16(buf, ZIP_VER);          // version needed
        buf = pu16(buf, 0);                // flags
        buf = pu16(buf, 0);                // method: STORE
        buf = pu16(buf, 0);                // mod time: 00:00:00
        buf = pu16(buf, ZIP_MOD_DATE);     // mod date: 2025-01-01
        buf = pu32(buf, crc);              // CRC-32
        buf = pu32(buf, sz);               // compressed size
        buf = pu32(buf, sz);               // uncompressed size
        buf = pu16(buf, (uint32_t)e->name.len);  // filename length
        buf = pu16(buf, 0);                // extra field length
        buf = pstr(buf, e->name);          // filename

        // File data
        buf = pbytes(buf, &e->data);

        zip_cd_entry rec = { .name = e->name, .crc32 = crc, .size = sz, .offset = off };
        cd = zip_cd_list_push(cd, rec);
    }

    // --- Phase 2: central directory ---
    uint32_t cd_offset = (uint32_t)buf.len;
    for (size_t j = 0; j < cd.len; j += 1) {
        const zip_cd_entry *r = &cd.ptr[j];

        // Central Directory File Header signature: PK\x01\x02
        buf = tk_bytes_push(buf, 0x50); buf = tk_bytes_push(buf, 0x4B);
        buf = tk_bytes_push(buf, 0x01); buf = tk_bytes_push(buf, 0x02);
        buf = pu16(buf, ZIP_VER);          // version made by
        buf = pu16(buf, ZIP_VER);          // version needed
        buf = pu16(buf, 0);                // flags
        buf = pu16(buf, 0);                // method: STORE
        buf = pu16(buf, 0);                // mod time
        buf = pu16(buf, ZIP_MOD_DATE);     // mod date: 2025-01-01
        buf = pu32(buf, r->crc32);
        buf = pu32(buf, r->size);          // compressed size
        buf = pu32(buf, r->size);          // uncompressed size
        buf = pu16(buf, (uint32_t)r->name.len);  // filename length
        buf = pu16(buf, 0);                // extra field length
        buf = pu16(buf, 0);                // file comment length
        buf = pu16(buf, 0);                // disk number start
        buf = pu16(buf, 0);                // internal file attributes
        buf = pu32(buf, 0);                // external file attributes
        buf = pu32(buf, r->offset);        // relative offset of local header
        buf = pstr(buf, r->name);          // filename
    }

    uint32_t cd_size = (uint32_t)buf.len - cd_offset;

    // --- Phase 3: end of central directory record (22 bytes) ---
    // signature: PK\x05\x06
    buf = tk_bytes_push(buf, 0x50); buf = tk_bytes_push(buf, 0x4B);
    buf = tk_bytes_push(buf, 0x05); buf = tk_bytes_push(buf, 0x06);
    buf = pu16(buf, 0);                    // disk number
    buf = pu16(buf, 0);                    // disk with start of CD
    buf = pu16(buf, (uint32_t)n);          // entries on this disk
    buf = pu16(buf, (uint32_t)n);          // total entries
    buf = pu32(buf, cd_size);              // size of central directory
    buf = pu32(buf, cd_offset);            // offset of central directory
    buf = pu16(buf, 0);                    // archive comment length

    // free the central-directory metadata list (the bytes are now in `buf`)
    tk_free0(cd.ptr);

    return buf;
}
