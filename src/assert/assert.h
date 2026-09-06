// src/assert/assert.h   (namespace 'teko::assert')
//
// teko::assert — the C SEED of assert.tks: the injected, non-shadowable testing
// assertions under the reserved `teko::` root. The generic CALL lowering emits
// `teko__assert__<name>(...)` for a call to `teko::assert::<name>`, so these symbols
// realize the seed in C (no codegen change). The typer injects the signatures
// (src/checker/scope.tks::builtin_fn, via checker::assert_builtin_name).
//
// FAIL LOUD (M.1): a false assertion is a PANIC (tk_panic), not a soft return.
//
// EVERY FAILURE SAYS WHAT IT EXPECTED AND WHAT IT GOT (owner ruling 2026-07-30 — "cada asserção tem
// de dizer o que esperava e o que obteve; uma que só diz 'falhou' reproduz o problema que estamos a
// matar"). The one exception is is_true/is_false, whose whole input is a bool: "got false" carries no
// information a human did not already have from the assertion's name. Those two messages are
// therefore byte-unchanged.
//
// THE SCENARIO NAME is how a test addresses itself BY WHAT IT DOES instead of by an exit code:
// scenario_begin names the case, every failure message is prefixed with that name, and scenario_ok
// emits `<name>: ok` on the current channel. A fixture that used to encode its cases as `exit(N)` —
// which caps at 255, collides with itself and TRUNCATES in silence — asserts them by name instead.
//
// PANIC IS NOT ASSERTED HERE, deliberately: a panic aborts the PROCESS, so "this panics" is a
// process-level observable and belongs to the regression runner (`Then it traps` + a stderr
// pattern), not to an in-process assertion. Teko's own failure channel is the `T | error` union, and
// is_ok/is_error assert that.
//
// Shares tk_str / tk_panic with GENERATED programs via src/runtime/teko_rt.h, so the same
// source links into both the compiler lib and the host-cc-built native binary.
#ifndef TK_ASSERT_H
#define TK_ASSERT_H

#include "../runtime/teko_rt.h"   // tk_str, tk_byte, tk_panic, tk_assert_scenario_*

void teko__assert__is_true(bool c);        // panic unless c
void teko__assert__is_false(bool c);       // panic unless !c

// the SCENARIO frame: name the case, then declare it passed.
void teko__assert__scenario_begin(tk_str name);
void teko__assert__scenario_ok(void);

// numeric equality/ordering — the message carries both operands.
void teko__assert__eq_i64(int64_t got, int64_t want);
void teko__assert__ne_i64(int64_t got, int64_t want);
void teko__assert__lt_i64(int64_t got, int64_t bound);
void teko__assert__le_i64(int64_t got, int64_t bound);
void teko__assert__gt_i64(int64_t got, int64_t bound);
void teko__assert__ge_i64(int64_t got, int64_t bound);
void teko__assert__eq_u64(uint64_t got, uint64_t want);
void teko__assert__eq_f64(double got, double want, double tol);

// `str` content.
void teko__assert__eq_str(tk_str got, tk_str want);
void teko__assert__ne_str(tk_str got, tk_str want);
void teko__assert__str_contains(tk_str hay, tk_str needle);
void teko__assert__str_absent(tk_str hay, tk_str needle);
void teko__assert__str_starts_with(tk_str hay, tk_str prefix);
void teko__assert__str_ends_with(tk_str hay, tk_str suffix);

// `[]T` shape and `[]byte` content (the one element type a non-generic seed can name). A `[]byte`
// crosses this boundary as a `tk_str` — the two have the SAME {ptr,len} layout, the precedent being
// teko_rt.h's own `tk_str_of_bytes(tk_str bytes)`.
void teko__assert__len_eq(uint64_t got, uint64_t want);
void teko__assert__bytes_eq(tk_str got, tk_str want);

// null-union presence and error-union outcome, each naming the member the caller narrowed on.
void teko__assert__is_present(tk_str what, bool present);
void teko__assert__is_absent(tk_str what, bool present);
void teko__assert__is_ok(tk_str what, bool ok);
void teko__assert__is_error(tk_str what, bool ok);

#endif // TK_ASSERT_H
