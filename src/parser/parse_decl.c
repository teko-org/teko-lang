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
        return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_name_at(t, n, p)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a parameter name") };
        }
        tk_str name = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_COLON)) {
            return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected ':' after a parameter name") };
        }
        tk_parsed_type_result ty = tk_parse_type(t, n, p + 2);
        if (!ty.ok) { return (tk_parsed_params_result){ .ok = false, .as.error = ty.as.error }; }
        tk_params_push(&params, &np, (tk_param){ .name = name, .type_ann = ty.as.value.node });
        p = ty.as.value.next;
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p += 1;                                          // consume `,`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_params_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ')' to close the parameter list") };
    }
    return (tk_parsed_params_result){ .ok = true, .as.value = { .params = params, .n_params = np, .next = p + 1 } };
}

tk_parsed_decl_result tk_parse_function(const tk_token *t, size_t n, size_t pos, bool is_test) {
    size_t p = pos;
    bool has_doc = false; tk_str doc = (tk_str){0};
    if (tk_is_kind_at(t, n, p, TK_TOKEN_DOC)) { has_doc = true; doc = t[p].text; p += 1; }
    tk_visibility vis = TK_VIS_PRIVATE;                              // default: own-namespace only
    if (tk_is_kind_at(t, n, p, TK_TOKEN_PUB))      { vis = TK_VIS_PUB; p += 1; }
    else if (tk_is_kind_at(t, n, p, TK_TOKEN_EXP)) { vis = TK_VIS_EXP; p += 1; }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_FN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `fn`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a function name") };
    }
    tk_str name = t[p].text; uint32_t name_line = t[p].line, name_col = t[p].col; p += 1;   // W-loc-2: the fn's source position
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
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_LBRACE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '{' for the function body") };
    }
    tk_parsed_block_result blk = tk_parse_block(t, n, p);
    if (!blk.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = blk.as.error }; }
    tk_function f = { .name = name, .params = ps.as.value.params, .nparams = ps.as.value.n_params,
        .has_return = has_return, .return_type = ret,
        .body = blk.as.value.statements, .nbody = blk.as.value.n,
        .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col, .is_test = is_test };
    tk_decl d = { .tag = TK_DECL_FUNCTION, .as.function = f };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = blk.as.value.next } };
}

static tk_parsed_fields_result parse_fields(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_field *fields = NULL; size_t nf = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {
        return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = 0, .next = p + 1 } };
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
    return (tk_parsed_fields_result){ .ok = true, .as.value = { .fields = fields, .n_fields = nf, .next = p + 1 } };
}

static tk_parsed_body_result parse_type_body(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_STRUCT)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_err_at(t, n, pos + 1, "expected '{' after `struct`") };
        }
        tk_parsed_fields_result fs = parse_fields(t, n, pos + 1);
        if (!fs.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = fs.as.error }; }
        tk_type_body b = { .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = fs.as.value.fields, .n_fields = fs.as.value.n_fields } };
        return (tk_parsed_body_result){ .ok = true, .as.value = { .node = b, .next = fs.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_ENUM)) {
        if (!tk_is_kind_at(t, n, pos + 1, TK_TOKEN_LBRACE)) {
            return (tk_parsed_body_result){ .ok = false, .as.error = tk_err_at(t, n, pos + 1, "expected '{' after `enum`") };
        }
        tk_parsed_names_result ms = parse_field_names(t, n, pos + 1);
        if (!ms.ok) { return (tk_parsed_body_result){ .ok = false, .as.error = ms.as.error }; }
        tk_type_body b = { .tag = TK_BODY_ENUM, .as.enum_body = { .members = ms.as.value.names, .n_members = ms.as.value.n_names } };
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
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_TYPE)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected `type`") };
    }
    p += 1;
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_IDENT)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a type name") };
    }
    tk_str name = t[p].text; uint32_t name_line = t[p].line, name_col = t[p].col; p += 1;   // W-loc-2: the type's source position
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_ASSIGN)) {
        return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected '=' in a type declaration") };
    }
    tk_parsed_body_result body = parse_type_body(t, n, p + 1);
    if (!body.ok) { return (tk_parsed_decl_result){ .ok = false, .as.error = body.as.error }; }
    tk_type_decl td = { .name = name, .body = body.as.value.node, .vis = vis, .has_doc = has_doc, .doc = doc, .line = name_line, .col = name_col };
    tk_decl d = { .tag = TK_DECL_TYPE, .as.type_decl = td };
    return (tk_parsed_decl_result){ .ok = true, .as.value = { .node = d, .next = body.as.value.next } };
}

static tk_parsed_decl_result parse_decl(const tk_token *t, size_t n, size_t pos) {
    // optional leading `#test` attribute (D2): `#` `test` then a function (only on functions).
    size_t start = pos;
    bool is_test = false;
    if (tk_is_kind_at(t, n, start, TK_TOKEN_HASH)) {
        if (!tk_is_kind_at(t, n, start + 1, TK_TOKEN_IDENT) || !text_is(t[start + 1].text, "test")) {
            return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, start + 1, "unknown attribute (only `#test` is recognized)") };
        }
        is_test = true; start = tk_skip_seps(t, n, start + 2);   // the attribute may sit on its own line, above the `fn`
    }
    size_t k = start;
    if (tk_is_kind_at(t, n, k, TK_TOKEN_DOC)) { k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_PUB) || tk_is_kind_at(t, n, k, TK_TOKEN_EXP)) { k += 1; }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_FN))   { return tk_parse_function(t, n, start, is_test); }
    if (tk_is_kind_at(t, n, k, TK_TOKEN_TYPE)) {
        if (is_test) { return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "`#test` may only precede a function") }; }
        return tk_parse_type_decl(t, n, start);
    }
    return (tk_parsed_decl_result){ .ok = false, .as.error = tk_err_at(t, n, k, "expected a declaration (`fn`/`type`, optionally `pub`/`exp`/doc); loose statements belong in main.tks") };
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
    tk_module m = { .uses = hdr.as.value.uses, .n_uses = hdr.as.value.n_uses, .decls = decls, .n_decls = nd };
    return (tk_parsed_module_result){ .ok = true, .as.value = { .node = m, .next = p } };
}
