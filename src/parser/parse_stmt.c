// src/parser/parse_stmt.c   (namespace 'teko::parser')
//
// Statement + block parsing, the C23 mirror of parser/parse_stmt.tks.
#include "parse_stmt.h"
#include "parse_expr.h"      // tk_parse_expr
#include "parse_type.h"      // tk_parse_type
#include "parse_pattern.h"   // parse_field_names (destructure target)
#include "cursor.h"          // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "optokens.h"        // tk_is_assign_op
#include "ast.h"             // tk_stmts_push

// file-local forward declarations (defined below; tk_parse_statement dispatches to them)
static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos);
static tk_parsed_stmt_result parse_assign (const tk_token *t, size_t n, size_t pos);
static tk_parsed_stmt_result parse_ref_assign(const tk_token *t, size_t n, size_t pos);

tk_parsed_stmt_result tk_parse_statement(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected a statement") };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_RETURN)) {
        size_t after = pos + 1;
        if (!tk_has_token(t, n, after) || tk_is_sep(t, n, after) || tk_is_kind_at(t, n, after, TK_TOKEN_RBRACE)) {
            tk_expr zero = { .tag = TK_EXPR_NUMBER, .as.number = { .value = 0 } };
            tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = false, .value = zero } };
            return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = after } };
        }
        tk_parsed_result v = tk_parse_expr(t, n, after);
        if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
        tk_statement s = { .tag = TK_STMT_RETURN, .as.ret = { .has_value = true, .value = v.as.value.node } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LOOP)) {
        // optional label: `loop NAME { … }` (an IDENT right after `loop`, before the brace)
        tk_str label = { .ptr = NULL, .len = 0 };
        size_t lbrace = pos + 1;
        if (tk_is_kind_at(t, n, pos + 1, TK_TOKEN_IDENT)) { label = t[pos + 1].text; lbrace = pos + 2; }
        if (!tk_is_kind_at(t, n, lbrace, TK_TOKEN_LBRACE)) {
            return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_err_at(t, n, lbrace, "expected '{' after `loop`") };
        }
        tk_parsed_block_result blk = tk_parse_block(t, n, lbrace);
        if (!blk.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = blk.as.error }; }
        tk_statement s = { .tag = TK_STMT_LOOP, .as.loop_stmt = { .label = label, .body = blk.as.value.items, .nbody = blk.as.value.n } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = blk.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_BREAK)) {
        // optional label on the SAME line: `break NAME` (a separator would tokenize between)
        tk_str label = { .ptr = NULL, .len = 0 }; size_t next = pos + 1;
        if (tk_is_kind_at(t, n, pos + 1, TK_TOKEN_IDENT)) { label = t[pos + 1].text; next = pos + 2; }
        tk_statement s = { .tag = TK_STMT_BREAK, .as.jump = { .label = label } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_CONTINUE)) {
        tk_str label = { .ptr = NULL, .len = 0 }; size_t next = pos + 1;
        if (tk_is_kind_at(t, n, pos + 1, TK_TOKEN_IDENT)) { label = t[pos + 1].text; next = pos + 2; }
        tk_statement s = { .tag = TK_STMT_CONTINUE, .as.jump = { .label = label } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_DEFER)) {
        // `defer { stmts }` — a mandatory block must follow immediately (C7.18).
        size_t lbrace = pos + 1;
        if (!tk_is_kind_at(t, n, lbrace, TK_TOKEN_LBRACE)) {
            return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_err_at(t, n, lbrace, "expected '{' after `defer`") };
        }
        tk_parsed_block_result blk = tk_parse_block(t, n, lbrace);
        if (!blk.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = blk.as.error }; }
        tk_statement s = { .tag = TK_STMT_DEFER, .as.defer_stmt = { .body = blk.as.value.items, .nbody = blk.as.value.n } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = blk.as.value.next } };
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LET) || tk_is_kind_at(t, n, pos, TK_TOKEN_MUT) || tk_is_kind_at(t, n, pos, TK_TOKEN_CONST)) {
        return parse_binding(t, n, pos);
    }
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) && tk_is_assign_op(t, n, pos + 1)) {
        return parse_assign(t, n, pos);
    }
    // (MEM-1b-ii) `r.value op= x` — assign THROUGH a reference: `name . value <assign-op>`. The ONLY
    // field-assignment target in value-functional Teko. Matched before the expr-statement fallback.
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT)
        && tk_is_kind_at(t, n, pos + 1, TK_TOKEN_DOT)
        && tk_is_name_at(t, n, pos + 2)
        && t[pos + 2].text.len == 5 && memcmp(t[pos + 2].text.ptr, "value", 5) == 0
        && tk_is_assign_op(t, n, pos + 3)) {
        return parse_ref_assign(t, n, pos);
    }
    // `i++` / `i--` — postfix increment/decrement STATEMENT sugar: desugar to the compound
    // assignment `i += 1` / `i -= 1` (reuses assign typing/codegen/VM; mut-guard included).
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) &&
        (tk_is_kind_at(t, n, pos + 1, TK_TOKEN_PLUSPLUS) || tk_is_kind_at(t, n, pos + 1, TK_TOKEN_MINUSMINUS))) {
        tk_token_kind op = tk_is_kind_at(t, n, pos + 1, TK_TOKEN_PLUSPLUS) ? TK_TOKEN_PLUSEQ : TK_TOKEN_MINUSEQ;
        tk_expr one = { .tag = TK_EXPR_NUMBER, .as.number = { .is_float = false, .value = 1 } };
        tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = t[pos].text, .op = op, .value = one, .deref = false } };
        return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = pos + 2 } };
    }
    tk_parsed_result e = tk_parse_expr(t, n, pos);
    if (!e.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = e.as.error }; }
    tk_statement s = { .tag = TK_STMT_EXPR, .as.expr_stmt = { .expr = e.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = e.as.value.next } };
}

tk_parsed_block_result tk_parse_block(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);   // consume `{`, skip leading separators
    tk_statement *stmts = NULL; size_t ns = 0;
    for (;;) {
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        tk_parsed_stmt_result s = tk_parse_statement(t, n, p);
        if (!s.ok) { return (tk_parsed_block_result){ .ok = false, .as.error = s.as.error }; }
        tk_stmts_push(&stmts, &ns, s.as.value.node);
        p = s.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        if (!tk_is_sep(t, n, p)) {
            return (tk_parsed_block_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ';', a newline, or '}' after a statement") };
        }
        p = tk_skip_seps(t, n, p);
    }
    return (tk_parsed_block_result){ .ok = true, .as.value = { .items = stmts, .n = ns, .next = p + 1 } };   // consume `}`
}

tk_type_expr no_type(void) {
    return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = NULL, .len = 0 } } };
}

// (2026-07-01 DEFARGS) a placeholder tk_expr for tk_param.default_expr when has_default is
// false -- never evaluated, mirrors no_type()'s sentinel convention.
tk_expr no_expr(void) {
    return (tk_expr){ .tag = TK_EXPR_NULL };
}

static tk_annotation_result parse_annotation(const tk_token *t, size_t n, size_t pos) {
    if (!tk_is_kind_at(t, n, pos, TK_TOKEN_COLON)) {
        return (tk_annotation_result){ .ok = true, .as.value = { .has_type = false, .type_ann = no_type(), .next = pos } };
    }
    tk_parsed_type_result ty = tk_parse_type(t, n, pos + 1);
    if (!ty.ok) { return (tk_annotation_result){ .ok = false, .as.error = ty.as.error }; }
    return (tk_annotation_result){ .ok = true, .as.value = { .has_type = true, .type_ann = ty.as.value.node, .next = ty.as.value.next } };
}

static tk_parsed_target_result parse_bind_target(const tk_token *t, size_t n, size_t pos) {
    if (tk_is_kind_at(t, n, pos, TK_TOKEN_LBRACE)) {
        tk_parsed_names_result names = parse_field_names(t, n, pos);
        if (!names.ok) { return (tk_parsed_target_result){ .ok = false, .as.error = names.as.error }; }
        tk_bind_target tgt = { .tag = TK_BIND_DESTRUCTURE, .as.destructure = { .names = names.as.value.items, .nnames = names.as.value.n_names } };
        return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = names.as.value.next } };
    }
    // `let _ = expr` — DISCARD binding: evaluate the value for effect, bind nothing. A SIMPLE
    // target named "_" (the wildcard); codegen lowers it to `(void)(expr);` (no C variable, so
    // repeated `let _` never collides), and it is never read. (B.21 — explicit ignore.)
    if (!tk_is_name_at(t, n, pos) && !tk_is_kind_at(t, n, pos, TK_TOKEN_UNDERSCORE)) {
        return (tk_parsed_target_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected a name, `_`, or `{ … }` after `let`/`mut`/`const`") };
    }
    tk_bind_target tgt = { .tag = TK_BIND_SIMPLE, .as.simple = { .name = t[pos].text } };
    return (tk_parsed_target_result){ .ok = true, .as.value = { .node = tgt, .next = pos + 1 } };
}

static tk_parsed_stmt_result parse_binding(const tk_token *t, size_t n, size_t pos) {
    tk_bind_kind kind = TK_BIND_LET;
    if (t[pos].kind == TK_TOKEN_MUT)   { kind = TK_BIND_MUT; }
    if (t[pos].kind == TK_TOKEN_CONST) { kind = TK_BIND_CONST; }
    tk_parsed_target_result tgt = parse_bind_target(t, n, pos + 1);
    if (!tgt.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = tgt.as.error }; }
    tk_annotation_result ann = parse_annotation(t, n, tgt.as.value.next);
    if (!ann.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = ann.as.error }; }
    if (!tk_is_kind_at(t, n, ann.as.value.next, TK_TOKEN_ASSIGN)) {
        return (tk_parsed_stmt_result){ .ok = false, .as.error = tk_err_at(t, n, ann.as.value.next, "expected '=' in a binding") };
    }
    tk_parsed_result v = tk_parse_expr(t, n, ann.as.value.next + 1);
    if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
    tk_statement s = { .tag = TK_STMT_BINDING, .as.binding = { .kind = kind, .target = tgt.as.value.node,
        .has_type = ann.as.value.has_type, .type_ann = ann.as.value.type_ann, .value = v.as.value.node } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}

static tk_parsed_stmt_result parse_assign(const tk_token *t, size_t n, size_t pos) {
    tk_str name = t[pos].text;
    tk_token_kind op = t[pos + 1].kind;
    tk_parsed_result v = tk_parse_expr(t, n, pos + 2);
    if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
    tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = name, .op = op, .value = v.as.value.node, .deref = false } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}

// (MEM-1b-ii) `name.value op= value` — write THROUGH a reference. `pos` at the ref name; the `.value`
// shape was matched at dispatch (tokens: name . value <assign-op> value).
static tk_parsed_stmt_result parse_ref_assign(const tk_token *t, size_t n, size_t pos) {
    tk_str name = t[pos].text;
    tk_token_kind op = t[pos + 3].kind;   // the assign op AFTER `.value`
    tk_parsed_result v = tk_parse_expr(t, n, pos + 4);
    if (!v.ok) { return (tk_parsed_stmt_result){ .ok = false, .as.error = v.as.error }; }
    tk_statement s = { .tag = TK_STMT_ASSIGN, .as.assign = { .name = name, .op = op, .value = v.as.value.node, .deref = true } };
    return (tk_parsed_stmt_result){ .ok = true, .as.value = { .node = s, .next = v.as.value.next } };
}
