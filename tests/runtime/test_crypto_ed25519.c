#include "unity.h"
#include "../../src/runtime/teko_crypto_ed25519.h"

#include <string.h>

static void from_hex(uint8_t* out, const char* hex, size_t n) {
    size_t i;
    for (i = 0u; i < n; ++i) {
        unsigned hi = (unsigned char)hex[i * 2u], lo = (unsigned char)hex[i * 2u + 1u];
        hi = (hi <= '9') ? (hi - '0') : ((hi | 0x20u) - 'a' + 10u);
        lo = (lo <= '9') ? (lo - '0') : ((lo | 0x20u) - 'a' + 10u);
        out[i] = (uint8_t)((hi << 4) | lo);
    }
}

// RFC 8032 §7.1 known-answer vectors.
void test_teko_crypto_ed25519_rfc8032_test1(void) {
    uint8_t pub1[32], sig1[64];
    uint8_t seed3[32], pub3_expect[32], sig3_expect[64], msg3[2];
    uint8_t pub3[32], sig3[64];

    // Test 1 (empty message): the signature verifies under the published key
    // (authoritative and independent of the secret seed).
    from_hex(pub1, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", 32u);
    from_hex(sig1,
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8"
        "821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b", 64u);
    TEST_ASSERT_EQUAL_INT(0, teko_ed25519_verify(sig1, NULL, 0u, pub1));

    // Test 3 (message 0xaf82): full derive + deterministic sign must reproduce the RFC
    // public key and signature exactly, then verify.
    from_hex(seed3, "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7", 32u);
    from_hex(pub3_expect, "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", 32u);
    from_hex(sig3_expect,
        "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff"
        "9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a", 64u);
    msg3[0] = 0xaf; msg3[1] = 0x82;

    teko_ed25519_pubkey(pub3, seed3);
    TEST_ASSERT_EQUAL_MEMORY(pub3_expect, pub3, 32u);

    teko_ed25519_sign(sig3, msg3, 2u, seed3, pub3);
    TEST_ASSERT_EQUAL_MEMORY(sig3_expect, sig3, 64u);

    TEST_ASSERT_EQUAL_INT(0, teko_ed25519_verify(sig3_expect, msg3, 2u, pub3_expect));
}

// Sign/verify round-trip on a non-empty message + tamper rejection.
void test_teko_crypto_ed25519_sign_verify_roundtrip(void) {
    uint8_t seed[32], pub[32], sig[64];
    const uint8_t msg[11] = { 'h', 'e', 'l', 'l', 'o', ' ', 't', 'e', 'k', 'o', '!' };
    unsigned i;
    for (i = 0u; i < 32u; ++i) seed[i] = (uint8_t)(i * 7u + 3u);

    teko_ed25519_pubkey(pub, seed);
    teko_ed25519_sign(sig, msg, sizeof(msg), seed, pub);
    TEST_ASSERT_EQUAL_INT(0, teko_ed25519_verify(sig, msg, sizeof(msg), pub));

    // Flip a message byte -> reject.
    {
        uint8_t bad[11];
        memcpy(bad, msg, sizeof(msg));
        bad[4] ^= 0x20u;
        TEST_ASSERT_EQUAL_INT(-1, teko_ed25519_verify(sig, bad, sizeof(bad), pub));
    }
    // Flip a signature byte -> reject.
    {
        uint8_t badsig[64];
        memcpy(badsig, sig, sizeof(sig));
        badsig[10] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(-1, teko_ed25519_verify(badsig, msg, sizeof(msg), pub));
    }
    // Wrong public key -> reject.
    {
        uint8_t badpub[32];
        memcpy(badpub, pub, sizeof(pub));
        badpub[0] ^= 0x01u;
        TEST_ASSERT_EQUAL_INT(-1, teko_ed25519_verify(sig, msg, sizeof(msg), badpub));
    }
}
