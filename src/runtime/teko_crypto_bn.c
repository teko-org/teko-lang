// Native multi-precision Montgomery arithmetic (see header). Portable C23, no __int128.

#include "teko_crypto_bn.h"

#include <string.h>

// --- limb helpers (length n, little-endian) ---------------------------------------------

static void teko_bn_from_be(uint32_t* limbs, int n, const uint8_t* bytes, size_t blen) {
    int i;
    size_t k;
    for (i = 0; i < n; ++i) limbs[i] = 0u;
    for (k = 0u; k < blen; ++k) {
        size_t pos = blen - 1u - k; // byte k counting from the least-significant end
        unsigned limb = (unsigned)(k >> 2);
        if ((int)limb >= n) break;
        limbs[limb] |= (uint32_t)bytes[pos] << (8u * (k & 3u));
    }
}

static void teko_bn_to_be(uint8_t* bytes, size_t blen, const uint32_t* limbs, int n) {
    size_t k;
    for (k = 0u; k < blen; ++k) {
        size_t pos = blen - 1u - k;
        unsigned limb = (unsigned)(k >> 2);
        uint32_t v = ((int)limb < n) ? limbs[limb] : 0u;
        bytes[pos] = (uint8_t)(v >> (8u * (k & 3u)));
    }
}

// Returns -1/0/1 for a<b / a==b / a>b.
static int teko_bn_cmp(const uint32_t* a, const uint32_t* b, int n) {
    int i;
    for (i = n - 1; i >= 0; --i) {
        if (a[i] != b[i]) return (a[i] < b[i]) ? -1 : 1;
    }
    return 0;
}

// out = a - b (n limbs); returns the final borrow.
static uint32_t teko_bn_sub(uint32_t* out, const uint32_t* a, const uint32_t* b, int n) {
    uint64_t borrow = 0u;
    int i;
    for (i = 0; i < n; ++i) {
        uint64_t s = (uint64_t)a[i] - b[i] - borrow;
        out[i] = (uint32_t)s;
        borrow = (s >> 32) & 1u;
    }
    return (uint32_t)borrow;
}

// -m^{-1} mod 2^32 via Newton's iteration (m0 must be odd).
static uint32_t teko_bn_n0(uint32_t m0) {
    uint32_t x = m0; // x = m0^{-1} mod 2^? — converges
    x *= 2u - m0 * x;
    x *= 2u - m0 * x;
    x *= 2u - m0 * x;
    x *= 2u - m0 * x;
    x *= 2u - m0 * x;
    return (uint32_t)(0u - x); // -m0^{-1} mod 2^32
}

// out = a*b mod m in the Montgomery domain (CIOS). a, b < m. Result < m.
static void teko_bn_montmul(uint32_t* out, const uint32_t* a, const uint32_t* b,
                            const uint32_t* m, int n, uint32_t n0) {
    uint32_t t[TEKO_BN_MAX_LIMBS + 2];
    int i, j;
    memset(t, 0, sizeof(uint32_t) * (size_t)(n + 2));

    for (i = 0; i < n; ++i) {
        uint64_t carry = 0u;
        uint32_t mm;
        uint64_t s;
        for (j = 0; j < n; ++j) {
            s = (uint64_t)t[j] + (uint64_t)a[i] * b[j] + carry;
            t[j] = (uint32_t)s;
            carry = s >> 32;
        }
        s = (uint64_t)t[n] + carry;
        t[n] = (uint32_t)s;
        t[n + 1] = (uint32_t)(s >> 32);

        mm = (uint32_t)(t[0] * n0);
        s = (uint64_t)t[0] + (uint64_t)mm * m[0];
        carry = s >> 32;
        for (j = 1; j < n; ++j) {
            s = (uint64_t)t[j] + (uint64_t)mm * m[j] + carry;
            t[j - 1] = (uint32_t)s;
            carry = s >> 32;
        }
        s = (uint64_t)t[n] + carry;
        t[n - 1] = (uint32_t)s;
        t[n] = (uint32_t)(t[n + 1] + (uint32_t)(s >> 32));
    }

    // Conditional final subtraction: if t has the extra top limb set, or t >= m.
    {
        uint32_t tmp[TEKO_BN_MAX_LIMBS];
        uint32_t borrow = teko_bn_sub(tmp, t, m, n);
        // Use (t - m) iff t[n] (overflow) is set, or no borrow (t >= m).
        uint32_t use_sub = (t[n] != 0u) || (borrow == 0u);
        memcpy(out, use_sub ? tmp : t, sizeof(uint32_t) * (size_t)n);
    }
}

int teko_mont_init(TekoMont* mont, const uint8_t* mod_be, size_t mod_len) {
    int n, i;
    uint32_t one[TEKO_BN_MAX_LIMBS];

    if (!mod_be || mod_len == 0u) return -1;
    n = (int)((mod_len + 3u) / 4u);
    if (n == 0 || n > TEKO_BN_MAX_LIMBS) return -1;

    teko_bn_from_be(mont->m, n, mod_be, mod_len);
    if ((mont->m[0] & 1u) == 0u) return -1; // modulus must be odd
    mont->n = n;
    mont->n0 = teko_bn_n0(mont->m[0]);

    // rr = R^2 mod m, R = 2^(32n). Compute by doubling 1 a total of 2*32*n times mod m
    // (each step is a conditional subtract, since the value stays < m).
    for (i = 0; i < n; ++i) one[i] = 0u;
    one[0] = 1u;
    {
        uint32_t acc[TEKO_BN_MAX_LIMBS];
        int bits = 2 * 32 * n;
        int b;
        memcpy(acc, one, sizeof(uint32_t) * (size_t)n);
        for (b = 0; b < bits; ++b) {
            // acc = (acc << 1) mod m
            uint32_t carry = 0u;
            for (i = 0; i < n; ++i) {
                uint32_t hi = acc[i] >> 31;
                acc[i] = (acc[i] << 1) | carry;
                carry = hi;
            }
            // carry (bit 32n) or acc >= m -> subtract m
            {
                uint32_t tmp[TEKO_BN_MAX_LIMBS];
                uint32_t borrow = teko_bn_sub(tmp, acc, mont->m, n);
                if (carry != 0u || borrow == 0u) {
                    memcpy(acc, tmp, sizeof(uint32_t) * (size_t)n);
                }
            }
        }
        memcpy(mont->rr, acc, sizeof(uint32_t) * (size_t)n);
    }

    // one_mont = R mod m = montmul(1, rr).
    teko_bn_montmul(mont->one, one, mont->rr, mont->m, mont->n, mont->n0);
    return 0;
}

int teko_mont_modexp(const TekoMont* mont,
                     const uint8_t* base_be, size_t base_len,
                     const uint8_t* exp_be, size_t exp_len,
                     uint8_t* out_be, size_t out_len) {
    uint32_t base[TEKO_BN_MAX_LIMBS];
    uint32_t bm[TEKO_BN_MAX_LIMBS];
    uint32_t result[TEKO_BN_MAX_LIMBS];
    int n = mont->n;
    size_t i;
    int started = 0;

    if (!base_be || !exp_be || !out_be) return -1;

    teko_bn_from_be(base, n, base_be, base_len);
    // Reduce base mod m if needed (single conditional subtract chain is insufficient in
    // general; require base < m as callers guarantee). Bring to Montgomery form.
    if (teko_bn_cmp(base, mont->m, n) >= 0) {
        uint32_t tmp[TEKO_BN_MAX_LIMBS];
        while (teko_bn_cmp(base, mont->m, n) >= 0) {
            teko_bn_sub(tmp, base, mont->m, n);
            memcpy(base, tmp, sizeof(uint32_t) * (size_t)n);
        }
    }
    teko_bn_montmul(bm, base, mont->rr, mont->m, n, mont->n0);   // bm = base * R mod m
    memcpy(result, mont->one, sizeof(uint32_t) * (size_t)n);     // result = 1 * R mod m

    // Left-to-right square-and-multiply over the exponent bits (MSB first).
    for (i = 0u; i < exp_len; ++i) {
        int bit;
        for (bit = 7; bit >= 0; --bit) {
            uint32_t e = (exp_be[i] >> bit) & 1u;
            if (started) {
                teko_bn_montmul(result, result, result, mont->m, n, mont->n0);
            }
            if (e) {
                teko_bn_montmul(result, result, bm, mont->m, n, mont->n0);
                started = 1;
            }
        }
    }

    // out = result * 1 (back from Montgomery domain).
    {
        uint32_t one[TEKO_BN_MAX_LIMBS];
        int j;
        for (j = 0; j < n; ++j) one[j] = 0u;
        one[0] = 1u;
        teko_bn_montmul(result, result, one, mont->m, n, mont->n0);
    }
    teko_bn_to_be(out_be, out_len, result, n);
    return 0;
}

int teko_bn_modexp(const uint8_t* mod_be, size_t mod_len,
                   const uint8_t* base_be, size_t base_len,
                   const uint8_t* exp_be, size_t exp_len,
                   uint8_t* out_be) {
    TekoMont mont;
    if (teko_mont_init(&mont, mod_be, mod_len) != 0) return -1;
    return teko_mont_modexp(&mont, base_be, base_len, exp_be, exp_len, out_be, mod_len);
}
