// src/driver.c — the Teko bootstrap DRIVER (F1: path-to-first-binary).
//
// Wires read → lex → parse → reconcile → check end-to-end. This is the milestone
// where `teko <file.tks>` first processes real Teko: the front-end pipeline runs
// and reports its verdict. Codegen/emit (F2) is NOT here — success == "type-checked".
#include "driver.h"

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

#include <stdio.h>           // fopen/fread/fclose, fprintf, printf
#include <stdlib.h>          // malloc, realloc, free
#include <string.h>          // strrchr, strcmp, memcpy
#include <unistd.h>          // chdir (run the project path from its own root — A3)
#include <sys/stat.h>        // mkdir — the native build output dir (./bin)
#include <dirent.h>          // opendir/readdir — find the single *.tkp manifest

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
// (C1.8/E2) RICH DIAGNOSTIC RENDERER — a host edge (its .tks mirror is driver.tks/main.tks,
// both already noted as deferred host edges; the structured tk_error fields are the E2 carve-out,
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

// Run the host C compiler over `cfile`, producing `binary`. Returns 0 on success.
static int run_cc(const char *cfile, const char *binary) {
    // F3: the generated C does `#include "teko_rt.h"` and calls tk_print/tk_println
    // (teko_rt.c) plus teko__assert__* (the teko::assert seed, src/assert/assert.c), so
    // the host cc must see the runtime dir (-I) and compile BOTH seed sources (M.5 — one
    // reuse-the-host-toolchain cc invocation, no extra build system).
    //   cc -std=c23 -I"<rt>" -I"<src>/assert" "<file.c>" "<rt>/teko_rt.c" \
    //      "<src>/assert/assert.c" -o "<bin>"
    // The generated C does `#include "assert.h"`, so cc must also see src/assert (-I).
    // Quote every path to tolerate spaces. cap covers the fixed text + both rt-dir copies
    // (the -I and the source path) + two src-dir copies (the -I and assert.c) + suffixes.
    size_t cap = strlen(cfile) + strlen(binary) + 2 * strlen(TK_RT_DIR)
               + 2 * strlen(TK_SRC_DIR) + strlen("/assert") + strlen("/teko_rt.c")
               + strlen("/assert/assert.c") + 96;   // fixed flags incl. -w/-std/-ferror-limit + quotes
    char *cmd = tk_alloc(cap);
    if (cmd == NULL) abort();
    // -w: silence warnings on generated C (it is machine output — its parenthesization,
    // exhaustiveness and literal widths are the codegen's contract, not the user's to read).
    // -ferror-limit=0 still shows every genuine ERROR (a real codegen bug must surface).
    snprintf(cmd, cap,
             "cc -std=c23 -w -ferror-limit=0 -I\"%s\" -I\"%s/assert\" \"%s\" \"%s/teko_rt.c\" \"%s/assert/assert.c\" -lm -o \"%s\"",
             TK_RT_DIR, TK_SRC_DIR, cfile, TK_RT_DIR, TK_SRC_DIR, binary);
    int rc = system(cmd);
    tk_free0(cmd);
    return rc;
}

// Lower → write .c → invoke cc → report. `stem` is the output path (no extension);
// `label` is what the diagnostics name (the project dir/name). Returns 0 on success.
static int tk_backend(const char *label, const char *stem, tk_tprogram prog, const char *out_dir) {
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
    fclose(f);
    tk_free0(emitted.as.value);
    if (wrote != srclen) { tk_free0(cfile); tk_free0(binp); return fail(label, "short write on generated C"); }

    int rc = run_cc(cfile, binp);
    tk_free0(cfile);
    if (rc != 0) { tk_free0(binp); return fail(label, "cc failed to build the generated C"); }

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

static int project_frontend(const char *dir, tk_tprogram *out, tk_manifest *manifest_out) {
    // Enter the project root so relative source paths resolve (the contained host edge).
    if (chdir(dir) != 0) return fail(dir, "cannot enter project directory");

    // --- find + read + parse the manifest (the single <name>.tkp at the root — A1) ---
    char mname[256];
    if (!find_manifest(mname, sizeof mname))
        return fail(dir, "no single .tkp manifest in project directory");
    tk_str_result mtext = tk_read_file(mname);
    if (!mtext.ok) return fail(dir, "cannot read .tkp manifest");
    tk_manifest_result mr = tk_parse_manifest(mtext.as.value);
    if (!mr.ok) return fail(dir, mr.as.error.message);
    tk_manifest m = mr.as.value;

    // --- discover the source tree (A2) ---
    tk_source_files_result df = tk_discover(m.name, m.source);
    if (!df.ok) return fail(dir, df.as.error.message);
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

    // --- report the discovered file → namespace map (the project's surface) ---
    printf("teko: project '%.*s' (source=%.*s) — %zu file(s):\n",
           (int)m.name.len, (const char *)m.name.ptr,
           (int)m.source.len, (const char *)m.source.ptr, files.len);
    for (size_t i = 0; i < files.len; i += 1)
        printf("  %.*s\t-> %.*s\n",
               (int)files.ptr[i].path.len,      (const char *)files.ptr[i].path.ptr,
               (int)files.ptr[i].namespace.len, (const char *)files.ptr[i].namespace.ptr);

    // --- assemble: read+parse every file, MERGE into ONE program (A3) ---
    tk_program_result asm_r = tk_assemble(files);
    if (!asm_r.ok) return fail("", asm_r.as.error.message);   // assemble bakes file:line:col into the message
    tk_program program = asm_r.as.value;

    // --- check the WHOLE merged program (M.1 — whole program checked together) ---
    tk_tprogram_result checked = tk_type_program(program);
    if (!checked.ok) return fail_diag(checked.as.error);   // (C1.8) located header + source snippet/caret + expected/actual

    printf("teko: %s: project assembled (%zu items) and type-checked OK\n",
           dir, program.len);
    *out = checked.as.value;
    *manifest_out = m;
    return 0;
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
    tk_tprogram prog;
    tk_manifest m;
    int rc = project_frontend(dir, &prog, &m);
    if (rc != 0) return rc;

    // --- backend (F2): lower the checked merged program to C, build it natively ---
    char *stem = cstr_of(m.name);
    rc = tk_backend(dir, stem, prog, out_dir);
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
    int rc = project_frontend(dir, &prog, &m);
    if (rc != 0) return rc;

    return tk_vm_run(prog);
}
