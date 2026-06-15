#include "unity.h"
#include "../../src/runtime/teko_crypto_bn.h"

#include <string.h>

// Textbook RSA (p=61, q=53, n=3233, e=17, d=413): 65^17 mod 3233 = 2790, and back.
void test_teko_bn_modexp_rsa3233(void) {
    const uint8_t n[2] = { 0x0c, 0xa1 };   // 3233
    const uint8_t e[1] = { 0x11 };         // 17
    const uint8_t d[2] = { 0x01, 0x9d };   // 413
    const uint8_t m[2] = { 0x00, 0x41 };   // 65
    uint8_t c[2], back[2];

    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(n, 2u, m, 2u, e, 1u, c));
    TEST_ASSERT_EQUAL_UINT8(0x0a, c[0]); // 2790 = 0x0AE6
    TEST_ASSERT_EQUAL_UINT8(0xe6, c[1]);

    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(n, 2u, c, 2u, d, 2u, back));
    TEST_ASSERT_EQUAL_MEMORY(m, back, 2u);
}

// Fermat's little theorem on the 256-bit NIST P-256 prime p: 2^(p-1) mod p == 1.
// Exercises multi-limb (8x32) Montgomery modexp against an authoritative modulus.
void test_teko_bn_modexp_fermat_p256(void) {
    static const uint8_t p[32] = {
        0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
    };
    uint8_t pm1[32];
    const uint8_t base[1] = { 0x02 };
    uint8_t out[32];
    uint8_t one[32];
    unsigned i;

    memcpy(pm1, p, 32u);
    pm1[31] = 0xfe; // p - 1

    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(p, 32u, base, 1u, pm1, 32u, out));

    memset(one, 0, 32u);
    one[31] = 1u;
    TEST_ASSERT_EQUAL_MEMORY(one, out, 32u);

    // Also a^p == a (mod p) for a small base (another Fermat form).
    {
        uint8_t out2[32], a7[32];
        const uint8_t seven[1] = { 0x07 };
        TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(p, 32u, seven, 1u, p, 32u, out2));
        memset(a7, 0, 32u);
        a7[31] = 7u;
        TEST_ASSERT_EQUAL_MEMORY(a7, out2, 32u);
        (void)i;
    }
}

// The NIST P-256 prime p (big-endian) — a representative multi-limb (8x32) odd modulus.
static const uint8_t k_p256_prime[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
};

// Field ops on a tiny single-limb modulus (m = 97): hand-computable mul/add/sub.
void test_teko_bn_fieldops_smallmod(void) {
    const uint8_t mod[1] = { 0x61 }; // 97
    TekoMont mont;
    uint32_t a[1], b[1], am[1], bm[1], prod[1], out[1];
    TEST_ASSERT_EQUAL_INT(0, teko_mont_init(&mont, mod, 1u));
    TEST_ASSERT_EQUAL_INT(1, mont.n);

    // 10 * 12 = 120 = 23 (mod 97), through Montgomery form.
    a[0] = 10u; b[0] = 12u;
    teko_mont_to(&mont, am, a);
    teko_mont_to(&mont, bm, b);
    teko_mont_mul(&mont, prod, am, bm);
    teko_mont_from(&mont, out, prod);
    TEST_ASSERT_EQUAL_UINT32(23u, out[0]);

    // 50 + 60 = 110 = 13 (mod 97); add is domain-agnostic (ordinary residues).
    a[0] = 50u; b[0] = 60u;
    teko_mont_add(&mont, out, a, b);
    TEST_ASSERT_EQUAL_UINT32(13u, out[0]);

    // 50 - 60 = -10 = 87 (mod 97).
    teko_mont_sub(&mont, out, a, b);
    TEST_ASSERT_EQUAL_UINT32(87u, out[0]);

    // to/from round-trip.
    a[0] = 42u;
    teko_mont_to(&mont, am, a);
    teko_mont_from(&mont, out, am);
    TEST_ASSERT_EQUAL_UINT32(42u, out[0]);
}

// Multi-limb Montgomery mul cross-checked against the independently-tested modexp path:
// a^2 and a^3 mod p computed via field ops must equal teko_bn_modexp(p, a, {2|3}).
void test_teko_bn_fieldops_montmul_vs_modexp(void) {
    static const uint8_t a_be[32] = {
        0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x00,0x0f,0x1e,0x2d,0x3c,0x4b,0x5a,0x69,0x78
    };
    const uint8_t two[1] = { 0x02 };
    const uint8_t three[1] = { 0x03 };
    TekoMont mont;
    uint32_t a[8], am[8], r2[8], r3[8], tmp[8];
    uint8_t expect2[32], expect3[32], got[32];

    TEST_ASSERT_EQUAL_INT(0, teko_mont_init(&mont, k_p256_prime, 32u));
    TEST_ASSERT_EQUAL_INT(8, mont.n);

    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(k_p256_prime, 32u, a_be, 32u, two, 1u, expect2));
    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(k_p256_prime, 32u, a_be, 32u, three, 1u, expect3));

    teko_bn_load_be(a, 8, a_be, 32u);
    teko_mont_to(&mont, am, a);

    teko_mont_mul(&mont, r2, am, am);             // a^2 (Montgomery)
    teko_mont_from(&mont, tmp, r2);
    teko_bn_store_be(got, 32u, tmp, 8);
    TEST_ASSERT_EQUAL_MEMORY(expect2, got, 32u);

    teko_mont_mul(&mont, r3, r2, am);             // a^3 (Montgomery)
    teko_mont_from(&mont, tmp, r3);
    teko_bn_store_be(got, 32u, tmp, 8);
    TEST_ASSERT_EQUAL_MEMORY(expect3, got, 32u);
}

// Multi-limb add/sub round-trips and the constant-time helpers.
void test_teko_bn_fieldops_addsub_and_select(void) {
    static const uint8_t a_be[32] = {
        0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10,
        0x0a,0x1b,0x2c,0x3d,0x4e,0x5f,0x60,0x71,0x82,0x93,0xa4,0xb5,0xc6,0xd7,0xe8,0xf9
    };
    static const uint8_t b_be[32] = {
        0x00,0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
        0xf0,0xe1,0xd2,0xc3,0xb4,0xa5,0x96,0x87,0x78,0x69,0x5a,0x4b,0x3c,0x2d,0x1e,0x0f
    };
    TekoMont mont;
    uint32_t a[8], b[8], s[8], d[8], z[8], sel[8];
    TEST_ASSERT_EQUAL_INT(0, teko_mont_init(&mont, k_p256_prime, 32u));
    teko_bn_load_be(a, 8, a_be, 32u);
    teko_bn_load_be(b, 8, b_be, 32u);

    // (a + b) - b == a.
    teko_mont_add(&mont, s, a, b);
    teko_mont_sub(&mont, d, s, b);
    TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(d, a, 8));

    // a - a == 0.
    teko_mont_sub(&mont, z, a, a);
    TEST_ASSERT_EQUAL_INT(1, teko_bn_is_zero(z, 8));
    TEST_ASSERT_EQUAL_INT(0, teko_bn_is_zero(a, 8));
    TEST_ASSERT_EQUAL_INT(0, teko_bn_eq(a, b, 8));

    // cselect: mask all-ones picks a, mask zero picks b.
    teko_bn_cselect(sel, a, b, 8, teko_bn_mask1(1u));
    TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(sel, a, 8));
    teko_bn_cselect(sel, a, b, 8, teko_bn_mask1(0u));
    TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(sel, b, 8));

    // cswap: swap iff mask set.
    {
        uint32_t x[8], y[8];
        memcpy(x, a, sizeof x);
        memcpy(y, b, sizeof y);
        teko_bn_cswap(x, y, 8, teko_bn_mask1(0u)); // no-op
        TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(x, a, 8));
        teko_bn_cswap(x, y, 8, teko_bn_mask1(1u)); // swap
        TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(x, b, 8));
        TEST_ASSERT_EQUAL_INT(1, teko_bn_eq(y, a, 8));
    }
}

// to-Montgomery / from-Montgomery identity is implicit, but check x^1 mod m == x for a
// multi-limb modulus and base (validates the Montgomery round-trip end to end).
void test_teko_bn_modexp_identity(void) {
    static const uint8_t mod[16] = {
        0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x01
    };
    static const uint8_t x[16] = {
        0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0,0x0f,0xed,0xcb,0xa9,0x87,0x65,0x43,0x21
    };
    const uint8_t one[1] = { 0x01 };
    uint8_t out[16];
    TEST_ASSERT_EQUAL_INT(0, teko_bn_modexp(mod, 16u, x, 16u, one, 1u, out));
    TEST_ASSERT_EQUAL_MEMORY(x, out, 16u); // x < mod, so x^1 mod mod == x
}
