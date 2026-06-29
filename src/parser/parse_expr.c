// src/parser/parse_expr.c   (namespace 'teko::parser')
//
// Expression parsing — the full precedence ladder, the C23 mirror of
// parser/parse_expr.tks. Same productions, precedence, associativity, and error
// messages as the Teko source. Recursive children are heap-boxed (tk_box_expr).
//
// W4a — STRUCT LITERALS `Name { field = value, … }`. The whole ladder threads a
// `allow_struct` flag: a struct literal is NOT recognized in the scrutinee position of
// `if`/`match` (where a `{` opens the block, not a literal) — the classic ambiguity,
// resolved Rust-style (LEGISLATION/CORRECTION_PLAN §9; M.1 no-ambiguity + M.2 legible).
// The restriction propagates through operators but RESETS inside `(` … `)` and call args
// (a delimiter makes the `{` unambiguous again). Public entries: tk_parse_expr (struct
// allowed) and tk_parse_expr_no_struct (the scrutinee form).
#include "parse_expr.h"
#include "parse_if.h"      // parse_if    (called by parse_atom)
#include "parse_match.h"   // parse_match (called by parse_atom)
#include "parse_path.h"    // parse_path
#include "parse_type.h"    // parse_type_primary (cast target)
#include "parse_lit.h"     // tk_lit_int, tk_lit_byte
#include "cursor.h"        // tk_has_token, tk_is_kind_at, tk_is_sep, tk_skip_seps
#include "optokens.h"      // tk_is_unary, tk_is_shift, …
#include "ast.h"           // tk_box_expr, tk_exprs_push, tk_strvec_push, tk_terms_push
#include "../lexer/lexer.h" // tk_tokenize (each interpolation hole is re-lexed + parsed)
#include "../text/text.h"  // tk_str_from_utf8 (build decoded literal pieces)
#include <stdlib.h>        // malloc, abort
#include <string.h>        // (str spans)

// the ladder is internal + flagged; the two public entries set the flag (bottom of file).
static tk_parsed_result parse_expr_a(const tk_token *t, size_t n, size_t pos, bool as_);
static tk_parsed_args_result parse_in_elems(const tk_token *t, size_t n, size_t pos);            // [ e0, … ] elements (`in` set only)
static tk_parsed_array_elems_result parse_array_elems(const tk_token *t, size_t n, size_t pos);  // [ e0, ..e1, … ] array literal elements (C6.7)

// (C1-POS/E1) stamp a freshly-built node with its FIRST-token source position. A node's
// first token is always `t[pos]` at its parser function's ENTRY: atom leaves start there,
// postfix wrappers start at the receiver, unary at the operator, and a left-associative
// binary chain starts at its left operand. Additive — an unstamped node keeps 0 (unknown),
// which the checker treats as "fall back to the item position", exactly as before.
static inline tk_expr tk_at(tk_expr e, const tk_token *t, size_t pos) {
    e.line = t[pos].line; e.col = t[pos].col; return e;
}

static tk_parsed_args_result parse_call_args(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);             // consume `(`, skip leading newlines (multi-line args)
    tk_expr *args = NULL; size_t na = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = 0, .next = p + 1 } };
    }
    for (;;) {
        tk_parsed_result a = parse_expr_a(t, n, p, true);   // inside `(` … `)` — struct literals allowed
        if (!a.ok) { return (tk_parsed_args_result){ .ok = false, .as.error = a.as.error }; }
        tk_exprs_push(&args, &na, a.as.value.node);
        p = tk_skip_seps(t, n, a.as.value.next);         // a `,` / `)` may sit on the next line (multi-line args)
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p = tk_skip_seps(t, n, p + 1);                   // consume `,` (+ optional newlines before the next arg)
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) { break; }   // trailing comma before `)`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RPAREN)) {
        return (tk_parsed_args_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ')' to close the argument list") };
    }
    return (tk_parsed_args_result){ .ok = true, .as.value = { .args = args, .n_args = na, .next = p + 1 } };
}

// `Name { field = value (, | ; | newline)* }` — a struct VALUE literal (W4a). `pos` is at `{`.
// Field VALUES parse with struct allowed (they sit inside the literal's braces).
// (W9.4) `type_args`/`nargs` are the explicit construction-site generic args (NULL/0 for the bare
// `Name { … }` form). `pos` is at the `{`.
static tk_parsed_result parse_struct_lit(const tk_token *t, size_t n, size_t pos, tk_path type_path, tk_type_expr *type_args, size_t nargs) {
    size_t p = tk_skip_seps(t, n, pos + 1);             // consume `{`, skip leading separators
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) {     // empty literal `Name { }`
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_STRUCT_LIT, .as.struct_lit = { .type_path = type_path, .type_args = type_args, .nargs = nargs, .field_names = NULL, .field_vals = NULL, .nfields = 0 } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p + 1 } };
    }
    tk_str *names = NULL; size_t nn = 0;
    tk_expr *vals = NULL; size_t nv = 0;
    for (;;) {
        if (!tk_is_name_at(t, n, p)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected a field name in a struct literal") };
        }
        tk_str fname = t[p].text;
        if (!tk_is_kind_at(t, n, p + 1, TK_TOKEN_ASSIGN)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected '=' after a field name in a struct literal") };
        }
        tk_parsed_result v = parse_expr_a(t, n, p + 2, true);
        if (!v.ok) { return v; }
        tk_strvec_push(&names, &nn, fname);
        tk_exprs_push(&vals, &nv, v.as.value.node);
        p = v.as.value.next;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { p = tk_skip_seps(t, n, p + 1); }       // `,` (+ optional newlines)
        else if (tk_is_sep(t, n, p))               { p = tk_skip_seps(t, n, p); }            // `;` / newline
        else if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }
        else return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ',', ';', a newline, or '}' after a struct-literal field") };
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACE)) { break; }                              // trailing separator
    }
    tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_STRUCT_LIT, .as.struct_lit = { .type_path = type_path, .type_args = type_args, .nargs = nargs, .field_names = names, .field_vals = vals, .nfields = nn } }, t, pos);
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p + 1 } };
}

// ---- string interpolation `$"…{expr}…"` (self-host parity) ----
// A small growable byte buffer for a DECODED literal piece (the same escape set the lexer's
// read_str decodes — \n \t \r \\ \" \' \0). Local to this TU.
typedef struct { tk_byte *ptr; size_t len; size_t cap; } tk_piece_buf;
static void piece_push(tk_piece_buf *b, tk_byte c) {
    if (b->len == b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 16;
        tk_byte *np = tk_realloc0(b->ptr, ncap); if (!np) abort();
        b->ptr = np; b->cap = ncap;
    }
    b->ptr[b->len++] = c;
}
// finalize a piece buffer into a fresh tk_str (decoded bytes are valid by construction).
static tk_str piece_to_str(tk_piece_buf *b) {
    tk_byte *buf = NULL;
    if (b->len > 0) { buf = tk_alloc(b->len); if (!buf) abort(); memcpy(buf, b->ptr, b->len); }
    tk_str_result r = tk_str_from_utf8(buf, b->len);
    if (!r.ok) abort();
    tk_free0(b->ptr); b->ptr = NULL; b->len = 0; b->cap = 0;
    return r.as.value;
}

// decode `\` + one byte into the piece buffer (mirrors lexer.c escape_byte); returns the
// index AFTER the escape, or 0 to signal a malformed escape (the caller errors).
static size_t piece_escape(tk_str raw, size_t i, tk_piece_buf *b, bool *ok) {
    *ok = true;
    if (i + 1 >= raw.len) { *ok = false; return i; }
    tk_byte e = raw.ptr[i + 1];
    tk_byte v;
    switch (e) {
        case 'n':  v = 0x0A; break;
        case 't':  v = 0x09; break;
        case 'r':  v = 0x0D; break;
        case '\\': v = 0x5C; break;
        case '"':  v = 0x22; break;
        case '\'': v = 0x27; break;
        case '0':  v = 0x00; break;
        default: *ok = false; return i;
    }
    piece_push(b, v);
    return i + 2;
}

// Parse a `$"…"` interpolation token's RAW inner text (`raw`) into the tk_interp node:
//   literal pieces (escapes decoded) interleaved with hole expressions.
// Each `{…}` hole is RE-LEXED (tk_tokenize on the hole's source substring) and parsed as a
// FULL Teko expression (tk_parse_expr). npieces == nholes + 1.
static tk_parsed_result parse_interp(const tk_token *t, size_t n, size_t pos) {
    tk_str raw = t[pos].text;
    tk_str   *pieces = NULL; size_t npieces = 0;
    tk_expr  *holes  = NULL; size_t nholes  = 0;
    tk_piece_buf cur = { NULL, 0, 0 };
    size_t i = 0;
    for (;;) {
        if (i >= raw.len) break;
        tk_byte c = raw.ptr[i];
        if (c == '\\') {
            bool ok; size_t ni = piece_escape(raw, i, &cur, &ok);
            if (!ok) { tk_free0(cur.ptr); tk_free0(pieces); tk_free0(holes);
                return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "unknown escape in interpolated string") }; }
            i = ni; continue;
        }
        if (c == '{') {
            // close the literal piece BEFORE this hole.
            tk_strvec_push(&pieces, &npieces, piece_to_str(&cur));
            // scan to the matching `}` (depth-tracking), recording the hole's source span.
            size_t hs = i + 1; size_t depth = 1; size_t j = hs;
            for (; j < raw.len; j += 1) {
                tk_byte hc = raw.ptr[j];
                if (hc == '{') depth += 1;
                else if (hc == '}') { depth -= 1; if (depth == 0) break; }
            }
            if (j >= raw.len) { tk_free0(cur.ptr); tk_free0(pieces); tk_free0(holes);
                return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "unterminated `{` in interpolated string") }; }
            tk_str hole_src = tk_str_slice(raw, hs, j);
            // re-lex + parse the hole as a FULL expression.
            tk_tokens_result tr = tk_tokenize(hole_src);
            if (!tr.ok) { tk_free0(pieces); tk_free0(holes);
                return (tk_parsed_result){ .ok = false, .as.error = tr.as.error }; }
            tk_parsed_result he = tk_parse_expr(tr.as.value.ptr, tr.as.value.len, 0);
            if (!he.ok) { tk_tokens_free(tr.as.value); tk_free0(pieces); tk_free0(holes); return he; }
            tk_exprs_push(&holes, &nholes, he.as.value.node);
            i = j + 1;                                  // past the `}`
            continue;
        }
        if (c == '}') {                                 // a stray closing brace (no open hole)
            tk_free0(cur.ptr); tk_free0(pieces); tk_free0(holes);
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "unexpected `}` in interpolated string") };
        }
        piece_push(&cur, c);
        i += 1;
    }
    // the trailing literal piece (after the last hole, or the whole string if no holes).
    tk_strvec_push(&pieces, &npieces, piece_to_str(&cur));
    tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_INTERP, .as.interp = { .pieces = pieces, .npieces = npieces, .holes = holes, .nholes = nholes } }, t, pos);
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
}

static tk_parsed_result parse_atom(const tk_token *t, size_t n, size_t pos, bool as_) {
    if (!tk_has_token(t, n, pos)) {
        return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected an expression") };
    }
    tk_token_kind k = t[pos].kind;
    if (k == TK_TOKEN_NUMBER) {
        tk_str txt = t[pos].text;
        tk_expr e;
        if (tk_lit_is_float(txt)) {
            e = tk_at((tk_expr){ .tag = TK_EXPR_NUMBER, .as.number = { .is_float = true, .fval = tk_lit_float(txt) } }, t, pos);
        } else {
            e = tk_at((tk_expr){ .tag = TK_EXPR_NUMBER, .as.number = { .is_float = false, .value = tk_lit_int(txt) } }, t, pos);
        }
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_STR) {
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_STR, .as.str = { .text = t[pos].text } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_INTERP) {
        return parse_interp(t, n, pos);   // $"…{expr}…" — split the raw text into pieces + hole exprs
    }
    if (k == TK_TOKEN_BYTE) {
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_BYTE, .as.byte = { .value = tk_lit_byte(t[pos].text) } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_TRUE || k == TK_TOKEN_FALSE) {
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_BOOL, .as.boolean = { .value = (k == TK_TOKEN_TRUE) } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_NULL) {
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_NULL }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pos + 1 } };
    }
    if (k == TK_TOKEN_LPAREN) {
        tk_parsed_result in = parse_expr_a(t, n, pos + 1, true);   // inside `(` … `)` — struct literals allowed
        if (!in.ok) { return in; }
        if (!tk_is_kind_at(t, n, in.as.value.next, TK_TOKEN_RPAREN)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, in.as.value.next, "expected ')' to close a parenthesized expression") };
        }
        return (tk_parsed_result){ .ok = true, .as.value = { .node = in.as.value.node, .next = in.as.value.next + 1 } };
    }
    if (k == TK_TOKEN_LBRACKET) {
        // `[ e0, ..e1, e2, … ]` — an array/slice literal (Increment B+, C6.7 spread). Each
        // element may be prefixed with `..` to mark it as a spread (is_spread=true). The checker
        // unifies non-spread element types and flattens spread operands.
        tk_parsed_array_elems_result aer = parse_array_elems(t, n, pos);
        if (!aer.ok) { return (tk_parsed_result){ .ok = false, .as.error = aer.as.error }; }
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_ARRAY, .as.array = {
            .elements = aer.as.value.elems, .nelements = aer.as.value.n_elems } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = aer.as.value.next } };
    }
    if (k == TK_TOKEN_IF)    { return parse_if(t, n, pos); }
    if (k == TK_TOKEN_MATCH) { return parse_match(t, n, pos); }
    if (tk_is_name_at(t, n, pos)) {   // IDENT or a contextual keyword (`type`/`to`) used as a value/path
        tk_parsed_path_result pp = parse_path(t, n, pos);
        if (!pp.ok) { return (tk_parsed_result){ .ok = false, .as.error = pp.as.error }; }
        if (tk_is_kind_at(t, n, pp.as.value.next, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, pp.as.value.next);
            if (!ca.ok) { return (tk_parsed_result){ .ok = false, .as.error = ca.as.error }; }
            tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_CALL, .as.call = { .callee = pp.as.value.node,
                .args = ca.as.value.args, .nargs = ca.as.value.n_args } }, t, pos);
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = ca.as.value.next } };
        }
        // (W9.4) explicit type-args at construction `Name<i64> { … }` — SPECULATIVE: parse `<…>`,
        // and commit to a typed struct literal ONLY when a `{` follows the closed list (so `a < b`
        // and a pending `>>` mid-close still backtrack to comparison). Requires `as_` like the bare form.
        if (as_ && tk_is_kind_at(t, n, pp.as.value.next, TK_TOKEN_LT)) {
            tk_parsed_type_args_result ta = tk_parse_type_args(t, n, pp.as.value.next);
            if (ta.ok && ta.as.value.pending_gt == 0 && tk_is_kind_at(t, n, ta.as.value.next, TK_TOKEN_LBRACE)) {
                return parse_struct_lit(t, n, ta.as.value.next, pp.as.value.node, ta.as.value.args, ta.as.value.nargs);
            }
        }
        // W4a — a struct literal `Name { … }` (only when allowed: not in an if/match scrutinee).
        if (as_ && tk_is_kind_at(t, n, pp.as.value.next, TK_TOKEN_LBRACE)) {
            return parse_struct_lit(t, n, pp.as.value.next, pp.as.value.node, NULL, 0);
        }
        if (pp.as.value.node.len == 1) {
            tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_VAR, .as.var = { .name = pp.as.value.node.segments[0].name } }, t, pos);
            return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pp.as.value.next } };
        }
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_PATH, .as.path = { .path = pp.as.value.node } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = pp.as.value.next } };
    }
    return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, pos, "expected an expression") };
}

static tk_parsed_result parse_postfix(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result prim = parse_atom(t, n, pos, as_);
    if (!prim.ok) { return prim; }
    tk_expr node = prim.as.value.node;
    size_t p = prim.as.value.next;
    for (;;) {
        // `[index]` subscript (W5-idx). The index is a FULL expression (struct literals
        // allowed inside the brackets — a delimiter makes the `{` unambiguous, like call
        // args / `(` … `)`). Wraps the current node as the receiver and continues the loop,
        // so `a[i][j]`, `a[i].f`, `a.f[i]` all chain. str→byte / []T→T (typed in the checker).
        if (tk_is_kind_at(t, n, p, TK_TOKEN_LBRACKET)) {
            tk_parsed_result idx = parse_expr_a(t, n, p + 1, true);   // inside `[` … `]` — struct literals allowed
            if (!idx.ok) { return idx; }
            if (!tk_is_kind_at(t, n, idx.as.value.next, TK_TOKEN_RBRACKET)) {
                return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, idx.as.value.next, "expected ']' to close a subscript index") };
            }
            tk_expr ix = tk_at((tk_expr){ .tag = TK_EXPR_INDEX, .as.index = { .receiver = tk_box_expr(node), .index = tk_box_expr(idx.as.value.node) } }, t, pos);
            node = ix; p = idx.as.value.next + 1;
            continue;
        }
        // `?.` safe field access (null-propagating — REBOOT_PLAN §203). Field-only:
        // `?.name(` does NOT form a safe method call in the seed (the chain stops at
        // the safe field, then `(` is handled at a higher level if/when it applies).
        if (tk_is_kind_at(t, n, p, TK_TOKEN_QDOT)) {
            if (!tk_is_name_at(t, n, p + 1)) {
                return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected a field name after '?.'") };
            }
            tk_str name = t[p + 1].text;
            tk_expr sf = tk_at((tk_expr){ .tag = TK_EXPR_SAFE_FIELD_ACCESS,
                .as.safe_field_access = { .receiver = tk_box_expr(node), .field = name } }, t, pos);
            node = sf; p = p + 2;
            continue;
        }
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_DOT)) { break; }
        if (!tk_is_name_at(t, n, p + 1)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, p + 1, "expected a field or method name after '.'") };
        }
        tk_str name = t[p + 1].text;
        if (tk_is_kind_at(t, n, p + 2, TK_TOKEN_LPAREN)) {
            tk_parsed_args_result ca = parse_call_args(t, n, p + 2);
            if (!ca.ok) { return (tk_parsed_result){ .ok = false, .as.error = ca.as.error }; }
            tk_expr m = tk_at((tk_expr){ .tag = TK_EXPR_METHOD_CALL, .as.method_call = { .receiver = tk_box_expr(node),
                .method = name, .args = ca.as.value.args, .nargs = ca.as.value.n_args } }, t, pos);
            node = m; p = ca.as.value.next;
        } else {
            tk_expr f = tk_at((tk_expr){ .tag = TK_EXPR_FIELD_ACCESS, .as.field_access = { .receiver = tk_box_expr(node), .field = name } }, t, pos);
            node = f; p = p + 2;
        }
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_unary(const tk_token *t, size_t n, size_t pos, bool as_) {
    if (tk_is_unary(t, n, pos)) {
        tk_token_kind op = t[pos].kind;
        tk_parsed_result o = parse_unary(t, n, pos + 1, as_);
        if (!o.ok) { return o; }
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_UNARY, .as.unary = { .op = op, .operand = tk_box_expr(o.as.value.node) } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = o.as.value.next } };
    }
    return parse_postfix(t, n, pos, as_);
}

static tk_parsed_result parse_cast(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_unary(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_kind_at(t, n, p, TK_TOKEN_TO)) {
        tk_parsed_type_result ty = parse_type_primary(t, n, p + 1);   // target is a type-PRIMARY
        if (!ty.ok) { return (tk_parsed_result){ .ok = false, .as.error = ty.as.error }; }
        tk_expr c = tk_at((tk_expr){ .tag = TK_EXPR_CAST, .as.cast = { .expr = tk_box_expr(node), .target = ty.as.value.node } }, t, pos);
        node = c; p = ty.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_shift(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_cast(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_shift(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_cast(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_expr b = tk_at((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_multiplicative(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_shift(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_multiplicative(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_shift(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_expr b = tk_at((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_additive(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_multiplicative(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_additive(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_multiplicative(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_expr b = tk_at((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = { .op = op, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

// `[ e0, ..e1, e2, … ]` — array literal element collector (C6.7). `pos` is at `[`. Each element
// may be prefixed with `..` (TK_TOKEN_DOTDOT) to produce a spread element (is_spread=true). After
// consuming `..` the operand is parsed at primary/unary level (parse_unary) to avoid swallowing the
// comma separator. Non-spread elements are parsed at the full expression level with struct literals
// allowed. Empty `[]` is valid (zero-element array). Newline-tolerant like call args.
static tk_parsed_array_elems_result parse_array_elems(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);              // consume `[`, skip leading newlines
    tk_array_elem *elems = NULL; size_t ne = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) {     // empty `[]`
        return (tk_parsed_array_elems_result){ .ok = true, .as.value = { .elems = elems, .n_elems = 0, .next = p + 1 } };
    }
    for (;;) {
        bool is_spread = false;
        if (tk_is_kind_at(t, n, p, TK_TOKEN_DOTDOT)) {   // `..expr` spread prefix (C6.7)
            is_spread = true;
            p = tk_skip_seps(t, n, p + 1);               // consume `..`, skip optional newlines
        }
        // spread operand: parse at unary level (avoids consuming the comma separator);
        // non-spread: parse at full expression level with struct literals allowed.
        tk_parsed_result a = is_spread
            ? parse_unary(t, n, p, false)
            : parse_expr_a(t, n, p, true);
        if (!a.ok) { return (tk_parsed_array_elems_result){ .ok = false, .as.error = a.as.error }; }
        tk_array_elem ae = { .is_spread = is_spread, .expr = tk_box_expr(a.as.value.node) };
        tk_array_elems_push(&elems, &ne, ae);
        p = tk_skip_seps(t, n, a.as.value.next);
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p = tk_skip_seps(t, n, p + 1);                   // consume `,`
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) { break; }  // trailing comma
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) {
        return (tk_parsed_array_elems_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ']' to close the array literal") };
    }
    return (tk_parsed_array_elems_result){ .ok = true, .as.value = { .elems = elems, .n_elems = ne, .next = p + 1 } };
}

// `[ e0, e1, … ]` — the membership SET, the RHS of `in` (Phase 2). `pos` is at `[`. Empty `[]`
// allowed → always false. Comma- and separator-tolerant (newlines, like multi-line call args).
// Elements parse with struct literals ALLOWED (a `[`/`,` delimiter makes a trailing `{` unambiguous
// again, like call args / `(` … `)`). Returns the elements + the position after `]`.
static tk_parsed_args_result parse_in_elems(const tk_token *t, size_t n, size_t pos) {
    size_t p = tk_skip_seps(t, n, pos + 1);             // consume `[`, skip leading newlines (multi-line set)
    tk_expr *elems = NULL; size_t ne = 0;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) {    // empty set `[]` → always false
        return (tk_parsed_args_result){ .ok = true, .as.value = { .args = elems, .n_args = 0, .next = p + 1 } };
    }
    for (;;) {
        tk_parsed_result a = parse_expr_a(t, n, p, true);   // inside `[` … `]` — struct literals allowed
        if (!a.ok) { return (tk_parsed_args_result){ .ok = false, .as.error = a.as.error }; }
        tk_exprs_push(&elems, &ne, a.as.value.node);
        p = tk_skip_seps(t, n, a.as.value.next);         // a `,` / `]` may sit on the next line
        if (!tk_is_kind_at(t, n, p, TK_TOKEN_COMMA)) { break; }
        p = tk_skip_seps(t, n, p + 1);                   // consume `,` (+ optional newlines before the next element)
        if (tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) { break; }   // trailing comma before `]`
    }
    if (!tk_is_kind_at(t, n, p, TK_TOKEN_RBRACKET)) {
        return (tk_parsed_args_result){ .ok = false, .as.error = tk_err_at(t, n, p, "expected ']' to close the `in` membership set") };
    }
    return (tk_parsed_args_result){ .ok = true, .as.value = { .args = elems, .n_args = ne, .next = p + 1 } };
}

static tk_parsed_result parse_comparison(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_additive(t, n, pos, as_);
    if (!first.ok) { return first; }
    // `<expr> in [ … ]` — membership test (Phase 2), at comparison precedence and NON-chaining.
    // The LHS is the just-parsed operand; the RHS is the special bracketed set. The `[` is
    // REQUIRED after `in` (there is no general array literal). The node is stamped at the LHS's
    // first token (`t[pos]`, this function's entry — C1-POS).
    if (tk_is_kind_at(t, n, first.as.value.next, TK_TOKEN_IN)) {
        size_t ip = first.as.value.next + 1;             // past `in`
        if (!tk_is_kind_at(t, n, ip, TK_TOKEN_LBRACKET)) {
            return (tk_parsed_result){ .ok = false, .as.error = tk_err_at(t, n, ip, "expected '[' to begin the `in` membership set") };
        }
        tk_parsed_args_result set = parse_in_elems(t, n, ip);
        if (!set.ok) { return (tk_parsed_result){ .ok = false, .as.error = set.as.error }; }
        tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_IN, .as.in_expr = {
            .lhs = tk_box_expr(first.as.value.node), .elems = set.as.value.args, .nelems = set.as.value.n_args } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = set.as.value.next } };
    }
    tk_cmp_term *terms = NULL; size_t nt = 0;
    size_t p = first.as.value.next;
    while (tk_is_comparison(t, n, p)) {
        tk_token_kind op = t[p].kind;
        tk_parsed_result rhs = parse_additive(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_terms_push(&terms, &nt, (tk_cmp_term){ .op = op, .operand = tk_box_expr(rhs.as.value.node) });
        p = rhs.as.value.next;
    }
    if (nt == 0) {
        return (tk_parsed_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
    }
    tk_expr e = tk_at((tk_expr){ .tag = TK_EXPR_COMPARE, .as.compare = { .first = tk_box_expr(first.as.value.node), .rest = terms, .nrest = nt } }, t, pos);
    return (tk_parsed_result){ .ok = true, .as.value = { .node = e, .next = p } };
}

static tk_parsed_result parse_and(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_comparison(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_andand(t, n, p)) {
        tk_parsed_result rhs = parse_comparison(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_expr b = tk_at((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_ANDAND, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

static tk_parsed_result parse_or(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_and(t, n, pos, as_);
    if (!first.ok) { return first; }
    tk_expr node = first.as.value.node;
    size_t p = first.as.value.next;
    while (tk_is_oror(t, n, p)) {
        tk_parsed_result rhs = parse_and(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)
        if (!rhs.ok) { return rhs; }
        tk_expr b = tk_at((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = { .op = TK_TOKEN_OROR, .left = tk_box_expr(node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        node = b; p = rhs.as.value.next;
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = node, .next = p } };
}

// level 9 (the loosest) — `??` Elvis / null-coalescing (REBOOT_PLAN §203). Below `||`.
// Right-associative: `a ?? b ?? c` is `a ?? (b ?? c)` (the standard coalescing shape).
static tk_parsed_result parse_coalesce(const tk_token *t, size_t n, size_t pos, bool as_) {
    tk_parsed_result first = parse_or(t, n, pos, as_);
    if (!first.ok) { return first; }
    size_t p = first.as.value.next;
    if (tk_is_kind_at(t, n, p, TK_TOKEN_QQ)) {
        tk_parsed_result rhs = parse_coalesce(t, n, tk_skip_seps(t, n, p + 1), as_);   // skip newlines after a trailing infix operator (line continuation)   // right-assoc → recurse
        if (!rhs.ok) { return rhs; }
        tk_expr c = tk_at((tk_expr){ .tag = TK_EXPR_COALESCE, .as.coalesce = {
            .left = tk_box_expr(first.as.value.node), .right = tk_box_expr(rhs.as.value.node) } }, t, pos);
        return (tk_parsed_result){ .ok = true, .as.value = { .node = c, .next = rhs.as.value.next } };
    }
    return (tk_parsed_result){ .ok = true, .as.value = { .node = first.as.value.node, .next = p } };
}

static tk_parsed_result parse_expr_a(const tk_token *t, size_t n, size_t pos, bool as_) {
    return parse_coalesce(t, n, pos, as_);   // descends the whole ladder (coalesce → or → …)
}

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos) {
    return parse_expr_a(t, n, pos, true);    // default: struct literals allowed
}

// the scrutinee form (B.20/B.15): a trailing `{` opens the if/match block, NOT a struct
// literal — so struct literals are suppressed at the top level of the condition/subject.
tk_parsed_result tk_parse_expr_no_struct(const tk_token *t, size_t n, size_t pos) {
    return parse_expr_a(t, n, pos, false);
}
