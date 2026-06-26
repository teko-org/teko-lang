// src/runtime/teko_rt.c   (namespace 'teko::runtime')
// libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
#include "teko_rt.h"
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort, malloc, _Exit
#include <string.h>   // memcpy
#include <signal.h>   // signal — native crash backtraces (C1.9)
#include <execinfo.h> // backtrace, backtrace_symbols_fd (C1.9)
#include <unistd.h>   // chdir, fork, execvp, _exit (host FFI bottoms)
#include <sys/wait.h> // waitpid — teko::process::run
#include <dirent.h>   // opendir/readdir — teko::fs::list_dir
#include <sys/stat.h> // mkdir — teko::fs::mkdir (build output dir)
#include <errno.h>    // errno/EEXIST — mkdir idempotence

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
}

// Captured process argv (the generated `main` calls tk_set_args before the virtual-main body).
static int    tk_g_argc = 0;
static char **tk_g_argv = NULL;
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

// --- amortized growable push (the teko::list::push lowering — see teko_rt.h) ---
// A small cache of recent live tails. Codegen threads ONE big output buffer linearly, so its tail
// stays cached and every append is in-place; a few interleaved small temp buffers keep their own
// slots. Value-correct because a push to anything that is not a recorded live tail copy-grows.
#define TK_PUSH_SLOTS 16
static struct { const void *ptr; uint64_t len, cap, esz; } tk_push_cache[TK_PUSH_SLOTS];
static unsigned tk_push_rr = 0;

void *tk_slice_push(const void *ptr, uint64_t len, const void *elem, uint64_t esz, uint64_t *out_len) {
    int slot = -1;
    if (ptr != NULL)
        for (unsigned s = 0; s < TK_PUSH_SLOTS; s += 1)
            if (tk_push_cache[s].ptr == ptr) { slot = (int)s; break; }
    // in-place ONLY when this is the live tail (same length witness + element size) with spare cap.
    if (slot >= 0 && tk_push_cache[slot].len == len && tk_push_cache[slot].esz == esz
        && len < tk_push_cache[slot].cap) {
        memcpy((char *)ptr + len * esz, elem, esz);
        tk_push_cache[slot].len = len + 1;
        *out_len = len + 1;
        return (void *)ptr;
    }
    // copy-grow geometrically into a fresh buffer (the old one is left intact — value semantics).
    uint64_t cap = (len < 4) ? 8 : (len * 2);
    void *buf = tk_alloc(cap * esz);
    if (len) memcpy(buf, ptr, len * esz);
    memcpy((char *)buf + len * esz, elem, esz);
    // reuse this buffer's slot if it was tracked (so a doubling buffer keeps ONE slot), else round-robin.
    int dst = (slot >= 0) ? slot : (int)(tk_push_rr++ % TK_PUSH_SLOTS);
    tk_push_cache[dst].ptr = buf; tk_push_cache[dst].len = len + 1;
    tk_push_cache[dst].cap = cap; tk_push_cache[dst].esz = esz;
    *out_len = len + 1;
    return buf;
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
