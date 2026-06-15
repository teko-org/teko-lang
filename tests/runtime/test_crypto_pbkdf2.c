#include "unity.h"
#include "../../src/runtime/teko_crypto_pbkdf2.h"

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

// RFC 7914 (scrypt) §11 known-answer vectors for PBKDF2-HMAC-SHA-256.
void test_teko_crypto_pbkdf2_sha256_rfc7914_vectors(void) {
    uint8_t dk[64];
    char hex[129];

    // PBKDF2-HMAC-SHA256("passwd", "salt", 1, 64).
    TEST_ASSERT_EQUAL_INT(0, teko_pbkdf2_hmac_sha256(
        (const uint8_t*)"passwd", 6u, (const uint8_t*)"salt", 4u, 1u, dk, 64u));
    to_hex(dk, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"
        "49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783", hex);

    // PBKDF2-HMAC-SHA256("Password", "NaCl", 80000, 64).
    TEST_ASSERT_EQUAL_INT(0, teko_pbkdf2_hmac_sha256(
        (const uint8_t*)"Password", 8u, (const uint8_t*)"NaCl", 4u, 80000u, dk, 64u));
    to_hex(dk, 64u, hex);
    TEST_ASSERT_EQUAL_STRING(
        "4ddcd8f60b98be21830cee5ef22701f9641a4418d04c0414aeff08876b34ab56"
        "a1d425a1225833549adb841b51c9b3176a272bdebba1d078478f62b397f33c8d", hex);

    // Bad arg: zero iterations is rejected.
    TEST_ASSERT_EQUAL_INT(-1, teko_pbkdf2_hmac_sha256(
        (const uint8_t*)"x", 1u, (const uint8_t*)"y", 1u, 0u, dk, 32u));
}

// PBKDF2-HMAC-SHA512 has no widely-canonical short vector here; verify the structural
// invariant over the shared generic code (deterministic; a short request is the prefix of
// a longer one — i.e. T-block concatenation is consistent).
void test_teko_crypto_pbkdf2_sha512_structural(void) {
    uint8_t a[100];
    uint8_t b[100];
    uint8_t prefix[64];

    TEST_ASSERT_EQUAL_INT(0, teko_pbkdf2_hmac_sha512(
        (const uint8_t*)"password", 8u, (const uint8_t*)"saltsalt", 8u, 100u, a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(0, teko_pbkdf2_hmac_sha512(
        (const uint8_t*)"password", 8u, (const uint8_t*)"saltsalt", 8u, 100u, b, sizeof(b)));
    TEST_ASSERT_EQUAL_MEMORY(a, b, sizeof(a)); // deterministic

    TEST_ASSERT_EQUAL_INT(0, teko_pbkdf2_hmac_sha512(
        (const uint8_t*)"password", 8u, (const uint8_t*)"saltsalt", 8u, 100u, prefix, sizeof(prefix)));
    TEST_ASSERT_EQUAL_MEMORY(a, prefix, sizeof(prefix));
}
