// src/build/assemble.c   (namespace 'teko::build')
//
// Multi-file project ASSEMBLY (crumb A3) — the THIRD step of project compilation
// (after manifest read A1, discovery A2). For each discovered file: read → tokenize →
// parse (main.tks → main-file parser; else module) → reconcile to items → MERGE all
// items into ONE tk_program (M.1 — whole program seen before it is checked).
//
// NAMESPACE TAGGING. Each merged item is stamped with its SourceFile namespace via
// the tk_item.namespace field (added minimally to parser/ast.h for A3). This carries
// provenance for later codegen mangling ONLY; the checker ignores it.
//
// CROSS-NAMESPACE NOTE (M.3 — honest). The checker resolves a call by the LAST path
// segment (expr.c::type_call: `c.callee.segments[c.callee.len - 1].name`) against one
// flat env that tk_collect fills with EVERY function in the merged program. So a call
// `util::greet()` resolves to the bare `greet` registered from another file — i.e. a
// merged multi-namespace program type-checks cross-namespace references TODAY, as long
// as bare names do not collide. The namespace prefix is provenance, not a resolution
// key. (Codegen of a multi-namespace program — mangling those bare names apart — is
// DEFERRED past A3; A3 stops at a CHECKED merged tast, which the VM test-runner D2
// consumes.)
#include "assemble.h"

#include "../driver.h"        // tk_read_file, tk_main_file_to_program, tk_module_to_program
#include "../lexer/lexer.h"   // tk_tokenize, tk_tokens_result
#include "../parser/parser.h" // tk_parse_main_file, tk_parse_module
#include "../parser/result.h" // tk_parsed_main_file_result, tk_parsed_module_result

#include <string.h>           // strrchr, strcmp, memcpy, strlen
#include <stdlib.h>           // malloc, realloc, free, abort
#include <stdio.h>            // snprintf (file-prefixed diagnostics)

// --- a local grow-append over tk_item (the AST header offers no tk_items_push; the
//     driver uses the same local pattern — driver.c::items_push). push CONSUMES/RETURNS.
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

static tk_program_result fail(const char *msg) {
    return (tk_program_result){ .ok = false, .as.error = tk_error_make(msg) };
}

// prefix a per-file diagnostic with its source-file path → "path:msg" (M.3 — name WHERE the
// error is). For a located error msg is "line:col: text", yielding "path:line:col: text".
static const char *diag_file(tk_str path, const char *msg) {
    size_t len = path.len + strlen(msg) + 2;
    char *buf = tk_alloc(len); if (!buf) abort();
    snprintf(buf, len, "%.*s:%s", (int)path.len, (const char *)path.ptr, msg);
    return buf;
}

// Is this file the project ENTRY point? (path basename == "main.tks".) The SourceFile
// path is a str VIEW (not necessarily NUL-terminated), so compare the trailing bytes
// directly rather than via strrchr/strcmp.
static bool is_main_file(tk_str path) {
    static const char suffix[] = "main.tks";
    const size_t slen = sizeof(suffix) - 1;   // 8
    if (path.len < slen) return false;
    const tk_byte *tail = path.ptr + (path.len - slen);
    if (memcmp(tail, suffix, slen) != 0) return false;
    // a bare "main.tks" or one preceded by a path separator (so "domain.tks" is NOT
    // mistaken for the entry — the entry lives at the PROJECT ROOT).
    if (path.len == slen) return true;
    return path.ptr[path.len - slen - 1] == '/';
}

// Read a SourceFile's path (a str view) into a NUL-terminated C string for the host-IO
// boundary (tk_read_file takes a const char*). Heap, process-lifetime (M.5).
static char *path_cstr(tk_str path) {
    char *buf = tk_alloc(path.len + 1);
    if (buf == NULL) abort();
    memcpy(buf, path.ptr, path.len);
    buf[path.len] = '\0';
    return buf;
}

tk_program_result tk_assemble(tk_source_files files) { return tk_assemble_sel(files, false); }

// tk_assemble_sel — the assembly core with an `include_tests` selector. `false` (production
// build/run) SKIPS `.tkt` test files; `true` (the `teko test` path — D2) INCLUDES them so their
// `#test` functions enter the merged program for the VM runner. (Mirrors assemble.tks assemble_sel.)
tk_program_result tk_assemble_sel(tk_source_files files, bool include_tests) {
    items_buf merged = { .ptr = NULL, .len = 0, .cap = 0 };

    for (size_t i = 0; i < files.len; i += 1) {
        tk_source_file sf = files.ptr[i];

        // Tests are NOT production source: a `.tkt` is run on the VM in the test sub-profile
        // (crumb D2). Skip it for a production build/run; include it only for `teko test`.
        bool is_tkt = sf.path.len >= 4 &&
            sf.path.ptr[sf.path.len-4]=='.' && sf.path.ptr[sf.path.len-3]=='t' &&
            sf.path.ptr[sf.path.len-2]=='k' && sf.path.ptr[sf.path.len-1]=='t';
        if (is_tkt && !include_tests) continue;

        char *cpath = path_cstr(sf.path);

        // --- read (M.1 host-IO via driver.c::tk_read_file) ---
        tk_str_result src = tk_read_file(cpath);
        tk_free0(cpath);
        if (!src.ok) { tk_free0(merged.ptr); return fail(src.as.error.message); }

        // --- lex ---
        tk_tokens_result toks = tk_tokenize(src.as.value);
        if (!toks.ok) { tk_free0(merged.ptr); return fail(diag_file(sf.path, toks.as.error.message)); }
        const tk_token *t = toks.as.value.ptr;
        size_t n = toks.as.value.len;

        // --- parse + reconcile (entry → main-file parser; else module) ---
        tk_program one;
        if (is_main_file(sf.path)) {
            tk_parsed_main_file_result pr = tk_parse_main_file(t, n, 0);
            if (!pr.ok) { tk_free0(merged.ptr); return fail(diag_file(sf.path, pr.as.error.message)); }
            one = tk_main_file_to_program(pr.as.value.node);
        } else {
            tk_parsed_module_result pr = tk_parse_module(t, n, 0);
            if (!pr.ok) { tk_free0(merged.ptr); return fail(diag_file(sf.path, pr.as.error.message)); }
            one = tk_module_to_program(pr.as.value.node);
        }

        // (#148 R1) the token buffer dies with this file's parse (the AST holds source views,
        // never token references). This twin's lists are realloc-backed — plain free.
        tk_free0((void *)t);

        // --- merge, tagging every item with this file's namespace (A3 provenance) ---
        for (size_t k = 0; k < one.len; k += 1) {
            tk_item it = one.items[k];
            it.namespace = sf.namespace;
            it.file      = sf.path;        // W-loc-2: tag the source file for checker diagnostics
            merged = items_push(merged, it);
        }
        tk_free0(one.items);   // the per-file program's backing array is copied out
    }

    return (tk_program_result){ .ok = true,
        .as.value = (tk_program){ .items = merged.ptr, .len = merged.len } };
}
