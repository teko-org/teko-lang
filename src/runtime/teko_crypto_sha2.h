#ifndef TEKO_CRYPTO_SHA2_H
#define TEKO_CRYPTO_SHA2_H

// Native SHA-2 (FIPS 180-4). Pure, portable C23 — no external libraries.
// Part of the embedded native runtime; the single source of truth for the
// `hash` language surface (Phase 13). KAT-tested in tests/runtime/test_crypto_sha2.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_SHA256_DIGEST_LEN 32u
#define TEKO_SHA256_BLOCK_LEN  64u

// Streaming context. Zero-initialize via teko_sha256_init before use.
typedef struct {
    uint32_t state[8];      // running hash state (h0..h7)
    uint64_t bit_len;       // total message length in bits
    uint8_t  buffer[TEKO_SHA256_BLOCK_LEN]; // partial-block staging
    size_t   buffer_len;    // bytes currently staged in buffer (< block len)
} TekoSha256Ctx;

// Streaming API: init -> update* -> final (writes 32 bytes into out).
void teko_sha256_init(TekoSha256Ctx* ctx);
void teko_sha256_update(TekoSha256Ctx* ctx, const uint8_t* data, size_t len);
void teko_sha256_final(TekoSha256Ctx* ctx, uint8_t out[TEKO_SHA256_DIGEST_LEN]);

// One-shot convenience: out = SHA-256(data[0..len)).
void teko_sha256(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA256_DIGEST_LEN]);

#endif // TEKO_CRYPTO_SHA2_H
