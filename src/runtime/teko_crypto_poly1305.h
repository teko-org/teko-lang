#ifndef TEKO_CRYPTO_POLY1305_H
#define TEKO_CRYPTO_POLY1305_H

// Native Poly1305 one-time authenticator (RFC 8439 §2.5). Portable C23, constant-time
// final reduction — the well-known 26-bit-limb ("donna 32-bit") field arithmetic, which
// needs only 32x32->64 multiplies (MSVC-safe; no __int128). No external libraries.
// KAT-tested against RFC 8439 in tests/runtime/test_crypto_poly1305.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_POLY1305_KEY_LEN 32u
#define TEKO_POLY1305_TAG_LEN 16u

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    size_t   leftover;
    uint8_t  buffer[16];
    uint8_t  final;
} TekoPoly1305Ctx;

// Streaming API: init -> update* -> finish (writes the 16-byte tag).
void teko_poly1305_init(TekoPoly1305Ctx* ctx, const uint8_t key[TEKO_POLY1305_KEY_LEN]);
void teko_poly1305_update(TekoPoly1305Ctx* ctx, const uint8_t* m, size_t bytes);
void teko_poly1305_finish(TekoPoly1305Ctx* ctx, uint8_t tag[TEKO_POLY1305_TAG_LEN]);

// One-shot convenience.
void teko_poly1305(const uint8_t* m, size_t bytes,
                   const uint8_t key[TEKO_POLY1305_KEY_LEN],
                   uint8_t tag[TEKO_POLY1305_TAG_LEN]);

#endif // TEKO_CRYPTO_POLY1305_H
