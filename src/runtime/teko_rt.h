// src/runtime/teko_rt.h   (namespace 'teko::runtime')
// libteko_rt: runtime for GENERATED Teko programs (M.1 fail-loud).
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

// tk_alloc — the allocation seam (S0). malloc(n) (n→1 when 0 so the result is unique); tk_panic
// on OOM (M.1). Generated code allocates through this: slice copy-append AND the auto-boxed
// recursive-value-type back-edges (tk_alloc(sizeof *p)). The S2 arena campaign swaps this seam.
void *tk_alloc(size_t n);

// tk_print — write exactly s.len bytes from s.ptr to stdout; no newline, no NUL.
void tk_print(tk_str s);
// tk_println — tk_print(s) then a single '\n' (0x0A).
void tk_println(tk_str s);
// Host output FFI bottoms (scope.c write/ewrite/eprint/eprintln) — s.len bytes, NUL-tolerant.
// write → stdout; ewrite/eprint → stderr; eprintln → stderr + '\n'.
void tk_write(tk_str s);
void tk_ewrite(tk_str s);
void tk_eprint(tk_str s);
void tk_eprintln(tk_str s);
// teko::float::parse(str) -> f64 (strtod over a NUL-terminated copy; non-numeric → 0.0).
double tk_float_parse(tk_str s);

// --- string interpolation `$"…{expr}…"` builders (self-host parity) ---
// These are EXTERN (linked from teko_rt.c), NOT static inline — both the generated C and
// the VM call them, and the VM forward-declares them (like tk_print) rather than including
// this header. Leak-tolerant (M.5 — the results are short-lived process-lifetime buffers).
//
// tk_str_concat — a fresh str holding a's bytes then b's bytes; the result OWNS the buffer.
tk_str tk_str_concat(tk_str a, tk_str b);
// tk_i64_to_str / tk_u64_to_str — the integer's DECIMAL text in a fresh str. The interp
// lowering widens every signed int hole to i64 and every unsigned hole to u64 (every Teko
// integer prim except u128/i128 fits; the checker scopes holes to what the corpus needs).
tk_str tk_i64_to_str(int64_t v);
tk_str tk_u64_to_str(uint64_t v);

// --- Phase 3 str/byte stdlib (the four recognized-but-not-yet-lowered builtins) ---
// Same contract as tk_str_concat: a fresh malloc'd buffer the result OWNS, tk_panic on OOM
// (M.1), leak-tolerant (M.5 — short-lived).
//
// tk_str_of_bytes — a fresh str COPYING the bytes of a []byte slice. A []byte slice has the
// SAME {ptr,len} shape as tk_str, so codegen passes the slice value directly; this copies it
// into a fresh owned buffer (the `str_of_bytes` builtin; the `str` builtin aliases it).
tk_str tk_str_of_bytes(tk_str bytes);
// tk_one_byte — a fresh 1-byte str holding c.
tk_str tk_one_byte(tk_byte c);
// tk_str_concat3 — a ++ b ++ c in a fresh owned buffer (two tk_str_concat steps).
tk_str tk_str_concat3(tk_str a, tk_str b, tk_str c);
// tk_ftoa — x rendered as %.17g float text (exact binary64 round-trip) in a fresh str.
tk_str tk_ftoa(double x);

// --- Phase 3 str query/slice builtins (the checker types these via tk_builtin_fn) ---
// The query helpers (eq/ends_with/contains/len) do NO allocation; the slice helpers return a
// FRESH owned buffer (same ownership as tk_str_concat), tk_panic on OOM/out-of-range (M.1).
//
// tk_str_eq — true iff a and b have the same length and the same bytes (memcmp; embedded NUL
// tolerated). No allocation.
bool tk_str_eq(tk_str a, tk_str b);
// tk_str_slice — the substring bytes [start, end) as a fresh owned str. An out-of-range slice
// (start > end, or end > s.len) PANICS (M.1, parity with the VM's bounds check). The empty
// slice (start == end) is a valid empty str (1-byte buffer so ptr is never NULL+stale len).
tk_str tk_str_slice(tk_str s, uint64_t start, uint64_t end);
// tk_str_slice_to — tk_str_slice(s, 0, end).
tk_str tk_str_slice_to(tk_str s, uint64_t end);
// tk_str_slice_from — tk_str_slice(s, start, s.len).
tk_str tk_str_slice_from(tk_str s, uint64_t start);
// tk_str_len — s.len (the byte length). No allocation.
uint64_t tk_str_len(tk_str s);
// tk_str_ends_with — true iff s ends with suffix (suffix.len <= s.len and the tail bytes
// match). No allocation.
bool tk_str_ends_with(tk_str s, tk_str suffix);
// tk_str_contains — true iff needle occurs in s (naive byte search; an empty needle → true).
// No allocation.
bool tk_str_contains(tk_str s, tk_str needle);
// tk_f64_g17 — x rendered as %.17g into a fresh owned str (the host float renderer; the same
// renderer as tk_ftoa, exposed under the `f64_g17` name the checker/codegen reference).
tk_str tk_f64_g17(double x);

// =========================================================================
// Host-FFI + arithmetic bottoms (Phase 7 / scope.c builtin_fn surface).
//
// THE LIFTING SEAM. The host I/O builtins return Teko `T | error` / `error?`
// sums and `[]str`. Those are GENERATED struct types (tk_u_str_error,
// tk_opt_error, tk_slice_str, …) defined in the emitted C, AFTER this header is
// #included — so the runtime CANNOT name them. Instead each primitive returns a
// FIXED-ABI result struct over header-knowable types only (tk_str + scalars +
// tk_str*), and codegen lifts that into the program's result type (the
// emit_host_ffi statement-expression). `error` lowers to its message tk_str.
// =========================================================================

// str | error  (read_file, var): ok → value; !ok → err (the message).
typedef struct { bool ok; tk_str value; tk_str err; } tk_ffi_sres;
// error?  (write_file, chdir): ok → success (no error); !ok → err present.
typedef struct { bool ok; tk_str err; } tk_ffi_ures;
// []str | error  (list_dir): ok → {ptr,len} entries; !ok → err.
typedef struct { bool ok; tk_str *ptr; uint64_t len; tk_str err; } tk_ffi_slres;
// u64 | error  (last_index_of): ok → value; !ok → not found.
typedef struct { bool ok; uint64_t value; } tk_ffi_u64res;

// teko::io::read_file(path) — slurp the whole file as UTF-8 bytes (owned copy).
tk_ffi_sres tk_rt_read_file(tk_str path);
// teko::env::var(name) — the environment value, or error when unset.
tk_ffi_sres tk_rt_getenv(tk_str name);
// teko::io::write_file(path, content) — (over)write the file; error on failure.
tk_ffi_ures tk_rt_write_file(tk_str path, tk_str content);
// teko::env::chdir(path) — change the process working directory; error on failure.
tk_ffi_ures tk_rt_chdir(tk_str path);
// teko::fs::mkdir(path) — create a directory (mode 0755); SUCCESS if it already exists.
tk_ffi_ures tk_rt_mkdir(tk_str path);
// teko::env::cwd() — the current working directory as an owned absolute path, or error.
tk_ffi_sres tk_rt_getcwd(void);
// teko::env::set_var(name, value) — set an environment variable; error on failure.
tk_ffi_ures tk_rt_setenv(tk_str name, tk_str value);
// teko::fs::list_dir(path) — the directory entries (excluding "." / ".."), or error.
tk_ffi_slres tk_rt_list_dir(tk_str path);
// teko::str::last_index_of(hay, needle) — byte index of the LAST occurrence, or not-found.
tk_ffi_u64res tk_rt_last_index_of(tk_str hay, tk_str needle);
// teko::process::run(argv) — fork/exec argv[0] with argv, wait, return its exit status
// (127 when argv is empty / exec fails). Takes the slice as ptr+len (no generated type).
int32_t tk_rt_run(const tk_str *argv, uint64_t n);
// teko::env::args() — the captured process argv as owned tk_str's; *n receives the count.
// tk_set_args must run first (the generated `main` calls it before the virtual-main body).
void    tk_set_args(int argc, char **argv);
tk_str *tk_rt_args(uint64_t *n);

// tk_slice_push — the AMORTIZED lowering of `teko::list::push` (a `[]T` grow-by-one). The
// language keeps value semantics (fixed slices, copy-to-grow — collections #2); this is purely a
// codegen optimization so a LINEAR `b = push(b, x)` chain (the codegen output buffer is 1.4MB+
// built this way) is O(1) amortized instead of O(n) copy-every-push → O(n²). A small cache of
// recent "live tails" lets an in-place append happen when `ptr` is the current end of a buffer
// this function grew (matched by ptr + length witness + element size, with spare capacity); ANY
// other slice — a stale/shorter version, a different or untracked buffer, or a full one — COPIES
// (geometric growth), so aliased/branched buffers never observe each other's appends (value-safe).
// `elem` points at one element of `esz` bytes; `*out_len` receives the new length; returns the
// (possibly same) data pointer.
void *tk_slice_push(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len);

// --- arithmetic FFI over the i128 carrier (sign-aware) + float bit-patterns ---
// div/rem: truncated division/remainder; sgn selects signed vs unsigned interpretation.
__int128 tk_div(__int128 a, __int128 b, bool sgn);
__int128 tk_rem(__int128 a, __int128 b, bool sgn);
// fdiv: float division (the VM's f64 `/`); int_to_float: i128 (sgn-aware) → f64.
double   tk_fdiv(double a, double b);
double   tk_int_to_float(__int128 v, bool sgn);
// f64 ↔ raw IEEE-754 bit pattern (the .tkb float (de)serialization edge).
uint64_t tk_f64_bits(double x);
double   tk_f64_from_bits(uint64_t bits);

// tk_panic — fail loud (M.1): "teko: panic: <msg>\n" to stderr, then non-zero exit.
_Noreturn void tk_panic(const char *msg);
// the Teko-level globals `panic(str)` / `exit(<int>)` (legislator's ruling — no `never` type).
// tk_panic_str takes a tk_str (ptr+len, tolerates embedded NUL); tk_exit ends with a status code.
_Noreturn void tk_panic_str(tk_str msg);
_Noreturn void tk_exit(int32_t code);
_Noreturn void tk_panic_div0(void);       // "division by zero"
_Noreturn void tk_panic_oob(void);        // "index out of bounds"
_Noreturn void tk_panic_cast(void);       // "impossible conversion" (the `x to T` guard — B.36 / M.1)
_Noreturn void tk_panic_overflow(void);   // "integer overflow"
// (C1.7) positioned OOB panic — prefix "line:col: " then the canonical OOB panic. codegen passes
// the offending index node's position (C1-POS) so a NATIVE index-out-of-bounds locates like the VM
// (vm_panic_oob_at). Native cast/div0 positioning is deferred (cast = 48 inline tk_to_* helpers;
// the div0 codegen guard is unwired — B3b) — a later "native panic positioning" pass (global loc).
_Noreturn void tk_panic_oob_at(uint32_t line, uint32_t col);

// teko::assert (the injected testing assertions) lives in its own C seed —
// src/assert/assert.{c,h} (canonical: src/assert/assert.tks). Generated programs that
// call them get the symbols because driver.c::run_cc compiles src/assert/assert.c
// alongside this runtime; the compiler lib links the same source via CMake.

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

// --- checked integer division / modulo: panic on a zero divisor (no UB / SIGFPE) ---
// One helper per signed/unsigned width (now incl. 128); codegen selects by the binary
// node's prim. (__int128 has no printf specifier but arithmetic on it is fine.)
static inline uint8_t  tk_div_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a / b); }
static inline uint16_t tk_div_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a / b); }
static inline uint32_t tk_div_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline uint64_t tk_div_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline unsigned __int128 tk_div_u128(unsigned __int128 a, unsigned __int128 b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int8_t   tk_div_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a / b); }
static inline int16_t  tk_div_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a / b); }
static inline int32_t  tk_div_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline int64_t  tk_div_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a / b; }
static inline __int128 tk_div_i128(__int128 a, __int128 b){ if (b == 0) tk_panic_div0(); return a / b; }

static inline uint8_t  tk_mod_u8 (uint8_t  a, uint8_t  b){ if (b == 0) tk_panic_div0(); return (uint8_t )(a % b); }
static inline uint16_t tk_mod_u16(uint16_t a, uint16_t b){ if (b == 0) tk_panic_div0(); return (uint16_t)(a % b); }
static inline uint32_t tk_mod_u32(uint32_t a, uint32_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline uint64_t tk_mod_u64(uint64_t a, uint64_t b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline unsigned __int128 tk_mod_u128(unsigned __int128 a, unsigned __int128 b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int8_t   tk_mod_i8 (int8_t   a, int8_t   b){ if (b == 0) tk_panic_div0(); return (int8_t  )(a % b); }
static inline int16_t  tk_mod_i16(int16_t  a, int16_t  b){ if (b == 0) tk_panic_div0(); return (int16_t )(a % b); }
static inline int32_t  tk_mod_i32(int32_t  a, int32_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline int64_t  tk_mod_i64(int64_t  a, int64_t  b){ if (b == 0) tk_panic_div0(); return a % b; }
static inline __int128 tk_mod_i128(__int128 a, __int128 b){ if (b == 0) tk_panic_div0(); return a % b; }

// --- checked FLOAT division: ruling (§5) — float ÷0 PANICS (parity with int, M.1) ---
// `%` on floats is invalid (the backend rejects it); only `/` rides these. Single-eval.
static inline double     tk_div_f64(double     a, double     b){ if (b == 0.0)     tk_panic_div0(); return a / b; }
static inline float      tk_div_f32(float      a, float      b){ if (b == 0.0f)    tk_panic_div0(); return a / b; }
static inline _Float16   tk_div_f16(_Float16   a, _Float16   b){ if (b == (_Float16)0) tk_panic_div0(); return a / b; }

// --- checked narrowing integer conversion: panic if the value can't fit the target ---
// The source value is widened to a 128-bit carrier WITHOUT loss (every Teko integer
// prim, incl. u128/i128, fits a 128-bit carrier): __int128 for a SIGNED source ("_s"),
// unsigned __int128 for an UNSIGNED source ("_u"). The range check then decides fit.
// A signed source -> unsigned target rides "_s" (the negative check catches it); an
// unsigned source -> signed target rides "_u" (the upper-bound check catches it).
// 128-bit-wide bounds are built from shifts so no literal exceeds what C can spell.
static inline uint8_t  tk_to_u8_s (__int128 v){ if (v < 0 || v > 0xFF)       tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_s(__int128 v){ if (v < 0 || v > 0xFFFF)     tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_s(__int128 v){ if (v < 0 || v > 0xFFFFFFFF) tk_panic_cast(); return (uint32_t)v; }
static inline uint64_t tk_to_u64_s(__int128 v){ if (v < 0 || v > (__int128)0xFFFFFFFFFFFFFFFFULL) tk_panic_cast(); return (uint64_t)v; }
static inline unsigned __int128 tk_to_u128_s(__int128 v){ if (v < 0) tk_panic_cast(); return (unsigned __int128)v; }
static inline uint8_t  tk_to_u8_u (unsigned __int128 v){ if (v > 0xFF)                tk_panic_cast(); return (uint8_t )v; }
static inline uint16_t tk_to_u16_u(unsigned __int128 v){ if (v > 0xFFFF)             tk_panic_cast(); return (uint16_t)v; }
static inline uint32_t tk_to_u32_u(unsigned __int128 v){ if (v > 0xFFFFFFFF)         tk_panic_cast(); return (uint32_t)v; }
static inline uint64_t tk_to_u64_u(unsigned __int128 v){ if (v > (unsigned __int128)0xFFFFFFFFFFFFFFFFULL) tk_panic_cast(); return (uint64_t)v; }

static inline int8_t   tk_to_i8_s (__int128 v){ if (v < -128          || v > 127)        tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_s(__int128 v){ if (v < -32768        || v > 32767)      tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_s(__int128 v){ if (v < -2147483648LL || v > 2147483647LL) tk_panic_cast(); return (int32_t)v; }
static inline int64_t  tk_to_i64_s(__int128 v){ if (v < -(__int128)0x8000000000000000ULL || v > (__int128)0x7FFFFFFFFFFFFFFFLL) tk_panic_cast(); return (int64_t)v; }
static inline int8_t   tk_to_i8_u (unsigned __int128 v){ if (v > 127)                tk_panic_cast(); return (int8_t )v; }
static inline int16_t  tk_to_i16_u(unsigned __int128 v){ if (v > 32767)             tk_panic_cast(); return (int16_t)v; }
static inline int32_t  tk_to_i32_u(unsigned __int128 v){ if (v > 2147483647LL)      tk_panic_cast(); return (int32_t)v; }
static inline int64_t  tk_to_i64_u(unsigned __int128 v){ if (v > (unsigned __int128)0x7FFFFFFFFFFFFFFFULL) tk_panic_cast(); return (int64_t)v; }
// i128 max == 2^127 - 1; built from shifts since no C literal can spell a 128-bit value.
static inline __int128 tk_to_i128_u(unsigned __int128 v){ if (v > (((unsigned __int128)0x7FFFFFFFFFFFFFFFULL << 64) | (unsigned __int128)0xFFFFFFFFFFFFFFFFULL)) tk_panic_cast(); return (__int128)v; }

// --- checked float -> int conversion: ruling (§5) — `to` truncates toward zero;
// NaN/inf or a value outside the target's range -> PANIC (parity with the int guard).
// Truncation toward zero is C's float->int conversion semantics; we range-check the
// SOURCE value in the float domain BEFORE converting. NaN fails every comparison, so a
// negated `(lo <= x && x <= hi)` traps NaN AND ±inf AND out-of-range in one test.
// Every f16/f32 value is exactly representable as a double, so the f32/f16 entry points
// widen losslessly and route through the f64 checker (single-eval entry typing preserved).
//
// Bound choice: for an 8/16/32-bit target both bounds are exact doubles, so the inclusive
// `[min, max]` compare is exact. For 64/128-bit targets the max integer is NOT an exact
// double, so the unsigned/positive ceiling is the EXCLUSIVE 2^W (resp. 2^(W-1)) via `<`,
// which is the exact double right above the representable range — any in-range value is
// strictly below it. The signed lower bound -2^(W-1) IS an exact double, so it stays `>=`.
static inline uint8_t  tk_to_u8_from_f64 (double x){ if(!(x>=0.0 && x<=255.0))             tk_panic_cast(); return (uint8_t )x; }
static inline uint8_t  tk_to_u8_from_f32 (float  x){ return tk_to_u8_from_f64((double)x); }
static inline uint8_t  tk_to_u8_from_f16 (_Float16 x){ return tk_to_u8_from_f64((double)x); }
static inline uint16_t tk_to_u16_from_f64(double x){ if(!(x>=0.0 && x<=65535.0))           tk_panic_cast(); return (uint16_t)x; }
static inline uint16_t tk_to_u16_from_f32(float  x){ return tk_to_u16_from_f64((double)x); }
static inline uint16_t tk_to_u16_from_f16(_Float16 x){ return tk_to_u16_from_f64((double)x); }
static inline uint32_t tk_to_u32_from_f64(double x){ if(!(x>=0.0 && x<=4294967295.0))      tk_panic_cast(); return (uint32_t)x; }
static inline uint32_t tk_to_u32_from_f32(float  x){ return tk_to_u32_from_f64((double)x); }
static inline uint32_t tk_to_u32_from_f16(_Float16 x){ return tk_to_u32_from_f64((double)x); }
static inline uint64_t tk_to_u64_from_f64(double x){ if(!(x>=0.0 && x<18446744073709551616.0)) tk_panic_cast(); return (uint64_t)x; }
static inline uint64_t tk_to_u64_from_f32(float  x){ return tk_to_u64_from_f64((double)x); }
static inline uint64_t tk_to_u64_from_f16(_Float16 x){ return tk_to_u64_from_f64((double)x); }
static inline unsigned __int128 tk_to_u128_from_f64(double x){ if(!(x>=0.0 && x<340282366920938463463374607431768211456.0)) tk_panic_cast(); return (unsigned __int128)x; }
static inline unsigned __int128 tk_to_u128_from_f32(float  x){ return tk_to_u128_from_f64((double)x); }
static inline unsigned __int128 tk_to_u128_from_f16(_Float16 x){ return tk_to_u128_from_f64((double)x); }

// Signed targets: valid x in [-2^(W-1), 2^(W-1) - 1]. For 8/16/32 the bounds are exact
// doubles; for 64/128 the upper bound is bounded strictly below 2^(W-1).
static inline int8_t   tk_to_i8_from_f64 (double x){ if(!(x>=-128.0 && x<=127.0))          tk_panic_cast(); return (int8_t )x; }
static inline int8_t   tk_to_i8_from_f32 (float  x){ return tk_to_i8_from_f64((double)x); }
static inline int8_t   tk_to_i8_from_f16 (_Float16 x){ return tk_to_i8_from_f64((double)x); }
static inline int16_t  tk_to_i16_from_f64(double x){ if(!(x>=-32768.0 && x<=32767.0))      tk_panic_cast(); return (int16_t)x; }
static inline int16_t  tk_to_i16_from_f32(float  x){ return tk_to_i16_from_f64((double)x); }
static inline int16_t  tk_to_i16_from_f16(_Float16 x){ return tk_to_i16_from_f64((double)x); }
static inline int32_t  tk_to_i32_from_f64(double x){ if(!(x>=-2147483648.0 && x<=2147483647.0)) tk_panic_cast(); return (int32_t)x; }
static inline int32_t  tk_to_i32_from_f32(float  x){ return tk_to_i32_from_f64((double)x); }
static inline int32_t  tk_to_i32_from_f16(_Float16 x){ return tk_to_i32_from_f64((double)x); }
static inline int64_t  tk_to_i64_from_f64(double x){ if(!(x>=-9223372036854775808.0 && x<9223372036854775808.0)) tk_panic_cast(); return (int64_t)x; }
static inline int64_t  tk_to_i64_from_f32(float  x){ return tk_to_i64_from_f64((double)x); }
static inline int64_t  tk_to_i64_from_f16(_Float16 x){ return tk_to_i64_from_f64((double)x); }
static inline __int128 tk_to_i128_from_f64(double x){ if(!(x>=-170141183460469231731687303715884105728.0 && x<170141183460469231731687303715884105728.0)) tk_panic_cast(); return (__int128)x; }
static inline __int128 tk_to_i128_from_f32(float  x){ return tk_to_i128_from_f64((double)x); }
static inline __int128 tk_to_i128_from_f16(_Float16 x){ return tk_to_i128_from_f64((double)x); }

#endif // TEKO_RT_H
