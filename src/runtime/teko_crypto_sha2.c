// Native SHA-256 (FIPS 180-4). Portable C23, constant-time over the message
// content (no data-dependent branches/indexing). No external libraries.

#include "teko_crypto_sha2.h"

#include <string.h>

// First 32 bits of the fractional parts of the cube roots of the first 64 primes.
static const uint32_t TEKO_SHA256_K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static inline uint32_t teko_rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

void teko_sha256_init(TekoSha256Ctx* ctx) {
    // First 32 bits of the fractional parts of the square roots of the first 8 primes.
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_len = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void teko_sha256_compress(uint32_t state[8], const uint8_t block[TEKO_SHA256_BLOCK_LEN]) {
    uint32_t w[64];
    unsigned i;

    // Message schedule: first 16 words are big-endian from the block.
    for (i = 0u; i < 16u; ++i) {
        w[i] = ((uint32_t)block[i * 4u + 0u] << 24)
             | ((uint32_t)block[i * 4u + 1u] << 16)
             | ((uint32_t)block[i * 4u + 2u] << 8)
             | ((uint32_t)block[i * 4u + 3u]);
    }
    for (i = 16u; i < 64u; ++i) {
        uint32_t s0 = teko_rotr32(w[i - 15u], 7) ^ teko_rotr32(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        uint32_t s1 = teko_rotr32(w[i - 2u], 17) ^ teko_rotr32(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (i = 0u; i < 64u; ++i) {
        uint32_t s1 = teko_rotr32(e, 6) ^ teko_rotr32(e, 11) ^ teko_rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + TEKO_SHA256_K[i] + w[i];
        uint32_t s0 = teko_rotr32(a, 2) ^ teko_rotr32(a, 13) ^ teko_rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void teko_sha256_update(TekoSha256Ctx* ctx, const uint8_t* data, size_t len) {
    ctx->bit_len += (uint64_t)len * 8u;

    // Drain any staged partial block first.
    if (ctx->buffer_len != 0u) {
        size_t need = TEKO_SHA256_BLOCK_LEN - ctx->buffer_len;
        size_t take = (len < need) ? len : need;
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        len  -= take;
        if (ctx->buffer_len == TEKO_SHA256_BLOCK_LEN) {
            teko_sha256_compress(ctx->state, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    // Process full blocks directly from the input.
    while (len >= TEKO_SHA256_BLOCK_LEN) {
        teko_sha256_compress(ctx->state, data);
        data += TEKO_SHA256_BLOCK_LEN;
        len  -= TEKO_SHA256_BLOCK_LEN;
    }

    // Stage the remainder.
    if (len != 0u) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}

void teko_sha256_final(TekoSha256Ctx* ctx, uint8_t out[TEKO_SHA256_DIGEST_LEN]) {
    const uint64_t bit_len = ctx->bit_len;
    unsigned i;

    // Append the 0x80 terminator directly into the staging buffer.
    ctx->buffer[ctx->buffer_len++] = 0x80u;

    // If there isn't room for the 8-byte length, finish this block with zeros
    // and compress it, then continue padding a fresh block.
    if (ctx->buffer_len > 56u) {
        memset(ctx->buffer + ctx->buffer_len, 0, TEKO_SHA256_BLOCK_LEN - ctx->buffer_len);
        teko_sha256_compress(ctx->state, ctx->buffer);
        ctx->buffer_len = 0u;
    }

    // Zero-pad up to the length field at offset 56.
    memset(ctx->buffer + ctx->buffer_len, 0, 56u - ctx->buffer_len);

    // Append the original message length as a big-endian 64-bit integer.
    for (i = 0u; i < 8u; ++i) {
        ctx->buffer[56u + i] = (uint8_t)(bit_len >> (56u - 8u * i));
    }
    teko_sha256_compress(ctx->state, ctx->buffer);
    ctx->buffer_len = 0u;

    for (i = 0u; i < 8u; ++i) {
        out[i * 4u + 0u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)(ctx->state[i]);
    }
}

void teko_sha256(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA256_DIGEST_LEN]) {
    TekoSha256Ctx ctx;
    teko_sha256_init(&ctx);
    teko_sha256_update(&ctx, data, len);
    teko_sha256_final(&ctx, out);
}
