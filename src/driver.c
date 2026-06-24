// src/driver.c — the Teko bootstrap DRIVER (F1: path-to-first-binary).
//
// Wires read → lex → parse → reconcile → check end-to-end. This is the milestone
// where `tekoc <file.tks>` first processes real Teko: the front-end pipeline runs
// and reports its verdict. Codegen/emit (F2) is NOT here — success == "type-checked".
#include "driver.h"

#include "lexer/lexer.h"     // tk_tokenize, tk_tokens_result
#include "parser/parser.h"   // tk_parse_main_file, tk_parse_module
#include "parser/result.h"   // tk_parsed_main_file_result, tk_parsed_module_result
#include "checker/typer.h"   // tk_type_program, tk_tprogram_result
#include "codegen/codegen_c.h" // tk_emit_c, tk_cstr_result (F2 backend)

#include <stdio.h>           // fopen/fread/fclose, fprintf, printf
#include <stdlib.h>          // malloc, realloc, free
#include <string.h>          // strrchr, strcmp, memcpy

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
    tk_byte *buf = malloc(n == 0 ? 1 : n);   // never malloc(0)
    if (buf == NULL) { fclose(f); return io_err("out of memory reading file"); }

    size_t got = fread(buf, 1, n, f);
    fclose(f);
    if (got != n) { free(buf); return io_err("short read on file"); }

    // The validated door from bytes to a str (UTF-8 FORCED — B.36). The buffer is
    // owned by the returned str's view for the lifetime of the compile (process-
    // lifetime in the bootstrap; no free needed — M.5 arena-style).
    tk_str_result s = tk_str_from_utf8(buf, n);
    if (!s.ok) { free(buf); }
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
        tk_item *np = realloc(b.ptr, ncap * sizeof(tk_item));
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
// B1d — the driver. read → lex → parse → reconcile → check.
// =========================================================================
static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int fail(const char *path, const char *message) {
    fprintf(stderr, "tekoc: %s: %s\n", path, message);
    return 1;
}

// =========================================================================
// B2 — the BACKEND wiring (F2): write the emitted C next to the input, invoke the
// host `cc`, produce the native binary. The output stem drops a trailing ".tks".
// =========================================================================

// Derive "<dir>/<stem>" (no extension) from the input path — heap-allocated.
static char *output_stem(const char *path) {
    size_t n = strlen(path);
    // strip a trailing ".tks" if present.
    size_t base = n;
    if (n >= 4 && strcmp(path + n - 4, ".tks") == 0) base = n - 4;
    char *out = malloc(base + 1);
    if (out == NULL) abort();
    memcpy(out, path, base);
    out[base] = '\0';
    return out;
}

// Run the host C compiler over `cfile`, producing `binary`. Returns 0 on success.
static int run_cc(const char *cfile, const char *binary) {
    // cc -std=c23 <file.c> -o <binary>  — quote paths to tolerate spaces.
    size_t cap = strlen(cfile) + strlen(binary) + 64;
    char *cmd = malloc(cap);
    if (cmd == NULL) abort();
    snprintf(cmd, cap, "cc -std=c23 \"%s\" -o \"%s\"", cfile, binary);
    int rc = system(cmd);
    free(cmd);
    return rc;
}

// Lower → write .c → invoke cc → report. Returns 0 on success.
static int tk_backend(const char *path, tk_tprogram prog) {
    tk_cstr_result emitted = tk_emit_c(prog);
    if (!emitted.ok) return fail(path, emitted.as.error.message);

    char *stem = output_stem(path);

    // The emitted C goes to "<stem>.c".
    size_t clen = strlen(stem) + 3;
    char *cfile = malloc(clen);
    if (cfile == NULL) abort();
    snprintf(cfile, clen, "%s.c", stem);

    FILE *f = fopen(cfile, "wb");
    if (f == NULL) { free(emitted.as.value); free(stem); free(cfile); return fail(path, "cannot write generated C"); }
    size_t srclen = strlen(emitted.as.value);
    size_t wrote = fwrite(emitted.as.value, 1, srclen, f);
    fclose(f);
    free(emitted.as.value);
    if (wrote != srclen) { free(stem); free(cfile); return fail(path, "short write on generated C"); }

    int rc = run_cc(cfile, stem);
    free(cfile);
    if (rc != 0) { free(stem); return fail(path, "cc failed to build the generated C"); }

    printf("tekoc: %s: built %s\n", path, stem);
    free(stem);
    return 0;
}

int tk_compile(const char *path) {
    // --- read (B1a) ---
    tk_str_result src = tk_read_file(path);
    if (!src.ok) return fail(path, src.as.error.message);

    // --- lex ---
    tk_tokens_result toks = tk_tokenize(src.as.value);
    if (!toks.ok) return fail(path, toks.as.error.message);
    const tk_token *t = toks.as.value.ptr;
    size_t n = toks.as.value.len;

    // --- parse + reconcile (entry chosen by basename — the .tkp decision is B1b) ---
    tk_program program;
    if (strcmp(basename_of(path), "main.tks") == 0) {
        tk_parsed_main_file_result pr = tk_parse_main_file(t, n, 0);
        if (!pr.ok) return fail(path, pr.as.error.message);
        program = tk_main_file_to_program(pr.as.value.node);
    } else {
        tk_parsed_module_result pr = tk_parse_module(t, n, 0);
        if (!pr.ok) return fail(path, pr.as.error.message);
        program = tk_module_to_program(pr.as.value.node);
    }

    // --- check ---
    tk_tprogram_result checked = tk_type_program(program);
    if (!checked.ok) return fail(path, checked.as.error.message);

    // --- backend (F2): lower the checked typed program to C, build it natively ---
    return tk_backend(path, checked.as.value);
}
