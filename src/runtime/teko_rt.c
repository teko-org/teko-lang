// src/runtime/teko_rt.c   (namespace 'teko::runtime')
// libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
// (C7.1f) expose POSIX (setenv/fork/execvp/opendir/getlogin/…) under strict `-std=c23` — musl
// (the Alpine/Linux pipeline) hides them otherwise. Harmless on macOS/glibc. MUST precede includes.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "teko_rt.h"
#include <ctype.h>    // isalpha (ROUND 0 UTF-8 codepoint ops)
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort, malloc, free, _Exit
#include <string.h>   // memcpy
#include <stddef.h>   // max_align_t, offsetof — arena chunk alignment (S1; also via teko_rt.h)
#include <inttypes.h> // PRId64, PRIx64, PRIX64 — format spec helpers
#include <stdarg.h>   // va_list, va_start/va_copy/va_end — fmt_alloc_vsnprintf heap-overflow path (issue #48)
#include <signal.h>   // signal — native crash backtraces (C1.9)
// execinfo (backtrace) exists on macOS + glibc, but NOT musl (the Alpine/Linux pipeline). Guard it
// so the runtime is musl-portable (C7.1f); without it the backtrace degrades to a one-line notice.
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h> // backtrace, backtrace_symbols_fd (C1.9)
#define TK_HAVE_BACKTRACE 1
#endif
#ifdef _WIN32
#include "../win32_compat.h"  // chdir→_chdir, mkdir, getcwd, setenv, dirent shim, tk_win32_spawnvp
#include <io.h>        // _dup, _dup2, _close — fd-redirect around tk_rt_run_quiet's _spawnvp (issue #73)
#else
#include <unistd.h>   // chdir, fork, execvp, _exit (host FFI bottoms)
#include <sys/wait.h> // waitpid — teko::process::run
#include <sys/resource.h> // getrusage — teko::mem::peak_rss (#148: the compiler reports its own memory cost)
#include <dirent.h>   // opendir/readdir — teko::fs::list_dir
#include <sys/stat.h> // mkdir — teko::fs::mkdir (build output dir)
#include <fcntl.h>    // O_WRONLY — /dev/null redirect for tk_rt_run_quiet (issue #73 cc probe)
#endif
#include <errno.h>    // errno/EEXIST — mkdir idempotence
#include <time.h>     // clock_gettime, localtime_r, CLOCK_REALTIME — teko::time ROUND 0

// (C1.9 / E4) NATIVE STACK TRACES. A generated Teko program links this runtime; on a panic (M.1)
// or a fatal signal (a bug in generated code), print a backtrace to stderr — the frames carry the
// generated function symbols (the mangled Teko names). E4: each frame is RESOLVED to its Teko
// `name (file:line)` via the `.tsym` map emitted beside the binary (E3), loaded as `<argv0>.tsym`.
// Without the map (or argv) it degrades to the raw C symbols. In the bootstrap (which also links
// this runtime via the VM), main.c installs ITS OWN handler INSIDE main() — so it wins there; this
// handler is active only in generated programs (which have no such main).
static int    tk_g_argc;   // captured argv (defined below; used here to locate <argv0>.tsym)
static char **tk_g_argv;
#if defined(TK_HAVE_BACKTRACE)
static char  *tk_tsym_buf; // the loaded .tsym contents, or NULL (process-lifetime)

// Load `<argv0>.tsym` once (best-effort — missing/unreadable is fine).
static void tk_tsym_load(void) {
    if (tk_tsym_buf != NULL || tk_g_argv == NULL || tk_g_argc < 1) return;
    char path[4096];
    snprintf(path, sizeof path, "%s.tsym", tk_g_argv[0]);
    FILE *f = fopen(path, "rb");
    if (f == NULL) return;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
    long sz = ftell(f);
    if (sz <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return; }
    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) { fclose(f); return; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    tk_tsym_buf = buf;
}

// If a .tsym c-symbol (a line's first \t-field) occurs in `frame`, print "=> <teko> (<file:line>)".
static void tk_tsym_resolve(const char *frame) {
    if (tk_tsym_buf == NULL) return;
    char *p = tk_tsym_buf;
    while (*p != '\0') {
        char *eol = strchr(p, '\n'); if (eol == NULL) eol = p + strlen(p);
        if (*p != '#') {
            char *tab = memchr(p, '\t', (size_t)(eol - p));
            if (tab != NULL) {
                size_t clen = (size_t)(tab - p);
                char csym[256];
                if (clen > 0 && clen < sizeof csym) {
                    memcpy(csym, p, clen); csym[clen] = '\0';
                    if (strstr(frame, csym) != NULL) {
                        fputs("        => ", stderr);
                        fwrite(tab + 1, 1, (size_t)(eol - (tab + 1)), stderr);   // <teko-name>\t<file:line>
                        fputc('\n', stderr);
                        return;
                    }
                }
            }
        }
        p = (*eol == '\0') ? eol : eol + 1;
    }
}

static void tk_backtrace(void) {
    void *frames[64];
    int n = backtrace(frames, 64);
    fputs("teko: stack trace:\n", stderr);
    char **syms = backtrace_symbols(frames, n);
    if (syms == NULL) { backtrace_symbols_fd(frames, n, 2 /* stderr */); return; }
    tk_tsym_load();
    for (int i = 0; i < n; i += 1) {
        fputs(syms[i], stderr); fputc('\n', stderr);
        tk_tsym_resolve(syms[i]);   // E4: append the Teko name + file:line, if known
    }
    free(syms);
}
#else
// musl (or any platform without execinfo): no symbolic backtrace — degrade to a one-line notice.
static void tk_backtrace(void) { fputs("teko: stack trace unavailable on this platform (no execinfo)\n", stderr); }
#endif
static void tk_rt_crash_handler(int sig) {
    fputs("\nteko: FATAL signal — a generated program crashed (M.1).\n", stderr);
    tk_backtrace();
    _Exit(128 + sig);   // async-signal-safe
}
__attribute__((constructor)) static void tk_rt_install_crash_handler(void) {
    signal(SIGSEGV, tk_rt_crash_handler);
#ifndef _WIN32
    signal(SIGBUS,  tk_rt_crash_handler);   // not defined on Windows
#endif
    signal(SIGILL,  tk_rt_crash_handler);
    signal(SIGFPE,  tk_rt_crash_handler);
}

// --- string interpolation builders (self-host parity) ---
// (#148 dark matter) obs hooks for the MALLOC-backed str/format helpers (invisible to the arena
// tables): attribute fresh-buffer bytes to the CALLING fn. Fwd decls — the obs block lives below.
static int tk_obs_enabled(void);
static void tk_obs_mstr_note(size_t n, void *ra);

// tk_str_concat — a fresh buffer = a.ptr[0..a.len] ++ b.ptr[0..b.len]; the result OWNS it.
// Allocation failure PANICS (M.1 fail-loud, never silent corruption). Leak-tolerant (M.5 —
// short-lived); a zero-length result uses a 1-byte buffer so ptr is never NULL+len mismatch.
tk_str tk_str_concat(tk_str a, tk_str b) {
    size_t n = a.len + b.len;
    if (tk_obs_enabled() == 1) tk_obs_mstr_note(n ? n : 1, __builtin_return_address(0));
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str concat)");
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ buf, n };
}

// (C7.1a) marshalling — the raw byte pointer of a Teko str (NOT NUL-terminated), for ptr+len C
// APIs like write(fd,buf,len). Borrows the str's buffer — valid only while the str is alive; the
// FFI boundary is unsafe by contract (cast away const). [teko::mem::as_ptr]
void *tk_as_ptr(tk_str s) { return (void *)s.ptr; }

// (C7.1a) marshalling — a fresh NUL-terminated C copy of a Teko str, for C `char*` APIs (getenv,
// fopen, …). A Teko str is NOT NUL-terminated, so this copy is required when the foreign function
// expects a `char*`. The caller owns the buffer (whole-program lifetime in the seed). [teko::mem::as_cstr]
void *tk_cstr_dup(tk_str s) {
    char *buf = malloc(s.len + 1);
    if (buf == NULL) tk_panic("out of memory (cstr dup)");
    if (s.len) memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    return buf;
}

// (C7.1a) marshalling — copy a NUL-terminated C string from a foreign pointer into a fresh Teko
// str (octets up to the NUL, exclusive). A NULL pointer yields the empty str. [teko::mem::str_from_cstr]
tk_str tk_str_from_cstr(const void *p) {
    size_t n = (p == NULL) ? 0 : strlen((const char *)p);
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str from cstr)");
    if (n) memcpy(buf, p, n);
    return (tk_str){ buf, n };
}

// (C7.1a) marshalling — copy n octets from a foreign pointer into a fresh Teko []byte. A NULL
// pointer or n==0 yields an empty slice (a 1-byte buffer so the pointer is distinct). The codegen
// lifts the returned {ptr,len} to the program's tk_slice_byte. [teko::mem::bytes_from_ptr]
tk_ffi_bytes tk_bytes_from_ptr(const void *p, uint64_t n) {
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (bytes from ptr)");
    if (n && p) memcpy(buf, p, (size_t)n);
    return (tk_ffi_bytes){ buf, (n && p) ? n : 0 };
}

// decimal text of an unsigned 64-bit value into a fresh str (no leading zeros; "0" for 0).
tk_str tk_u64_to_str(uint64_t v) {
    char tmp[20];                 // u64 max = 20 digits
    size_t i = 0;
    if (v == 0) { tmp[i++] = '0'; }
    else { while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } }
    if (tk_obs_enabled() == 1) tk_obs_mstr_note(i ? i : 1, __builtin_return_address(0));
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
    if (tk_obs_enabled() == 1) tk_obs_mstr_note(n ? n : 1, __builtin_return_address(0));
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str of bytes)");
    if (n) memcpy(buf, bytes.ptr, n);
    return (tk_str){ buf, n };
}

// tk_bytes_of_str — zero-copy view of a str's bytes as a tk_slice_byte. Same ptr and len,
// reinterpret-cast from const to mutable pointer; the slice is read-only in practice.
tk_slice_byte tk_bytes_of_str(tk_str s) {
    return (tk_slice_byte){ (tk_byte *)s.ptr, s.len };
}

// rt_valid_utf8 — strict RFC 3629 well-formedness check (reject overlong encodings, UTF-16
// surrogates U+D800..U+DFFF, and codepoints > U+10FFFF). Mirrors src/text/text.c's static
// valid_utf8 byte-for-byte; duplicated here (not shared) because teko_rt.c is a SEPARATE link
// unit from the compiler's own bootstrap text.c — generated Teko programs link teko_rt.c only,
// never the compiler-internal text.c (ROUND 0 — str_from_utf8 the user-facing builtin).
static bool rt_valid_utf8(const tk_byte *s, size_t len) {
    size_t i = 0;
    while (i < len) {
        tk_byte b = s[i];
        if (b <= 0x7F) { i += 1; continue; }        // ASCII — a single byte

        size_t  cont;                                // continuation bytes that follow
        tk_byte lo, hi;                              // valid range for the FIRST of them
        if      (b >= 0xC2 && b <= 0xDF) { cont = 1; lo = 0x80; hi = 0xBF; }
        else if (b == 0xE0)              { cont = 2; lo = 0xA0; hi = 0xBF; } // no overlong
        else if (b >= 0xE1 && b <= 0xEC) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xED)              { cont = 2; lo = 0x80; hi = 0x9F; } // no surrogate
        else if (b >= 0xEE && b <= 0xEF) { cont = 2; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF0)              { cont = 3; lo = 0x90; hi = 0xBF; } // no overlong
        else if (b >= 0xF1 && b <= 0xF3) { cont = 3; lo = 0x80; hi = 0xBF; }
        else if (b == 0xF4)              { cont = 3; lo = 0x80; hi = 0x8F; } // <= U+10FFFF
        else return false;                           // 0x80..0xC1, 0xF5..0xFF: invalid lead

        if (len - i <= cont) return false;           // truncated: not enough bytes
        if (s[i + 1] < lo || s[i + 1] > hi) return false;            // first continuation
        for (size_t k = 2; k <= cont; k += 1) {                      // the rest, plain
            if (s[i + k] < 0x80 || s[i + k] > 0xBF) return false;
        }
        i += cont + 1;
    }
    return true;
}

// tk_rt_str_from_utf8 — the validated bytes -> str constructor (ROUND 0 / B.36). ok → a fresh
// str COPYING the bytes; !ok → err "invalid UTF-8". Takes ptr+len (the []byte ABI; the codegen
// lift splits the generated tk_slice_byte the same way write_file_bytes's data arg is split).
tk_ffi_sres tk_rt_str_from_utf8(const tk_byte *ptr, uint64_t len) {
    if (!rt_valid_utf8(ptr, (size_t)len)) {
        tk_byte *msg = malloc(13);
        if (msg == NULL) tk_panic("out of memory (str_from_utf8 error)");
        memcpy(msg, "invalid UTF-8", 13);
        return (tk_ffi_sres){ .ok = false, .err = (tk_str){ msg, 13 } };
    }
    tk_byte *buf = malloc(len ? len : 1);
    if (buf == NULL) tk_panic("out of memory (str_from_utf8)");
    if (len) memcpy(buf, ptr, (size_t)len);
    return (tk_ffi_sres){ .ok = true, .value = (tk_str){ buf, len } };
}

// tk_one_byte — a fresh 1-byte str holding c.
tk_str tk_one_byte(tk_byte c) {
    tk_byte *buf = malloc(1);
    if (buf == NULL) tk_panic("out of memory (one byte)");
    buf[0] = c;
    return (tk_str){ buf, 1 };
}

// tk_char_to_u32 — decode a `char` (1–4 UTF-8 bytes) to its scalar codepoint. The bytes are valid
// UTF-8 by construction (the lexer validated the `c'…'` literal), so a straight lead+continuation
// decode is sufficient. A 0-length char is impossible (the lexer rejects it) — returns 0 if seen.
uint32_t tk_char_to_u32(tk_char c) {
    if (c.len == 0) return 0;
    uint8_t b0 = c.ptr[0];
    if (b0 <= 0x7F) return b0;                                  // 1 byte (ASCII)
    if (c.len == 2) return ((uint32_t)(b0 & 0x1F) << 6)
                         |  (uint32_t)(c.ptr[1] & 0x3F);
    if (c.len == 3) return ((uint32_t)(b0 & 0x0F) << 12)
                         | ((uint32_t)(c.ptr[1] & 0x3F) << 6)
                         |  (uint32_t)(c.ptr[2] & 0x3F);
    return ((uint32_t)(b0 & 0x07) << 18)                        // 4 bytes
         | ((uint32_t)(c.ptr[1] & 0x3F) << 12)
         | ((uint32_t)(c.ptr[2] & 0x3F) << 6)
         |  (uint32_t)(c.ptr[3] & 0x3F);
}

// utf8_lead_len — byte width of a UTF-8 sequence starting with lead byte b0.
// Returns 1 for ASCII (b0 < 0x80), 2 for 0xC0–0xDF, 3 for 0xE0–0xEF, 4 for 0xF0–0xF7.
// Any other byte (continuation or invalid) is treated as 1 (graceful degradation).
static size_t utf8_lead_len(uint8_t b0) {
    if (b0 < 0x80) return 1;
    if (b0 < 0xC0) return 1;   // continuation byte — malformed; consume as 1
    if (b0 < 0xE0) return 2;
    if (b0 < 0xF0) return 3;
    return 4;
}

// tk_str_len_chars — count UTF-8 codepoints in s (no allocation).
uint64_t tk_str_len_chars(tk_str s) {
    uint64_t count = 0;
    size_t i = 0;
    while (i < s.len) {
        i += utf8_lead_len(s.ptr[i]);
        count += 1;
    }
    return count;
}

// tk_str_chars — split s into a malloc'd array of tk_char, one per UTF-8 codepoint.
// Each tk_char borrows INTO s.ptr (no copy of codepoint bytes).
tk_slice_char tk_str_chars(tk_str s) {
    uint64_t count = tk_str_len_chars(s);
    tk_char *arr = malloc((count ? count : 1) * sizeof *arr);
    if (arr == NULL) tk_panic("out of memory (chars)");
    uint64_t ci = 0;
    size_t i = 0;
    while (i < s.len) {
        size_t w = utf8_lead_len(s.ptr[i]);
        if (i + w > s.len) w = s.len - i;   // clamp if bytes run short
        arr[ci] = (tk_char){ (uint8_t *)(s.ptr + i), (uint64_t)w };
        ci += 1;
        i  += w;
    }
    return (tk_slice_char){ arr, count };
}

// tk_str_concat3 REMOVED (2026-07-01) — superseded by `concat(params pieces: []str)`, bridged
// at the call site (codegen.c/.tks) by folding N pieces via tk_str_concat; no runtime symbol needed.

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

// --- Format spec helpers ($"{x:F2}" / $"{x:[fmt]}") ---
// All produce fresh malloc'd str; tk_panic on OOM. snprintf into a small stack buffer for the
// common case (all everyday formatted numbers fit); a user-supplied width/precision (e.g.
// `$"{x:F500}"`) can make snprintf's would-have-written length exceed that buffer, so every
// helper here is capacity-aware: `n` is snprintf's return (bytes it WOULD write, NOT bytes it
// DID write into a short buffer — see C11 7.21.6.5p3), and `fmt_from_buf` only trusts `tmp` for
// up to `n < cap` bytes. Once `n >= cap`, `tmp` holds a truncated result — this file always
// prefers CORRECT OUTPUT over silent truncation, so every call site re-runs its snprintf into a
// heap buffer sized `n + 1` (see fmt_alloc_vsnprintf) rather than ever memcpy-ing past what was
// actually written.
static tk_str fmt_from_buf(char *tmp, size_t cap, int n) {
    if (n < 0) n = 0;
    size_t len = (size_t)n;
    if (len >= cap) len = cap ? cap - 1 : 0;   // defensive: never trust more than snprintf actually wrote
    tk_byte *buf = malloc(len ? len : 1);
    if (buf == NULL) tk_panic("out of memory (fmt)");
    if (len) memcpy(buf, tmp, len);
    return (tk_str){ buf, len };
}
// fmt_alloc_vsnprintf — snprintf `format` with `args` into a HEAP buffer sized to fit the full
// (untruncated) result, and return it as an owned tk_str. Used as the overflow path once a
// stack-buffer snprintf reports `n >= cap` (issue #48): re-runs the SAME format/args at the
// correct size instead of ever copying past a truncated stack buffer. The heap buffer is
// per-call scratch — freed here, not process-lifetime (M.5 governs leaks, not this).
static tk_str fmt_alloc_vsnprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    va_list args2;
    va_copy(args2, args);
    char probe[1];
    int n = vsnprintf(probe, sizeof probe, format, args);
    va_end(args);
    if (n < 0) { va_end(args2); return fmt_from_buf(probe, sizeof probe, 0); }
    size_t need = (size_t)n + 1;               // + NUL, vsnprintf's own requirement
    char *heap = malloc(need);
    if (heap == NULL) { va_end(args2); tk_panic("out of memory (fmt)"); }
    int n2 = vsnprintf(heap, need, format, args2);
    va_end(args2);
    tk_str out = fmt_from_buf(heap, need, n2);
    free(heap);
    return out;
}
static int fmt_parse_prec(tk_str spec, int def) {
    // parse optional trailing digits from spec (e.g. "F2" → 2, "F" → def)
    // Clamp the accumulator at FMT_PREC_MAX (still consuming all digits) to avoid signed
    // overflow on a maliciously/accidentally huge digit run (e.g. `$"{x:[F999999999999]}"`);
    // any in-range value (well under this bound) round-trips exactly as before.
    enum { FMT_PREC_MAX = 10000 };
    int p = def; size_t i = 0;
    while (i < spec.len && (spec.ptr[i] < '0' || spec.ptr[i] > '9')) i++;
    if (i < spec.len) {
        p = 0;
        while (i < spec.len && spec.ptr[i] >= '0' && spec.ptr[i] <= '9') {
            if (p < FMT_PREC_MAX) p = p * 10 + (spec.ptr[i] - '0');
            if (p > FMT_PREC_MAX) p = FMT_PREC_MAX;
            i++;
        }
    }
    return p;
}
tk_str tk_fmt_f(double val, int prec) {
    char tmp[128]; int n = snprintf(tmp, sizeof tmp, "%.*f", prec, val);
    if (n < 0 || (size_t)n >= sizeof tmp) return fmt_alloc_vsnprintf("%.*f", prec, val);
    return fmt_from_buf(tmp, sizeof tmp, n);
}
tk_str tk_fmt_d(int64_t val, int width) {
    char tmp[64]; int n = snprintf(tmp, sizeof tmp, "%0*" PRId64, width, val);
    if (n < 0 || (size_t)n >= sizeof tmp) return fmt_alloc_vsnprintf("%0*" PRId64, width, val);
    return fmt_from_buf(tmp, sizeof tmp, n);
}
tk_str tk_fmt_x_upper(uint64_t val) { char tmp[32]; return fmt_from_buf(tmp, sizeof tmp, snprintf(tmp, sizeof tmp, "%" PRIX64, val)); }
tk_str tk_fmt_x_lower(uint64_t val) { char tmp[32]; return fmt_from_buf(tmp, sizeof tmp, snprintf(tmp, sizeof tmp, "%" PRIx64, val)); }
tk_str tk_fmt_e(double val, int prec) {
    char tmp[128]; int n = snprintf(tmp, sizeof tmp, "%.*e", prec, val);
    if (n < 0 || (size_t)n >= sizeof tmp) return fmt_alloc_vsnprintf("%.*e", prec, val);
    return fmt_from_buf(tmp, sizeof tmp, n);
}
tk_str tk_fmt_g(double val, int prec) {
    char tmp[128]; int n = snprintf(tmp, sizeof tmp, "%.*g", prec, val);
    if (n < 0 || (size_t)n >= sizeof tmp) return fmt_alloc_vsnprintf("%.*g", prec, val);
    return fmt_from_buf(tmp, sizeof tmp, n);
}
tk_str tk_fmt_b(uint64_t val) {
    char tmp[65]; int i = 0;
    if (val == 0) { tmp[i++] = '0'; } else { for (int b = 63; b >= 0; b--) { if ((val >> (unsigned)b) & 1u) { for (; b >= 0; b--) tmp[i++] = (char)('0' + ((val >> (unsigned)b) & 1u)); break; } } }
    return fmt_from_buf(tmp, sizeof tmp, i);
}
tk_str tk_fmt_p(double val, int prec) {
    char tmp[128]; int n = snprintf(tmp, sizeof tmp, "%.*f%%", prec, val * 100.0);
    if (n < 0 || (size_t)n >= sizeof tmp) return fmt_alloc_vsnprintf("%.*f%%", prec, val * 100.0);
    return fmt_from_buf(tmp, sizeof tmp, n);
}
// tk_fmt_n_f / tk_fmt_n_i — format with a thousands separator: snprintf the plain digits, then
// insert commas while copying into `out`. `out` must hold the digits PLUS one comma per group of
// 3 PLUS a sign PLUS NUL headroom; for large `n` (huge width/precision) the fixed-size `out`
// stack buffers can't hold that, so both fall back to a heap buffer sized to the worst case
// (n digits -> at most n commas -> 2*n+2 bytes is always enough) instead of over-writing a fixed
// array (issue #48 — `out[160]`/`out[48]` over-write).
tk_str tk_fmt_n_f(double val, int prec) {
    // Format with thousands separator: format without first, then insert commas.
    char tmp[128]; int n = snprintf(tmp, sizeof tmp, "%.*f", prec, val);
    if (n < 0) return fmt_from_buf(tmp, sizeof tmp, n);
    char tmp_stack[128];
    char *src = tmp;
    char *heap_src = NULL;
    if ((size_t)n >= sizeof tmp) {
        // re-run into a heap buffer big enough for the untruncated digits
        heap_src = malloc((size_t)n + 1);
        if (heap_src == NULL) tk_panic("out of memory (fmt)");
        int n2 = snprintf(heap_src, (size_t)n + 1, "%.*f", prec, val);
        if (n2 < 0) { free(heap_src); return fmt_from_buf(tmp_stack, sizeof tmp_stack, 0); }
        n = n2;
        src = heap_src;
    }
    // find decimal point (if any)
    int dot = n; for (int k = 0; k < n; k++) { if (src[k] == '.') { dot = k; break; } }
    size_t out_cap = (size_t)n * 2 + 2;   // worst case: a comma every digit, plus sign, plus slack
    char out_stack[160];
    char *out = (out_cap <= sizeof out_stack) ? out_stack : malloc(out_cap);
    if (out == NULL) { if (heap_src) free(heap_src); tk_panic("out of memory (fmt)"); }
    int o = 0, start = (src[0] == '-') ? 1 : 0;
    if (src[0] == '-') out[o++] = '-';
    for (int k = start; k < dot; k++) {
        int pos = dot - k - 1;
        out[o++] = src[k];
        if (pos > 0 && pos % 3 == 0) out[o++] = ',';
    }
    for (int k = dot; k < n; k++) out[o++] = src[k];
    tk_str result = fmt_from_buf(out, out_cap, o);
    if (out != out_stack) free(out);
    if (heap_src) free(heap_src);
    return result;
}
tk_str tk_fmt_n_i(int64_t val) {
    char tmp[32]; int n = snprintf(tmp, sizeof tmp, "%" PRId64, val);
    if (n <= 0) return fmt_from_buf(tmp, sizeof tmp, n);
    // n is bounded by int64 digit count (<= 20) here, so the fixed `tmp`/`out` are always
    // sufficient — no overflow path is reachable for this signature; kept capacity-aware for
    // consistency with tk_fmt_n_f and to stay correct if the format ever widens.
    size_t out_cap = (size_t)n * 2 + 2;
    char out_stack[48];
    char *out = (out_cap <= sizeof out_stack) ? out_stack : malloc(out_cap);
    if (out == NULL) tk_panic("out of memory (fmt)");
    int o = 0, start = (tmp[0] == '-') ? 1 : 0;
    if (tmp[0] == '-') out[o++] = '-';
    for (int k = start; k < n; k++) { int pos = n - k - 1; out[o++] = tmp[k]; if (pos > 0 && pos % 3 == 0) out[o++] = ','; }
    tk_str result = fmt_from_buf(out, out_cap, o);
    if (out != out_stack) free(out);
    return result;
}
// Dynamic dispatchers: parse first char of spec (case-insensitive) + optional digits.
tk_str tk_fmt_dyn_f64(double val, tk_str spec) {
    if (spec.len == 0) return tk_ftoa(val);
    int prec = fmt_parse_prec(spec, 6);
    switch (spec.ptr[0] | 0x20) {  // tolower
        case 'f': return tk_fmt_f(val, prec);
        case 'e': return tk_fmt_e(val, prec);
        case 'g': return tk_fmt_g(val, prec);
        case 'n': return tk_fmt_n_f(val, prec);
        case 'p': return tk_fmt_p(val, prec);
        default:  return tk_ftoa(val);
    }
}
tk_str tk_fmt_dyn_i64(int64_t val, tk_str spec) {
    if (spec.len == 0) return tk_i64_to_str(val);
    int prec = fmt_parse_prec(spec, 0);
    switch (spec.ptr[0] | 0x20) {
        case 'd': return tk_fmt_d(val, prec ? prec : 1);
        case 'x': return (spec.ptr[0] == 'X') ? tk_fmt_x_upper((uint64_t)val) : tk_fmt_x_lower((uint64_t)val);
        case 'b': return tk_fmt_b((uint64_t)val);
        case 'n': return tk_fmt_n_i(val);
        default:  return tk_i64_to_str(val);
    }
}
tk_str tk_fmt_dyn_u64(uint64_t val, tk_str spec) {
    if (spec.len == 0) return tk_u64_to_str(val);
    int prec = fmt_parse_prec(spec, 0);
    switch (spec.ptr[0] | 0x20) {
        case 'd': return tk_fmt_d((int64_t)val, prec ? prec : 1);
        case 'x': return (spec.ptr[0] == 'X') ? tk_fmt_x_upper(val) : tk_fmt_x_lower(val);
        case 'b': return tk_fmt_b(val);
        case 'n': return tk_fmt_n_i((int64_t)val);
        default:  return tk_u64_to_str(val);
    }
}

// --- Phase 3 str query/slice builtins (query helpers allocate nothing; slice helpers follow
// tk_str_concat's ownership — a fresh malloc'd buffer the result OWNS, tk_panic on OOM) ---

// tk_str_eq — same length AND same bytes. memcmp (NOT strcmp — strings may hold embedded NUL);
// a zero-length pair compares equal without touching ptr (memcmp of 0 bytes is well-defined).
bool tk_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

// tk_str_slice — the bytes [start, end) as a ZERO-COPY VIEW into the parent str (#148). SAFE
// because a Teko `str` is IMMUTABLE and its buffer is never individually freed (arena/root or
// malloc'd-and-retained; mem::free frees only []T slice buffers, and str() snapshots its input),
// so a view has exactly the parent's lifetime and is observably identical to the old fresh-owned
// copy — while eliminating the dominant allocation in the compiler (measured 108M tiny copies /
// 762 MB + malloc overhead on a self-build via name_last_segment alone). Bounds: an out-of-range
// slice (start > end, or end past the byte length) PANICS (M.1, fail-loud — matches the VM's
// index bounds check). An empty slice keeps a valid non-NULL ptr into the parent.
tk_str tk_str_slice(tk_str s, uint64_t start, uint64_t end) {
    if (start > end || end > s.len) tk_panic("string slice out of range");
    return (tk_str){ s.ptr + start, (size_t)(end - start) };
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

// --- ROUND 0: UTF-8 codepoint operations ---

// tk_char_at — return the tk_char at 0-based codepoint index i in s. Panics if out of range.
// The returned tk_char borrows INTO s.ptr (no copy).
tk_char tk_char_at(tk_str s, int64_t i) {
    if (i < 0) tk_panic("char_at: negative codepoint index");
    uint64_t idx = (uint64_t)i;
    size_t pos = 0;
    uint64_t ci = 0;
    while (pos < s.len) {
        size_t w = utf8_lead_len(s.ptr[pos]);
        if (w > s.len - pos) w = s.len - pos;  // clamp at string end
        if (ci == idx) return (tk_char){ (uint8_t *)(s.ptr + pos), (uint64_t)w };
        ci  += 1;
        pos += w;
    }
    tk_panic("char_at: codepoint index out of range");
}

// tk_str_slice_chars — substring from codepoint index `from` (inclusive) to `to` (exclusive),
// returned as a fresh owned str (copied). Panics if from > to or to > codepoint count.
tk_str tk_str_slice_chars(tk_str s, int64_t from, int64_t to) {
    if (from < 0 || to < 0) tk_panic("str_slice_chars: negative codepoint index");
    uint64_t ufrom = (uint64_t)from, uto = (uint64_t)to;
    if (ufrom > uto) tk_panic("str_slice_chars: from > to");
    // walk to byte offsets for `from` and `to`
    size_t byte_from = 0, byte_to = 0;
    uint64_t ci = 0;
    size_t pos = 0;
    while (pos <= s.len) {
        if (ci == ufrom) byte_from = pos;
        if (ci == uto)   { byte_to = pos; break; }
        if (pos == s.len) {
            // ran out of codepoints before reaching `to`
            tk_panic("str_slice_chars: codepoint index out of range");
        }
        size_t w = utf8_lead_len(s.ptr[pos]);
        if (w > s.len - pos) w = s.len - pos;
        ci  += 1;
        pos += w;
    }
    if (ufrom > uto) tk_panic("str_slice_chars: from > to (post-walk)"); // unreachable but defensive
    size_t n = byte_to - byte_from;
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (str_slice_chars)");
    if (n) memcpy(buf, s.ptr + byte_from, n);
    return (tk_str){ buf, n };
}

// tk_is_alpha — true if the codepoint is a Unicode letter. ASCII: uses isalpha(3).
// Multibyte (lead byte >= 0x80): returns true (simplified ROUND 0 rule — all non-ASCII are
// treated as letters; a future round may consult Unicode tables).
bool tk_is_alpha(tk_char c) {
    if (c.len == 0) return false;
    uint8_t b0 = c.ptr[0];
    if (b0 < 0x80) return (bool)isalpha((unsigned char)b0);
    return true;  // non-ASCII codepoint → treated as letter (ROUND 0 simplification)
}

// tk_is_digit — true iff the codepoint is an ASCII decimal digit '0'–'9'. Multibyte → false.
bool tk_is_digit(tk_char c) {
    if (c.len == 0) return false;
    uint8_t b0 = c.ptr[0];
    if (b0 < 0x80) return b0 >= (uint8_t)'0' && b0 <= (uint8_t)'9';
    return false;
}

// tk_is_space — true iff the codepoint is ASCII whitespace. Multibyte → false.
bool tk_is_space(tk_char c) {
    if (c.len == 0) return false;
    uint8_t b0 = c.ptr[0];
    if (b0 < 0x80) return b0 == ' ' || b0 == '\t' || b0 == '\n'
                       || b0 == '\r' || b0 == '\f' || b0 == '\v';
    return false;
}

// Static lowercase lookup table for ASCII (used by tk_to_lower / tk_to_upper).
// Avoids calling tolower/toupper which are locale-dependent.
static const uint8_t tk_ascii_lower[128] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,'0','1','2','3','4','5','6','7','8','9',
    58,59,60,61,62,63,64,
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    91,92,93,94,95,96,
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    123,124,125,126,127
};
static const uint8_t tk_ascii_upper[128] = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,
    32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,'0','1','2','3','4','5','6','7','8','9',
    58,59,60,61,62,63,64,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    91,92,93,94,95,96,
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    123,124,125,126,127
};

// Per-ASCII-character static byte stores for tk_to_lower / tk_to_upper.
// These are valid for the program lifetime so returned tk_char views are always safe.
static uint8_t tk_lower_byte[128];
static uint8_t tk_upper_byte[128];

// tk_to_lower — ASCII lowercase. Non-ASCII chars returned unchanged (borrowed view).
tk_char tk_to_lower(tk_char c) {
    if (c.len == 0) return c;
    uint8_t b0 = c.ptr[0];
    if (b0 < 0x80) {
        tk_lower_byte[b0] = tk_ascii_lower[b0];
        return (tk_char){ &tk_lower_byte[b0], 1 };
    }
    return c;  // non-ASCII: return unchanged
}

// tk_to_upper — ASCII uppercase. Non-ASCII chars returned unchanged (borrowed view).
tk_char tk_to_upper(tk_char c) {
    if (c.len == 0) return c;
    uint8_t b0 = c.ptr[0];
    if (b0 < 0x80) {
        tk_upper_byte[b0] = tk_ascii_upper[b0];
        return (tk_char){ &tk_upper_byte[b0], 1 };
    }
    return c;  // non-ASCII: return unchanged
}

// ── Arena allocation (S1) — bump allocator over a chunk-list. See teko_rt.h. ──
// Each chunk is one malloc'd block: a header + payload, bump-filled by `used`. The
// `max_align_t data[]` flexible member forces the payload base to max_align_t alignment,
// so rounding `used` up to that alignment aligns every sub-allocation exactly as malloc
// would. Chunks are libc-malloc'd, so tk_region_drop's free() on each is heap-correct (no
// arena interior pointer is ever passed to libc free). NOTE: the seed is single-threaded;
// the lazy root init (tk_g_root) is not synchronized — revisit at S8 (concurrency).
struct tk_chunk { struct tk_chunk *next; size_t cap; size_t used; max_align_t data[]; };
// (W9.3b) `reg_next` is an INTRUSIVE link into the GLOBAL live-region registry (tk_g_regs) — no extra
// allocation. tk_region_new prepends; tk_region_drop unlinks; tk_regions_free_all walks + frees all.
// (S2) `parent` is the arena TREE edge (NULL = no parent — the root, or a deliberately
// parentless region); DISTINCT from `reg_next` (the flat global live-region list, unrelated to
// tree shape). `entries`/`nentries`/`entries_cap` is the per-region type→instance registry: a
// small realloc-array (no hashing — arena depths/entry counts are small; same growth pattern as
// TK_RT_LIST). Lazy: NULL/0 until the first tk_region_register call.
typedef struct { uint64_t type_id; void *instance; } tk_region_entry;
struct tk_region {
    struct tk_chunk  *head;
    struct tk_region  *reg_next;
    struct tk_region  *parent;
    tk_region_entry   *entries;
    size_t             nentries;
    size_t             entries_cap;
    uint64_t           gen;        // (S2 Level-1) unique generation stamp — distinguishes a dropped-and-reused region address from a live one (push-cache safety)
};
_Static_assert(offsetof(struct tk_chunk, data) % _Alignof(max_align_t) == 0,
               "chunk payload base must be max_align_t-aligned");

// Allocate a chunk with `payload` usable bytes; NULL on OOM (the caller decides retry/panic).
static struct tk_chunk *tk_chunk_try(size_t payload) {
    struct tk_chunk *c = malloc(offsetof(struct tk_chunk, data) + payload);
    if (c != NULL) { c->next = NULL; c->cap = payload; c->used = 0; }
    return c;
}

// (W9.3b) the GLOBAL registry of live regions (single-threaded seed; S8 revisits concurrency). A
// region is on this list from tk_region_new until tk_region_drop or tk_regions_free_all removes it.
static tk_region *tk_g_regs = NULL;
static tk_region *tk_g_root = NULL;   // single-threaded seed (S8 revisit); lazy + idempotent (the root is also on tk_g_regs)
static uint64_t   tk_g_region_gen = 0;   // (S2 Level-1) monotonic region-generation counter; every tk_region_new stamps r->gen from it (never reused, so a recycled address always carries a fresh gen)

// =========================================================================
// (mem::free ruling 2026-07-03) FREE-LIST OVERLAY over the ROOT region — the runtime seat of
// `teko::mem::free_slice` (and the coming `free<T>`). A bump arena cannot return an individual
// block to the bump, so an explicitly freed block is PARKED on a size-class free list instead,
// and tk_region_alloc consults that list BEFORE bumping — real REUSE: an explicit free stops
// the footprint from growing even though the region is never dropped.
//   * bins: exact-size classes for blocks ≤ 4096 B (16-byte steps — max_align granularity, the
//     same rounding tk_region_alloc applies), single-probe pop = O(1);
//   * large list: > 4096 B, bounded first-fit (size must be ≥ request and ≤ 2× — no headers, so
//     a block is never split; the cap avoids quadratic scans).
// ROOT-ONLY by design: parked blocks live inside root chunks, which are never freed mid-run —
// except by the test-gate rewind (tk_arena_pop), which PURGES the whole list first (its parked
// blocks may sit inside the chunks the rewind frees). Single-threaded seed (S8 revisits).
typedef struct tk_freenode { struct tk_freenode *next; size_t bytes; } tk_freenode;
#define TK_FREE_BINS 4096                           // (i+1)*16 bytes, i.e. 16..65536 — (#148 Level-2) the doubling-ladder steps of struct lists (esz ~100-300 B × cap 32-256) must BIN exactly (the bounded large-list scan barely reuses them); 4096 ptr slots = 32 KB static
static tk_freenode *tk_free_bins[TK_FREE_BINS];
static tk_freenode *tk_free_large = NULL;
static unsigned long long tk_free_parked_bytes = 0, tk_free_reused_bytes = 0, tk_free_reused_count = 0;
static void tk_free_purge(void) {                   // rewind/termination: parked blocks may now dangle
    for (int i = 0; i < TK_FREE_BINS; i += 1) tk_free_bins[i] = NULL;
    tk_free_large = NULL;
    tk_free_parked_bytes = 0;
}
static void *tk_free_take(size_t an) {
    // (#148 Level-2) TAKE = CEIL-16 — never UNDERSTATE the need. The arena's alignment quantum is
    // _Alignof(max_align_t) (8 on arm64 Darwin), so `an` may not be a 16-multiple: flooring the bin
    // index handed a 16-byte block to a 24-byte request — an 8-byte OVERRUN into the neighbor.
    // With ceil, bin[qa] blocks are exactly qa ≥ an bytes. (qa ≥ 16 always, so the old bins[-1]
    // underflow guard (#150) is subsumed.)
    size_t qa = (an + 15) & ~(size_t)15;
    if (qa <= (size_t)TK_FREE_BINS * 16) {
        tk_freenode **bin = &tk_free_bins[qa / 16 - 1];
        if (*bin != NULL) {
            tk_freenode *n = *bin; *bin = n->next;
            tk_free_parked_bytes -= qa; tk_free_reused_bytes += qa; tk_free_reused_count += 1;
            return n;
        }
        return NULL;
    }
    tk_freenode **pp = &tk_free_large;
    for (int scan = 0; *pp != NULL && scan < 32; pp = &(*pp)->next, scan += 1) {
        tk_freenode *n = *pp;
        if (n->bytes >= an && n->bytes <= an * 2) {   // fit without a split (no headers to split with)
            *pp = n->next;
            tk_free_parked_bytes -= n->bytes; tk_free_reused_bytes += n->bytes; tk_free_reused_count += 1;
            return n;
        }
    }
    return NULL;
}

// =========================================================================
// (S2 groundwork) ARENA LIFETIME OBSERVABILITY — env-gated, zero overhead when off.
//
// RULING 2026-07-03: the no-GC arena never cleans because allocation LIFETIMES are not
// observable — before any scoped-cleanup work, make them observable and let the DATA guide
// how aggressive cleanup can safely be. `TEKO_ARENA_OBS=1` (or `=<path>`) enables:
//   * a per-CALL-SITE histogram of ROOT-region bytes (process lifetime — today never freed),
//     keyed by tk_alloc's return address and symbolized via dladdr — the "lifetime map";
//   * a per-site histogram of SCOPED-region bytes (freed at tk_region_drop);
//   * lifecycle counters: regions created/dropped, bytes reclaimed by drops and by the
//     test-gate arena rewind (tk_arena_pop) — i.e. how much the arena ACTUALLY frees.
// Dumped periodically (every 512 MB — survives a SIGKILL under memory pressure) and at
// process exit (tk_regions_free_all). Off (the default): one predicted int compare per alloc.
// =========================================================================
#if !defined(_WIN32)
#include <dlfcn.h>      /* dladdr — call-site symbolization for the obs tables (POSIX, incl. musl) */
#endif
#ifdef TK_HAVE_BACKTRACE
#include <execinfo.h>   /* (#148 RA2) backtrace — glibc/macOS only; musl has no execinfo */
#endif
#define TK_OBS_CAP 16384                       // open-addressed site table (power of two)
typedef struct { void *ra; unsigned long long bytes, count; } tk_obs_site;
static tk_obs_site tk_obs_root[TK_OBS_CAP];    // allocations landing in the ROOT region (never freed today)
static tk_obs_site tk_obs_scoped[TK_OBS_CAP];  // allocations landing in scoped regions (freed at drop)
// (#148 RA1) copy-grow bytes attributed to the GENERATED CALLING FN (not tk_slice_push itself, which
// the RA0 tables blame as one opaque line). tk_slice_push (the root wrapper) parks ITS caller's RA in
// tk_g_push_ra so the core can attribute through the wrapper hop. A separate VIEW of the same bytes —
// overlaps the root/scoped tables by design (different aggregation of the same allocations).
static tk_obs_site tk_obs_push[TK_OBS_CAP];
static unsigned long long tk_obs_push_bytes = 0;
static void *tk_g_push_ra = NULL;
// (#148 RA2) grows > 4 KB attributed one level HIGHER (the append helper's CALLER, via backtrace) —
// answers "who drives codegen::cb's expensive copy-grows" when RA1 blames the helper itself.
static tk_obs_site tk_obs_push2[TK_OBS_CAP];
static unsigned long long tk_obs_push2_bytes = 0;
// (#148 miss-reason) WHY did the live-tail witness fail, split small vs BIG (>1 MB) grows:
// [0]=slot empty  [1]=slot holds another ptr  [2]=ptr matches, len differs  [3]=cap full (legit doubling)  [4]=esz/region/gen mismatch
static unsigned long long tk_obs_miss[5], tk_obs_miss_big[5];
// (#148 dark matter) fresh MALLOC'd str/format buffers (tk_str_concat / slice / of_bytes /
// u64_to_str — outside the arena, invisible to the tables above), attributed to the CALLING fn.
static tk_obs_site tk_obs_mstr[TK_OBS_CAP];
static unsigned long long tk_obs_mstr_bytes = 0, tk_obs_mstr_count = 0;
static void tk_obs_add(tk_obs_site *tab, void *ra, size_t n);   // fwd — defined just below
static void tk_obs_mstr_note(size_t n, void *ra) {
    tk_obs_mstr_bytes += n; tk_obs_mstr_count += 1;
    tk_obs_add(tk_obs_mstr, ra, n);
}
static unsigned long long tk_obs_root_bytes = 0, tk_obs_scoped_bytes = 0;
static unsigned long long tk_obs_drop_bytes = 0;     // bytes reclaimed by tk_region_drop (chunk `used` sums)
static unsigned long long tk_obs_rewind_bytes = 0;   // bytes reclaimed by tk_arena_pop rewinds
static unsigned long long tk_obs_regions_new = 0, tk_obs_regions_dropped = 0;
static unsigned long long tk_obs_next_dump = 512ull * 1024 * 1024;
static int tk_obs_on = -1;                     // -1 = not probed; 0 = off; 1 = on
static const char *tk_obs_path = "/tmp/teko_arena_obs.txt";
static int tk_obs_enabled(void) {
    if (tk_obs_on < 0) {
        const char *e = getenv("TEKO_ARENA_OBS");
        tk_obs_on = (e != NULL && *e != '\0') ? 1 : 0;
        if (tk_obs_on && strcmp(e, "1") != 0) tk_obs_path = e;
    }
    return tk_obs_on;
}
static void tk_obs_add(tk_obs_site *tab, void *ra, size_t n) {
    size_t h = ((uintptr_t)ra >> 4) & (TK_OBS_CAP - 1);
    for (;;) {
        if (tab[h].ra == ra || tab[h].ra == NULL) { tab[h].ra = ra; tab[h].bytes += n; tab[h].count += 1; return; }
        h = (h + 1) & (TK_OBS_CAP - 1);
    }
}
static void tk_obs_dump_table(FILE *fp, const char *label, tk_obs_site *tab, unsigned long long total) {
    fprintf(fp, "=== %s: %.2f MB total ===\n", label, total / 1048576.0);
    int printed[40]; int np = 0;                             // non-destructive top-30 (periodic-safe)
    for (int k = 0; k < 30; k += 1) {
        int best = -1; unsigned long long bb = 0;
        for (int i = 0; i < TK_OBS_CAP; i += 1) {
            if (tab[i].ra == NULL || tab[i].bytes <= bb) continue;
            int seen = 0; for (int j = 0; j < np; j += 1) if (printed[j] == i) { seen = 1; break; }
            if (!seen) { bb = tab[i].bytes; best = i; }
        }
        if (best < 0) break;
        printed[np++] = best;
        const char *nm = "?";
#if !defined(_WIN32)
        Dl_info di;
        if (dladdr(tab[best].ra, &di) && di.dli_sname) nm = di.dli_sname;
#endif
        fprintf(fp, "  %2d  %9.1f MB  %11llu allocs  %s\n", k, tab[best].bytes / 1048576.0, tab[best].count, nm);
    }
}
static void tk_obs_dump(void) {
    if (tk_obs_on != 1) return;
    FILE *fp = fopen(tk_obs_path, "w");
    if (fp == NULL) fp = stderr;
    unsigned long long live = tk_obs_root_bytes + tk_obs_scoped_bytes;
    unsigned long long reclaimed = tk_obs_drop_bytes + tk_obs_rewind_bytes;
    fprintf(fp, "=== ARENA LIFETIME MAP (TEKO_ARENA_OBS) ===\n");
    fprintf(fp, "root (process-lifetime, never freed): %10.1f MB\n", tk_obs_root_bytes / 1048576.0);
    fprintf(fp, "scoped (freed at region drop):        %10.1f MB\n", tk_obs_scoped_bytes / 1048576.0);
    fprintf(fp, "reclaimed by region drops:            %10.1f MB   (%llu of %llu regions dropped)\n",
            tk_obs_drop_bytes / 1048576.0, tk_obs_regions_dropped, tk_obs_regions_new);
    fprintf(fp, "reclaimed by test-gate rewinds:       %10.1f MB\n", tk_obs_rewind_bytes / 1048576.0);
    fprintf(fp, "mem::free — parked now / reused:      %10.1f MB / %.1f MB (%llu reuses)\n",
            tk_free_parked_bytes / 1048576.0, tk_free_reused_bytes / 1048576.0, tk_free_reused_count);
    fprintf(fp, "reclaim ratio: %.1f%%  (reclaimed / allocated)\n\n",
            live ? 100.0 * (double)reclaimed / (double)live : 0.0);
    tk_obs_dump_table(fp, "ROOT-lifetime bytes by call site", tk_obs_root, tk_obs_root_bytes);
    tk_obs_dump_table(fp, "SCOPED-lifetime bytes by call site", tk_obs_scoped, tk_obs_scoped_bytes);
    tk_obs_dump_table(fp, "PUSH copy-grow bytes by CALLING fn (RA1, #148)", tk_obs_push, tk_obs_push_bytes);
    tk_obs_dump_table(fp, "PUSH >4KB grows by the helper's CALLER (RA2, #148)", tk_obs_push2, tk_obs_push2_bytes);
    fprintf(fp, "=== PUSH miss reasons (all | >1MB grows): empty %llu|%llu  other-ptr %llu|%llu  len %llu|%llu  cap-full %llu|%llu  esz/gen %llu|%llu ===\n",
            tk_obs_miss[0], tk_obs_miss_big[0], tk_obs_miss[1], tk_obs_miss_big[1],
            tk_obs_miss[2], tk_obs_miss_big[2], tk_obs_miss[3], tk_obs_miss_big[3],
            tk_obs_miss[4], tk_obs_miss_big[4]);
    fprintf(fp, "=== FREE-LIST: parked %.1f MB now, reused %.1f MB across %llu takes ===\n",
            (double)tk_free_parked_bytes / 1048576.0, (double)tk_free_reused_bytes / 1048576.0, tk_free_reused_count);
    tk_obs_dump_table(fp, "MALLOC'd str/format buffers by CALLING fn (#148 dark matter)", tk_obs_mstr, tk_obs_mstr_bytes);
    fprintf(fp, "=== MALLOC str total: %.1f MB across %llu buffers ===\n", (double)tk_obs_mstr_bytes / 1048576.0, tk_obs_mstr_count);
    // (#148 dark matter) CHUNK accounting — how much malloc'd arena capacity is NOT covered by the
    // attributed `used` bytes (bump-tail waste + alignment padding), per live region, summed.
    {
        unsigned long long cap = 0, used = 0, nchunks = 0, nregs = 0;
        for (tk_region *r = tk_g_regs; r != NULL; r = r->reg_next) {
            nregs += 1;
            for (struct tk_chunk *c = r->head; c != NULL; c = c->next) { nchunks += 1; cap += c->cap; used += c->used; }
        }
        fprintf(fp, "=== CHUNKS: %llu regions, %llu chunks, malloc'd cap %.1f MB, used %.1f MB, tail-waste %.1f MB ===\n",
                nregs, nchunks, (double)cap / 1048576.0, (double)used / 1048576.0, (double)(cap - used) / 1048576.0);
    }
    if (fp != stderr) fclose(fp);
}

tk_region *tk_region_new(tk_region *parent) {
    tk_region *r = malloc(sizeof *r);          // the region header is itself a libc block
    if (r == NULL) tk_panic("out of memory");  // (so tk_region_drop can free() it)
    if (tk_obs_enabled()) tk_obs_regions_new += 1;   // (S2 obs) lifecycle counter
    r->head = NULL;                            // lazy: the first alloc creates the head chunk
    r->reg_next = tk_g_regs;                   // (W9.3b) prepend onto the live-region registry
    tk_g_regs = r;
    r->parent = parent;                        // (S2) the arena tree edge
    r->entries = NULL; r->nentries = 0; r->entries_cap = 0;   // (S2) the per-region registry, lazy
    r->gen = ++tk_g_region_gen;                 // (S2 Level-1) unique lifetime stamp (0 is never assigned, so a zeroed cache entry never matches a live region)
    return r;
}

// (S2) bind type_id → instance in r's OWN table. A second registration of the same type_id
// OVERWRITES (storage primitive only — true duplicate-registration errors belong to a higher
// DI layer, not the arena).
void tk_region_register(tk_region *r, uint64_t type_id, void *instance) {
    if (r == NULL) return;
    for (size_t i = 0; i < r->nentries; i += 1) {
        if (r->entries[i].type_id == type_id) { r->entries[i].instance = instance; return; }
    }
    if (r->nentries == r->entries_cap) {
        size_t ncap = r->entries_cap == 0 ? 4 : r->entries_cap * 2;
        tk_region_entry *ne = realloc(r->entries, ncap * sizeof *ne);
        if (ne == NULL) tk_panic("out of memory");
        r->entries = ne; r->entries_cap = ncap;
    }
    r->entries[r->nentries++] = (tk_region_entry){ .type_id = type_id, .instance = instance };
}

// (S2) walk r, then r->parent, then r->parent->parent, … until type_id is found (else NULL).
void *tk_region_lookup(tk_region *r, uint64_t type_id) {
    for (; r != NULL; r = r->parent) {
        for (size_t i = 0; i < r->nentries; i += 1) {
            if (r->entries[i].type_id == type_id) return r->entries[i].instance;
        }
    }
    return NULL;
}

void *tk_region_alloc(tk_region *r, size_t n) {
    if (n == 0) n = 1;                              // n→1: a zero-size alloc yields a distinct pointer
    // (S2 obs) SCOPED-lifetime side of the map — a direct allocation into a non-root region (class
    // objects, frame regions). The ROOT side is recorded in tk_alloc (whose RA0 is the REAL site;
    // recording here would blame everything on tk_alloc itself).
    if (tk_obs_on == 1 && r != tk_g_root) { tk_obs_scoped_bytes += n; tk_obs_add(tk_obs_scoped, __builtin_return_address(0), n); }
    size_t align = _Alignof(max_align_t);
    size_t an = (n + (align - 1)) & ~(align - 1);   // round the request up to alignment
    // (mem::free) REUSE an explicitly freed block first — root-only (parked blocks live in root
    // chunks). A hit costs one bin probe; an empty free list costs one NULL compare.
    if (r == tk_g_root) {
        void *reused = tk_free_take(an);
        if (reused != NULL) return reused;
    }
    if (r->head != NULL) {                          // fits in the current chunk?
        size_t base = (r->head->used + (align - 1)) & ~(align - 1);
        if (base <= r->head->cap && an <= r->head->cap - base) {
            r->head->used = base + an;
            return (char *)r->head->data + base;
        }
    }
    // New chunk. Ordinary requests get the default chunk (so subsequent small allocs share
    // it); a request larger than the default gets a chunk just big enough. If the (possibly
    // large) chunk malloc fails, retry at the exact request size before panicking, so any
    // allocation the old malloc(n) could satisfy still succeeds (keeps OOM near the old edge).
    size_t want = an > TK_REGION_DEFAULT_CHUNK ? an : TK_REGION_DEFAULT_CHUNK;
    struct tk_chunk *c = tk_chunk_try(want);
    if (c == NULL && want != an) c = tk_chunk_try(an);
    if (c == NULL) tk_panic("out of memory");       // M.1 — identical message to the old tk_alloc
    c->used = an;
    c->next = r->head;
    r->head = c;
    return (char *)c->data;                          // base 0 is max_align_t-aligned (flexible member)
}

void tk_region_drop(tk_region *r) {
    if (r == NULL) return;                           // NULL-tolerant
    // (W9.3b) unlink from the live-region registry FIRST, so tk_regions_free_all can never see (and
    // double-free) a region that a normal scope exit already dropped. Single-linked-list removal.
    if (tk_g_regs == r) {
        tk_g_regs = r->reg_next;
    } else {
        for (tk_region *p = tk_g_regs; p != NULL; p = p->reg_next) {
            if (p->reg_next == r) { p->reg_next = r->reg_next; break; }
        }
    }
    r->reg_next = NULL;
    struct tk_chunk *c = r->head;
    r->head = NULL;                                  // MEM Step-1 idempotency: clear before free so a re-entrant/second walk frees nothing
    if (tk_obs_on == 1) {                            // (S2 obs) how much a region drop ACTUALLY reclaims
        tk_obs_regions_dropped += 1;
        for (struct tk_chunk *oc = c; oc != NULL; oc = oc->next) tk_obs_drop_bytes += oc->used;
    }
    while (c != NULL) { struct tk_chunk *next = c->next; free(c); c = next; }
    free(r->entries); r->entries = NULL; r->nentries = 0; r->entries_cap = 0;   // (S2) the per-region registry — a separate malloc'd array, not chunk-backed
    free(r);
}

// (W9.3b) free EVERY still-live region (root + every live scoped frame/block region) and empty the
// registry. Idempotent + re-entrancy-safe: it detaches the whole list into a local FIRST, then frees
// each off the local — so tk_region_drop's registry-unlink (which it calls indirectly? no — we free
// chunks directly here) and any re-entrant call both see an empty registry. We free chunks + headers
// directly (NOT via tk_region_drop) to avoid the O(n) per-region registry search on a list we already
// own end-to-end. A second call is a no-op (the registry is empty). Hooked at the termination choke
// points: tk_panic* (abort skips atexit), tk_exit, and the lazy atexit below (normal return / exit()).
void tk_regions_free_all(void) {
    // (S2 obs) FINAL lifetime-map dump — this is every termination edge's choke point (atexit /
    // tk_exit / tk_panic), so an enabled run always ends with a complete map on disk.
    { static int obs_dumped = 0; if (!obs_dumped && tk_obs_on == 1) { obs_dumped = 1; tk_obs_dump(); } }
    tk_free_purge();                                 // (mem::free) parked blocks live inside chunks freed below
    tk_region *r = tk_g_regs;
    tk_g_regs = NULL;                                // empty the registry BEFORE freeing (re-entrancy)
    tk_g_root = NULL;                                // the root is on the list; it is freed below too
    while (r != NULL) {
        tk_region *rnext = r->reg_next;
        struct tk_chunk *c = r->head;
        while (c != NULL) { struct tk_chunk *cnext = c->next; free(c); c = cnext; }
        free(r->entries);   // (S2) the per-region registry — a separate malloc'd array, not chunk-backed
        free(r);
        r = rnext;
    }
}

tk_region *tk_region_root(void) {
    if (tk_g_root == NULL) {
        tk_g_root = tk_region_new(NULL);   // (S2) the tree root — no parent
        // (W9.3b) register the leak-clean termination hook ONCE (the root is created exactly once per
        // process, lazily). atexit fires on normal main return AND on libc exit() (tk_exit's path), so
        // a NORMALLY-terminating program is leak-clean too; tk_regions_free_all is idempotent, so the
        // explicit tk_exit/tk_panic calls plus this hook never double-free.
        atexit(tk_regions_free_all);
    }
    return tk_g_root;
}

void *tk_alloc(size_t n) {
    // (S1) Route through the process root region: bump-allocated, never dropped = today's
    // malloc-everywhere leak (M.5). OOM still panics (M.1, never NULL). Same contract as the
    // S0 malloc(n?n:1), only the bytes now come from a region chunk instead of libc directly.
    if (tk_obs_enabled()) {                       // (S2 obs) ROOT-lifetime side of the map, keyed by the REAL caller
        tk_obs_root_bytes += (n ? n : 1);
        tk_obs_add(tk_obs_root, __builtin_return_address(0), n ? n : 1);
        if (tk_obs_root_bytes > tk_obs_next_dump) { tk_obs_next_dump += 512ull * 1024 * 1024; tk_obs_dump(); }   // periodic (survives SIGKILL)
    }
    return tk_region_alloc(tk_region_root(), n);
}

// (#109 test-gate memory) A per-scope CHECKPOINT/REWIND of the process root region's bump position.
// The root region is a LIFO chunk-list (tk_region_alloc PREPENDS new chunks), so a checkpoint =
// (head chunk, its used offset). Rewind frees every chunk PREPENDED after the checkpoint and resets
// the checkpoint chunk's bump offset — bulk-freeing everything the root region allocated in between.
//
// Used ONLY by the test-gate runner (vm's run_tests_cov) to bound memory: each #test's transient
// allocations (env cells, list copies, string concats — the self-host VM's copy-everything values,
// [[selfhost-vm-perf]]) are freed after the test, so 659 tests no longer accumulate 9+ GB. SOUND
// because run_tests_cov is compiled C (its loop state lives on the C stack, NOT in the arena) and the
// coverage sinks are libc-heap (realloc/malloc above) — so nothing referenced after a test lives in
// the rewound span. Balanced push/pop (depth ~1); a stack over the fixed cap is counted but not saved
// (pop then no-ops), keeping push/pop balanced without ever rewinding past a recorded mark.
typedef struct { struct tk_chunk *chunk; size_t used; } tk_arena_mark;
static tk_arena_mark tk_arena_marks[64];
static int tk_arena_msp = 0;
void tk_arena_push(void) {
    if (tk_arena_msp >= 0 && tk_arena_msp < 64) {
        tk_region *r = tk_region_root();
        tk_arena_marks[tk_arena_msp].chunk = r->head;
        tk_arena_marks[tk_arena_msp].used  = r->head ? r->head->used : 0;
    }
    tk_arena_msp += 1;
}
static void tk_push_cache_purge(void);   // fwd — the cache lives beside tk_slice_push below

void tk_arena_pop(void) {
    if (tk_arena_msp <= 0) return;
    tk_arena_msp -= 1;
    if (tk_arena_msp >= 64) return;            // an over-deep push saved nothing — do not rewind
    tk_free_purge();   // (mem::free) parked blocks may live inside the chunks this rewind frees
    tk_push_cache_purge();   // (#148 safety) rewound ROOT addresses get recycled by later allocs with the SAME region+gen — a stale live-tail entry could false-hit and in-place-write into foreign memory; purge closes it
    tk_region *r = tk_region_root();
    tk_arena_mark m = tk_arena_marks[tk_arena_msp];
    struct tk_chunk *c = r->head;
    // NULL-bounded: the mark's chunk is always in the chain (chunks only ever prepend), so the
    // walk ends at m.chunk; the c != NULL bound makes that invariant explicit (SAST NullDeref).
    while (c != NULL && c != m.chunk) {
        struct tk_chunk *next = c->next;
        if (tk_obs_on == 1) tk_obs_rewind_bytes += c->used;   // (S2 obs) bytes the rewind reclaims
        free(c); c = next;                                    // free chunks newer than the mark
    }
    r->head = m.chunk;
    if (m.chunk != NULL) {
        if (tk_obs_on == 1 && m.chunk->used > m.used) tk_obs_rewind_bytes += m.chunk->used - m.used;   // partial-chunk rewind
        m.chunk->used = m.used;
    }
}

void tk_print(tk_str s) {
    // Exactly s.len bytes; tolerate embedded NUL; no strlen/puts.
    fwrite(s.ptr, 1, s.len, stdout);
}

void tk_println(tk_str s) {
    tk_print(s);
    fputc('\n', stdout);   // single 0x0A
}

// Host output FFI bottoms (scope.c: write/ewrite/eprint/eprintln) — exactly s.len bytes, tolerate
// embedded NUL. write → stdout; ewrite/eprint → stderr; eprintln → stderr + '\n'.
void tk_write(tk_str s)    { fwrite(s.ptr, 1, s.len, stdout); }
void tk_ewrite(tk_str s)   { fwrite(s.ptr, 1, s.len, stderr); }
void tk_eprint(tk_str s)   { fwrite(s.ptr, 1, s.len, stderr); }
void tk_eprintln(tk_str s) { fwrite(s.ptr, 1, s.len, stderr); fputc('\n', stderr); }

// teko::float::parse(str) -> f64 — strtod over a NUL-terminated copy (s may contain no NUL and is
// not NUL-terminated). A non-numeric / empty string yields 0.0 (strtod's no-conversion result).
double tk_float_parse(tk_str s) {
    char *buf = (char *)tk_alloc(s.len + 1);
    if (s.len) memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    double v = strtod(buf, NULL);
    return v;
}

_Noreturn void tk_panic(const char *msg) {
    // Loud + non-zero (M.1): SIGABRT via abort() (conventional 134).
    fputs("teko: panic: ", stderr);
    fputs(msg, stderr);
    fputc('\n', stderr);
    tk_backtrace();   // (C1.9) show the call stack
    tk_regions_free_all();   // (W9.3b) abort() skips atexit — free the arena regions explicitly first
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
    tk_regions_free_all();   // (W9.3b) abort() skips atexit — free the arena regions explicitly first
    abort();
}
// the Teko-level `exit(<int>)` — end the program with a status code (no panic message).
// (W9.3b) free every live arena region before exiting so a diverging exit() is leak-clean (the atexit
// hook would also fire, but the explicit call keeps the contract local + obvious; free_all is idempotent).
_Noreturn void tk_exit(int32_t code) { tk_regions_free_all(); exit(code); }

_Noreturn void tk_panic_div0(void)     { tk_panic("division by zero"); }
_Noreturn void tk_panic_oob(void)      { tk_panic("index out of bounds"); }
// (C1.7-CAST) Global cast-location set by codegen just before every tk_to_* call.
// line==0 means position unknown (position setter was skipped).
uint32_t _tk_cast_loc_line = 0;
uint32_t _tk_cast_loc_col  = 0;
_Noreturn void tk_panic_cast(void) {
    if (_tk_cast_loc_line) {
        char buf[32];
        snprintf(buf, sizeof buf, "%u:%u: ", (unsigned)_tk_cast_loc_line, (unsigned)_tk_cast_loc_col);
        fputs(buf, stderr);
    }
    tk_panic("impossible conversion");
}
_Noreturn void tk_panic_overflow(void) { tk_panic("integer overflow"); }

// (C1.7) positioned OOB — print "line:col: " (same shape as the VM's vm_panic_pos), then the
// canonical "teko: panic: index out of bounds\n", so VM and native locate identically.
_Noreturn void tk_panic_oob_at(uint32_t line, uint32_t col) {
    char buf[32];
    snprintf(buf, sizeof buf, "%u:%u: ", (unsigned)line, (unsigned)col);
    fputs(buf, stderr);
    tk_panic_oob();
}

// =========================================================================
// Host-FFI + arithmetic bottoms (the lifting seam — see teko_rt.h). The
// codegen-side emit_host_ffi turns each fixed-ABI result struct into the
// program's `T | error` / `error?` / `[]str` value. `error` is its message str.
// =========================================================================

// NUL-terminate a tk_str into a fresh owned C string (callers pass it to libc).
static char *tk_cstr(tk_str s) {
    char *c = (char *)tk_alloc(s.len + 1);
    if (s.len) memcpy(c, s.ptr, s.len);
    c[s.len] = '\0';
    return c;
}
// A fresh owned tk_str holding the bytes of a C string (the message / value carrier).
static tk_str tk_str_of_cstr(const char *c) {
    size_t n = strlen(c);
    tk_byte *buf = (tk_byte *)tk_alloc(n ? n : 1);
    if (n) memcpy(buf, c, n);
    return (tk_str){ buf, n };
}

tk_ffi_sres tk_rt_read_file(tk_str path) {
    char *p = tk_cstr(path);
    FILE *f = fopen(p, "rb");
    if (f == NULL) return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("cannot open file") };
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("cannot seek file") }; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("cannot size file") }; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("cannot rewind file") }; }
    size_t n = (size_t)sz;
    tk_byte *buf = (tk_byte *)tk_alloc(n ? n : 1);
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("short read on file") };
    return (tk_ffi_sres){ .ok = true, .value = (tk_str){ buf, n } };
}

// (DT3) tk_rt_stdin_eof_flag — set by the LAST tk_rt_read_line call: true iff it read zero
// bytes before hitting EOF (stdin fully exhausted). A plain `bool`/`str` return (not the
// {ok,value,err} FFI-lift shape) so a brand-new host primitive stays lowerable by ANY codegen
// generation — new/unrecognized `str | error`-shaped externs need a per-name lift the SEED's
// frozen codegen.c cannot learn post-release (the bootstrap-seed constraint); a direct `bool`/
// `str` return needs no lift at all (mirrors tk_rt_os/tk_rt_version's already-working shape).
static bool tk_rt_stdin_eof_flag = false;

// (DT3) tk_rt_stdin_eof() — teko::io::stdin_eof(): did the LAST read_line() hit real EOF (no
// more input at all)? Read this AFTER an empty read_line() result to tell "EOF" from "a blank
// line" (both yield an empty str).
bool tk_rt_stdin_eof(void) { return tk_rt_stdin_eof_flag; }

// (DT3) tk_rt_read_line — one line from stdin, byte-at-a-time (portable — no POSIX-only
// getline needed). Stops at '\n' (consumed, not kept) or EOF; a trailing '\r' (a Windows
// "\r\n" source piped in) is also stripped. Sets tk_rt_stdin_eof_flag when zero bytes were
// read before EOF (the "no more input" case — an empty str otherwise means a genuine blank
// line); EOF after at least one byte still yields that final, unterminated line (matches a
// shell's own paste-without-trailing-newline behavior) and leaves the EOF flag false.
tk_str tk_rt_read_line(void) {
    tk_byte_list acc = tk_byte_list_empty();
    bool saw_any = false;
    for (;;) {
        int ch = fgetc(stdin);
        if (ch == EOF) break;
        saw_any = true;
        if (ch == '\n') break;
        acc = tk_byte_list_push(acc, (tk_byte)ch);
    }
    tk_rt_stdin_eof_flag = !saw_any;
    if (acc.len > 0 && acc.ptr[acc.len - 1] == '\r') acc.len = acc.len - 1;
    tk_byte *buf = (tk_byte *)tk_alloc(acc.len ? acc.len : 1);
    if (acc.len) memcpy(buf, acc.ptr, acc.len);
    tk_byte_list_free(acc);
    return (tk_str){ buf, acc.len };
}

tk_ffi_sres tk_rt_getenv(tk_str name) {
    char *n = tk_cstr(name);
    const char *v = getenv(n);
    if (v == NULL) return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("environment variable not set") };
    return (tk_ffi_sres){ .ok = true, .value = tk_str_of_cstr(v) };
}

tk_ffi_ures tk_rt_write_file(tk_str path, tk_str content) {
    char *p = tk_cstr(path);
    FILE *f = fopen(p, "wb");
    if (f == NULL) return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot open file for writing") };
    size_t put = content.len ? fwrite(content.ptr, 1, content.len, f) : 0;
    int rc = fclose(f);
    if (put != content.len || rc != 0) return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("short write on file") };
    return (tk_ffi_ures){ .ok = true };
}

// C7.12 — write raw bytes to a file (the .tkl package output path; binary, not UTF-8).
// Mirrors tk_rt_write_file but accepts a []byte ptr+len pair instead of a tk_str.
tk_ffi_ures tk_rt_write_file_bytes(tk_str path, const tk_byte *ptr, uint64_t len) {
    char *p = tk_cstr(path);
    FILE *f = fopen(p, "wb");
    if (f == NULL) return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot open file for writing") };
    size_t put = len ? fwrite(ptr, 1, (size_t)len, f) : 0;
    int rc = fclose(f);
    if (put != (size_t)len || rc != 0) return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("short write on file") };
    return (tk_ffi_ures){ .ok = true };
}

tk_ffi_ures tk_rt_chdir(tk_str path) {
    char *p = tk_cstr(path);
    if (chdir(p) != 0) return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot change directory") };
    return (tk_ffi_ures){ .ok = true };
}

tk_ffi_ures tk_rt_mkdir(tk_str path) {
    char *p = tk_cstr(path);
    // already-exists is success (idempotent — the build output dir may persist between builds).
    if (mkdir(p, 0755) != 0 && errno != EEXIST)
        return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot create directory") };
    return (tk_ffi_ures){ .ok = true };
}

// (issue #79) teko::fs::remove_file(path) — delete the file at `path` via libc remove().
// Already-absent is success (idempotent — mirrors mkdir's already-exists-is-success
// contract; "ensure the file does not exist"). First use: cleaning the cc-family probe
// file (<binary>.ccprobe.c) in the build backend's cc_family_is_clang twins (issue #73).
tk_ffi_ures tk_rt_remove_file(tk_str path) {
    char *p = tk_cstr(path);
    if (remove(p) != 0 && errno != ENOENT)
        return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot remove file") };
    return (tk_ffi_ures){ .ok = true };
}

tk_ffi_sres tk_rt_getcwd(void) {
    char buf[4096];
    if (getcwd(buf, sizeof buf) == NULL)
        return (tk_ffi_sres){ .ok = false, .err = tk_str_of_cstr("cannot read the working directory") };
    return (tk_ffi_sres){ .ok = true, .value = tk_str_of_cstr(buf) };
}

tk_ffi_ures tk_rt_setenv(tk_str name, tk_str value) {
    char *n = tk_cstr(name);
    char *v = tk_cstr(value);
    if (setenv(n, v, 1) != 0)
        return (tk_ffi_ures){ .ok = false, .err = tk_str_of_cstr("cannot set environment variable") };
    return (tk_ffi_ures){ .ok = true };
}

tk_ffi_slres tk_rt_list_dir(tk_str path) {
    char *p = tk_cstr(path);
    DIR *d = opendir(p);
    if (d == NULL) return (tk_ffi_slres){ .ok = false, .err = tk_str_of_cstr("cannot open directory") };
    // Grow-append the entry names (skip "." / "..") into an owned tk_str array.
    size_t cap = 8, n = 0;
    tk_str *out = (tk_str *)tk_alloc(cap * sizeof *out);
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' && (e->d_name[1] == '\0' || (e->d_name[1] == '.' && e->d_name[2] == '\0'))) continue;
        if (n == cap) {
            size_t ncap = cap * 2;
            tk_str *grown = (tk_str *)tk_alloc(ncap * sizeof *grown);
            memcpy(grown, out, n * sizeof *out);
            out = grown; cap = ncap;
        }
        out[n] = tk_str_of_cstr(e->d_name);
        n += 1;
    }
    closedir(d);
    return (tk_ffi_slres){ .ok = true, .ptr = out, .len = (uint64_t)n };
}

tk_ffi_u64res tk_rt_last_index_of(tk_str hay, tk_str needle) {
    // Byte index of the LAST occurrence of needle in hay (an empty needle → hay.len).
    if (needle.len == 0) return (tk_ffi_u64res){ .ok = true, .value = (uint64_t)hay.len };
    if (needle.len > hay.len) return (tk_ffi_u64res){ .ok = false };
    for (size_t i = hay.len - needle.len + 1; i-- > 0; ) {
        if (memcmp(hay.ptr + i, needle.ptr, needle.len) == 0)
            return (tk_ffi_u64res){ .ok = true, .value = (uint64_t)i };
        if (i == 0) break;
    }
    return (tk_ffi_u64res){ .ok = false };
}

int32_t tk_rt_run(const tk_str *argv, uint64_t n) {
    if (n == 0) return 127;
    // Build a NUL-terminated argv (each arg NUL-terminated; the vector NULL-terminated).
    char **cargv = (char **)tk_alloc((n + 1) * sizeof *cargv);
    for (uint64_t i = 0; i < n; i += 1) cargv[i] = tk_cstr(argv[i]);
    cargv[n] = NULL;
#ifdef _WIN32
    // _spawnvp(_P_WAIT) is synchronous: blocks until the child exits, returns its exit code.
    int w = tk_win32_spawnvp(cargv[0], cargv);
    return (w == -1) ? 127 : (int32_t)(int8_t)w;
#else
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {                      // child: exec; on failure exit 127 (POSIX convention)
        execvp(cargv[0], cargv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 127;
    if (WIFEXITED(status)) return (int32_t)(int8_t)WEXITSTATUS(status);
    return 127;
#endif
}

// (issue #73) teko::process::run_quiet(args) — same contract as tk_rt_run, but the child's
// stdout/stderr are redirected to the null device. Used by the build backend's cc flag-family
// probe (compiling a throwaway empty translation unit to test whether the host cc accepts
// clang-only flags) so a deliberately-rejected flag doesn't leak an "unrecognized option" line
// into the user's build output. [teko::process]
int32_t tk_rt_run_quiet(const tk_str *argv, uint64_t n) {
    if (n == 0) return 127;
    char **cargv = (char **)tk_alloc((n + 1) * sizeof *cargv);
    for (uint64_t i = 0; i < n; i += 1) cargv[i] = tk_cstr(argv[i]);
    cargv[n] = NULL;
#ifdef _WIN32
    // No fork/dup2 on Windows; redirect the whole process's std handles around the
    // synchronous _spawnvp call, then restore them.
    fflush(NULL);
    int saved_out = _dup(_fileno(stdout));
    int saved_err = _dup(_fileno(stderr));
    FILE *null_out = freopen("NUL", "w", stdout);
    FILE *null_err = freopen("NUL", "w", stderr);
    (void)null_out; (void)null_err;
    int w = tk_win32_spawnvp(cargv[0], cargv);
    fflush(NULL);
    _dup2(saved_out, _fileno(stdout));
    _dup2(saved_err, _fileno(stderr));
    _close(saved_out);
    _close(saved_err);
    return (w == -1) ? 127 : (int32_t)(int8_t)w;
#else
    pid_t pid = fork();
    if (pid < 0) return 127;
    if (pid == 0) {                      // child: redirect std{out,err} to /dev/null, then exec
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
        execvp(cargv[0], cargv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return 127;
    if (WIFEXITED(status)) return (int32_t)(int8_t)WEXITSTATUS(status);
    return 127;
#endif
}

// Captured process argv (the generated `main` calls tk_set_args before the virtual-main body).
// tk_g_argc / tk_g_argv are declared near the top (the stack-trace's .tsym loader uses them).
void tk_set_args(int argc, char **argv) { tk_g_argc = argc; tk_g_argv = argv; }
tk_str *tk_rt_args(uint64_t *n) {
    uint64_t c = (uint64_t)(tk_g_argc < 0 ? 0 : tk_g_argc);
    tk_str *out = (tk_str *)tk_alloc((c ? c : 1) * sizeof *out);
    for (uint64_t i = 0; i < c; i += 1) {
        size_t len = strlen(tk_g_argv[i]);
        out[i] = (tk_str){ (const tk_byte *)tk_g_argv[i], len };   // argv lives for the process
    }
    *n = c;
    return out;
}

// (C7.1f) the HOST operating system — "macos" / "linux" / "windows" (else "unknown"). Drives the
// per-OS [extern.*] resolution and the `#os(...)` conditional-compilation guard. A compile-time
// constant (the bootstrap/self-host runs ON the host it builds for; cross-target overrides via
// the manifest `[extern] target`). [teko::os]
tk_str tk_rt_os(void) {
#if defined(__APPLE__)
    static const char *s = "macos";
#elif defined(_WIN32)
    static const char *s = "windows";
#elif defined(__linux__)
    static const char *s = "linux";
#else
    static const char *s = "unknown";
#endif
    return (tk_str){ (const tk_byte *)s, strlen(s) };
}

// (CLI --version) the build's VERSION STRING — the RAW project-manifest `version` +
// `-<suffix>` (e.g. "0.0.1.0-bootstrap"), the SINGLE SOURCE OF TRUTH being teko.tkp.
// TEKO_VERSION_STRING is injected at COMPILE TIME by both build paths: CMake defines it for
// the C-bootstrap `teko` (from teko.tkp), and the self-host backend (driver.c/project.tks
// run_cc) passes `-DTEKO_VERSION_STRING="<version>[-<suffix>]"` from the ALREADY-PARSED
// manifest when it compiles this file — so `--version` never reads a file at runtime (an
// installed binary has no manifest) and both engines embed byte-identically. The fallback
// below is an honest placeholder for a raw `cc`-built teko_rt.c with no define (never the
// shipped path). [backs teko::env::version]
#ifndef TEKO_VERSION_STRING
#define TEKO_VERSION_STRING "0.0.0.0-dev"
#endif
// (#148) tk_peak_rss — this process's PEAK resident set size in BYTES, so the compiler can
// report its own memory cost at the end of a build. Darwin's ru_maxrss is bytes; Linux's is
// KILOBYTES. 0 = unavailable (the caller suppresses the print).
uint64_t tk_peak_rss(void) {
#if defined(_WIN32)
    return 0;   /* PeakWorkingSetSize via psapi — deferred; 0 suppresses the print */
#else
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 0;
#if defined(__APPLE__)
    return (uint64_t)ru.ru_maxrss;
#else
    return (uint64_t)ru.ru_maxrss * 1024u;
#endif
#endif
}

// The version arrives as a BARE preprocessor token (-DTEKO_VERSION_STRING=0.0.1.0-bootstrap) and
// is STRINGIZED here — embedding the quotes in the -D flag broke on Windows (the CRT command-line
// re-parsing of the spawned cc ate them; caught by the first release run's self-host chain).
#define TK_VERSTR2(x) #x
#define TK_VERSTR1(x) TK_VERSTR2(x)
tk_str tk_rt_version(void) {
    static const char *s = TK_VERSTR1(TEKO_VERSION_STRING);
    return (tk_str){ (const tk_byte *)s, strlen(s) };
}

// D3 — test-coverage sink (host side-channel; see teko_rt.h). A growable array of distinct ids,
// deduped on insert (the id count is bounded by the project's function count, so linear dedup is
// fine). tk_cov_reset starts a fresh run; tk_cov_mark records a function-entry id; tk_cov_distinct
// reports how many distinct functions executed.
static uint64_t *tk_cov_ids = NULL;
static uint64_t  tk_cov_n   = 0;
static uint64_t  tk_cov_cap = 0;
void tk_cov_reset(void) { tk_cov_n = 0; }   // keep the buffer; just forget the marks
void tk_cov_mark(uint64_t id) {
    for (uint64_t i = 0; i < tk_cov_n; i += 1) if (tk_cov_ids[i] == id) return;   // dedup
    if (tk_cov_n == tk_cov_cap) {
        uint64_t ncap = tk_cov_cap ? tk_cov_cap * 2 : 64;
        // (#109 test-gate memory) realloc (libc heap), NOT the arena — this buffer must SURVIVE the
        // per-test arena rewind (tk_arena_pop) that bounds the self-host test gate's memory.
        uint64_t *grown = (uint64_t *)realloc(tk_cov_ids, ncap * sizeof *grown);
        if (!grown) abort();
        tk_cov_ids = grown;
        tk_cov_cap = ncap;
    }
    tk_cov_ids[tk_cov_n++] = id;
}
uint64_t tk_cov_distinct(void) { return tk_cov_n; }
bool tk_cov_is_marked(uint64_t id) {
    for (uint64_t i = 0; i < tk_cov_n; i += 1) if (tk_cov_ids[i] == id) return true;
    return false;
}

// D3-branch — branch-coverage sink (a SEPARATE set, so it never perturbs the function-coverage
// count above). Recorded only when tk_cov_branches_on(true) — a plain `teko test`/build pays one
// flag check per branch and nothing else. A branch id packs (current-fn items-index, line, col,
// outcome): the current fn is the TOP of a small enter/leave stack the VM pushes around each call,
// which makes (line,col) unique per FILE globally unique (two files may share a line:col). The
// report queries tk_cov_branch_hit(fn, line, col, outcome) walking the typed program.
static uint64_t *tk_covb_ids = NULL;
static uint64_t  tk_covb_n   = 0;
static uint64_t  tk_covb_cap = 0;
static int       tk_covb_on  = 0;
static uint64_t *tk_fn_stack = NULL;
static uint64_t  tk_fn_sp    = 0;
static uint64_t  tk_fn_cap   = 0;
static uint64_t tk_branch_id(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome) {
    // [54]=base · [38..54)=fn(16b) · [14..38)=line(24b) · [6..14)=col(8b) · [0..6)=outcome(6b)
    return ((uint64_t)1 << 54) + (fn << 38) + ((uint64_t)line << 14)
         + (((uint64_t)col & 0xFF) << 6) + (outcome & 0x3F);
}
void tk_cov_branches_on(bool on) { tk_covb_on = on ? 1 : 0; }
void tk_cov_branch_reset(void) { tk_covb_n = 0; tk_fn_sp = 0; }
void tk_cov_enter(uint64_t fn) {
    if (!tk_covb_on) return;
    if (tk_fn_sp == tk_fn_cap) {
        uint64_t ncap = tk_fn_cap ? tk_fn_cap * 2 : 256;
        // (#109 test-gate memory) realloc (libc heap) so this coverage fn-attribution stack survives
        // the per-test arena rewind — tk_cov_enter runs inside each test (cov on), so an arena backing
        // would be freed by tk_arena_pop and read at the next enter (the ASan-caught UAF).
        uint64_t *g = (uint64_t *)realloc(tk_fn_stack, ncap * sizeof *g); if (!g) abort();
        tk_fn_stack = g; tk_fn_cap = ncap;
    }
    tk_fn_stack[tk_fn_sp++] = fn;
}
void tk_cov_leave(void) { if (tk_covb_on && tk_fn_sp > 0) tk_fn_sp -= 1; }
static void tk_covb_add(uint64_t id) {
    for (uint64_t i = 0; i < tk_covb_n; i += 1) if (tk_covb_ids[i] == id) return;
    if (tk_covb_n == tk_covb_cap) {
        uint64_t ncap = tk_covb_cap ? tk_covb_cap * 2 : 256;
        // (#109 test-gate memory) realloc (libc heap) so it survives the per-test arena rewind.
        uint64_t *grown = (uint64_t *)realloc(tk_covb_ids, ncap * sizeof *grown); if (!grown) abort();
        tk_covb_ids = grown; tk_covb_cap = ncap;
    }
    tk_covb_ids[tk_covb_n++] = id;
}
void tk_cov_branch(uint32_t line, uint32_t col, uint64_t outcome) {
    if (!tk_covb_on) return;
    uint64_t fn = tk_fn_sp > 0 ? tk_fn_stack[tk_fn_sp - 1] : 0;
    tk_covb_add(tk_branch_id(fn, line, col, outcome));
}
bool tk_cov_branch_hit(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome) {
    uint64_t id = tk_branch_id(fn, line, col, outcome);
    for (uint64_t i = 0; i < tk_covb_n; i += 1) if (tk_covb_ids[i] == id) return true;
    return false;
}

// D3-line — LINE-coverage sink. Lines are marked on EVERY evaluated expression (far more often than
// fns/branches), so this is an open-addressing HASH SET (O(1) insert/lookup) instead of the linear
// dedup above. A line id packs (current-fn idx, line) via the same enter/leave fn stack; 0 = empty.
static uint64_t *tk_line_ids = NULL;
static uint64_t  tk_line_cap = 0;   // power of two
static uint64_t  tk_line_n   = 0;
static int       tk_lines_on = 0;
static uint64_t tk_line_id(uint64_t fn, uint32_t line) { return ((fn << 24) | (uint64_t)line) + 1; }   // ≥1 (0 = empty slot)
static void tk_line_rehash(uint64_t ncap) {
    // (#109 test-gate memory) malloc (libc heap), NOT the arena — this hash table must SURVIVE the
    // per-test arena rewind. A rehash reassigns every slot, so it is malloc-new + free-old (never
    // realloc). The old table is malloc-backed after the first grow (NULL on the first), so free() is safe.
    uint64_t *nt = (uint64_t *)malloc(ncap * sizeof *nt); if (!nt) abort();
    for (uint64_t i = 0; i < ncap; i += 1) nt[i] = 0;
    for (uint64_t i = 0; i < tk_line_cap; i += 1) {
        uint64_t id = tk_line_ids[i]; if (!id) continue;
        uint64_t h = (id * 1099511628211ull) & (ncap - 1);
        while (nt[h]) h = (h + 1) & (ncap - 1);
        nt[h] = id;
    }
    free(tk_line_ids);
    tk_line_ids = nt; tk_line_cap = ncap;
}
void tk_cov_lines_on(bool on) { tk_lines_on = on ? 1 : 0; }
void tk_cov_line_reset(void) { tk_line_n = 0; for (uint64_t i = 0; i < tk_line_cap; i += 1) tk_line_ids[i] = 0; }
void tk_cov_line(uint32_t line) {
    if (!tk_lines_on || line == 0) return;
    uint64_t fn = tk_fn_sp > 0 ? tk_fn_stack[tk_fn_sp - 1] : 0;
    uint64_t id = tk_line_id(fn, line);
    if (tk_line_cap == 0) tk_line_rehash(1024);
    else if (tk_line_n * 2 >= tk_line_cap) tk_line_rehash(tk_line_cap * 2);
    uint64_t h = (id * 1099511628211ull) & (tk_line_cap - 1);
    while (tk_line_ids[h]) { if (tk_line_ids[h] == id) return; h = (h + 1) & (tk_line_cap - 1); }
    tk_line_ids[h] = id; tk_line_n += 1;
}
bool tk_cov_line_hit(uint64_t fn, uint32_t line) {
    if (tk_line_cap == 0) return false;
    uint64_t id = tk_line_id(fn, line);
    uint64_t h = (id * 1099511628211ull) & (tk_line_cap - 1);
    while (tk_line_ids[h]) { if (tk_line_ids[h] == id) return true; h = (h + 1) & (tk_line_cap - 1); }
    return false;
}

// D3-cross-process (#265, reuse of the #168 .tkcov protocol) — the native test gate runs the tests in
// a CHILD process, so its three coverage sinks live in the child. The child dumps them to a `.tkcov`
// file at exit; the parent (the compiler) MERGES that file into ITS sinks, then runs the same static
// walk + floors it always ran. The coverage id is the prog.items index in BOTH processes (they share
// the same TProgram), so the packed branch/line ids are process-portable and just re-inserted.
// File layout (host byte order — parent and child are the same build): magic "TKCOV1\0\0", then three
// (count:u64, ids:u64[count]) sections in order fns / branches / lines.

// tk_line_insert_raw — insert a pre-packed line id (from a merge), bypassing the tk_lines_on gate and
// the fn-stack packing tk_cov_line uses. Same open-addressing set as tk_cov_line.
static void tk_line_insert_raw(uint64_t id) {
    if (id == 0) return;
    if (tk_line_cap == 0) tk_line_rehash(1024);
    else if (tk_line_n * 2 >= tk_line_cap) tk_line_rehash(tk_line_cap * 2);
    uint64_t h = (id * 1099511628211ull) & (tk_line_cap - 1);
    while (tk_line_ids[h]) { if (tk_line_ids[h] == id) return; h = (h + 1) & (tk_line_cap - 1); }
    tk_line_ids[h] = id; tk_line_n += 1;
}

static bool tk_cov_write_section(FILE *f, const uint64_t *ids, uint64_t n) {
    if (fwrite(&n, sizeof n, 1, f) != 1) return false;
    if (n && fwrite(ids, sizeof *ids, n, f) != n) return false;
    return true;
}

void tk_cov_dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    static const char magic[8] = { 'T','K','C','O','V','1','\0','\0' };
    if (fwrite(magic, 1, 8, f) != 8) { fclose(f); return; }
    (void)tk_cov_write_section(f, tk_cov_ids, tk_cov_n);
    (void)tk_cov_write_section(f, tk_covb_ids, tk_covb_n);
    // lines are a sparse hash table — compact the non-empty slots into a temporary contiguous array.
    uint64_t *lines = NULL;
    uint64_t ln = 0;
    if (tk_line_n) {
        lines = (uint64_t *)malloc(tk_line_n * sizeof *lines);
        if (lines) {
            for (uint64_t i = 0; i < tk_line_cap; i += 1) { if (tk_line_ids[i]) lines[ln++] = tk_line_ids[i]; }
        }
    }
    (void)tk_cov_write_section(f, lines, ln);
    free(lines);
    fclose(f);
}

static uint64_t *tk_cov_read_section(FILE *f, uint64_t *out_n) {
    uint64_t n = 0;
    *out_n = 0;
    if (fread(&n, sizeof n, 1, f) != 1) return NULL;
    if (n == 0) return NULL;
    uint64_t *ids = (uint64_t *)malloc(n * sizeof *ids);
    if (!ids) return NULL;
    if (fread(ids, sizeof *ids, n, f) != n) { free(ids); return NULL; }
    *out_n = n;
    return ids;
}

bool tk_cov_merge(tk_str path) {
    char *cpath = (char *)tk_cstr_dup(path);
    FILE *f = fopen(cpath, "rb");
    free(cpath);
    if (!f) return false;
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || memcmp(magic, "TKCOV1\0\0", 8) != 0) { fclose(f); return false; }
    uint64_t n = 0;
    uint64_t *fns = tk_cov_read_section(f, &n);
    for (uint64_t i = 0; i < n; i += 1) tk_cov_mark(fns[i]);
    free(fns);
    uint64_t *br = tk_cov_read_section(f, &n);
    for (uint64_t i = 0; i < n; i += 1) tk_covb_add(br[i]);
    free(br);
    uint64_t *ln = tk_cov_read_section(f, &n);
    for (uint64_t i = 0; i < n; i += 1) tk_line_insert_raw(ln[i]);
    free(ln);
    fclose(f);
    return true;
}

// --- amortized growable push (the teko::list::push lowering — see teko_rt.h) ---
// Each growing buffer's spare capacity is tracked in a POINTER-KEYED HASH of live tails (single-probe,
// O(1) lookup). A push to a recorded live tail (same ptr + length witness + element size, spare cap)
// grows IN PLACE; anything else copy-grows geometrically into a fresh buffer (value-correct — the old
// buffer is left intact). The previous design was a 16-slot LINEAR cache: the compiler builds MANY
// lists INTERLEAVED (deep checker/codegen recursion), so 16 slots thrashed to a ~0% hit rate and
// nearly every push fell to the O(n) copy-grow → O(n²) memory (measured: 98 GB / 98 M allocs on a
// source-only self-build). A 65536-bucket hash keyed by the buffer pointer keeps every live tail
// resident regardless of interleaving depth, so a list built by N pushes copy-grows only O(log N)
// times (the geometric doublings) instead of N — O(n²) → O(n). (#109 memory — the self-host's
// dominant consumer.)
//
// (S2 Level-1) STALE-SLOT SAFETY. In S1 the "arena addresses are never reused" invariant made every
// stale slot harmless. Frame regions BREAK it: tk_region_drop frees chunks, so a later region can
// hand back the SAME address a dropped buffer used — an in-place append into that recycled address
// would corrupt a live allocation. So each slot now records the OWNING region + its generation, and
// an in-place hit additionally requires (region, region->gen) to match: a recycled address always
// carries a fresh region and/or gen, so it can never be mistaken for a live tail. (The root free-list
// reuse path is orthogonal — tk_free_block still EVICTS the slot on an explicit mem::free, since that
// recycles an address WITHIN the same region/gen.)
#define TK_PUSH_HASH_SIZE (1u << 16)   // 65536 single-probe buckets
static struct { const void *ptr; uint64_t len, cap, esz; tk_region *region; uint64_t region_gen; } tk_push_cache[TK_PUSH_HASH_SIZE];
static inline unsigned tk_push_slot(const void *p) {
    return (unsigned)((((uintptr_t)p >> 4) * 11400714819323198485ull) >> 48) & (TK_PUSH_HASH_SIZE - 1);
}
// (#148 safety) drop EVERY live-tail witness — called by tk_arena_pop before a rewind recycles root
// addresses (region+gen can't distinguish a recycled address WITHIN the same region).
static void tk_push_cache_purge(void) { memset(tk_push_cache, 0, sizeof tk_push_cache); }

// (S2 Level-1) the region-aware core: the grown buffer is allocated in `region`. `tk_slice_push`
// (below) is the unchanged default lowering target (root); codegen emits THIS variant only for a
// slice binding the escape analysis proves frame-local, passing the function's `_tkfr` frame region,
// so the whole buffer history (geometric doublings + in-place tail) is bulk-freed on frame exit.
void *tk_slice_push_r(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len, tk_region *region) {
    unsigned h = ptr ? tk_push_slot(ptr) : 0;
    // in-place ONLY when this is the live tail (same ptr + length witness + element size) with spare cap
    // AND still owned by the same live region generation (see STALE-SLOT SAFETY above).
    if (ptr != NULL && tk_push_cache[h].ptr == ptr && tk_push_cache[h].len == len
        && tk_push_cache[h].esz == esz && len < tk_push_cache[h].cap
        && tk_push_cache[h].region == region && tk_push_cache[h].region_gen == region->gen) {
        memcpy((char *)ptr + len * esz, elem, esz);
        tk_push_cache[h].len = len + 1;
        *out_len = len + 1;
        if (tk_g_push_ra != NULL) tk_g_push_ra = NULL;   // (#148 RA1) consume the wrapper's parked RA (NULL when obs off — predictable, ~free)
        return (void *)ptr;
    }
    // copy-grow geometrically into a fresh buffer (the old one is left intact — value semantics).
    // Root goes through tk_alloc (keeps the obs RA0 attribution + free-list reuse); a frame region
    // bumps directly (no free-list — that is root-only by design).
    // (#148 R3b) RIGHT-SIZED first rung: cap starts at 1 and doubles (1→2→4→8…), not at a flat 8.
    // The obs map showed the arena's #1 cost was NOT ladder garbage but OVERCAPACITY in millions of
    // small LIVE final buffers (most blocks/arg-lists hold 1–3 elements; a flat first cap of 8
    // wasted ~87% of every one, ~hundreds of MB corpus-wide). The extra early doublings are tiny
    // memcpys, and every superseded rung at an fo site is parked and recycled by the free-list.
    uint64_t cap = (len == 0) ? 1 : (len * 2);
    // (#148 RA1) attribute this grow to the GENERATED calling fn: the wrapper parked its caller's RA
    // in tk_g_push_ra; a direct (routed) call attributes its own return address.
    if (tk_obs_enabled() == 1) {
        int hop = tk_g_push_ra != NULL;   // did we arrive through the tk_slice_push wrapper?
        void *ra1 = hop ? tk_g_push_ra : __builtin_return_address(0);
        tk_obs_push_bytes += cap * esz; tk_obs_add(tk_obs_push, ra1, cap * esz);
        {   // (#148 miss-reason) classify WHY the in-place witness failed for this grow
            int why = (ptr == NULL || tk_push_cache[h].ptr == NULL) ? 0
                    : (tk_push_cache[h].ptr != ptr)                  ? 1
                    : (tk_push_cache[h].len != len)                  ? 2
                    : (tk_push_cache[h].esz == esz && len >= tk_push_cache[h].cap) ? 3 : 4;
            tk_obs_miss[why] += 1;
            if (cap * esz > (1u << 20)) tk_obs_miss_big[why] += 1;
        }
#ifdef TK_HAVE_BACKTRACE
        if (cap * esz > 4096) {   // (#148 RA2) the expensive grows: attribute the append helper's CALLER
            void *fr[6]; int nf = backtrace(fr, 6);
            int idx = 2 + hop;    // fr[0]=this fn, fr[1]=wrapper|caller, fr[2+hop]=the helper's caller
            if (nf > idx) { tk_obs_push2_bytes += cap * esz; tk_obs_add(tk_obs_push2, fr[idx], cap * esz); }
        }
#endif
    }
    tk_g_push_ra = NULL;
    // (#148) the OLD buffer's own witness (a true doubling: same ptr, cap exhausted) is superseded by
    // this grow — clear it so a dead multi-MB entry never squats its slot blocking future tenants.
    if (ptr != NULL && tk_push_cache[h].ptr == ptr) tk_push_cache[h].ptr = NULL;
    void *buf = (region == tk_g_root) ? tk_alloc(cap * esz) : tk_region_alloc(region, cap * esz);
    if (len && ptr != NULL) memcpy(buf, ptr, len * esz);
    memcpy((char *)buf + len * esz, elem, esz);
    // (#148 — the 11.5 GB fix) SIZE-AWARE eviction. Blind overwrite let 150M tiny cache inserts clobber
    // the multi-MB output buffer's slot ~2300×; every clobber forced a FULL multi-MB copy-grow on its
    // next append (measured: ~7.5k spurious grows averaging ~1.5 MB = 11.5 GB of 13.5 GB total churn —
    // 85%). Policy: an incumbent with a LARGER footprint keeps the slot; the smaller newcomer is simply
    // NOT cached (its next push copy-grows a small buffer — cheap). Safety unchanged: the witness only
    // ever authorizes in-place when ptr+len+esz+region+gen ALL match; not caching is always safe.
    unsigned hb = tk_push_slot(buf);
    if (tk_push_cache[hb].ptr == NULL || tk_push_cache[hb].cap * tk_push_cache[hb].esz <= cap * esz) {
        tk_push_cache[hb].ptr = buf; tk_push_cache[hb].len = len + 1;
        tk_push_cache[hb].cap = cap; tk_push_cache[hb].esz = esz;
        tk_push_cache[hb].region = region; tk_push_cache[hb].region_gen = region->gen;
    }
    *out_len = len + 1;
    return buf;
}

// (#148 R2) tk_append_bytes_fo — BULK append of `n` bytes onto a []byte builder, with FREE-OLD
// on copy-grow BY DECREE: the ONLY caller is the emitters' `cb` helper (codegen.tks), whose buffer
// is threaded LINEARLY through every emit fn (`out = cb(out, …)` / take-buf-return-grown) — no
// alias of an intermediate buffer ever survives, so the old buffer is parked for reuse the moment
// a grow replaces it. TEKO_MEM_PARANOID is the decree's guard (poison + never reuse → a violation
// fails the gate loudly). Replaces cb's per-byte push loop: one memcpy per fragment (CPU win) and
// realloc-parity ladders (memory win).
void *tk_append_bytes_fo(const void *ptr, uint64_t len, const void *src, uint64_t n, uint64_t *out_len) {
    if (n == 0) { *out_len = len; return (void *)ptr; }
    if (ptr != NULL) {   // in-place: the live tail with enough spare capacity
        unsigned h = tk_push_slot(ptr);
        if (tk_push_cache[h].ptr == ptr && tk_push_cache[h].len == len && tk_push_cache[h].esz == 1
            && tk_push_cache[h].region == tk_g_root && tk_push_cache[h].region_gen == tk_g_root->gen
            && len + n <= tk_push_cache[h].cap) {
            memcpy((char *)ptr + len, src, n);
            tk_push_cache[h].len = len + n;
            *out_len = len + n;
            return (void *)ptr;
        }
    }
    // copy-grow: geometric, but never below what the fragment needs.
    uint64_t cap = (len < 4) ? 8 : (len * 2);
    if (cap < len + n) cap = len + n;
    uint64_t old_bytes = len;
    if (ptr != NULL) {
        unsigned h = tk_push_slot(ptr);
        if (tk_push_cache[h].ptr == ptr && tk_push_cache[h].esz == 1 && tk_push_cache[h].cap > len)
            old_bytes = tk_push_cache[h].cap;   // the live tail — its full capacity is reusable
        tk_push_cache[h].ptr = NULL;            // superseded (mirrors tk_slice_push_r's clear)
    }
    void *buf = tk_alloc(cap);
    if (len) memcpy(buf, ptr, len);
    memcpy((char *)buf + len, src, n);
    unsigned hb = tk_push_slot(buf);
    if (tk_push_cache[hb].ptr == NULL || tk_push_cache[hb].cap * tk_push_cache[hb].esz <= cap) {
        tk_push_cache[hb].ptr = buf; tk_push_cache[hb].len = len + n;
        tk_push_cache[hb].cap = cap; tk_push_cache[hb].esz = 1;
        tk_push_cache[hb].region = tk_g_root; tk_push_cache[hb].region_gen = tk_g_root->gen;
    }
    if (ptr != NULL) tk_free_block((void *)ptr, old_bytes);   // the DECREE: the old buffer is dead
    *out_len = len + n;
    return buf;
}

// the default root-region lowering (unchanged contract) — a thin wrapper over the region-aware core.
void *tk_slice_push(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len) {
    // (#148 RA1) park THIS caller's return address so the core attributes the grow to the generated
    // fn, not to this wrapper hop. Only under obs (zero writes when off).
    if (tk_obs_enabled() == 1) tk_g_push_ra = __builtin_return_address(0);
    return tk_slice_push_r(ptr, len, elem, esz, out_len, tk_region_root());
}

// (#148 S2 Level-2) tk_slice_push_fo — FREE-OLD-on-grow, for a self-append whose chain the checker
// PROVED linear (born from list::empty(), self-append-only writes, no capture before the fn's final
// statement — see escape.tks::assign_frees_old). On a copy-grow the OLD buffer is dead by that proof,
// so it is PARKED on the free-list for reuse (realloc parity with the hand-written C twin, which
// frees per grow). The true capacity comes from the push-cache when this buffer is the live tail
// (usual case); otherwise the conservative len*esz lower bound. An in-place hit parks nothing.
void *tk_slice_push_fo(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len) {
    if (tk_obs_enabled() == 1) tk_g_push_ra = __builtin_return_address(0);
    const void *old = ptr;
    uint64_t old_bytes = len * esz;
    if (ptr != NULL) {
        unsigned h = tk_push_slot(ptr);
        if (tk_push_cache[h].ptr == ptr && tk_push_cache[h].esz == esz && tk_push_cache[h].cap > len)
            old_bytes = tk_push_cache[h].cap * esz;   // the live tail — its full capacity is reusable
    }
    void *buf = tk_slice_push_r(ptr, len, elem, esz, out_len, tk_region_root());
    if (buf != old && old != NULL) {
        // (#148 Level-2 BISECT) TEKO_FO_MAX=N limits parking to the first N grows (binary-search
        // the guilty park); TEKO_FO_TRACE at the boundary dumps the parking site's backtrace.
        static long long fo_max = -2, fo_count = 0;
        if (fo_max == -2) { const char *e = getenv("TEKO_FO_MAX"); fo_max = (e && *e) ? atoll(e) : -1; }
        if (fo_max >= 0) {
            if (fo_count >= fo_max) return buf;              // parking budget exhausted — plain push
            fo_count += 1;
#ifdef TK_HAVE_BACKTRACE
            if (fo_count == fo_max && getenv("TEKO_FO_TRACE")) {
                void *fr[8]; int nf = backtrace(fr, 8);
                fprintf(stderr, "== FO park #%lld ==\n", fo_count);
                backtrace_symbols_fd(fr, nf, 2);
            }
#endif
        }
        tk_free_block((void *)old, old_bytes);
    }
    return buf;
}

// (mem::free ruling 2026-07-03) tk_free_block — PARK an explicitly freed root-arena block on the
// free list (see the free-list overlay above tk_region_alloc) so the next same-size allocation
// REUSES it. This is `teko::mem::free`'s runtime seat: the []T arm passes the slice buffer
// (`ptr`, `len*esz` — a LOWER bound of the true capacity: the geometric spare tail is simply not
// reclaimed); the coming Ref<T>/class arm drops the object's own region instead and never lands
// here. NULL/short blocks are no-ops (a parked node needs 16 usable bytes). The push cache's
// entry for `p` is EVICTED first, so a stale live-tail record can never in-place-append into a
// block that was freed and reused (the aliased-copy hazard is the user's explicit razor — the
// direct binding is scrubbed by the lowering; see teko-mem-free-design).
void tk_free_block(void *p, uint64_t bytes) {
    if (p == NULL) return;
    { static int dbg=-1; if (dbg<0) dbg = getenv("TEKO_FO_DEBUG")?1:0;
      if (dbg) fprintf(stderr, "PARK %p bytes=%llu\n", p, (unsigned long long)bytes); }
    unsigned h = tk_push_slot(p);
    if (tk_push_cache[h].ptr == p) tk_push_cache[h].ptr = NULL;   // evict the live-tail record
    // (#148 Level-2) PARK = FLOOR-16 — never LIE about the block's size. The arena aligns to
    // _Alignof(max_align_t) (8 on arm64 Darwin), so an 8-byte block really is 8 bytes: rounding UP
    // overran the bump-adjacent NEIGHBOR by 8 (freenode header / paranoid poison corrupted live
    // data — caught by the poisoned-emission micro-repro). tk_free_take rounds the REQUEST up
    // (ceil-16), so a parked block only ever serves requests ≤ its floored true size.
    size_t usable = (size_t)bytes & ~(size_t)15;
    // (#148 Level-2 oracle) TEKO_MEM_PARANOID: POISON the block and never park it. Arena reuse is
    // invisible to ASan, so a wrong linearity proof would corrupt silently; with poison, any
    // read-after-park yields 0xDD garbage and the gate/diff harness fails LOUDLY instead.
    static int tk_paranoid = -1;
    if (tk_paranoid < 0) { const char *e = getenv("TEKO_MEM_PARANOID"); tk_paranoid = (e != NULL && *e != '\0') ? 1 : 0; }
    if (tk_paranoid == 1) { if (usable) memset(p, 0xDD, usable); return; }
    if (usable < sizeof(tk_freenode)) return;                     // too small to park — leak it (bump can't shrink)
    tk_freenode *n = (tk_freenode *)p;
    n->bytes = usable;
    if (usable <= (size_t)TK_FREE_BINS * 16) {
        tk_freenode **bin = &tk_free_bins[usable / 16 - 1];
        n->next = *bin; *bin = n;
    } else {
        n->next = tk_free_large; tk_free_large = n;
    }
    tk_free_parked_bytes += usable;
}

// --- arithmetic FFI over the i128 carrier (sign-aware) + float bit patterns ---
__int128 tk_div(__int128 a, __int128 b, bool sgn) {
    if (b == 0) tk_panic_div0();
    if (sgn) return a / b;
    return (__int128)((unsigned __int128)a / (unsigned __int128)b);
}
__int128 tk_rem(__int128 a, __int128 b, bool sgn) {
    if (b == 0) tk_panic_div0();
    if (sgn) return a % b;
    return (__int128)((unsigned __int128)a % (unsigned __int128)b);
}
double tk_fdiv(double a, double b) { if (b == 0.0) tk_panic_div0(); return a / b; }
double tk_int_to_float(__int128 v, bool sgn) {
    if (sgn) return (double)v;
    return (double)(unsigned __int128)v;
}
uint64_t tk_f64_bits(double x)      { uint64_t b; memcpy(&b, &x, sizeof b); return b; }
double   tk_f64_from_bits(uint64_t bits) { double x; memcpy(&x, &bits, sizeof x); return x; }

// ============================================================================
// teko::time  ROUND 0 — Date/Time placeholder types
// Five value structs:  DateTime / TimeSpan / Time / Date / DateTimeOffset.
// All ticks in nanoseconds; Date in days since 1970-01-01 (= day 0).
// DateTime.ticks is signed (__int128) so (dt_a - dt_b) always fits in TimeSpan.
// ============================================================================

// --- helpers ---

// POSIX-only: return nanoseconds since Unix epoch as a signed i128.
// Windows branch uses FILETIME (100-ns ticks since 1601-01-01).
static __int128 tk_time_now_ns(void) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t w = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // Subtract Windows epoch offset (1601-01-01 → 1970-01-01 = 116444736000000000 × 100ns ticks).
    w -= (uint64_t)116444736000000000ULL;
    return (__int128)w * 100;  // 100-ns ticks → nanoseconds
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (__int128)(int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}

#define TK_NS_PER_DAY   ((uint64_t)86400ULL * 1000000000ULL)

// Gregorian calendar from a days-since-epoch value (Julian Day Number algorithm).
// Based on the Richards (2013) algorithm; handles negative days (pre-1970 dates).
static void tk_jdn_to_ymd(int32_t days, int32_t *y, int32_t *m, int32_t *d_out) {
    int64_t jdn = (int64_t)days + 2440588LL;  // JDN of 1970-01-01 = 2440588
    int64_t f = jdn + 1401LL + (((4LL * jdn + 274277LL) / 146097LL) * 3LL / 4LL) - 38LL;
    int64_t e = 4LL * f + 3LL;
    int64_t g = (e % 1461LL) / 4LL;
    int64_t h = 5LL * g + 2LL;
    *d_out = (int32_t)((h % 153LL) / 5LL + 1LL);
    *m     = (int32_t)((h / 153LL + 2LL) % 12LL + 1LL);
    *y     = (int32_t)(e / 1461LL - 4716LL + (14LL - (int64_t)*m) / 12LL);
}

// --- constructors ---

tk_datetime tk_rt_datetime_now(void) {
    return (tk_datetime){ .ticks = tk_time_now_ns() };
}

tk_datetimeoffset tk_rt_datetime_local_now(void) {
    __int128 ns = tk_time_now_ns();
    int16_t offset_min = 0;
#if defined(_WIN32)
    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);
    // Windows Bias is minutes west of UTC; negate to get east (positive = ahead of UTC).
    offset_min = -(int16_t)tzi.Bias;
#else
    time_t now = (time_t)(ns / 1000000000LL);
    struct tm loc;
    localtime_r(&now, &loc);
    offset_min = (int16_t)(loc.tm_gmtoff / 60);
#endif
    return (tk_datetimeoffset){ .ticks = ns, .offset_minutes = offset_min };
}

tk_date tk_rt_date_today(void) {
    __int128 ns = tk_time_now_ns();
    // Days since epoch: divide signed ns by ns-per-day.
    // Use floor division (towards -inf) so negative ns (pre-1970) maps correctly.
    int64_t ns64 = (int64_t)(ns / 1000000000LL);  // seconds
    int32_t days;
    if (ns64 >= 0) {
        days = (int32_t)((uint64_t)ns64 / 86400ULL);
    } else {
        // floor division for negative seconds
        days = (int32_t)(((int64_t)ns64 - 86399LL) / 86400LL);
    }
    return (tk_date){ .days = days };
}

tk_time tk_rt_time_now_utc(void) {
    __int128 ns = tk_time_now_ns();
    // Time-of-day: ns modulo one day, always non-negative.
    uint64_t day_ns = TK_NS_PER_DAY;
    // For positive ns: straightforward modulo.
    // For negative ns (pre-1970): C % is truncated, adjust to positive.
    int64_t ns64 = (int64_t)(ns % (__int128)day_ns);
    if (ns64 < 0) ns64 += (int64_t)day_ns;
    return (tk_time){ .ticks = (uint64_t)ns64 };
}

tk_timespan tk_rt_timespan_from_ns(int64_t ns) {
    return (tk_timespan){ .ticks = (__int128)ns };
}

tk_date tk_rt_date_from_days(int32_t days) {
    return (tk_date){ .days = days };
}

// --- accessors ---

__int128 tk_rt_datetime_to_unix_ns(tk_datetime dt) {
    return dt.ticks;
}

int32_t tk_rt_date_year(tk_date d) {
    int32_t y, m, dd;
    tk_jdn_to_ymd(d.days, &y, &m, &dd);
    return y;
}

int32_t tk_rt_date_month(tk_date d) {
    int32_t y, m, dd;
    tk_jdn_to_ymd(d.days, &y, &m, &dd);
    return m;
}

int32_t tk_rt_date_day_of_month(tk_date d) {
    int32_t y, m, dd;
    tk_jdn_to_ymd(d.days, &y, &m, &dd);
    return dd;
}

int32_t tk_rt_time_hour(tk_time t) {
    return (int32_t)(t.ticks / 3600000000000ULL);
}

int32_t tk_rt_time_minute(tk_time t) {
    return (int32_t)((t.ticks % 3600000000000ULL) / 60000000000ULL);
}

int32_t tk_rt_time_second(tk_time t) {
    return (int32_t)((t.ticks % 60000000000ULL) / 1000000000ULL);
}

int16_t tk_rt_dto_offset_minutes(tk_datetimeoffset dto) {
    return dto.offset_minutes;
}

// --- arithmetic ---

tk_datetime tk_rt_datetime_add(tk_datetime dt, tk_timespan span) {
    return (tk_datetime){ .ticks = dt.ticks + span.ticks };
}

tk_datetime tk_rt_datetime_sub(tk_datetime dt, tk_timespan span) {
    return (tk_datetime){ .ticks = dt.ticks - span.ticks };
}

tk_timespan tk_rt_datetime_diff(tk_datetime a, tk_datetime b) {
    return (tk_timespan){ .ticks = a.ticks - b.ticks };
}

tk_timespan tk_rt_timespan_add(tk_timespan a, tk_timespan b) {
    return (tk_timespan){ .ticks = a.ticks + b.ticks };
}

tk_timespan tk_rt_timespan_sub(tk_timespan a, tk_timespan b) {
    return (tk_timespan){ .ticks = a.ticks - b.ticks };
}

__int128 tk_rt_timespan_to_ns(tk_timespan span) {
    return span.ticks;
}

tk_date tk_rt_date_add_days(tk_date d, int32_t days) {
    return (tk_date){ .days = d.days + days };
}

int32_t tk_rt_date_diff_days(tk_date a, tk_date b) {
    return a.days - b.days;
}
