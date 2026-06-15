// Native Poly1305 (RFC 8439 §2.5), 26-bit-limb field arithmetic. Portable C23, no
// external libraries, no __int128 (32x32->64 multiplies only).

#include "teko_crypto_poly1305.h"

#include <string.h>

static inline uint32_t teko_p1_ld32(const uint8_t* p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void teko_p1_st32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void teko_poly1305_init(TekoPoly1305Ctx* ctx, const uint8_t key[32]) {
    // r &= 0x0ffffffc0ffffffc0ffffffc0fffffff (clamp).
    ctx->r[0] = (teko_p1_ld32(key + 0)) & 0x03ffffffu;
    ctx->r[1] = (teko_p1_ld32(key + 3) >> 2) & 0x03ffff03u;
    ctx->r[2] = (teko_p1_ld32(key + 6) >> 4) & 0x03ffc0ffu;
    ctx->r[3] = (teko_p1_ld32(key + 9) >> 6) & 0x03f03fffu;
    ctx->r[4] = (teko_p1_ld32(key + 12) >> 8) & 0x000fffffu;

    ctx->h[0] = ctx->h[1] = ctx->h[2] = ctx->h[3] = ctx->h[4] = 0u;

    ctx->pad[0] = teko_p1_ld32(key + 16);
    ctx->pad[1] = teko_p1_ld32(key + 20);
    ctx->pad[2] = teko_p1_ld32(key + 24);
    ctx->pad[3] = teko_p1_ld32(key + 28);

    ctx->leftover = 0u;
    ctx->final = 0u;
}

static void teko_poly1305_blocks(TekoPoly1305Ctx* ctx, const uint8_t* m, size_t bytes) {
    const uint32_t hibit = ctx->final ? 0u : (1u << 24);
    uint32_t r0 = ctx->r[0], r1 = ctx->r[1], r2 = ctx->r[2], r3 = ctx->r[3], r4 = ctx->r[4];
    uint32_t s1 = r1 * 5u, s2 = r2 * 5u, s3 = r3 * 5u, s4 = r4 * 5u;
    uint32_t h0 = ctx->h[0], h1 = ctx->h[1], h2 = ctx->h[2], h3 = ctx->h[3], h4 = ctx->h[4];

    while (bytes >= 16u) {
        uint64_t d0, d1, d2, d3, d4;
        uint32_t c;

        // h += m (with the high bit set unless this is the final padded block).
        h0 += (teko_p1_ld32(m + 0)) & 0x03ffffffu;
        h1 += (teko_p1_ld32(m + 3) >> 2) & 0x03ffffffu;
        h2 += (teko_p1_ld32(m + 6) >> 4) & 0x03ffffffu;
        h3 += (teko_p1_ld32(m + 9) >> 6) & 0x03ffffffu;
        h4 += (teko_p1_ld32(m + 12) >> 8) | hibit;

        // h *= r (mod 2^130 - 5).
        d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x03ffffffu;
        d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x03ffffffu;
        d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x03ffffffu;
        d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x03ffffffu;
        d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x03ffffffu;
        h0 += c * 5u; c = h0 >> 26; h0 &= 0x03ffffffu; h1 += c;

        m += 16u;
        bytes -= 16u;
    }

    ctx->h[0] = h0; ctx->h[1] = h1; ctx->h[2] = h2; ctx->h[3] = h3; ctx->h[4] = h4;
}

void teko_poly1305_update(TekoPoly1305Ctx* ctx, const uint8_t* m, size_t bytes) {
    if (ctx->leftover != 0u) {
        size_t want = 16u - ctx->leftover;
        if (want > bytes) want = bytes;
        memcpy(ctx->buffer + ctx->leftover, m, want);
        bytes -= want;
        m += want;
        ctx->leftover += want;
        if (ctx->leftover < 16u) return;
        teko_poly1305_blocks(ctx, ctx->buffer, 16u);
        ctx->leftover = 0u;
    }

    if (bytes >= 16u) {
        size_t want = bytes & ~(size_t)15u;
        teko_poly1305_blocks(ctx, m, want);
        m += want;
        bytes -= want;
    }

    if (bytes != 0u) {
        memcpy(ctx->buffer + ctx->leftover, m, bytes);
        ctx->leftover += bytes;
    }
}

void teko_poly1305_finish(TekoPoly1305Ctx* ctx, uint8_t tag[16]) {
    uint32_t h0, h1, h2, h3, h4, c;
    uint32_t g0, g1, g2, g3, g4;
    uint32_t mask;
    uint64_t f;

    if (ctx->leftover != 0u) {
        size_t i = ctx->leftover;
        ctx->buffer[i++] = 1u;
        for (; i < 16u; ++i) ctx->buffer[i] = 0u;
        ctx->final = 1u;
        teko_poly1305_blocks(ctx, ctx->buffer, 16u);
    }

    // Fully carry h.
    h0 = ctx->h[0]; h1 = ctx->h[1]; h2 = ctx->h[2]; h3 = ctx->h[3]; h4 = ctx->h[4];
    c = h1 >> 26; h1 &= 0x03ffffffu; h2 += c;
    c = h2 >> 26; h2 &= 0x03ffffffu; h3 += c;
    c = h3 >> 26; h3 &= 0x03ffffffu; h4 += c;
    c = h4 >> 26; h4 &= 0x03ffffffu; h0 += c * 5u;
    c = h0 >> 26; h0 &= 0x03ffffffu; h1 += c;

    // Compute h + -p (i.e. h - (2^130 - 5)); select it iff h >= p (constant time).
    g0 = h0 + 5u; c = g0 >> 26; g0 &= 0x03ffffffu;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x03ffffffu;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x03ffffffu;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x03ffffffu;
    g4 = h4 + c - (1u << 26);

    mask = (g4 >> 31) - 1u; // all-ones if no borrow (h >= p), else 0
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    // Pack the 130-bit number into four 32-bit little-endian words (mod 2^128).
    h0 = (h0) | (h1 << 26);
    h1 = (h1 >> 6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 << 8);

    // tag = (h + pad) mod 2^128.
    f = (uint64_t)h0 + ctx->pad[0]; h0 = (uint32_t)f;
    f = (uint64_t)h1 + ctx->pad[1] + (f >> 32); h1 = (uint32_t)f;
    f = (uint64_t)h2 + ctx->pad[2] + (f >> 32); h2 = (uint32_t)f;
    f = (uint64_t)h3 + ctx->pad[3] + (f >> 32); h3 = (uint32_t)f;

    teko_p1_st32(tag + 0, h0);
    teko_p1_st32(tag + 4, h1);
    teko_p1_st32(tag + 8, h2);
    teko_p1_st32(tag + 12, h3);

    // Wipe sensitive state.
    memset(ctx, 0, sizeof(*ctx));
}

void teko_poly1305(const uint8_t* m, size_t bytes, const uint8_t key[32], uint8_t tag[16]) {
    TekoPoly1305Ctx ctx;
    teko_poly1305_init(&ctx, key);
    teko_poly1305_update(&ctx, m, bytes);
    teko_poly1305_finish(&ctx, tag);
}
