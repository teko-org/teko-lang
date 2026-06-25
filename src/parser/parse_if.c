// src/parser/parse_if.c   (namespace 'teko::parser')
//
// `if`/`else` parsing, the C23 mirror of parser/parse_if.tks.
//
// A branch body is EITHER a braced block `{ … }` OR a single bracketless statement
// that begins on the next line (separator-gated): `if cond <newline> <one stmt>` and
// `else <newline> <one stmt>`. The bracketless form lowers to a 1-element block, so the
// checker / VM / codegen treat it exactly like a braced block — bracketless is a pure
// front-end convenience (it simplifies single-line then/else forms).
#include "parse_if.h"
#include "parse_expr.h"   // tk_parse_expr
#include "parse_stmt.h"   // tk_parse_block, tk_parse_statement
#include "cursor.h"       // tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "ast.h"          // tk_box_expr, tk_stmts_push

// A branch body beginning at `pos`: `{ … }`, or one bracketless statement on the next
// line. `ctx` is the error shown when `pos` is neither a brace nor a separator. PUBLIC
// (declared in parse_if.h) so match-arm bodies reuse the EXACT same logic (parse_arm.c) —
// an arm body is identical to an `if` then/else branch (B.20).
tk_parsed_block_result tk_parse_if_body(const tk_token *t, size_t n, size_t pos, const char *ctx) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACE)) {
        return tk_parse_block(t, n, pos);
    }
    if (!tk_is_sep(t, n, pos)) {   // bracketless requires the body on the NEXT line
        return (tk_parsed_block_result){ .ok = false, .as.error = tk_err_at(t, n, pos, ctx) };
    }
    size_t p = tk_skip_seps(t, n, pos);
    tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
    if (!s.ok) { return (tk_parsed_block_result){ .ok = false, .as.error = s.as.error }; }
    tk_statement *one = NULL; size_t n1 = 0;
    tk_stmts_push(&one, &n1, s.as.value.node);
    return (tk_parsed_block_result){ .ok = true, .as.value = { .statements = one, .n = n1, .next = s.as.value.next } };
}

tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos) {
    tk_parsed_result cond = tk_parse_expr_no_struct(t, n, pos + 1);   // trailing `{` is the then-block, not a struct literal (W4a)
    if (!cond.ok) { return cond; }
    tk_parsed_block_result tb = tk_parse_if_body(t, n, cond.as.value.next, "expected '{' or a newline after the `if` condition");
    if (!tb.ok) { return (tk_parsed_result){ .ok = false, .as.error = tb.as.error }; }
    size_t then_next = tb.as.value.next;

    // Peek for `else` across separators, so `else` may sit on its own line (`} \n else`,
    // or after a bracketless then-body). If there is no `else`, do NOT consume the
    // separators — they belong to the enclosing block.
    size_t q = tk_skip_seps(t, n, then_next);
    if (!tk_is_kind_at(t, n, q, TK_TOKEN_ELSE)) {
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
            .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
            .has_else = false, .else_blk = NULL, .nelse = 0 } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = then_next } };
    }

    // `else if …` (same line) — the else branch is another if-expression, one statement.
    if (tk_is_kind_at(t, n, q + 1, TK_TOKEN_IF)) {
        tk_parsed_result elif = parse_if(t, n, q + 1);
        if (!elif.ok) { return elif; }
        tk_statement *eb = NULL; size_t neb = 0;
        tk_statement es = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = elif.as.value.node } };
        tk_stmts_push(&eb, &neb, es);
        tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
            .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
            .has_else = true, .else_blk = eb, .nelse = neb } };
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = elif.as.value.next } };
    }

    // `else { … }` or bracketless `else <newline> <stmt>`.
    tk_parsed_block_result ebk = tk_parse_if_body(t, n, q + 1, "expected '{', `if`, or a newline after `else`");
    if (!ebk.ok) { return (tk_parsed_result){ .ok = false, .as.error = ebk.as.error }; }
    tk_expr e = { .tag = TK_EXPR_IF, .as.if_expr = { .cond = tk_box_expr(cond.as.value.node),
        .then_blk = tb.as.value.statements, .nthen = tb.as.value.n,
        .has_else = true, .else_blk = ebk.as.value.statements, .nelse = ebk.as.value.n } };
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ebk.as.value.next } };
}
