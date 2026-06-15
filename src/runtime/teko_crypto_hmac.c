// Native HMAC (RFC 2104 / FIPS 198-1) over SHA-256 and SHA-512. Portable C23.
//
//   K0    = key,            if len(key) <= block, zero-padded to block length
//         = H(key),         if len(key) >  block, then zero-padded to block length
//   HMAC  = H( (K0 ^ opad) || H( (K0 ^ ipad) || msg ) )

#include "teko_crypto_hmac.h"

#include <string.h>

void teko_hmac_sha256(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA256_DIGEST_LEN]) {
    uint8_t k0[TEKO_SHA256_BLOCK_LEN];
    uint8_t ipad[TEKO_SHA256_BLOCK_LEN];
    uint8_t opad[TEKO_SHA256_BLOCK_LEN];
    uint8_t inner[TEKO_SHA256_DIGEST_LEN];
    TekoSha256Ctx ctx;
    unsigned i;

    memset(k0, 0, sizeof(k0));
    if (key_len > TEKO_SHA256_BLOCK_LEN) {
        teko_sha256(key, key_len, k0); // K0 = H(key), tail stays zero-padded
    } else {
        memcpy(k0, key, key_len);
    }

    for (i = 0u; i < TEKO_SHA256_BLOCK_LEN; ++i) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    // inner = H( (K0 ^ ipad) || msg )
    teko_sha256_init(&ctx);
    teko_sha256_update(&ctx, ipad, TEKO_SHA256_BLOCK_LEN);
    teko_sha256_update(&ctx, msg, msg_len);
    teko_sha256_final(&ctx, inner);

    // out = H( (K0 ^ opad) || inner )
    teko_sha256_init(&ctx);
    teko_sha256_update(&ctx, opad, TEKO_SHA256_BLOCK_LEN);
    teko_sha256_update(&ctx, inner, TEKO_SHA256_DIGEST_LEN);
    teko_sha256_final(&ctx, out);
}

void teko_hmac_sha512(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA512_DIGEST_LEN]) {
    uint8_t k0[TEKO_SHA512_BLOCK_LEN];
    uint8_t ipad[TEKO_SHA512_BLOCK_LEN];
    uint8_t opad[TEKO_SHA512_BLOCK_LEN];
    uint8_t inner[TEKO_SHA512_DIGEST_LEN];
    TekoSha512Ctx ctx;
    unsigned i;

    memset(k0, 0, sizeof(k0));
    if (key_len > TEKO_SHA512_BLOCK_LEN) {
        teko_sha512(key, key_len, k0);
    } else {
        memcpy(k0, key, key_len);
    }

    for (i = 0u; i < TEKO_SHA512_BLOCK_LEN; ++i) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    teko_sha512_init(&ctx);
    teko_sha512_update(&ctx, ipad, TEKO_SHA512_BLOCK_LEN);
    teko_sha512_update(&ctx, msg, msg_len);
    teko_sha512_final(&ctx, inner);

    teko_sha512_init(&ctx);
    teko_sha512_update(&ctx, opad, TEKO_SHA512_BLOCK_LEN);
    teko_sha512_update(&ctx, inner, TEKO_SHA512_DIGEST_LEN);
    teko_sha512_final(&ctx, out);
}

void teko_hmac_sha384(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[TEKO_SHA384_DIGEST_LEN]) {
    uint8_t k0[TEKO_SHA512_BLOCK_LEN];   // SHA-384 shares SHA-512's 128-byte block
    uint8_t ipad[TEKO_SHA512_BLOCK_LEN];
    uint8_t opad[TEKO_SHA512_BLOCK_LEN];
    uint8_t inner[TEKO_SHA384_DIGEST_LEN];
    TekoSha512Ctx ctx;
    unsigned i;

    memset(k0, 0, sizeof(k0));
    if (key_len > TEKO_SHA512_BLOCK_LEN) {
        teko_sha384(key, key_len, k0); // 48-byte digest, tail stays zero-padded
    } else {
        memcpy(k0, key, key_len);
    }

    for (i = 0u; i < TEKO_SHA512_BLOCK_LEN; ++i) {
        ipad[i] = (uint8_t)(k0[i] ^ 0x36u);
        opad[i] = (uint8_t)(k0[i] ^ 0x5cu);
    }

    teko_sha384_init(&ctx);
    teko_sha512_update(&ctx, ipad, TEKO_SHA512_BLOCK_LEN);
    teko_sha512_update(&ctx, msg, msg_len);
    teko_sha384_final(&ctx, inner);

    teko_sha384_init(&ctx);
    teko_sha512_update(&ctx, opad, TEKO_SHA512_BLOCK_LEN);
    teko_sha512_update(&ctx, inner, TEKO_SHA384_DIGEST_LEN);
    teko_sha384_final(&ctx, out);
}
