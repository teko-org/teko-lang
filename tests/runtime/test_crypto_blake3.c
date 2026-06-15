#include "unity.h"
#include "../../src/runtime/teko_crypto_blake3.h"

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

// The official BLAKE3 test vectors use input byte i = i % 251.
static void fill_pattern(uint8_t* buf, size_t len) {
    size_t i;
    for (i = 0u; i < len; ++i) {
        buf[i] = (uint8_t)(i % 251u);
    }
}

static void assert_blake3_pattern(size_t len, const char* expect32) {
    uint8_t* buf = (len > 0u) ? (uint8_t*)malloc(len) : NULL;
    uint8_t out[TEKO_BLAKE3_OUT_LEN];
    char hex[TEKO_BLAKE3_OUT_LEN * 2u + 1u];
    if (len > 0u) {
        TEST_ASSERT_NOT_NULL(buf);
        fill_pattern(buf, len);
    }
    teko_blake3(buf, len, out);
    to_hex(out, TEKO_BLAKE3_OUT_LEN, hex);
    TEST_ASSERT_EQUAL_STRING(expect32, hex);
    if (buf) free(buf);
}

// Official BLAKE3 test vectors (default hash, first 32 output bytes). The lengths span
// a single short block, a full block, a full chunk, and multi-chunk trees (exercising
// the parent-node CV stack and the trailing-zero-bit merge logic).
void test_teko_crypto_blake3_known_answer_vectors(void) {
    assert_blake3_pattern(0u,
        "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
    assert_blake3_pattern(1u,
        "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213");
    assert_blake3_pattern(3u,
        "e1be4d7a8ab5560aa4199eea339849ba8e293d55ca0a81006726d184519e647f");
    assert_blake3_pattern(64u,
        "4eed7141ea4a5cd4b788606bd23f46e212af9cacebacdc7d1f4c6dc7f2511b98");
    assert_blake3_pattern(1024u,
        "42214739f095a406f3fc83deb889744ac00df831c10daa55189b5d121c855af7");
    assert_blake3_pattern(2048u,
        "e776b6028c7cd22a4d0ba182a8bf62205d2ef576467e838ed6f2529b85fba24a");
    assert_blake3_pattern(3072u,
        "b98cb0ff3623be03326b373de6b9095218513e64f1ee2edd2525c7ad1e5cffd2");
}

// Chunked updates in odd sizes must equal the one-shot digest across chunk boundaries.
void test_teko_crypto_blake3_streaming_matches_oneshot(void) {
    size_t len = 5000u;
    uint8_t* buf = (uint8_t*)malloc(len);
    uint8_t one_shot[TEKO_BLAKE3_OUT_LEN];
    size_t chunk;
    TEST_ASSERT_NOT_NULL(buf);
    fill_pattern(buf, len);
    teko_blake3(buf, len, one_shot);

    for (chunk = 1u; chunk <= 401u; chunk += 100u) {
        TekoBlake3Ctx ctx;
        uint8_t streamed[TEKO_BLAKE3_OUT_LEN];
        size_t off;
        teko_blake3_init(&ctx);
        for (off = 0u; off < len; off += chunk) {
            size_t take = (len - off < chunk) ? (len - off) : chunk;
            teko_blake3_update(&ctx, buf + off, take);
        }
        teko_blake3_finalize(&ctx, streamed, TEKO_BLAKE3_OUT_LEN);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_BLAKE3_OUT_LEN);
    }
    free(buf);
}
