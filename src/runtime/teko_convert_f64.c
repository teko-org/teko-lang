// =====================================================================================
// teko_convert_f64.c — Phase 17.C: shortest-round-trip f64 -> string formatter.
//
// The single C source of truth for `convert.float_to_str` (runtime id 50, surfaced in
// 17.D). KAT-tested in the Unity suite, compiled identically into the native teko_rt
// archive AND the wasm32 reactor (freestanding) so native output == WASM output.
//
// -------------------------------------------------------------------------------------
// ALGORITHM: Ryu (shortest round-trip).
//
//   The shortest-decimal core (`d2d` + its helpers `umul128`/`shiftright128`/
//   `mulShift64`/`mulShiftAll64`/`pow5Factor`/`multipleOfPowerOf5`/`log10Pow2`/
//   `log10Pow5`/`pow5bits`/`div5..div1e8`, the power-of-5 split tables, and the
//   `d2d_small_int` fast path) is transcribed from the reference implementation:
//
//       github.com/ulfjack/ryu  (ryu/d2s.c, ryu/d2s_intrinsics.h, ryu/common.h,
//       ryu/digit_table.h, ryu/d2s_full_table.h)
//       Ulf Adams, "Ryu: fast float-to-string conversion", PLDI 2018.
//
//   The reference is dual-licensed Apache License 2.0 OR Boost Software License 1.0.
//   This transcription is used under those terms WITH attribution; it is NOT placed in
//   the public domain (the project transcribes RFCs/papers/reference code with
//   attribution — same posture as the crypto runtimes). The power-of-5 tables live in
//   teko_convert_f64_tables.h, reproduced byte-for-byte from the reference.
//
//   PORTABLE 64-bit-only path (HARD constraint): we DO NOT use `__int128` and DO NOT
//   emit any 128-bit compiler libcall (`__multi3`/`__udivti3`). We always take Ryu's
//   `!HAS_UINT128 && !HAS_64_BIT_INTRINSICS` branch: the 64x64->128 product is computed
//   from 32-bit halves in `umul128`, and the 128-bit shift in `shiftright128`. This is
//   MSVC-safe portable C (no GCC/clang-only intrinsics, no C23 auto/nullptr, no VLAs),
//   so the file compiles unchanged on the native libc targets, the Windows MSVC gate,
//   and `--target=wasm32 -ffreestanding -nostdlib`.
//
// -------------------------------------------------------------------------------------
// RENDERER (Teko output policy — NOT Ryu's scientific `to_chars`).
//
//   Ryu yields the shortest decimal {sign, digits (uint64 `output`), decimal exponent}.
//   A CUSTOM renderer turns that into Teko's culture-invariant string:
//
//   * Plain `.`-decimal: ASCII digits, '.' separator (never ',' / locale), leading '-'
//     for negatives, NO digit grouping.
//   * ALWAYS at least one fractional digit, so a float reads as a float: whole values
//     render with a trailing ".0"  (1.0, 42.0, 100.0 — matching the owner's `1.0` KAT,
//     NOT "1").
//   * Specials:  NaN -> "NaN";  +Inf -> "Infinity";  -Inf -> "-Infinity";
//                +0.0 -> "0.0";  -0.0 -> "-0.0"  (sign preserved, round-trips).
//   * SHORTEST digits (Ryu's output) — never padded to 17.
//   * `e`-notation only at EXTREME exponents. Let e10 be the exponent of the value in
//     normalized d.ddd x 10^e10 form (i.e. e10 = (decimal-point exponent) =
//     ryuExponent + numDigits - 1). We use SCIENTIFIC when  e10 < -4  OR  e10 >= 21,
//     and PLAIN decimal otherwise. This mirrors ECMAScript Number.prototype.toString
//     (well-understood, keeps 1e-4 .. 1e20 in plain form). Scientific form is
//     `d[.ddd]e{+|-}NN`: lowercase 'e', explicit sign, exponent printed with no leading
//     zeros (and, for consistency with the float-reads-as-float rule, with a fractional
//     part in the significand: e.g. "5e-324", "1.7976931348623157e308").
//     Examples: 100.0 -> "100.0" (plain); 1e21 -> "1e+21" (sci); 5e-324 -> "5e-324"
//     (sci); 1.7976931348623157e308 -> "1.7976931348623157e+308" (sci).
//
//   The result is a fresh malloc'd NUL-terminated buffer the caller owns (free), like
//   the rest of teko_convert.c. Returns NULL on OOM.
//
//   Pure / deterministic: no global mutable state, no clock, no rand — so native and
//   WASM produce byte-identical output.
// =====================================================================================

#include "teko_convert.h"
#include "teko_convert_f64_tables.h"

#include <stdint.h>
#include <stdlib.h>  // malloc
#include <string.h>  // memcpy

// We deliberately avoid <stdbool.h> assumptions in shared code; use plain int flags.

// --- IEEE-754 binary64 layout --------------------------------------------------------
#define TEKO_D_MANTISSA_BITS 52
#define TEKO_D_EXPONENT_BITS 11
#define TEKO_D_BIAS 1023

// --- bit reinterpret (no aliasing UB; memcpy is the portable idiom) ------------------
static uint64_t teko_double_to_bits(double d) {
    uint64_t bits = 0;
    memcpy(&bits, &d, sizeof(double));
    return bits;
}

// --- portable 64x64 -> 128 product + 128-bit shift (NO __int128) ---------------------
// Transcribed from ryu/d2s_intrinsics.h (the !HAS_UINT128 && !HAS_64_BIT_INTRINSICS
// branch). Computes the full 128-bit product of a and b via 32-bit halves; returns the
// low 64 bits, writes the high 64 bits through productHi.
static uint64_t teko_umul128(const uint64_t a, const uint64_t b, uint64_t* const productHi) {
    const uint32_t aLo = (uint32_t)a;
    const uint32_t aHi = (uint32_t)(a >> 32);
    const uint32_t bLo = (uint32_t)b;
    const uint32_t bHi = (uint32_t)(b >> 32);

    const uint64_t b00 = (uint64_t)aLo * bLo;
    const uint64_t b01 = (uint64_t)aLo * bHi;
    const uint64_t b10 = (uint64_t)aHi * bLo;
    const uint64_t b11 = (uint64_t)aHi * bHi;

    const uint32_t b00Lo = (uint32_t)b00;
    const uint32_t b00Hi = (uint32_t)(b00 >> 32);

    const uint64_t mid1 = b10 + b00Hi;
    const uint32_t mid1Lo = (uint32_t)(mid1);
    const uint32_t mid1Hi = (uint32_t)(mid1 >> 32);

    const uint64_t mid2 = b01 + mid1Lo;
    const uint32_t mid2Lo = (uint32_t)(mid2);
    const uint32_t mid2Hi = (uint32_t)(mid2 >> 32);

    const uint64_t pHi = b11 + (uint64_t)mid1Hi + (uint64_t)mid2Hi;
    const uint64_t pLo = ((uint64_t)mid2Lo << 32) | b00Lo;

    *productHi = pHi;
    return pLo;
}

// Returns the lower 64 bits of (hi*2^64 + lo) >> dist, with 0 < dist < 64.
static uint64_t teko_shiftright128(const uint64_t lo, const uint64_t hi, const uint32_t dist) {
    // 0 < dist < 64 always holds in d2d (j-64 / j-64-1 land in [49,58] / [2,59]).
    return (hi << (64 - dist)) | (lo >> dist);
}

// --- 64-bit constant divisions (plain C; the compiler turns these into mul+shift on a
//     64-bit target with NO 128-bit libcall) ----------------------------------------
static uint64_t teko_div5(const uint64_t x)   { return x / 5; }
static uint64_t teko_div10(const uint64_t x)  { return x / 10; }
static uint64_t teko_div100(const uint64_t x) { return x / 100; }
static uint64_t teko_div1e8(const uint64_t x) { return x / 100000000ull; }

// --- power-of-5 / log helpers (ryu/common.h, ryu/d2s_intrinsics.h) -------------------
static int32_t teko_log2pow5(const int32_t e) {
    return (int32_t)((((uint32_t)e) * 1217359u) >> 19);
}
static int32_t teko_pow5bits(const int32_t e) {
    return (int32_t)(((((uint32_t)e) * 1217359u) >> 19) + 1);
}
static uint32_t teko_log10Pow2(const int32_t e) {
    return (((uint32_t)e) * 78913u) >> 18;
}
static uint32_t teko_log10Pow5(const int32_t e) {
    return (((uint32_t)e) * 732923u) >> 20;
}

static uint32_t teko_pow5Factor(uint64_t value) {
    const uint64_t m_inv_5 = 14757395258967641293ull; // 5 * m_inv_5 = 1 (mod 2^64)
    const uint64_t n_div_5 = 3689348814741910323ull;  // 2^64 / 5
    uint32_t count = 0;
    for (;;) {
        value *= m_inv_5;
        if (value > n_div_5) break;
        ++count;
    }
    return count;
}
// Returns 1 if value is divisible by 5^p.
static int teko_multipleOfPowerOf5(const uint64_t value, const uint32_t p) {
    return teko_pow5Factor(value) >= p;
}
// Returns 1 if value is divisible by 2^p (p < 64).
static int teko_multipleOfPowerOf2(const uint64_t value, const uint32_t p) {
    return (value & ((1ull << p) - 1)) == 0;
}

// --- mulShift64 / mulShiftAll64 (portable 64-bit path) -------------------------------
static uint64_t teko_mulShift64(const uint64_t m, const uint64_t* const mul, const int32_t j) {
    uint64_t high1;                                        // 128
    const uint64_t low1 = teko_umul128(m, mul[1], &high1); // 64
    uint64_t high0;                                        // 64
    teko_umul128(m, mul[0], &high0);                       // 0
    const uint64_t sum = high0 + low1;
    if (sum < high0) {
        ++high1; // overflow into high1
    }
    return teko_shiftright128(sum, high1, (uint32_t)(j - 64));
}

// The "faster if we don't have a 64x64->128 multiplication" variant (ryu portable path).
static uint64_t teko_mulShiftAll64(uint64_t m, const uint64_t* const mul, const int32_t j,
                                   uint64_t* const vp, uint64_t* const vm, const uint32_t mmShift) {
    m <<= 1;
    uint64_t tmp;
    const uint64_t lo = teko_umul128(m, mul[0], &tmp);
    uint64_t hi;
    const uint64_t mid = tmp + teko_umul128(m, mul[1], &hi);
    hi += (mid < tmp); // overflow into hi

    const uint64_t lo2 = lo + mul[0];
    const uint64_t mid2 = mid + mul[1] + (lo2 < lo);
    const uint64_t hi2 = hi + (mid2 < mid);
    *vp = teko_shiftright128(mid2, hi2, (uint32_t)(j - 64 - 1));

    if (mmShift == 1) {
        const uint64_t lo3 = lo - mul[0];
        const uint64_t mid3 = mid - mul[1] - (lo3 > lo);
        const uint64_t hi3 = hi - (mid3 > mid);
        *vm = teko_shiftright128(mid3, hi3, (uint32_t)(j - 64 - 1));
    } else {
        const uint64_t lo3 = lo + lo;
        const uint64_t mid3 = mid + mid + (lo3 < lo);
        const uint64_t hi3 = hi + hi + (mid3 < mid);
        const uint64_t lo4 = lo3 - mul[0];
        const uint64_t mid4 = mid3 - mul[1] - (lo4 > lo3);
        const uint64_t hi4 = hi3 - (mid4 > mid3);
        *vm = teko_shiftright128(mid4, hi4, (uint32_t)(j - 64));
    }

    return teko_shiftright128(mid, hi, (uint32_t)(j - 64 - 1));
}

// --- decimal length of a 17-digit-or-fewer value -------------------------------------
static uint32_t teko_decimalLength17(const uint64_t v) {
    if (v >= 10000000000000000ull) { return 17; }
    if (v >= 1000000000000000ull)  { return 16; }
    if (v >= 100000000000000ull)   { return 15; }
    if (v >= 10000000000000ull)    { return 14; }
    if (v >= 1000000000000ull)     { return 13; }
    if (v >= 100000000000ull)      { return 12; }
    if (v >= 10000000000ull)       { return 11; }
    if (v >= 1000000000ull)        { return 10; }
    if (v >= 100000000ull)         { return 9; }
    if (v >= 10000000ull)          { return 8; }
    if (v >= 1000000ull)           { return 7; }
    if (v >= 100000ull)            { return 6; }
    if (v >= 10000ull)             { return 5; }
    if (v >= 1000ull)              { return 4; }
    if (v >= 100ull)               { return 3; }
    if (v >= 10ull)                { return 2; }
    return 1;
}

// A floating decimal representing mantissa * 10^exponent.
typedef struct teko_floating_decimal_64 {
    uint64_t mantissa;
    int32_t exponent;
} teko_floating_decimal_64;

// --- the Ryu shortest core (transcribed from d2s.c::d2d) -----------------------------
static teko_floating_decimal_64 teko_d2d(const uint64_t ieeeMantissa, const uint32_t ieeeExponent) {
    int32_t e2;
    uint64_t m2;
    if (ieeeExponent == 0) {
        e2 = 1 - TEKO_D_BIAS - TEKO_D_MANTISSA_BITS - 2;
        m2 = ieeeMantissa;
    } else {
        e2 = (int32_t)ieeeExponent - TEKO_D_BIAS - TEKO_D_MANTISSA_BITS - 2;
        m2 = (1ull << TEKO_D_MANTISSA_BITS) | ieeeMantissa;
    }
    const int even = (m2 & 1) == 0;
    const int acceptBounds = even;

    // Step 2: interval of valid decimal representations.
    const uint64_t mv = 4 * m2;
    const uint32_t mmShift = (ieeeMantissa != 0 || ieeeExponent <= 1) ? 1u : 0u;

    // Step 3: convert to decimal power base using 128-bit arithmetic.
    uint64_t vr, vp, vm;
    int32_t e10;
    int vmIsTrailingZeros = 0;
    int vrIsTrailingZeros = 0;
    if (e2 >= 0) {
        const uint32_t q = teko_log10Pow2(e2) - (e2 > 3 ? 1u : 0u);
        e10 = (int32_t)q;
        const int32_t k = TEKO_DOUBLE_POW5_INV_BITCOUNT + teko_pow5bits((int32_t)q) - 1;
        const int32_t i = -e2 + (int32_t)q + k;
        vr = teko_mulShiftAll64(m2, TEKO_DOUBLE_POW5_INV_SPLIT[q], i, &vp, &vm, mmShift);
        if (q <= 21) {
            const uint32_t mvMod5 = ((uint32_t)mv) - 5 * ((uint32_t)teko_div5(mv));
            if (mvMod5 == 0) {
                vrIsTrailingZeros = teko_multipleOfPowerOf5(mv, q);
            } else if (acceptBounds) {
                vmIsTrailingZeros = teko_multipleOfPowerOf5(mv - 1 - mmShift, q);
            } else {
                vp -= teko_multipleOfPowerOf5(mv + 2, q) ? 1u : 0u;
            }
        }
    } else {
        const uint32_t q = teko_log10Pow5(-e2) - ((-e2) > 1 ? 1u : 0u);
        e10 = (int32_t)q + e2;
        const int32_t i = -e2 - (int32_t)q;
        const int32_t k = teko_pow5bits(i) - TEKO_DOUBLE_POW5_BITCOUNT;
        const int32_t j = (int32_t)q - k;
        vr = teko_mulShiftAll64(m2, TEKO_DOUBLE_POW5_SPLIT[i], j, &vp, &vm, mmShift);
        if (q <= 1) {
            vrIsTrailingZeros = 1;
            if (acceptBounds) {
                vmIsTrailingZeros = (mmShift == 1);
            } else {
                --vp;
            }
        } else if (q < 63) {
            vrIsTrailingZeros = teko_multipleOfPowerOf2(mv, q);
        }
    }

    // Step 4: find the shortest decimal in the interval.
    int32_t removed = 0;
    uint8_t lastRemovedDigit = 0;
    uint64_t output;
    if (vmIsTrailingZeros || vrIsTrailingZeros) {
        // General (rare) case.
        for (;;) {
            const uint64_t vpDiv10 = teko_div10(vp);
            const uint64_t vmDiv10 = teko_div10(vm);
            if (vpDiv10 <= vmDiv10) {
                break;
            }
            const uint32_t vmMod10 = ((uint32_t)vm) - 10 * ((uint32_t)vmDiv10);
            const uint64_t vrDiv10 = teko_div10(vr);
            const uint32_t vrMod10 = ((uint32_t)vr) - 10 * ((uint32_t)vrDiv10);
            vmIsTrailingZeros &= (vmMod10 == 0);
            vrIsTrailingZeros &= (lastRemovedDigit == 0);
            lastRemovedDigit = (uint8_t)vrMod10;
            vr = vrDiv10;
            vp = vpDiv10;
            vm = vmDiv10;
            ++removed;
        }
        if (vmIsTrailingZeros) {
            for (;;) {
                const uint64_t vmDiv10 = teko_div10(vm);
                const uint32_t vmMod10 = ((uint32_t)vm) - 10 * ((uint32_t)vmDiv10);
                if (vmMod10 != 0) {
                    break;
                }
                const uint64_t vpDiv10 = teko_div10(vp);
                const uint64_t vrDiv10 = teko_div10(vr);
                const uint32_t vrMod10 = ((uint32_t)vr) - 10 * ((uint32_t)vrDiv10);
                vrIsTrailingZeros &= (lastRemovedDigit == 0);
                lastRemovedDigit = (uint8_t)vrMod10;
                vr = vrDiv10;
                vp = vpDiv10;
                vm = vmDiv10;
                ++removed;
            }
        }
        if (vrIsTrailingZeros && lastRemovedDigit == 5 && vr % 2 == 0) {
            // Round even if the exact number is .....50..0.
            lastRemovedDigit = 4;
        }
        output = vr + (((vr == vm && (!acceptBounds || !vmIsTrailingZeros)) || lastRemovedDigit >= 5) ? 1u : 0u);
    } else {
        // Common case (~99.3%).
        int roundUp = 0;
        const uint64_t vpDiv100 = teko_div100(vp);
        const uint64_t vmDiv100 = teko_div100(vm);
        if (vpDiv100 > vmDiv100) {
            const uint64_t vrDiv100 = teko_div100(vr);
            const uint32_t vrMod100 = ((uint32_t)vr) - 100 * ((uint32_t)vrDiv100);
            roundUp = (vrMod100 >= 50);
            vr = vrDiv100;
            vp = vpDiv100;
            vm = vmDiv100;
            removed += 2;
        }
        for (;;) {
            const uint64_t vpDiv10 = teko_div10(vp);
            const uint64_t vmDiv10 = teko_div10(vm);
            if (vpDiv10 <= vmDiv10) {
                break;
            }
            const uint64_t vrDiv10 = teko_div10(vr);
            const uint32_t vrMod10 = ((uint32_t)vr) - 10 * ((uint32_t)vrDiv10);
            roundUp = (vrMod10 >= 5);
            vr = vrDiv10;
            vp = vpDiv10;
            vm = vmDiv10;
            ++removed;
        }
        output = vr + ((vr == vm || roundUp) ? 1u : 0u);
    }
    const int32_t exp = e10 + removed;

    teko_floating_decimal_64 fd;
    fd.exponent = exp;
    fd.mantissa = output;
    return fd;
}

// Fast path for small integers (transcribed from d2s.c::d2d_small_int). Returns 1 and
// fills *v on success, 0 otherwise.
static int teko_d2d_small_int(const uint64_t ieeeMantissa, const uint32_t ieeeExponent,
                              teko_floating_decimal_64* const v) {
    const uint64_t m2 = (1ull << TEKO_D_MANTISSA_BITS) | ieeeMantissa;
    const int32_t e2 = (int32_t)ieeeExponent - TEKO_D_BIAS - TEKO_D_MANTISSA_BITS;

    if (e2 > 0) {
        return 0; // f >= 2^53 is an integer; handled by d2d.
    }
    if (e2 < -52) {
        return 0; // f < 1.
    }
    const uint64_t mask = (1ull << -e2) - 1;
    const uint64_t fraction = m2 & mask;
    if (fraction != 0) {
        return 0;
    }
    v->mantissa = m2 >> -e2;
    v->exponent = 0;
    return 1;
}

// --- digit extraction into a small fixed buffer --------------------------------------
// Writes the decimal digits of `output` (most-significant first) into `digits`, returns
// the count. `output` has at most 17 digits.
static uint32_t teko_extract_digits(uint64_t output, char* digits) {
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + (int)(output % 10));
        output /= 10;
    } while (output != 0);
    // reverse into digits[]
    for (int k = 0; k < n; k++) {
        digits[k] = tmp[n - 1 - k];
    }
    return (uint32_t)n;
}

// =====================================================================================
// Public entry: Teko culture-invariant shortest-round-trip f64 -> string.
// =====================================================================================
char* teko_convert_f64_to_string(double v) {
    const uint64_t bits = teko_double_to_bits(v);
    const int ieeeSign = (int)((bits >> (TEKO_D_MANTISSA_BITS + TEKO_D_EXPONENT_BITS)) & 1u);
    const uint64_t ieeeMantissa = bits & ((1ull << TEKO_D_MANTISSA_BITS) - 1);
    const uint32_t ieeeExponent =
        (uint32_t)((bits >> TEKO_D_MANTISSA_BITS) & ((1u << TEKO_D_EXPONENT_BITS) - 1u));

    // --- specials --------------------------------------------------------------------
    if (ieeeExponent == ((1u << TEKO_D_EXPONENT_BITS) - 1u)) {
        // NaN (any mantissa) or +/-Infinity.
        const char* s;
        if (ieeeMantissa != 0) {
            s = "NaN";
        } else {
            s = ieeeSign ? "-Infinity" : "Infinity";
        }
        size_t len = strlen(s) + 1;
        char* out = (char*)malloc(len);
        if (!out) return NULL;
        memcpy(out, s, len);
        return out;
    }
    if (ieeeExponent == 0 && ieeeMantissa == 0) {
        // +/-0.0 — sign preserved so it round-trips.
        const char* s = ieeeSign ? "-0.0" : "0.0";
        size_t len = strlen(s) + 1;
        char* out = (char*)malloc(len);
        if (!out) return NULL;
        memcpy(out, s, len);
        return out;
    }

    // --- shortest decimal {digits, exponent} -----------------------------------------
    teko_floating_decimal_64 fd;
    if (!teko_d2d_small_int(ieeeMantissa, ieeeExponent, &fd)) {
        fd = teko_d2d(ieeeMantissa, ieeeExponent);
    } else {
        // Move trailing decimal zeros into the exponent (e.g. 100 -> 1e2) so the
        // digit string is canonical/shortest for the renderer's e10 decision.
        for (;;) {
            const uint64_t q = teko_div10(fd.mantissa);
            const uint32_t r = ((uint32_t)fd.mantissa) - 10 * ((uint32_t)q);
            if (r != 0) {
                break;
            }
            fd.mantissa = q;
            ++fd.exponent;
        }
    }

    // digits[] = shortest significand digits (MSD first); nd = count (1..17).
    char digits[20];
    const uint32_t nd = teko_extract_digits(fd.mantissa, digits);

    // e10 = exponent of the value in normalized  d.ddd x 10^e10  form.
    // value = digits x 10^fd.exponent, and digits has nd digits, so the leading digit
    // sits at place (fd.exponent + nd - 1).
    const int32_t e10 = fd.exponent + (int32_t)nd - 1;

    // Choose plain vs scientific (documented policy: sci iff e10 < -4 OR e10 >= 21).
    const int useSci = (e10 < -4) || (e10 >= 21);

    // Worst-case buffer: sign + 17 digits + '.' + 'e' + sign + 3 exp digits + NUL.
    // Plain form can need leading "0." + up to 4 zeros, or trailing zeros up to e10.
    // 64 bytes is comfortably enough for every double under this policy.
    char buf[64];
    int p = 0;
    if (ieeeSign) {
        buf[p++] = '-';
    }

    if (useSci) {
        // d[.ddd]e{+|-}NN
        buf[p++] = digits[0];
        if (nd > 1) {
            buf[p++] = '.';
            for (uint32_t k = 1; k < nd; k++) {
                buf[p++] = digits[k];
            }
        }
        buf[p++] = 'e';
        int32_t expo = e10;
        if (expo < 0) {
            buf[p++] = '-';
            expo = -expo;
        } else {
            buf[p++] = '+';
        }
        // exponent, no leading zeros (range here is roughly 5..324)
        char ed[4];
        int en = 0;
        do {
            ed[en++] = (char)('0' + (expo % 10));
            expo /= 10;
        } while (expo != 0);
        while (en > 0) {
            buf[p++] = ed[--en];
        }
    } else if (e10 < 0) {
        // 0.00ddd  — leading-zero fractional form. e10 in [-4 .. -1].
        buf[p++] = '0';
        buf[p++] = '.';
        // number of zeros between '.' and the first significant digit = (-e10 - 1)
        for (int32_t z = 0; z < (-e10 - 1); z++) {
            buf[p++] = '0';
        }
        for (uint32_t k = 0; k < nd; k++) {
            buf[p++] = digits[k];
        }
    } else {
        // Plain decimal with the point at position (e10+1) from the left.
        // intDigits = e10 + 1 integer digits.
        const int32_t intDigits = e10 + 1;
        if ((int32_t)nd <= intDigits) {
            // All significand digits are in the integer part; pad with zeros, then ".0".
            for (uint32_t k = 0; k < nd; k++) {
                buf[p++] = digits[k];
            }
            for (int32_t z = 0; z < intDigits - (int32_t)nd; z++) {
                buf[p++] = '0';
            }
            buf[p++] = '.';
            buf[p++] = '0';
        } else {
            // Point falls inside the significand digits.
            for (int32_t k = 0; k < intDigits; k++) {
                buf[p++] = digits[k];
            }
            buf[p++] = '.';
            for (uint32_t k = (uint32_t)intDigits; k < nd; k++) {
                buf[p++] = digits[k];
            }
        }
    }

    buf[p] = '\0';
    char* out = (char*)malloc((size_t)p + 1);
    if (!out) return NULL;
    memcpy(out, buf, (size_t)p + 1);
    return out;
}

// =====================================================================================
// Phase 17.E — CHECKED, correctly-rounded, freestanding decimal string -> f64 parser.
//
// ALGORITHM: "simple decimal conversion" (David Gay / Go `strconv` decimal-shift style).
//
//   The input is parsed into a fixed-capacity DECIMAL-DIGIT buffer with a decimal-point
//   position (`dp`) and a `truncated` flag (set if more than the buffer's worth of
//   significant digits were seen). The arbitrary-precision decimal is then SHIFTED toward
//   a binary value: while the value is too large for the f64 mantissa range we divide the
//   decimal by 2 (incrementing the binary exponent), and while it is too small we multiply
//   the decimal by 2 (decrementing it) — both implemented as exact decimal long-shift over
//   the digit buffer (`decimal_shift`). Once the value is in [2^52, 2^53), we round the
//   fractional bit off with ROUND-HALF-TO-EVEN, then assemble the IEEE-754 binary64 fields.
//
//   This is the SAME family as Go's `strconv/decimal.go` + `atof.go` slow path. It is
//   self-contained (NO big power-of-5 / power-of-10 table — distinct from Ryu's tables and
//   from an Eisel-Lemire/Clinger fast-path table), chosen for COMPACTNESS + obviously
//   correct rounding. It is the source of truth: a Clinger-style fast path is NOT used
//   here (the round-trip KAT against the 17.C formatter is the correctness safety net).
//
//   FREESTANDING (HARD constraint, identical to the formatter above): no `strtod`/`math.h`/
//   `setlocale`/`__int128`, no FP arithmetic in the conversion (the result is built from
//   the integer mantissa + a single `ldexp`-free bit assembly). Only mem*/plain integer ops.
//
//   ACCEPTED GRAMMAR (culture-invariant; matches the formatter's plain + scientific output):
//     ws* [+|-] ( D+ [. D*] | . D+ ) ( [eE] [+|-] D+ )? ws*       (D = ASCII digit)
//   REJECTED (returns 0, *out untouched): empty, non-numeric, trailing junk, lone '.'/'e',
//   a sign with no digits, AND `NaN`/`Infinity` (strict numeric parse, matching 16.F). This
//   means parse(format(x)) is NOT defined for x in {NaN, ±Inf} — by design (those are not
//   finite values; a strict numeric input must reject them, fail-loud at the surface).
//
//   RANGE: overflow (|value| would round to +Inf, i.e. binary exponent past the f64 max)
//   is FAIL-LOUD (return 0), exactly as 16.F rejects integer overflow. Underflow to a
//   subnormal or to ±0.0 is representable and therefore SUCCEEDS (not an error). The sign of
//   zero is preserved ("-0.0" -> -0.0) so it round-trips with the formatter.
// =====================================================================================

// Capacity: enough significant decimal digits to round any f64 correctly. A binary64 needs
// at most 767 significant decimal digits to distinguish the hardest subnormal; Go uses 800.
#define TEKO_DEC_DIGITS 800

typedef struct teko_dec {
    uint8_t d[TEKO_DEC_DIGITS]; // significant decimal digits, MSD first, value in 0..9
    int     nd;                 // number of digits in d[]
    int     dp;                 // decimal point: value = 0.d[0]d[1]... x 10^dp (d[0] is 10^(dp-1))
    int     neg;                // sign
    int     truncated;         // 1 if digits were dropped past TEKO_DEC_DIGITS (rounding hint)
} teko_dec;

// Maximum bits we shift per step. <= 60 keeps the digit-accumulator (`n*10 + d`, with
// `n < 2^k * something`) inside u64 without overflow.
#define TEKO_MAX_SHIFT 57

// Trim trailing zero digits (they don't affect the value; keeps nd minimal).
static void teko_dec_trim(teko_dec* a) {
    while (a->nd > 0 && a->d[a->nd - 1] == 0) a->nd--;
    if (a->nd == 0) a->dp = 0;
}

// Drop LEADING zero digits, lowering dp by one each (value unchanged: a leading 0 in
// d[0].d[1]...x10^dp means the true leading digit is one place lower). Needed after a left
// shift whose digit-growth upper bound over-allocated by one leading position.
static void teko_dec_strip_leading(teko_dec* a) {
    int z = 0;
    while (z < a->nd && a->d[z] == 0) z++;
    if (z == 0) return;
    for (int i = z; i < a->nd; i++) a->d[i - z] = a->d[i];
    a->nd -= z;
    a->dp -= z;
    if (a->nd == 0) a->dp = 0;
}

// value /= 2^k (right shift), 0 < k <= TEKO_MAX_SHIFT. Long-division of the decimal digit
// string by 2^k (exact). Transcribed from Go's strconv/decimal.go rightShift (adapted to the
// fixed buffer): reads digits MSD-first, carrying the remainder; result digits are written in
// place. dp grows by however many leading quotient digits are zero (handled by re-trimming /
// the read-ahead). Digits past the buffer set `truncated`.
static void teko_dec_shift_right(teko_dec* a, uint32_t k) {
    int r = 0;          // read index into a->d
    int w = 0;          // write index
    uint64_t n = 0;     // running dividend accumulator
    // Pull leading digits until the first quotient digit (n >> k) is nonzero. (Transcribed
    // verbatim from Go strconv/decimal.go rightShift; r counts consumed positions.)
    for (; (n >> k) == 0; r++) {
        if (r >= a->nd) {
            if (n == 0) { a->nd = 0; return; } // exact zero
            // Ran out of input digits but n != 0: keep multiplying (implicit trailing zeros)
            // until a quotient digit appears, counting the consumed positions in r.
            while ((n >> k) == 0) { n *= 10; r++; }
            break;
        }
        n = n * 10 + a->d[r];
    }
    a->dp -= (r - 1);
    uint64_t mask = ((uint64_t)1 << k) - 1;
    // Pass 1: while real input digits remain.
    for (; r < a->nd; r++) {
        uint8_t dig = (uint8_t)(n >> k);
        n &= mask;
        if (w < TEKO_DEC_DIGITS) { a->d[w] = dig; w++; }
        else if (dig != 0) { a->truncated = 1; }
        n = n * 10 + a->d[r];
    }
    // Pass 2: drain the remaining fraction (implicit trailing zeros).
    while (n > 0) {
        uint8_t dig = (uint8_t)(n >> k);
        n &= mask;
        if (w < TEKO_DEC_DIGITS) { a->d[w] = dig; w++; }
        else if (dig != 0) { a->truncated = 1; }
        n = n * 10;
    }
    a->nd = w;
    teko_dec_trim(a);
}

// value *= 2^k (left shift), 0 < k <= TEKO_MAX_SHIFT. Transcribed from Go's decimal.go
// leftShift: process digits LSD-first with a carry; the result grows by `delta` leading
// digits where delta = floor(k*log10(2)) or that+1 (decided by comparing the high digits to
// the shift's prefix). We size by the upper bound ceil(k*log10(2)) and let leading zeros trim.
static int teko_left_grow_ub(uint32_t k) {
    long num = (long)k * 30103L;       // k * log10(2) * 1e5, slightly OVER the true value
    int g = (int)(num / 100000L);
    if (num % 100000L != 0) g++;       // ceil
    return g;                          // strict upper bound on added leading digits
}
static void teko_dec_shift_left(teko_dec* a, uint32_t k) {
    int delta = teko_left_grow_ub(k);
    int rd = a->nd - 1;                 // read index (LSD-first)
    int wr = a->nd - 1 + delta;         // write index (LSD-first), result is nd+delta wide
    uint64_t n = 0;
    while (rd >= 0) {
        n += ((uint64_t)a->d[rd]) << k;
        uint64_t quo = n / 10;
        uint64_t rem = n - quo * 10;
        if (wr < TEKO_DEC_DIGITS) {
            a->d[wr] = (uint8_t)rem;
        } else if (rem != 0) {
            a->truncated = 1;
        }
        wr--; rd--;
        n = quo;
    }
    while (n > 0) {
        uint64_t quo = n / 10;
        uint64_t rem = n - quo * 10;
        if (wr < TEKO_DEC_DIGITS) {
            a->d[wr] = (uint8_t)rem;
        } else if (rem != 0) {
            a->truncated = 1;
        }
        wr--;
        n = quo;
    }
    // `delta` is an UPPER bound on the digit growth; if the actual growth was smaller, the
    // top write positions [0..wr] were never filled (the carry ran out early). Zero those
    // holes so strip_leading can drop them (Go computes the exact delta via a cutoff table;
    // we zero-then-strip, which is equivalent and table-free).
    for (int z = 0; z <= wr && z < TEKO_DEC_DIGITS; z++) a->d[z] = 0;
    a->nd += delta;
    if (a->nd > TEKO_DEC_DIGITS) a->nd = TEKO_DEC_DIGITS;
    a->dp += delta;
    teko_dec_strip_leading(a);         // the digit-growth bound may over-allocate by one
    teko_dec_trim(a);
}

// Generic shift: positive = left (*2^k), negative = right (/2^|k|). Applies in MAX_SHIFT chunks.
static void teko_dec_shift(teko_dec* a, int shift) {
    if (a->nd == 0) return;            // 0 stays 0
    while (shift > 0) {
        int s = shift > TEKO_MAX_SHIFT ? TEKO_MAX_SHIFT : shift;
        teko_dec_shift_left(a, (uint32_t)s);
        shift -= s;
        if (a->nd == 0) return;
    }
    while (shift < 0) {
        int s = -shift > TEKO_MAX_SHIFT ? TEKO_MAX_SHIFT : -shift;
        teko_dec_shift_right(a, (uint32_t)s);
        shift += s;
        if (a->nd == 0) return;
    }
}

// Extract the rounded integer value of the decimal (digits d[0..dp), the integer part), with
// round-half-to-even on the fractional tail. Transcribed from Go decimal.go RoundedInteger.
// Caller guarantees dp is small enough that the integer part fits in u64 (<= ~19 digits).
static uint64_t teko_dec_rounded_integer(const teko_dec* a) {
    if (a->dp < 0) return 0;
    uint64_t n = 0;
    int i;
    int id = a->dp; if (id > a->nd) id = a->nd;
    for (i = 0; i < id; i++) n = n * 10 + a->d[i];
    for (; i < a->dp; i++) n *= 10;          // integer digits past nd are implicit zeros
    // Round-half-to-even on the fraction starting at index dp.
    int roundup = 0;
    if (a->dp < a->nd) {
        if (a->d[a->dp] == 5 && a->dp + 1 == a->nd) {     // exactly halfway?
            roundup = a->truncated ? 1 : (n & 1);          // sticky -> up; else to even
        } else {
            roundup = a->d[a->dp] >= 5;
        }
    }
    if (roundup) n++;
    return n;
}

// Acceleration table: a safe per-step binary shift for a given |dp|. Matches Go decimal.go
// powtab (the number of bits 10^i needs, capped); using it makes the scaling loop O(log).
static const int teko_powtab[] = {
    1, 3, 6, 9, 13, 16, 19, 23, 26
};
#define TEKO_POWTAB_N ((int)(sizeof(teko_powtab) / sizeof(teko_powtab[0])))

// f64 floatInfo: mantbits=52, expbits=11, bias = -1023 (Go convention).
#define TEKO_F64_MANTBITS 52
#define TEKO_F64_EXPBITS 11
#define TEKO_F64_BIAS (-1023)

// Convert the parsed decimal to f64 bits. Transcribed from Go strconv/atof.go
// (*decimal).floatBits, specialized to binary64. Returns 1 on success (writes *out), 0 on
// overflow to ±Inf (FAIL-LOUD). Underflow to a subnormal/±0.0 SUCCEEDS.
static int teko_dec_to_f64(teko_dec* a, double* out) {
    int exp;
    uint64_t mant;
    int overflow = 0;

    if (a->nd == 0) {           // zero
        mant = 0; exp = TEKO_F64_BIAS;
        goto out;
    }
    if (a->dp > 310) goto overflow;          // obvious overflow
    if (a->dp < -330) { mant = 0; exp = TEKO_F64_BIAS; goto out; } // obvious underflow -> 0

    // Scale by powers of two until the value is in [0.5, 1).
    exp = 0;
    while (a->dp > 0) {
        int n = (a->dp >= TEKO_POWTAB_N) ? 27 : teko_powtab[a->dp];
        teko_dec_shift(a, -n); exp += n;
    }
    while (a->dp < 0 || (a->dp == 0 && a->nd > 0 && a->d[0] < 5)) {
        int n = (-a->dp >= TEKO_POWTAB_N) ? 27 : teko_powtab[-a->dp];
        teko_dec_shift(a, n); exp -= n;
    }

    // Our range is [0.5,1) but the IEEE significand range is [1,2): drop one.
    exp--;

    // Minimum representable exponent is bias+1.
    if (exp < TEKO_F64_BIAS + 1) {
        int n = TEKO_F64_BIAS + 1 - exp;
        teko_dec_shift(a, -n); exp += n;
    }

    if (exp - TEKO_F64_BIAS >= (1 << TEKO_F64_EXPBITS) - 1) goto overflow;

    // Extract 1 + mantbits = 53 bits.
    teko_dec_shift(a, (int)(1 + TEKO_F64_MANTBITS));
    mant = teko_dec_rounded_integer(a);

    // Rounding may have produced 2^53; renormalize.
    if (mant == (2ull << TEKO_F64_MANTBITS)) {
        mant >>= 1; exp++;
        if (exp - TEKO_F64_BIAS >= (1 << TEKO_F64_EXPBITS) - 1) goto overflow;
    }

    // Denormal: if the implicit leading bit is absent, force the subnormal exponent code.
    if ((mant & (1ull << TEKO_F64_MANTBITS)) == 0) exp = TEKO_F64_BIAS;

    goto out;

overflow:
    mant = 0;
    exp = (1 << TEKO_F64_EXPBITS) - 1 + TEKO_F64_BIAS;
    overflow = 1;

out:;
    if (overflow) return 0;   // FAIL-LOUD: leave *out UNTOUCHED (no ±Inf write on rejection)
    uint64_t bits = mant & ((1ull << TEKO_F64_MANTBITS) - 1);
    bits |= (uint64_t)((exp - TEKO_F64_BIAS) & ((1 << TEKO_F64_EXPBITS) - 1)) << TEKO_F64_MANTBITS;
    if (a->neg) bits |= (1ull << TEKO_F64_MANTBITS) << TEKO_F64_EXPBITS;
    double v; memcpy(&v, &bits, sizeof(double));
    *out = v;
    return overflow ? 0 : 1;   // overflow to ±Inf is FAIL-LOUD (return 0; *out is ±Inf)
}

static int teko_pf_is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int teko_pf_is_digit(char c) { return c >= '0' && c <= '9'; }

// Public entry: checked, correctly-rounded, freestanding decimal-string -> f64.
int teko_convert_parse_f64(const char* s, double* out) {
    if (!s) return 0;
    const char* p = s;
    while (teko_pf_is_ws(*p)) p++;

    teko_dec a;
    memset(a.d, 0, sizeof(a.d));
    a.nd = 0; a.dp = 0; a.neg = 0; a.truncated = 0;

    if (*p == '+' || *p == '-') { a.neg = (*p == '-'); p++; }

    // Parse the significand into Go's canonical decimal: d[0..nd) are the digits (with leading
    // zeros dropped), and dp = number of digits to the LEFT of the decimal point, so
    //   value = d[0].d[1]d[2]... x 10^dp   (i.e. d[i] has place 10^(dp-1-i)).
    // We accumulate every digit (suppressing leading zeros, adjusting dp for those after the
    // point), then trim. dp is set when the '.' is seen.
    int sawdot = 0;
    int sawdigit = 0;
    int seen_nonzero = 0;
    for (;;) {
        char c = *p;
        if (c == '.') {
            if (sawdot) return 0;        // a second '.' is malformed
            sawdot = 1;
            a.dp = a.nd;                 // digits seen so far are the integer part
            p++;
            continue;
        }
        if (!teko_pf_is_digit(c)) break;
        sawdigit = 1;
        p++;
        if (c == '0' && !seen_nonzero) {
            // Leading zero: never stored. Before the point it contributes nothing; after the
            // point each one lowers the exponent of the first significant digit by one.
            if (sawdot) a.dp--;
            continue;
        }
        seen_nonzero = 1;
        if (a.nd < TEKO_DEC_DIGITS) {
            a.d[a.nd++] = (uint8_t)(c - '0');
        } else if (c != '0') {
            a.truncated = 1;            // dropped a nonzero digit past the buffer
        }
    }
    if (!sawdigit) return 0;            // need at least one digit (rejects "", "+", ".", "e1")
    if (!sawdot) a.dp = a.nd;           // no '.' -> the point is after all integer digits

    // Trim trailing zeros (e.g. "100" -> nd=1, dp=3): they don't change the value and keep
    // the rounding logic clean. dp already encodes the magnitude.
    teko_dec_trim(&a);

    // Optional exponent.
    if (*p == 'e' || *p == 'E') {
        p++;
        int esign = 1;
        if (*p == '+' || *p == '-') { esign = (*p == '-') ? -1 : 1; p++; }
        if (!teko_pf_is_digit(*p)) return 0; // lone 'e' / 'e+' is malformed
        long exp = 0;
        while (teko_pf_is_digit(*p)) {
            exp = exp * 10 + (*p - '0');
            if (exp > 100000000L) exp = 100000000L; // clamp; huge exponent -> over/underflow
            p++;
        }
        a.dp += (int)(esign * exp);
    }

    while (teko_pf_is_ws(*p)) p++;
    if (*p != '\0') return 0;         // trailing junk (rejects "1.2.3" via dot, "1.0x", etc.)

    // Early range gates (avoid pathological shift loops on absurd exponents):
    if (a.nd != 0) {
        if (a.dp > 310)  return 0;    // > ~1.8e308 magnitude -> overflow, fail-loud
        if (a.dp < -340) {            // < ~5e-324 magnitude -> underflow to ±0.0
            uint64_t bits = a.neg ? (1ull << 63) : 0ull;
            double v; memcpy(&v, &bits, sizeof(double));
            *out = v;
            return 1;
        }
    }

    return teko_dec_to_f64(&a, out);
}
