// runtime/teko_rt.h — libteko_rt: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#ifndef TEKO_RT_H
#define TEKO_RT_H

#include <stdint.h>   // uint8_t
#include <stddef.h>   // size_t
#include <stdbool.h>  // bool

// byte — one octet (mirrors src/text/text.h's tk_byte; same rep).
typedef uint8_t tk_byte;

// str — a VIEW into UTF-8 bytes; len is in BYTES, NOT NUL-terminated.
// Identical shape to src/text/text.h's tk_str.
typedef struct {
    const tk_byte *ptr;   // the bytes
    size_t         len;   // length in BYTES
} tk_str;

// tk_print — write exactly s.len bytes from s.ptr to stdout; no newline, no NUL.
void tk_print(tk_str s);
// tk_println — tk_print(s) then a single '\n' (0x0A).
void tk_println(tk_str s);

// tk_panic — fail loud (M.1): "teko: panic: <msg>\n" to stderr, then non-zero exit.
_Noreturn void tk_panic(const char *msg);
_Noreturn void tk_panic_div0(void);       // "division by zero"
_Noreturn void tk_panic_oob(void);        // "index out of bounds"
_Noreturn void tk_panic_cast(void);       // "impossible conversion" (the `x to T` guard — B.36 / M.1)
_Noreturn void tk_panic_overflow(void);   // "integer overflow"

// =========================================================================
// teko::assert — injected testing assertions (canonical: src/assert/assert.tks).
// The generic CALL lowering emits `teko__assert__<name>(...)` for a call to
// `teko::assert::<name>`, so these symbols realize the seed in C (no codegen change).
// Each FAILS LOUD (M.1): on a false assertion -> tk_panic("assertion failed: <name>").
// Seed subset is NON-generic; equals/is_error/is_ok need generics — DEFERRED (M.3).
// =========================================================================
void teko__assert__is_true(bool c);        // panic unless c
void teko__assert__is_false(bool c);       // panic unless !c
void teko__assert__str_contains(tk_str hay, tk_str needle);  // panic unless needle ⊆ hay

// =========================================================================
// F3 runtime guards (M.1 — fail loud, never silent corruption / metal hazard):
// the backend emits calls to these in place of raw C `/`, `%`, and narrowing
// casts. All are SINGLE-EVALUATION (each argument is computed once by the caller
// and passed by value), so codegen never duplicates an operand subtree.
//
// COVERAGE RULE (M.3): conversion/division possibility is validated at RUNTIME;
// impossibility -> PANIC. Constants out of range are the checker's job (compile
// error) and are out of scope here.
//
// NOTE (out of scope): overflow guarding for +,-,* is DEFERRED — it is
// profile-dependent (panic in debug / wrap in release) and build PROFILES do not
// exist yet. tk_panic_overflow() above exists for that future wiring.
// =========================================================================

// --- checked division / modulo: panic on a zero divisor (no UB / SIGFPE) ---
// One helper per signed/unsigned width; codegen selects by the binary node's prim.
static inline uint8_t  tk_div_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a / b); }
static inline uint16_t tk_div_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a / b); }
static inline uint32_t tk_div_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline uint64_t tk_div_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int8_t   tk_div_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a / b); }
static inline int16_t  tk_div_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a / b); }
static inline int32_t  tk_div_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int64_t  tk_div_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a / b; }

static inline uint8_t  tk_mod_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a % b); }
static inline uint16_t tk_mod_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a % b); }
static inline uint32_t tk_mod_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline uint64_t tk_mod_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int8_t   tk_mod_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a % b); }
static inline int16_t  tk_mod_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a % b); }
static inline int32_t  tk_mod_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int64_t  tk_mod_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a % b; }

// --- checked narrowing conversion: panic if the value can't fit the target ---
// The source value is widened to a carrier (int64_t for signed targets, uint64_t
// for unsigned) WITHOUT loss: codegen only emits these when the SOURCE prim fits
// the carrier. A signed source -> unsigned target rides tk_to_u* with an int64_t
// carrier (the negative check catches it); an unsigned source -> signed target
// rides tk_to_i* with a uint64_t carrier (the upper-bound check catches it). u64
// and i64 each need both carriers, so both signedness families are provided.
static inline uint8_t  tk_to_u8_s (int64_t  v){ if (v < 0 || v > 0xFF)       tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_s(int64_t  v){ if (v < 0 || v > 0xFFFF)     tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_s(int64_t  v){ if (v < 0 || v > 0xFFFFFFFF) tk_panic_cast(); return (uint32_t)v; }
static inline uint64_t tk_to_u64_s(int64_t  v){ if (v < 0)                   tk_panic_cast(); return (uint64_t)v; }
static inline uint8_t  tk_to_u8_u (uint64_t v){ if (v > 0xFF)                tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_u(uint64_t v){ if (v > 0xFFFF)             tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_u(uint64_t v){ if (v > 0xFFFFFFFF)         tk_panic_cast(); return (uint32_t)v; }

static inline int8_t   tk_to_i8_s (int64_t  v){ if (v < -128       || v > 127)        tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_s(int64_t  v){ if (v < -32768     || v > 32767)      tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_s(int64_t  v){ if (v < -2147483648LL || v > 2147483647LL) tk_panic_cast(); return (int32_t)v; }
static inline int8_t   tk_to_i8_u (uint64_t v){ if (v > 127)                tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_u(uint64_t v){ if (v > 32767)             tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_u(uint64_t v){ if (v > 2147483647LL)      tk_panic_cast(); return (int32_t)v; }
static inline int64_t  tk_to_i64_u(uint64_t v){ if (v > 9223372036854775807ULL) tk_panic_cast(); return (int64_t)v; }

#endif // TEKO_RT_H
