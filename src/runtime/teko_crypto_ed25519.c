// Native Ed25519 (RFC 8032). Portable C23, built on the shared fe25519 field + SHA-512.
// Edwards point arithmetic and scalar reduction after the public-domain TweetNaCl design.

#include "teko_crypto_ed25519.h"
#include "teko_crypto_fe25519.h"
#include "teko_crypto_sha512.h"

#include <string.h>

static const teko_fe TEKO_ED_GF0 = { 0 };
static const teko_fe TEKO_ED_GF1 = { 1 };
static const teko_fe TEKO_ED_D = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};
static const teko_fe TEKO_ED_D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};
static const teko_fe TEKO_ED_X = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};
static const teko_fe TEKO_ED_Y = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};
static const teko_fe TEKO_ED_I = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

// Group order L (little-endian bytes).
static const int64_t TEKO_ED_L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10
};

static void teko_ed_hash(uint8_t out[64], const uint8_t* a, size_t alen,
                         const uint8_t* b, size_t blen, const uint8_t* c, size_t clen) {
    TekoSha512Ctx ctx;
    teko_sha512_init(&ctx);
    if (alen) teko_sha512_update(&ctx, a, alen);
    if (blen) teko_sha512_update(&ctx, b, blen);
    if (clen) teko_sha512_update(&ctx, c, clen);
    teko_sha512_final(&ctx, out);
}

// Edwards point P = (X, Y, Z, T) as four field elements.
static void teko_ed_add(teko_fe p[4], teko_fe q[4]) {
    teko_fe a, b, c, d, t, e, f, g, h;
    teko_fe_sub(a, p[1], p[0]); teko_fe_sub(t, q[1], q[0]); teko_fe_mul(a, a, t);
    teko_fe_add(b, p[0], p[1]); teko_fe_add(t, q[0], q[1]); teko_fe_mul(b, b, t);
    teko_fe_mul(c, p[3], q[3]); teko_fe_mul(c, c, TEKO_ED_D2);
    teko_fe_mul(d, p[2], q[2]); teko_fe_add(d, d, d);
    teko_fe_sub(e, b, a); teko_fe_sub(f, d, c); teko_fe_add(g, d, c); teko_fe_add(h, b, a);
    teko_fe_mul(p[0], e, f); teko_fe_mul(p[1], h, g); teko_fe_mul(p[2], g, f); teko_fe_mul(p[3], e, h);
}

static void teko_ed_cswap(teko_fe p[4], teko_fe q[4], int b) {
    int i;
    for (i = 0; i < 4; ++i) teko_fe_cswap(p[i], q[i], b);
}

static void teko_ed_pack(uint8_t r[32], teko_fe p[4]) {
    teko_fe tx, ty, zi;
    teko_fe_inv(zi, p[2]);
    teko_fe_mul(tx, p[0], zi);
    teko_fe_mul(ty, p[1], zi);
    teko_fe_pack(r, ty);
    r[31] ^= (uint8_t)(teko_fe_parity(tx) << 7);
}

static void teko_ed_scalarmult(teko_fe p[4], teko_fe q[4], const uint8_t s[32]) {
    int i;
    teko_fe_copy(p[0], TEKO_ED_GF0);
    teko_fe_copy(p[1], TEKO_ED_GF1);
    teko_fe_copy(p[2], TEKO_ED_GF1);
    teko_fe_copy(p[3], TEKO_ED_GF0);
    for (i = 255; i >= 0; --i) {
        int b = (s[i >> 3] >> (i & 7)) & 1;
        teko_ed_cswap(p, q, b);
        teko_ed_add(q, p);
        teko_ed_add(p, p);
        teko_ed_cswap(p, q, b);
    }
}

static void teko_ed_scalarbase(teko_fe p[4], const uint8_t s[32]) {
    teko_fe q[4];
    teko_fe_copy(q[0], TEKO_ED_X);
    teko_fe_copy(q[1], TEKO_ED_Y);
    teko_fe_copy(q[2], TEKO_ED_GF1);
    teko_fe_mul(q[3], TEKO_ED_X, TEKO_ED_Y);
    teko_ed_scalarmult(p, q, s);
}

// Reduce x (64 limbs) modulo L into the 32-byte r.
static void teko_ed_modL(uint8_t r[32], int64_t x[64]) {
    int64_t carry;
    int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * TEKO_ED_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) {
        x[j] += carry - (x[31] >> 4) * TEKO_ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j) x[j] -= carry * TEKO_ED_L[j];
    for (i = 0; i < 32; ++i) {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

static void teko_ed_reduce(uint8_t r[64]) {
    int64_t x[64];
    int i;
    for (i = 0; i < 64; ++i) x[i] = (int64_t)(uint64_t)r[i];
    for (i = 0; i < 64; ++i) r[i] = 0;
    teko_ed_modL(r, x);
}

static int teko_ed_unpackneg(teko_fe r[4], const uint8_t p[32]) {
    teko_fe t, chk, num, den, den2, den4, den6;

    teko_fe_copy(r[2], TEKO_ED_GF1);
    teko_fe_unpack(r[1], p);
    teko_fe_sq(num, r[1]);
    teko_fe_mul(den, num, TEKO_ED_D);
    teko_fe_sub(num, num, r[2]);
    teko_fe_add(den, r[2], den);

    teko_fe_sq(den2, den);
    teko_fe_sq(den4, den2);
    teko_fe_mul(den6, den4, den2);
    teko_fe_mul(t, den6, num);
    teko_fe_mul(t, t, den);

    teko_fe_pow2523(t, t);
    teko_fe_mul(t, t, num);
    teko_fe_mul(t, t, den);
    teko_fe_mul(t, t, den);
    teko_fe_mul(r[0], t, den);

    teko_fe_sq(chk, r[0]);
    teko_fe_mul(chk, chk, den);
    if (teko_fe_neq(chk, num)) teko_fe_mul(r[0], r[0], TEKO_ED_I);

    teko_fe_sq(chk, r[0]);
    teko_fe_mul(chk, chk, den);
    if (teko_fe_neq(chk, num)) return -1;

    if (teko_fe_parity(r[0]) == (p[31] >> 7)) teko_fe_sub(r[0], TEKO_ED_GF0, r[0]);
    teko_fe_mul(r[3], r[0], r[1]);
    return 0;
}

void teko_ed25519_pubkey(uint8_t pub[32], const uint8_t seed[32]) {
    uint8_t d[64];
    teko_fe p[4];
    teko_ed_hash(d, seed, 32u, NULL, 0u, NULL, 0u);
    d[0] &= 248; d[31] &= 127; d[31] |= 64;
    teko_ed_scalarbase(p, d);
    teko_ed_pack(pub, p);
    memset(d, 0, sizeof(d));
}

void teko_ed25519_sign(uint8_t sig[64], const uint8_t* msg, size_t msg_len,
                       const uint8_t seed[32], const uint8_t pub[32]) {
    uint8_t d[64], r[64], h[64];
    int64_t x[64];
    teko_fe p[4];
    int i, j;

    teko_ed_hash(d, seed, 32u, NULL, 0u, NULL, 0u);
    d[0] &= 248; d[31] &= 127; d[31] |= 64;

    // r = H(prefix || M), R = r*B.
    teko_ed_hash(r, d + 32, 32u, msg, msg_len, NULL, 0u);
    teko_ed_reduce(r);
    teko_ed_scalarbase(p, r);
    teko_ed_pack(sig, p);

    // h = H(R || A || M).
    teko_ed_hash(h, sig, 32u, pub, 32u, msg, msg_len);
    teko_ed_reduce(h);

    // S = (r + h*a) mod L.
    for (i = 0; i < 64; ++i) x[i] = 0;
    for (i = 0; i < 32; ++i) x[i] = (int64_t)(uint64_t)r[i];
    for (i = 0; i < 32; ++i) for (j = 0; j < 32; ++j) x[i + j] += (int64_t)h[i] * (int64_t)d[j];
    teko_ed_modL(sig + 32, x);

    memset(d, 0, sizeof(d));
}

int teko_ed25519_verify(const uint8_t sig[64], const uint8_t* msg, size_t msg_len,
                        const uint8_t pub[32]) {
    uint8_t h[64], rcheck[32];
    teko_fe p[4], q[4];
    int i;

    if (sig[63] & 224) return -1;          // S must be reduced (high bits clear)
    if (teko_ed_unpackneg(q, pub)) return -1;

    teko_ed_hash(h, sig, 32u, pub, 32u, msg, msg_len);
    teko_ed_reduce(h);

    teko_ed_scalarmult(p, q, h);           // p = h * (-A)
    {
        teko_fe bp[4];
        teko_fe_copy(bp[0], TEKO_ED_X);
        teko_fe_copy(bp[1], TEKO_ED_Y);
        teko_fe_copy(bp[2], TEKO_ED_GF1);
        teko_fe_mul(bp[3], TEKO_ED_X, TEKO_ED_Y);
        teko_ed_scalarmult(q, bp, sig + 32); // q = S*B
    }
    teko_ed_add(p, q);                      // p = S*B - h*A
    teko_ed_pack(rcheck, p);

    // Constant-time compare against R = sig[0..31].
    {
        uint8_t diff = 0u;
        for (i = 0; i < 32; ++i) diff |= (uint8_t)(rcheck[i] ^ sig[i]);
        return diff == 0u ? 0 : -1;
    }
}
