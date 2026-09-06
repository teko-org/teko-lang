// src/assert/assert.c   (namespace 'teko::assert')
//
// The C SEED impl of assert.tks. Each FAILS LOUD (M.1): on a false assertion ->
// tk_panic("assertion failed: <name>"). Shares tk_str + tk_panic with generated programs
// (src/runtime/teko_rt.h), so this one source links into BOTH the compiler lib (via CMake) and
// the host-cc-built native binary (driver.c::run_cc compiles it alongside teko_rt.c).
#include "assert.h"
#include <stdio.h>      // snprintf — every failure message names the expected and the got value
#include <inttypes.h>   // PRId64/PRIu64 — the integer families' operands
#include <string.h>     // memcmp — bytes_eq

// THE SEED'S DUAL ROLE, and why WEAK alone could not carry it.
//
// run_cc links this seed into EVERY native binary so a program that CALLS teko::assert but does not
// DEFINE it resolves the symbols. But the teko compiler's OWN corpus DOES define them (assert.tks ->
// the generated teko.c), so that build must not see a second definition. Weak linkage used to serve
// both roles: the corpus's strong definitions win when present, the seed fills in when absent.
//
// WEAK IS BROKEN ON PE/COFF. GCC lowers a weak DEFINITION to the real symbol renamed
// `.weak.<name>.` plus an *undefined* weak external `<name>`, and GNU ld for PE does not resolve
// another object's strong reference against it. Reproduced with x86_64-w64-mingw32-gcc,
// order-independent:
//     undefined reference to `teko__assert__is_true'
// So on Windows this seed was effectively DEAD: any program calling teko::assert without defining it
// could not LINK. `regressor.tkr`'s `assert_native` scenario is exactly such a program, and the
// 0.3.1 regression fold is what first ran it on Windows at all.
//
// THE FIX — the ROLE is chosen by the BUILD, not guessed by the linker. `TK_ASSERT_SEED_STRONG` is
// defined by `project.tks::build_cc_argv` exactly when the program being built does NOT declare
// `teko::assert` itself (`program_declares_assert_seed`), i.e. exactly when this seed is the ONLY
// definition. Then the definitions are STRONG and resolve on ELF, Mach-O and PE/COFF alike.
//
// WEAK REMAINS THE DEFAULT, and that is deliberate: a PREVIOUSLY RELEASED seed compiles this tree
// without passing the new flag, and it links teko.c + assert.c together. Strong-by-default would
// make every existing seed fail to bootstrap gen1 with a duplicate symbol. Defaulting to weak keeps
// the flagless link line byte-identical to today's — which is also why the compiler's own build (the
// no-flag case) keeps its exact link line, and the FIXPOINT with it.
#ifndef TK_ASSERT_SEED_STRONG
#define TK_ASSERT_WEAK __attribute__((weak))
#else
#define TK_ASSERT_WEAK
#endif

// TK_ASSERT_MSG_MAX — the failure-message buffer. A message is one line a human reads, and a bound
// keeps the whole failure path allocation-free: an assertion that had to allocate to report itself
// could not report an out-of-memory failure.
#define TK_ASSERT_MSG_MAX 1024

// tk_assert_fail — the ONE exit every assertion below panics through: the scenario prefix (empty when
// no scenario is named, which is what keeps is_true/is_false byte-identical to their historic text),
// the assertion's name, then what it expected and what it got.
static void tk_assert_fail(const char *what, const char *detail) {
    tk_str p = tk_assert_scenario_prefix();
    const char *pp = p.len ? (const char *)p.ptr : "";
    char msg[TK_ASSERT_MSG_MAX];
    snprintf(msg, sizeof msg, "%.*sassertion failed: %s — %s", (int)p.len, pp, what, detail);
    tk_panic(msg);
}

// tk_assert_fail_bare — the exit for the two assertions whose ONLY input is a bool: naming what
// they got ("false") tells a reader nothing the assertion's own name did not, so these keep their
// historic one-clause message. The scenario prefix still applies, and it is empty by default, so the
// text is byte-identical to the pre-scenario seed for every existing call site.
static void tk_assert_fail_bare(const char *what) {
    tk_str p = tk_assert_scenario_prefix();
    const char *pp = p.len ? (const char *)p.ptr : "";
    char msg[TK_ASSERT_MSG_MAX];
    snprintf(msg, sizeof msg, "%.*sassertion failed: %s", (int)p.len, pp, what);
    tk_panic(msg);
}

// tk_assert_substr — the plain byte-substring scan both str_contains and str_absent decide on; no
// allocation. An EMPTY needle is contained in any haystack (the vacuous truth the historic seed had).
static bool tk_assert_substr(tk_str hay, tk_str needle) {
    if (needle.len == 0) return true;
    if (needle.len > hay.len) return false;
    for (size_t i = 0; i + needle.len <= hay.len; i += 1) {
        size_t j = 0;
        while (j < needle.len && hay.ptr[i + j] == needle.ptr[j]) j += 1;
        if (j == needle.len) return true;
    }
    return false;
}

// tk_assert_str_eq — byte equality of two spans, NUL-tolerant (never strcmp).
static bool tk_assert_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

TK_ASSERT_WEAK void teko__assert__is_true(bool c)  { if (!c) tk_assert_fail_bare("is_true"); }
TK_ASSERT_WEAK void teko__assert__is_false(bool c) { if ( c) tk_assert_fail_bare("is_false"); }

TK_ASSERT_WEAK void teko__assert__scenario_begin(tk_str name) { tk_assert_scenario_set(name); }
TK_ASSERT_WEAK void teko__assert__scenario_ok(void)           { tk_assert_scenario_ok(); }

TK_ASSERT_WEAK void teko__assert__eq_i64(int64_t got, int64_t want) {
    if (got == want) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %" PRId64 ", got %" PRId64, want, got);
    tk_assert_fail("eq_i64", d);
}

TK_ASSERT_WEAK void teko__assert__ne_i64(int64_t got, int64_t want) {
    if (got != want) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected anything but %" PRId64 ", got %" PRId64, want, got);
    tk_assert_fail("ne_i64", d);
}

TK_ASSERT_WEAK void teko__assert__lt_i64(int64_t got, int64_t bound) {
    if (got < bound) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected < %" PRId64 ", got %" PRId64, bound, got);
    tk_assert_fail("lt_i64", d);
}

TK_ASSERT_WEAK void teko__assert__le_i64(int64_t got, int64_t bound) {
    if (got <= bound) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected <= %" PRId64 ", got %" PRId64, bound, got);
    tk_assert_fail("le_i64", d);
}

TK_ASSERT_WEAK void teko__assert__gt_i64(int64_t got, int64_t bound) {
    if (got > bound) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected > %" PRId64 ", got %" PRId64, bound, got);
    tk_assert_fail("gt_i64", d);
}

TK_ASSERT_WEAK void teko__assert__ge_i64(int64_t got, int64_t bound) {
    if (got >= bound) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected >= %" PRId64 ", got %" PRId64, bound, got);
    tk_assert_fail("ge_i64", d);
}

TK_ASSERT_WEAK void teko__assert__eq_u64(uint64_t got, uint64_t want) {
    if (got == want) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %" PRIu64 ", got %" PRIu64, want, got);
    tk_assert_fail("eq_u64", d);
}

TK_ASSERT_WEAK void teko__assert__eq_f64(double got, double want, double tol) {
    double diff = got - want;
    if (diff < 0) diff = -diff;
    if (diff <= tol) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %.17g within %.17g, got %.17g", want, tol, got);
    tk_assert_fail("eq_f64", d);
}

TK_ASSERT_WEAK void teko__assert__eq_str(tk_str got, tk_str want) {
    if (tk_assert_str_eq(got, want)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected \"%.*s\", got \"%.*s\"",
             (int)want.len, (const char *)want.ptr, (int)got.len, (const char *)got.ptr);
    tk_assert_fail("eq_str", d);
}

TK_ASSERT_WEAK void teko__assert__ne_str(tk_str got, tk_str want) {
    if (!tk_assert_str_eq(got, want)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected anything but \"%.*s\", got \"%.*s\"",
             (int)want.len, (const char *)want.ptr, (int)got.len, (const char *)got.ptr);
    tk_assert_fail("ne_str", d);
}

TK_ASSERT_WEAK void teko__assert__str_contains(tk_str hay, tk_str needle) {
    if (tk_assert_substr(hay, needle)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected to contain \"%.*s\", got \"%.*s\"",
             (int)needle.len, (const char *)needle.ptr, (int)hay.len, (const char *)hay.ptr);
    tk_assert_fail("str_contains", d);
}

TK_ASSERT_WEAK void teko__assert__str_absent(tk_str hay, tk_str needle) {
    if (!tk_assert_substr(hay, needle)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected NOT to contain \"%.*s\", got \"%.*s\"",
             (int)needle.len, (const char *)needle.ptr, (int)hay.len, (const char *)hay.ptr);
    tk_assert_fail("str_absent", d);
}

TK_ASSERT_WEAK void teko__assert__str_starts_with(tk_str hay, tk_str prefix) {
    if (prefix.len <= hay.len && (prefix.len == 0 || memcmp(hay.ptr, prefix.ptr, prefix.len) == 0)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected to start with \"%.*s\", got \"%.*s\"",
             (int)prefix.len, (const char *)prefix.ptr, (int)hay.len, (const char *)hay.ptr);
    tk_assert_fail("str_starts_with", d);
}

TK_ASSERT_WEAK void teko__assert__str_ends_with(tk_str hay, tk_str suffix) {
    if (suffix.len <= hay.len && (suffix.len == 0 || memcmp(hay.ptr + (hay.len - suffix.len), suffix.ptr, suffix.len) == 0)) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected to end with \"%.*s\", got \"%.*s\"",
             (int)suffix.len, (const char *)suffix.ptr, (int)hay.len, (const char *)hay.ptr);
    tk_assert_fail("str_ends_with", d);
}

TK_ASSERT_WEAK void teko__assert__len_eq(uint64_t got, uint64_t want) {
    if (got == want) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected length %" PRIu64 ", got %" PRIu64, want, got);
    tk_assert_fail("len_eq", d);
}

TK_ASSERT_WEAK void teko__assert__bytes_eq(tk_str got, tk_str want) {
    if (got.len != want.len) {
        char dl[TK_ASSERT_MSG_MAX];
        snprintf(dl, sizeof dl, "expected %" PRIu64 " bytes, got %" PRIu64, want.len, got.len);
        tk_assert_fail("bytes_eq", dl);
    }
    for (uint64_t i = 0; i < got.len; i += 1) {
        if (got.ptr[i] == want.ptr[i]) continue;
        char d[TK_ASSERT_MSG_MAX];
        snprintf(d, sizeof d, "expected byte 0x%02X at index %" PRIu64 ", got 0x%02X",
                 (unsigned)want.ptr[i], i, (unsigned)got.ptr[i]);
        tk_assert_fail("bytes_eq", d);
    }
}

TK_ASSERT_WEAK void teko__assert__is_present(tk_str what, bool present) {
    if (present) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %.*s to be present, got null", (int)what.len, (const char *)what.ptr);
    tk_assert_fail("is_present", d);
}

TK_ASSERT_WEAK void teko__assert__is_absent(tk_str what, bool present) {
    if (!present) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %.*s to be null, got a present value", (int)what.len, (const char *)what.ptr);
    tk_assert_fail("is_absent", d);
}

TK_ASSERT_WEAK void teko__assert__is_ok(tk_str what, bool ok) {
    if (ok) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %.*s to succeed, got the error member", (int)what.len, (const char *)what.ptr);
    tk_assert_fail("is_ok", d);
}

TK_ASSERT_WEAK void teko__assert__is_error(tk_str what, bool ok) {
    if (!ok) return;
    char d[TK_ASSERT_MSG_MAX];
    snprintf(d, sizeof d, "expected %.*s to yield its error member, got a value", (int)what.len, (const char *)what.ptr);
    tk_assert_fail("is_error", d);
}
