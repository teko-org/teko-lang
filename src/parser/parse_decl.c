// src/parser/parse_decl.c   (namespace 'teko::parser')
//
// Declaration parsing, the C23 mirror of parser/parse_decl.tks.
#include "parse_decl.h"
#include "parse_type.h"   // tk_parse_type
#include "parse_stmt.h"   // tk_parse_block, no_type
#include "parse_pattern.h"// parse_field_names (enum members)
#include "parse_file.h"   // parse_use_header (module `use` header)
#include "cursor.h"       // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "ast.h"          // tk_params_push, tk_fields_push, tk_decls_push
#include <string.h>       // memcmp, strlen (attribute-name compare)

// does a token's text equal the C string `lit`? (the `#test` attribute-name check.)
static bool text_is(tk_str text, const char *lit) {
    size_t m = strlen(lit);
    return text.len == m && (m == 0 || memcmp(text.ptr, lit, m) == 0);
}

static tk_parsed_params_result parse_params(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos + 1;                                  // consume `(`
    tk_param *params = NULL; size_t np = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_params_result){ .ok = true, .as.value = { .items = params, .n_params = 0, .next = p + 1 } };
    }
    for (;;) {
        // `params` — a CONTEXTUAL variadic-parameter modifier (2026-07-01), like `from` in the
        // `extern` position: NOT a reserved word (295+ existing uses as a plain identifier/field
        // name across the corpus), so it only triggers when `params` is immediately followed by
        // ANOTHER name + `:` — i.e. `params pieces: []T`. A parameter *literally named* `params`
        // (`params: []T`, colon right after) is unaffected. Trailing-only: rejected below if
        // another param follows. The marked param's type must be a slice `[]T`.
        bool has_params_kw = tk_is_kind_at(t, n, p, TK_TOKEN_IDENT) && text_is(t[p].text, "params")
                              && tk_is_name_at(t, n, p + 1) && tk_is_kind_at(t, n, p + 2, TK_TOKEN_COLON);
        if (has_params_kw) { p += 1; }
        if (!tk_is_name_at(t, n, p)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a parameter name") };
        }
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected ':' after a parameter name") };
        }
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) { return (tk_parsed_params_result){ .ok = false, .as.error = ty.as.error }; }
        if (has_params_kw && ty.as.value.node.tag != TK_TEXPR_SLICE) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p + 2, "'params' parameter must have a slice type ([]T)") };
        }
        tk_params_push(&params, &np, (tk_param){ .name = name, .type_ann = ty.as.value.node, .is_params = has_params_kw });
        p = ty.as.value.next;
        if (has_params_kw && tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p, "'params' must be the last parameter") };
        }
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p += 1;                                          // consume `,`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ')' to close the parameter list") };
    }
    return (tk_parsed_params_result){ .ok = true, .as.value = { .items = params, .n_params = np, .next = p + 1 } };
}

// (S4) An OPTIONAL generic type-parameter list `<T, U, …>` after a fn/type NAME (parse_decl.tks
// twin). No `<` → empty list, pos unchanged. Decl-site `<` is unambiguous (the `fn`/`type` keyword
// already committed the parse to a decl); a decl list never nests, so it closes with a single `>`.
static tk_parsed_names_result parse_type_params(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_LT)) {
        return (tk_parsed_names_result){ .ok = true, .as.value = { .items = NULL, .n_names = 0, .next = pos } };
    }
    size_t p = pos + 1;
    tk_str *names = NULL; size_t nn = 0;
    for (;;) {
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a type-parameter name in '<…>'") };
        }
        tk_strvec_push(&names, &nn, t[p].text);
        p += 1;
        // Nullability `?` is NEVER allowed on a type-PARAMETER declaration (legislation 2026-06-28):
        // `?` marks a USE of a type (`a: T?`, `-> T?`), never the param decl `<T?>` (nor a constraint).
        if (tk_is_kind_at(t, n, p, TK_TOKEN_QUESTION)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_err_at(t, n, p, "a nullability `?` is not allowed on a type parameter — use `T?` only in a parameter or return type") };
        }
        if (tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { p += 1; }
        else if (tk_is_kind_at(t, n, p, TK_TOKEN_GT)) { p += 1; break; }
        else { return (tk_parsed_names_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ',' or '>' in a type-parameter list") }; }
    }
    return (tk_parsed_names_result){ .ok = true, .as.value = { .items = names, .n_names = nn, .next = p } };
}

tk_parsed_decl_result tk_parse_function(const tk_token *t, size_t n, size_t pos, bool is_test, tk_str os_guard) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    tk_visibility vis = TK_VIS_PRIVATE;                              // default: own-namespace only
    if (tk_is_kind_at(t, n, p, TK_TOKEN_PUB))      { vis = TK_VIS_PUB; p += 1; }
    else if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { vis = TK_VIS_EXP; p += 1; }
    bool is_extern = false;                                          // C7.1a: `extern fn …` (foreign, no body)
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXTERN)) { is_extern = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `fn`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a function name") };
    }
    tk_str name = t[p].text; uint32_t name_line = t[p].line, name_col = t[p].col; p += 1;   // W-loc-2: the fn's source position
    tk_parsed_names_result tps = parse_type_params(t, n, p);   // (S4) `<T, …>`
    if (!tps.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = tps.as.error }; }
    p = tps.as.value.next;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LPAREN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '(' for the parameter list") };
    }
    tk_parsed_params_result ps = parse_params(t, n, p);
    if (!ps.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = ps.as.error }; }
    p = ps.as.value.next;
    bool has_return = false; tk_type_expr ret = no_type();
    if (tk_is_kind_at(t, n, p, TK_TOKEN_ARROW)) {
        tk_parsed_type_result r = tk_parse_type(t, n, p + 1);
        if (!r.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = r.as.error }; }
        has_return = true; ret = r.as.value.node; p = r.as.value.next;
    }
    if (is_extern) {
        if (is_test) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "`#test` may not precede `extern`") }; }
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN)) {
            return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `= \"symbol\"` to bind the C symbol of an `extern` function") };
        }
        p += 1;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_STR)) {
            return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a quoted C symbol after `=` in an `extern` function") };
        }
        tk_str c_symbol = t[p].text; p += 1;
        tk_str from_lib = (tk_str){0};                              // "" = implicit libc (C7.1a, macOS)
        // `from` is CONTEXTUAL (not a reserved word — the corpus uses it as an identifier), so
        // match it as a plain Ident in the extern position.
        if (tk_is_kind_at(t, n, p, TK_TOKEN_IDENT) && text_is(t[p].text, "from")) {
            p += 1;
            if (!tk_is_kind_at(t, n, p, TK_TOKEN_STR)) {
                return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a quoted library name after `from`") };
            }
            from_lib = t[p].text; p += 1;
        }
        tk_function ef = { .name = name, .type_params = tps.as.value.items, .n_type_params = tps.as.value.n_names, .params = ps.as.value.items, .nparams = ps.as.value.n_params,
            .has_return = has_return, .return_type = ret,
            .body = NULL, .nbody = 0,
            .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col, .is_test = is_test,
            .is_extern = true, .c_symbol = c_symbol, .from_lib = from_lib, .os_guard = os_guard };
        tk_decl ed = { .tag = TK_DECL_FUNCTION, .as.function = ef };
        return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = ed, .next = p } };
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '{' for the function body") };
    }
    tk_parsed_block_result blk = tk_parse_block(t, n, p);
    if (!blk.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = blk.as.error }; }
    tk_function f = { .name = name, .type_params = tps.as.value.items, .n_type_params = tps.as.value.n_names, .params = ps.as.value.items, .nparams = ps.as.value.n_params,
        .has_return = has_return, .return_type = ret,
        .body = blk.as.value.items, .nbody = blk.as.value.n,
        .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col, .is_test = is_test,
        .is_extern = false, .c_symbol = (tk_str){0}, .from_lib = (tk_str){0}, .os_guard = os_guard };
    tk_decl d = { .tag = TK_DECL_FUNCTION, .as.function = f };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = blk.as.value.next } };
}

static tk_parsed_fields_result parse_fields(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_field *fields = NULL; size_t nf = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {
        return (tk_parsed_fields_result){ .ok = true, .as.value = { .items = fields, .n_fields = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_name_at(t, n, p)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a field name") };
        }
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected ':' after a field name") };
        }
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) { return (tk_parsed_fields_result){ .ok = false, .as.error = ty.as.error }; }
        tk_fields_push(&fields, &nf, (tk_field){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_fields_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';', a newline, or '}' after a field") };
        }
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }   // trailing separator
    }
    return (tk_parsed_fields_result){ .ok = true, .as.value = { .items = fields, .n_fields = nf, .next = p + 1 } };
}

static tk_parsed_body_result parse_type_body(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_STRUCT)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_err_at(t, n, pos + 1, "expected '{' after `struct`") };
        }
        tk_parsed_fields_result fs = parse_fields(t, n, pos + 1);
        if (!fs.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = fs.as.error }; }
        tk_type_body b = { .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = fs.as.value.items, .n_fields = fs.as.value.n_fields } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = fs.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_ENUM)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_err_at(t, n, pos + 1, "expected '{' after `enum`") };
        }
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error }; }
        tk_type_body b = { .tag = TK_BODY_ENUM, .as.enum_body = { .members = ms.as.value.items, .n_members = ms.as.value.n_names } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ms.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_FLAGS)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_err_at(t, n, pos + 1, "expected '{' after `flags`") };
        }
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error }; }
        tk_type_body b = { .tag = TK_BODY_FLAGS, .as.flags_body = { .members = ms.as.value.items, .n_members = ms.as.value.n_names } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ms.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_VARIANT)) {
        tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
        if (!ty.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ty.as.error }; }
        tk_type_body b = { .tag = TK_BODY_VARIANT, .as.variant_body = { .type_expr = ty.as.value.node } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = ty.as.value.next } };
    }
    // ELSE: `type Name = <type-expr>` — a TRANSPARENT alias (slice `[]T`, a named type, a
    // union `A | B`, an optional `T?`, …). Parse a full type-expression (self-host parity).
    tk_parsed_type_result al = tk_parse_type(t, n, pos);
    if (!al.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = al.as.error }; }
    tk_type_body b = { .tag = TK_BODY_ALIAS, .as.alias_body = { .alias = al.as.value.node } };
    return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = al.as.value.next } };
}

tk_parsed_decl_result tk_parse_type_decl(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    tk_visibility vis = TK_VIS_PRIVATE;                              // default: own-namespace only
    if (tk_is_kind_at(t, n, p, TK_TOKEN_PUB))      { vis = TK_VIS_PUB; p += 1; }
    else if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { vis = TK_VIS_EXP; p += 1; }
    bool is_extern = false;                                          // C7.1a: `extern type Name` (opaque foreign handle)
    if (tk_is_kind_at(t, n, p, TK_TOKEN_EXTERN)) { is_extern = true; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_TYPE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `type`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a type name") };
    }
    tk_str name = t[p].text; uint32_t name_line = t[p].line, name_col = t[p].col; p += 1;   // W-loc-2: the type's source position
    tk_parsed_names_result tps = parse_type_params(t, n, p);   // (S4) `type Box<T> = …`
    if (!tps.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = tps.as.error }; }
    p = tps.as.value.next;
    if (is_extern) {
        // `extern type Name` — an OPAQUE foreign handle: no `= <body>`, lowers to `void *`.
        tk_type_body eb = { .tag = TK_BODY_EXTERN, .as.extern_body = { 0 } };
        tk_type_decl etd = { .name = name, .type_params = tps.as.value.items, .n_type_params = tps.as.value.n_names, .body = eb, .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col };
        tk_decl ed = { .tag = TK_DECL_TYPE, .as.type_decl = etd };
        return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = ed, .next = p } };
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '=' in a type declaration") };
    }
    tk_parsed_body_result body = parse_type_body(t, n, p + 1);
    if (!body.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = body.as.error }; }
    tk_type_decl td = { .name = name, .type_params = tps.as.value.items, .n_type_params = tps.as.value.n_names, .body = body.as.value.node, .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = body.as.value.next } };
}

// `[doc] [pub|exp] flags Name { Member; Member; … }` (C8.2).
// A standalone bitflag-enum declaration: `flags` keyword + name + member-name list.
// Power-of-2 values are auto-assigned by the checker (C8.3); no `=` and no manual values here.
static tk_parsed_decl_result tk_parse_flags_decl(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    tk_visibility vis = TK_VIS_PRIVATE;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_PUB))      { vis = TK_VIS_PUB; p += 1; }
    else if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { vis = TK_VIS_EXP; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FLAGS)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `flags`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a type name after `flags`") };
    }
    tk_str name = t[p].text; uint32_t name_line = t[p].line, name_col = t[p].col; p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '{' after the flags type name") };
    }
    tk_parsed_names_result ms = parse_field_names(t, n, p);
    if (!ms.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = ms.as.error }; }
    tk_type_body b = { .tag = TK_BODY_FLAGS, .as.flags_body = { .members = ms.as.value.items, .n_members = ms.as.value.n_names } };
    tk_type_decl td = { .name = name, .body = b, .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = ms.as.value.next } };
}

static tk_parsed_decl_result parse_decl(const tk_token *t, size_t n, size_t pos) {
    // optional leading `#test` attribute (D2): `#` `test` then a function (only on functions).
    size_t start = pos;
    bool is_test = false;
    tk_str os_guard = (tk_str){0};
    // optional leading attributes: `#test` (D2) and `#os("…")` (C7.1f) — both function-only.
    for (;;) {
        if (!tk_is_kind_at(t, n, start, TK_TOKEN_HASH)) break;
        if (!tk_is_kind_at(t, n, start + 1, TK_TOKEN_IDENT)) {
            return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 1, "expected an attribute name after `#`") };
        }
        if (text_is(t[start + 1].text, "test")) {
            is_test = true; start = tk_skip_seps(t, n, start + 2);
        } else if (text_is(t[start + 1].text, "os")) {
            // `#os("linux")` — Hash Ident '(' Str ')'.
            if (!tk_is_kind_at(t, n, start + 2, TK_TOKEN_LPAREN)) return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 2, "expected '(' after `#os`") };
            if (!tk_is_kind_at(t, n, start + 3, TK_TOKEN_STR))    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 3, "expected a quoted OS name in `#os(\"…\")`") };
            os_guard = t[start + 3].text;
            if (!tk_is_kind_at(t, n, start + 4, TK_TOKEN_RPAREN)) return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 4, "expected ')' to close `#os(…)`") };
            start = tk_skip_seps(t, n, start + 5);
        } else {
            return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 1, "unknown attribute (only `#test` and `#os(\"…\")` are recognized)") };
        }
    }
    size_t k = start;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_DOC)) { k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_PUB) || tk_is_kind_at(t, n, k, TK_TOKEN_EXP)) { k += 1; }
    bool saw_extern = false;                                        // C7.1a: peek past `extern` to reach `fn`/`type`
    if (tk_is_kind_at(t, n, k, TK_TOKEN_EXTERN)) { saw_extern = true; k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FN))   { return tk_parse_function(t, n, start, is_test, os_guard); }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_TYPE)) {
        if (is_test) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "`#test` may only precede a function") }; }
        if (os_guard.len) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "`#os(\"…\")` may only precede a function") }; }
        return tk_parse_type_decl(t, n, start);   // handles the optional `extern` (→ opaque handle)
    }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FLAGS)) {
        if (is_test) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "`#test` may only precede a function") }; }
        if (os_guard.len) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "`#os(\"…\")` may only precede a function") }; }
        return tk_parse_flags_decl(t, n, start);
    }
    if (saw_extern) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "expected `fn` or `type` after `extern`") };
    }
    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "expected a declaration (`fn`/`type`/`flags`, optionally `pub`/`exp`/doc); loose statements belong in main.tks") };
}

tk_parsed_module_result tk_parse_module(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_uses_result hdr = parse_use_header(t, n, pos);
    if (!hdr.ok) { return (tk_parsed_module_result){ .ok = false, .as.error = hdr.as.error }; }
    size_t p = tk_skip_seps(t, n, hdr.as.value.next);
    tk_decl *decls = NULL; size_t nd = 0;
    for (;;) {
        if (!tk_has_token(t, n, p)) { break; }
        tk_parsed_decl_result d = parse_decl(t, n, p);
        if (!d.ok) { return (tk_parsed_module_result){ .ok = false, .as.error = d.as.error }; }
        tk_decls_push(&decls, &nd, d.as.value.node);
        p = d.as.value.next;
        if (!tk_has_token(t, n, p)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_module_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';' or a newline after a declaration") };
        }
        p = tk_skip_seps(t, n, p);
    }
    tk_module m = { .uses = hdr.as.value.items, .n_uses = hdr.as.value.n_uses, .decls = decls, .n_decls = nd };
    return (tk_parsed_module_result){ .ok = true, .as.value = { .node = m, .next = p } };
}
