#include "unity.h"
#include "../../src/runtime/teko_crypto_scrypt.h"

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

// RFC 7914 §12 known-answer vectors for scrypt.
void test_teko_crypto_scrypt_rfc7914_vectors(void) {
    uint8_t dk[64];
    char hex[129];

    // scrypt("", "", N=16, r=1, p=1, dkLen=64).
    TEST_ASSERT_EQUAL_INT(0, teko_scrypt((const uint8_t*)"", 0u, (const uint8_t*)"", 0u,
                                         16u, 1u, 1u, dk, 64u));
    to_hex(dk, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "77d6576238657b203b19ca42c18a0497f16b4844e3074ae8dfdffa3fede21442"
        "fcd0069ded0948f8326a753a0fc81f17e8d3e0fb2e0d3628cf35e20c38d18906", hex);

    // scrypt("password", "NaCl", N=1024, r=8, p=16, dkLen=64).
    TEST_ASSERT_EQUAL_INT(0, teko_scrypt((const uint8_t*)"password", 8u,
                                         (const uint8_t*)"NaCl", 4u, 1024u, 8u, 16u, dk, 64u));
    to_hex(dk, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162"
        "2eaf30d92e22a3886ff109279d9830dac727afb94a83ee6d8360cbdfa2cc0640", hex);

    // scrypt("pleaseletmein", "SodiumChloride", N=16384, r=8, p=1, dkLen=64).
    TEST_ASSERT_EQUAL_INT(0, teko_scrypt((const uint8_t*)"pleaseletmein", 13u,
                                         (const uint8_t*)"SodiumChloride", 14u,
                                         16384u, 8u, 1u, dk, 64u));
    to_hex(dk, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "7023bdcb3afd7348461c06cd81fd38ebfda8fbba904f8e3ea9b543f6545da1f2"
        "d5432955613f0fcf62d49705242a9af9e61e85dc0d651e40dfcf017b45575887", hex);

    // Invalid N (not a power of two) is rejected.
    TEST_ASSERT_EQUAL_INT(-1, teko_scrypt((const uint8_t*)"x", 1u, (const uint8_t*)"y", 1u,
                                          1000u, 1u, 1u, dk, 64u));
}
