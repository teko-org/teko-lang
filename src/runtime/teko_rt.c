// src/runtime/teko_rt.c   (namespace 'teko::runtime')
// libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#include "teko_rt.h"
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort, malloc
#include <string.h>   // memcpy

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
