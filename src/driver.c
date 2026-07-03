// src/driver.c — the Teko bootstrap DRIVER (F1: path-to-first-binary).
//
// Wires read → lex → parse → reconcile → check end-to-end. This is the milestone
// where `teko <file.tks>` first processes real Teko: the front-end pipeline runs
// and reports its verdict. Codegen/emit (F2) is NOT here — success == "type-checked".
#include "driver.h"

#include <sys/stat.h>        // mkdir — ensure the -o dir exists for the --coverage report
#include "lexer/lexer.h"     // tk_tokenize, tk_tokens_result
#include "parser/parser.h"   // tk_parse_main_file, tk_parse_module
#include "parser/result.h"   // tk_parsed_main_file_result, tk_parsed_module_result
#include "checker/typer.h"   // tk_type_program, tk_tprogram_result
#include "codegen/codegen.h" // tk_emit_c, tk_cstr_result (F2 backend)
#include "build/manifest.h"  // tk_parse_manifest, tk_manifest (A1)
#include "build/discover.h"  // tk_discover, tk_source_files (A2)
#include "build/assemble.h"  // tk_assemble, tk_program_result (A3)
#include "vm/vm.h"           // tk_vm_run (Eixo D — the debug/test VM)
#include "text/text.h"       // tk_str_from_utf8 (manifest source view)
// C7.12 package backend: tkb_frame.h, zip.h, and collect.h are compatible with the
// includes above. emit/header.h conflicts via tkb_read.h (tk_strs typedef clash with
// manifest.h) — forward-declare tk_emit_program and tk_bytes_result directly instead.
#include "emit/tkb_buf.h"    // tk_bytes — needed by zip.h and the package emitters
#include "emit/tkb_frame.h"  // tk_serialize_program — .tkb whole-program codec (C7.12/C7.16)
#include "compress/compress.h"   // tk_write_zip — ZIP-STORE .tkl writer (C7.12, teko::compress)
#include "checker/collect.h" // tk_type_table_of — rebuild TypeTable from TProgram (C7.12)
// Forward-declare tk_emit_program to avoid pulling in header.h → tkb_read.h → tk_strs clash.
TK_RESULT(tk_bytes, tk_bytes_result);   // []byte | error — tk_emit_program's result type
tk_bytes_result tk_emit_program(tk_tprogram prog, tk_type_table table);
// C7.10: forward-declare tk_deserialize_program (from emit/tkb_read.h) to avoid pulling in
// tkb_read.h which re-typedefs tk_strs (clashing with the local definition in manifest.c / this TU).
// tk_tprogram_result is already defined via checker/tast.h (included by checker/typer.h above).
tk_tprogram_result tk_deserialize_program(const tk_byte *data, size_t len);

#include <stdio.h>           // fopen/fread/fclose, fprintf, printf
#include <stdlib.h>          // malloc, realloc, free
#include <string.h>          // strrchr, strcmp, memcpy
#ifdef _WIN32
#include "win32_compat.h"    // chdir→_chdir, mkdir, getcwd, setenv, dirent shim, tk_win32_spawnvp
#include <io.h>              // _dup, _dup2, _close — fd-redirect around spawn_wait's _spawnvp (issue #73)
#else
#include <unistd.h>          // chdir (run the project path from its own root — A3)
#include <spawn.h>           // posix_spawnp — safer cc invocation without shell
#include <sys/wait.h>        // waitpid, WIFEXITED, WEXITSTATUS
#include <sys/stat.h>        // mkdir — the native build output dir (./bin)
#include <dirent.h>          // opendir/readdir — find the single *.tkp manifest
#include <fcntl.h>           // O_WRONLY — /dev/null redirect for the quiet cc flag probe (issue #73)
#endif

// F3: where the minimal execution runtime (teko_rt.h/.c) lives. CMake injects the
// absolute path; a non-CMake `cc`-built teko falls back to the in-tree dir.
#ifndef TK_RT_DIR
#define TK_RT_DIR "src/runtime"
#endif

// F3: where the compiler source tree lives. The generated programs also link the
// teko::assert C seed (src/assert/assert.c), so the host cc must compile it too. CMake
// injects the absolute path; a non-CMake `cc`-built teko falls back to the in-tree dir.
#ifndef TK_SRC_DIR
#define TK_SRC_DIR "src"
#endif

// =========================================================================
// B1a — the IO boundary (the bootstrap's ONE host-IO function — M.1, contained).
// fopen/fread/fclose is the unsafe host edge: raw bytes cross here, are UTF-8
// validated via tk_str_from_utf8, and everything downstream trusts the str (B.36).
// =========================================================================
static tk_str_result io_err(const char *m) {
    return (tk_str_result){ .ok = false, .as.error = tk_error_make(m) };
}

tk_str_result tk_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return io_err("cannot open file");

    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return io_err("cannot seek file"); }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return io_err("cannot size file"); }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return io_err("cannot rewind file"); }

    size_t n = (size_t)sz;
    tk_byte *buf = tk_alloc(n == 0 ? 1 : n);   // never tk_alloc(0)
    if (buf == NULL) { fclose(f); return io_err("out of memory reading file"); }

    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) { tk_free0(buf); return io_err("short read on file"); }

    // The validated door from bytes to a str (UTF-8 FORCED — B.36). The buffer is
    // owned by the returned str's view for the lifetime of the compile (process-
    // lifetime in the bootstrap; no free needed — M.5 arena-style).
    tk_str_result s = tk_str_from_utf8(buf, n);
    if (!s.ok) { tk_free0(buf); }
    return s;
}

// =========================================================================
// B1c — R-main reconciliation. A local grow-append over tk_item (tk_items_push is
// not provided by the AST header; the prompt sanctions a local grow-append).
// =========================================================================
typedef struct { tk_item *ptr; size_t len; size_t cap; } items_buf;

static items_buf items_push(items_buf b, tk_item it) {
    if (b.len == b.cap) {
        size_t ncap = (b.cap == 0) ? 8 : (b.cap * 2);
        tk_item *np = tk_realloc0(b.ptr, ncap * sizeof(tk_item));
        if (np == NULL) abort();   // allocation failure PANICS (M.5)
        b.ptr = np;
        b.cap = ncap;
    }
    b.ptr[b.len] = it;
    b.len += 1;
    return b;
}

// MainFile = uses + virtual-main statements (parse_file.tks). Faithful flatten:
// every `use` becomes a TK_ITEM_USE, every body statement a TK_ITEM_STATEMENT.
tk_program tk_main_file_to_program(tk_main_file mf) {
    items_buf b = { .ptr = NULL, .len = 0, .cap = 0 };
    for (size_t i = 0; i < mf.n_uses; i += 1)
        b = items_push(b, (tk_item){ .tag = TK_ITEM_USE, .as.use_decl = mf.uses[i] });
    for (size_t i = 0; i < mf.n_body; i += 1)
        b = items_push(b, (tk_item){ .tag = TK_ITEM_STATEMENT, .as.statement = mf.body[i] });
    return (tk_program){ .items = b.ptr, .len = b.len };
}

// Module = uses + decls (parse_file.tks). Each Decl (Function | TypeDecl) maps to the
// matching item kind.
tk_program tk_module_to_program(tk_module m) {
    items_buf b = { .ptr = NULL, .len = 0, .cap = 0 };
    for (size_t i = 0; i < m.n_uses; i += 1)
        b = items_push(b, (tk_item){ .tag = TK_ITEM_USE, .as.use_decl = m.uses[i] });
    for (size_t i = 0; i < m.n_decls; i += 1) {
        tk_decl d = m.decls[i];
        if (d.tag == TK_DECL_FUNCTION)
            b = items_push(b, (tk_item){ .tag = TK_ITEM_FUNCTION, .as.function = d.as.function });
        else   // TK_DECL_TYPE
            b = items_push(b, (tk_item){ .tag = TK_ITEM_TYPE_DECL, .as.type_decl = d.as.type_decl });
    }
    return (tk_program){ .items = b.ptr, .len = b.len };
}

// =========================================================================
// B1d — the driver. Shared diagnostics helper.
// =========================================================================
static int fail(const char *path, const char *message) {
    // An empty path means the message already carries its own file:line:col (e.g. a
    // per-file assemble diagnostic) — print it bare, don't prefix a redundant location.
    if (path == NULL || path[0] == '\0') fprintf(stderr, "teko: %s\n", message);
    else fprintf(stderr, "teko: %s: %s\n", path, message);
    return 1;
}

// =========================================================================
// (C1.8/E2) RICH DIAGNOSTIC RENDERER — the C-side of this logic; driver.tks mirrors
// `fail` (the simpler path) because the structured tk_error fields are the E2 carve-out,
// not yet on the Teko surface `error`). When the checker hands back a STRUCTURED error (file +
// line + col set — C1-POS/C1.8), print the located header (unchanged behavior), then the offending
// SOURCE LINE read from the file, then a caret '^' aligned under the column, and — when present —
// an "expected … found …" type line. With no structured position, fall back to plain `fail`.
// =========================================================================

// Find the 1-based `line` within `src`, returning a VIEW of its bytes (no trailing newline) in
// *out. Returns true if the line exists. (Contained host-text walk — same spirit as compute_loc.)
static bool nth_line(tk_str src, uint32_t line, tk_str *out) {
    if (line == 0) return false;
    uint32_t cur = 1;
    size_t start = 0;
    for (size_t i = 0; i < src.len; i += 1) {
        if (cur == line) {
            size_t end = i;                          // scan to end-of-line from `start`
            while (end < src.len && src.ptr[end] != '\n') end += 1;
            *out = (tk_str){ .ptr = src.ptr + start, .len = end - start };
            return true;
        }
        if (src.ptr[i] == '\n') { cur += 1; start = i + 1; }
    }
    if (cur == line && start <= src.len) {           // the final line (no trailing '\n')
        *out = (tk_str){ .ptr = src.ptr + start, .len = src.len - start };
        return true;
    }
    return false;
}

// Print the offending source line + a caret under column `col` (1-based). The caret prefix
// REPRODUCES the source line's leading bytes up to col-1, emitting a TAB where the source has a
// tab and a SPACE otherwise — so the caret aligns under proportional/tab-indented code. Tabs
// count as ONE column, matching the lexer's compute_loc convention (lexer.c). If col exceeds the
// line length we clamp the caret to the line's end (still honest — points at the last column).
static void print_caret(tk_str srcline, uint32_t col) {
    fprintf(stderr, "  %.*s\n", (int)srcline.len, (const char *)srcline.ptr);
    fputs("  ", stderr);                             // align under the 2-space source indent above
    uint32_t upto = col > 0 ? col - 1 : 0;
    for (uint32_t i = 0; i < upto; i += 1) {
        char c = (i < srcline.len && srcline.ptr[i] == '\t') ? '\t' : ' ';
        fputc(c, stderr);
    }
    fputs("^\n", stderr);
}

// Render a checker error richly. `e.message` already carries file:line:col (the located string —
// the fallback). When structured fields are set, add the source snippet + caret + type line.
static int fail_diag(tk_error e) {
    fprintf(stderr, "teko: %s\n", e.message);        // the located header (unchanged behavior)
    if (e.file != NULL && e.line > 0) {
        // e.file is a NUL-terminated path (discover.c::owned_str / the main.tks literal keep the
        // NUL), so it is safe to fopen here (the one contained host edge — M.1).
        tk_str_result fr = tk_read_file(e.file);
        tk_str srcline;
        if (fr.ok && nth_line(fr.as.value, e.line, &srcline))
            print_caret(srcline, e.col);
    }
    if (e.expected != NULL && e.actual != NULL)
        fprintf(stderr, "  expected %s, found %s\n", e.expected, e.actual);
    return 1;
}

// =========================================================================
// B2 — the BACKEND wiring (F2): write the emitted C, invoke the host `cc`, produce the
// native binary. Teko compiles PROJECTS (§2.6), so the output stem comes from the
// manifest `name`, not from any single input file.
// =========================================================================

static char *cstr_of(tk_str s);   // (defined below — used by run_cc for the [extern] cc/target/sysroot knobs)
static tk_str target_os_of(tk_manifest m);   // (defined below — C7.1f: target OS for [extern.libs.<os>] selection)
static bool   os_str_eq(tk_str a, tk_str b); // (defined below — os name equality)

// (C7.1k) write a minimal macOS Info.plist (mirror of project.tks::plist_xml) for the Mach-O
// `__TEXT,__info_plist` section, so the binary carries native version metadata (mdls / Get Info).
// Returns true on success. Fields must be plain text (no XML metacharacters); the seed's are.
static bool write_macos_plist(const char *path, tk_manifest m) {
    FILE *p = fopen(path, "wb");
    if (p == NULL) return false;
    tk_str ver = m.version.len ? m.version : (tk_str){ (const tk_byte *)"0.0.0.0", 7 };
    fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
          "<plist version=\"1.0\">\n<dict>\n", p);
    fprintf(p, "  <key>CFBundleName</key><string>%.*s</string>\n", (int)m.name.len, (const char *)m.name.ptr);
    fprintf(p, "  <key>CFBundleIdentifier</key><string>org.teko.%.*s</string>\n", (int)m.name.len, (const char *)m.name.ptr);
    fprintf(p, "  <key>CFBundleShortVersionString</key><string>%.*s</string>\n", (int)ver.len, (const char *)ver.ptr);
    fprintf(p, "  <key>CFBundleVersion</key><string>%.*s</string>\n", (int)ver.len, (const char *)ver.ptr);
    if (m.description.len)
        fprintf(p, "  <key>CFBundleGetInfoString</key><string>%.*s — %.*s</string>\n</dict>\n</plist>\n",
                (int)m.name.len, (const char *)m.name.ptr, (int)m.description.len, (const char *)m.description.ptr);
    else
        fprintf(p, "  <key>CFBundleGetInfoString</key><string>%.*s</string>\n</dict>\n</plist>\n",
                (int)m.name.len, (const char *)m.name.ptr);
    fclose(p);
    return true;
}

// Spawn `cc` with `argv` (argv[0] is ignored — `cc` is used as the exec path) and wait for
// exit. No shell involved. When `quiet` is set, the child's stdout/stderr are redirected to
// the null device — used by the flag probe so a deliberately-rejected flag doesn't leak an
// "unrecognized option" line into the user's build output. Returns the child's exit code, or
// -1 if it could not be spawned/waited on.
static int spawn_wait(const char *cc, char *const argv[], bool quiet) {
#ifdef _WIN32
    if (!quiet) {
        int w = tk_win32_spawnvp(cc, argv);
        return (w == -1) ? -1 : w;
    }
    // Redirect the child's stdout/stderr to NUL for the probe, restoring afterward.
    fflush(NULL);
    int saved_out = _dup(_fileno(stdout));
    int saved_err = _dup(_fileno(stderr));
    FILE *null_out = freopen("NUL", "w", stdout);
    FILE *null_err = freopen("NUL", "w", stderr);
    (void)null_out; (void)null_err;
    int w = tk_win32_spawnvp(cc, argv);
    fflush(NULL);
    _dup2(saved_out, _fileno(stdout));
    _dup2(saved_err, _fileno(stderr));
    _close(saved_out);
    _close(saved_err);
    return (w == -1) ? -1 : w;
#else
    extern char **environ;
    pid_t pid;
    int rc;
    if (quiet) {
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        rc = posix_spawnp(&pid, cc, &fa, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&fa);
    } else {
        rc = posix_spawnp(&pid, cc, NULL, NULL, argv, environ);
    }
    if (rc != 0) return -1;
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

// (issue #73) Toolchain portability: `-std=c23` and `-ferror-limit=0` are clang spellings.
// GCC (Ubuntu's default `cc`) rejects both — GCC 13 (ubuntu-latest/24.04) only understands
// the pre-standardization `-std=c2x` name (GCC 14+ also accepts `-std=c23`, but c2x is the
// portable spelling), and GCC's unlimited-errors flag is `-fmax-errors=0` not
// `-ferror-limit=0`. Detect the compiler family ONCE per build via a flag-acceptance probe
// (exit-code only — no output capture needed): try compiling an empty translation unit with
// the clang-only flags under `-fsyntax-only`. clang accepts them (exit 0); GCC rejects the
// unrecognized options (nonzero exit). Mirrors project.tks::cc_family_is_clang.
// The probe file <binary>.ccprobe.c is deleted after the probe (best-effort, unchecked —
// the .tks twins use teko::fs::remove_file, the host primitive ratified in issue #79).
static bool cc_family_is_clang(const char *cc, const char *binary) {
    size_t plen = strlen(binary) + strlen(".ccprobe.c") + 1;
    char *path = tk_alloc(plen);
    snprintf(path, plen, "%s.ccprobe.c", binary);
    FILE *f = fopen(path, "wb");
    if (f == NULL) { tk_free0(path); return true; }   // can't probe (no file created) — assume clang (today's default), fail open
    fputs("int main(void){return 0;}\n", f);
    fclose(f);

    char *argv[] = { (char *)cc, "-std=c23", "-ferror-limit=0", "-fsyntax-only", path, NULL };
    int rc = spawn_wait(cc, argv, true);
    remove(path);   // best-effort cleanup (mirrors the .tks twins' remove_file)
    tk_free0(path);
    return rc == 0;
}

// Run the host C compiler over `cfile`, producing `binary`. Returns 0 on success.
static int run_cc(const char *cfile, const char *binary, tk_manifest m) {
    // F3: build argv for posix_spawnp — mirrors driver.tks::run_cc's teko::process::run(argv).
    // Passing paths as argv elements (no shell) eliminates shell-injection risk.
    char *ccprog = m.cc.len ? cstr_of(m.cc) : NULL;
    const char *cc = ccprog ? ccprog : "cc";
    bool is_clang = cc_family_is_clang(cc, binary);
    char *target_str  = m.target.len  ? cstr_of(m.target)  : NULL;
    char *sysroot_str = m.sysroot.len ? cstr_of(m.sysroot) : NULL;
    tk_str tos = target_os_of(m);

    // (CLI --version) embed the project's RAW manifest version as a compile-time constant so
    // tk_rt_version() (linked from teko_rt.c) returns it — the source of truth is the project's
    // own `.tkp` (teko.tkp for teko itself). Mirrors CMake's TEKO_VERSION_STRING for the bootstrap
    // build and project.tks::run_cc's -D flag. RAW `version` + `-<suffix>` (no gen substitution).
    char *verdef = NULL;
    {
        size_t base = strlen("-DTEKO_VERSION_STRING=\"\"");
        size_t vl = base + m.version.len + (m.suffix.len ? 1 + m.suffix.len : 0) + 1;
        verdef = tk_alloc(vl);
        if (m.suffix.len)
            snprintf(verdef, vl, "-DTEKO_VERSION_STRING=\"%.*s-%.*s\"",
                     (int)m.version.len, (const char *)m.version.ptr,
                     (int)m.suffix.len,  (const char *)m.suffix.ptr);
        else
            snprintf(verdef, vl, "-DTEKO_VERSION_STRING=\"%.*s\"",
                     (int)m.version.len, (const char *)m.version.ptr);
    }

    // C7.1k: macOS Info.plist section for Mach-O version metadata.
    char *plistp = NULL, *secarg = NULL;
    if (tos.len == 5 && memcmp(tos.ptr, "macos", 5) == 0) {
        size_t pl = strlen(binary) + strlen(".Info.plist") + 1;
        plistp = tk_alloc(pl);
        snprintf(plistp, pl, "%s.Info.plist", binary);
        if (write_macos_plist(plistp, m)) {
            size_t sl = strlen("-Wl,-sectcreate,__TEXT,__info_plist,") + strlen(plistp) + 1;
            secarg = tk_alloc(sl);
            snprintf(secarg, sl, "-Wl,-sectcreate,__TEXT,__info_plist,%s", plistp);
        }
    }

    // Build -I and source path strings (combined -I<dir> form, no shell quoting needed).
    char *i_rt = tk_alloc(strlen(TK_RT_DIR) + 3);
    snprintf(i_rt, strlen(TK_RT_DIR) + 3, "-I%s", TK_RT_DIR);
    char *i_assert = tk_alloc(strlen(TK_SRC_DIR) + 10);
    snprintf(i_assert, strlen(TK_SRC_DIR) + 10, "-I%s/assert", TK_SRC_DIR);
    char *rt_c = tk_alloc(strlen(TK_RT_DIR) + 12);
    snprintf(rt_c, strlen(TK_RT_DIR) + 12, "%s/teko_rt.c", TK_RT_DIR);
    char *assert_c = tk_alloc(strlen(TK_SRC_DIR) + 18);
    snprintf(assert_c, strlen(TK_SRC_DIR) + 18, "%s/assert/assert.c", TK_SRC_DIR);

    // Convert per-OS link flags to cstrs.
    size_t nos_lib = 0;
    for (size_t i = 0; i < m.os_lib_flag.len; i++)
        if (os_str_eq(m.os_lib_os.ptr[i], tos)) nos_lib++;
    char **os_flags = nos_lib ? tk_alloc(nos_lib * sizeof(char *)) : NULL;
    size_t of = 0;
    for (size_t i = 0; i < m.os_lib_flag.len; i++) {
        if (!os_str_eq(m.os_lib_os.ptr[i], tos)) continue;
        os_flags[of++] = cstr_of(m.os_lib_flag.ptr[i]);
    }

    // Convert [extern.libs] link flags to cstrs.
    char **lf = m.link_flags.len ? tk_alloc(m.link_flags.len * sizeof(char *)) : NULL;
    for (size_t i = 0; i < m.link_flags.len; i++)
        lf[i] = cstr_of(m.link_flags.ptr[i]);

    // Assemble argv: fixed flags + optional target/sysroot + includes + sources + libs + output.
    // (issue #73) +1 headroom for the extra GCC flag slot (`-fmax-errors=0` replaces the single
    // clang `-ferror-limit=0` 1-for-1, but keep max_args honest either way).
    size_t max_args = 26 + m.link_flags.len + nos_lib;   // +1 for the -DTEKO_VERSION_STRING flag
    char **argv = tk_alloc(max_args * sizeof(char *));
    size_t argc = 0;

    argv[argc++] = (char *)cc;
    argv[argc++] = is_clang ? "-std=c23" : "-std=c2x";
    argv[argc++] = "-w";
    if (is_clang) argv[argc++] = "-ferror-limit=0";
    else          argv[argc++] = "-fmax-errors=0";   // GCC's unlimited-errors analog
    if (verdef) argv[argc++] = verdef;               // -DTEKO_VERSION_STRING for teko_rt.c (CLI --version)
    if (target_str)  { argv[argc++] = "-target";    argv[argc++] = target_str; }
    if (sysroot_str) { argv[argc++] = "--sysroot";  argv[argc++] = sysroot_str; }
    if (m.freestanding) argv[argc++] = "-nostdlib";
    argv[argc++] = i_rt;
    argv[argc++] = i_assert;
    argv[argc++] = (char *)cfile;
    argv[argc++] = rt_c;
    argv[argc++] = assert_c;
    if (!m.freestanding) argv[argc++] = "-lm";
    for (size_t i = 0; i < m.link_flags.len; i++) argv[argc++] = lf[i];
    for (size_t i = 0; i < nos_lib;           i++) argv[argc++] = os_flags[i];
    if (secarg) argv[argc++] = secarg;
    argv[argc++] = "-o";
    argv[argc++] = (char *)binary;
    argv[argc]   = NULL;

    // Invoke the host compiler directly (no shell) — full diagnostics visible (not quiet).
    int rc = spawn_wait(cc, argv, false);

    if (lf) {
        for (size_t i = 0; i < m.link_flags.len; i++) tk_free0(lf[i]);
        tk_free0(lf);
    }
    if (os_flags) {
        for (size_t i = 0; i < nos_lib; i++) tk_free0(os_flags[i]);
        tk_free0(os_flags);
    }
    tk_free0(argv);
    tk_free0(i_rt); tk_free0(i_assert); tk_free0(rt_c); tk_free0(assert_c);
    if (target_str)  tk_free0(target_str);
    if (sysroot_str) tk_free0(sysroot_str);
    if (secarg) tk_free0(secarg);
    if (plistp) tk_free0(plistp);
    if (verdef) tk_free0(verdef);
    if (ccprog) tk_free0(ccprog);
    return rc;
}

// Lower → write .c → invoke cc → report. `stem` is the output path (no extension);
// `label` is what the diagnostics name (the project dir/name). Returns 0 on success.
static int tk_backend(const char *label, const char *stem, tk_tprogram prog, const char *out_dir, tk_manifest m) {
    // (C7.1m) artifact-kind dispatch. Binary → the native executable backend below. The library kinds
    // are HONEST STOPS until their increments land (M.3 — no fake artifact): static/shared = native
    // lib output (ar / -shared); package = a `.tkl` ZIP of .tkh+.tkb+.tsym, which needs the PROGRAM-
    // level `.tkb` typed-tree codec (today the `.tkb` is expression-only) — the keystone next step.
    if (m.artifact == TK_ARTIFACT_STATIC)
        return fail(label, "static library output (.a) is not yet implemented — planned (C7.1m)");
    if (m.artifact == TK_ARTIFACT_SHARED)
        return fail(label, "shared library output (.dylib/.so/.dll) is not yet implemented — planned (C7.1m)");
    if (m.artifact == TK_ARTIFACT_PACKAGE) {
        // C7.12: emit .tkh + .tkb (+ best-effort .tsym) → ZIP-STORE → <out_dir>/<name>-<version>.tkl
        // trim a trailing `/` from the output dir (mirrors the Binary path below).
        size_t odlen = strlen(out_dir);
        char od[4096];
        if (odlen > 0 && odlen < sizeof od) {
            memcpy(od, out_dir, odlen);
            if (od[odlen - 1] == '/') odlen -= 1;
            od[odlen] = '\0';
        } else {
            snprintf(od, sizeof od, "%s", out_dir);
        }
        mkdir(od, 0755);   // idempotent

        // .tkh — exported interface (type signatures + docs for `exp` items)
        tk_type_table table = tk_type_table_of(prog);
        tk_bytes_result tkh_r = tk_emit_program(prog, table);
        if (!tkh_r.ok) return fail(label, tkh_r.as.error.message);
        tk_bytes tkh_bytes = tkh_r.as.value;

        // .tkb — whole-program typed tree (C7.16 codec)
        tk_bytes tkb_bytes = tk_serialize_program(&prog);

        // .tsym — symbol map (best-effort; always included)
        tk_cstr_result tsym_r = tk_emit_tsym(prog);
        tk_bytes tsym_bytes = tk_bytes_empty();
        if (tsym_r.ok && tsym_r.as.value) {
            size_t tsym_len = strlen(tsym_r.as.value);
            for (size_t i = 0; i < tsym_len; i += 1)
                tsym_bytes = tk_bytes_push(tsym_bytes, (tk_byte)tsym_r.as.value[i]);
            tk_free0(tsym_r.as.value);
        }

        // Assemble the ZIP entries: <name>.tkh, <name>.tkb, <name>.tsym
        char tkh_name[256], tkb_name[256], tsym_name[256];
        snprintf(tkh_name,  sizeof tkh_name,  "%.*s.tkh",  (int)m.name.len, (const char *)m.name.ptr);
        snprintf(tkb_name,  sizeof tkb_name,  "%.*s.tkb",  (int)m.name.len, (const char *)m.name.ptr);
        snprintf(tsym_name, sizeof tsym_name, "%.*s.tsym", (int)m.name.len, (const char *)m.name.ptr);
#define CSTR(s) ((tk_str){ .ptr = (const unsigned char *)(s), .len = strlen(s) })
        tk_zip_entry entries[3] = {
            { .name = CSTR(tkh_name),  .data = tkh_bytes  },
            { .name = CSTR(tkb_name),  .data = tkb_bytes  },
            { .name = CSTR(tsym_name), .data = tsym_bytes },
        };
#undef CSTR
        tk_bytes zip_bytes = tk_write_zip(entries, 3);

        // <out_dir>/<name>-<version>.tkl
        const char *ver = (m.version.len > 0) ? NULL : "0.0.0";   // sentinel; real below
        char ver_buf[256];
        if (m.version.len > 0)
            snprintf(ver_buf, sizeof ver_buf, "%.*s", (int)m.version.len, (const char *)m.version.ptr);
        else
            snprintf(ver_buf, sizeof ver_buf, "0.0.0");
        (void)ver;
        char tkl_name[512];
        snprintf(tkl_name, sizeof tkl_name, "%.*s-%s.tkl",
                 (int)m.name.len, (const char *)m.name.ptr, ver_buf);
        char tkl_path[4096];
        snprintf(tkl_path, sizeof tkl_path, "%s/%s", od, tkl_name);

        FILE *fp = fopen(tkl_path, "wb");
        if (fp == NULL) { tk_free0(zip_bytes.ptr); return fail(label, "cannot write .tkl to the output directory"); }
        size_t wrote = fwrite(zip_bytes.ptr, 1, zip_bytes.len, fp);
        int close_rc = fclose(fp);
        tk_free0(zip_bytes.ptr);
        if (wrote != zip_bytes.len) return fail(label, "short write on .tkl package");
        if (close_rc != 0) return fail(label, "failed to close .tkl package");

        printf("teko: %s: packaged %s\n", label, tkl_path);
        return 0;
    }
    tk_cstr_result emitted = tk_emit_c(prog);
    if (!emitted.ok) return fail(label, emitted.as.error.message);

    // Native build artifacts go to `out_dir` (default "bin", or the `-o <dir>` argument), created
    // at the project root — cwd is the project after the front-end chdir'd. The generated C is
    // "<out_dir>/<stem>.c"; the binary is "<out_dir>/<stem>".
    // trim a trailing `/` so `-o ./bin/build/` doesn't yield `./bin/build//teko`.
    size_t odlen = strlen(out_dir);
    char od[4096];
    if (odlen > 0 && odlen < sizeof od) { memcpy(od, out_dir, odlen); if (od[odlen - 1] == '/') odlen -= 1; od[odlen] = '\0'; }
    else { snprintf(od, sizeof od, "%s", out_dir); }
    mkdir(od, 0755);   // ignore EEXIST — only a hard mkdir failure surfaces below via fopen
    size_t blen = strlen(od) + strlen(stem) + 4;   // "<out>/" + stem + ".c" + NUL
    char *cfile = tk_alloc(blen);
    char *binp  = tk_alloc(blen);
    if (cfile == NULL || binp == NULL) abort();
    snprintf(cfile, blen, "%s/%s.c", od, stem);
    snprintf(binp,  blen, "%s/%s", od, stem);

    FILE *f = fopen(cfile, "wb");
    if (f == NULL) { tk_free0(emitted.as.value); tk_free0(cfile); tk_free0(binp); return fail(label, "cannot write generated C to the output directory"); }
    size_t srclen = strlen(emitted.as.value);
    size_t wrote = fwrite(emitted.as.value, 1, srclen, f);
    tk_free0(emitted.as.value);
    // C7.1k — append the build-metadata C so every binary carries its identity (what(1)/strings).
    char *meta = tk_emit_meta(m.name, m.version, m.suffix, m.description);
    if (meta != NULL) { fwrite(meta, 1, strlen(meta), f); tk_free0(meta); }
    fclose(f);
    if (wrote != srclen) { tk_free0(cfile); tk_free0(binp); return fail(label, "short write on generated C"); }

    int rc = run_cc(cfile, binp, m);
    tk_free0(cfile);
    if (rc != 0) { tk_free0(binp); return fail(label, "cc failed to build the generated C"); }

    // E3 — write the symbol map alongside the binary (best-effort; a write failure never fails the
    // build — the .tsym is debug metadata for the E4 native stack-trace, not the artifact).
    tk_cstr_result tsym = tk_emit_tsym(prog);
    if (tsym.ok) {
        size_t tlen = strlen(binp) + 6;   // binp + ".tsym" + NUL
        char *tsymf = tk_alloc(tlen);
        if (tsymf != NULL) {
            snprintf(tsymf, tlen, "%s.tsym", binp);
            FILE *tf = fopen(tsymf, "wb");
            if (tf != NULL) { fwrite(tsym.as.value, 1, strlen(tsym.as.value), tf); fclose(tf); }
            tk_free0(tsymf);
        }
        tk_free0(tsym.as.value);
    }

    printf("teko: %s: built %s\n", label, binp);
    tk_free0(binp);
    return 0;
}

// =========================================================================
// The PROJECT FRONT-END (shared by build + run). read `.tkp` → discover → assemble →
// check (whole). Runs from the project root (chdir) so the manifest `source` and the
// discovered file paths (relative) resolve against the project. On success, `*out` holds
// the checked merged typed program and `*manifest_out` the parsed manifest (its `name`
// names the build artifact). Returns 0 on a clean check; non-zero (with a diagnostic)
// otherwise.
// =========================================================================
// Find the single `*.tkp` manifest in the current directory (the project has exactly one).
// Returns its name in `out` (1 on found, 0 if none/many). The name is conventionally
// `<name>.tkp` (e.g. teko.tkp) — self-describing — so we glob the extension rather than
// hardcode a filename. (Contained host edge — M.1.)
static int find_manifest(char *out, size_t cap) {
    DIR *d = opendir(".");
    if (d == NULL) return 0;
    int found = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        if (n < 4 || strcmp(e->d_name + n - 4, ".tkp") != 0) continue;
        if (found || n >= cap) { found = 2; continue; }   // 2 = ambiguous (more than one)
        memcpy(out, e->d_name, n + 1);
        found = 1;
    }
    closedir(d);
    return found == 1;
}

// =========================================================================
// C7.10 — DEP LOADING: load one dep's .tkl package, extract its .tkb, deserialize the
// TProgram. Mirrors project.tks::load_dep_program. Returns ok=true on success.
// =========================================================================

// Read a file as raw bytes (binary, no UTF-8 check). Returns {ptr,len} or NULL on error.
// Caller frees with tk_free0.
static unsigned char *read_file_bytes(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    size_t n = (size_t)sz;
    unsigned char *buf = (unsigned char *)tk_alloc(n ? n : 1);
    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) { tk_free0(buf); return NULL; }
    *out_len = n;
    return buf;
}

// Load one dep TProgram from its .tkl in packages/<dep>-*.tkl (mirrors project.tks::load_dep_program).
// On success *out receives the deserialized TProgram and true is returned.
// On failure a diagnostic is printed to stderr and false is returned.
static bool load_dep_program(const char *dep, tk_tprogram *out) {
    // List packages/ directory to find <dep>-*.tkl.
    char pkgdir[256];
    snprintf(pkgdir, sizeof pkgdir, "packages");
    DIR *d = opendir(pkgdir);
    if (d == NULL) {
        fprintf(stderr, "teko: dep '%s': packages/ directory not found — place .tkl packages in packages/\n", dep);
        return false;
    }
    size_t dlen = strlen(dep);
    char tkl_path[4096]; tkl_path[0] = '\0';
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        size_t n = strlen(e->d_name);
        // Must start with <dep>- and end with .tkl
        if (n < dlen + 5) continue;   // at least "dep-x.tkl"
        if (memcmp(e->d_name, dep, dlen) != 0) continue;
        if (e->d_name[dlen] != '-') continue;
        if (n < 4 || strcmp(e->d_name + n - 4, ".tkl") != 0) continue;
        snprintf(tkl_path, sizeof tkl_path, "packages/%s", e->d_name);
        break;
    }
    closedir(d);

    if (tkl_path[0] == '\0') {
        fprintf(stderr, "teko: dep '%s': package not found (looked in packages/)\n", dep);
        return false;
    }

    // Read the .tkl as raw bytes.
    size_t tkl_len = 0;
    unsigned char *tkl_bytes = read_file_bytes(tkl_path, &tkl_len);
    if (tkl_bytes == NULL) {
        fprintf(stderr, "teko: dep '%s': cannot read .tkl file '%s'\n", dep, tkl_path);
        return false;
    }

    // Unzip — find <dep>.tkb entry.
    size_t nentries = 0;
    tk_zip_entry *entries = tk_read_zip(tkl_bytes, tkl_len, &nentries);
    tk_free0(tkl_bytes);

    // Build the expected .tkb name: <dep>.tkb
    char tkb_name[256];
    snprintf(tkb_name, sizeof tkb_name, "%s.tkb", dep);

    const tk_byte *tkb_data = NULL; size_t tkb_len = 0;
    for (size_t i = 0; i < nentries; i += 1) {
        size_t nn = entries[i].name.len;
        if (nn == strlen(tkb_name) && memcmp(entries[i].name.ptr, tkb_name, nn) == 0) {
            tkb_data = entries[i].data.ptr;
            tkb_len  = entries[i].data.len;
            break;
        }
    }
    if (tkb_data == NULL) {
        fprintf(stderr, "teko: dep '%s': .tkl does not contain %s\n", dep, tkb_name);
        tk_free0(entries);
        return false;
    }

    // Deserialize the TProgram from the .tkb bytes.
    tk_tprogram_result pr = tk_deserialize_program(tkb_data, tkb_len);
    tk_free0(entries);   // frees name+data copies
    if (!pr.ok) {
        fprintf(stderr, "teko: dep '%s': .tkb deserialization failed: %s\n", dep, pr.as.error.message);
        return false;
    }
    *out = pr.as.value;
    return true;
}

// A simple growable tk_titem buffer — local to driver.c, mirrors TK_LIST(tk_titem, …) logic.
typedef struct { tk_titem *ptr; size_t len; size_t cap; } driver_titem_list;
static driver_titem_list driver_titem_push(driver_titem_list b, tk_titem it) {
    if (b.len == b.cap) {
        size_t nc = b.cap ? b.cap * 2 : 8;
        b.ptr = (tk_titem *)tk_realloc0(b.ptr, nc * sizeof(tk_titem));
        b.cap = nc;
    }
    b.ptr[b.len++] = it;
    return b;
}

// Load all deps listed in the manifest and return a TProgram with all their items concatenated.
// Mirrors project.tks::load_deps_program. Returns ok=true (with empty program) when m.deps is empty.
static bool load_deps_tprogram(tk_manifest m, tk_tprogram *out) {
    driver_titem_list all = { .ptr = NULL, .len = 0, .cap = 0 };
    for (size_t i = 0; i < m.deps.len; i += 1) {
        char dep_cstr[256];
        tk_str dep = m.deps.ptr[i];
        if (dep.len >= sizeof dep_cstr) {
            fprintf(stderr, "teko: dep name too long\n");
            return false;
        }
        memcpy(dep_cstr, dep.ptr, dep.len);
        dep_cstr[dep.len] = '\0';

        tk_tprogram dep_prog;
        if (!load_dep_program(dep_cstr, &dep_prog)) return false;
        for (size_t j = 0; j < dep_prog.nitems; j += 1)
            all = driver_titem_push(all, dep_prog.items[j]);
    }
    *out = (tk_tprogram){ .items = all.ptr, .nitems = all.len };
    return true;
}

// frontend_body — the front-end AFTER the chdir (manifest → discover → assemble → check),
// operating in the CURRENT directory. Split out so the D4 gate can retry it WITHOUT tests after a
// test-assembly failure on the SAME chdir (a second chdir to a relative project dir would fail).
// `quiet` suppresses the failure prints (the caller decides whether it's a hard error or a
// degrade-to-warning). (Mirrors project.tks frontend_body.)
// (C7.1f) #os conditional compilation — target-OS selection + the prune. Mirrors project.tks.
tk_str tk_rt_os(void);   // teko_rt.c — host OS (tk_str is from text.h)
static bool os_has_lit(tk_str s, const char *lit) {
    size_t m = strlen(lit);
    if (s.len < m) return false;
    for (size_t i = 0; i + m <= s.len; i++) if (memcmp(s.ptr + i, lit, m) == 0) return true;
    return false;
}
static tk_str target_os_of(tk_manifest m) {
    if (m.target.len) {
        if (os_has_lit(m.target, "linux")) return (tk_str){ (const tk_byte *)"linux", 5 };
        if (os_has_lit(m.target, "darwin") || os_has_lit(m.target, "macos") || os_has_lit(m.target, "apple")) return (tk_str){ (const tk_byte *)"macos", 5 };
        if (os_has_lit(m.target, "windows") || os_has_lit(m.target, "mingw") || os_has_lit(m.target, "w64")) return (tk_str){ (const tk_byte *)"windows", 7 };
    }
    return tk_rt_os();
}
static bool os_str_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }
// drop every `#os("…")`-guarded function whose guard ≠ the target OS (in place; preserves order).
static tk_program prune_os(tk_program program, tk_str tos) {
    size_t w = 0;
    for (size_t i = 0; i < program.len; i += 1) {
        tk_item it = program.items[i];
        bool keep = true;
        if (it.tag == TK_ITEM_FUNCTION) {
            tk_str g = it.as.function.os_guard;
            if (g.len != 0 && !os_str_eq(g, tos)) keep = false;
        }
        if (keep) program.items[w++] = it;
    }
    program.len = w;
    return program;
}

static int frontend_body(const char *dir, tk_tprogram *out, tk_manifest *manifest_out,
                         bool include_tests, bool quiet) {
    // --- find + read + parse the manifest (the single <name>.tkp at the root — A1) ---
    char mname[256];
    if (!find_manifest(mname, sizeof mname))
        return quiet ? 1 : fail(dir, "no single .tkp manifest in project directory");
    tk_str_result mtext = tk_read_file(mname);
    if (!mtext.ok) return quiet ? 1 : fail(dir, "cannot read .tkp manifest");
    tk_manifest_result mr = tk_parse_manifest(mtext.as.value);
    if (!mr.ok) return quiet ? 1 : fail(dir, mr.as.error.message);
    tk_manifest m = mr.as.value;

    // --- discover the source tree (A2) ---
    tk_source_files_result df = tk_discover(m.name, m.source);
    if (!df.ok) return quiet ? 1 : fail(dir, df.as.error.message);
    tk_source_files files = df.as.value;

    // The entry MainFile lives at the PROJECT ROOT, not under `source` (discover walks
    // only `source`). Inject it into the file set so the merged program has an entry,
    // tagged with the bare root namespace (provenance). Skip it if absent (a library).
    { tk_str_result em = tk_read_file("main.tks");
      if (em.ok) {
          tk_str mainp = { .ptr = (const tk_byte *)"main.tks", .len = 8 };
          files = tk_source_files_push(files,
              (tk_source_file){ .path = mainp, .namespace = m.name });
      } }

    // --- C7.9: A4 main-file rule — Binary REQUIRES main.tks; library kinds FORBID it ---
    { bool has_main = false;
      for (size_t i = 0; i < files.len; i++) {
          tk_str p = files.ptr[i].path;
          if (p.len >= 8 && memcmp(p.ptr + p.len - 8, "main.tks", 8) == 0) { has_main = true; break; }
      }
      tk_artifact_result arule = tk_check_main_file_rule(m.artifact, has_main);
      if (!arule.ok) return quiet ? 1 : fail(dir, arule.as.error.message); }

    // --- report the discovered file → namespace map (the project's surface) ---
    if (!quiet) {
        printf("teko: project '%.*s' (source=%.*s) — %zu file(s):\n",
               (int)m.name.len, (const char *)m.name.ptr,
               (int)m.source.len, (const char *)m.source.ptr, files.len);
        for (size_t i = 0; i < files.len; i += 1)
            printf("  %.*s\t-> %.*s\n",
                   (int)files.ptr[i].path.len,      (const char *)files.ptr[i].path.ptr,
                   (int)files.ptr[i].namespace.len, (const char *)files.ptr[i].namespace.ptr);
    }

    // --- assemble: read+parse every file, MERGE into ONE program (A3) ---
    tk_program_result asm_r = tk_assemble_sel(files, include_tests);
    if (!asm_r.ok) return quiet ? 1 : fail("", asm_r.as.error.message);   // assemble bakes file:line:col in
    tk_program program = asm_r.as.value;

    // --- C7.1f: keep only the target OS's `#os("…")` function variants (conditional compilation) ---
    program = prune_os(program, target_os_of(m));

    // --- C7.10: load dep .tkl packages and build a dep TProgram (mirrors project.tks::load_deps_program) ---
    tk_tprogram dep_prog = { .items = NULL, .nitems = 0 };
    if (m.deps.len > 0) {
        if (!load_deps_tprogram(m, &dep_prog))
            return quiet ? 1 : 1;   // error already printed by load_dep_program
    }

    // --- check the WHOLE merged program (M.1 — whole program checked together) ---
    // With deps: use tk_type_program_with_deps to inject dep signatures before type-checking.
    // Without deps (dep_prog.nitems == 0): falls back to standard type-checking.
    tk_tprogram_result checked = tk_type_program_with_deps(program, dep_prog);
    if (!checked.ok) return quiet ? 1 : fail_diag(checked.as.error);   // (C1.8) located header + snippet/caret

    if (!quiet)
        printf("teko: %s: project assembled (%zu items) and type-checked OK\n", dir, program.len);
    *out = checked.as.value;
    *manifest_out = m;
    return 0;
}

static int project_frontend(const char *dir, tk_tprogram *out, tk_manifest *manifest_out, bool include_tests) {
    // Enter the project root so relative source paths resolve (the contained host edge).
    if (chdir(dir) != 0) return fail(dir, "cannot enter project directory");
    return frontend_body(dir, out, manifest_out, include_tests, false);
}

// Heap-allocate a NUL-terminated copy of a tk_str (the manifest `name`, used as the
// output binary stem). Allocation failure aborts (M.5).
static char *cstr_of(tk_str s) {
    char *out = tk_alloc(s.len + 1);
    if (out == NULL) abort();
    memcpy(out, s.ptr, s.len);
    out[s.len] = '\0';
    return out;
}

// =========================================================================
// A3 — the PROJECT BUILD entry (`teko build <dir>`). Front-end (manifest → discover →
// assemble → check), then the native BACKEND over the merged program. The output binary
// stem is the manifest `name` (written at the project root, since project_frontend has
// chdir'd there). Single-namespace projects lower; an unsupported multi-namespace
// mangling case fails with codegen's honest message (no silent mis-emit).
// =========================================================================
int tk_compile_project(const char *dir, const char *out_dir) {
    return tk_compile_project_g(dir, out_dir, false);
}

// strip_tests — a copy of the program WITHOUT `#test` functions (D4): a release binary must not
// carry test code. (Mirrors project.tks strip_tests.)
static tk_tprogram strip_tests(tk_tprogram prog) {
    tk_titem *kept = tk_alloc(prog.nitems ? prog.nitems * sizeof(tk_titem) : 1);
    if (!kept) abort();
    size_t k = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag == TK_TITEM_FUNCTION && prog.items[i].as.function.is_test) continue;
        kept[k++] = prog.items[i];
    }
    return (tk_tprogram){ .items = kept, .nitems = k };
}

// has_tests — does the program contain any `#test` function? (The gate runs iff this is true; a
// project with NO tests builds unconditionally — the "no .tkt → ignore tests" rule.)
static bool has_tests(tk_tprogram prog) {
    for (size_t i = 0; i < prog.nitems; i += 1)
        if (prog.items[i].tag == TK_TITEM_FUNCTION && prog.items[i].as.function.is_test) return true;
    return false;
}

// tk_compile_project_g — the build with the D4 TEST GATE (ALWAYS ON; `--no-test` is IGNORED for a
// build — a release MUST run its tests). Assemble WITH the `.tkt` tests; if tests exist they MUST
// assemble, run, all pass, AND meet the coverage threshold — else the build is BLOCKED (no graceful
// degradation). The ONLY skip is a project with NO `#test` functions. (Mirrors project.tks
// compile_project_g.)
int tk_compile_project_g(const char *dir, const char *out_dir, bool gen_cov) {
    tk_tprogram prog;
    tk_manifest m;

    // D4 gate (STRICT): enter the project ONCE, assemble WITH the tests. A failure to assemble/
    // typecheck (production code OR test files) is a hard BUILD FAILURE when tests are present.
    if (chdir(dir) != 0) return fail(dir, "cannot enter project directory");
    int frc = frontend_body(dir, &prog, &m, true, false);
    if (frc != 0) return frc;   // assembly/typecheck failed → BLOCK (the diagnostic already printed)

    if (has_tests(prog)) {
        // The gate ALWAYS records branches (for the BRANCH floor); `--coverage` also writes the report
        // to <out_dir>/cobertura.xml (mkdir the dir first — idempotent). A failed assert aborts here.
        char cov_path[2048];
        if (gen_cov) { mkdir(out_dir, 0755); snprintf(cov_path, sizeof cov_path, "%s/cobertura.xml", out_dir); }
        else cov_path[0] = '\0';
        int trc = tk_vm_run_tests_cov(prog, true, gen_cov, cov_path);
        if (trc != 0) return trc;
        // D4 coverage floors (from the `.tkp` [coverage] section; default 80/80/80): BLOCK when
        // FUNCTION, LINE, or BRANCH coverage is below its floor.
        uint64_t fcov = tk_vm_coverage_pct(prog);
        if (fcov < m.cov_functions) {
            fprintf(stderr, "teko: %s: function coverage %llu%% is below the %llu%% floor ([coverage] functions) — add tests\n",
                    dir, (unsigned long long)fcov, (unsigned long long)m.cov_functions);
            return 1;
        }
        uint64_t lcov = tk_vm_line_coverage_pct(prog);
        if (lcov < m.cov_lines) {
            fprintf(stderr, "teko: %s: line coverage %llu%% is below the %llu%% floor ([coverage] lines) — add tests\n",
                    dir, (unsigned long long)lcov, (unsigned long long)m.cov_lines);
            return 1;
        }
        uint64_t bcov = tk_vm_branch_coverage_pct(prog);
        if (bcov < m.cov_branches) {
            fprintf(stderr, "teko: %s: branch coverage %llu%% is below the %llu%% floor ([coverage] branches) — add tests\n",
                    dir, (unsigned long long)bcov, (unsigned long long)m.cov_branches);
            return 1;
        }
        prog = strip_tests(prog);          // a release binary carries no test code
    }
    // else: no `#test` functions → ignore the gate, build the production program.

    // --- backend (F2): lower the checked merged program to C, build it natively ---
    char *stem = cstr_of(m.name);
    int rc = tk_backend(dir, stem, prog, out_dir, m);
    tk_free0(stem);
    return rc;
}

// =========================================================================
// Eixo D — the PROJECT RUN entry (`teko run <dir>`): the debug profile. Same front-end
// as the build, but INTERPRET the checked merged tree on the VM (tk_vm_run) instead of
// codegen → cc. The process exit code is the virtual-main's (a panic exits non-zero from
// inside the VM with a "teko: panic: …" message).
// =========================================================================
int tk_run_project(const char *dir) {
    tk_tprogram prog;
    tk_manifest m;
    int rc = project_frontend(dir, &prog, &m, false);
    if (rc != 0) return rc;

    return tk_vm_run(prog);
}

// =========================================================================
// D2 — the PROJECT TEST entry (`teko test <dir>`). Same front-end as run, but ASSEMBLED WITH
// the `.tkt` test files, then the VM runs every `#test` function (tk_vm_run_tests) instead of
// the virtual-main. Fail-fast: a failed assertion panics from inside the VM (non-zero exit).
// (Mirrors project.tks test_project.)
// =========================================================================
int tk_test_project(const char *dir, bool gen_cov) {
    tk_tprogram prog;
    tk_manifest m;
    int rc = project_frontend(dir, &prog, &m, true);
    if (rc != 0) return rc;

    // `--coverage` → record branches + write <cwd>/cobertura.xml (project root; no `-o` for `teko test`).
    return gen_cov ? tk_vm_run_tests_cov(prog, true, true, "cobertura.xml") : tk_vm_run_tests(prog);
}
