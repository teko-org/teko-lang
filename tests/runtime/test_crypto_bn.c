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
