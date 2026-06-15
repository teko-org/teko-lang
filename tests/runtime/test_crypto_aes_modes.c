#include "unity.h"
#include "../../src/runtime/teko_crypto_aes_modes.h"

#include <string.h>

static void to_hex(const uint8_t* d, size_t n, char* out) {
    static const char* HEX = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < n; ++i) {
        out[i * 2u + 0u] = HEX[(d[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = HEX[d[i] & 0xFu];
    }
    out[n * 2u] = '\0';
}

// The shared NIST SP 800-38A plaintext (4 blocks) and AES-128 key.
static const uint8_t NIST_KEY128[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
};
static const uint8_t NIST_PT[64] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
};

// NIST SP 800-38A F.5.1 — CTR-AES128.
void test_teko_crypto_aes_ctr_nist_vector(void) {
    static const uint8_t iv[16] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };
    TekoAesKey k;
    uint8_t ct[64];
    uint8_t back[64];
    char hex[129];

    TEST_ASSERT_EQUAL_INT(0, teko_aes_init(&k, NIST_KEY128, 16u));

    teko_aes_ctr_xor(&k, iv, NIST_PT, ct, 64u);
    to_hex(ct, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff"
        "5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee", hex);

    // CTR is symmetric: re-running over the ciphertext restores the plaintext.
    teko_aes_ctr_xor(&k, iv, ct, back, 64u);
    TEST_ASSERT_EQUAL_MEMORY(NIST_PT, back, 64u);

    // A partial (non-block-multiple) length still round-trips.
    {
        uint8_t pct[37], pbk[37];
        teko_aes_ctr_xor(&k, iv, NIST_PT, pct, 37u);
        teko_aes_ctr_xor(&k, iv, pct, pbk, 37u);
        TEST_ASSERT_EQUAL_MEMORY(NIST_PT, pbk, 37u);
    }
}

// NIST SP 800-38A F.2.1 — CBC-AES128.
void test_teko_crypto_aes_cbc_nist_vector(void) {
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    TekoAesKey k;
    uint8_t ct[64];
    uint8_t back[64];
    char hex[129];

    TEST_ASSERT_EQUAL_INT(0, teko_aes_init(&k, NIST_KEY128, 16u));

    TEST_ASSERT_EQUAL_INT(0, teko_aes_cbc_encrypt(&k, iv, NIST_PT, ct, 64u));
    to_hex(ct, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
        "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7", hex);

    TEST_ASSERT_EQUAL_INT(0, teko_aes_cbc_decrypt(&k, iv, ct, back, 64u));
    TEST_ASSERT_EQUAL_MEMORY(NIST_PT, back, 64u);

    // Non-block-multiple length is rejected.
    TEST_ASSERT_EQUAL_INT(-1, teko_aes_cbc_encrypt(&k, iv, NIST_PT, ct, 30u));
}
