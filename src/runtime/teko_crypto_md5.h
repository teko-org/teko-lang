#ifndef TEKO_CRYPTO_MD5_H
#define TEKO_CRYPTO_MD5_H

// Native MD5 (RFC 1321). Pure, portable C23 — no external libraries.
//
// ⚠️ LEGACY / INSECURE: MD5 is cryptographically broken (practical collisions). It is
// provided ONLY for backward compatibility and interop (e.g. UUID v3, legacy checksums) —
// NEVER for security purposes (no signatures, no password hashing, no integrity against an
// adversary). Use SHA-256/SHA-3/BLAKE3 for anything security-relevant.
//
// KAT-tested against RFC 1321 in tests/runtime/test_crypto_md5.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_MD5_DIGEST_LEN 16u
#define TEKO_MD5_BLOCK_LEN  64u

typedef struct {
    uint32_t state[4];
    uint64_t bit_len;
    uint8_t  buffer[TEKO_MD5_BLOCK_LEN];
    size_t   buffer_len;
} TekoMd5Ctx;

void teko_md5_init(TekoMd5Ctx* ctx);
void teko_md5_update(TekoMd5Ctx* ctx, const uint8_t* data, size_t len);
void teko_md5_final(TekoMd5Ctx* ctx, uint8_t out[TEKO_MD5_DIGEST_LEN]);
void teko_md5(const uint8_t* data, size_t len, uint8_t out[TEKO_MD5_DIGEST_LEN]);

#endif // TEKO_CRYPTO_MD5_H
