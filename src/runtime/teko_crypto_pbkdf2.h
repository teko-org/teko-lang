#ifndef TEKO_CRYPTO_PBKDF2_H
#define TEKO_CRYPTO_PBKDF2_H

// Native PBKDF2 (RFC 2898 / PKCS#5) over the Teko HMAC primitives. Pure, portable C23 —
// no external libraries. KAT-tested against the RFC 7914 §11 PBKDF2-HMAC-SHA256 vectors
// in tests/runtime/test_crypto_pbkdf2.c. (RFC 6070's vectors are HMAC-SHA1, which Teko
// deliberately does not implement; RFC 7914 provides the authoritative SHA-256 vectors.)

#include <stdint.h>
#include <stddef.h>

// Derive dk_len bytes from (password, salt) with `iterations` rounds.
// Returns 0 on success, -1 on bad args (iterations == 0) or allocation failure.
int teko_pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                            const uint8_t* salt, size_t salt_len,
                            uint32_t iterations,
                            uint8_t* dk, size_t dk_len);

int teko_pbkdf2_hmac_sha512(const uint8_t* password, size_t password_len,
                            const uint8_t* salt, size_t salt_len,
                            uint32_t iterations,
                            uint8_t* dk, size_t dk_len);

#endif // TEKO_CRYPTO_PBKDF2_H
