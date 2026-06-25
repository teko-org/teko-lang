// src/parser/parse_match.c   (namespace 'teko::parser')
//
// `match` parsing, the C23 mirror of parser/parse_match.tks.
#include "parse_match.h"
#include "parse_expr.h"   // tk_parse_expr
#include "parse_arm.h"    // tk_parse_arm
#include "cursor.h"       // tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "ast.h"          // tk_box_expr, tk_arms_push

static tk_parsed_arms_result parse_arms(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_arm *arms = NULL; size_t na = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        tk_parsed_arm_result a = tk_parse_arm(t, n, p);
        if (!a.ok) { return (tk_parsed_arms_result){ .ok = false, .as.error = a.as.error }; }
        tk_arms_push(&arms, &na, a.as.value.node);
        p = a.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_arms_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';', a newline, or '}' after a match arm") };
        }
        p = tk_skip_seps(t, n, p);
    }
    if (na == 0) {
        return (tk_parsed_arms_result){ .ok = false, .as.error = tk_err_at(t, n, p, "a `match` needs at least one arm") };
    }
    return (tk_parsed_arms_result){ .ok = true, .as.value = { .arms = arms, .n_arms = na, .next = p + 1 } };
}

tk_parsed_result parse_match(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result subj = tk_parse_expr_no_struct(t, n, pos + 1);   // trailing `{` opens the arms, not a struct literal (W4a)
    if (!subj.ok) { return subj; }
    if (!tk_is_kind_at(t, n, subj.as.value.next, TK_TOKEN_LBRACE)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, subj.as.value.next, "expected '{' after the `match` subject") };
    }
    tk_parsed_arms_result arms = parse_arms(t, n, subj.as.value.next);
    if (!arms.ok) { return (tk_parsed_result){ .ok = false, .as.error = arms.as.error }; }
    tk_expr e = { .tag = TK_EXPR_MATCH, .as.match_expr = { .subject = tk_box_expr(subj.as.value.node),
        .arms = arms.as.value.arms, .narms = arms.as.value.n_arms } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = arms.as.value.next } };
}
