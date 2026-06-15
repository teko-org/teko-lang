#include "unity.h"
#include "../../src/runtime/teko_crypto_md5.h"

#include <string.h>

static void assert_md5(const char* msg, const char* expect_hex) {
    uint8_t digest[TEKO_MD5_DIGEST_LEN];
    char got[TEKO_MD5_DIGEST_LEN * 2u + 1u];
    static const char* HEX = "0123456789abcdef";
    unsigned i;
    teko_md5((const uint8_t*)msg, strlen(msg), digest);
    for (i = 0u; i < TEKO_MD5_DIGEST_LEN; ++i) {
        got[i * 2u + 0u] = HEX[(digest[i] >> 4) & 0xFu];
        got[i * 2u + 1u] = HEX[digest[i] & 0xFu];
    }
    got[TEKO_MD5_DIGEST_LEN * 2u] = '\0';
    TEST_ASSERT_EQUAL_STRING(expect_hex, got);
}

// RFC 1321 Appendix A.5 known-answer vectors. (MD5 is legacy/insecure — interop only.)
void test_teko_crypto_md5_rfc1321_vectors(void) {
    assert_md5("", "d41d8cd98f00b204e9800998ecf8427e");
    assert_md5("a", "0cc175b9c0f1b6a831c399e269772661");
    assert_md5("abc", "900150983cd24fb0d6963f7d28e17f72");
    assert_md5("message digest", "f96b697d7cb7938d525a2f31aaf161d0");
    assert_md5("abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b");
    assert_md5("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
               "d174ab98d277d9f5a5611c2c9f419d9f");
    assert_md5("12345678901234567890123456789012345678901234567890123456789012345678901234567890",
               "57edf4a22be3c955ac49da2e2107b67a");
}

// Multi-block + streaming-vs-one-shot equivalence (spans the 64-byte block boundary).
void test_teko_crypto_md5_streaming_matches_oneshot(void) {
    uint8_t msg[200];
    uint8_t one_shot[TEKO_MD5_DIGEST_LEN];
    size_t i, chunk;
    for (i = 0u; i < sizeof(msg); ++i) msg[i] = (uint8_t)(i * 5u + 2u);
    teko_md5(msg, sizeof(msg), one_shot);

    for (chunk = 1u; chunk <= 17u; ++chunk) {
        TekoMd5Ctx ctx;
        uint8_t streamed[TEKO_MD5_DIGEST_LEN];
        size_t off;
        teko_md5_init(&ctx);
        for (off = 0u; off < sizeof(msg); off += chunk) {
            size_t take = (sizeof(msg) - off < chunk) ? (sizeof(msg) - off) : chunk;
            teko_md5_update(&ctx, msg + off, take);
        }
        teko_md5_final(&ctx, streamed);
        TEST_ASSERT_EQUAL_MEMORY(one_shot, streamed, TEKO_MD5_DIGEST_LEN);
    }
}
