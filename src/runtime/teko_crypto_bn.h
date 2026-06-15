#ifndef TEKO_CRYPTO_BN_H
#define TEKO_CRYPTO_BN_H

// Native multi-precision integer / Montgomery modular arithmetic — the bignum layer shared
// by RSA and the NIST P-curves (Phase 13.3b). Pure, portable C23 — fixed-capacity 32-bit
// limbs with 64-bit accumulators (MSVC-safe, no __int128). Montgomery (CIOS) multiplication
// with runtime-derived R²/n′ constants (no hand-transcribed magic). KAT-tested in
// tests/runtime/test_crypto_bn.c.

#include <stdint.h>
#include <stddef.h>

// Supports moduli up to ~4160 bits (RSA-4096 + headroom).
#define TEKO_BN_MAX_LIMBS 130

typedef struct {
    uint32_t m[TEKO_BN_MAX_LIMBS];   // odd modulus, little-endian limbs
    uint32_t rr[TEKO_BN_MAX_LIMBS];  // R^2 mod m, R = 2^(32*n)
    uint32_t one[TEKO_BN_MAX_LIMBS]; // R mod m (Montgomery form of 1)
    uint32_t n0;                     // -m^{-1} mod 2^32
    int n;                           // active limb count
} TekoMont;

// Initialize from a big-endian modulus (must be odd, fit in MAX_LIMBS). Returns 0, or -1.
int teko_mont_init(TekoMont* mont, const uint8_t* mod_be, size_t mod_len);

// out_be = (base ^ exp) mod m, all big-endian. out_len should be the modulus byte length.
// Returns 0 on success, -1 on bad args.
int teko_mont_modexp(const TekoMont* mont,
                     const uint8_t* base_be, size_t base_len,
                     const uint8_t* exp_be, size_t exp_len,
                     uint8_t* out_be, size_t out_len);

// Convenience: out_be = (base ^ exp) mod modulus (inits a Montgomery context internally).
int teko_bn_modexp(const uint8_t* mod_be, size_t mod_len,
                   const uint8_t* base_be, size_t base_len,
                   const uint8_t* exp_be, size_t exp_len,
                   uint8_t* out_be);

#endif // TEKO_CRYPTO_BN_H
