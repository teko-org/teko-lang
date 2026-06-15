#ifndef TEKO_CRYPTO_ED25519_H
#define TEKO_CRYPTO_ED25519_H

// Native Ed25519 signatures (RFC 8032) over the twisted Edwards Curve25519. Pure, portable
// C23 — no external libraries; built on the shared fe25519 field + Teko SHA-512. KAT-tested
// against RFC 8032 in tests/runtime/test_crypto_ed25519.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_ED25519_SEED_LEN 32u
#define TEKO_ED25519_PUB_LEN  32u
#define TEKO_ED25519_SIG_LEN  64u

// Derive the 32-byte public key from a 32-byte secret seed.
void teko_ed25519_pubkey(uint8_t pub[TEKO_ED25519_PUB_LEN], const uint8_t seed[TEKO_ED25519_SEED_LEN]);

// Produce a 64-byte signature of msg under (seed, pub).
void teko_ed25519_sign(uint8_t sig[TEKO_ED25519_SIG_LEN],
                       const uint8_t* msg, size_t msg_len,
                       const uint8_t seed[TEKO_ED25519_SEED_LEN],
                       const uint8_t pub[TEKO_ED25519_PUB_LEN]);

// Verify a signature. Returns 0 if valid, -1 otherwise.
int teko_ed25519_verify(const uint8_t sig[TEKO_ED25519_SIG_LEN],
                        const uint8_t* msg, size_t msg_len,
                        const uint8_t pub[TEKO_ED25519_PUB_LEN]);

#endif // TEKO_CRYPTO_ED25519_H
