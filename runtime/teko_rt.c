// runtime/teko_rt.c — libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#include "teko_rt.h"
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort

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

// --- teko::assert — fail loud on a false assertion (M.1). Canonical: src/assert/assert.tks. ---
void teko__assert__is_true(bool c)  { if (!c) tk_panic("assertion failed: is_true"); }
void teko__assert__is_false(bool c) { if ( c) tk_panic("assertion failed: is_false"); }

void teko__assert__str_contains(tk_str hay, tk_str needle) {
    // Plain byte-substring scan over the spans; no allocation. Empty needle ⊆ any hay.
    if (needle.len == 0) return;
    if (needle.len <= hay.len) {
        for (size_t i = 0; i + needle.len <= hay.len; i += 1) {
            size_t j = 0;
            while (j < needle.len && hay.ptr[i + j] == needle.ptr[j]) j += 1;
            if (j == needle.len) return;   // found
        }
    }
    tk_panic("assertion failed: str_contains");
}

_Noreturn void tk_panic_div0(void)     { tk_panic("division by zero"); }
_Noreturn void tk_panic_oob(void)      { tk_panic("index out of bounds"); }
_Noreturn void tk_panic_cast(void)     { tk_panic("impossible conversion"); }
_Noreturn void tk_panic_overflow(void) { tk_panic("integer overflow"); }
