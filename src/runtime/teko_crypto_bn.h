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

// --- Limb-level field operations (shared by the NIST P-curves; Phase 13.3b) -------------
//
// These operate on little-endian limb arrays of length mont->n. Modular add/sub are domain
// agnostic (they work on either ordinary or Montgomery-form residues since both are linear).
// Montgomery multiplication consumes and produces Montgomery-form values. teko_mont_to /
// teko_mont_from convert an ordinary residue (< m) to/from Montgomery form. All inputs must
// be reduced (< m); outputs are reduced. None branch on the limb *values* (constant-time
// w.r.t. secret data), so they are safe for secret-dependent curve arithmetic.

// out = a * b mod m, all in Montgomery form (out, a, b are Montgomery-form residues).
void teko_mont_mul(const TekoMont* mont, uint32_t* out,
                   const uint32_t* a, const uint32_t* b);

// out = a + b mod m (domain agnostic; a, b < m).
void teko_mont_add(const TekoMont* mont, uint32_t* out,
                   const uint32_t* a, const uint32_t* b);

// out = a - b mod m (domain agnostic; a, b < m).
void teko_mont_sub(const TekoMont* mont, uint32_t* out,
                   const uint32_t* a, const uint32_t* b);

// out = a * R mod m  (ordinary residue a < m  ->  Montgomery form).
void teko_mont_to(const TekoMont* mont, uint32_t* out, const uint32_t* a);

// out = a * R^{-1} mod m  (Montgomery form a  ->  ordinary residue).
void teko_mont_from(const TekoMont* mont, uint32_t* out, const uint32_t* a);

// Constant-time conditional copy: out = (mask ? a : b), per limb. mask must be 0 or
// 0xFFFFFFFF (use teko_bn_mask1 to broadcast a 0/1 flag). out may alias a or b.
void teko_bn_cselect(uint32_t* out, const uint32_t* a, const uint32_t* b,
                     int n, uint32_t mask);

// Constant-time conditional swap of a and b when mask == 0xFFFFFFFF (no-op when 0).
void teko_bn_cswap(uint32_t* a, uint32_t* b, int n, uint32_t mask);

// Broadcast a 0/1 flag to a full 0x00000000 / 0xFFFFFFFF limb mask (branchless).
uint32_t teko_bn_mask1(uint32_t flag);

// Load/store little-endian limbs from/to a big-endian byte buffer (zero-extending / -padding).
void teko_bn_load_be(uint32_t* limbs, int n, const uint8_t* bytes, size_t blen);
void teko_bn_store_be(uint8_t* bytes, size_t blen, const uint32_t* limbs, int n);

// Constant-time comparisons over n limbs. teko_bn_is_zero returns 1 iff all limbs are 0.
// teko_bn_eq returns 1 iff a == b. Both return 0/1 without a value-dependent branch.
int teko_bn_is_zero(const uint32_t* a, int n);
int teko_bn_eq(const uint32_t* a, const uint32_t* b, int n);

#endif // TEKO_CRYPTO_BN_H
