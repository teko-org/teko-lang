#ifndef TEKO_CRYPTO_SCRYPT_H
#define TEKO_CRYPTO_SCRYPT_H

// Native scrypt memory-hard KDF (RFC 7914). Pure, portable C23 — no external libraries.
// Built on Teko's PBKDF2-HMAC-SHA256 + Salsa20/8. KAT-tested against RFC 7914 §12 in
// tests/runtime/test_crypto_scrypt.c.

#include <stdint.h>
#include <stddef.h>

// scrypt(P, S, N, r, p) -> dk_len bytes. N must be a power of two > 1; r, p >= 1.
// Returns 0 on success, -1 on invalid parameters or allocation failure.
int teko_scrypt(const uint8_t* password, size_t password_len,
                const uint8_t* salt, size_t salt_len,
                uint64_t n, uint32_t r, uint32_t p,
                uint8_t* dk, size_t dk_len);

#endif // TEKO_CRYPTO_SCRYPT_H
