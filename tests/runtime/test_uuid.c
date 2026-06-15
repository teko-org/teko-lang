#include "unity.h"
#include "../../src/runtime/teko_uuid.h"

#include <string.h>

static void assert_format(const uint8_t u[16], const char* expect) {
    char s[37];
    teko_uuid_format(s, u);
    s[36] = '\0';
    TEST_ASSERT_EQUAL_STRING(expect, s);
}

// RFC 4122 / RFC 9562 known-answer vectors for the deterministic forms.
void test_teko_uuid_known_answer_vectors(void) {
    uint8_t u[16];

    // nil UUID.
    teko_uuid_nil(u);
    assert_format(u, "00000000-0000-0000-0000-000000000000");

    // v3 (MD5) and v5 (SHA-1) of "python.org" under the DNS namespace (canonical examples).
    teko_uuid_v3(u, TEKO_UUID_NS_DNS, (const uint8_t*)"python.org", 10u);
    assert_format(u, "6fa459ea-ee8a-3ca4-894e-db77e160355e");
    TEST_ASSERT_EQUAL_INT(3, teko_uuid_version(u));
    TEST_ASSERT_EQUAL_INT(1, teko_uuid_is_rfc4122_variant(u));

    teko_uuid_v5(u, TEKO_UUID_NS_DNS, (const uint8_t*)"python.org", 10u);
    assert_format(u, "886313e1-3b8a-5372-9b90-0c9aee199e5d");
    TEST_ASSERT_EQUAL_INT(5, teko_uuid_version(u));
    TEST_ASSERT_EQUAL_INT(1, teko_uuid_is_rfc4122_variant(u));
}

// Canonical parse/format round-trip + malformed-input rejection.
void test_teko_uuid_parse_format(void) {
    uint8_t u[16], u2[16];
    const char* canon = "886313e1-3b8a-5372-9b90-0c9aee199e5d";
    char s[37];

    TEST_ASSERT_EQUAL_INT(0, teko_uuid_parse(u, canon));
    teko_uuid_format(s, u);
    s[36] = '\0';
    TEST_ASSERT_EQUAL_STRING(canon, s);

    // Uppercase parses to the same bytes (case-insensitive).
    TEST_ASSERT_EQUAL_INT(0, teko_uuid_parse(u2, "886313E1-3B8A-5372-9B90-0C9AEE199E5D"));
    TEST_ASSERT_EQUAL_MEMORY(u, u2, 16u);

    // Malformed inputs are rejected.
    TEST_ASSERT_EQUAL_INT(-1, teko_uuid_parse(u, "886313e1-3b8a-5372-9b90-0c9aee199e5"));   // too short
    TEST_ASSERT_EQUAL_INT(-1, teko_uuid_parse(u, "886313e1x3b8a-5372-9b90-0c9aee199e5d"));  // bad dash
    TEST_ASSERT_EQUAL_INT(-1, teko_uuid_parse(u, "886313e1-3b8a-5372-9b90-0c9aee199e5dZZ")); // trailing junk
    TEST_ASSERT_EQUAL_INT(-1, teko_uuid_parse(u, "g86313e1-3b8a-5372-9b90-0c9aee199e5d"));  // non-hex
}

// v4: random — structure (version/variant), distinctness.
void test_teko_uuid_v4_structure(void) {
    uint8_t a[16], b[16];
    TEST_ASSERT_EQUAL_INT(0, teko_uuid_v4(a));
    TEST_ASSERT_EQUAL_INT(0, teko_uuid_v4(b));
    TEST_ASSERT_EQUAL_INT(4, teko_uuid_version(a));
    TEST_ASSERT_EQUAL_INT(1, teko_uuid_is_rfc4122_variant(a));
    TEST_ASSERT_EQUAL_INT(4, teko_uuid_version(b));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, 16u));
}

// v8: custom — version 8 + variant stamped; all other (non version/variant) bytes preserved.
void test_teko_uuid_v8_structure(void) {
    uint8_t data[16], u[16];
    unsigned i;
    for (i = 0u; i < 16u; ++i) data[i] = (uint8_t)(0x10u + i);

    teko_uuid_v8(u, data);
    TEST_ASSERT_EQUAL_INT(8, teko_uuid_version(u));
    TEST_ASSERT_EQUAL_INT(1, teko_uuid_is_rfc4122_variant(u));
    // Bytes 6 and 8 carry version/variant; every other byte is the supplied custom data.
    for (i = 0u; i < 16u; ++i) {
        if (i == 6u || i == 8u) continue;
        TEST_ASSERT_EQUAL_UINT8(data[i], u[i]);
    }
    // The low nibble of byte 6 and low 6 bits of byte 8 are preserved from the custom data.
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((data[6] & 0x0fu) | 0x80u), u[6]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)((data[8] & 0x3fu) | 0x80u), u[8]);
}

// v7: time-ordered — the 48-bit big-endian timestamp prefix is preserved; version/variant set.
void test_teko_uuid_v7_structure(void) {
    uint8_t a[16], b[16];
    uint64_t ts = 0x0123456789abULL; // 48-bit ms timestamp
    unsigned i;

    TEST_ASSERT_EQUAL_INT(0, teko_uuid_v7(a, ts));
    TEST_ASSERT_EQUAL_INT(7, teko_uuid_version(a));
    TEST_ASSERT_EQUAL_INT(1, teko_uuid_is_rfc4122_variant(a));
    for (i = 0u; i < 6u; ++i) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)(ts >> (40u - 8u * i)), a[i]);
    }

    // Same timestamp, different random tail -> distinct UUIDs.
    TEST_ASSERT_EQUAL_INT(0, teko_uuid_v7(b, ts));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, 16u));
}
