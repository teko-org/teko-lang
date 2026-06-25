// src/parser/parse_file.c   (namespace 'teko::parser')
//
// File-level parsing, the C23 mirror of parser/parse_file.tks.
#include "parse_file.h"
#include "parse_path.h"   // parse_path
#include "parse_stmt.h"   // tk_parse_statement
#include "cursor.h"       // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "ast.h"          // tk_uses_push, tk_stmts_push

static bool is_decl_start(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_FN) || tk_is_kind_at(t, n, pos, TK_TOKEN_TYPE);
}

static tk_parsed_use_result parse_use(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_path_result pp = parse_path(t, n, pos + 1);
    if (!pp.ok) { return (tk_parsed_use_result){ .ok = false, .as.error = pp.as.error }; }
    bool has_alias = false; tk_str alias = (tk_str){0}; size_t p = pp.as.value.next;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_AS)) {
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_use_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected a name after `as` in a `use`") };
        }
        has_alias = true; alias = t[p + 1].text; p += 2;
    }
    tk_use_decl u = { .path = pp.as.value.node, .has_alias = has_alias, .alias = alias };
    return (tk_parsed_use_result){ .ok = true, .as.value = { .node = u, .next = p } };
}

tk_parsed_uses_result parse_use_header(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos);
    tk_use_decl *uses = NULL; size_t nu = 0;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_USE)) {
        tk_parsed_use_result u = parse_use(t, n, p);
        if (!u.ok) { return (tk_parsed_uses_result){ .ok = false, .as.error = u.as.error }; }
        tk_uses_push(&uses, &nu, u.as.value.node);
        p = u.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_uses_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';' or a newline after a `use`") };
        }
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_uses_result){ .ok = true, .as.value = { .uses = uses, .n_uses = nu, .next = p } };
}

tk_parsed_main_file_result tk_parse_main_file(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) { return (tk_parsed_main_file_result){ .ok = false, .as.error = hdr.as.error }; }
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_statement *body = NULL; size_t nb = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) { break; }
        if (is_decl_start(t, n, p)) {
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_err_at(t, n, p, "main.tks is a virtual main: it may not declare types or functions (only `use` + statements)") };
        }
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) { return (tk_parsed_main_file_result){ .ok = false, .as.error = s.as.error }; }
        tk_stmts_push(&body, &nb, s.as.value.node);
        p = s.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_main_file_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';' or a newline after a statement") };
        }
        p = tk_skip_seps(t, n, p);
    }
    tk_main_file mf = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .body = body, .n_body = nb };
    return (tk_parsed_main_file_result){ .ok = true, .as.value = { .node = mf, .next = p } };
}
