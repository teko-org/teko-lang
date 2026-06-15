#ifndef TEKO_CRYPTO_HMAC_H
#define TEKO_CRYPTO_HMAC_H

// Native HMAC (RFC 2104 / FIPS 198-1) over the Teko hash primitives. Portable C23,
// no external libraries. Source of truth for keyed-MAC and the KDF layer (HKDF/PBKDF2).
// KAT-tested against RFC 4231 in tests/runtime/test_crypto_hmac.c.

#include <stdint.h>
#include <stddef.h>

#include "teko_crypto_sha2.h"
#include "teko_crypto_sha512.h"

// HMAC-SHA-256: out = HMAC(key, msg), 32-byte tag.
void teko_hmac_sha256(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA256_DIGEST_LEN]);

// HMAC-SHA-512: out = HMAC(key, msg), 64-byte tag.
void teko_hmac_sha512(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA512_DIGEST_LEN]);

// HMAC-SHA-384: out = HMAC(key, msg), 48-byte tag (block size 128, like SHA-512). Used by
// the RFC 6979 deterministic-nonce generator for P-384 ECDSA.
void teko_hmac_sha384(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA384_DIGEST_LEN]);

#endif // TEKO_CRYPTO_HMAC_H
