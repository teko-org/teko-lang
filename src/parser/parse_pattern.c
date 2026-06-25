// src/parser/parse_pattern.c   (namespace 'teko::parser')
//
// Pattern parsing, the C23 mirror of parser/parse_pattern.tks.
#include "parse_pattern.h"
#include "parse_path.h"   // parse_path
#include "parse_lit.h"    // tk_lit_int, tk_lit_byte
#include "cursor.h"       // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "ast.h"          // tk_pats_push, tk_strvec_push

static tk_parsed_pattern_result parse_pattern_primary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected a pattern") };
    }
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_UNDERSCORE) {
        tk_pattern p = { .tag = TK_PAT_WILDCARD };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_NUMBER) {
        tk_str txt = t[pos].text;
        tk_expr v;
        if (tk_lit_is_float(txt)) {
            v = (tk_expr){ .tag = TK_EXPR_NUMBER, .as.number = { .is_float = true, .fval = tk_lit_float(txt) } };
        } else {
            v = (tk_expr){ .tag = TK_EXPR_NUMBER, .as.number = { .is_float = false, .value = tk_lit_int(txt) } };
        }
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_STR) {
        tk_expr v = { .tag = TK_EXPR_STR, .as.str = { .text = t[pos].text } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_BYTE) {
        tk_expr v = { .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } };
        tk_pattern p = { .tag = TK_PAT_LITERAL, .as.literal = { .value = v } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_IDENT) {
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) { return (tk_parsed_pattern_result){ .ok = false, .as.error = pp.as.error }; }
        size_t after = pp.as.value.next;
        if (tk_is_kind_at(t, n, after, TK_TOKEN_AS)) {
            if (!tk_is_kind_at(t, n, after + 1, TK_TOKEN_IDENT)) {
                return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_err_at(t, n, after + 1, "expected a name after `as` in a pattern") };
            }
            tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = true, .binding = t[after + 1].text } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after + 2 } };
        }
        if (tk_is_kind_at(t, n, after, TK_TOKEN_LBRACE)) {
            tk_parsed_names_result fns = parse_field_names(t, n, after);
            if (!fns.ok) { return (tk_parsed_pattern_result){ .ok = false, .as.error = fns.as.error }; }
            tk_pattern fp = { .tag = TK_PAT_FIELD, .as.field = { .type_name = pp.as.value.node, .fields = fns.as.value.names, .n_fields = fns.as.value.n_names } };
            return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = fp, .next = fns.as.value.next } };
        }
        tk_pattern p = { .tag = TK_PAT_BIND, .as.bind = { .type_name = pp.as.value.node, .has_binding = false, .binding = (tk_str){0} } };
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = after } };
    }
    return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected a pattern") };
}

static tk_parsed_pattern_result parse_pattern_range(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result lo = parse_pattern_primary(t, n, pos);
    if (!lo.ok) { return lo; }
    if (!tk_is_kind_at(t, n, lo.as.value.next, TK_TOKEN_DOTDOTEQ)) { return lo; }
    if (lo.as.value.node.tag != TK_PAT_LITERAL) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "a range bound must be a literal") };
    }
    tk_expr lo_e = lo.as.value.node.as.literal.value;
    tk_parsed_pattern_result hi = parse_pattern_primary(t, n, lo.as.value.next + 1);
    if (!hi.ok) { return hi; }
    if (hi.as.value.node.tag != TK_PAT_LITERAL) {
        return (tk_parsed_pattern_result){ .ok = false, .as.error = tk_err_at(t, n, lo.as.value.next + 1, "a range bound must be a literal") };
    }
    tk_pattern p = { .tag = TK_PAT_RANGE, .as.range = { .lo = lo_e, .hi = hi.as.value.node.as.literal.value } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = p, .next = hi.as.value.next } };
}

tk_parsed_pattern_result tk_parse_pattern(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result first = parse_pattern_range(t, n, pos);
    if (!first.ok) { return first; }
    tk_pattern *opts = NULL; size_t no = 0;
    tk_pats_push(&opts, &no, first.as.value.node);
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_PIPE)) {
        tk_parsed_pattern_result o = parse_pattern_range(t, n, p + 1);
        if (!o.ok) { return o; }
        tk_pats_push(&opts, &no, o.as.value.node);
        p = o.as.value.next;
    }
    if (no == 1) {
        return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    }
    tk_pattern alt = { .tag = TK_PAT_ALT, .as.alt = { .options = opts, .n_options = no } };
    return (tk_parsed_pattern_result){ .ok = true, .as.value = { .node = alt, .next = p } };
}

// `{ f; g }` — a field-name list (parse_pattern.tks). `pos` is at `{`.
tk_parsed_names_result parse_field_names(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_str *names = NULL; size_t nn = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {
        return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = 0, .next = p + 1 } };
    }
    for (;;) {
        if (!tk_is_name_at(t, n, p)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a field name in `Type { … }`") };
        }
        tk_strvec_push(&names, &nn, t[p].text);
        p += 1;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_names_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';', a newline, or '}' after a field name") };
        }
        p = tk_skip_seps(t, n, p);
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }   // trailing separator
    }
    return (tk_parsed_names_result){ .ok = true, .as.value = { .names = names, .n_names = nn, .next = p + 1 } };   // consume `}`
}
