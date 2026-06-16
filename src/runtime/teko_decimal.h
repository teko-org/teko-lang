// =====================================================================================
// teko_decimal.h — Phase 17.F.1: the 256-byte exact base-10 `decimal` value type.
//
// The single C source of truth for Teko's exact base-10 decimal (the owner-LOCKED
// fixed-width 256-byte design — NOT C# System.Decimal). Arithmetic + banker's rounding
// live in teko_decimal.c; parse/format land in 17.F.2 and the language surface/opcodes in
// 17.F.3/.4. This file is consumed by the KAT suite now and by the runtime wrappers later;
// it is freestanding-safe (no libc beyond <stdint.h>).
//
// VALUE MODEL (owner-LOCKED):
//   value = (-1)^sign * COEFF * 10^(-scale)
//     sign   in {0,1}            (0 = positive; zero canonicalizes to sign 0)
//     scale  in [0,38]           (# of base-10 fractional digits)
//     COEFF  = sum(limb[i] << 64*i), an unsigned 1984-bit integer (31 LITTLE-ENDIAN
//              u64 limbs). Always finite (no NaN/Inf); flags is reserved 0.
//
//   ABI: ALWAYS passed BY POINTER (256 bytes never crosses by value). This is the
//   native/WASM ABI the 17.F.3 opcodes lower to.
// =====================================================================================
#ifndef TEKO_DECIMAL_H
#define TEKO_DECIMAL_H

#include <stdint.h>

#define TEKO_DECIMAL_LIMBS 31      // 31 * 64 = 1984 coefficient bits
#define TEKO_DECIMAL_MAX_SCALE 38  // bounded fractional digits (banker's-rounded beyond)

typedef struct teko_decimal {
    uint8_t  sign;                       // [0]    0 = positive, 1 = negative
    uint8_t  scale;                      // [1]    0..38  (# base-10 fractional digits)
    uint8_t  flags;                      // [2]    reserved, always 0 (always-finite)
    uint8_t  _pad[5];                    // [3..7] zero -> 8-byte metadata block
    uint64_t limb[TEKO_DECIMAL_LIMBS];   // [8..255] LE unsigned base-10 COEFFICIENT (248 B)
} teko_decimal;

_Static_assert(sizeof(teko_decimal) == 256, "teko_decimal must be exactly 256 bytes");
_Static_assert(_Alignof(teko_decimal) == 8, "teko_decimal must be 8-byte aligned");

// --- public API (all by pointer; return int: 1 = ok, 0 = fail-loud/overflow/divzero) ---
// On failure (return 0), *out is left zeroed (teko_decimal_zero); the caller's
// teko_rt_decimal_* wrapper turns 0 into teko_rt_die (native exit 70 / wasm trap).
int  teko_decimal_add(const teko_decimal* a, const teko_decimal* b, teko_decimal* out);
int  teko_decimal_sub(const teko_decimal* a, const teko_decimal* b, teko_decimal* out);
int  teko_decimal_mul(const teko_decimal* a, const teko_decimal* b, teko_decimal* out);
int  teko_decimal_div(const teko_decimal* a, const teko_decimal* b, teko_decimal* out);
int  teko_decimal_mod(const teko_decimal* a, const teko_decimal* b, teko_decimal* out);
// compare: writes -1 (a<b), 0 (a==b, +0==-0), or 1 (a>b) to *out_lt_eq_gt. Returns 1
// (compare cannot fail — operands are always valid finite decimals).
int  teko_decimal_cmp(const teko_decimal* a, const teko_decimal* b, int* out_lt_eq_gt);

// Set *out to canonical zero (+0, scale 0, COEFF 0).
void teko_decimal_zero(teko_decimal* out);

// Test/loader helper (parse is 17.F.2): build a decimal from a (sign, scale, coefficient-
// digit-string) triple. `digits` is the unsigned coefficient as ASCII decimal (e.g. "12345");
// leading zeros allowed; empty/NULL => coefficient 0. Returns 1 on success, 0 if scale > 38
// or the coefficient would exceed 1984 bits. The result is NOT normalized (the digit string
// and scale are stored as given, except a zero coefficient canonicalizes sign to 0).
int  teko_decimal_from_components(uint8_t sign, uint8_t scale, const char* digits,
                                  teko_decimal* out);

#endif // TEKO_DECIMAL_H
