#include "unity.h"
#include "../../src/runtime/teko_crypto_hmac.h"

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

static void assert_hmac256(const uint8_t* key, size_t kl,
                           const uint8_t* msg, size_t ml, const char* expect) {
    uint8_t mac[TEKO_SHA256_DIGEST_LEN];
    char got[TEKO_SHA256_DIGEST_LEN * 2u + 1u];
    teko_hmac_sha256(key, kl, msg, ml, mac);
    to_hex(mac, TEKO_SHA256_DIGEST_LEN, got);
    TEST_ASSERT_EQUAL_STRING(expect, got);
}

static void assert_hmac512(const uint8_t* key, size_t kl,
                           const uint8_t* msg, size_t ml, const char* expect) {
    uint8_t mac[TEKO_SHA512_DIGEST_LEN];
    char got[TEKO_SHA512_DIGEST_LEN * 2u + 1u];
    teko_hmac_sha512(key, kl, msg, ml, mac);
    to_hex(mac, TEKO_SHA512_DIGEST_LEN, got);
    TEST_ASSERT_EQUAL_STRING(expect, got);
}

static void assert_hmac384(const uint8_t* key, size_t kl,
                           const uint8_t* msg, size_t ml, const char* expect) {
    uint8_t mac[TEKO_SHA384_DIGEST_LEN];
    char got[TEKO_SHA384_DIGEST_LEN * 2u + 1u];
    teko_hmac_sha384(key, kl, msg, ml, mac);
    to_hex(mac, TEKO_SHA384_DIGEST_LEN, got);
    TEST_ASSERT_EQUAL_STRING(expect, got);
}

// RFC 4231 known-answer vectors for HMAC-SHA-256 and HMAC-SHA-512.
void test_teko_crypto_hmac_rfc4231_vectors(void) {
    uint8_t key[131];
    uint8_t data[152];

    // Case 1: key = 0x0b x 20, data = "Hi There".
    memset(key, 0x0b, 20u);
    assert_hmac256(key, 20u, (const uint8_t*)"Hi There", 8u,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    assert_hmac512(key, 20u, (const uint8_t*)"Hi There", 8u,
        "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
        "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
    assert_hmac384(key, 20u, (const uint8_t*)"Hi There", 8u,
        "afd03944d84895626b0825f4ab46907f15f9dadbe4101ec682aa034c7cebc59c"
        "faea9ea9076ede7f4af152e8b2fa9cb6");

    // Case 2: key = "Jefe", data = "what do ya want for nothing?".
    assert_hmac256((const uint8_t*)"Jefe", 4u,
        (const uint8_t*)"what do ya want for nothing?", 28u,
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    assert_hmac512((const uint8_t*)"Jefe", 4u,
        (const uint8_t*)"what do ya want for nothing?", 28u,
        "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea250554"
        "9758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737");
    assert_hmac384((const uint8_t*)"Jefe", 4u,
        (const uint8_t*)"what do ya want for nothing?", 28u,
        "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e"
        "8e2240ca5e69e2c78b3239ecfab21649");

    // Case 3: key = 0xaa x 20, data = 0xdd x 50.
    memset(key, 0xaa, 20u);
    memset(data, 0xdd, 50u);
    assert_hmac256(key, 20u, data, 50u,
        "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    assert_hmac512(key, 20u, data, 50u,
        "fa73b0089d56a284efb0f0756c890be9b1b5dbdd8ee81a3655f83e33b2279d39"
        "bf3e848279a722c806b485a47e67c807b946a337bee8942674278859e13292fb");

    // Case 4: key = 0x01..0x19 (25 bytes), data = 0xcd x 50.
    {
        unsigned i;
        for (i = 0u; i < 25u; ++i) { key[i] = (uint8_t)(i + 1u); }
    }
    memset(data, 0xcd, 50u);
    assert_hmac256(key, 25u, data, 50u,
        "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    assert_hmac512(key, 25u, data, 50u,
        "b0ba465637458c6990e5a8c5f61d4af7e576d97ff94b872de76f8050361ee3db"
        "a91ca5c11aa25eb4d679275cc5788063a5f19741120c4f2de2adebeb10a298dd");
    assert_hmac384(key, 25u, data, 50u,
        "3e8a69b7783c25851933ab6290af6ca77a9981480850009cc5577c6e1f573b4e"
        "6801dd23c4a7d679ccf8a386c674cffb");

    // Case 6: key = 0xaa x 131 (> block size, hashed first), short data.
    memset(key, 0xaa, 131u);
    assert_hmac256(key, 131u,
        (const uint8_t*)"Test Using Larger Than Block-Size Key - Hash Key First", 54u,
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    assert_hmac512(key, 131u,
        (const uint8_t*)"Test Using Larger Than Block-Size Key - Hash Key First", 54u,
        "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f352"
        "6b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598");

    // Case 7: key = 0xaa x 131, long data (> block size).
    {
        const char* msg7 =
            "This is a test using a larger than block-size key and a larger "
            "than block-size data. The key needs to be hashed before being "
            "used by the HMAC algorithm.";
        size_t ml = strlen(msg7);
        assert_hmac256(key, 131u, (const uint8_t*)msg7, ml,
            "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2");
        assert_hmac512(key, 131u, (const uint8_t*)msg7, ml,
            "e37b6a775dc87dbaa4dfa9f96e5e3ffddebd71f8867289865df5a32d20cdc944"
            "b6022cac3c4982b10d5eeb55c3e4de15134676fb6de0446065c97440fa8c6a58");
    }
}
