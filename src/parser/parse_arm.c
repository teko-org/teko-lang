// src/parser/parse_arm.c   (namespace 'teko::parser')
//
// Match-arm parsing, the C23 mirror of parser/parse_arm.tks.
#include "parse_arm.h"
#include "parse_pattern.h"   // tk_parse_pattern
#include "parse_expr.h"      // tk_parse_expr
#include "parse_if.h"        // tk_parse_if_body — REUSED for arm bodies (= an `if` branch, B.20)
#include "parse_stmt.h"      // tk_parse_statement (the same-line bracketless arm body)
#include "cursor.h"          // tk_is_kind_at, tk_is_sep
#include "ast.h"             // tk_stmts_push

static tk_guard_result parse_guard(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_WHEN)) {
        tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };   // placeholder, gated by has_when
        return (tk_guard_result){ .ok = true, .as.value = { .has_when = false, .guard = zero, .next = pos } };
    }
    tk_parsed_result g = tk_parse_expr(t, n, pos + 1);
    if (!g.ok) { return (tk_guard_result){ .ok = false, .as.error = g.as.error }; }
    return (tk_guard_result){ .ok = true, .as.value = { .has_when = true, .guard = g.as.value.node, .next = g.as.value.next } };
}

tk_parsed_arm_result tk_parse_arm(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_pattern_result pat = tk_parse_pattern(t, n, pos);
    if (!pat.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = pat.as.error }; }
    tk_guard_result g = parse_guard(t, n, pat.as.value.next);
    if (!g.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = g.as.error }; }
    if (!tk_is_kind_at(t, n, g.as.value.next, TK_TOKEN_FATARROW)) {
        return (tk_parsed_arm_result){ .ok = false, .as.error = tk_err_at(t, n, g.as.value.next, "expected '=>' after a match pattern") };
    }
    // The arm body is a STATEMENT BLOCK, EXACTLY like an `if` then/else branch (B.20): a
    // `{ … }` block, OR one bracketless statement right after `=>` (covers `=> expr`,
    // `=> return x`, `=> break`, `=> continue`). REUSE parse_if_body — but unlike an `if`
    // branch (whose bracketless body sits on the NEXT line), an arm's bracketless body
    // follows `=>` on the SAME line, so pass straight to tk_parse_statement when there is
    // no brace and no separator; defer to tk_parse_if_body for the braced / next-line form.
    size_t bpos = g.as.value.next + 1;
    tk_parsed_block_result body;
    if (tk_is_kind_at(t, n, bpos, TK_TOKEN_LBRACE) || tk_is_sep(t, n, bpos)) {
        body = tk_parse_if_body(t, n, bpos, "expected '{' or a body after '=>'");
    } else {
        tk_parsed_stmt_result s = tk_parse_statement(t, n, bpos);
        if (!s.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = s.as.error }; }
        tk_statement *one = NULL; size_t n1 = 0;
        tk_stmts_push(&one, &n1, s.as.value.node);
        body = (tk_parsed_block_result){ .ok = true, .as.value = { .statements = one, .n = n1, .next = s.as.value.next } };
    }
    if (!body.ok) { return (tk_parsed_arm_result){ .ok = false, .as.error = body.as.error }; }
    tk_arm arm = { .pattern = pat.as.value.node, .has_when = g.as.value.has_when, .guard = g.as.value.guard,
                   .body = body.as.value.statements, .nbody = body.as.value.n };
    return (tk_parsed_arm_result){ .ok = true, .as.value = { .node = arm, .next = body.as.value.next } };
}
