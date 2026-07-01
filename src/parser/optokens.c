// src/parser/optokens.c   (namespace 'teko::parser')
//
// Operator-class token predicates, the C23 mirror of parser/optokens.tks.
#include "optokens.h"
#include "cursor.h"   // tk_has_token

bool tk_is_unary(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_MINUS || k == TK_TOKEN_TILDE || k == TK_TOKEN_BANG;
}

bool tk_is_shift(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_SHL || k == TK_TOKEN_SHR;
}

bool tk_is_multiplicative(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_STAR || k == TK_TOKEN_SLASH || k == TK_TOKEN_PERCENT || k == TK_TOKEN_AMP;
}

// (2026-07-01) `~` joins additive precedence — arity-polymorphic like `-`: unary `~x` is
// bitwise NOT, binary `a ~ b` is string concat (NOT commutative, honest at the same level as `-`).
bool tk_is_additive(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_PLUS || k == TK_TOKEN_MINUS || k == TK_TOKEN_PIPE || k == TK_TOKEN_CARET ||
           k == TK_TOKEN_TILDE;
}

bool tk_is_comparison(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_LT || k == TK_TOKEN_GT || k == TK_TOKEN_LE ||
           k == TK_TOKEN_GE || k == TK_TOKEN_EQEQ || k == TK_TOKEN_NE;
}

bool tk_is_andand(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == TK_TOKEN_ANDAND;
}

bool tk_is_oror(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == TK_TOKEN_OROR;
}

bool tk_is_assign_op(const tk_token *t, size_t n, size_t pos) {
    if (!tk_has_token(t, n, pos)) { return false; }
    tk_token_kind k = t[pos].kind;
    return k == TK_TOKEN_ASSIGN ||
           k == TK_TOKEN_PLUSEQ  || k == TK_TOKEN_MINUSEQ ||
           k == TK_TOKEN_STAREQ  || k == TK_TOKEN_SLASHEQ ||
           k == TK_TOKEN_PERCENTEQ ||
           k == TK_TOKEN_AMPEQ   || k == TK_TOKEN_PIPEEQ  ||
           k == TK_TOKEN_CARETEQ ||
           k == TK_TOKEN_SHLEQ   || k == TK_TOKEN_SHREQ;
}
