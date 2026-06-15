#include "unity.h"
#include "../../src/runtime/teko_crypto_sha512.h"

#include <string.h>
#include <stdlib.h>

static void to_hex(const uint8_t* d, size_t n, char* out) {
    static const char* HEX = "0123456789abcdef";
    size_t i;
    for (i = 0u; i < n; ++i) {
        out[i * 2u + 0u] = HEX[(d[i] >> 4) & 0xFu];
        out[i * 2u + 1u] = HEX[d[i] & 0xFu];
    }
    out[n * 2u] = '\0';
}

static void assert_sha512_hex(const uint8_t* data, size_t len, const char* expect_hex) {
    uint8_t digest[TEKO_SHA512_DIGEST_LEN];
    char got[TEKO_SHA512_DIGEST_LEN * 2u + 1u];
    teko_sha512(data, len, digest);
    to_hex(digest, TEKO_SHA512_DIGEST_LEN, got);
    TEST_ASSERT_EQUAL_STRING(expect_hex, got);
}

static void assert_sha384_hex(const uint8_t* data, size_t len, const char* expect_hex) {
    uint8_t digest[TEKO_SHA384_DIGEST_LEN];
    char got[TEKO_SHA384_DIGEST_LEN * 2u + 1u];
    teko_sha384(data, len, digest);
    to_hex(digest, TEKO_SHA384_DIGEST_LEN, got);
    TEST_ASSERT_EQUAL_STRING(expect_hex, got);
}

// FIPS 180-4 known-answer vectors for SHA-512.
void test_teko_crypto_sha512_known_answer_vectors(void) {
    assert_sha512_hex((const uint8_t*)"", 0u,
        "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
        "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");

    assert_sha512_hex((const uint8_t*)"abc", 3u,
        "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
        "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    // 112-byte two-block message (exercises the length-overflow pad path).
    assert_sha512_hex(
        (const uint8_t*)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                        "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112u,
        "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018"
        "501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909");

    // 'a' x 1,000,000 long-message NIST vector.
    {
        uint8_t* big = (uint8_t*)malloc(1000000u);
        TEST_ASSERT_NOT_NULL(big);
        memset(big, 'a', 1000000u);
        assert_sha512_hex(big, 1000000u,
            "e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973eb"
            "de0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b");
        free(big);
    }
}

// FIPS 180-4 known-answer vectors for SHA-384 (same core, truncated output).
void test_teko_crypto_sha384_known_answer_vectors(void) {
    assert_sha384_hex((const uint8_t*)"", 0u,
        "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
        "274edebfe76f65fbd51ad2f14898b95b");

    assert_sha384_hex((const uint8_t*)"abc", 3u,
        "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
        "8086072ba1e7cc2358baeca134c825a7");
}

// Streaming in odd chunk sizes must equal the one-shot SHA-512 digest.
void test_teko_crypto_sha512_streaming_matches_oneshot(void) {
    static const char* msg =
        "The quick brown fox jumps over the lazy dog. "
        "Now is the time for all good men to come to the aid of their country.";
    size_t len = strlen(msg);
    uint8_t one_shot[TEKO_SHA512_DIGEST_LEN];
    size_t chunk;

    teko_sha512((const uint8_t*)msg, len, one_shot);

    for (chunk = 1u; chunk <= 33u; ++chunk) {
        TekoSha512Ctx ctx;
        uint8_t streamed[TEKO_SHA512_DIGEST_LEN];
        size_t off;
        teko_sha512_init(&ctx);
        for (off = 0u; off < len; off += chunk) {
            size_t take = (len - off < chunk) ? (len - off) : chunk;
            teko_sha512_update(&ctx, (const uint8_t*)msg + off, take);
        }
        teko_sha512_final(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_SHA512_DIGEST_LEN);
    }
}
