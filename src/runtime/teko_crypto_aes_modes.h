#ifndef TEKO_CRYPTO_AES_MODES_H
#define TEKO_CRYPTO_AES_MODES_H

// AES block-cipher modes (NIST SP 800-38A): CTR and CBC over the constant-time AES core.
// Portable C23, no external libraries. KAT-tested in tests/runtime/test_crypto_aes_modes.c.

#include <stdint.h>
#include <stddef.h>

#include "teko_crypto_aes.h"

// AES-CTR (symmetric: same call encrypts and decrypts). `iv` is the 16-byte initial counter
// block, incremented as a 128-bit big-endian integer per block. Any length; in/out may alias.
void teko_aes_ctr_xor(const TekoAesKey* key, const uint8_t iv[16],
                      const uint8_t* in, uint8_t* out, size_t len);

// AES-CBC. `len` must be a non-zero multiple of 16 (no padding). Returns 0, or -1 on bad len.
int teko_aes_cbc_encrypt(const TekoAesKey* key, const uint8_t iv[16],
                         const uint8_t* in, uint8_t* out, size_t len);
int teko_aes_cbc_decrypt(const TekoAesKey* key, const uint8_t iv[16],
                         const uint8_t* in, uint8_t* out, size_t len);

#endif // TEKO_CRYPTO_AES_MODES_H
