#include "unity.h"
#include "../../src/runtime/teko_crypto_poly1305.h"

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

// RFC 8439 §2.5.2 — the worked Poly1305 example.
void test_teko_crypto_poly1305_rfc8439_vector(void) {
    static const uint8_t key[32] = {
        0x85, 0xd6, 0xbe, 0x78, 0x57, 0x55, 0x6d, 0x33,
        0x7f, 0x44, 0x52, 0xfe, 0x42, 0xd5, 0x06, 0xa8,
        0x01, 0x03, 0x80, 0x8a, 0xfb, 0x0d, 0xb2, 0xfd,
        0x4a, 0xbf, 0xf6, 0xaf, 0x41, 0x49, 0xf5, 0x1b
    };
    const char* msg = "Cryptographic Forum Research Group"; // 34 bytes
    uint8_t tag[16];
    char hex[33];

    teko_poly1305((const uint8_t*)msg, strlen(msg), key, tag);
    to_hex(tag, 16u, hex);
    TEST_ASSERT_EQUAL_STRING("a8061dc1305136c6c22b8baf0c0127a9", hex);
}

// Streaming in odd chunk sizes must equal the one-shot tag (boundary coverage).
void test_teko_crypto_poly1305_streaming_matches_oneshot(void) {
    static const uint8_t key[32] = {
        0x85, 0xd6, 0xbe, 0x78, 0x57, 0x55, 0x6d, 0x33,
        0x7f, 0x44, 0x52, 0xfe, 0x42, 0xd5, 0x06, 0xa8,
        0x01, 0x03, 0x80, 0x8a, 0xfb, 0x0d, 0xb2, 0xfd,
        0x4a, 0xbf, 0xf6, 0xaf, 0x41, 0x49, 0xf5, 0x1b
    };
    uint8_t msg[200];
    uint8_t one_shot[16];
    size_t chunk, i;

    for (i = 0u; i < sizeof(msg); ++i) msg[i] = (uint8_t)(i * 7u + 1u);
    teko_poly1305(msg, sizeof(msg), key, one_shot);

    for (chunk = 1u; chunk <= 19u; ++chunk) {
        TekoPoly1305Ctx ctx;
        uint8_t streamed[16];
        size_t off;
        teko_poly1305_init(&ctx, key);
        for (off = 0u; off < sizeof(msg); off += chunk) {
            size_t take = (sizeof(msg) - off < chunk) ? (sizeof(msg) - off) : chunk;
            teko_poly1305_update(&ctx, msg + off, take);
        }
        teko_poly1305_finish(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, 16u);
    }
}
