// src/runtime/teko_rt.c   (namespace 'teko::runtime')
// libteko_rt impl: runtime for GENERATED Teko programs (M.1 fail-loud).
// Distinct from the compiler's own src/core.h; self-contained, libc-only.
// (C7.1f) expose POSIX (setenv/fork/execvp/opendir/getlogin/…) under strict `-std=c23` — musl
// (the Alpine/Linux pipeline) hides them otherwise. Harmless on macOS/glibc. MUST precede includes.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifdef _WIN32
#define _CRT_RAND_S   // rand_s (CSPRNG in the ucrt, no import lib) — must precede <stdlib.h> (#194 C6)
#endif
#include "teko_rt.h"
#include <ctype.h>    // isalpha (ROUND 0 UTF-8 codepoint ops)
#include <stdio.h>    // fwrite, fputc, fputs, stdout, stderr
#include <stdlib.h>   // abort, malloc, free, _Exit
#include <string.h>   // memcpy
#include <stddef.h>   // max_align_t, offsetof — arena chunk alignment (S1; also via teko_rt.h)
#include <inttypes.h> // PRId64, PRIx64, PRIX64 — format spec helpers
#include <signal.h>   // signal — native crash backtraces (C1.9)
#include <setjmp.h>   // setjmp/longjmp — the test-mode capture (§14). The ONLY user is tk_test_run.
// execinfo (backtrace) exists on macOS + glibc, but NOT musl (the Alpine/Linux pipeline). Guard it
// so the runtime is musl-portable (C7.1f); without it the backtrace degrades to a one-line notice.
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h> // backtrace, backtrace_symbols_fd (C1.9)
#define TK_HAVE_BACKTRACE 1
#endif
#ifdef _WIN32
#include "../win32_compat.h"  // chdir→_chdir, mkdir, getcwd, setenv, dirent shim, tk_win32_spawnvp
#include <malloc.h>   // _aligned_malloc / _aligned_free — over-aligned arena chunks (tk_chunk_alloc)
#include <io.h>        // _dup, _dup2, _close — fd-redirect around tk_rt_run_quiet's _spawnvp (issue #73); _pipe, _read, _get_osfhandle (F5); _open/_write (append)
#include <fcntl.h>    // _O_WRONLY/_O_CREAT/_O_APPEND/_O_BINARY/_O_NOINHERIT — tk_rt_append_file, tk_rt_pipe
#include <sys/stat.h> // _S_IREAD/_S_IWRITE — the mode tk_rt_append_file creates a new file with
#else
#include <unistd.h>   // chdir, fork, execvp, _exit (host FFI bottoms)
#include <sys/wait.h> // waitpid — teko::process::run
#include <sys/resource.h> // getrusage — teko::mem::peak_rss (#148: the compiler reports its own memory cost)
#include <dirent.h>   // opendir/readdir — teko::fs::list_dir
#include <sys/stat.h> // mkdir — teko::fs::mkdir (build output dir)
#include <fcntl.h>    // O_WRONLY — /dev/null redirect for tk_rt_run_quiet (issue #73 cc probe); FD_CLOEXEC on a pipe pair (F5)
#include <poll.h>     // poll — tk_rt_fd_wait_readable's exact-deadline wait on a pipe end (F5)
#include <sys/random.h> // getentropy (macOS) / getrandom (Linux glibc>=2.25, musl) — teko::crypto::rand (#194 C6)
#include <pthread.h>  // §16-TRANSITIONAL: pthread_create/detach/join for tk_thread_spawn (§10 C0a). Retired when teko_rt.c goes to FFI.
#include <sys/socket.h> // §10 C0c: socket/bind/sendto/recvfrom — the tk_oschan AF_UNIX transport (Linux-first)
#include <sys/un.h>     // §10 C0c: struct sockaddr_un — abstract-namespace reader name
#endif
#include <errno.h>    // errno/EEXIST — mkdir idempotence
#include <time.h>     // clock_gettime, localtime_r, CLOCK_REALTIME — teko::time ROUND 0

// (C1.9 / E4) NATIVE STACK TRACES. A generated Teko program links this runtime; on a panic (M.1)
// or a fatal signal (a bug in generated code), print a backtrace to stderr — the frames carry the
// generated function symbols (the mangled Teko names). E4: each frame is RESOLVED to its Teko
// `name (file:line)` via the `.tsym` map emitted beside the binary (E3), loaded as `<argv0>.tsym`.
// Without the map (or argv) it degrades to the raw C symbols. In the bootstrap (which also links
// this runtime), main.c installs ITS OWN handler INSIDE main() — so it wins there; this
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
    // (2026-08-03, native-region-crash, chunk-canary) TEKO_NATIVE_CRASH_CORE=1 — a CI-narrowing aid,
    // same env-gated pattern as the other TEKO_NATIVE_* diagnostics: byte-identical when unset. The
    // ordinary handler always _Exit()s (async-signal-safe, no core), which is right for CI but
    // leaves no postmortem artifact to inspect registers/memory with gdb. When armed, restore the
    // signal's default disposition and re-raise instead, so (with `ulimit -c unlimited`) the OS
    // produces a real core dump alongside the symbolized backtrace above.
    if (getenv("TEKO_NATIVE_CRASH_CORE") != NULL) {
        signal(sig, SIG_DFL);
        raise(sig);
        return;   // unreachable once the default handler re-delivers, kept for clarity
    }
    // (Windows visibility fix, Theory CI #62) _Exit() does NOT run the libc stream-flush atexit
    // machinery — only exit() does. On POSIX this fflush is a no-op (stderr is already unbuffered
    // there by convention), but it is NOT a no-op everywhere this same handler runs: see the
    // fflush in tk_win_seh_handler below for why Windows needs it explicitly.
    fflush(stderr);
    _Exit(128 + sig);   // async-signal-safe
}
#if defined(_WIN32)
// (Windows SEH twin of tk_rt_crash_handler/tk_backtrace above.) POSIX raises a SIGSEGV/SIGBUS/
// SIGILL/SIGFPE signal on a fault; Windows raises a Structured Exception instead — `signal(SIGSEGV,
// ...)` (still installed below, unconditionally, on both platforms) is NOT reliably delivered for a
// real hardware fault under this project's Windows toolchain (clang --target=x86_64-pc-windows-msvc
// — see src/build/project.tks's WINDOWS_CC_NAME/build_cc_argv; MinGW is explicitly banned there,
// owner ruling 2026-08-05), so without this the native fixpoint gate died SILENT on Windows:
// no address, nothing to feed addr2line. This is installed as a VECTORED handler
// (AddVectoredExceptionHandler) so it runs before any language/CRT __try frame gets a look, and it
// prints the SAME per-frame shape backtrace_symbols prints on the POSIX side —
// `<module-path>(+0x<offset-from-module-base>)[0x<absolute-address>]` — resolved by hand
// (GetModuleHandleExA + GetModuleFileNameA) since dbghelp/SymFromAddr symbolication is deliberately
// NOT used here, exactly like the POSIX side: resolving the printed module-relative offset to a
// Teko name/line happens OFFLINE, via `addr2line -e <binary> 0x<offset>` against the very binary
// the fixpoint already built.

// True when `code` is one of the fault classes the POSIX handler installs for (SIGSEGV, SIGBUS,
// SIGILL, SIGFPE) — the Structured-Exception codes a generated program can actually raise on a
// genuine bug, as opposed to a first-chance exception a debugger or the CRT itself expects to see
// (a C++ throw, a breakpoint, …), which must fall through this handler untouched.
static bool tk_win_seh_is_fatal(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_INT_OVERFLOW:
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
        case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_FLT_OVERFLOW:
        case EXCEPTION_FLT_UNDERFLOW:
        case EXCEPTION_FLT_DENORMAL_OPERAND:
        case EXCEPTION_FLT_INEXACT_RESULT:
        case EXCEPTION_FLT_STACK_CHECK:
            return true;
        default:
            return false;
    }
}

// One POSIX-shaped backtrace line for `addr` — `<module-path>(+0x<offset>)[0x<absolute>]`,
// matching what glibc's backtrace_symbols prints for an unsymbolized (stripped/static) frame on the
// POSIX side (tk_backtrace above), so the SAME addr2line recipe resolves either platform's crash.
// `addr` falling outside every loaded module (module lookup failure) degrades to a bare
// `[0x<absolute>]` line — still useful, just not addr2line-able.
static void tk_win_seh_print_frame(void *addr) {
    HMODULE hmod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             (LPCSTR)addr, &hmod) || hmod == NULL) {
        fprintf(stderr, "[0x%p]\n", addr);
        return;
    }
    char path[1024];
    DWORD len = GetModuleFileNameA(hmod, path, (DWORD)sizeof path);
    if (len == 0 || len >= sizeof path) {
        fprintf(stderr, "[0x%p]\n", addr);
        return;
    }
    uintptr_t offset = (uintptr_t)addr - (uintptr_t)hmod;
    fprintf(stderr, "%s(+0x%" PRIxPTR ")[0x%p]\n", path, offset, addr);
}

// SEH twin of tk_backtrace: the fault address (ep->ExceptionRecord->ExceptionAddress) followed by
// up to 62 captured return addresses (RtlCaptureStackBackTrace), each printed via
// tk_win_seh_print_frame — module-relative, exactly like the POSIX per-frame shape.
static void tk_win_seh_backtrace(EXCEPTION_POINTERS *ep) {
    fputs("teko: stack trace:\n", stderr);
    fputs("fault address: ", stderr);
    tk_win_seh_print_frame(ep->ExceptionRecord->ExceptionAddress);
    void *frames[62];
    USHORT n = RtlCaptureStackBackTrace(0, 62, frames, NULL);
    for (USHORT i = 0; i < n; i += 1) tk_win_seh_print_frame(frames[i]);
}

// The vectored exception handler itself — the Structured-Exception counterpart of
// tk_rt_crash_handler. A non-fatal code (a C++ throw, a debugger breakpoint, …) is left ALONE
// (EXCEPTION_CONTINUE_SEARCH, unchanged, so nothing downstream — the CRT, a debugger, a language
// __try — ever notices this handler exists). A fatal code prints the same "FATAL" header and
// module-relative backtrace the POSIX side prints, then terminates via _Exit (ISO C, matching
// tk_rt_crash_handler's own async-signal-safe exit) with a non-zero status so the native fixpoint
// gate observes the failure exactly like a POSIX abort.
static LONG WINAPI tk_win_seh_handler(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (!tk_win_seh_is_fatal(code)) return EXCEPTION_CONTINUE_SEARCH;
    fprintf(stderr, "\nteko: FATAL exception 0x%08lX — a generated program crashed (M.1).\n", (unsigned long)code);
    tk_win_seh_backtrace(ep);
    // (Theory CI #62, windows-x86_64 native leg: gen2 crashed 130ms into "building gen3", with
    // NOT EVEN the first "lexer 0/N files" progress line visible — the Linux legs print that AND a
    // full symbolized backtrace on the same class of crash.) ROOT CAUSE: unlike POSIX (glibc/musl),
    // the Universal CRT (UCRT — ucrtbase.dll) this binary links fully buffers stdio streams,
    // INCLUDING stderr, once they are not attached to an interactive console. This project's
    // Windows toolchain is `clang --target=x86_64-pc-windows-msvc` (src/build/project.tks's
    // WINDOWS_CC_NAME/WINDOWS_MSVC_TRIPLE/build_cc_argv; MinGW is explicitly banned there, owner
    // ruling 2026-08-05, over MinGW-gcc's own -O2 slowness and cc1/PATH/libm breakage) — targeting
    // `-windows-msvc` links the SAME UCRT the MSVC toolchain does, via `lld-link`/`link.exe`
    // through the clang driver, not MinGW's own runtime. The buffered-when-redirected behavior is
    // this UCRT's, not a MinGW artifact — and it is exactly the case in CI, since the fixpoint gate
    // pipes the child through `2>&1 | tee "$bg_log" | sed ... >&2` (fixpoint_gate.sh's build_gen).
    // progress.tks bakes in the OPPOSITE assumption ("the C standard library never buffers stderr")
    // — true on POSIX, false on Windows/UCRT once redirected. So every `teko::io::eprint`/`eprintln` progress line
    // (tk_eprint/tk_eprintln, both plain fwrite/fputc to stderr with no flush) sat in the CRT's
    // stdio buffer, AND this handler's own FATAL header + backtrace above sat in that same buffer
    // — then `_Exit()` tore the process down without running the flush-on-exit machinery
    // (`_Exit`/`_exit`, unlike `exit`, is specified to skip it), so a hard fault dropped everything
    // silently no matter how much was printed. tk_rt_install_crash_handler below now forces
    // `_IONBF` on stderr (and stdout) as the very first thing it does — before any application
    // code can write a byte — which fixes the progress lines AND makes this fflush() usually a
    // no-op; it stays as defense-in-depth in case some other code path re-buffers the stream later.
    fflush(stderr);
    _Exit(128 + (int)(code & 0x7f));
    return EXCEPTION_CONTINUE_SEARCH;   // unreachable, kept for the compiler's return-path check
}
#endif
__attribute__((constructor)) static void tk_rt_install_crash_handler(void) {
#if defined(_WIN32)
    // (Theory CI #62 root cause, part 1 of 2 — see the fflush comment in tk_win_seh_handler above
    // for the full account of WHY: this project's Windows toolchain is
    // `clang --target=x86_64-pc-windows-msvc`, which links the UCRT.) `setvbuf`/`fflush` are plain
    // ISO C / UCRT stdio API, not a compiler-specific extension — they behave identically whichever
    // front end (clang here, historically MinGW-gcc, MSVC's own cl.exe) drives the same UCRT, so
    // this fix takes effect under this project's actual toolchain exactly as reasoned. MUST run
    // before anything else in this constructor, and before any application code gets a chance to
    // write a byte: `setvbuf` is only well-defined before the stream has been touched. Forcing
    // `_IONBF` here makes stderr (and stdout, for the same buffered-when-redirected reason) behave
    // on Windows exactly the way progress.tks already assumes stderr behaves everywhere —
    // unbuffered, so every write lands immediately instead of sitting in the CRT's stdio buffer for
    // a crash's `_Exit()` to discard unflushed.
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    // (Theory CI #62 root cause, part 2 of 2 — a SEPARATE gap from the buffering one above, and
    // plausible on its own for a checker-phase crash walking 154 files' worth of recursive ASTs.)
    // EXCEPTION_STACK_OVERFLOW is in tk_win_seh_is_fatal's list above, but WITHOUT this call the
    // vectored handler runs on the SAME, already-exhausted stack that just faulted: the first
    // guard page is gone, so fprintf/RtlCaptureStackBackTrace/tk_win_seh_backtrace can (and
    // typically do) immediately re-fault on entry, delivering a SECOND, unhandled access
    // violation with NO output — silent, exactly what Theory CI #62 observed, and independent of
    // the stdio-buffering fix above (that fix cannot help if the handler body never gets to run).
    // `SetThreadStackGuarantee` must be called BEFORE the overflow, on the thread that may
    // overflow (the main thread — this compiler is single-threaded), reserving extra committed
    // stack the OS hands back specifically for a stack-overflow handler to run in. 64 KiB matches
    // what MSDN's own SEH sample uses and is ample for this handler's non-recursive, fixed-frame
    // print path.
    { ULONG tk_stk_guarantee = 64 * 1024; SetThreadStackGuarantee(&tk_stk_guarantee); }
#endif
    signal(SIGSEGV, tk_rt_crash_handler);
#ifndef _WIN32
    signal(SIGBUS,  tk_rt_crash_handler);   // not defined on Windows
#endif
    signal(SIGILL,  tk_rt_crash_handler);
    signal(SIGFPE,  tk_rt_crash_handler);
#if defined(_WIN32)
    AddVectoredExceptionHandler(1, tk_win_seh_handler);   // runs first — catches what signal() misses
#endif
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


// (C7.1a) marshalling — copy n octets from a foreign pointer into a fresh Teko []byte. A NULL
// pointer or n==0 yields an empty slice (a 1-byte buffer so the pointer is distinct). The codegen
// lifts the returned {ptr,len} to the program's tk_slice_byte. [teko::mem::bytes_from_ptr]
tk_ffi_bytes tk_bytes_from_ptr(const void *p, uint64_t n) {
    tk_byte *buf = malloc(n ? n : 1);
    if (buf == NULL) tk_panic("out of memory (bytes from ptr)");
    if (n && p) memcpy(buf, p, (size_t)n);
    return (tk_ffi_bytes){ buf, (n && p) ? n : 0 };
}



// tk_str_concat_len / tk_i64_to_str_len / tk_u64_to_str_len — out-parameter-length twins of the
// three builders above (declared in teko_rt.h; see that comment for WHY). Each is a thin wrapper:
// it calls its own two-word twin, writes the length out, and returns the pointer half.
const tk_byte *tk_str_concat_len(const tk_byte *a_ptr, uint64_t a_len, const tk_byte *b_ptr, uint64_t b_len, uint64_t *out_len) {
    tk_str r = tk_str_concat((tk_str){ a_ptr, a_len }, (tk_str){ b_ptr, b_len });
    *out_len = r.len;
    return r.ptr;
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

const tk_byte *tk_str_of_bytes_len(const tk_byte *ptr, uint64_t len, uint64_t *out_len) {
    tk_str r = tk_str_of_bytes((tk_str){ ptr, len });
    *out_len = r.len;
    return r.ptr;
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

// tk_rt_str_from_utf8_ok — the native backend's own-register twin of `tk_rt_str_from_utf8`
// (0.3.1.0 degrau 21, mirrors `tk_rt_last_index_of_ok`'s own shape): the bool return IS the
// found/failed flag, and the SAME two out-parameters carry EITHER outcome's (ptr, len) pair — the
// decoded str's on success, the "invalid UTF-8" message's on failure — so the native lowering's
// bool-branch decides which meaning to read without this twin owing it a THIRD out-parameter.
bool tk_rt_str_from_utf8_ok(const tk_byte *ptr, uint64_t len, const tk_byte **out_ptr, uint64_t *out_len) {
    tk_ffi_sres r = tk_rt_str_from_utf8(ptr, len);
    if (r.ok) {
        *out_ptr = (const tk_byte *)r.value.ptr;
        *out_len = r.value.len;
        return true;
    }
    *out_ptr = (const tk_byte *)r.err.ptr;
    *out_len = r.err.len;
    return false;
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

// tk_str_concat3 REMOVED (2026-07-01) — superseded by `concat(params pieces: []str)`, bridged
// at the call site (codegen.c/.tks) by folding N pieces via tk_str_concat; no runtime symbol needed.

// --- Phase 3 str query/slice builtins (query helpers allocate nothing; slice helpers follow
// tk_str_concat's ownership — a fresh malloc'd buffer the result OWNS, tk_panic on OOM) ---

// tk_str_eq — same length AND same bytes. memcmp (NOT strcmp — strings may hold embedded NUL).
//
// The empty pair returns BEFORE memcmp, and the reason is not that the comparison would be wrong.
// An earlier version of this comment argued the call was safe "because memcmp of 0 bytes is
// well-defined" — true about the READ, false about the CALL. memcmp's parameters carry `nonnull`,
// so passing a null pointer is undefined regardless of n, and an empty tk_str legitimately carries
// ptr == NULL. UBSan caught it live: teko_rt.c:555 "null pointer passed as argument 1, which is
// declared to never be null", on the first run of the ASan+UBSan lane.
bool tk_str_eq(tk_str a, tk_str b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

// tk_char_eq — same length AND same bytes (memcmp). The lexer only ever emits the ONE valid
// UTF-8 encoding of a codepoint, so byte-equality already IS codepoint-equality; no decode
// needed. The empty-length early return mirrors tk_str_eq's own (a null `.ptr` would violate
// memcmp's `nonnull` contract) even though a real `tk_char` is never zero-length in practice —
// keeping the two functions' shape identical is cheaper to audit than arguing the case is dead.
bool tk_char_eq(tk_char a, tk_char b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.ptr, b.ptr, a.len) == 0;
}

// tk_slice_eq_bytes — `[]T == []T` for a POD element (integer/bool/byte prim, no interior
// pointer): same length AND memcmp of the packed backing array. See the header doc for why this
// is NOT reused for float/char/str elements.
bool tk_slice_eq_bytes(const void *a_ptr, uint64_t a_len, const void *b_ptr, uint64_t b_len, uint64_t elem_size) {
    if (a_len != b_len) return false;
    if (a_len == 0) return true;
    return memcmp(a_ptr, b_ptr, (size_t)(a_len * elem_size)) == 0;
}

// tk_slice_f32_eq / tk_slice_f64_eq — same length AND every element equal by IEEE `==` (NOT
// memcmp — see the header doc for why a float's bits are not its value equality).
bool tk_slice_f32_eq(const float *a_ptr, uint64_t a_len, const float *b_ptr, uint64_t b_len) {
    if (a_len != b_len) return false;
    for (uint64_t i = 0; i < a_len; i += 1) { if (a_ptr[i] != b_ptr[i]) return false; }
    return true;
}
bool tk_slice_f64_eq(const double *a_ptr, uint64_t a_len, const double *b_ptr, uint64_t b_len) {
    if (a_len != b_len) return false;
    for (uint64_t i = 0; i < a_len; i += 1) { if (a_ptr[i] != b_ptr[i]) return false; }
    return true;
}

// tk_slice_char_eq — same length AND every element `tk_char_eq`. A `[]char` element is
// byte-for-byte a `tk_char` (both the `{uint8_t*,uint64_t}` shape), so the caller's backing array
// IS an array of `tk_char` already, with no bridging needed.
bool tk_slice_char_eq(const tk_char *a_ptr, uint64_t a_len, const tk_char *b_ptr, uint64_t b_len) {
    if (a_len != b_len) return false;
    for (uint64_t i = 0; i < a_len; i += 1) { if (!tk_char_eq(a_ptr[i], b_ptr[i])) return false; }
    return true;
}

// tk_slice_str_eq — same length AND every element `tk_str_eq`. A `[]str` element is
// byte-for-byte a `tk_str`, for the same reason `tk_slice_char_eq` needs no bridging.
bool tk_slice_str_eq(const tk_str *a_ptr, uint64_t a_len, const tk_str *b_ptr, uint64_t b_len) {
    if (a_len != b_len) return false;
    for (uint64_t i = 0; i < a_len; i += 1) { if (!tk_str_eq(a_ptr[i], b_ptr[i])) return false; }
    return true;
}

// (TR3) tk_str_hash — FNV-1a over the str's bytes (offset basis 14695981039346656037, prime
// 1099511628211, u64 wraparound). Mirrors di_type_id's derivation so a str-field structural
// `Hash` folds bytes identically on both engines; an empty str hashes to the offset basis.
uint64_t tk_str_hash(tk_str s) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < s.len; i++) {
        h ^= (uint64_t)(unsigned char)s.ptr[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// (TR3) tk_str_cmp — lexicographic byte compare: -1 if a < b, 1 if a > b, 0 if equal. Compares the
// common prefix byte-by-byte (unsigned), then the shorter str is the lesser. memcmp is NOT used
// (its sign is only guaranteed for the first differing byte, and Teko strings may hold embedded
// NUL). Used by a str-field structural `Ord`.
int64_t tk_str_cmp(tk_str a, tk_str b) {
    size_t n = a.len < b.len ? a.len : b.len;
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)a.ptr[i];
        unsigned char cb = (unsigned char)b.ptr[i];
        if (ca < cb) return -1;
        if (ca > cb) return 1;
    }
    if (a.len < b.len) return -1;
    if (a.len > b.len) return 1;
    return 0;
}

// =========================================================================
// (enabling primitive — staged off; no compiler source calls this yet) HEAP-BACKED STRING
// INTERN TABLE — a per-pass memoization cache for codegen's repeated string-builders (e.g. an
// inline variant's C type name, today rebuilt byte-identically at every use site). Deliberately
// backed by plain malloc/free, NEVER the arena: a codegen pass may straddle an arena rewind
// (tk_arena_pop / tk_arena_commit above), and a cached tk_str viewing a since-freed arena chunk
// would dangle. tk_intern_reset frees every entry so a repeated compiler invocation (or a
// per-#test rewind) never lets one compilation's cached strings leak into the next.
// =========================================================================
typedef struct tk_intern_entry {
    tk_byte *key; size_t key_len;
    tk_byte *val; size_t val_len;
    struct tk_intern_entry *next;
} tk_intern_entry;
#define TK_INTERN_BUCKETS 1024   // power of two — bucket = hash & (N-1)
// (E1-C1) tk_intern_table is now a per-task member (see the seam beside the F1 families); the bucket
// array travels with the task and tk_intern_reset frees its entries without touching another task's.
// (E1-C1) The intern accessors themselves live BELOW `struct tk_task` (search tk_intern_find): they
// dereference the per-task seam, so they must follow the struct's definition, not precede it.

// tk_str_slice — the bytes [start, end) as a ZERO-COPY VIEW into the parent str (#148). SAFE
// because a Teko `str` is IMMUTABLE and its buffer is never individually freed (arena/root or
// malloc'd-and-retained; mem::free frees only []T slice buffers, and str() snapshots its input),
// so a view has exactly the parent's lifetime and is observably identical to the old fresh-owned
// copy — while eliminating the dominant allocation in the compiler (measured 108M tiny copies /
// 762 MB + malloc overhead on a self-build via name_last_segment alone). Bounds: an out-of-range
// slice (start > end, or end past the byte length) PANICS (M.1, fail-loud on out-of-bounds).
// An empty slice keeps a valid non-NULL ptr into the parent.
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

// tk_str_slice_len / tk_str_slice_to_len / tk_str_slice_from_len — out-parameter-length twins of
// the three slice builders above (declared in teko_rt.h; see that comment for WHY). Each is a
// thin wrapper: it calls its own two-word twin, writes the length out, and returns the pointer
// half.
const tk_byte *tk_str_slice_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t start, uint64_t end, uint64_t *out_len) {
    tk_str r = tk_str_slice((tk_str){ s_ptr, s_len }, start, end);
    *out_len = r.len;
    return r.ptr;
}
const tk_byte *tk_str_slice_to_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t end, uint64_t *out_len) {
    tk_str r = tk_str_slice_to((tk_str){ s_ptr, s_len }, end);
    *out_len = r.len;
    return r.ptr;
}
const tk_byte *tk_str_slice_from_len(const tk_byte *s_ptr, uint64_t s_len, uint64_t start, uint64_t *out_len) {
    tk_str r = tk_str_slice_from((tk_str){ s_ptr, s_len }, start);
    *out_len = r.len;
    return r.ptr;
}
// tk_str_len — the byte length (no allocation).
uint64_t tk_str_len(tk_str s) {
    return s.len;
}

// tk_str_ends_with — the tail of s equals suffix. A suffix longer than s can't match; otherwise
// memcmp the last suffix.len bytes. An empty suffix matches every string, and returns EARLY for
// the same reason tk_str_eq does: memcmp's `nonnull` contract is violated by a null argument
// whatever n is, and an empty suffix legitimately carries ptr == NULL. The early return also
// avoids forming `s.ptr + s.len` when s itself is the empty str with a null ptr.
bool tk_str_ends_with(tk_str s, tk_str suffix) {
    if (suffix.len > s.len) return false;
    if (suffix.len == 0) return true;
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

// --- ROUND 0: UTF-8 codepoint operations ---

// ── Arena allocation (S1) — bump allocator over a chunk-list. See teko_rt.h. ──
// Each chunk is one aligned-malloc'd block: a header + payload, bump-filled by `used`.
// The payload must satisfy the STRONGEST alignment any Teko value needs, which is NOT
// _Alignof(max_align_t) on every target: on arm64 that is 8, while a 16-byte-aligned node would
// then land 8-byte-aligned and its dereferences would be UB (UBSan-flagged). TK_ARENA_ALIGN is
// therefore max_align_t's alignment FLOORED AT 16 — 16 on arm64, still 16 on x86_64, the same
// value the old `_Alignof(__int128)` form produced on every supported target, now spelled without
// naming a 128-bit type (128-bit primitives are gone from the language, owner ruling 2026-07-30) —
// and it drives BOTH the payload-base over-alignment (`_Alignas` on `data`) and the `used`/size
// rounding in tk_region_alloc. Chunks come from tk_chunk_alloc (a portable aligned allocator) and
// are released through tk_chunk_free, so tk_region_drop's release on each is heap-correct (no
// arena interior pointer is ever passed to the deallocator). NOTE: the seed is single-
// threaded; the lazy root init (tk_g_root) is not synchronized — revisit at S8 (concurrency).
// max_align_t is not portable in C mode: clang targeting MSVC defers <stddef.h>'s max_align_t
// to the MSVC headers, which expose it only for C++, so a C build fails to see it. Name the
// maximally-aligned fundamental types directly instead — the alignment is floored at 16 below,
// so only the fundamental max alignment (<= 16 on every supported 64-bit target) matters.
typedef union { long long tk_ll_; long double tk_ld_; void *tk_p_; } tk_max_align_t;
#define TK_ARENA_ALIGN_FLOOR 16
#define TK_ARENA_ALIGN                                                          \
    (_Alignof(tk_max_align_t) > TK_ARENA_ALIGN_FLOOR                            \
         ? _Alignof(tk_max_align_t)                                             \
         : (size_t)TK_ARENA_ALIGN_FLOOR)
struct tk_chunk { struct tk_chunk *next; size_t cap; size_t used; _Alignas(TK_ARENA_ALIGN) unsigned char data[]; };
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
_Static_assert(offsetof(struct tk_chunk, data) % TK_ARENA_ALIGN == 0,
               "chunk payload base must be TK_ARENA_ALIGN-aligned");
_Static_assert(_Alignof(struct tk_chunk) % TK_ARENA_ALIGN == 0,
               "chunk struct alignment must cover TK_ARENA_ALIGN (the _Alignas on data)");

// Portable aligned block allocator for chunks: guarantees a TK_ARENA_ALIGN-aligned base so the
// _Alignas(TK_ARENA_ALIGN) payload member is honored even where malloc under-aligns (the C
// standard only promises _Alignof(max_align_t)). POSIX uses posix_memalign (freed with plain
// free); Windows uses _aligned_malloc (freed with _aligned_free — see tk_chunk_free). NULL on OOM.
static void *tk_chunk_alloc(size_t bytes) {
#if defined(_WIN32)
    return _aligned_malloc(bytes, TK_ARENA_ALIGN);
#else
    // posix_memalign requires size a multiple of nothing but alignment a power-of-two multiple of
    // sizeof(void*); TK_ARENA_ALIGN (16) satisfies that on every 64-bit target. On failure it
    // returns non-zero and leaves the out-pointer indeterminate, so normalize to NULL.
    void *p = NULL;
    if (posix_memalign(&p, TK_ARENA_ALIGN, bytes) != 0) return NULL;
    return p;
#endif
}

// tk_canary_forget — fwd (2026-08-03, native-region-crash, chunk-canary); defined beside the rest
// of the diagnostic near tk_region_alloc. tk_chunk_free is the SOLE deallocator for a tk_chunk (a
// legitimate tk_region_drop / tk_arena_pop rewind frees chunks all the time), so every release MUST
// scrub any matching ring entry — otherwise the diagnostic would read a freed-and-reused address on
// its next check and report a "corruption" that is really just a false positive over memory the
// allocator legitimately handed to someone else.
static void tk_canary_forget(struct tk_chunk *c);

// Release a chunk block obtained from tk_chunk_alloc. Windows _aligned_malloc blocks MUST NOT be
// passed to plain free(); every chunk-release site routes through here to stay heap-correct.
static void tk_chunk_free(struct tk_chunk *c) {
    tk_canary_forget(c);
#if defined(_WIN32)
    _aligned_free(c);
#else
    free(c);
#endif
}

// Allocate a chunk with `payload` usable bytes; NULL on OOM (the caller decides retry/panic).
static struct tk_chunk *tk_chunk_try(size_t payload) {
    struct tk_chunk *c = tk_chunk_alloc(offsetof(struct tk_chunk, data) + payload);
    if (c != NULL) { c->next = NULL; c->cap = payload; c->used = 0; }
    return c;
}

// =========================================================================
// (F1) THE TASK — the seat of the memory discipline, one per concurrent flow of control.
//
// Before F1 the discipline was PROCESS-GLOBAL: one root region, one arena mark stack, one free
// list. That is unsound the moment two flows run at once, because the marks are a single LIFO —
// task B's tk_arena_pop rewinds the root past allocations task A is still using, and A's live
// objects become other people's fresh bytes. Each family below is therefore moved INTO the task.
//
// THE SIGNATURES DO NOT CHANGE. tk_alloc/tk_region_alloc/tk_arena_push/... keep their exact
// prototypes and read tk_task_current() from the inside, so the ~262 call sites in the runtime
// and the 80 emitted-C literals in src/codegen/codegen.tks are untouched by F1.
//
// WHAT DELIBERATELY STAYS PROCESS-GLOBAL, and why it MUST:
//   * tk_g_region_gen — the region-generation counter. Per-task counters would each start at 0
//     and hand the SAME (region-address, gen) pair to two tasks, so tk_push_cache would false-hit
//     across tasks and an in-place append would write into a foreign live tail. The generation is
//     what makes a recycled address distinguishable, so it must be unique BETWEEN tasks: one
//     counter, bumped atomically (see tk_region_gen_next).
//   * tk_obs_* — the arena lifetime map. It is a process-wide DIAGNOSTIC AGGREGATE (env-gated,
//     off by default), not part of the memory discipline: nothing is freed on its say-so, so a
//     race there costs a wrong histogram, never a wrong free. Splitting it per task would also
//     put ~1.9 MB of tables (5 x TK_OBS_CAP x sizeof(tk_obs_site)) in EVERY task and turn one
//     lifetime map into N partial ones. It needs a lock when tasks actually run concurrently.
//
// THE ACCESSOR IS THE WHOLE SEAM — one function, two encarnations. Today: a _Thread_local
// pointer, which is a single %fs-relative load under the local-exec TLS model. In the native
// Teko runtime: pthread_getspecific / TlsGetValue behind an `extern fn`, i.e. an ordinary C call
// to an external symbol, which the own backend already emits. Neither encarnation asks the
// backend for a new capability, and swapping one for the other touches ONE function.
// =========================================================================
typedef struct tk_freenode { struct tk_freenode *next; size_t bytes; } tk_freenode;
#define TK_FREE_BINS 4096                           // (i+1)*16 bytes, i.e. 16..65536 — (#148 Level-2) the doubling-ladder steps of struct lists (esz ~100-300 B × cap 32-256) must BIN exactly (the bounded large-list scan barely reuses them); 4096 ptr slots = 32 KB per task
#define TK_PUSH_HASH_SIZE (1u << 16)                // 65536 single-probe buckets
#define TK_ARENA_MARK_MAX 64                        // depth of the per-task arena checkpoint stack (see tk_arena_push)
#define TK_REGION_STACK_MAX 64                       // (C1) depth of the per-task current-region stack (see tk_region_enter)
// TK_TEST_CAPTURE_DEPTH_MAX — how deep the capture stack goes. Two is what exists (a harness run
// and a guard's inner run); the slack costs a few hundred bytes and removes a cliff.
#define TK_TEST_CAPTURE_DEPTH_MAX 4
typedef struct { struct tk_chunk *chunk; size_t used; } tk_arena_mark;
typedef struct { const void *ptr; uint64_t len, cap, esz; tk_region *region; uint64_t region_gen; } tk_push_slot_entry;

/** tk_chan — one captured output stream of a running `#test` (the per-test channel; see the
    PER-TEST CHANNEL section). Defined here, ahead of `struct tk_task`, so the task can hold its
    stdout/stderr capture by value (E1-C1: the channel is per-task, not a process singleton). */
typedef struct { char *buf; size_t len; size_t cap; } tk_chan;

typedef struct tk_task {
    tk_region      *regs;                 // (W9.3b) registry of THIS task's live regions: on it from tk_region_new until drop
    tk_region      *root;                 // this task's root region — lazy + idempotent (the root is also on `regs`)
    tk_freenode    *free_bins[TK_FREE_BINS];   // mem::free overlay, ROOT-only: parked blocks live in THIS task's root chunks
    tk_freenode    *free_large;           // > 4096 B parked blocks, bounded first-fit
    unsigned long long free_parked_bytes, free_reused_bytes, free_reused_count;   // free-list accounting
    tk_arena_mark   arena_marks[TK_ARENA_MARK_MAX];   // checkpoint stack: (head chunk, its used offset)
    int             arena_msp;            // checkpoint stack pointer (may exceed the cap; see tk_arena_push)
    tk_region      *cur_regions[TK_REGION_STACK_MAX];   // (C1) current-region stack: tk_alloc bumps from the TOP
    int             cur_rsp;              // (C1) current-region stack pointer (may exceed the cap; see tk_region_enter)
    tk_push_slot_entry push_cache[TK_PUSH_HASH_SIZE];   // (#148) live-tail witnesses for in-place slice append
    jmp_buf         test_jb[TK_TEST_CAPTURE_DEPTH_MAX];   // (§14) test-mode capture — a longjmp NEVER crosses tasks
    volatile sig_atomic_t test_depth;     // >0 = a captured body is running on THIS task
    volatile int32_t test_how[TK_TEST_CAPTURE_DEPTH_MAX];    // per-level outcome kind
    volatile int32_t test_code[TK_TEST_CAPTURE_DEPTH_MAX];   // per-level outcome code
    // (E1-C1) Category C — the singletons a `#test` body or the gate touches. Process-global before
    // this axis, now per-task so a reshuffled test cannot read another test's residue. Large tables
    // (intern) keep their malloc'd BUFFER across a reset; only the counts/marks zero.
    tk_intern_entry *intern_table[TK_INTERN_BUCKETS];        // C-1 codegen string-intern cache
    long            test_ran, test_passed, test_failed, test_exited;    // C-6 test tally
    int32_t         test_probe_last_code;                    // C-7 last probe's outcome code
    char            scope_buf[TK_TEST_SCOPE_MAX]; size_t scope_len;     // C-8 running test's scope label
    char            scen_name[TK_TEST_LABEL_MAX]; size_t scen_len; char scen_prefix[TK_TEST_LABEL_MAX + 2];   // C-9 current scenario
    tk_chan         chan_out, chan_err; char chan_label[TK_TEST_LABEL_MAX]; size_t chan_label_len; bool chan_open;   // C-10 stdout/stderr capture
    bool            rt_stdin_eof_flag;                       // C-11 stdin EOF sticky flag
    tk_byte         rt_fd_stage[TK_RT_PIPE_CAPACITY]; size_t rt_fd_staged, rt_fd_taken;   // C-12 fd staging (pipe)
} tk_task;

// tk_g_main_task — the task every program starts on. Statically allocated (not malloc'd) so the
// very first allocation, which happens long before any allocator is usable, already has a seat.
static tk_task tk_g_main_task;

// tk_g_current_task — the seam. _Thread_local under the C seed: one %fs-relative load, which the
// measurement in docs/medicoes/bench_tk_task_current.c puts at +0.04 ns/alloc at -O2 and
// +2.0 ns/alloc at -O0 versus the old plain global.
static _Thread_local tk_task *tk_g_current_task = NULL;

// tk_task_current — the task owning this flow of control's memory discipline. Never NULL: a flow
// that has not been given a task explicitly runs on the main task, which is what keeps every
// single-task program byte-for-byte the program it was before F1.
tk_task *tk_task_current(void) {
    tk_task *t = tk_g_current_task;
    if (t == NULL) { t = &tk_g_main_task; tk_g_current_task = t; }
    return t;
}

// The 11 families, now read through the seam. The names are unchanged ON PURPOSE: every existing
// use site keeps its exact text, so F1 is a change of WHERE the state lives, not of who touches it.
#define tk_g_regs             (tk_task_current()->regs)
#define tk_g_root             (tk_task_current()->root)
#define tk_free_bins          (tk_task_current()->free_bins)
#define tk_free_large         (tk_task_current()->free_large)
#define tk_free_parked_bytes  (tk_task_current()->free_parked_bytes)
#define tk_free_reused_bytes  (tk_task_current()->free_reused_bytes)
#define tk_free_reused_count  (tk_task_current()->free_reused_count)
#define tk_arena_marks        (tk_task_current()->arena_marks)
#define tk_arena_msp          (tk_task_current()->arena_msp)
#define tk_cur_regions        (tk_task_current()->cur_regions)
#define tk_cur_rsp            (tk_task_current()->cur_rsp)
#define tk_push_cache         (tk_task_current()->push_cache)
#define tk_test_jb            (tk_task_current()->test_jb)
#define tk_test_depth         (tk_task_current()->test_depth)
#define tk_test_how           (tk_task_current()->test_how)
#define tk_test_code          (tk_task_current()->test_code)

// (E1-C1) The Category C families, read through the SAME seam. As with the 11 F1 families the names
// are unchanged on purpose: every existing use site in this file keeps its exact text, so this axis
// is a change of WHERE the state lives, not of who touches it.
#define tk_intern_table       (tk_task_current()->intern_table)
#define tk_test_ran           (tk_task_current()->test_ran)
#define tk_test_passed        (tk_task_current()->test_passed)
#define tk_test_failed        (tk_task_current()->test_failed)
#define tk_test_exited        (tk_task_current()->test_exited)
#define tk_test_probe_last_code (tk_task_current()->test_probe_last_code)
#define tk_scope_buf          (tk_task_current()->scope_buf)
#define tk_scope_len          (tk_task_current()->scope_len)
#define tk_scen_name          (tk_task_current()->scen_name)
#define tk_scen_len           (tk_task_current()->scen_len)
#define tk_scen_prefix        (tk_task_current()->scen_prefix)
#define tk_chan_out           (tk_task_current()->chan_out)
#define tk_chan_err           (tk_task_current()->chan_err)
#define tk_chan_label         (tk_task_current()->chan_label)
#define tk_chan_label_len     (tk_task_current()->chan_label_len)
#define tk_chan_open          (tk_task_current()->chan_open)
#define tk_rt_stdin_eof_flag  (tk_task_current()->rt_stdin_eof_flag)
#define tk_rt_fd_stage        (tk_task_current()->rt_fd_stage)
#define tk_rt_fd_staged       (tk_task_current()->rt_fd_staged)
#define tk_rt_fd_taken        (tk_task_current()->rt_fd_taken)

// (E1-C1) The string-intern accessors, moved down from beside their typedef so they can read the
// per-task seam above (`tk_intern_table` now dereferences `struct tk_task`, complete only here).

// tk_intern_find — the bucket chain entry whose key equals `key`, or NULL. Shared by get/put so
// the equal-key scan (hash bucket + length + memcmp) has exactly one definition.
static tk_intern_entry *tk_intern_find(size_t bucket, tk_str key) {
    for (tk_intern_entry *e = tk_intern_table[bucket]; e != NULL; e = e->next) {
        if (e->key_len == key.len && (key.len == 0 || memcmp(e->key, key.ptr, key.len) == 0)) return e;
    }
    return NULL;
}

// tk_intern_dup — a fresh heap copy of `s` (>=1 byte, so an empty value never shares tk_alloc's
// NULL-for-zero convention with a real miss). OOM panics (M.1).
static tk_byte *tk_intern_dup(tk_str s) {
    tk_byte *b = malloc(s.len ? s.len : 1);
    if (b == NULL) tk_panic("out of memory (intern)");
    if (s.len) memcpy(b, s.ptr, s.len);
    return b;
}

// tk_intern_get returns a FRESH, INDEPENDENT copy of the cached value — never a view into the
// table's own entry. A view would dangle the instant a LATER tk_intern_put overwrites (or
// tk_intern_reset frees) that same entry, breaking Teko's str value-semantics contract (a
// returned str's lifetime is never tied to some other call's future). The copy costs one hash
// lookup + one memcpy, still far cheaper than the caller's original rebuild-from-scratch loop.
tk_str tk_intern_get(tk_str key) {
    size_t h = (size_t)(tk_str_hash(key) & (TK_INTERN_BUCKETS - 1));
    tk_intern_entry *e = tk_intern_find(h, key);
    if (e == NULL) return (tk_str){ NULL, 0 };
    tk_str cached = { e->val, e->val_len };
    return (tk_str){ tk_intern_dup(cached), e->val_len };
}

tk_str tk_intern_put(tk_str key, tk_str value) {
    size_t h = (size_t)(tk_str_hash(key) & (TK_INTERN_BUCKETS - 1));
    tk_intern_entry *e = tk_intern_find(h, key);
    if (e != NULL) {
        free(e->val);
        e->val = tk_intern_dup(value); e->val_len = value.len;
        return value;
    }
    tk_intern_entry *ne = malloc(sizeof *ne);
    if (ne == NULL) tk_panic("out of memory (intern)");
    ne->key = tk_intern_dup(key); ne->key_len = key.len;
    ne->val = tk_intern_dup(value); ne->val_len = value.len;
    ne->next = tk_intern_table[h];
    tk_intern_table[h] = ne;
    return value;
}

void tk_intern_reset(void) {
    for (int i = 0; i < TK_INTERN_BUCKETS; i += 1) {
        tk_intern_entry *e = tk_intern_table[i];
        while (e != NULL) {
            tk_intern_entry *next = e->next;
            free(e->key); free(e->val); free(e);
            e = next;
        }
        tk_intern_table[i] = NULL;
    }
}

// (E1-C2/C3) THE CENTRALISED PER-TASK RESET. The leak the reshuffle exposes is a forgotten reset;
// the cure is to make forgetting impossible by giving the current task's ephemeral state exactly ONE
// place that clears it. A new per-task singleton added in the future is cleared here and nowhere
// else, so no call site can omit it — the invariant, by construction rather than by vigilance.

// tk_task_reset_transient — the leak-prone, NON-accumulator slice of the current task's ephemeral
// state: everything a `#test` can dirty that neither a later test nor the run summary is meant to
// read back. Run at the entry of every `#test` (tk_test_begin) so a reshuffled order can expose no
// residue, and folded into the full tk_task_reset so the two can never drift apart. It clears the
// string-intern table, the scope and scenario labels, the probe
// code and the stdin/fd staging; it deliberately leaves the coverage sinks and the tally alone —
// those ACCUMULATE across a lane's tests and are read AFTER the run, never between tests.
static void tk_task_reset_transient(void) {
    tk_task *t = tk_task_current();
    tk_intern_reset();                                   // frees this task's cached entries; buckets stay
    t->scope_len = 0;                                    // C-8
    t->scen_len = 0;                                     // C-9
    t->test_probe_last_code = 0;                         // C-7
    t->rt_stdin_eof_flag = false;                        // C-11
    t->rt_fd_staged = 0; t->rt_fd_taken = 0;             // C-12
}

// tk_task_reset — the FULL single-site reset: the transient slice PLUS the accumulators, returning
// the current task to the ephemeral-clean state of "before any test". The coverage sinks and the
// capture channels keep their malloc'd BUFFER (only the counts/marks zero), so the reset is never a
// reallocation. Every table is cleared through its own existing reset (one definition apiece), so the
// buffer-preservation contract lives in one place per family. The exported symbol takes no argument
// and resolves the task through the same `tk_task_current()` seam as tk_arena_push — the wrapper the
// `task_reset` builtin lowers to; the full-rewind-of-a-foreign-lane variant belongs with the lane
// pool of the thread mode (S8), where a parent resets a lane whose flow of control has ended.
void tk_task_reset(void) {
    tk_task *t = tk_task_current();
    tk_task_reset_transient();
    t->test_ran = 0; t->test_passed = 0; t->test_failed = 0; t->test_exited = 0;   // C-6 tally
    t->chan_out.len = 0; t->chan_err.len = 0;            // C-10 capture buffers kept, contents forgotten
    t->chan_label_len = 0; t->chan_open = false;
    t->arena_msp = 0;                                    // forget arena checkpoints; root chunks untouched
}

// tk_push_cache_purge — fwd; the cache lives beside tk_slice_push far below, but the task teardown
// above it must be able to drop every live-tail witness before the memory they name is reclaimed.
static void tk_push_cache_purge(void);

// tk_g_region_gen — PROCESS-GLOBAL by necessity (see the ruling above). tk_region_gen_next hands
// out the next stamp; it is the one counter that must never be per-task.
static uint64_t tk_g_region_gen = 0;

// tk_region_gen_next — the next never-reused region generation, unique ACROSS tasks. Atomic where
// the compiler offers C11 atomics, so two tasks creating regions at once cannot collide on a stamp.
static uint64_t tk_region_gen_next(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_add_fetch(&tk_g_region_gen, 1, __ATOMIC_RELAXED);
#else
    return ++tk_g_region_gen;
#endif
}

// (E1-C4) A dependency-free test-and-set SPINLOCK, in the same __atomic idiom as tk_region_gen_next
// so it needs no <pthread.h> and no extra link. It guards the two Category-B singletons that must
// stay process-global (§1b of the axis design): the arena-lifetime observability map and the lazy
// PROGRAM region init. Both are engaged only where a race is actually possible — the obs funnel only
// under TEKO_ARENA_OBS, the program-region guard only on the first touch — so a single-threaded run
// (every process-shard leg today) pays a predicted, uncontended acquire and nothing more. On a
// compiler without the GNU atomics the lock degrades to a no-op, exactly as the generation counter
// does: correct for the single-threaded reality, and the thread mode that needs the real thing (S8)
// is not landed.
static volatile int tk_g_spin_obs = 0;
static void tk_spin_lock(volatile int *lock) {
#if defined(__GNUC__) || defined(__clang__)
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE)) { }
#else
    (void)lock;
#endif
}
static void tk_spin_unlock(volatile int *lock) {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
#else
    (void)lock;
#endif
}

// =========================================================================
// (mem::free ruling 2026-07-03) FREE-LIST OVERLAY over the ROOT region — the runtime seat of
// `teko::mem::free_slice` (and the coming `free<T>`). A bump arena cannot return an individual
// block to the bump, so an explicitly freed block is PARKED on a size-class free list instead,
// and tk_region_alloc consults that list BEFORE bumping — real REUSE: an explicit free stops
// the footprint from growing even though the region is never dropped.
//   * bins: exact-size classes for blocks ≤ 4096 B (16-byte steps = TK_ARENA_ALIGN granularity,
//     the same rounding tk_region_alloc applies), single-probe pop = O(1);
//   * large list: > 4096 B, bounded first-fit (size must be ≥ request and ≤ 2× — no headers, so
//     a block is never split; the cap avoids quadratic scans).
// ROOT-ONLY by design: parked blocks live inside root chunks, which are never freed mid-run —
// except by the test-gate rewind (tk_arena_pop), which PURGES the whole list first (its parked
// blocks may sit inside the chunks the rewind frees). PER TASK since F1: a parked block belongs to
// the root region of the task that freed it, so another task must never be handed it.
static void tk_free_purge(void) {                   // rewind/termination: parked blocks may now dangle
    tk_task *t = tk_task_current();                 // one seam read, not TK_FREE_BINS of them
    for (int i = 0; i < TK_FREE_BINS; i += 1) t->free_bins[i] = NULL;
    t->free_large = NULL;
    t->free_parked_bytes = 0;
}
// tk_canary_check_head — fwd; the diagnostic itself is defined beside tk_region_alloc further down
// (it needs `struct tk_chunk` and the region machinery in scope), but tk_free_take/tk_free_block
// sit ahead of it in the file and are call sites too (2026-08-03, native-region-crash, chunk-canary).
static void tk_canary_check_head(tk_region *r, const char *where);
tk_region *tk_region_current(void);   // fwd (defined later in this file; public in teko_rt.h)

static void *tk_free_take(size_t an) {
    tk_canary_check_head(tk_region_current(), "tk_free_take:entry");
    // (#148 Level-2) TAKE = CEIL-16 — never UNDERSTATE the need. The bin granularity is 16 bytes
    // (= TK_ARENA_ALIGN); requests round up so bin[qa] blocks are exactly qa ≥ an bytes. Ceil (not
    // floor) so a 24-byte request never gets handed a 16-byte block (an 8-byte OVERRUN into the
    // neighbor). (qa ≥ 16 always, so the old bins[-1] underflow guard (#150) is subsumed.)
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
// (journal J3, family #27) tk_obs_pid_path holds the PID-STAMPED default `TEKO_ARENA_OBS=1` answers
// with, so two `teko test` runs on the SAME host no longer atrophy each other's histogram with a
// last-writer-wins dump to one shared, unstamped /tmp file — the one family of this scan with no run
// identity at all before this fix. An EXPLICIT path (`TEKO_ARENA_OBS=<path>`) is left untouched: the
// caller named it on purpose and gets exactly that file, stamped or not.
static char tk_obs_pid_path[64];
static int tk_obs_enabled(void) {
    if (tk_obs_on < 0) {
        const char *e = getenv("TEKO_ARENA_OBS");
        tk_obs_on = (e != NULL && *e != '\0') ? 1 : 0;
        if (tk_obs_on) {
            if (strcmp(e, "1") == 0) {
                snprintf(tk_obs_pid_path, sizeof tk_obs_pid_path, "/tmp/teko_arena_obs.%lld.txt", (long long)tk_rt_pid());
                tk_obs_path = tk_obs_pid_path;
            } else {
                tk_obs_path = e;
            }
        }
    }
    return tk_obs_on;
}
static void tk_obs_add(tk_obs_site *tab, void *ra, size_t n) {
    // (E1-C4) The ONE funnel every histogram write passes through, so one lock here serialises the
    // whole open-addressed insert against concurrent lanes. A race would corrupt a probe count, never
    // a free — the map is a diagnostic — so the guard is scoped to the funnel and to obs being on.
    tk_spin_lock(&tk_g_spin_obs);
    size_t h = ((uintptr_t)ra >> 4) & (TK_OBS_CAP - 1);
    for (;;) {
        if (tab[h].ra == ra || tab[h].ra == NULL) {
            tab[h].ra = ra; tab[h].bytes += n; tab[h].count += 1;
            tk_spin_unlock(&tk_g_spin_obs);
            return;
        }
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
        // (obs) also emit the RAW return address and the module base so a STATIC/stripped bootstrap
        // binary (where dladdr yields no dli_sname → "?") can still be named offline: the file offset
        // `ra - fbase` fed to `addr2line -e <bin>` resolves the generated calling fn. Zero cost when
        // obs is off (this runs only inside tk_obs_dump).
        void *ra = tab[best].ra; void *fbase = NULL;
#if !defined(_WIN32)
        Dl_info di;
        if (dladdr(ra, &di)) { if (di.dli_sname) nm = di.dli_sname; fbase = di.dli_fbase; }
#endif
        fprintf(fp, "  %2d  %9.1f MB  %11llu allocs  %-24s ra=%p base=%p off=0x%llx\n",
                k, tab[best].bytes / 1048576.0, tab[best].count, nm, ra, fbase,
                (unsigned long long)((char *)ra - (char *)fbase));
    }
}
static void tk_obs_dump(void) {
    if (tk_obs_on != 1) return;
    FILE *fp = fopen(tk_obs_path, "w");
    if (fp == NULL) fp = stderr;
    unsigned long long live = tk_obs_root_bytes + tk_obs_scoped_bytes;
    // (reclaim ratio honesty) the free-list REUSE is genuine reclamation — a same-size allocation
    // served from a parked block costs no new arena bytes — but the ratio historically counted only
    // region drops + test-gate rewinds, so a corpus whose reclamation is almost entirely the
    // escape-proven tk_slice_push_fo free-old parking read as "0.0% reclaimed" while the free-list
    // was in fact recycling hundreds of MB (measured: disabling it via TEKO_FO_MAX=0 raises the
    // self-build peak by ~0.6 GB). Counting reused bytes makes the headline number tell that truth.
    unsigned long long reclaimed = tk_obs_drop_bytes + tk_obs_rewind_bytes + tk_free_reused_bytes;
    fprintf(fp, "=== ARENA LIFETIME MAP (TEKO_ARENA_OBS) ===\n");
    fprintf(fp, "root (process-lifetime, never freed): %10.1f MB\n", tk_obs_root_bytes / 1048576.0);
    fprintf(fp, "scoped (freed at region drop):        %10.1f MB\n", tk_obs_scoped_bytes / 1048576.0);
    fprintf(fp, "reclaimed by region drops:            %10.1f MB   (%llu of %llu regions dropped)\n",
            tk_obs_drop_bytes / 1048576.0, tk_obs_regions_dropped, tk_obs_regions_new);
    fprintf(fp, "reclaimed by test-gate rewinds:       %10.1f MB\n", tk_obs_rewind_bytes / 1048576.0);
    fprintf(fp, "reclaimed by free-list reuse (fo):    %10.1f MB   (%llu takes)\n",
            tk_free_reused_bytes / 1048576.0, tk_free_reused_count);
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

// tk_region_new_on — the region constructor, parameterised by the REGISTRY the new region joins.
// (F2) The program region must be on NO task's registry, so which list a region joins is an
// argument here instead of the assumption ("the current task's") it used to be.
static tk_region *tk_region_new_on(tk_region **registry, tk_region *parent) {
    tk_region *r = malloc(sizeof *r);          // the region header is itself a libc block
    if (r == NULL) tk_panic("out of memory");  // (so tk_region_drop can free() it)
    if (tk_obs_enabled()) tk_obs_regions_new += 1;   // (S2 obs) lifecycle counter
    r->head = NULL;                            // lazy: the first alloc creates the head chunk
    r->reg_next = *registry;                   // (W9.3b) prepend onto the live-region registry
    *registry = r;
    r->parent = parent;                        // (S2) the arena tree edge
    r->entries = NULL; r->nentries = 0; r->entries_cap = 0;   // (S2) the per-region registry, lazy
    r->gen = tk_region_gen_next();              // (S2 Level-1) unique lifetime stamp (0 is never assigned, so a zeroed cache entry never matches a live region); (F1) unique ACROSS tasks
    return r;
}

tk_region *tk_region_new(tk_region *parent) {
    return tk_region_new_on(&tk_task_current()->regs, parent);
}



// (2026-08-03, native-region-crash, chunk-canary) TEKO_NATIVE_CHUNK_CANARY — a CI-narrowing aid, the
// same env-gated pattern as TEKO_NATIVE_REGION_CHECK/TEKO_NATIVE_CONST_SIZE_CHECK: byte-identical
// when unset (one predicted getenv-cached int compare), NOT part of the ordinary output. It exists
// to bisect the "wild-write cumulative" that eventually SIGSEGVs deep inside tk_region_alloc
// (docs/design/native-lowering-oom-por-funcao-0.3.1.md's own successors already named the region-
// balance and const-size suspects and cleared them; this one targets a stray native store that
// overruns a bump-allocated block and lands on a NEIGHBORING tk_chunk's header fields).
//
// NO STRUCT CHANGE: `struct tk_chunk` keeps its exact layout (no magic field — that would move
// `offsetof(data)` unconditionally, the one thing every existing diagnostic here refuses to do).
// Instead a SEPARATE side-table snapshots {cap, next} for EVERY chunk this process ever creates, AT
// CREATION TIME (tk_region_alloc's own new-chunk branch, the only place a tk_chunk is born), and
// re-checks the CURRENT head chunk of the region a caller is about to allocate from on every
// tk_region_alloc entry: a live chunk's `cap`/`next` never change after creation (only `used` does,
// by the bump itself), so any discrepancy is definitionally a write nobody but this runtime's own
// chunk bookkeeping should ever perform.
//
// FULL HISTORY, NOT A RECENCY WINDOW — this replaced an earlier 512-chunk RING (measured, see the
// commit history): a full self-build run reached the very LAST native-lowering item
// (virtual-main's own final ELF emission, `elf_collect_const_entries`/`elf_partition_data_rel_ro`)
// and its `tk_slice_push_fo` grow still crashed inside `tk_region_alloc` with the ring reporting
// NOTHING — proof the corrupted head chunk predates whatever the last 512 births were. An
// open-addressed hash table (keyed by chunk address, tombstone-free — `used` is sticky so a probe
// chain past a freed slot still finds anyone hashed behind it) gives O(1) record/forget/lookup at
// unbounded size, so a chunk born at item 12 that only becomes somebody's live head again at
// virtual-main is still covered.
#define TK_CANARY_TAB_BITS 20
#define TK_CANARY_TAB_SIZE (1u << TK_CANARY_TAB_BITS)   // 1,048,576 slots * 32 B ≈ 32 MB — one-time diagnostic run only
typedef struct { struct tk_chunk *chunk; size_t cap; struct tk_chunk *next_snapshot; int used; int live; } tk_canary_entry;
static tk_canary_entry *tk_canary_tab = NULL;   // lazily malloc'd (only when the diagnostic is on)
static int tk_canary_tripped = 0;               // latches so one corruption is reported once, not per-call-site

static int tk_canary_enabled(void) {
    static int v = -1;
    if (v < 0) {
        const char *e = getenv("TEKO_NATIVE_CHUNK_CANARY");
        v = (e != NULL && e[0] == '1') ? 1 : 0;
        if (v == 1) tk_canary_tab = calloc(TK_CANARY_TAB_SIZE, sizeof *tk_canary_tab);
    }
    return v;
}

static unsigned tk_canary_hash(const struct tk_chunk *c) {
    uintptr_t v = (uintptr_t)c;
    v ^= v >> 15; v *= (uintptr_t)0x2545F4914F6CDD1DULL; v ^= v >> 13;
    return (unsigned)v & (TK_CANARY_TAB_SIZE - 1);
}

// tk_canary_check_region — fwd (defined further down, once tk_g_program is in scope): does `r`
// (the region a caller is about to allocate FROM) still appear on a live region registry at all? A
// `tk_region` is itself a small plain-malloc'd struct (`tk_region_new_on`), NOT part of any chunk
// arena — a wild write that lands on ONE OF THOSE instead of a chunk header would corrupt fields the
// chunk table above never inspects (head/reg_next/parent/gen), and the very FIRST symptom would be
// exactly what the mission's repro shows: no chunk-table trip, yet `tk_region_alloc` still dies
// dereferencing `r->head`. Complements, does not replace, the chunk-header table.
static void tk_canary_check_region(tk_region *r, const char *where);

// tk_canary_record — snapshot a just-born chunk's immutable header fields into the table. Called
// ONLY from tk_region_alloc's new-chunk branch (the sole birthplace of a tk_chunk), and only when
// the diagnostic is on. Linear probe to the first slot that is either unused or already names `c`
// (a legitimate re-birth at a reused address after tk_canary_forget).
static void tk_canary_record(struct tk_chunk *c) {
    unsigned h = tk_canary_hash(c);
    for (unsigned probe = 0; probe < TK_CANARY_TAB_SIZE; probe += 1) {
        unsigned i = (h + probe) & (TK_CANARY_TAB_SIZE - 1);
        if (!tk_canary_tab[i].used || tk_canary_tab[i].chunk == c) {
            tk_canary_tab[i] = (tk_canary_entry){ .chunk = c, .cap = c->cap, .next_snapshot = c->next, .used = 1, .live = 1 };
            return;
        }
    }
    // table saturated (every slot `used`) — astronomically unlikely at 2^20 entries; drop silently,
    // this is a best-effort diagnostic, never a correctness mechanism.
}

// tk_canary_forget — mark the entry naming `c` dead (called from tk_chunk_free, the sole
// deallocator): a LEGITIMATE tk_region_drop / tk_arena_pop rewind frees chunks constantly, and
// without this the diagnostic would read a freed-and-reused address on its next check and report a
// false "corruption" over memory the allocator has since handed to someone else entirely. `used`
// stays set (a tombstone) so the probe chain for anyone hashed behind this slot still terminates
// correctly; only `live` clears.
static void tk_canary_forget(struct tk_chunk *c) {
    if (!tk_canary_enabled()) return;
    unsigned h = tk_canary_hash(c);
    for (unsigned probe = 0; probe < TK_CANARY_TAB_SIZE; probe += 1) {
        unsigned i = (h + probe) & (TK_CANARY_TAB_SIZE - 1);
        if (!tk_canary_tab[i].used) return;   // never recorded (or table saturated past here) — nothing to forget
        if (tk_canary_tab[i].chunk == c) { tk_canary_tab[i].live = 0; return; }
    }
}

// tk_canary_check_head — verify `r->head` (the ONLY chunk tk_region_alloc actually dereferences)
// against its birth-time snapshot, wherever in history it was recorded. `where` names the call site
// so the report pins which family of allocator activity was running when the corruption was FIRST
// observed — not necessarily where the corrupting write itself executed, but the nearest allocator
// checkpoint after it, which is exactly the "next allocation names the culprit" narrowing this
// diagnostic buys.
static void tk_canary_check_head(tk_region *r, const char *where) {
    if (!tk_canary_enabled() || tk_canary_tripped || r == NULL || r->head == NULL) return;
    struct tk_chunk *c = r->head;
    unsigned h = tk_canary_hash(c);
    unsigned probe;
    for (probe = 0; probe < TK_CANARY_TAB_SIZE; probe += 1) {
        unsigned i = (h + probe) & (TK_CANARY_TAB_SIZE - 1);
        if (!tk_canary_tab[i].used) break;          // exhausted the probe chain without finding `c` — fall through to the report below
        if (tk_canary_tab[i].chunk != c) continue;
        tk_canary_entry *e = &tk_canary_tab[i];
        // EVERY tk_chunk this process ever creates is recorded right after tk_chunk_try (the sole
        // birthplace) with the SAME gate this check reads, so a genuinely-live, non-NULL `r->head`
        // that is either ABSENT from the table or found but marked `!live` (a chunk this runtime
        // itself already freed) is not a coverage gap — it is `r->head` no longer naming a real,
        // still-alive chunk at all: the smoking gun for a wild write that clobbered the HANDLE
        // (r->head itself, or whatever upstream variable fed it), not merely a chunk's header.
        if (!e->live) {
            tk_canary_tripped = 1;
            fprintf(stderr,
                "TEKO_NATIVE_CHUNK_CANARY: r->head %p at %s names a chunk this runtime ALREADY FREED "
                "(tk_canary_forget saw it) — r->head is dangling, not merely corrupted\n",
                (void *)c, where);
#ifdef TK_HAVE_BACKTRACE
            { void *fr[24]; int nf = backtrace(fr, 24); fprintf(stderr, "  backtrace at detection:\n"); backtrace_symbols_fd(fr, nf, 2); }
#endif
            fflush(stderr); abort();
        }
        if (c->cap != e->cap || c->next != e->next_snapshot) {
            tk_canary_tripped = 1;
            fprintf(stderr,
                "TEKO_NATIVE_CHUNK_CANARY: chunk %p header corrupted, first observed at %s\n"
                "  cap:  recorded=%zu now=%zu\n"
                "  next: recorded=%p now=%p\n"
                "  used(now)=%zu\n",
                (void *)c, where,
                e->cap, c->cap,
                (void *)e->next_snapshot, (void *)c->next,
                c->used);
#ifdef TK_HAVE_BACKTRACE
            void *fr[24]; int nf = backtrace(fr, 24);
            fprintf(stderr, "  backtrace at detection:\n");
            backtrace_symbols_fd(fr, nf, 2);
#endif
            fflush(stderr);
            abort();
        }
        return;
    }
    // Probe chain exhausted (an empty slot, `!used`) without ever finding `c`: this address was
    // NEVER born through tk_region_alloc's own chunk-creation branch, the sole birthplace — so
    // `r->head` is not a real chunk at all. Given the diagnostic is armed from process start (the
    // very first tk_region_alloc call evaluates the gate before any chunk exists), this is not a
    // recording gap; it is `r->head` itself holding garbage — the wild write clobbered the HANDLE,
    // not a chunk's header fields.
    tk_canary_tripped = 1;
    fprintf(stderr,
        "TEKO_NATIVE_CHUNK_CANARY: r->head %p at %s was NEVER recorded as a chunk this runtime "
        "created — the handle itself is garbage (a wild write clobbered r->head or whatever fed "
        "it), not a chunk header\n",
        (void *)c, where);
#ifdef TK_HAVE_BACKTRACE
    { void *fr[24]; int nf = backtrace(fr, 24); fprintf(stderr, "  backtrace at detection:\n"); backtrace_symbols_fd(fr, nf, 2); }
#endif
    fflush(stderr);
    abort();
}

void *tk_region_alloc(tk_region *r, size_t n) {
    tk_canary_check_head(r, "tk_region_alloc:entry");
    tk_canary_check_region(r, "tk_region_alloc:entry");
    if (n == 0) n = 1;                              // n→1: a zero-size alloc yields a distinct pointer
    // (S2 obs) SCOPED-lifetime side of the map — a direct allocation into a non-root region (class
    // objects, frame regions). The ROOT side is recorded in tk_alloc (whose RA0 is the REAL site;
    // recording here would blame everything on tk_alloc itself).
    if (tk_obs_on == 1 && r != tk_g_root) { tk_obs_scoped_bytes += n; tk_obs_add(tk_obs_scoped, __builtin_return_address(0), n); }
    size_t align = TK_ARENA_ALIGN;
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
    if (tk_canary_enabled()) tk_canary_record(c);   // snapshot the just-born chunk's {cap, next}
    return (char *)c->data;                          // base 0 is TK_ARENA_ALIGN-aligned (over-aligned flexible member)
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
    while (c != NULL) { struct tk_chunk *next = c->next; tk_chunk_free(c); c = next; }
    free(r->entries); r->entries = NULL; r->nentries = 0; r->entries_cap = 0;   // (S2) the per-region registry — a separate malloc'd array, not chunk-backed
    free(r);
}

// (#337) tk_region_drop_subtree(root) — the `adopt` bulk-drop. Drop `root` AND every live region
// whose ->parent ancestor chain reaches `root`, in a single sweep. Reuses the existing parent-ptr
// tree (remodel §1 L4 "owner-tag new; tree exists"): the "owner tag" is the ancestor-reaches-root
// predicate over the ->parent edges, so NO new field is needed. Cycles among the OBJECTS the
// subtree holds are irrelevant — nothing is freed per-object; the whole knot goes at once.
// NULL-tolerant. The registry is snapshotted into a local list of the reachable regions FIRST (so
// the walk is not disturbed by the per-region unlink), then each is freed off the local — the same
// re-entrancy discipline tk_regions_free_all uses. The ancestor walk is O(depth) per region, so the
// sweep is O(regions * depth); adopt subtrees are small, so this naive form is acceptable (a child
// list would make it linear but adds a field to every region — deferred until profiling demands it).
static int tk_region_ancestor_reaches(tk_region *r, tk_region *root) {
    for (tk_region *p = r; p != NULL; p = p->parent) {
        if (p == root) return 1;
    }
    return 0;
}

void tk_region_drop_subtree(tk_region *root) {
    if (root == NULL) return;                        // NULL-tolerant
    // Snapshot every reachable region into a detached local list threaded via its `reg_next` slot,
    // unlinking each from the global registry as it is collected — so a second sweep or
    // tk_regions_free_all never sees (and double-frees) a region this sweep owns.
    tk_region *doomed = NULL;
    tk_region *cur = tk_g_regs;
    while (cur != NULL) {
        tk_region *rnext = cur->reg_next;
        if (tk_region_ancestor_reaches(cur, root)) {
            if (tk_g_regs == cur) {
                tk_g_regs = cur->reg_next;
            } else {
                for (tk_region *p = tk_g_regs; p != NULL; p = p->reg_next) {
                    if (p->reg_next == cur) { p->reg_next = cur->reg_next; break; }
                }
            }
            cur->reg_next = doomed;                  // prepend onto the detached local list
            doomed = cur;
        }
        cur = rnext;
    }
    // Free each collected region off the detached local (chunks + entries + header), preserving the
    // arena-obs accounting tk_region_drop does — so TEKO_ARENA_OBS reports the whole subtree as
    // reclaimed, not leaked to the process root.
    while (doomed != NULL) {
        tk_region *dnext = doomed->reg_next;
        struct tk_chunk *c = doomed->head;
        doomed->head = NULL;
        if (tk_obs_on == 1) {
            tk_obs_regions_dropped += 1;
            for (struct tk_chunk *oc = c; oc != NULL; oc = oc->next) tk_obs_drop_bytes += oc->used;
        }
        while (c != NULL) { struct tk_chunk *next = c->next; tk_chunk_free(c); c = next; }
        free(doomed->entries); doomed->entries = NULL; doomed->nentries = 0; doomed->entries_cap = 0;
        free(doomed);
        doomed = dnext;
    }
}

// (W9.3b) free EVERY still-live region (root + every live scoped frame/block region) and empty the
// registry. Idempotent + re-entrancy-safe: it detaches the whole list into a local FIRST, then frees
// each off the local — so tk_region_drop's registry-unlink (which it calls indirectly? no — we free
// chunks directly here) and any re-entrant call both see an empty registry. We free chunks + headers
// directly (NOT via tk_region_drop) to avoid the O(n) per-region registry search on a list we already
// own end-to-end. A second call is a no-op (the registry is empty). Hooked at the termination choke
// points: tk_panic* (abort skips atexit), tk_exit, and the lazy atexit below (normal return / exit()).
// tk_registry_free — free every region on one registry list and empty it. The list is emptied
// BEFORE the walk so a re-entrant call (a panic during teardown) finds nothing left to free.
static void tk_registry_free(tk_region **registry) {
    tk_region *r = *registry;
    *registry = NULL;
    while (r != NULL) {
        tk_region *rnext = r->reg_next;
        struct tk_chunk *c = r->head;
        while (c != NULL) { struct tk_chunk *cnext = c->next; tk_chunk_free(c); c = cnext; }
        free(r->entries);   // (S2) the per-region registry — a separate malloc'd array, not chunk-backed
        free(r);
        r = rnext;
    }
}

// (F2) tk_g_program_regs / tk_g_program — the PROGRAM region and its registry, process-wide and on
// no task's list. See tk_region_program for why this exists.
static tk_region *tk_g_program_regs = NULL;
static tk_region *tk_g_program      = NULL;

// tk_canary_check_region — definition (prototype declared beside the chunk-canary ring, above).
// `r` is genuinely live iff it is on THIS task's region registry (`tk_g_regs`, walked via
// `reg_next` — the same list `tk_regions_free_all` trusts) OR it is the one process-wide PROGRAM
// region/registry (`tk_g_program_regs`, F2 — a separate list by design, never on any task's own).
// Anything else is a region handle this runtime never handed out as still alive: a dangling
// pointer left over after a drop, or outright garbage from a wild write that clobbered the
// variable HOLDING the handle (a LowerCtx's region_stack entry, a saved u64 region handle, …).
static void tk_canary_check_region(tk_region *r, const char *where) {
    if (!tk_canary_enabled() || tk_canary_tripped || r == NULL) return;
    for (tk_region *p = tk_g_regs; p != NULL; p = p->reg_next) { if (p == r) return; }
    for (tk_region *p = tk_g_program_regs; p != NULL; p = p->reg_next) { if (p == r) return; }
    tk_canary_tripped = 1;
    fprintf(stderr,
        "TEKO_NATIVE_CHUNK_CANARY: region %p passed to %s is NOT on any live region registry "
        "(dangling/garbage region handle)\n",
        (void *)r, where);
#ifdef TK_HAVE_BACKTRACE
    void *fr[24]; int nf = backtrace(fr, 24);
    fprintf(stderr, "  backtrace at detection:\n");
    backtrace_symbols_fd(fr, nf, 2);
#endif
    fflush(stderr);
    abort();
}

// __dso_handle — glibc's atexit() lives in libc_nonshared.a and takes the ADDRESS of a per-DSO handle
// to register process-exit finalizers through __cxa_atexit. The compiler's own crtbeginS.o normally
// DEFINES this symbol, but teko's DIRECT ld link (link_object_elf_direct, project.tks:2163 — no cc
// driver) deliberately omits crtbeginS.o, so on a strict x86_64-glibc toolchain (binutils 2.42) the
// hidden reference from atexit.oS is undefined and the FINAL link fails ("hidden symbol __dso_handle
// isn't defined"). Provide it WEAK + hidden from the maintained runtime: when a crtbeginS.o IS on the
// line (the cc-driver route, build_cc_argv) its STRONG definition wins; on the direct-ld route this
// satisfies the reference. The VALUE is irrelevant for a MAIN executable — atexit passes &__dso_handle
// and program-exit finalization sweeps the NULL-keyed handler list regardless. aarch64-glibc links
// clean (it never left the reference undefined), so this is inert there; macOS/Windows/musl are not
// __GLIBC__ and never compile it (avoiding any clash with the system ___dso_handle on Mach-O).
#if defined(__GLIBC__)
__attribute__((weak, visibility("hidden"))) void *__dso_handle = 0;
#endif

// tk_termination_hook_once — register the leak-clean atexit hook exactly once, whichever of the
// root region or the program region is materialized first. atexit fires on normal main return AND
// on libc exit() (tk_exit's path), so a NORMALLY-terminating program is leak-clean too;
// tk_regions_free_all is idempotent, so the explicit tk_exit/tk_panic calls never double-free.
static void tk_termination_hook_once(void) {
    static int registered = 0;
    if (registered) return;
    registered = 1;
    atexit(tk_regions_free_all);
}

// (F3) tk_names_forget — drop the name registry's view of the program region. Defined with the rest
// of the registry below; declared here because tk_regions_free_all frees the very chunks that table
// is bump-allocated in, and a table pointer that outlived them would be a dangling read.
static void tk_names_forget(void);

void tk_regions_free_all(void) {
    // (S2 obs) FINAL lifetime-map dump — this is every termination edge's choke point (atexit /
    // tk_exit / tk_panic), so an enabled run always ends with a complete map on disk.
    { static int obs_dumped = 0; if (!obs_dumped && tk_obs_on == 1) { obs_dumped = 1; tk_obs_dump(); } }
    tk_free_purge();                                 // (mem::free) parked blocks live inside chunks freed below
    tk_task *t = tk_task_current();
    t->root = NULL;                                  // the root is on the registry; it is freed below too
    tk_registry_free(&t->regs);
    tk_names_forget();                               // (F3) the name table is bump-allocated in the program region
    tk_registry_free(&tk_g_program_regs);            // (F2) the program region outlives tasks, but not the process
    tk_g_program = NULL;
}

tk_region *tk_region_root(void) {
    tk_task *t = tk_task_current();
    if (t->root == NULL) {
        t->root = tk_region_new_on(&t->regs, NULL);   // (S2) the tree root — no parent
        tk_termination_hook_once();
    }
    return t->root;
}

// (C1) tk_region_current — the region tk_alloc bump-allocates from RIGHT NOW: the top of this task's
// current-region stack, or the root when the stack is empty (the behaviour-identical default). A NULL
// slot or an over-deep pointer also falls through to the root, so an unbalanced/over-deep enter can
// never hand back garbage — it degrades to the root.
tk_region *tk_region_current(void) {
    tk_task *t = tk_task_current();
    if (t->cur_rsp > 0 && t->cur_rsp <= TK_REGION_STACK_MAX) {
        tk_region *r = t->cur_regions[t->cur_rsp - 1];
        if (r != NULL) return r;
    }
    return tk_region_root();
}

void tk_region_enter(tk_region *child) {
    if (tk_cur_rsp >= 0 && tk_cur_rsp < TK_REGION_STACK_MAX) tk_cur_regions[tk_cur_rsp] = child;
    tk_cur_rsp += 1;
}

void tk_region_leave(void) {
    if (tk_cur_rsp <= 0) return;
    tk_cur_rsp -= 1;
}

// (C1) the u64-handle ABI twins the Teko `extern fn` region surface binds to — see teko_rt.h. A
// tk_region* rides through Teko as a uintptr_t-wide u64; these cast at the boundary so the extern
// prototypes match without an int↔pointer conversion.
uint64_t tk_region_new_u(uint64_t parent) {
    return (uint64_t)(uintptr_t)tk_region_new((tk_region *)(uintptr_t)parent);
}

uint64_t tk_region_root_u(void) {
    return (uint64_t)(uintptr_t)tk_region_root();
}

// (0.3.1 native-memoria NP1) tk_region_current_u — tk_region_current() as a u64 handle. The native
// backend captures R_ret (the caller's chosen result region, == the current region at function
// entry) through this at fn entry, the native dialect of the C route's A1 (which de-static'd
// tk_region_current itself). Additive, behaviour-identical: no caller until the move flip (NP6).
uint64_t tk_region_current_u(void) {
    return (uint64_t)(uintptr_t)tk_region_current();
}

void tk_region_drop_u(uint64_t region) {
    tk_region_drop((tk_region *)(uintptr_t)region);
}

void tk_region_enter_u(uint64_t child) {
    tk_region_enter((tk_region *)(uintptr_t)child);
}

// ============================ (§16 crumb D) the arena-over-mmap P2 seam ===========================
//
// The Teko arena-over-mmap core (src/runtime/arena.tks) keeps ALL of its state INSIDE the raw memory
// it mmaps, read/written as words — solving the bootstrap circularity (§1). It needs exactly ONE
// mutable process word to reach that state: the address of its CONTROL block, which tk_alloc-shaped
// entry points (called by generated code that carries no state) must recover. Rather than add a
// module-mutable-word language surface, §1.3 reuses the maintained-C exception (the F1 tk_task seam
// family) for this one word. _Thread_local mirrors F1's per-task discipline: each flow of control's
// arena state is its own, exactly as tk_g_current_task already is. 0 until the arena's first-touch
// path installs it; ADD-ALONGSIDE (no current caller — the arena core is unwired at crumb D).
static _Thread_local uint64_t tk_g_arena_control = 0;

// tk_arena_control_get — read this task's arena CONTROL-block address (0 until first init). The one
// mutable process word P2 requires (docs/design/arena-em-teko.md §2), kept in the maintained-C seam
// so the Teko arena needs NO module-mutable-state language surface. Allocation-free.
uint64_t tk_arena_control_get(void) {
    return tk_g_arena_control;
}

// tk_arena_control_set — install this task's arena CONTROL-block address (once, lazily, by the Teko
// arena's first-touch path). The write half of the P2 seam. Allocation-free.
void tk_arena_control_set(uint64_t addr) {
    tk_g_arena_control = addr;
}

// tk_arena_paranoid — the cached TEKO_MEM_PARANOID probe, hoisted out of the C arena's free path
// (the tk_paranoid static at tk_free_block) so the Teko arena reads the same oracle without a Teko
// string / getenv-of-a-rodata-literal (§1.2 forbids a computed str on the arena path). Probed once,
// lazily; 1 when the variable is set to a non-empty value, else 0. Allocation-free.
uint64_t tk_arena_paranoid(void) {
    static int tk_arena_paranoid_cache = -1;
    if (tk_arena_paranoid_cache < 0) {
        const char *e = getenv("TEKO_MEM_PARANOID");
        tk_arena_paranoid_cache = (e != NULL && *e != '\0') ? 1 : 0;
    }
    return (uint64_t)tk_arena_paranoid_cache;
}

// tk_region_program — the PROGRAM region: one per process, owned by no task.
//
// (F2) F1 left the runtime with N task roots and nothing else, and an object allocated in a task
// root dies when that task's regions are freed. The owner's ruling that "every channel lives in
// the program arena or on the spine, it is a singleton" therefore has no seat after F1 unless one
// is built: a region that is NOT any task's root, so it survives both a task's tk_arena_pop (which
// only rewinds that task's root) and the task's exit (which only frees that task's registry).
// Freed at process termination by tk_regions_free_all, so it is still leak-clean.
// (E1-C4) The lazy init is double-checked under the obs spinlock so two lanes touching the program
// region for the first time at once cannot both create it (which would leak one region and hand the
// two lanes different "singletons"). The fast path — an already-built region — takes no lock at all,
// so the single-threaded reality of a process-shard leg pays one predicted pointer compare. Reusing
// the obs lock (never held across a program-region touch) keeps this to one process-wide primitive.
tk_region *tk_region_program(void) {
    if (tk_g_program != NULL) return tk_g_program;
    tk_spin_lock(&tk_g_spin_obs);
    if (tk_g_program == NULL) {
        tk_region *r = tk_region_new_on(&tk_g_program_regs, NULL);
        tk_termination_hook_once();
        tk_g_program = r;
    }
    tk_spin_unlock(&tk_g_spin_obs);
    return tk_g_program;
}

// ── (F3) THE NAME REGISTRY ────────────────────────────────────────────────────────────────────
// The contract, the packing and the reason a stale name is an error rather than undefined behaviour
// are in teko_rt.h next to the prototypes. What follows is only how it is built.
//
// A slot keeps its `resource` across a close ON PURPOSE: a recycled slot whose previous tenant was a
// cell reuses that cell's 16 bytes instead of abandoning them in the bump-allocated program region,
// so an open/close loop costs storage once per SLOT rather than once per open. Liveness therefore
// cannot be read off `resource`; `live` is the field that says it.
#define TK_NAMES_GEN_SHIFT   32u                       // the name's low half is the slot, the high half the stamp
#define TK_NAMES_SLOT_MASK   0xFFFFFFFFu               // …so a slot index is 32 bits wide
#define TK_NAMES_NO_SLOT     0xFFFFFFFFu               // free-list terminator, and therefore not a usable index
#define TK_NAMES_GEN_MAX     0xFFFFFFFFu               // a slot that would reach this stamp is RETIRED, never reissued
#define TK_NAMES_INITIAL_CAP 16u                       // 16 slots = 384 bytes, one doubling step from there
typedef struct {
    void    *resource;    // what the name stands for; retained across a close so a recycled slot can reuse storage
    uint32_t generation;  // the stamp a live name carries in its high half; bumped by every close
    uint32_t kind;        // the LAST kind stored here (never cleared — that is what makes storage reuse safe)
    uint32_t next_free;   // free-list link, or TK_NAMES_NO_SLOT
    uint32_t live;        // 1 while a resource is registered under the current stamp
} tk_name_slot;
_Static_assert(sizeof(tk_name_slot) <= 32, "a registry slot must stay small — the table is per-process");

static tk_name_slot *tk_g_names      = NULL;             // the slot array, bump-allocated in the program region
static uint32_t      tk_g_names_cap  = 0;                // slots the array can hold
static uint32_t      tk_g_names_used = 0;                // slots ever handed out (the array's high-water mark)
static uint32_t      tk_g_names_live = 0;                // slots live right now
static uint32_t      tk_g_names_free = TK_NAMES_NO_SLOT; // head of the LIFO free list of recyclable slots

// tk_names_forget — the table dies with the program region that holds it (tk_regions_free_all).
// Every previously issued name becomes UNKNOWN, which is a status and not a dangling pointer.
static void tk_names_forget(void) {
    tk_g_names = NULL; tk_g_names_cap = 0; tk_g_names_used = 0;
    tk_g_names_live = 0; tk_g_names_free = TK_NAMES_NO_SLOT;
}

// tk_names_grow — double the slot array inside the program region and copy the live prefix over.
// The bump allocator has no realloc, so the previous array is abandoned until process exit: the
// steady-state cost of reaching capacity C is C * 24 bytes live plus (C - 16) * 24 abandoned.
// 0 when the index space is exhausted (which is a refusal, never a wrap).
static int tk_names_grow(void) {
    uint64_t ncap = tk_g_names_cap == 0 ? (uint64_t)TK_NAMES_INITIAL_CAP : (uint64_t)tk_g_names_cap * 2u;
    if (ncap > (uint64_t)TK_NAMES_NO_SLOT) ncap = (uint64_t)TK_NAMES_NO_SLOT;
    if (ncap <= (uint64_t)tk_g_names_cap) return 0;
    tk_name_slot *grown = (tk_name_slot *)tk_region_alloc(tk_region_program(), (size_t)ncap * sizeof *grown);
    if (tk_g_names_used > 0) memcpy(grown, tk_g_names, (size_t)tk_g_names_used * sizeof *grown);
    tk_g_names = grown;
    tk_g_names_cap = (uint32_t)ncap;
    return 1;
}

// tk_names_take — reserve a slot and mint its name, WITHOUT touching `kind` or `resource`: the
// caller decides those, and the cell path needs the OLD kind to know whether the old storage is
// reusable. NULL (and *out_name unchanged) when no slot can be issued.
static tk_name_slot *tk_names_take(uint64_t *out_name) {
    uint32_t index;
    if (tk_g_names_free != TK_NAMES_NO_SLOT) {
        index = tk_g_names_free;
        tk_g_names_free = tk_g_names[index].next_free;
    } else {
        if (tk_g_names_used == tk_g_names_cap && !tk_names_grow()) return NULL;
        index = tk_g_names_used;
        tk_g_names_used += 1;
        tk_g_names[index].resource = NULL;
        tk_g_names[index].generation = 1;              // stamp 0 is never issued, so no valid name is ever 0
        tk_g_names[index].kind = 0;
    }
    tk_name_slot *s = &tk_g_names[index];
    s->next_free = TK_NAMES_NO_SLOT;
    s->live = 1;
    tk_g_names_live += 1;
    *out_name = ((uint64_t)s->generation << TK_NAMES_GEN_SHIFT) | (uint64_t)index;
    return s;
}

// tk_names_slot_at — the slot a name addresses, or NULL when the name does not address a live one.
// The three comparisons ARE the guarantee: a stamp above the slot's was never issued, a stamp below
// it belonged to a tenant that has been closed, and an equal stamp on a free slot is one that has
// not been handed out yet.
static tk_name_slot *tk_names_slot_at(uint64_t name, int64_t *status) {
    uint32_t index = (uint32_t)(name & (uint64_t)TK_NAMES_SLOT_MASK);
    uint32_t gen   = (uint32_t)(name >> TK_NAMES_GEN_SHIFT);
    if (gen == 0 || index >= tk_g_names_used) { *status = TK_NAMES_ERR_UNKNOWN; return NULL; }
    tk_name_slot *s = &tk_g_names[index];
    if (gen > s->generation) { *status = TK_NAMES_ERR_UNKNOWN; return NULL; }
    if (gen < s->generation) { *status = TK_NAMES_ERR_CLOSED;  return NULL; }
    if (s->live == 0)        { *status = TK_NAMES_ERR_UNKNOWN; return NULL; }
    *status = TK_NAMES_OK;
    return s;
}

int64_t tk_names_status(uint64_t name) {
    int64_t status = TK_NAMES_OK;
    tk_names_slot_at(name, &status);
    return status;
}


void *tk_names_lookup(uint64_t name, uint32_t kind) {
    int64_t status = TK_NAMES_OK;
    tk_name_slot *s = tk_names_slot_at(name, &status);
    if (s == NULL || s->kind != kind) return NULL;
    return s->resource;
}

int64_t tk_names_close(uint64_t name) {
    int64_t status = TK_NAMES_OK;
    tk_name_slot *s = tk_names_slot_at(name, &status);
    if (s == NULL) return status;
    s->live = 0;
    s->generation += 1;                                // every name ever issued for this slot is now strictly older
    tk_g_names_live -= 1;
    if (s->generation >= TK_NAMES_GEN_MAX) return TK_NAMES_OK;   // RETIRED: the stamp may not wrap, so the slot goes
    s->next_free = tk_g_names_free;
    tk_g_names_free = (uint32_t)(name & (uint64_t)TK_NAMES_SLOT_MASK);
    return TK_NAMES_OK;
}

uint64_t tk_names_live_count(void) { return (uint64_t)tk_g_names_live; }

uint64_t tk_names_capacity(void) { return (uint64_t)tk_g_names_cap; }

uint64_t tk_names_slot_of(uint64_t name) { return name & (uint64_t)TK_NAMES_SLOT_MASK; }

uint64_t tk_names_generation_of(uint64_t name) { return name >> TK_NAMES_GEN_SHIFT; }

uint64_t tk_names_cell_open(int64_t value) {
    if (value == TK_NAMES_NO_VALUE) return 0;          // the one payload the read sentinel cannot represent
    uint64_t name = 0;
    tk_name_slot *s = tk_names_take(&name);
    if (s == NULL) return 0;
    if (s->kind != TK_NAMES_KIND_CELL || s->resource == NULL) {
        s->resource = tk_region_alloc(tk_region_program(), sizeof(int64_t));
        s->kind = TK_NAMES_KIND_CELL;
    }
    *(int64_t *)s->resource = value;
    return name;
}

int64_t tk_names_cell_status(uint64_t name) {
    int64_t status = TK_NAMES_OK;
    tk_name_slot *s = tk_names_slot_at(name, &status);
    if (s == NULL) return status;
    if (s->kind != TK_NAMES_KIND_CELL) return TK_NAMES_ERR_KIND;
    return TK_NAMES_OK;
}

int64_t tk_names_cell_read(uint64_t name) {
    int64_t *cell = (int64_t *)tk_names_lookup(name, TK_NAMES_KIND_CELL);
    if (cell == NULL) return TK_NAMES_NO_VALUE;
    return *cell;
}

// tk_task_begin — create a fresh task and make it the one this flow of control runs on, returning
// the task that was current so tk_task_end can restore it. The new task starts with an EMPTY
// memory discipline: no root (the first allocation makes one), no marks, no parked blocks — which
// is precisely what makes its tk_arena_pop unable to reach another task's allocations.
tk_task *tk_task_begin(void) {
    tk_task *previous = tk_task_current();
    tk_task *fresh = calloc(1, sizeof *fresh);   // calloc: every family must start zeroed
    if (fresh == NULL) tk_panic("out of memory");
    tk_g_current_task = fresh;
    return previous;
}

// tk_task_end — free everything the current task allocated and hand this flow of control back to
// `previous`. The task's regions die with it; anything that must outlive the task belongs in the
// program region (tk_region_program). The main task is statically allocated, so it is never freed.
void tk_task_end(tk_task *previous) {
    tk_task *ending = tk_task_current();
    // Every witness the task holds names memory the registry free below is about to reclaim: parked
    // free-list blocks and live-tail push-cache entries both live INSIDE those chunks, and the marks
    // point at them. They are dropped FIRST, while `ending` is still the current task, so that a task
    // which is reused rather than freed (the main task) cannot false-hit on a dead region afterwards.
    tk_free_purge();
    tk_push_cache_purge();
    ending->arena_msp = 0;
    tk_registry_free(&ending->regs);
    ending->root = NULL;
    tk_g_current_task = previous;
    if (ending != &tk_g_main_task) free(ending);
}

#ifndef _WIN32
// ============================ (§10 C0a) tk_thread_spawn ============================================
//
// The whole §10 concurrency block (C0a thread-spawn, C0b memchan, C5 waitgroup) is pthread-backed and
// therefore POSIX-only: <pthread.h> is itself #ifndef _WIN32-guarded, and the Windows compiler build
// links no -pthread (see append_pthread_flag). Windows §10 concurrency is a later §16 native-lib task;
// until then these definitions are simply absent on Windows. Nothing in the single-threaded teko
// compiler calls them, so their absence is link-safe — codegen only emits tk_thread_spawn into USER
// programs, which are out of scope here.
//
// The spawned thread's _Thread_local tk_g_current_task is INDEPENDENT of the spawning thread's (one
// %fs-relative load per thread). tk_task_begin on the new thread starts from an EMPTY discipline (no
// root, no marks) so its tk_arena_pop/tk_task_end can NEVER reach another task's allocations. Args do
// not cross arenas because they were deep-copied into ctx_blob (libc heap, task-independent) by the
// spawning thread BEFORE hand-off; the target reads only the blob, never the spawning arena. The copy
// IS the isolation. pthread is the §16-TRANSITIONAL mechanism.

// tk_thread_call — what pthread's void* start-routine slot carries: the codegen trampoline `entry`
// and its ctx_blob, so ONE C start-routine serves every spawn-site. Malloc'd by the spawning thread,
// freed FIRST thing on the new thread.
typedef struct { void (*entry)(void *); void *ctx_blob; } tk_thread_call;

// tk_thread_start — the pthread start-routine. The arena bracket lives HERE, invisible to emitted
// code: tk_task_begin installs a FRESH sub-root arena for this thread, `entry(blob)` runs the
// deep-copied args + the target, tk_task_end frees the whole arena. The blob is freed by `entry`
// itself (the S3 trampoline); tk_thread_call is freed here.
static void *tk_thread_start(void *raw) {
    tk_thread_call call = *(tk_thread_call *)raw;
    free(raw);
    tk_task *saved = tk_task_begin();   // this thread's OWN sub-root arena (D1)
    call.entry(call.ctx_blob);          // trampoline: unpack blob -> target(args) -> free(blob)
    tk_task_end(saved);                 // the arena dies with the thread
    return NULL;
}

void tk_thread_spawn(void (*entry)(void *), void *ctx_blob) {
    tk_thread_call *call = malloc(sizeof *call);
    if (call == NULL) tk_panic("out of memory");
    call->entry = entry;
    call->ctx_blob = ctx_blob;
    pthread_t th;
    if (pthread_create(&th, NULL, tk_thread_start, call) != 0) tk_panic("spawn: cannot create thread");
    pthread_detach(th);                 // fire-and-forget: no join, no return, no handle (§10)
}

// tk_thread_handle — opaque, heap-boxed so a u64 can name it (AWAIT-BATCH twin).
typedef struct { pthread_t th; } *tk_thread_handle;





// ============================ (§10 C0b) tk_memchan ================================================
//
// A bounded/unbounded in-process MPSC FIFO of opaque fixed-width elements. The struct SHELL lives in
// the F2 program region (stable address, survives any producing task's tk_task_end); the ring buffer
// and pthread objects are heap/OS resources tk_memchan_end releases. pthread mutex + two conds is the
// right tool for a channel wait (milliseconds), not the atomic spinlock (microscopic critical
// sections); pthread is already linked for C0a.
struct tk_memchan {
    unsigned char *buf;        // ring: cap*elem_size bytes (malloc — freeable at end; NOT arena)
    uint64_t elem_size;
    uint64_t cap;              // slot count (== bounds for bounded; current capacity when unbounded)
    uint64_t head, tail, count;
    int closed;                // guarded by lock
    int unbounded;             // bounds == 0
    pthread_mutex_t lock;
    pthread_cond_t  not_empty; // recv waits here
    pthread_cond_t  not_full;  // bounded send waits here
};








// tk_memchan_selftest_arg / _producer — the producer half of the C0b selftest, reached through the
// joinable spawn twin so the caller has a deterministic finish edge. The blob is this arg; the
// producer frees it (tk_thread_start frees only the call record, never the blob).
typedef struct { tk_memchan *ch; int64_t n; } tk_memchan_selftest_arg;


// ============================ (§10 C5) tk_waitgroup ==============================================
//
// A manual counter with a blocking wait, F2-resident. add(n) before spawn is the race-free path;
// done() via the worker's handle; wait() on the creator's ctx. Same pthread idiom as tk_memchan.
struct tk_waitgroup { int64_t count; pthread_mutex_t lock; pthread_cond_t zero; };


// tk_waitgroup_selftest_arg / _worker — n workers each done() once after an add(n); wait must return
// only once every worker has signalled. Returns 0 when the barrier held.
typedef struct { tk_waitgroup *wg; } tk_waitgroup_selftest_arg;
#endif // _WIN32 — end of the pthread-backed §10 concurrency block (C0a/C0b/C5)

// ============================ (§10 C0c) tk_oschan ================================================
//
// An AF_UNIX SOCK_DGRAM MPSC transport (Linux-first — the abstract namespace and the socket ABI are
// Linux's). One reader socket bound to a key-derived abstract name; N writers each with a lazily
// opened, thread-local-cached socket. Datagram boundaries make each push atomic for free. Fixed-width
// elements only in v1.
#if !defined(_WIN32)
struct tk_oschan {
    int reader_fd;
    uint64_t elem_size;
    int bounded;                 // bounds != 0 => non-blocking send (observable EAGAIN back-pressure)
    struct sockaddr_un addr;     // the reader's bound abstract name
    socklen_t addrlen;
    int ended;
};



// A per-thread writer-socket cache keyed by the channel pointer: a writer never needs its own bound
// name to sendto the reader. Bounded to a handful of channels per thread; overflow opens an
// uncached socket (still correct, just not reused).
#define TK_OSCHAN_WCACHE 8
static _Thread_local struct { tk_oschan *ch; int fd; } tk_oschan_wcache[TK_OSCHAN_WCACHE];
static _Thread_local int tk_oschan_wcache_n = 0;







typedef struct { tk_oschan *ch; int64_t id; int64_t r; } tk_oschan_selftest_arg;
#else
#endif


void *tk_alloc(size_t n) {
    // (S1) Route through the process root region: bump-allocated, never dropped = today's
    // malloc-everywhere leak (M.5). OOM still panics (M.1, never NULL). Same contract as the
    // S0 malloc(n?n:1), only the bytes now come from a region chunk instead of libc directly.
    // (C1) The default allocation now bumps from the CURRENT region (the top of the per-task
    // current-region stack), which is the root when nothing was entered — behaviour-identical for a
    // program that never calls tk_region_enter. When a child is current, tk_region_alloc records the
    // SCOPED-lifetime obs; recording the ROOT obs here too would double-count, so the root-lifetime
    // histogram is only fed when the current region actually IS the root.
    tk_region *cur = tk_region_current();
    if (tk_obs_enabled() && cur == tk_task_current()->root) {   // (S2 obs) ROOT-lifetime side of the map, keyed by the REAL caller
        tk_obs_root_bytes += (n ? n : 1);
        tk_obs_add(tk_obs_root, __builtin_return_address(0), n ? n : 1);
        if (tk_obs_root_bytes > tk_obs_next_dump) { tk_obs_next_dump += 512ull * 1024 * 1024; tk_obs_dump(); }   // periodic (survives SIGKILL)
    }
    return tk_region_alloc(cur, n);
}

// (#109 test-gate memory) A per-scope CHECKPOINT/REWIND of the process root region's bump position.
// The root region is a LIFO chunk-list (tk_region_alloc PREPENDS new chunks), so a checkpoint =
// (head chunk, its used offset). Rewind frees every chunk PREPENDED after the checkpoint and resets
// the checkpoint chunk's bump offset — bulk-freeing everything the root region allocated in between.
//
// Used ONLY by the test-gate runner (run_tests_cov) to bound memory: each #test's transient
// allocations (env cells, list copies, string concats — the self-host compiler's copy-everything
// values) are freed after the test, so 659 tests no longer accumulate 9+ GB. SOUND
// because run_tests_cov is compiled C (its loop state lives on the C stack, NOT in the arena) and the
// coverage sinks are libc-heap (realloc/malloc above) — so nothing referenced after a test lives in
// the rewound span. Balanced push/pop (depth ~1); a stack over the fixed cap is counted but not saved
// (pop then no-ops), keeping push/pop balanced without ever rewinding past a recorded mark.
void tk_arena_push(void) {
    if (tk_arena_msp >= 0 && tk_arena_msp < TK_ARENA_MARK_MAX) {
        tk_region *r = tk_region_root();
        tk_arena_marks[tk_arena_msp].chunk = r->head;
        tk_arena_marks[tk_arena_msp].used  = r->head ? r->head->used : 0;
    }
    tk_arena_msp += 1;
}

void tk_arena_pop(void) {
    if (tk_arena_msp <= 0) return;
    tk_arena_msp -= 1;
    if (tk_arena_msp >= TK_ARENA_MARK_MAX) return;   // an over-deep push saved nothing — do not rewind
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
        tk_chunk_free(c); c = next;                           // free chunks newer than the mark
    }
    r->head = m.chunk;
    if (m.chunk != NULL) {
        if (tk_obs_on == 1 && m.chunk->used > m.used) tk_obs_rewind_bytes += m.chunk->used - m.used;   // partial-chunk rewind
        m.chunk->used = m.used;
    }
}

// (enabling primitive — staged off; no compiler source calls this yet) tk_arena_commit — the
// Boundary-A counterpart to tk_arena_pop: discard the top mark WITHOUT rewinding, so every
// allocation made since the matching tk_arena_push stays live, folded into the (now-current) mark
// below it. Mirrors tk_arena_pop's depth bookkeeping exactly (an over-deep push that saved no mark
// commits just as cheaply as it would have rewound) but touches neither chunks nor the free-list —
// nothing here was ever freed, so there is nothing to purge.
void tk_arena_commit(void) {
    if (tk_arena_msp <= 0) return;
    tk_arena_msp -= 1;
}

// --- the PER-TEST CHANNEL ---------------------------------------------------------------------
//
// See teko_rt.h for WHY this exists. The short version, measured on this tree: a `#test` that
// prints anything of its own pushed the harness's closing `ok` onto the NEXT line, so a suite count
// anchored on `... ok` under-reported the run and a failing test's report quoted a NEIGHBOUR's
// bytes. The fix is not tidier printing — it is that a test never writes to the shared stream at
// all while it runs.
//
// TWO BUFFERS, NOT ONE. stdout and stderr are different claims (what the program produced vs. what
// it complained about) and a report that merges them destroys the distinction the streams exist to
// make; they are emitted under different prefixes for that reason.
//
// THE ORDER IS VERDICT-FIRST, deliberately, over interleaving-with-a-prefix. Interleaving keeps
// chronology but leaves the verdict line's POSITION dependent on how much the test printed — which
// is exactly the property that broke counting. Verdict-first makes `test <label> ... ok` one ATOMIC
// line whose shape no test body can perturb, and the captured bytes that follow are unambiguously
// that test's because nothing else can come between them.
//
// The channel is per-task (E1-C1: `tk_chan` and the capture members moved into `tk_task`), not a
// stack: tests do not nest, and a captured print must reach the running test's OWN channel, never a
// neighbour's — which per-task storage guarantees even when lanes run concurrently.

// tk_chan_append — append n bytes to a channel, growing geometrically. A failed growth DROPS the
// bytes instead of aborting: losing a diagnostic line is bad, killing the run that would have
// reported it is worse.
static void tk_chan_append(tk_chan *c, const void *p, size_t n) {
    if (n == 0) return;
    if (c->len + n > c->cap) {
        size_t want = c->cap ? c->cap : TK_TEST_CHAN_MIN_CAP;
        while (want < c->len + n) want *= 2;
        char *grown = (char *)realloc(c->buf, want);
        if (!grown) return;
        c->buf = grown;
        c->cap = want;
    }
    memcpy(c->buf + c->len, p, n);
    c->len += n;
}

// tk_chan_emit_prefixed — write a captured buffer to dst as whole LINES, each opened by prefix, so
// every line is attributed to the test whose verdict line precedes it. An unterminated last line
// gets its newline here: a report made of half-lines cannot be read by line-oriented tooling.
static void tk_chan_emit_prefixed(FILE *dst, const tk_chan *c, const char *prefix) {
    size_t i = 0;
    while (i < c->len) {
        size_t j = i;
        while (j < c->len && c->buf[j] != '\n') j += 1;
        fputs(prefix, dst);
        fwrite(c->buf + i, 1, j - i, dst);
        fputc('\n', dst);
        i = (j < c->len) ? j + 1 : j;
    }
}

// tk_chan_emit_body — both captured streams of the closing channel: stdout first (what the test
// produced), then stderr (what it complained about).
static void tk_chan_emit_body(void) {
    tk_chan_emit_prefixed(stdout, &tk_chan_out, TK_TEST_OUT_PREFIX);
    tk_chan_emit_prefixed(stdout, &tk_chan_err, TK_TEST_ERR_PREFIX);
}

// tk_chan_verdict_line — `test <label> ... <verdict>` on the REAL stdout, then a flush, so the test's
// name is on the wire before anything (including an abort three lines later) can lose it.
static void tk_chan_verdict_line(const char *verdict) {
    fputs("test ", stdout);
    fwrite(tk_chan_label, 1, tk_chan_label_len, stdout);
    fputs(" ... ", stdout);
    fputs(verdict, stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

void tk_test_begin(tk_str label) {
    tk_task_reset_transient();   // (E1-C3) each `#test` enters on a clean transient slate — the reshuffle can expose no residue
    tk_chan_out.len = 0;
    tk_chan_err.len = 0;
    tk_chan_label_len = label.len < sizeof tk_chan_label ? label.len : sizeof tk_chan_label - 1;
    if (tk_chan_label_len) memcpy(tk_chan_label, label.ptr, tk_chan_label_len);
    tk_chan_open = true;
}


// tk_test_close_failed — close the open channel with a FAILING verdict, then this test's OWN captured
// bytes. Closing the channel FIRST is what lets anything written afterwards reach the real streams.
static void tk_test_close_failed(const char *verdict) {
    if (!tk_chan_open) return;
    tk_chan_open = false;
    tk_chan_verdict_line(verdict);
    tk_chan_emit_body();
    fflush(stdout);
}

// tk_test_fail_report — the FAILING half of tk_test_end, called from the UNCAPTURED panic path before
// it aborts (a program, or a test binary whose panic escaped the capture).
static void tk_test_fail_report(void) { tk_test_close_failed("FAILED"); }

// --- the CAPTURE (see teko_rt.h for the ruling and the three results) -----------------------------
//
// A STACK AND NOT A SINGLE BUFFER, and the reason is the guard. The channel is a singleton because
// harness-level tests do not nest, and that is still true; but the ONLY way a `#test` written in
// Teko can exercise this mechanism today is to run a captured body from INSIDE a captured body (the
// language cannot yet hand a function's address to an extern — see tk_test_capture_probe). A single
// buffer would have the inner run clear the outer's capture on the way out, so the outer test's own
// later panic would jump into a frame that no longer exists.
//
// The depth is what the two chokepoints below read. It is raised ONLY by tk_test_run, which is
// emitted ONLY by the test-mode profiles — so a program can never take the captured branch, and the
// boundary is structural instead of a flag anyone can flip by mistake.
//
// (F1) The capture stack lives on the TASK — TK_TEST_CAPTURE_DEPTH_MAX and the buffers moved into
// tk_task, because a longjmp must land in a frame of the SAME flow of control that set it up.

// tk_test_capturing — is a captured body running right now? The one question the chokepoints ask.
static bool tk_test_capturing(void) { return tk_test_depth > 0; }

// TK_TEST_VERDICT_MAX — the failing verdict buffer, sized for "FAILED (exit -2147483648)" and slack.
#define TK_TEST_VERDICT_MAX 64

// (E1-C1) tk_test_ran/passed/failed/exited are per-task members (see the seam): the tally a lane
// accumulates is its own, and the parent sums lanes after the barrier (§3 of the axis design).

// tk_test_capture_leave — record this level's outcome, pop the stack, and hand control back to the
// tk_test_run that owns the innermost frame. Popping BEFORE the jump is what makes the outer level
// (if any) the one a subsequent panic reaches.
_Noreturn static void tk_test_capture_leave(int32_t how, int32_t code) {
    int d = (int)tk_test_depth - 1;
    tk_test_how[d] = how;
    tk_test_code[d] = code;
    tk_test_depth = d;
    longjmp(tk_test_jb[d], 1);
}

// tk_test_capture_panic — the captured half of the panic chokepoints: the panic line goes into THIS
// test's own captured stderr (never the shared stream, which is what used to attribute an abort to a
// test ~66 tests before the real one), then control returns to tk_test_run.
_Noreturn static void tk_test_capture_panic(const char *msg, size_t len) {
    tk_chan_append(&tk_chan_err, TK_PANIC_MARKER, strlen(TK_PANIC_MARKER));
    tk_chan_append(&tk_chan_err, msg, len);
    tk_chan_append(&tk_chan_err, "\n", 1);
    tk_test_capture_leave(TK_TEST_PANICKED, 0);
}











// --- the SHARD filter ---------------------------------------------------------------------------
//
// Parallelism for an in-process suite cannot be threads: the arena, the coverage sinks and this very
// channel are process-wide singletons, and making them thread-local would be a far larger change
// than the parallelism is worth. It is PROCESSES — the driver runs the SAME test binary N times,
// each with `TEKO_TEST_SHARD=<i>/<n>`, and each process runs only the tests whose ORDINAL is
// congruent to i mod n. Round-robin, not contiguous blocks, because per-test cost is uneven and a
// contiguous split puts the whole expensive tail in one shard.
//
// The ordinal is counted HERE, so it is the same sequence in every shard regardless of which tests
// each one actually ran — the property that makes the union of the shards exactly the suite.
static long tk_shard_index = -1;
static long tk_shard_count = 0;
static long tk_shard_seen  = 0;

// tk_shard_parse — read TK_TEST_SHARD_ENV once, as `<i>/<n>` with 0 <= i < n. Anything else (unset,
// empty, non-numeric, out of range) selects NO sharding, which is the safe reading: a mistyped value
// must run the whole suite, never silently skip most of it.
static void tk_shard_parse(void) {
    tk_shard_count = 0;
    tk_shard_index = 0;
    const char *v = getenv(TK_TEST_SHARD_ENV);
    if (!v || !*v) return;
    char *end = NULL;
    long i = strtol(v, &end, 10);
    if (!end || *end != '/') return;
    long n = strtol(end + 1, &end, 10);
    if (!end || *end != '\0') return;
    if (n <= 1 || i < 0 || i >= n) return;
    tk_shard_index = i;
    tk_shard_count = n;
}


// --- the TEST SCOPE (see teko_rt.h for WHY the discipline is isolation, uniformly) ----------------
//
// tk_scope_byte_ok — the bytes a scope token may carry. Deliberately narrow: the token becomes part
// of a FILE NAME, so anything a path, a shell or a Windows filesystem reads specially is out. Every
// qualified test label carries `::`, which is therefore rewritten rather than passed through.
static bool tk_scope_byte_ok(char c) {
    if (c >= 'a' && c <= 'z') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '0' && c <= '9') return true;
    return c == '_' || c == '-';
}

// (E1-C1) tk_scope_buf/tk_scope_len are per-task members (see the seam): each test's derived scope
// token is its own, so two lanes deriving paths at once never share the buffer.

tk_str tk_test_scope(void) {
    if (!tk_chan_open) return (tk_str){ (const tk_byte *)"", 0 };
    if (tk_shard_index < 0) tk_shard_parse();
    int head = snprintf(tk_scope_buf, sizeof tk_scope_buf, ".s%ld-", tk_shard_count > 1 ? tk_shard_index : 0L);
    size_t n = head > 0 ? (size_t)head : 0;
    size_t i = 0;
    while (i < tk_chan_label_len && n + 1 < sizeof tk_scope_buf) {
        char c = tk_chan_label[i];
        tk_scope_buf[n] = tk_scope_byte_ok(c) ? c : '_';
        n += 1;
        i += 1;
    }
    tk_scope_len = n;
    return (tk_str){ (const tk_byte *)tk_scope_buf, tk_scope_len };
}

// --- the SCENARIO NAME ---------------------------------------------------------------------------
//
// See teko_rt.h for WHY a case is addressed by name and not by the exit code its failure produces.
// The name lives HERE rather than in the assert seed because the thing it is written onto is the
// per-test channel above: `<name>: ok` has to land on the running test's own channel, and the panic
// prefix has to survive the seed being replaced by the corpus's own strong definitions.
// (E1-C1) tk_scen_name/len/prefix are per-task members (see the seam): the scenario a lane names is
// written onto that lane's channel, so the label must travel with the task, not a shared singleton.

void tk_assert_scenario_set(tk_str name) {
    tk_scen_len = name.len < sizeof tk_scen_name ? name.len : sizeof tk_scen_name - 1;
    if (tk_scen_len) memcpy(tk_scen_name, name.ptr, tk_scen_len);
    memcpy(tk_scen_prefix, tk_scen_name, tk_scen_len);
    tk_scen_prefix[tk_scen_len]     = ':';
    tk_scen_prefix[tk_scen_len + 1] = ' ';
}

tk_str tk_assert_scenario_prefix(void) {
    if (tk_scen_len == 0) return (tk_str){ (const tk_byte *)"", 0 };
    return (tk_str){ (const tk_byte *)tk_scen_prefix, tk_scen_len + 2 };
}

void tk_assert_scenario_ok(void) {
    if (tk_scen_len == 0) tk_panic("assertion failed: scenario_ok — no scenario is named (scenario_begin was never called)");
    const size_t pre = sizeof TK_SCENARIO_OK_PREFIX - 1;
    char line[TK_TEST_LABEL_MAX + sizeof TK_SCENARIO_OK_PREFIX + 4];
    memcpy(line, TK_SCENARIO_OK_PREFIX, pre);
    memcpy(line + pre, tk_scen_name, tk_scen_len);
    memcpy(line + pre + tk_scen_len, ": ok", 4);
    tk_println((tk_str){ (const tk_byte *)line, pre + tk_scen_len + 4 });
    tk_scen_len = 0;
}

void tk_print(tk_str s) {
    // Exactly s.len bytes; tolerate embedded NUL; no strlen/puts.
    if (tk_chan_open) { tk_chan_append(&tk_chan_out, s.ptr, s.len); return; }
    fwrite(s.ptr, 1, s.len, stdout);
}

void tk_println(tk_str s) {
    if (tk_chan_open) { tk_chan_append(&tk_chan_out, s.ptr, s.len); tk_chan_append(&tk_chan_out, "\n", 1); return; }
    tk_print(s);
    fputc('\n', stdout);   // single 0x0A
}


// Host output FFI bottoms (scope.c: write/ewrite/eprint/eprintln) — exactly s.len bytes, tolerate
// embedded NUL. write → stdout; ewrite/eprint → stderr; eprintln → stderr + '\n'.
void tk_write(tk_str s)    { if (tk_chan_open) { tk_chan_append(&tk_chan_out, s.ptr, s.len); return; } fwrite(s.ptr, 1, s.len, stdout); }
void tk_ewrite(tk_str s)   { if (tk_chan_open) { tk_chan_append(&tk_chan_err, s.ptr, s.len); return; } fwrite(s.ptr, 1, s.len, stderr); }
void tk_eprint(tk_str s)   { if (tk_chan_open) { tk_chan_append(&tk_chan_err, s.ptr, s.len); return; } fwrite(s.ptr, 1, s.len, stderr); }
void tk_eprintln(tk_str s) { if (tk_chan_open) { tk_chan_append(&tk_chan_err, s.ptr, s.len); tk_chan_append(&tk_chan_err, "\n", 1); return; } fwrite(s.ptr, 1, s.len, stderr); fputc('\n', stderr); }

// teko::float::parse(str) -> f64 — strtod over a NUL-terminated copy (s may contain no NUL and is
// not NUL-terminated). A non-numeric / empty string yields 0.0 (strtod's no-conversion result).
double tk_float_parse(tk_str s) {
    char *buf = (char *)tk_alloc(s.len + 1);
    if (s.len) memcpy(buf, s.ptr, s.len);
    buf[s.len] = '\0';
    double v = strtod(buf, NULL);
    return v;
}

uint64_t tk_rt_float_parse_bits(tk_str s) {
    union { double f; uint64_t u; } bits;
    bits.f = tk_float_parse(s);
    return bits.u;
}

_Noreturn void tk_panic(const char *msg) {
    // Under CAPTURE (test mode only) this stops the TEST, not the process: the line lands in this
    // test's own channel and control returns to tk_test_run. `_Noreturn` stays honest — longjmp
    // does not return either.
    if (tk_test_capturing()) tk_test_capture_panic(msg, strlen(msg));
    // Loud + non-zero (M.1): the TK_PANIC_MARKER line first, then SIGABRT via abort().
    // The per-test channel is closed and REPORTED first, so the reader sees which test died and
    // what THAT test had written before the panic message rather than a neighbour's bytes.
    tk_test_fail_report();
    fputs(TK_PANIC_MARKER, stderr);
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
    if (tk_test_capturing()) tk_test_capture_panic((const char *)msg.ptr, msg.len);
    tk_test_fail_report();
    fputs(TK_PANIC_MARKER, stderr);
    fwrite(msg.ptr, 1, msg.len, stderr);
    fputc('\n', stderr);
    tk_backtrace();   // (C1.9) show the call stack
    tk_regions_free_all();   // (W9.3b) abort() skips atexit — free the arena regions explicitly first
    abort();
}

_Noreturn void tk_panic_div0(void)     { tk_panic("division by zero"); }
_Noreturn void tk_panic_overflow(void) { tk_panic("integer overflow"); }

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
// `str` return needs no lift at all (mirrors tk_rt_version's already-working shape).
// (E1-C1) Per-task member (see the seam beside the F1 families).

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

// tk_rt_read_stdin — slurp stdin until EOF, a bare tk_str (like tk_rt_read_line/tk_rt_version —
// no error union, so the call lowers through the fully GENERIC extern path with no per-name
// codegen dispatch; a `str | error` shape would need a NEW emit_host_ffi entry, which the
// CURRENTLY RELEASED seed's codegen cannot lower — see #229). stdin is not seekable (unlike
// read_file's fseek/ftell sizing), so this grows a plain malloc'd scratch buffer by doubling,
// then makes ONE tk_alloc + memcpy into the arena-owned result (mirrors tk_str_of_cstr). A
// genuine read failure is exceedingly rare for a host frontier primitive with no recoverable-
// error channel (mirrors read_line()/version()) — panic (M.1 fail-loud) rather than truncating.
tk_str tk_rt_read_stdin(void) {
    size_t cap = 64 * 1024;
    size_t len = 0;
    char *scratch = (char *)malloc(cap);
    if (scratch == NULL) tk_panic("cannot allocate stdin buffer");
    for (;;) {
        if (len == cap) {
            cap = cap * 2;
            char *grown = (char *)realloc(scratch, cap);
            if (grown == NULL) { free(scratch); tk_panic("cannot grow stdin buffer"); }
            scratch = grown;
        }
        size_t got = fread(scratch + len, 1, cap - len, stdin);
        len += got;
        if (got == 0) break;
    }
    if (ferror(stdin)) { free(scratch); tk_panic("cannot read stdin"); }
    tk_byte *buf = (tk_byte *)tk_alloc(len ? len : 1);
    if (len) memcpy(buf, scratch, len);
    free(scratch);
    return (tk_str){ buf, len };
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

// (0.3.1.0) tk_rt_append_file(path, content) — see teko_rt.h for the contract and for why this is a
// primitive rather than a read-modify-write. RAW `open`+`write` and not stdio "ab": the POSIX
// append guarantee is a property of a `write` to an `O_APPEND` DESCRIPTOR, and stdio is free to
// split one `fwrite` across several of them, which would give up exactly the atomicity this exists
// to gain. The loop below finishes a PARTIAL write — necessary for correctness, and the one case
// that is no longer a single atomic act (teko_rt.h says so rather than pretending otherwise).
int32_t tk_rt_append_file(tk_str path, tk_str content) {
    char *p = tk_cstr(path);
#ifdef _WIN32
    int fd = _open(p, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
    if (fd < 0) return TK_RT_APPEND_FAILED;
    size_t done = 0;
    int32_t status = 0;
    while (done < content.len) {
#ifdef _WIN32
        int put = _write(fd, content.ptr + done, (unsigned int)(content.len - done));
#else
        ssize_t put = write(fd, content.ptr + done, content.len - done);
#endif
        if (put <= 0) { status = TK_RT_APPEND_FAILED; break; }
        done += (size_t)put;
    }
#ifdef _WIN32
    if (_close(fd) != 0) status = TK_RT_APPEND_FAILED;
#else
    if (close(fd) != 0) status = TK_RT_APPEND_FAILED;
#endif
    return status;
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

// tk_sort_names — byte-lexicographic insertion sort over an owned tk_str array.
//
// EXISTS TO MAKE THE COMPILER DETERMINISTIC, not for tidiness. `readdir` returns entries in an
// order the filesystem chooses; ext4, overlayfs, APFS and tmpfs all disagree. `discover.tks`
// walks exactly that order and never sorts, so the order in which a project's sources are
// discovered — and therefore the order declarations are collected and types resolved — was a
// property of the machine, not of the tree.
//
// It stayed invisible while a regression project held one or two files. Folding the corpus into
// nine projects took `qualified_optional` to 43, and it surfaced immediately: the musl lane
// failed with `q089_iface_value_hetero_slice/body.tks:57:8: array element type mismatch` on a
// tree that compiled clean on every other lane and locally.
//
// The stake is larger than that red. gen2 == gen3 and the `nightly === gen1` reproducibility gate
// both assert that the same tree yields the same bytes; a machine-dependent discovery order makes
// that false by construction, and it would have failed as "non-reproducible build" with no
// visible cause. Sorting at this boundary fixes it for EVERY caller of list_dir at once, which is
// why it lives here and not in the one walker that happened to expose it.
//
// Insertion sort: directory sizes here are tens of entries, and a simple algorithm with no
// allocation is worth more than an asymptotic win nothing will ever notice.
static void tk_sort_names(tk_str *a, size_t n) {
    for (size_t i = 1; i < n; i += 1) {
        tk_str key = a[i];
        size_t j = i;
        while (j > 0) {
            tk_str prev = a[j - 1];
            size_t m = prev.len < key.len ? prev.len : key.len;
            int c = 0;
            if (m > 0) c = memcmp(prev.ptr, key.ptr, m);
            if (c == 0) c = prev.len < key.len ? -1 : (prev.len > key.len ? 1 : 0);
            if (c <= 0) break;
            a[j] = prev;
            j -= 1;
        }
        a[j] = key;
    }
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
    tk_sort_names(out, n);
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

bool tk_rt_last_index_of_ok(tk_str hay, tk_str needle, uint64_t *out_index) {
    tk_ffi_u64res r = tk_rt_last_index_of(hay, needle);
    if (r.ok) { *out_index = r.value; }
    return r.ok;
}

// --- native-backend own-register twins for the aggregate-returning host FFI (0.3.1.0 degrau 34) ---
// Each wraps the real (struct-returning) primitive above and re-shapes its result into the
// single-register `bool` + shared (ptr, len) out-parameter convention the native backend's `LCall`
// can capture — no host logic is duplicated (the SUPREME RULE seam: signature/wiring only). See
// teko_rt.h for why the direct SRET/rax:rdx path faults on that backend.

// tk_ffi_sres_into_out — unpack an sres into the shared (ptr, len) out-slots: the value on success,
// the error message on failure, the caller's `bool` deciding which meaning it wrote.
static bool tk_ffi_sres_into_out(tk_ffi_sres r, const tk_byte **out_ptr, uint64_t *out_len) {
    if (r.ok) {
        *out_ptr = r.value.ptr;
        *out_len = r.value.len;
        return true;
    }
    *out_ptr = r.err.ptr;
    *out_len = r.err.len;
    return false;
}

// tk_ffi_ures_into_out — the `error | null` twin's shared tail: `true` (no error) leaves the
// out-slots untouched; on failure the error message's (ptr, len) travel through them.
static bool tk_ffi_ures_into_out(tk_ffi_ures r, const tk_byte **out_err_ptr, uint64_t *out_err_len) {
    if (r.ok) return true;
    *out_err_ptr = r.err.ptr;
    *out_err_len = r.err.len;
    return false;
}

bool tk_rt_getenv_ok(const tk_byte *name_ptr, uint64_t name_len, const tk_byte **out_ptr, uint64_t *out_len) {
    return tk_ffi_sres_into_out(tk_rt_getenv((tk_str){ name_ptr, (size_t)name_len }), out_ptr, out_len);
}

bool tk_rt_getcwd_ok(const tk_byte **out_ptr, uint64_t *out_len) {
    return tk_ffi_sres_into_out(tk_rt_getcwd(), out_ptr, out_len);
}


bool tk_rt_list_dir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_ptr, uint64_t *out_len) {
    tk_ffi_slres r = tk_rt_list_dir((tk_str){ path_ptr, (size_t)path_len });
    if (r.ok) {
        *out_ptr = (const tk_byte *)r.ptr;
        *out_len = r.len;
        return true;
    }
    *out_ptr = r.err.ptr;
    *out_len = r.err.len;
    return false;
}

bool tk_rt_setenv_ok(const tk_byte *name_ptr, uint64_t name_len, const tk_byte *value_ptr, uint64_t value_len, const tk_byte **out_err_ptr, uint64_t *out_err_len) {
    return tk_ffi_ures_into_out(tk_rt_setenv((tk_str){ name_ptr, (size_t)name_len }, (tk_str){ value_ptr, (size_t)value_len }), out_err_ptr, out_err_len);
}

bool tk_rt_chdir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len) {
    return tk_ffi_ures_into_out(tk_rt_chdir((tk_str){ path_ptr, (size_t)path_len }), out_err_ptr, out_err_len);
}

bool tk_rt_mkdir_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len) {
    return tk_ffi_ures_into_out(tk_rt_mkdir((tk_str){ path_ptr, (size_t)path_len }), out_err_ptr, out_err_len);
}

bool tk_rt_remove_file_ok(const tk_byte *path_ptr, uint64_t path_len, const tk_byte **out_err_ptr, uint64_t *out_err_len) {
    return tk_ffi_ures_into_out(tk_rt_remove_file((tk_str){ path_ptr, (size_t)path_len }), out_err_ptr, out_err_len);
}



// (romaneio .31) TK_RT_SIGNAL_EXIT_BASE — the shell convention for "died by signal N": 128 + N.
// A child killed by SIGABRT (6) therefore reports 134, exactly what `sh -c` reports for the same
// child, so an expected exit code is the same whether the program is run directly through
// teko::process::run or behind a shell.
#define TK_RT_SIGNAL_EXIT_BASE 128

// (romaneio .31) tk_rt_wait_status_code — the exit code a waited-for POSIX child reports.
//
// M.3 FIX: this used to be `WIFEXITED ? WEXITSTATUS : 127`, so EVERY death by signal collapsed onto
// 127 — the same value execvp-failed (`_exit(127)`) and fork-failed already return. teko::process::run
// could not distinguish "the child aborted" from "I could not start the child", and a panicking
// child (SIGABRT) surfaced as a spawn failure. A signalled child now reports 128 + signal.
//
// What stays ambiguous, honestly: a child that CHOSE to exit 127 is indistinguishable from a failed
// execvp, because POSIX gives the exec'd-image failure no other channel. That is the convention's
// own limit, not a lost distinction.
//
// SECOND M.3 FIX (vagão 20): every remaining PARENT-side "I could not run it" now reports
// TK_RT_SPAWN_FAILED (teko_rt.h) instead of 127. The `test / windows` lane of PR #94 failed with
// `exit 127, expected 134` on a scenario whose captured stderr held a real panic — i.e. the child
// demonstrably ran — and 127 could not say whether that came from a failed `_spawnvp`, from `sh`
// reporting command-not-found, or from a child exiting 127 itself. A sentinel that cannot be
// confused with a child's own status is what makes the next run answerable.
//
// NOT YET NORMALIZED — the Windows half. `tk_win32_spawnvp` returns `_spawnvp`'s value, which for a
// child killed by an abort/exception is a CRT/NTSTATUS-shaped code, not 128+signal; what it actually
// is can only be OBSERVED on a Windows runner, and this file will not guess it.
#ifndef _WIN32
static int32_t tk_rt_wait_status_code(int status) {
    if (WIFEXITED(status)) return (int32_t)WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return (int32_t)(TK_RT_SIGNAL_EXIT_BASE + WTERMSIG(status));
    // Neither exited nor signalled: waitpid returned a status this code cannot interpret, which is
    // a failure to OBSERVE the child, not an exit status the child produced.
    return TK_RT_SPAWN_FAILED;
}
#endif

int32_t tk_rt_run(const tk_str *argv, uint64_t n) {
    if (n == 0) return TK_RT_SPAWN_FAILED;
    // Build a NUL-terminated argv (each arg NUL-terminated; the vector NULL-terminated).
    char **cargv = (char **)tk_alloc((n + 1) * sizeof *cargv);
    for (uint64_t i = 0; i < n; i += 1) cargv[i] = tk_cstr(argv[i]);
    cargv[n] = NULL;
#ifdef _WIN32
    // _spawnvp(_P_WAIT) is synchronous: blocks until the child exits, returns its exit code.
    int w = tk_win32_spawnvp(cargv[0], cargv);
    return (w == -1) ? TK_RT_SPAWN_FAILED : (int32_t)w;
#else
    pid_t pid = fork();
    if (pid < 0) return TK_RT_SPAWN_FAILED;
    if (pid == 0) {                      // child: exec; on failure exit 127 (POSIX convention —
        execvp(cargv[0], cargv);         // the exec'd image has no other channel to report through)
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return TK_RT_SPAWN_FAILED;
    return tk_rt_wait_status_code(status);
#endif
}

// (issue #73) teko::process::run_quiet(args) — same contract as tk_rt_run, but the child's
// stdout/stderr are redirected to the null device. Used by the build backend's cc flag-family
// probe (compiling a throwaway empty translation unit to test whether the host cc accepts
// clang-only flags) so a deliberately-rejected flag doesn't leak an "unrecognized option" line
// into the user's build output. [teko::process]
int32_t tk_rt_run_quiet(const tk_str *argv, uint64_t n) {
    if (n == 0) return TK_RT_SPAWN_FAILED;
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
    return (w == -1) ? TK_RT_SPAWN_FAILED : (int32_t)w;
#else
    pid_t pid = fork();
    if (pid < 0) return TK_RT_SPAWN_FAILED;
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
    if (waitpid(pid, &status, 0) < 0) return TK_RT_SPAWN_FAILED;
    return tk_rt_wait_status_code(status);
#endif
}

// tk_rt_next_nul_token — the next NUL-delimited token in `s`, starting at `*pos` (a VIEW into `s`,
// never copied); `*pos` advances past the token, and past the delimiting NUL when there is one (the
// final token in a payload has none, and is bounded by `s.len` instead). The one splitting step
// every field of tk_rt_spawn_redirected's payload shares (teko_rt.h: TOKEN_SEP/spawn_payload).
static tk_str tk_rt_next_nul_token(tk_str s, size_t *pos) {
    size_t start = *pos;
    size_t i = start;
    while (i < s.len && s.ptr[i] != '\0') i += 1;
    tk_str tok = { s.ptr + start, i - start };
    *pos = (i < s.len) ? i + 1 : i;
    return tok;
}

// tk_rt_token_u64 — the decimal value of a NUL-delimited token: the `<argc>`/`<envc>` element-count
// prefixes tk_rt_spawn_redirected's payload is built from (process.tks: spawn_payload).
static uint64_t tk_rt_token_u64(tk_str tok) {
    uint64_t v = 0;
    for (size_t i = 0; i < tok.len; i += 1) {
        if (tok.ptr[i] < '0' || tok.ptr[i] > '9') break;
        v = (v * 10) + (uint64_t)(tok.ptr[i] - '0');
    }
    return v;
}

// tk_rt_read_nul_vector — read a COUNT-prefixed run of `n` NUL-delimited tokens starting at `*pos`
// into a fresh NULL-terminated `char **` vector (the shape `execvp`/`putenv` want). `n` was already
// read by the caller (the `<argc>`/`<envc>` prefix) — this only consumes the `n` tokens after it.
static char **tk_rt_read_nul_vector(tk_str s, size_t *pos, uint64_t n) {
    char **out = (char **)tk_alloc((n + 1) * sizeof *out);
    for (uint64_t i = 0; i < n; i += 1) out[i] = tk_cstr(tk_rt_next_nul_token(s, pos));
    out[n] = NULL;
    return out;
}

// (0.3.1.0 F5) tk_rt_token_fd — the DESCRIPTOR a payload's `<in_fd>`/`<out_fd>`/`<err_fd>` token
// names, or TK_RT_FD_NONE when the token is empty (the slot names no descriptor) or holds anything
// that is not a run of decimal digits. Deliberately NOT tk_rt_token_u64: that one reads an empty
// token as 0, and 0 is a real descriptor (standard input) — the one value an "absent" token must
// never decode to.
static int tk_rt_token_fd(tk_str tok) {
    if (tok.len == 0) return TK_RT_FD_NONE;
    int v = 0;
    for (size_t i = 0; i < tok.len; i += 1) {
        if (tok.ptr[i] < '0' || tok.ptr[i] > '9') return TK_RT_FD_NONE;
        v = (v * 10) + (int)(tok.ptr[i] - '0');
    }
    return v;
}

#ifndef _WIN32
// (0.3.1.0 F5) tk_rt_redirect — one child stream's descriptor plus WHO OWNS IT. `owned` is true
// only for a descriptor tk_rt_open_redirect opened from a path: the parent closes exactly those and
// leaves a caller-supplied one alone, which is the whole ownership rule teko_rt.h states.
typedef struct { int fd; bool owned; } tk_rt_redirect;

// (0.3.1.0 F5) tk_rt_open_redirect — the descriptor one redirection slot resolves to. A descriptor
// the caller passed (`given`) wins outright and is never opened, never closed here; otherwise a
// non-empty `path` is opened HERE, in the parent, before the fork — so a relative path resolves
// against the PARENT's working directory, never the child's `dir`. An empty slot yields
// TK_RT_FD_NONE, i.e. "the child inherits the parent's own stream".
static tk_rt_redirect tk_rt_open_redirect(tk_str path, int given, int flags) {
    if (given >= 0) return (tk_rt_redirect){ given, false };
    if (path.len == 0) return (tk_rt_redirect){ TK_RT_FD_NONE, false };
    char *p = tk_cstr(path);
    return (tk_rt_redirect){ open(p, flags, 0644), true };
}

// (0.3.1.0 F5) tk_rt_child_lift — the same descriptor, guaranteed to sit ABOVE the standard range.
// A caller-supplied descriptor may legitimately BE 0/1/2, and binding the three slots in order
// would then overwrite a later slot's source with an earlier slot's dup2 before it is ever read.
// Lifting every source clear of 0..2 first makes the three binds independent of their order.
static tk_rt_redirect tk_rt_child_lift(tk_rt_redirect r) {
    if (r.fd < 0 || r.fd > STDERR_FILENO) return r;
    int hi = fcntl(r.fd, F_DUPFD, STDERR_FILENO + 1);
    if (hi < 0) return r;
    return (tk_rt_redirect){ hi, true };
}

// (0.3.1.0 F5) tk_rt_child_drop — close one lifted source descriptor in the child, unless it is a
// standard stream (which is the redirection's own result) or a descriptor an earlier drop already
// closed. THE DUPLICATE GUARD IS NOT DEFENSIVE PADDING: one pipe write end bound to BOTH stdout and
// stderr is the ordinary way to merge a child's two streams into one, and closing it after the
// first bind would make the second `dup2` operate on a closed descriptor.
static void tk_rt_child_drop(int fd, int already_a, int already_b) {
    if (fd <= STDERR_FILENO) return;
    if (fd == already_a || fd == already_b) return;
    close(fd);
}

// (0.3.1.0 F5) tk_rt_child_bind_all — bind the three resolved descriptors onto the child's own
// standard streams, in the child, after the fork: lift every source clear of 0..2, dup2 all three,
// then drop each distinct source. Split from `tk_rt_spawn_redirected` so the ordering rules the two
// helpers above encode live in one place instead of inline in the fork's child arm.
static void tk_rt_child_bind_all(tk_rt_redirect in_r, tk_rt_redirect out_r, tk_rt_redirect err_r) {
    tk_rt_redirect a = tk_rt_child_lift(in_r);
    tk_rt_redirect b = tk_rt_child_lift(out_r);
    tk_rt_redirect c = tk_rt_child_lift(err_r);
    if (a.fd >= 0) dup2(a.fd, STDIN_FILENO);
    if (b.fd >= 0) dup2(b.fd, STDOUT_FILENO);
    if (c.fd >= 0) dup2(c.fd, STDERR_FILENO);
    if (a.fd >= 0) tk_rt_child_drop(a.fd, TK_RT_FD_NONE, TK_RT_FD_NONE);
    if (b.fd >= 0) tk_rt_child_drop(b.fd, a.fd, TK_RT_FD_NONE);
    if (c.fd >= 0) tk_rt_child_drop(c.fd, a.fd, b.fd);
}

// (0.3.1.0 F5) tk_rt_parent_release — drop the PARENT's copy of a redirection descriptor, and only
// when the parent opened it. A caller-supplied descriptor survives untouched; closing the caller's
// write end is the caller's own `tk_rt_close_fd`, because that close is what makes the read end see
// end-of-file and the caller may want to hand the same write end to a second child first.
static void tk_rt_parent_release(tk_rt_redirect r) {
    if (r.owned && r.fd >= 0) close(r.fd);
}
#endif

// (0.3.1.2 — process-half regression harness; 0.3.1.0 F5 — descriptor slots) tk_rt_spawn_redirected
// (payload) — see teko_rt.h for the contract and `payload`'s self-describing shape. POSIX resolves
// the three redirection slots in the PARENT (a caller's descriptor as-is, else a path opened here),
// forks, and has the CHILD dup2 them onto its own stdin/stdout/stderr before `chdir`+`execvp`; the
// parent releases only the descriptors IT opened, on every path, success or failure, so a later
// reader of `.out`/`.err` sees EOF once the child is done with them.
int64_t tk_rt_spawn_redirected(tk_str payload) {
    size_t pos = 0;
    uint64_t argv_n = tk_rt_token_u64(tk_rt_next_nul_token(payload, &pos));
    if (argv_n == 0) return TK_RT_SPAWN_FAILED;
    char **cargv = tk_rt_read_nul_vector(payload, &pos, argv_n);
    uint64_t env_n = tk_rt_token_u64(tk_rt_next_nul_token(payload, &pos));
    char **cenv = tk_rt_read_nul_vector(payload, &pos, env_n);
    tk_str dir      = tk_rt_next_nul_token(payload, &pos);
    tk_str in_path  = tk_rt_next_nul_token(payload, &pos);
    tk_str out_path = tk_rt_next_nul_token(payload, &pos);
    tk_str err_path = tk_rt_next_nul_token(payload, &pos);
    int in_given  = tk_rt_token_fd(tk_rt_next_nul_token(payload, &pos));
    int out_given = tk_rt_token_fd(tk_rt_next_nul_token(payload, &pos));
    int err_given = tk_rt_token_fd(tk_rt_next_nul_token(payload, &pos));
#ifdef _WIN32
    char *cdir = dir.len ? tk_cstr(dir) : NULL;
    char *cin  = in_path.len  ? tk_cstr(in_path)  : NULL;
    char *cout = out_path.len ? tk_cstr(out_path) : NULL;
    char *cerr = err_path.len ? tk_cstr(err_path) : NULL;
    return tk_win32_spawn_redirected(cargv, cdir, cenv, (size_t)env_n, cin, cout, cerr,
                                     in_given, out_given, err_given);
#else
    tk_rt_redirect in_r  = tk_rt_open_redirect(in_path,  in_given,  O_RDONLY);
    tk_rt_redirect out_r = tk_rt_open_redirect(out_path, out_given, O_WRONLY | O_CREAT | O_TRUNC);
    tk_rt_redirect err_r = tk_rt_open_redirect(err_path, err_given, O_WRONLY | O_CREAT | O_TRUNC);
    bool dir_is_here = dir.len == 0 || (dir.len == 1 && dir.ptr[0] == '.');
    pid_t pid = fork();
    if (pid < 0) {
        tk_rt_parent_release(in_r);
        tk_rt_parent_release(out_r);
        tk_rt_parent_release(err_r);
        return TK_RT_SPAWN_FAILED;
    }
    if (pid == 0) {
        tk_rt_child_bind_all(in_r, out_r, err_r);
        for (uint64_t i = 0; i < env_n; i += 1) putenv(cenv[i]);
        if (!dir_is_here) {
            char *d = tk_cstr(dir);
            if (chdir(d) != 0) _exit(127);
        }
        execvp(cargv[0], cargv);
        _exit(127);
    }
    tk_rt_parent_release(in_r);
    tk_rt_parent_release(out_r);
    tk_rt_parent_release(err_r);
    return (int64_t)pid;
#endif
}

// (0.3.1.2) tk_rt_wait_one(raw) — see teko_rt.h for the contract. POSIX reaps the pid `raw` names
// through the same `tk_rt_wait_status_code` reading `tk_rt_run` uses, so a signal-killed child
// reports 128+signo identically whether it was launched through `run` or `spawn_redirected`.
int32_t tk_rt_wait_one(int64_t raw) {
    if (raw < 0) return TK_RT_SPAWN_FAILED;
#ifdef _WIN32
    return tk_win32_wait_one(raw);
#else
    pid_t pid = (pid_t)raw;
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return TK_RT_SPAWN_FAILED;
    return tk_rt_wait_status_code(status);
#endif
}

// =========================================================================
// (0.3.1.0 F5) ANONYMOUS PIPES. See teko_rt.h for the full contract of each entry point below —
// the packing, the ownership rule, and the Windows asymmetry with its measured number.
// =========================================================================

// TK_RT_FD_HALF_BITS — how far the WRITE end is shifted in `tk_rt_pipe`'s packing. 32, because a
// descriptor is an `int` on both hosts, so two of them fit an i64 exactly with no value lost and no
// range to check.
#define TK_RT_FD_HALF_BITS 32
// TK_RT_FD_HALF_MASK — the low half a packed pair's READ end occupies.
#define TK_RT_FD_HALF_MASK ((int64_t)0xFFFFFFFF)

// tk_rt_pack_pipe — the two fresh descriptors as one non-negative i64 (see teko_rt.h for why one
// register and not two out-parameters). Both halves are known >= 0 here: the caller only reaches
// this after the host's pipe call reported success.
static int64_t tk_rt_pack_pipe(int read_fd, int write_fd) {
    return (((int64_t)write_fd) << TK_RT_FD_HALF_BITS) | (((int64_t)read_fd) & TK_RT_FD_HALF_MASK);
}

int64_t tk_rt_pipe(void) {
    int fds[2];
#ifdef _WIN32
    // _pipe, not CreatePipe: this returns CRT DESCRIPTORS, the same currency the POSIX arm and the
    // whole Teko surface speak, where CreatePipe would hand back HANDLEs that no other entry point
    // here accepts. _O_NOINHERIT keeps an unrelated CreateProcess from cloning either end; the one
    // child that IS meant to have it gets an explicitly inheritable duplicate
    // (tk_win32_redirect_handle_of_fd). _O_BINARY so a byte written is the byte read — text mode
    // would rewrite "\n" into "\r\n" mid-pipe and make a captured stream differ by host.
    if (_pipe(fds, TK_RT_PIPE_CAPACITY, _O_BINARY | _O_NOINHERIT) != 0) return TK_RT_FD_NONE;
#else
    if (pipe(fds) != 0) return TK_RT_FD_NONE;
    // FD_CLOEXEC rather than pipe2(O_CLOEXEC): pipe2 is a Linux extension macOS does not have, and
    // this pair of fcntl calls is the portable spelling of the same guarantee. `dup2` clears the
    // flag on its TARGET, so the child that is deliberately given an end through
    // tk_rt_spawn_redirected still keeps it across the exec.
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    return tk_rt_pack_pipe(fds[0], fds[1]);
}



int32_t tk_rt_close_fd(int64_t fd) {
    if (fd < 0) return TK_RT_FD_NONE;
#ifdef _WIN32
    return (_close((int)fd) == 0) ? 0 : TK_RT_FD_NONE;
#else
    return (close((int)fd) == 0) ? 0 : TK_RT_FD_NONE;
#endif
}

#ifdef _WIN32
// tk_rt_win_peek_ready — one non-blocking reading of a Windows anonymous pipe: READY when bytes are
// pending OR the write end is gone (ERROR_BROKEN_PIPE, which is end-of-file and must wake a reader
// exactly as pending bytes do), TIMEOUT when neither yet, ERROR when the descriptor names no pipe.
// The single step tk_rt_fd_wait_readable's polling loop repeats.
static int32_t tk_rt_win_peek_ready(int64_t fd) {
    HANDLE h = (HANDLE)_get_osfhandle((int)fd);
    if (h == INVALID_HANDLE_VALUE) return TK_RT_FD_WAIT_ERROR;
    DWORD avail = 0;
    if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL)) {
        return (GetLastError() == ERROR_BROKEN_PIPE) ? TK_RT_FD_WAIT_READY : TK_RT_FD_WAIT_ERROR;
    }
    return (avail > 0) ? TK_RT_FD_WAIT_READY : TK_RT_FD_WAIT_TIMEOUT;
}
#endif

int32_t tk_rt_fd_wait_readable(int64_t fd, int64_t timeout_ms) {
    if (fd < 0) return TK_RT_FD_WAIT_ERROR;
#ifdef _WIN32
    // The POLLING arm, and its cost is stated in teko_rt.h rather than hidden: MSVC has no
    // blocking-with-deadline wait for an anonymous pipe, so this asks every
    // TK_RT_PIPE_POLL_INTERVAL_MS until the deadline. UNVERIFIED on a real Windows host.
    int64_t waited = 0;
    for (;;) {
        int32_t peek = tk_rt_win_peek_ready(fd);
        if (peek != TK_RT_FD_WAIT_TIMEOUT) return peek;
        if (timeout_ms >= 0 && waited >= timeout_ms) return TK_RT_FD_WAIT_TIMEOUT;
        Sleep(TK_RT_PIPE_POLL_INTERVAL_MS);
        waited += TK_RT_PIPE_POLL_INTERVAL_MS;
    }
#else
    struct pollfd pfd;
    pfd.fd = (int)fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int timeout = (timeout_ms < 0) ? -1 : (int)timeout_ms;
    int r = poll(&pfd, 1, timeout);
    if (r < 0) return TK_RT_FD_WAIT_ERROR;
    if (r == 0) return TK_RT_FD_WAIT_TIMEOUT;
    // POLLHUP counts as READY, not as an error: it is how `poll` announces that the last write end
    // closed, i.e. the end-of-file the reader is waiting for. Reporting it as an error would turn a
    // child's clean exit into a failure.
    if ((pfd.revents & (POLLIN | POLLHUP)) != 0) return TK_RT_FD_WAIT_READY;
    return TK_RT_FD_WAIT_ERROR;
#endif
}

// tk_rt_fd_stage — the ONE staging buffer tk_rt_fd_fill writes and tk_rt_fd_take_byte drains. A
// single process-wide slot, exactly as teko_rt.h states: a fill discards whatever the previous one
// left, so one drain must finish before the next begins. Static rather than arena-allocated so a
// fill costs no allocation at all — the whole point of staging is that the per-byte call underneath
// it never touches the host.
// (E1-C1) tk_rt_fd_stage/staged/taken are per-task members (see the seam): the staging slot is one
// per task, so a lane draining a pipe never sees bytes a different lane's fill left.

int64_t tk_rt_fd_fill(int64_t fd, int64_t timeout_ms) {
    tk_rt_fd_staged = 0;
    tk_rt_fd_taken = 0;
    if (fd < 0) return TK_RT_FD_FILL_ERROR;
    int32_t ready = tk_rt_fd_wait_readable(fd, timeout_ms);
    if (ready == TK_RT_FD_WAIT_TIMEOUT) return TK_RT_FD_FILL_TIMEOUT;
    if (ready != TK_RT_FD_WAIT_READY) return TK_RT_FD_FILL_ERROR;
#ifdef _WIN32
    int got = _read((int)fd, tk_rt_fd_stage, (unsigned int)sizeof tk_rt_fd_stage);
#else
    ssize_t got = read((int)fd, tk_rt_fd_stage, sizeof tk_rt_fd_stage);
#endif
    if (got < 0) return TK_RT_FD_FILL_ERROR;
    tk_rt_fd_staged = (size_t)got;
    return (int64_t)got;
}

int32_t tk_rt_fd_take_byte(void) {
    if (tk_rt_fd_taken >= tk_rt_fd_staged) return -1;
    int32_t b = (int32_t)tk_rt_fd_stage[tk_rt_fd_taken];
    tk_rt_fd_taken += 1;
    return b;
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


// (E1-C6) tk_rt_nproc — the number of processors the OS grants THIS process right now (online CPUs),
// at least 1. The OS-granted count is what the test/regression job pools default to and clamp their
// env override against, so the parallelism a machine may use is the machine's business and never a
// source literal. POSIX reads sysconf; Windows reads NUMBER_OF_PROCESSORS (no extra header, unlike
// GetSystemInfo). An unavailable or nonsensical count degrades to 1 — a serial run is always valid.
uint64_t tk_rt_nproc(void) {
#if defined(_WIN32)
    const char *e = getenv("NUMBER_OF_PROCESSORS");
    long n = e != NULL ? strtol(e, NULL, 10) : 0;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
    return n > 0 ? (uint64_t)n : 1;
}

// (#194 C6) teko::crypto::rand::secure_bytes(n) — n bytes from the host CSPRNG. Every
// platform bottom fills a caller-owned buffer in fixed-size (or single) chunks and PANICS
// (M.1) on a genuine host entropy failure — a CSPRNG that silently degrades to weak/short
// output is a security defect, never a soft `error` here. n == 0 allocates a 1-byte buffer
// (never NULL) but reports len == 0, mirroring tk_str_slice's empty-slice convention.
tk_slice_byte tk_rt_secure_bytes(uint64_t n) {
    tk_byte *out = (tk_byte *)tk_alloc(n == 0 ? 1 : n);
#if defined(_WIN32)
    // rand_s (ucrt, RtlGenRandom-backed) is a CSPRNG in the CRT — needs NO import lib, unlike
    // BCryptGenRandom's bcrypt.lib which the self-host link cannot resolve for a runtime symbol the
    // compiler's own corpus never calls. Fills 32 bits per rand_s call.
    uint64_t filled = 0;
    while (filled < n) {
        unsigned int v;
        if (rand_s(&v) != 0) tk_panic("teko::crypto::rand::secure_bytes: rand_s failed");
        uint64_t chunk = n - filled;
        if (chunk > 4) chunk = 4;
        for (uint64_t k = 0; k < chunk; k += 1) out[filled + k] = (tk_byte)((v >> (8 * k)) & 0xFFu);
        filled += chunk;
    }
#elif defined(__APPLE__)
    // getentropy(2) caps a single call at 256 bytes and errors past that — chunk the fill.
    uint64_t filled = 0;
    while (filled < n) {
        uint64_t chunk = n - filled;
        if (chunk > 256) chunk = 256;
        if (getentropy(out + filled, (size_t)chunk) != 0) {
            tk_panic("teko::crypto::rand::secure_bytes: getentropy failed");
        }
        filled += chunk;
    }
#else
    // getrandom(2) (Linux, glibc/musl) may return fewer bytes than requested (a signal
    // interruption or a short read from the blocking-entropy-pool edge) — loop to fill.
    uint64_t filled = 0;
    while (filled < n) {
        ssize_t got = getrandom(out + filled, (size_t)(n - filled), 0);
        if (got < 0) tk_panic("teko::crypto::rand::secure_bytes: getrandom failed");
        filled += (uint64_t)got;
    }
#endif
    return (tk_slice_byte){ out, n };
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

const tk_byte *tk_rt_version_len(uint64_t *out_len) {
    tk_str r = tk_rt_version();
    *out_len = r.len;
    return r.ptr;
}

// tk_rt_read_line_len / tk_rt_read_stdin_len / tk_assert_scenario_prefix_len / tk_test_scope_len —
// the out-parameter-length twins for the remaining plain-tk_str-returning host primitives reached
// through an `extern fn` on the native backend (0.3.1.0 degrau 35). Each defers to its by-value
// primitive and re-shapes the fat return into (return ptr, *out_len) so the length survives the
// backend's single-result-register call convention. No logic is duplicated.
const tk_byte *tk_rt_read_line_len(uint64_t *out_len) {
    tk_str r = tk_rt_read_line();
    *out_len = r.len;
    return r.ptr;
}

const tk_byte *tk_rt_read_stdin_len(uint64_t *out_len) {
    tk_str r = tk_rt_read_stdin();
    *out_len = r.len;
    return r.ptr;
}

const tk_byte *tk_assert_scenario_prefix_len(uint64_t *out_len) {
    tk_str r = tk_assert_scenario_prefix();
    *out_len = r.len;
    return r.ptr;
}

const tk_byte *tk_test_scope_len(uint64_t *out_len) {
    tk_str r = tk_test_scope();
    *out_len = r.len;
    return r.ptr;
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
// (F1) TK_PUSH_HASH_SIZE and the cache itself moved into tk_task — the witnesses name regions, and
// a region belongs to exactly one task. The generation stays process-global so the (region, gen)
// pair a witness records can never be minted twice across tasks.
static inline unsigned tk_push_slot(const void *p) {
    return (unsigned)((((uintptr_t)p >> 4) * 11400714819323198485ull) >> 48) & (TK_PUSH_HASH_SIZE - 1);
}
// (#148 safety) drop EVERY live-tail witness — called by tk_arena_pop before a rewind recycles root
// addresses (region+gen can't distinguish a recycled address WITHIN the same region).
static void tk_push_cache_purge(void) {
    tk_task *t = tk_task_current();
    memset(t->push_cache, 0, sizeof t->push_cache);
}

// (2026-08-03, native-region-crash, push-redzone) TEKO_NATIVE_PUSH_REDZONE — a CI-narrowing aid,
// same env-gated pattern as the other TEKO_NATIVE_* diagnostics: byte-identical when unset. The
// chunk-header canary (TEKO_NATIVE_CHUNK_CANARY) exhaustively ruled out a corrupted chunk header
// or a dangling region handle across three deterministic repros; TEKO_MEM_PARANOID=1 (which never
// reuses a parked block, only poisons it) did NOT eliminate the crash either — it just moved the
// corruption into glibc's OWN malloc metadata (`malloc.c:4196 assertion failed: chunk_main_arena`),
// proof this is a genuine OUT-OF-BOUNDS WRITE, not merely a premature free-old reuse. The victim
// observed live (gdb, deterministic, ASLR off) was a still-PARKED freenode whose memory held
// readable ASCII text ("declare"...) — a []byte/[]T growable buffer this runtime's OWN push-cache
// tracks as a live tail is being written PAST the length it reports.
//
// This places a small REDZONE (TK_REDZONE_BYTES of a fixed magic pattern) in the SLACK capacity
// right after every push's just-written element (whenever slack exists), and verifies it is still
// intact on the very NEXT push to the SAME live tail — found via the EXISTING push-cache witness,
// no new tracking table. A stomped redzone means something wrote INTO that buffer's spare capacity
// BETWEEN two pushes, bypassing tk_slice_push_r/tk_append_bytes_fo entirely (an aliased write
// through some OTHER name/reference to the same buffer, or a native store with the wrong esz/width
// computing an address past this buffer's own bounds).
#define TK_REDZONE_BYTES 16
#define TK_REDZONE_BYTE  0xC5

static int tk_redzone_enabled(void) {
    static int v = -1;
    if (v < 0) { const char *e = getenv("TEKO_NATIVE_PUSH_REDZONE"); v = (e != NULL && e[0] == '1') ? 1 : 0; }
    return v;
}

// tk_redzone_place — write up to TK_REDZONE_BYTES of the magic pattern into the SLACK just past
// `used` elements of `esz` bytes each, bounded by `cap` (never claims more than genuinely exists).
static void tk_redzone_place(void *ptr, uint64_t used, uint64_t esz, uint64_t cap) {
    if (!tk_redzone_enabled() || ptr == NULL || cap <= used) return;
    uint64_t slack_bytes = (cap - used) * esz;
    uint64_t rz = slack_bytes < TK_REDZONE_BYTES ? slack_bytes : TK_REDZONE_BYTES;
    if (rz == 0) return;
    memset((char *)ptr + used * esz, TK_REDZONE_BYTE, (size_t)rz);
}

// tk_redzone_verify — check a PREVIOUSLY placed redzone is still intact, right before this push
// trusts `ptr`/`used` again. A miss (no redzone to check) is NOT proof of safety, only an absence
// of coverage; a hit is definitive proof of a wild write into this exact buffer's slack.
static void tk_redzone_verify(const void *ptr, uint64_t used, uint64_t esz, uint64_t cap, const char *where) {
    if (!tk_redzone_enabled() || ptr == NULL || cap <= used) return;
    uint64_t slack_bytes = (cap - used) * esz;
    uint64_t rz = slack_bytes < TK_REDZONE_BYTES ? slack_bytes : TK_REDZONE_BYTES;
    if (rz == 0) return;
    const unsigned char *p = (const unsigned char *)ptr + used * esz;
    for (uint64_t i = 0; i < rz; i += 1) {
        if (p[i] == (unsigned char)TK_REDZONE_BYTE) continue;
        fprintf(stderr,
            "TEKO_NATIVE_PUSH_REDZONE: buffer %p redzone stomped, detected at %s\n"
            "  used=%llu cap=%llu esz=%llu byte[%llu]=0x%02x (expected 0x%02x)\n"
            "  bytes at breach (up to 32): ",
            ptr, where,
            (unsigned long long)used, (unsigned long long)cap, (unsigned long long)esz,
            (unsigned long long)i, p[i], TK_REDZONE_BYTE);
        uint64_t show = rz < 32 ? rz : 32;
        for (uint64_t j = 0; j < show; j += 1) fprintf(stderr, "%02x ", p[j]);
        fputc('\n', stderr);
        fprintf(stderr, "  as text: \"");
        for (uint64_t j = 0; j < show; j += 1) fputc((p[j] >= 32 && p[j] < 127) ? p[j] : '.', stderr);
        fprintf(stderr, "\"\n");
#ifdef TK_HAVE_BACKTRACE
        { void *fr[24]; int nf = backtrace(fr, 24); fprintf(stderr, "  backtrace at detection:\n"); backtrace_symbols_fd(fr, nf, 2); }
#endif
        fflush(stderr);
        abort();
    }
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
    // (C1 hardening) follow the CURRENT region like tk_alloc does, so a buffer grown inside a scratch
    // window is cache-tagged with the CHILD's (region, gen) — not root — and tk_region_drop retires it
    // via the same generation guard the whole fix relies on. cur == root when no window is open, so
    // this is byte-for-byte the old behaviour for every existing caller (cb runs only at root scope).
    tk_region *cur = tk_region_current();
    if (ptr != NULL) {
        unsigned h0 = tk_push_slot(ptr);
        // (chunk-canary/push-redzone bug, self-caught) same STALE-SLOT SAFETY as
        // tk_slice_push_r's own redzone check — see its comment.
        if (tk_push_cache[h0].ptr == ptr && tk_push_cache[h0].esz == 1
            && tk_push_cache[h0].region == cur && tk_push_cache[h0].region_gen == cur->gen) {
            tk_redzone_verify(ptr, tk_push_cache[h0].len, 1, tk_push_cache[h0].cap, "tk_append_bytes_fo:entry");
        }
    }
    if (ptr != NULL) {   // in-place: the live tail with enough spare capacity
        unsigned h = tk_push_slot(ptr);
        if (tk_push_cache[h].ptr == ptr && tk_push_cache[h].len == len && tk_push_cache[h].esz == 1
            && tk_push_cache[h].region == cur && tk_push_cache[h].region_gen == cur->gen
            && len + n <= tk_push_cache[h].cap) {
            memcpy((char *)ptr + len, src, n);
            tk_push_cache[h].len = len + n;
            *out_len = len + n;
            tk_redzone_place((void *)ptr, len + n, 1, tk_push_cache[h].cap);
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
        tk_push_cache[hb].region = cur; tk_push_cache[hb].region_gen = cur->gen;
    }
    tk_redzone_place(buf, len + n, 1, cap);
    if (ptr != NULL) tk_free_block((void *)ptr, old_bytes);   // the DECREE: the old buffer is dead
    *out_len = len + n;
    return buf;
}

// (0.3.1.0 degrau 4 — NATIVE-AGG-SLICE-BY-ADDRESS) tk_slice_elem_box — copy one aggregate element
// into the CURRENT region's storage and hand back its address, so a push inside a loop stores a
// distinct address per ITERATION instead of the one frame slot the native backend allocates per
// instruction. tk_alloc already maps n == 0 to a unique block, so a zero-sized aggregate still yields
// a distinct address; the copy follows tk_region_current() (root until a tk_region_enter), matching
// the buffer tk_slice_push grows.
void *tk_slice_elem_box(const void *elem, uint64_t esz) {
    void *p = tk_alloc((size_t)esz);
    if (esz != 0) memcpy(p, elem, (size_t)esz);
    return p;
}

// tk_slice_variant_widen — see teko_rt.h. The 24 and 8 below are the own backend's own
// variant_wrapper_bytes()/variant_payload_offset(): a boxed variant is a 24-byte block whose i64 tag
// sits at offset 0 and whose payload region begins at offset 8. `[]T` for a variant T is itself
// by-address, so the result is a `len`-long array of 8-byte pointers into freshly boxed wrappers.
void *tk_slice_variant_widen(const void *src, uint64_t len, uint64_t src_stride,
                             int64_t tag, int32_t mode, uint64_t pcopy) {
    void **dst = (void **)tk_alloc((size_t)(len * 8));
    for (uint64_t i = 0; i < len; i++) {
        unsigned char *w = (unsigned char *)tk_alloc(24);
        memset(w, 0, 24);
        *(int64_t *)w = tag;
        const unsigned char *s = (const unsigned char *)src + i * src_stride;
        if (mode == 0) {
            if (pcopy != 0) memcpy(w + 8, s, (size_t)pcopy);
        } else if (mode == 1) {
            memcpy(w + 8, s, 16);
        } else if (mode == 2) {
            const void *inner = *(const void *const *)s;
            void *box = tk_alloc((size_t)pcopy);
            if (pcopy != 0) memcpy(box, inner, (size_t)pcopy);
            *(void **)(w + 8) = box;
        }
        dst[i] = (void *)w;
    }
    return (void *)dst;
}

// (0.3.1.0 degrau 18) tk_mem_copy — see teko_rt.h for why the native lowering needs this at all
// (a reference deref-assignment's aggregate arm, `store_assign_aggregate_ref`).
void tk_mem_copy(void *dst, const void *src, uint64_t n) {
    if (n != 0) memcpy(dst, src, (size_t)n);
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
    tk_canary_check_head(tk_region_current(), "tk_free_block:entry");
    { static int dbg=-1; if (dbg<0) dbg = getenv("TEKO_FO_DEBUG")?1:0;
      if (dbg) fprintf(stderr, "PARK %p bytes=%llu\n", p, (unsigned long long)bytes); }
    unsigned h = tk_push_slot(p);
    if (tk_push_cache[h].ptr == p) tk_push_cache[h].ptr = NULL;   // evict the live-tail record
    // (C1) Inside a scoped-region window (cur_rsp != 0) the block belongs to a child region that
    // tk_region_drop bulk-frees; parking it on the ROOT free-list would dangle after the drop and be
    // handed back to a root allocation (a UAF the gen check cannot see, since the free-list has no
    // generation). At root scope this is a no-op guard, so root-only frees park exactly as before.
    if (tk_cur_rsp != 0) return;
    // (#148 Level-2) PARK = FLOOR-16 — never LIE about the block's size. The caller's `bytes` is a
    // true LOWER bound (len*esz) that need not be a 16-multiple: flooring to the 16-byte bin
    // granularity keeps the parked size ≤ the block's real usable extent, so serving it never
    // overruns the bump-adjacent NEIGHBOR (freenode header / paranoid poison would corrupt live
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

// --- checked float division + float bit patterns ---
// The sign-aware `tk_div`/`tk_rem`/`tk_int_to_float` trio that used to live here rode a 128-bit
// carrier and is REMOVED (128-bit primitives, integer and float, are gone from the language —
// owner ruling 2026-07-30). See teko_rt.h's declaration block for why nothing called them.
double tk_fdiv(double a, double b) { if (b == 0.0) tk_panic_div0(); return a / b; }
uint64_t tk_f64_bits(double x)      { uint64_t b; memcpy(&b, &x, sizeof b); return b; }
double   tk_f64_from_bits(uint64_t bits) { double x; memcpy(&x, &bits, sizeof x); return x; }

// ============================================================================
// teko::time  (drop-128 A6 redesign, owner-ratified table 2026-07-24)
// Every host-dependent read below returns a bare SCALAR (int32_t/uint64_t/int16_t/int64_t),
// never one of the carrier structs — see teko_rt.h's block comment for why. The
// `tk_rt_date_from_days`/`tk_rt_date_year`/`tk_rt_date_month`/`tk_rt_date_day_of_month`
// struct-taking quartet is the ONE exception, kept only for the pre-existing
// examples/regressions/time_types fixture's own direct extern re-declaration.
// ============================================================================

// --- helpers ---

// POSIX-only: return nanoseconds since Unix epoch as a signed i64 (fits: +-292 years,
// comfortably spanning any real wall-clock read). Windows branch uses FILETIME (100-ns
// ticks since 1601-01-01).
static int64_t tk_wall_now_ns(void) {
#if defined(_WIN32)
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    uint64_t w = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    // Subtract Windows epoch offset (1601-01-01 → 1970-01-01 = 116444736000000000 × 100ns ticks).
    w -= (uint64_t)116444736000000000ULL;
    return (int64_t)(w * 100);  // 100-ns ticks → nanoseconds
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
#endif
}








int16_t tk_rt_wall_offset_minutes(void) {
#if defined(_WIN32)
    TIME_ZONE_INFORMATION tzi;
    GetTimeZoneInformation(&tzi);
    // Windows Bias is minutes WEST of UTC; negate to get east (positive = ahead of UTC).
    return -(int16_t)tzi.Bias;
#else
    time_t now = (time_t)(tk_wall_now_ns() / 1000000000LL);
    struct tm loc;
    localtime_r(&now, &loc);
    return (int16_t)(loc.tm_gmtoff / 60);
#endif
}


// tk_nproc — logical CPUs the OS grants this process. Floor 1: an OS that answers 0 or fails is
// treated as one CPU, because a run always has at least the lane executing it. No allocation.
uint64_t tk_nproc(void) {
#if defined(_WIN32)
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n < 1 ? (uint64_t)1 : (uint64_t)n;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n < 1 ? (uint64_t)1 : (uint64_t)n;
#endif
}
// ===== journal runtime funds (design journaling-de-corrida-0.3.1 §2.3 / §6) — BEGIN =====
// See teko_rt.h for the per-symbol contract. NEW symbols only; no function above is edited, so a drain
// rebase against the M2 (tk_region_enter/leave, ~:1855) and 5th-gap (tk_slice_push*, ~:3762) edits
// elsewhere in this file is mechanical. The str-taking entries are transcribed into lir/lower.tks's
// by-value-tk_str arity tables, per that file's "the table mirrors the header" law.

#define TK_JOURNAL_NOTE_PREFIX_MAX 512

// The async-signal-safe note target: the last descriptor tk_journal_open handed out, plus the
// pre-shaped record head tk_journal_arm stored. `volatile sig_atomic_t` for the fd because a signal
// handler reads it. The prefix bytes are written only OUTSIDE a handler (tk_journal_arm, at run open)
// and read inside one — safe because arming completes long before a polite signal can arrive.
static volatile sig_atomic_t tk_journal_note_fd = -1;
static char tk_journal_note_prefix[TK_JOURNAL_NOTE_PREFIX_MAX];
static volatile size_t tk_journal_note_prefix_len = 0;

// tk_journal_write_raw — one blocking, loop-completed write of `n` bytes to `fd`. Shared by the
// buffered append and the async-signal-safe note; uses only write(2)/_write, so it is legal in a
// signal handler.
static int tk_journal_write_raw(int fd, const char *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
#ifdef _WIN32
        int put = _write(fd, buf + done, (unsigned int)(n - done));
#else
        ssize_t put = write(fd, buf + done, n - done);
#endif
        if (put <= 0) return -1;
        done += (size_t)put;
    }
    return 0;
}

int64_t tk_journal_open(tk_str path) {
    char *p = tk_cstr(path);
#ifdef _WIN32
    int fd = _open(p, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(p, O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
    if (fd < 0) return -1;
    tk_journal_note_fd = fd;   // the segment a polite signal will note into (§6)
    return (int64_t)fd;
}

int32_t tk_journal_append(tk_str rec, int64_t seg) {
    if (tk_journal_write_raw((int)seg, (const char *)rec.ptr, rec.len) != 0)
        return (int32_t)(errno ? errno : -1);
    return 0;
}

void tk_journal_arm(tk_str prefix) {
    size_t n = prefix.len < (size_t)(TK_JOURNAL_NOTE_PREFIX_MAX - 1) ? prefix.len : (size_t)(TK_JOURNAL_NOTE_PREFIX_MAX - 1);
    if (n) memcpy(tk_journal_note_prefix, prefix.ptr, n);
    tk_journal_note_prefix_len = n;
}

void tk_journal_note(int sig) {
    int fd = (int)tk_journal_note_fd;
    if (fd < 0) return;
    if (tk_journal_note_prefix_len)
        (void)tk_journal_write_raw(fd, tk_journal_note_prefix, tk_journal_note_prefix_len);
    char num[16];
    size_t i = sizeof num;
    unsigned int v = (sig < 0) ? 0u : (unsigned int)sig;
    if (v == 0u) num[--i] = '0';
    while (v > 0u) { num[--i] = (char)('0' + (v % 10u)); v /= 10u; }
    (void)tk_journal_write_raw(fd, num + i, sizeof num - i);
    (void)tk_journal_write_raw(fd, "\n", 1);
}

int32_t tk_rt_rename(tk_str from, tk_str to) {
    char *f = tk_cstr(from);
    char *t = tk_cstr(to);
    if (rename(f, t) != 0) return (int32_t)(errno ? errno : -1);
    return 0;
}

int64_t tk_rt_pid(void) {
#ifdef _WIN32
    return (int64_t)GetCurrentProcessId();
#else
    return (int64_t)getpid();
#endif
}

bool tk_rt_pid_alive(int64_t pid) {
    if (pid <= 0) return false;
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (h == NULL) return false;
    DWORD w = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return w == WAIT_TIMEOUT;
#else
    if (kill((pid_t)pid, 0) == 0) return true;
    return errno == EPERM;
#endif
}

// tk_rt_stop_handler — the POLITE-SIGNAL arm (design §6). Someone asked the program to stop (Ctrl-C, a
// CI timeout, a cancel): write ONE `stop <sig>` record to the armed segment, then RESTORE the default
// disposition and re-raise, so the process terminates EXACTLY as it would have with no handler at all.
// The only observable difference is the note — the exit path, the wait-status, the core dump are the
// signal's own default. ASYNC-SIGNAL-SAFE by construction: tk_journal_note writes pre-built bytes,
// signal() and raise() are on POSIX's async-safe list, and nothing here allocates or formats.
static void tk_rt_stop_handler(int sig) {
    tk_journal_note(sig);
    signal(sig, SIG_DFL);
    raise(sig);
}

// tk_rt_install_stop_handlers — install tk_rt_stop_handler for the "someone asked you to stop" signals
// the crash installer (SIGSEGV/BUS/ILL/FPE — "the program broke") deliberately leaves alone. SIGINT
// and SIGTERM exist on Windows too; SIGHUP and SIGQUIT are POSIX-only, under the same guard
// tk_win32_spawnvp's platform code already uses. A SEPARATE constructor from the crash installer so a
// drain rebase against that region stays trivial.
__attribute__((constructor)) static void tk_rt_install_stop_handlers(void) {
    signal(SIGINT,  tk_rt_stop_handler);
    signal(SIGTERM, tk_rt_stop_handler);
#ifndef _WIN32
    signal(SIGHUP,  tk_rt_stop_handler);
    signal(SIGQUIT, tk_rt_stop_handler);
#endif
}
// ===== journal runtime funds — END =====
