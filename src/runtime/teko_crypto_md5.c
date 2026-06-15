// Native MD5 (RFC 1321). Portable C23. LEGACY/INSECURE — see the header.

#include "teko_crypto_md5.h"

#include <string.h>

static const uint32_t TEKO_MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const uint8_t TEKO_MD5_S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static inline uint32_t teko_md5_rotl(uint32_t x, unsigned c) {
    return (x << c) | (x >> (32u - c));
}

void teko_md5_init(TekoMd5Ctx* ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->bit_len = 0u;
    ctx->buffer_len = 0u;
    memset(ctx->buffer, 0, sizeof(ctx->buffer));
}

static void teko_md5_compress(uint32_t state[4], const uint8_t block[64]) {
    uint32_t m[16];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    unsigned i;

    for (i = 0u; i < 16u; ++i) {
        m[i] = ((uint32_t)block[i * 4u + 0u]) | ((uint32_t)block[i * 4u + 1u] << 8)
             | ((uint32_t)block[i * 4u + 2u] << 16) | ((uint32_t)block[i * 4u + 3u] << 24);
    }

    for (i = 0u; i < 64u; ++i) {
        uint32_t f;
        unsigned g;
        if (i < 16u) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32u) { f = (d & b) | (~d & c); g = (5u * i + 1u) & 15u; }
        else if (i < 48u) { f = b ^ c ^ d; g = (3u * i + 5u) & 15u; }
        else { f = c ^ (b | ~d); g = (7u * i) & 15u; }

        f = f + a + TEKO_MD5_K[i] + m[g];
        a = d; d = c; c = b;
        b = b + teko_md5_rotl(f, TEKO_MD5_S[i]);
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

void teko_md5_update(TekoMd5Ctx* ctx, const uint8_t* data, size_t len) {
    ctx->bit_len += (uint64_t)len * 8u;

    if (ctx->buffer_len != 0u) {
        size_t need = TEKO_MD5_BLOCK_LEN - ctx->buffer_len;
        size_t take = (len < need) ? len : need;
        memcpy(ctx->buffer + ctx->buffer_len, data, take);
        ctx->buffer_len += take;
        data += take;
        len -= take;
        if (ctx->buffer_len == TEKO_MD5_BLOCK_LEN) {
            teko_md5_compress(ctx->state, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }

    while (len >= TEKO_MD5_BLOCK_LEN) {
        teko_md5_compress(ctx->state, data);
        data += TEKO_MD5_BLOCK_LEN;
        len -= TEKO_MD5_BLOCK_LEN;
    }

    if (len != 0u) {
        memcpy(ctx->buffer + ctx->buffer_len, data, len);
        ctx->buffer_len += len;
    }
}

void teko_md5_final(TekoMd5Ctx* ctx, uint8_t out[16]) {
    const uint64_t bit_len = ctx->bit_len;
    unsigned i;

    ctx->buffer[ctx->buffer_len++] = 0x80u;
    if (ctx->buffer_len > 56u) {
        memset(ctx->buffer + ctx->buffer_len, 0, TEKO_MD5_BLOCK_LEN - ctx->buffer_len);
        teko_md5_compress(ctx->state, ctx->buffer);
        ctx->buffer_len = 0u;
    }
    memset(ctx->buffer + ctx->buffer_len, 0, 56u - ctx->buffer_len);

    for (i = 0u; i < 8u; ++i) ctx->buffer[56u + i] = (uint8_t)(bit_len >> (8u * i)); // little-endian
    teko_md5_compress(ctx->state, ctx->buffer);

    for (i = 0u; i < 4u; ++i) {
        out[i * 4u + 0u] = (uint8_t)(ctx->state[i]);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 3u] = (uint8_t)(ctx->state[i] >> 24);
    }
}

void teko_md5(const uint8_t* data, size_t len, uint8_t out[16]) {
    TekoMd5Ctx ctx;
    teko_md5_init(&ctx);
    teko_md5_update(&ctx, data, len);
    teko_md5_final(&ctx, out);
}
