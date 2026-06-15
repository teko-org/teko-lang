// Native scrypt (RFC 7914). Portable C23, no external libraries. Built on PBKDF2-HMAC-SHA256
// (already KAT-proven) + Salsa20/8 / BlockMix / ROMix.

#include "teko_crypto_scrypt.h"
#include "teko_crypto_pbkdf2.h"

#include <stdlib.h>
#include <string.h>

static inline uint32_t teko_sc_rotl(uint32_t a, unsigned b) {
    return (a << b) | (a >> (32u - b));
}

static inline uint32_t teko_sc_ld32(const uint8_t* p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void teko_sc_st32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// Salsa20/8 core on a 64-byte block (out = in + rounds(in)).
static void teko_salsa20_8(uint8_t out[64], const uint8_t in[64]) {
    uint32_t x[16], orig[16];
    unsigned i;
    for (i = 0u; i < 16u; ++i) { x[i] = teko_sc_ld32(in + i * 4u); orig[i] = x[i]; }

    for (i = 0u; i < 8u; i += 2u) {
        x[ 4] ^= teko_sc_rotl(x[ 0] + x[12], 7);  x[ 8] ^= teko_sc_rotl(x[ 4] + x[ 0], 9);
        x[12] ^= teko_sc_rotl(x[ 8] + x[ 4],13);  x[ 0] ^= teko_sc_rotl(x[12] + x[ 8],18);
        x[ 9] ^= teko_sc_rotl(x[ 5] + x[ 1], 7);  x[13] ^= teko_sc_rotl(x[ 9] + x[ 5], 9);
        x[ 1] ^= teko_sc_rotl(x[13] + x[ 9],13);  x[ 5] ^= teko_sc_rotl(x[ 1] + x[13],18);
        x[14] ^= teko_sc_rotl(x[10] + x[ 6], 7);  x[ 2] ^= teko_sc_rotl(x[14] + x[10], 9);
        x[ 6] ^= teko_sc_rotl(x[ 2] + x[14],13);  x[10] ^= teko_sc_rotl(x[ 6] + x[ 2],18);
        x[ 3] ^= teko_sc_rotl(x[15] + x[11], 7);  x[ 7] ^= teko_sc_rotl(x[ 3] + x[15], 9);
        x[11] ^= teko_sc_rotl(x[ 7] + x[ 3],13);  x[15] ^= teko_sc_rotl(x[11] + x[ 7],18);
        x[ 1] ^= teko_sc_rotl(x[ 0] + x[ 3], 7);  x[ 2] ^= teko_sc_rotl(x[ 1] + x[ 0], 9);
        x[ 3] ^= teko_sc_rotl(x[ 2] + x[ 1],13);  x[ 0] ^= teko_sc_rotl(x[ 3] + x[ 2],18);
        x[ 6] ^= teko_sc_rotl(x[ 5] + x[ 4], 7);  x[ 7] ^= teko_sc_rotl(x[ 6] + x[ 5], 9);
        x[ 4] ^= teko_sc_rotl(x[ 7] + x[ 6],13);  x[ 5] ^= teko_sc_rotl(x[ 4] + x[ 7],18);
        x[11] ^= teko_sc_rotl(x[10] + x[ 9], 7);  x[ 8] ^= teko_sc_rotl(x[11] + x[10], 9);
        x[ 9] ^= teko_sc_rotl(x[ 8] + x[11],13);  x[10] ^= teko_sc_rotl(x[ 9] + x[ 8],18);
        x[12] ^= teko_sc_rotl(x[15] + x[14], 7);  x[13] ^= teko_sc_rotl(x[12] + x[15], 9);
        x[14] ^= teko_sc_rotl(x[13] + x[12],13);  x[15] ^= teko_sc_rotl(x[14] + x[13],18);
    }

    for (i = 0u; i < 16u; ++i) teko_sc_st32(out + i * 4u, x[i] + orig[i]);
}

// BlockMix: B is 2r 64-byte blocks; out gets the even-then-odd permutation of the chain.
static void teko_sc_blockmix(const uint8_t* b, uint8_t* out, uint32_t r, uint8_t x[64]) {
    uint32_t i, j;
    memcpy(x, b + (2u * r - 1u) * 64u, 64u);
    for (i = 0u; i < 2u * r; ++i) {
        uint8_t t[64];
        for (j = 0u; j < 64u; ++j) t[j] = (uint8_t)(x[j] ^ b[i * 64u + j]);
        teko_salsa20_8(x, t);
        {
            uint32_t pos = ((i & 1u) == 0u) ? (i / 2u) : (r + i / 2u);
            memcpy(out + (size_t)pos * 64u, x, 64u);
        }
    }
}

static uint64_t teko_sc_integerify(const uint8_t* x, uint32_t r) {
    // Little-endian 32-bit read of the last 64-byte block (sufficient for power-of-two N).
    return (uint64_t)teko_sc_ld32(x + (size_t)(2u * r - 1u) * 64u);
}

// ROMix on a single 128*r-byte block, in place. v is scratch of N*128*r bytes; xy is 2*128*r.
static void teko_sc_romix(uint8_t* block, uint32_t r, uint64_t n, uint8_t* v, uint8_t* xy) {
    size_t blocklen = (size_t)128u * r;
    uint8_t* x = xy;
    uint8_t* y = xy + blocklen;
    uint8_t xblk[64];
    uint64_t i;

    memcpy(x, block, blocklen);
    for (i = 0u; i < n; ++i) {
        memcpy(v + i * blocklen, x, blocklen);
        teko_sc_blockmix(x, y, r, xblk);
        memcpy(x, y, blocklen);
    }
    for (i = 0u; i < n; ++i) {
        uint64_t j = teko_sc_integerify(x, r) & (n - 1u); // n is a power of two
        size_t k;
        const uint8_t* vj = v + j * blocklen;
        for (k = 0u; k < blocklen; ++k) x[k] ^= vj[k];
        teko_sc_blockmix(x, y, r, xblk);
        memcpy(x, y, blocklen);
    }
    memcpy(block, x, blocklen);
}

int teko_scrypt(const uint8_t* password, size_t password_len,
                const uint8_t* salt, size_t salt_len,
                uint64_t n, uint32_t r, uint32_t p,
                uint8_t* dk, size_t dk_len) {
    size_t blocklen;
    uint8_t* b = NULL;
    uint8_t* v = NULL;
    uint8_t* xy = NULL;
    uint32_t i;
    int rc = -1;

    // Parameter validation: N a power of two > 1, r/p >= 1.
    if (r == 0u || p == 0u || n < 2u || (n & (n - 1u)) != 0u) return -1;

    blocklen = (size_t)128u * r;
    b = (uint8_t*)malloc(blocklen * p);
    v = (uint8_t*)malloc(blocklen * (size_t)n);
    xy = (uint8_t*)malloc(blocklen * 2u);
    if (!b || !v || !xy) goto done;

    // B = PBKDF2(P, S, 1, p * 128 * r).
    if (teko_pbkdf2_hmac_sha256(password, password_len, salt, salt_len, 1u, b, blocklen * p) != 0) {
        goto done;
    }

    for (i = 0u; i < p; ++i) {
        teko_sc_romix(b + (size_t)i * blocklen, r, n, v, xy);
    }

    // DK = PBKDF2(P, B, 1, dk_len).
    if (teko_pbkdf2_hmac_sha256(password, password_len, b, blocklen * p, 1u, dk, dk_len) != 0) {
        goto done;
    }
    rc = 0;

done:
    if (b) { memset(b, 0, blocklen * p); free(b); }
    if (v) { memset(v, 0, blocklen * (size_t)n); free(v); }
    if (xy) { memset(xy, 0, blocklen * 2u); free(xy); }
    return rc;
}
