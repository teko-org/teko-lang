// Native SHA-512 / SHA-384 (FIPS 180-4). Portable C23, constant-time over the
// message content (no data-dependent branches/indexing). No external libraries.

#include "teko_crypto_sha512.h"

#include <string.h>

// First 64 bits of the fractional parts of the cube roots of the first 80 primes.
static const uint64_t TEKO_SHA512_K[80] = {
    0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
    0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
    0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
    0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
    0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
    0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
    0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
    0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
    0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
    0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
    0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
    0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
    0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
    0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
    0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
    0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
    0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL
};

static inline uint64_t teko_rotr64(uint64_t x, unsigned n) {
    return (x >> n) | (x << (64u - n));
}

static void teko_sha512_compress(uint64_t state[8], const uint8_t block[TEKO_SHA512_BLOCK_LEN]) {
    uint64_t w[80];
    unsigned i;

    for (i = 0u; i < 16u; ++i) {
        w[i] = ((uint64_t)block[i * 8u + 0u] << 56)
             | ((uint64_t)block[i * 8u + 1u] << 48)
             | ((uint64_t)block[i * 8u + 2u] << 40)
             | ((uint64_t)block[i * 8u + 3u] << 32)
             | ((uint64_t)block[i * 8u + 4u] << 24)
             | ((uint64_t)block[i * 8u + 5u] << 16)
             | ((uint64_t)block[i * 8u + 6u] << 8)
             | ((uint64_t)block[i * 8u + 7u]);
    }
    for (i = 16u; i < 80u; ++i) {
        uint64_t s0 = teko_rotr64(w[i - 15u], 1) ^ teko_rotr64(w[i - 15u], 8) ^ (w[i - 15u] >> 7);
        uint64_t s1 = teko_rotr64(w[i - 2u], 19) ^ teko_rotr64(w[i - 2u], 61) ^ (w[i - 2u] >> 6);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint64_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint64_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (i = 0u; i < 80u; ++i) {
        uint64_t s1 = teko_rotr64(e, 14) ^ teko_rotr64(e, 18) ^ teko_rotr64(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = h + s1 + ch + TEKO_SHA512_K[i] + w[i];
        uint64_t s0 = teko_rotr64(a, 28) ^ teko_rotr64(a, 34) ^ teko_rotr64(a, 39);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = s0 + maj;

        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void teko_sha512_init(TekoSha512Ctx* ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->len_lo = 0u;
    ctx->len_hi = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void teko_sha384_init(TekoSha512Ctx* ctx) {
    ctx->state[0] = 0xcbbb9d5dc1059ed8ULL;
    ctx->state[1] = 0x629a292a367cd507ULL;
    ctx->state[2] = 0x9159015a3070dd17ULL;
    ctx->state[3] = 0x152fecd8f70e5939ULL;
    ctx->state[4] = 0x67332667ffc00b31ULL;
    ctx->state[5] = 0x8eb44a8768581511ULL;
    ctx->state[6] = 0xdb0c2e0d64f98fa7ULL;
    ctx->state[7] = 0x47b5481dbefa4fa4ULL;
    ctx->len_lo = 0u;
    ctx->len_hi = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

void teko_sha512_update(TekoSha512Ctx* ctx, const uint8_t* data, size_t len) {
    uint64_t add_bits = (uint64_t)len * 8u;
    ctx->len_lo += add_bits;
    if (ctx->len_lo < add_bits) {
        ctx->len_hi += 1u; // carry into the high 64 bits of the 128-bit length
    }

    if (ctx->buffer_len != 0u) {
        size_t need = TEKO_SHA512_BLOCK_LEN - ctx->buffer_len;
        size_t take = (len < need) ? len : need;
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        len  -= take;
        if (ctx->buffer_len == TEKO_SHA512_BLOCK_LEN) {
            teko_sha512_compress(ctx->state, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (len >= TEKO_SHA512_BLOCK_LEN) {
        teko_sha512_compress(ctx->state, data);
        data += TEKO_SHA512_BLOCK_LEN;
        len  -= TEKO_SHA512_BLOCK_LEN;
    }

    if (len != 0u) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}

// Shared finalization: pad and emit the full 8-word (64-byte) big-endian state.
static void teko_sha512_finish(TekoSha512Ctx* ctx, uint8_t out[TEKO_SHA512_DIGEST_LEN]) {
    const uint64_t len_lo = ctx->len_lo;
    const uint64_t len_hi = ctx->len_hi;
    unsigned i;

    ctx->buffer[ctx->buffer_len++] = 0x80u;

    // Need 16 bytes for the 128-bit length; if not enough room, flush this block.
    if (ctx->buffer_len > 112u) {
        memset(ctx->buffer + ctx->buffer_len, 0, TEKO_SHA512_BLOCK_LEN - ctx->buffer_len);
        teko_sha512_compress(ctx->state, ctx->buffer);
        ctx->buffer_len = 0u;
    }

    memset(ctx->buffer + ctx->buffer_len, 0, 112u - ctx->buffer_len);

    for (i = 0u; i < 8u; ++i) {
        ctx->buffer[112u + i] = (uint8_t)(len_hi >> (56u - 8u * i));
        ctx->buffer[120u + i] = (uint8_t)(len_lo >> (56u - 8u * i));
    }
    teko_sha512_compress(ctx->state, ctx->buffer);
    ctx->buffer_len = 0u;

    for (i = 0u; i < 8u; ++i) {
        out[i * 8u + 0u] = (uint8_t)(ctx->state[i] >> 56);
        out[i * 8u + 1u] = (uint8_t)(ctx->state[i] >> 48);
        out[i * 8u + 2u] = (uint8_t)(ctx->state[i] >> 40);
        out[i * 8u + 3u] = (uint8_t)(ctx->state[i] >> 32);
        out[i * 8u + 4u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 8u + 5u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 8u + 6u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 8u + 7u] = (uint8_t)(ctx->state[i]);
    }
}

void teko_sha512_final(TekoSha512Ctx* ctx, uint8_t out[TEKO_SHA512_DIGEST_LEN]) {
    teko_sha512_finish(ctx, out);
}

void teko_sha384_final(TekoSha512Ctx* ctx, uint8_t out[TEKO_SHA384_DIGEST_LEN]) {
    uint8_t full[TEKO_SHA512_DIGEST_LEN];
    teko_sha512_finish(ctx, full);
    memcpy(out, full, TEKO_SHA384_DIGEST_LEN); // SHA-384 = leftmost 48 bytes
}

void teko_sha512(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA512_DIGEST_LEN]) {
    TekoSha512Ctx ctx;
    teko_sha512_init(&ctx);
    teko_sha512_update(&ctx, data, len);
    teko_sha512_final(&ctx, out);
}

void teko_sha384(const uint8_t* data, size_t len, uint8_t out[TEKO_SHA384_DIGEST_LEN]) {
    TekoSha512Ctx ctx;
    teko_sha384_init(&ctx);
    teko_sha512_update(&ctx, data, len);
    teko_sha384_final(&ctx, out);
}
