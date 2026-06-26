// src/runtime/teko_rt.c   (namespace 'teko::runtime')
// libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#include "teko_rt.h"
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort, malloc, _Exit
#include <string.h>   // memcpy
#include <signal.h>   // signal — native crash backtraces (C1.9)
#include <execinfo.h> // backtrace, backtrace_symbols_fd (C1.9)

// (C1.9) NATIVE STACK TRACES. A generated Teko program links this runtime; on a panic (M.1)
// or a fatal signal (a bug in generated code), print a C backtrace to stderr — the frames carry
// the generated function symbols (the mangled Teko names), so a native crash is debuggable
// without a debugger. (Per-frame Teko file:line via `.tsym` is a later enhancement — Eixo E3;
// this delivers the call stack now.) In the bootstrap (which also links this runtime via the VM),
// main.c installs ITS OWN handler INSIDE main(), AFTER this constructor runs — so it wins there;
// this handler is the active one only in generated programs (which have no such main).
static void tk_backtrace(void) {
    void *frames[64];
    int n = backtrace(frames, 64);
    fputs("teko: stack trace:\n", stderr);
    backtrace_symbols_fd(frames, n, 2 /* stderr */);
}
static void tk_rt_crash_handler(int sig) {
    fputs("\nteko: FATAL signal — a generated program crashed (M.1).\n", stderr);
    tk_backtrace();
    _Exit(128 + sig);   // async-signal-safe
}
__attribute__((constructor)) static void tk_rt_install_crash_handler(void) {
    signal(SIGSEGV, tk_rt_crash_handler);
    signal(SIGBUS,  tk_rt_crash_handler);
    signal(SIGILL,  tk_rt_crash_handler);
    signal(SIGFPE,  tk_rt_crash_handler);
}

// --- string interpolation builders (self-host parity) ---
// tk_str_concat — a fresh buffer = a.ptr[0..a.len] ++ b.ptr[0..b.len]; the result OWNS it.
// Allocation failure PANICS (M.1 fail-loud, never silent corruption). Leak-tolerant (M.5 —
// short-lived); a zero-length result uses a 1-byte buffer so ptr is never NULL+len mismatch.
tk_str tk_str_concat(tk_str a, tk_str b) {
    size_t n = a.len + b.len;
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str concat)");
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ buf, n };
}

// decimal text of an unsigned 64-bit value into a fresh str (no leading zeros; "0" for 0).
tk_str tk_u64_to_str(uint64_t v) {
    char tmp[20];                 // u64 max = 20 digits
    size_t i = 0;
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } }
    tk_byte *buf = malloc(i ? i : 1);
    if (buf == NULL) tk_panic("out of memory (int to str)");
    for (size_t j = 0; j < i; j += 1) buf[j] = (tk_byte)tmp[i - 1 - j];   // reverse
    return (tk_str){ buf, i };
}

// decimal text of a signed 64-bit value; a '-' prefix for negatives (uses the unsigned
// magnitude so INT64_MIN is handled without overflow).
tk_str tk_i64_to_str(int64_t v) {
    if (v >= 0) return tk_u64_to_str((uint64_t)v);
    uint64_t mag = (uint64_t)(-(v + 1)) + 1u;    // |INT64_MIN| without UB
    tk_str digits = tk_u64_to_str(mag);
    tk_byte *buf = malloc(digits.len + 1);
    if (buf == NULL) tk_panic("out of memory (int to str)");
    buf[0] = (tk_byte)'-';
    if (digits.len) memcpy(buf + 1, digits.ptr, digits.len);
    return (tk_str){ buf, digits.len + 1 };
}

// --- Phase 3 str/byte stdlib (modeled exactly on tk_str_concat — fresh malloc'd buffer the
// result OWNS, tk_panic on OOM (M.1), leak-tolerant (M.5)) ---

// tk_str_of_bytes — COPY a []byte slice (same {ptr,len} shape as tk_str) into a fresh owned
// str. A zero-length result uses a 1-byte buffer so ptr is never NULL with a stale len.
tk_str tk_str_of_bytes(tk_str bytes) {
    size_t n = bytes.len;
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str of bytes)");
    if (n) memcpy(buf, bytes.ptr, n);
    return (tk_str){ buf, n };
}

// tk_one_byte — a fresh 1-byte str holding c.
tk_str tk_one_byte(tk_byte c) {
    tk_byte *buf = malloc(1);
    if (buf == NULL) tk_panic("out of memory (one byte)");
    buf[0] = c;
    return (tk_str){ buf, 1 };
}

// tk_str_concat3 — a ++ b ++ c via two tk_str_concat steps. Each step allocates a fresh
// owned buffer (the intermediate a++b is leaked — M.5 short-lived).
tk_str tk_str_concat3(tk_str a, tk_str b, tk_str c) {
    return tk_str_concat(tk_str_concat(a, b), c);
}

// tk_ftoa — x rendered as %.17g (exact binary64 round-trip; same renderer as codegen's float
// literal emission) into a temp, then COPIED into a fresh owned str.
tk_str tk_ftoa(double x) {
    char tmp[40];                 // %.17g of a double fits in well under 40 chars
    int n = snprintf(tmp, sizeof tmp, "%.17g", x);
    if (n < 0) tk_panic("ftoa: snprintf failed");
    size_t len = (size_t)n;
    tk_byte *buf = malloc(len ? len : 1);
    if (buf == NULL) tk_panic("out of memory (ftoa)");
    if (len) memcpy(buf, tmp, len);
    return (tk_str){ buf, len };
}

// --- Phase 3 str query/slice builtins (query helpers allocate nothing; slice helpers follow
// tk_str_concat's ownership — a fresh malloc'd buffer the result OWNS, tk_panic on OOM) ---

// tk_str_eq — same length AND same bytes. memcmp (NOT strcmp — strings may hold embedded NUL);
// a zero-length pair compares equal without touching ptr (memcmp of 0 bytes is well-defined).
bool tk_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

// tk_str_slice — the bytes [start, end) COPIED into a fresh owned str. Bounds: an out-of-range
// slice (start > end, or end past the byte length) PANICS (M.1, fail-loud — matches the VM's
// index bounds check). The empty slice (start == end, in range) uses a 1-byte buffer so ptr is
// never NULL with a stale len (parity with tk_str_concat's zero-length handling).
tk_str tk_str_slice(tk_str s, uint64_t start, uint64_t end) {
    if (start > end || end > s.len) tk_panic("string slice out of range");
    size_t n = (size_t)(end - start);
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str slice)");
    if (n) memcpy(buf, s.ptr + start, n);
    return (tk_str){ buf, n };
}

// tk_str_slice_to — slice from the start to `end`.
tk_str tk_str_slice_to(tk_str s, uint64_t end) {
    return tk_str_slice(s, 0, end);
}

// tk_str_slice_from — slice from `start` to the byte length.
tk_str tk_str_slice_from(tk_str s, uint64_t start) {
    return tk_str_slice(s, start, s.len);
}

// tk_str_len — the byte length (no allocation).
uint64_t tk_str_len(tk_str s) {
    return s.len;
}

// tk_str_ends_with — the tail of s equals suffix. A suffix longer than s can't match; otherwise
// memcmp the last suffix.len bytes. An empty suffix matches (memcmp of 0 bytes is equal).
bool tk_str_ends_with(tk_str s, tk_str suffix) {
    if (suffix.len > s.len) return false;
    return memcmp(s.ptr + (s.len - suffix.len), suffix.ptr, suffix.len) == 0;
}

// tk_str_contains — naive byte search: true iff needle occurs anywhere in s. An empty needle is
// trivially contained (matches at offset 0). Each candidate offset is memcmp'd against needle;
// the last candidate offset is s.len - needle.len (inclusive).
bool tk_str_contains(tk_str s, tk_str needle) {
    if (needle.len == 0) return true;
    if (needle.len > s.len) return false;
    size_t last = s.len - needle.len;
    for (size_t i = 0; i <= last; i += 1) {
        if (memcmp(s.ptr + i, needle.ptr, needle.len) == 0) return true;
    }
    return false;
}

// tk_f64_g17 — x as %.17g in a fresh owned str (the host float renderer; same behavior as
// tk_ftoa, exposed under the name the checker/codegen reference for `f64_g17`).
tk_str tk_f64_g17(double x) {
    char tmp[40];                 // %.17g of a double fits in well under 40 chars
    int n = snprintf(tmp, sizeof tmp, "%.17g", x);
    if (n < 0) tk_panic("f64_g17: snprintf failed");
    size_t len = (size_t)n;
    tk_byte *buf = malloc(len ? len : 1);
    if (buf == NULL) tk_panic("out of memory (f64_g17)");
    if (len) memcpy(buf, tmp, len);
    return (tk_str){ buf, len };
}

void *tk_alloc(size_t n) {
    void *p = malloc(n ? n : 1);   // n→1 so a zero-size alloc still yields a unique pointer
    if (p == NULL) tk_panic("out of memory");
    return p;
}

void tk_print(tk_str s) {
    // Exactly s.len bytes; tolerate embedded NUL; no strlen/puts.
    fwrite(s.ptr, 1, s.len, stdout);
}

void tk_println(tk_str s) {
    tk_print(s);
    fputc('\n', stdout);   // single 0x0A
}

_Noreturn void tk_panic(const char *msg) {
    // Loud + non-zero (M.1): SIGABRT via abort() (conventional 134).
    fputs("teko: panic: ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    tk_backtrace();   // (C1.9) show the call stack
    abort();
}

// teko::assert lives in its own C seed now (src/assert/assert.{c,h}); driver.c::run_cc
// compiles that source alongside this one so generated programs still link the symbols.

// the Teko-level `panic(str)` — same loud abort (M.1) but the message is a tk_str (ptr+len),
// tolerating embedded NUL (exactly msg.len bytes to stderr). The error case `panic(error)` lowers
// to its `.message` str at the call site.
_Noreturn void tk_panic_str(tk_str msg) {
    fputs("teko: panic: ", stderr);
    fwrite(msg.ptr, 1, msg.len, stderr);
    fputc('\n', stderr);
    tk_backtrace();   // (C1.9) show the call stack
    abort();
}
// the Teko-level `exit(<int>)` — end the program with a status code (no panic message).
_Noreturn void tk_exit(int32_t code) { exit(code); }

_Noreturn void tk_panic_div0(void)     { tk_panic("division by zero"); }
_Noreturn void tk_panic_oob(void)      { tk_panic("index out of bounds"); }
_Noreturn void tk_panic_cast(void)     { tk_panic("impossible conversion"); }
_Noreturn void tk_panic_overflow(void) { tk_panic("integer overflow"); }

// (C1.7) positioned OOB — print "line:col: " (same shape as the VM's vm_panic_pos), then the
// canonical "teko: panic: index out of bounds\n", so VM and native locate identically.
_Noreturn void tk_panic_oob_at(uint32_t line, uint32_t col) {
    char buf[32];
    snprintf(buf, sizeof buf, "%u:%u: ", (unsigned)line, (unsigned)col);
    fputs(buf, stderr);
    tk_panic_oob();
}
