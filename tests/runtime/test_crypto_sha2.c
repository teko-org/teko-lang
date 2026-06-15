#include "unity.h"
#include "../../src/runtime/teko_crypto_sha2.h"

#include <string.h>
#include <stdlib.h>

// Compare a computed digest against an expected hex string (64 lowercase hex chars).
static void assert_sha256_hex(const uint8_t* data, size_t len, const char* expect_hex) {
    uint8_t digest[TEKO_SHA256_DIGEST_LEN];
    char got_hex[TEKO_SHA256_DIGEST_LEN * 2u + 1u];
    static const char* HEX = "0123456789abcdef";
    unsigned i;

    teko_sha256(data, len, digest);
    for (i = 0u; i < TEKO_SHA256_DIGEST_LEN; ++i) {
        got_hex[i * 2u + 0u] = HEX[(digest[i] >> 4) & 0xFu];
        got_hex[i * 2u + 1u] = HEX[digest[i] & 0xFu];
    }
    got_hex[TEKO_SHA256_DIGEST_LEN * 2u] = '\0';
    TEST_ASSERT_EQUAL_STRING(expect_hex, got_hex);
}

// FIPS 180-4 / NIST known-answer vectors + round-trip / streaming equivalence.
void test_teko_crypto_sha256_known_answer_vectors(void) {
    // Empty message (NIST): SHA-256("") .
    assert_sha256_hex((const uint8_t*)"", 0u,
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // "abc" — the canonical FIPS 180-4 single-block example.
    assert_sha256_hex((const uint8_t*)"abc", 3u,
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // 56-byte message — the FIPS two-block example (exercises the length-overflow pad path).
    assert_sha256_hex(
        (const uint8_t*)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56u,
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // 'a' x 1,000,000 — the long-message NIST vector (exercises many full blocks).
    {
        uint8_t* big = (uint8_t*)malloc(1000000u);
        TEST_ASSERT_NOT_NULL(big);
        memset(big, 'a', 1000000u);
        assert_sha256_hex(big, 1000000u,
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        free(big);
    }
}

// Streaming in arbitrary chunk sizes must equal the one-shot digest (boundary coverage:
// chunks that straddle the 64-byte block boundary and leave odd partial buffers).
void test_teko_crypto_sha256_streaming_matches_oneshot(void) {
    static const char* msg =
        "The quick brown fox jumps over the lazy dog. "
        "Pack my box with five dozen liquor jugs. "
        "Sphinx of black quartz, judge my vow.";
    size_t len = strlen(msg);
    uint8_t one_shot[TEKO_SHA256_DIGEST_LEN];
    size_t chunk;

    teko_sha256((const uint8_t*)msg, len, one_shot);

    // Re-hash feeding 1..17-byte chunks; every split must reproduce the one-shot digest.
    for (chunk = 1u; chunk <= 17u; ++chunk) {
        TekoSha256Ctx ctx;
        uint8_t streamed[TEKO_SHA256_DIGEST_LEN];
        size_t off;

        teko_sha256_init(&ctx);
        for (off = 0u; off < len; off += chunk) {
            size_t take = (len - off < chunk) ? (len - off) : chunk;
            teko_sha256_update(&ctx, (const uint8_t*)msg + off, take);
        }
        teko_sha256_final(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_SHA256_DIGEST_LEN);
    }
}
