#ifndef TEKO_CRYPTO_SHA512_H
#define TEKO_CRYPTO_SHA512_H

// Native SHA-512 / SHA-384 (FIPS 180-4). Pure, portable C23 — no external libraries.
// Part of the embedded native runtime; source of truth for the `hash` language surface.
// KAT-tested in tests/runtime/test_crypto_sha512.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_SHA512_DIGEST_LEN 64u
#define TEKO_SHA384_DIGEST_LEN 48u
#define TEKO_SHA512_BLOCK_LEN  128u

// Streaming context shared by SHA-512 and SHA-384 (same compression, differing IV/output).
typedef struct {
    uint64_t state[8];      // running hash state (h0..h7)
    uint64_t len_lo;        // message length in bits, low 64 bits
    uint64_t len_hi;        // message length in bits, high 64 bits
    uint8_t  buffer[TEKO_SHA512_BLOCK_LEN]; // partial-block staging
    size_t   buffer_len;    // bytes currently staged (< block len)
} TekoSha512Ctx;

// SHA-512 (64-byte digest).
void teko_sha512_init(TekoSha512Ctx* ctx);
void teko_sha512_update(TekoSha512Ctx* ctx, const uint8_t* data, size_t len);
void teko_sha512_final(TekoSha512Ctx* ctx, uint8_t out[TEKO_SHA512_DIGEST_LEN]);
void teko_sha512(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA512_DIGEST_LEN]);

// SHA-384 (48-byte digest) — same core, different IV, truncated output.
void teko_sha384_init(TekoSha512Ctx* ctx);
void teko_sha384_final(TekoSha512Ctx* ctx, uint8_t out[TEKO_SHA384_DIGEST_LEN]);
void teko_sha384(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA384_DIGEST_LEN]);

#endif // TEKO_CRYPTO_SHA512_H
