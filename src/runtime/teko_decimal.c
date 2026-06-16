// =====================================================================================
// teko_decimal.c — Phase 17.F.1: the 256-byte exact base-10 `decimal` runtime core.
//
// The single C source of truth for Teko's exact base-10 decimal type: self-contained
// 64-bit-limb arithmetic (add/sub/mul/div/mod/compare) with banker's rounding, no libc
// beyond mem* and NO __int128 / libm / snprintf / malloc. Compiles unchanged on the
// native libc targets, the Windows MSVC gate, and --target=wasm32 -ffreestanding -nostdlib.
// KAT-tested in the Unity suite against a Python `decimal` oracle (tools/gen_decimal_kats.py).
//
// -------------------------------------------------------------------------------------
// VALUE MODEL (owner-LOCKED — see teko_decimal.h):
//   value = (-1)^sign * COEFF * 10^(-scale),   scale in [0,38],
//   COEFF = a 1984-bit unsigned integer in 31 LITTLE-ENDIAN u64 limbs. Always finite.
//
// -------------------------------------------------------------------------------------
// ARITHMETIC SEMANTICS  (pinned EXACTLY so the Python `decimal` oracle agrees BYTE-FOR-BYTE;
//                        this block is the owner-confirmation surface — a semantic
//                        disagreement could only hide here):
//
//   Every binary op computes the EXACT mathematical result, then applies exactly two
//   reductions, IN THIS ORDER:
//     (1) Fractional cap + banker's rounding. If the exact result needs > 38 fractional
//         decimal digits, round to scale 38 using ROUND-HALF-TO-EVEN. Results that need
//         <= 38 fractional digits are EXACT (never rounded). Integer (left-of-point)
//         precision is NEVER rounded away.
//     (2) Overflow check (fail-loud). If the resulting COEFF does not fit 1984 bits
//         (i.e. >= 2^1984, ~ a 598-digit coefficient) -> FAIL-LOUD (return 0). div/mod by
//         zero -> FAIL-LOUD (return 0). On failure *out is zeroed.
//
//   Per-op:
//     add/sub: align both operands to s = max(sa, sb) (scaling the smaller coefficient up
//              by *10^(s-si); if THAT alignment overflows 1984 bits -> fail-loud). Then do a
//              signed add/sub of the coefficients. Result scale = s. (No fractional growth,
//              so reduction (1) is a no-op; (2) applies.) Result coefficient 0 => +0 scale s.
//     mul:     sign = sa^sb; s_r = sa+sb; COEFF_r = COEFF_a * COEFF_b. If s_r > 38, drop the
//              (s_r-38) lowest decimal digits of COEFF_r with banker's rounding, s_r = 38.
//              Then reduction (2).
//     div:     b==0 -> fail-loud. sign = sa^sb. Result scale = 38. Compute, in exact integer
//              arithmetic, Q with one guard digit:
//                  scaled_num = COEFF_a * 10^(38 - sa + sb + 1)   [if the exponent is
//                               negative, scale the DENOMINATOR by 10^(-exp) instead]
//                  q1 = scaled_num / COEFF_b ; r1 = scaled_num % COEFF_b
//              then round q1 (which carries one extra low digit) to scale 38 by banker's-
//              dropping that 1 guard digit, with r1 != 0 making the dropped-digit boundary
//              STICKY (so a non-terminating division never lands exactly on a .5 tie unless
//              it truly is one). Result coefficient kept at scale 38 UN-normalized (no
//              trailing-zero trim — see NORMALIZATION). Then reduction (2).
//     mod:     match Python Decimal.__mod__:  a - (a // b) * b  where // truncates toward
//              ZERO. Implemented as: t = trunc(a / b) (the exact integer quotient, sign
//              sa^sb, NO rounding — truncated), then result = a - t*b (exact, via sub/mul).
//              Sign of a non-zero result = sign of a. b==0 -> fail-loud.
//     cmp:     compare signs first (+0 == -0, so a zero coefficient is treated as sign 0);
//              for equal signs compare magnitudes by aligning to a common scale in a WIDE
//              (62-limb) temp WITHOUT mutating the inputs (the alignment may exceed 1984
//              bits in the temp). Returns -1/0/1; cannot fail.
//
//   NORMALIZATION (decision): results are stored UN-NORMALIZED at the natural scale produced
//   by the rules above. add/sub keep scale = max(sa,sb); mul keeps s_r (capped at 38); div
//   keeps scale 38. We do NOT strip trailing zero fractional digits. The Python oracle emits
//   the SAME un-normalized encoding (it scales the exact Decimal to the rule's scale before
//   serializing), so the 256-byte byte-for-byte KAT holds. (Equality compares VALUES, so
//   1.50 == 1.5 under cmp; but the ENCODING of `1.50 + 0` is the scale-2 form 1.50, matching
//   the oracle.)
//
//   PURE / DETERMINISTIC: no globals, no clock, no rand — native and WASM are byte-identical.
// =====================================================================================

#include "teko_decimal.h"

#include <stdint.h>
#include <string.h>  // memcpy, memset, memcmp

// Wide working width for big-integer temporaries. The widest exact product we form is
// COEFF_a * COEFF_b (31 + 31 = 62 limbs); div scales a 31-limb numerator by up to
// 10^(38+1+38) ~ 2^256 (~5 limbs) into a <=37-limb value. 64 limbs gives margin.
#define BN_WIDE 64

// ----------------------------------------------------------------------------------------
// Portable 64x64 -> 128 product (NO __int128): low returned, high through *hi. Same idiom
// as the Ryu portable path in teko_convert_f64.c.
// ----------------------------------------------------------------------------------------
static uint64_t umul64(uint64_t a, uint64_t b, uint64_t* hi) {
    const uint32_t aLo = (uint32_t)a, aHi = (uint32_t)(a >> 32);
    const uint32_t bLo = (uint32_t)b, bHi = (uint32_t)(b >> 32);
    const uint64_t b00 = (uint64_t)aLo * bLo;
    const uint64_t b01 = (uint64_t)aLo * bHi;
    const uint64_t b10 = (uint64_t)aHi * bLo;
    const uint64_t b11 = (uint64_t)aHi * bHi;
    const uint32_t b00Hi = (uint32_t)(b00 >> 32);
    const uint64_t mid1 = b10 + b00Hi;
    const uint32_t mid1Lo = (uint32_t)mid1;
    const uint32_t mid1Hi = (uint32_t)(mid1 >> 32);
    const uint64_t mid2 = b01 + mid1Lo;
    const uint32_t mid2Hi = (uint32_t)(mid2 >> 32);
    *hi = b11 + (uint64_t)mid1Hi + (uint64_t)mid2Hi;
    return a * b; // low 64 bits == native wraparound product
}

// ----------------------------------------------------------------------------------------
// Fixed-width little-endian bignum helpers over uint64_t[N]. All temps are stack arrays of
// BN_WIDE limbs; nothing is heap-allocated. Logical length is tracked by callers.
// ----------------------------------------------------------------------------------------

// Significant-limb count (index past the highest nonzero limb). 0 for a zero value.
static int bn_len(const uint64_t* x, int n) {
    int i = n;
    while (i > 0 && x[i - 1] == 0) i--;
    return i;
}

static int bn_is_zero(const uint64_t* x, int n) {
    for (int i = 0; i < n; i++) if (x[i] != 0) return 0;
    return 1;
}

// Compare magnitudes of two N-limb LE values: -1 (a<b), 0 (a==b), 1 (a>b).
static int bn_cmp(const uint64_t* a, const uint64_t* b, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

// out = a + b (N limbs each). Returns the final carry (1 if it overflowed N limbs).
static uint64_t bn_add(const uint64_t* a, const uint64_t* b, uint64_t* out, int n) {
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t s = a[i] + carry;
        uint64_t c1 = (s < a[i]) ? 1u : 0u;
        s += b[i];
        uint64_t c2 = (s < b[i]) ? 1u : 0u;
        out[i] = s;
        carry = c1 + c2;
    }
    return carry;
}

// out = a - b (N limbs each), requires a >= b. Returns 0 (the final borrow is guaranteed 0).
static uint64_t bn_sub(const uint64_t* a, const uint64_t* b, uint64_t* out, int n) {
    uint64_t borrow = 0;
    for (int i = 0; i < n; i++) {
        uint64_t ai = a[i];
        uint64_t t = ai - borrow;
        uint64_t b1 = (t > ai) ? 1u : 0u;     // underflow from the borrow subtraction
        uint64_t r = t - b[i];
        uint64_t b2 = (r > t) ? 1u : 0u;      // underflow from subtracting b[i]
        out[i] = r;
        borrow = b1 + b2;
    }
    return borrow;
}

// out[W] = a * b, schoolbook. `alen`/`blen` = the SIGNIFICANT input limb counts; out must
// have room for at least alen+blen limbs (capped at W = BN_WIDE; the caller guarantees the
// true product fits, since coefficients are <= 31 limbs and alen+blen <= 62 < W). The high
// limbs of out up to W are zeroed.
static void bn_mul(const uint64_t* a, int alen, const uint64_t* b, int blen, uint64_t* out) {
    for (int i = 0; i < BN_WIDE; i++) out[i] = 0;
    for (int i = 0; i < alen; i++) {
        if (a[i] == 0) continue;
        uint64_t carry = 0;
        for (int j = 0; j < blen; j++) {
            uint64_t hi;
            uint64_t lo = umul64(a[i], b[j], &hi);
            // out[i+j] += lo + carry; propagate into hi.
            uint64_t cur = out[i + j];
            uint64_t s = cur + lo;
            uint64_t c1 = (s < cur) ? 1u : 0u;
            s += carry;
            uint64_t c2 = (s < carry) ? 1u : 0u;
            out[i + j] = s;
            carry = hi + c1 + c2;             // cannot overflow u64: hi <= 2^64-2
        }
        // flush the final carry up the column.
        int k = i + blen;
        while (carry != 0 && k < BN_WIDE) {
            uint64_t cur = out[k];
            uint64_t s = cur + carry;
            out[k] = s;
            carry = (s < cur) ? 1u : 0u;
            k++;
        }
    }
}

// out = x * 10^k (N limbs in place; out distinct or same as x). Returns 1 on success, 0 if
// the result overflows N limbs (a true overflow the caller treats as fail-loud, EXCEPT div
// uses a wide-enough buffer that it never trips). Multiplies by 10^9 chunks then a remainder.
static const uint64_t POW10_U64[20] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull, 1000000ull, 10000000ull,
    100000000ull, 1000000000ull, 10000000000ull, 100000000000ull, 1000000000000ull,
    10000000000000ull, 100000000000000ull, 1000000000000000ull, 10000000000000000ull,
    100000000000000000ull, 1000000000000000000ull, 10000000000000000000ull
};

// Multiply x[N] by a single-limb m in place; returns carry-out (the limb past index N-1).
static uint64_t bn_mul_small(uint64_t* x, uint64_t m, int n) {
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t hi;
        uint64_t lo = umul64(x[i], m, &hi);
        uint64_t s = lo + carry;
        uint64_t c1 = (s < lo) ? 1u : 0u;
        x[i] = s;
        carry = hi + c1;
    }
    return carry;
}

// x[N] *= 10^k, in place. Returns 1 on success, 0 if it overflows N limbs.
static int bn_mul_pow10(uint64_t* x, int k, int n) {
    while (k > 0) {
        int step = k > 19 ? 19 : k;   // 10^19 fits in u64
        uint64_t carry = bn_mul_small(x, POW10_U64[step], n);
        if (carry != 0) return 0;     // overflowed N limbs
        k -= step;
    }
    return 1;
}

// Single-limb divide: q[N] = x[N] / d, returns remainder. d != 0.
static uint64_t bn_divmod_small(const uint64_t* x, uint64_t d, uint64_t* q, int n) {
    uint64_t rem = 0;
    for (int i = n - 1; i >= 0; i--) {
        // (rem:x[i]) / d  via 32-bit half-limb long division (no __int128).
        uint64_t cur_hi = rem;
        uint64_t cur_lo = x[i];
        // Process the 128-bit (cur_hi:cur_lo) numerator one 32-bit chunk at a time.
        uint64_t acc = cur_hi;          // < d (invariant), fits 64 bits
        uint64_t qword = 0;
        for (int half = 1; half >= 0; half--) {
            acc = (acc << 32) | ((cur_lo >> (half * 32)) & 0xFFFFFFFFull);
            uint64_t qh = acc / d;
            acc = acc - qh * d;
            qword = (qword << 32) | qh;
        }
        q[i] = qword;
        rem = acc;
    }
    return rem;
}

// ----------------------------------------------------------------------------------------
// Knuth Algorithm D long division: q = u / v, r = u % v, over base-2^32 half-limbs.
// u has `ulen` u64 limbs (LE), v has `vlen` u64 limbs (LE), v != 0. q gets up to `ulen`
// u64 limbs, r gets up to `vlen` u64 limbs. All buffers caller-provided, BN_WIDE wide.
// This is the #1 correctness risk; the byte-for-byte non-terminating-division KATs guard it.
// ----------------------------------------------------------------------------------------

// Compare two 32-bit half-limb arrays of equal length `len` (LE): -1/0/1.
static int u32_cmp(const uint32_t* a, const uint32_t* b, int len) {
    for (int i = len - 1; i >= 0; i--) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

// View a u64[] as a 32-bit half-limb array (LE). We work in base b = 2^32.
static int bn_divmod(const uint64_t* u64in, int ulen,
                     const uint64_t* v64in, int vlen,
                     uint64_t* q64, uint64_t* r64) {
    // Half-limb (32-bit) views.
    uint32_t u[2 * BN_WIDE + 2];
    uint32_t v[2 * BN_WIDE + 2];
    int m32, n32;

    // Expand to 32-bit limbs, trimming high zeros.
    {
        int n = vlen;
        n32 = 0;
        for (int i = 0; i < n; i++) {
            v[2 * i]     = (uint32_t)v64in[i];
            v[2 * i + 1] = (uint32_t)(v64in[i] >> 32);
        }
        n32 = 2 * n;
        while (n32 > 0 && v[n32 - 1] == 0) n32--;
        if (n32 == 0) return 0; // div by zero
    }
    {
        int n = ulen;
        for (int i = 0; i < n; i++) {
            u[2 * i]     = (uint32_t)u64in[i];
            u[2 * i + 1] = (uint32_t)(u64in[i] >> 32);
        }
        m32 = 2 * n;
        while (m32 > 0 && u[m32 - 1] == 0) m32--;
    }

    // Zero outputs.
    for (int i = 0; i < BN_WIDE; i++) { q64[i] = 0; r64[i] = 0; }

    // If numerator < denominator (in half-limb length / value), quotient 0, remainder u.
    if (m32 < n32 || (m32 == n32 && u32_cmp(u, v, m32) < 0)) {
        for (int i = 0; i < (m32 + 1) / 2; i++) {
            uint64_t lo = (i * 2 < m32) ? u[i * 2] : 0u;
            uint64_t hi = (i * 2 + 1 < m32) ? u[i * 2 + 1] : 0u;
            r64[i] = lo | (hi << 32);
        }
        return 1;
    }

    // Single-limb (32-bit) divisor: simple long division.
    if (n32 == 1) {
        uint64_t d = v[0];
        uint64_t rem = 0;
        uint32_t qq[2 * BN_WIDE + 2];
        for (int i = m32 - 1; i >= 0; i--) {
            uint64_t cur = (rem << 32) | u[i];
            qq[i] = (uint32_t)(cur / d);
            rem = cur % d;
        }
        for (int i = 0; i < (m32 + 1) / 2; i++) {
            uint64_t lo = qq[i * 2];
            uint64_t hi = (i * 2 + 1 < m32) ? qq[i * 2 + 1] : 0u;
            q64[i] = lo | (hi << 32);
        }
        r64[0] = rem;
        return 1;
    }

    // --- Knuth Algorithm D (base 2^32) -------------------------------------------------
    const uint64_t B = 1ull << 32;
    int n = n32;
    int m = m32 - n32;   // quotient has m+1 half-limbs

    // D1. Normalize: shift so the top divisor half-limb >= B/2.
    int s = 0;
    {
        uint32_t top = v[n - 1];
        while ((top & 0x80000000u) == 0) { top <<= 1; s++; }
    }
    uint32_t un[2 * BN_WIDE + 4];
    uint32_t vn[2 * BN_WIDE + 2];
    // Normalize (shift-by-(32-s) is UB when s==0, so handle s==0 separately).
    if (s == 0) {
        for (int i = 0; i < n; i++) vn[i] = v[i];
        for (int i = 0; i <= m32; i++) un[i] = (i < m32) ? u[i] : 0u;
        un[m32] = 0;
    } else {
        for (int i = n - 1; i > 0; i--)
            vn[i] = (uint32_t)((v[i] << s) | ((uint64_t)v[i - 1] >> (32 - s)));
        vn[0] = (uint32_t)(v[0] << s);
        // Normalize u into un[0..m32], with un[m32] holding the overflow.
        un[m32] = (uint32_t)((uint64_t)u[m32 - 1] >> (32 - s));
        for (int i = m32 - 1; i > 0; i--)
            un[i] = (uint32_t)((u[i] << s) | ((uint64_t)u[i - 1] >> (32 - s)));
        un[0] = (uint32_t)(u[0] << s);
    }

    uint32_t qh[2 * BN_WIDE + 2];
    // D2-D7. Main loop, from j = m down to 0.
    for (int j = m; j >= 0; j--) {
        // D3. Estimate qhat.
        uint64_t num = ((uint64_t)un[j + n] << 32) | un[j + n - 1];
        uint64_t qhat = num / vn[n - 1];
        uint64_t rhat = num - qhat * vn[n - 1];
        while (qhat >= B || qhat * vn[n - 2] > (rhat << 32) + un[j + n - 2]) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= B) break;
        }
        // D4. Multiply and subtract.
        int64_t borrow = 0;
        uint64_t carry = 0;
        for (int i = 0; i < n; i++) {
            uint64_t p = qhat * vn[i] + carry;
            carry = p >> 32;
            int64_t sub = (int64_t)un[j + i] - (int64_t)(uint32_t)p - borrow;
            un[j + i] = (uint32_t)sub;
            borrow = (sub < 0) ? 1 : 0;
        }
        int64_t sub = (int64_t)un[j + n] - (int64_t)carry - borrow;
        un[j + n] = (uint32_t)sub;
        // D5/D6. If we subtracted too much, add back.
        if (sub < 0) {
            qhat--;
            uint64_t c = 0;
            for (int i = 0; i < n; i++) {
                uint64_t t = (uint64_t)un[j + i] + vn[i] + c;
                un[j + i] = (uint32_t)t;
                c = t >> 32;
            }
            un[j + n] = (uint32_t)(un[j + n] + c);
        }
        qh[j] = (uint32_t)qhat;
    }

    // Pack quotient half-limbs (indices 0..m) into q64.
    int qhalfs = m + 1;
    for (int i = 0; i < (qhalfs + 1) / 2; i++) {
        uint64_t lo = qh[i * 2];
        uint64_t hi = (i * 2 + 1 < qhalfs) ? qh[i * 2 + 1] : 0u;
        q64[i] = lo | (hi << 32);
    }

    // D8. Denormalize the remainder: (un[0..n-1] >> s).
    uint32_t rem[2 * BN_WIDE + 2];
    if (s == 0) {
        for (int i = 0; i < n; i++) rem[i] = un[i];
    } else {
        for (int i = 0; i < n - 1; i++)
            rem[i] = (uint32_t)((un[i] >> s) | ((uint64_t)un[i + 1] << (32 - s)));
        rem[n - 1] = un[n - 1] >> s;
    }
    for (int i = 0; i < (n + 1) / 2; i++) {
        uint64_t lo = rem[i * 2];
        uint64_t hi = (i * 2 + 1 < n) ? rem[i * 2 + 1] : 0u;
        r64[i] = lo | (hi << 32);
    }
    return 1;
}

// ----------------------------------------------------------------------------------------
// Decimal-level helpers.
// ----------------------------------------------------------------------------------------

// Does x[N] fit in 1984 bits (i.e. limbs [31..N) are all zero)?
static int bn_fits_coeff(const uint64_t* x, int n) {
    for (int i = TEKO_DECIMAL_LIMBS; i < n; i++) if (x[i] != 0) return 0;
    return 1;
}

// Round x by dropping the low `d` decimal digits with round-half-to-even.
//   divisor = 10^d ; q = x/divisor ; r = x%divisor ;
//   if 2*r > divisor || (2*r == divisor && (q is odd)) -> q++.
// `sticky` (extra dropped low precision below the d digits) forces 2*r != divisor exact-tie
// to act as > when nonzero. x has `n` limbs; out (n limbs) receives q. d >= 0.
static void bn_round_drop_decimals(const uint64_t* x, int d, int sticky, uint64_t* out, int n) {
    if (d <= 0) { for (int i = 0; i < n; i++) out[i] = x[i]; return; }
    // Build divisor = 10^d in a buffer.
    uint64_t div[BN_WIDE];
    for (int i = 0; i < n; i++) div[i] = 0;
    div[0] = 1;
    if (!bn_mul_pow10(div, d, n)) {
        // 10^d overflowed n limbs => x < divisor, so q=0, r=x. Tie impossible (divisor huge).
        // Round-half-to-even with q=0: round up only if 2*r > divisor, impossible here.
        for (int i = 0; i < n; i++) out[i] = 0;
        return;
    }
    uint64_t q[BN_WIDE], r[BN_WIDE];
    bn_divmod(x, n, div, n, q, r);
    // Compare 2*r vs divisor.
    uint64_t r2[BN_WIDE];
    for (int i = 0; i < n; i++) r2[i] = r[i];
    uint64_t carry = bn_mul_small(r2, 2, n); // 2*r (carry should be 0 since r < divisor <= ~10^38)
    (void)carry;
    int c = bn_cmp(r2, div, n);
    int roundup = 0;
    if (c > 0) {
        roundup = 1;
    } else if (c == 0) {
        roundup = sticky ? 1 : (int)(q[0] & 1ull); // exact half: to even (or sticky -> up)
    } else {
        roundup = 0;
    }
    if (roundup) {
        uint64_t one[BN_WIDE];
        for (int i = 0; i < n; i++) one[i] = 0;
        one[0] = 1;
        bn_add(q, one, q, n);
    }
    for (int i = 0; i < n; i++) out[i] = q[i];
}

// Load a teko_decimal's coefficient into a wide N-limb buffer (zero-extended).
static void dec_load(const teko_decimal* a, uint64_t* x, int n) {
    for (int i = 0; i < n; i++) x[i] = (i < TEKO_DECIMAL_LIMBS) ? a->limb[i] : 0u;
}

// Store a coefficient + sign + scale into out (fails if coeff doesn't fit 1984 bits).
static int dec_store(uint8_t sign, uint8_t scale, const uint64_t* x, int n, teko_decimal* out) {
    if (!bn_fits_coeff(x, n)) { teko_decimal_zero(out); return 0; }
    memset(out, 0, sizeof(*out));
    out->scale = scale;
    for (int i = 0; i < TEKO_DECIMAL_LIMBS; i++) out->limb[i] = x[i];
    // Canonicalize zero's sign.
    out->sign = bn_is_zero(out->limb, TEKO_DECIMAL_LIMBS) ? 0u : (sign ? 1u : 0u);
    return 1;
}

// ----------------------------------------------------------------------------------------
// Public API.
// ----------------------------------------------------------------------------------------

void teko_decimal_zero(teko_decimal* out) {
    memset(out, 0, sizeof(*out));
}

int teko_decimal_from_components(uint8_t sign, uint8_t scale, const char* digits,
                                 teko_decimal* out) {
    teko_decimal_zero(out);
    if (scale > TEKO_DECIMAL_MAX_SCALE) return 0;
    uint64_t x[BN_WIDE];
    for (int i = 0; i < BN_WIDE; i++) x[i] = 0;
    if (digits) {
        for (const char* p = digits; *p; p++) {
            if (*p < '0' || *p > '9') return 0;
            uint64_t carry = bn_mul_small(x, 10, BN_WIDE);
            if (carry != 0) return 0;
            uint64_t add = (uint64_t)(*p - '0');
            uint64_t one[BN_WIDE];
            for (int i = 0; i < BN_WIDE; i++) one[i] = 0;
            one[0] = add;
            bn_add(x, one, x, BN_WIDE);
        }
    }
    return dec_store(sign, scale, x, BN_WIDE, out);
}

// Helper: add/sub the magnitudes of two same-scale wide coefficients with signs, producing a
// signed result (out magnitude + out sign). subtract = (effective signs differ).
//   We compute result = (sa? -A : A) +/- ... ; here we pass already-aligned A,B with signs.
static void signed_combine(const uint64_t* a, int sa_neg, const uint64_t* b, int sb_neg,
                           int do_sub, uint64_t* out, int* out_neg, int n) {
    // Effective sign of B for the operation: do_sub flips it.
    int b_neg = do_sub ? !sb_neg : sb_neg;
    if (sa_neg == b_neg) {
        // Same sign: add magnitudes, keep that sign.
        bn_add(a, b, out, n);
        *out_neg = bn_is_zero(out, n) ? 0 : sa_neg;
    } else {
        // Opposite signs: subtract smaller from larger; sign of larger wins.
        int c = bn_cmp(a, b, n);
        if (c >= 0) {
            bn_sub(a, b, out, n);
            *out_neg = bn_is_zero(out, n) ? 0 : sa_neg;
        } else {
            bn_sub(b, a, out, n);
            *out_neg = bn_is_zero(out, n) ? 0 : b_neg;
        }
    }
}

static int decimal_addsub(const teko_decimal* a, const teko_decimal* b, int do_sub,
                          teko_decimal* out) {
    int sa = a->scale, sb = b->scale;
    int s = sa > sb ? sa : sb;
    uint64_t A[BN_WIDE], Bb[BN_WIDE];
    dec_load(a, A, BN_WIDE);
    dec_load(b, Bb, BN_WIDE);
    // Align the smaller-scale operand up to scale s.
    if (sa < s) { if (!bn_mul_pow10(A, s - sa, BN_WIDE)) { teko_decimal_zero(out); return 0; } }
    if (sb < s) { if (!bn_mul_pow10(Bb, s - sb, BN_WIDE)) { teko_decimal_zero(out); return 0; } }
    uint64_t R[BN_WIDE];
    int rneg;
    signed_combine(A, a->sign ? 1 : 0, Bb, b->sign ? 1 : 0, do_sub, R, &rneg, BN_WIDE);
    return dec_store((uint8_t)(rneg ? 1 : 0), (uint8_t)s, R, BN_WIDE, out);
}

int teko_decimal_add(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    return decimal_addsub(a, b, 0, out);
}
int teko_decimal_sub(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    return decimal_addsub(a, b, 1, out);
}

int teko_decimal_mul(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    uint64_t A[BN_WIDE], Bb[BN_WIDE], P[BN_WIDE];
    dec_load(a, A, BN_WIDE);
    dec_load(b, Bb, BN_WIDE);
    // Inputs are <= 31 significant limbs, so the product is <= 62 limbs < BN_WIDE.
    int alen = bn_len(A, BN_WIDE);
    int blen = bn_len(Bb, BN_WIDE);
    bn_mul(A, alen, Bb, blen, P);
    int s_r = (int)a->scale + (int)b->scale;
    int sign = (a->sign ^ b->sign) ? 1 : 0;
    if (s_r > TEKO_DECIMAL_MAX_SCALE) {
        int drop = s_r - TEKO_DECIMAL_MAX_SCALE;
        uint64_t Q[BN_WIDE];
        bn_round_drop_decimals(P, drop, 0, Q, BN_WIDE);
        return dec_store((uint8_t)sign, (uint8_t)TEKO_DECIMAL_MAX_SCALE, Q, BN_WIDE, out);
    }
    return dec_store((uint8_t)sign, (uint8_t)s_r, P, BN_WIDE, out);
}

// Truncated integer quotient of |a| / |b| at a given result scale (NO rounding, toward zero),
// returning the magnitude in q[2*BN_WIDE] and whether a nonzero remainder existed (sticky).
// result_scale = the scale of the quotient's value (digits to the right of the point).
//   value(a)/value(b) = (Ca/Cb) * 10^(sb - sa). To get the quotient at `result_scale`:
//   Q = floor( Ca * 10^(result_scale - sa + sb) / Cb ), with the +exp on the numerator (or,
//   if negative, on the denominator). The remainder tells us if it was exact.
static int decimal_quotient_trunc(const teko_decimal* a, const teko_decimal* b,
                                  int result_scale, uint64_t* q, int* sticky) {
    uint64_t Ca[BN_WIDE], Cb[BN_WIDE];
    dec_load(a, Ca, BN_WIDE);
    dec_load(b, Cb, BN_WIDE);
    if (bn_is_zero(Cb, BN_WIDE)) return 0; // div by zero
    int exp = result_scale - (int)a->scale + (int)b->scale;
    uint64_t num[BN_WIDE], den[BN_WIDE];
    for (int i = 0; i < BN_WIDE; i++) { num[i] = Ca[i]; den[i] = Cb[i]; }
    if (exp > 0) {
        if (!bn_mul_pow10(num, exp, BN_WIDE)) return 0; // numerator overflow -> fail-loud
    } else if (exp < 0) {
        if (!bn_mul_pow10(den, -exp, BN_WIDE)) return 0;
    }
    uint64_t r[BN_WIDE];
    if (!bn_divmod(num, BN_WIDE, den, BN_WIDE, q, r)) return 0;
    *sticky = bn_is_zero(r, BN_WIDE) ? 0 : 1;
    return 1;
}

int teko_decimal_div(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    uint64_t Cb[BN_WIDE];
    dec_load(b, Cb, BN_WIDE);
    if (bn_is_zero(Cb, BN_WIDE)) { teko_decimal_zero(out); return 0; }
    int sign = (a->sign ^ b->sign) ? 1 : 0;
    // Compute the quotient to scale 38 with ONE extra guard digit (scale 39), then banker's-
    // round that guard digit away (the remainder makes the guard boundary sticky).
    int guard_scale = TEKO_DECIMAL_MAX_SCALE + 1;
    uint64_t q[BN_WIDE];
    int sticky = 0;
    if (!decimal_quotient_trunc(a, b, guard_scale, q, &sticky)) { teko_decimal_zero(out); return 0; }
    uint64_t rounded[BN_WIDE];
    bn_round_drop_decimals(q, 1, sticky, rounded, BN_WIDE); // drop the 1 guard digit
    return dec_store((uint8_t)sign, (uint8_t)TEKO_DECIMAL_MAX_SCALE, rounded, BN_WIDE, out);
}

int teko_decimal_mod(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    uint64_t Cb[BN_WIDE];
    dec_load(b, Cb, BN_WIDE);
    if (bn_is_zero(Cb, BN_WIDE)) { teko_decimal_zero(out); return 0; }
    // Python Decimal.__mod__: a - (a // b) * b, // truncates toward zero.
    // 1) t = trunc(a/b) as an INTEGER (scale 0), magnitude in qmag, sign = sa^sb.
    uint64_t qmag[BN_WIDE];
    int sticky = 0;
    if (!decimal_quotient_trunc(a, b, 0, qmag, &sticky)) { teko_decimal_zero(out); return 0; }
    int tsign = (a->sign ^ b->sign) ? 1 : 0;
    teko_decimal t;
    if (!dec_store((uint8_t)tsign, 0, qmag, BN_WIDE, &t)) { teko_decimal_zero(out); return 0; }
    // 2) prod = t * b   (exact)
    teko_decimal prod;
    if (!teko_decimal_mul(&t, b, &prod)) { teko_decimal_zero(out); return 0; }
    // 3) result = a - prod   (exact)
    if (!teko_decimal_sub(a, &prod, out)) { teko_decimal_zero(out); return 0; }
    // Sign of a nonzero result must equal sign of a (it already does by construction, but
    // canonicalize: a - trunc(a/b)*b has the sign of a when nonzero). dec_store already
    // canonicalized zero's sign; nothing more to do.
    return 1;
}

int teko_decimal_cmp(const teko_decimal* a, const teko_decimal* b, int* out_lt_eq_gt) {
    int za = bn_is_zero(a->limb, TEKO_DECIMAL_LIMBS);
    int zb = bn_is_zero(b->limb, TEKO_DECIMAL_LIMBS);
    int sa = za ? 0 : (a->sign ? 1 : 0);  // +0 == -0
    int sb = zb ? 0 : (b->sign ? 1 : 0);
    if (za && zb) { *out_lt_eq_gt = 0; return 1; }
    if (sa != sb) { *out_lt_eq_gt = sa ? -1 : 1; return 1; } // negative < positive
    // Same sign: compare magnitudes aligned to common scale in a wide temp.
    int s = a->scale > b->scale ? a->scale : b->scale;
    uint64_t A[BN_WIDE], Bb[BN_WIDE];
    dec_load(a, A, BN_WIDE);
    dec_load(b, Bb, BN_WIDE);
    // Alignment into the wide temp; on the off-chance it overflows BN_WIDE (it cannot for
    // valid 1984-bit coeffs scaled by <=38), treat the larger raw magnitude as bigger.
    if ((int)a->scale < s) bn_mul_pow10(A, s - (int)a->scale, BN_WIDE);
    if ((int)b->scale < s) bn_mul_pow10(Bb, s - (int)b->scale, BN_WIDE);
    int c = bn_cmp(A, Bb, BN_WIDE);
    // For negatives, the larger magnitude is the smaller value.
    if (sa) c = -c;
    *out_lt_eq_gt = c;
    return 1;
}
