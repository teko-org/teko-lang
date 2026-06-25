// src/parser/cursor.c   (namespace 'teko::parser')
//
// Token-cursor predicates, the C23 mirror of parser/cursor.tks. The C cursor carries
// the token slice explicitly as (t, n, pos).
//
// NOTE: cursor.tks also declares `kind_at`, `expect`, and `skip_terminators`; the C
// seed never realized those (the parser uses is_kind_at + is_sep/skip_seps instead),
// so they have no C body here — not invented (faithful redistribution only).
#include "cursor.h"
#include "../core.h"          // tk_error, tk_error_make
#include <stdint.h>
#include <stdio.h>            // snprintf
#include <stdlib.h>           // malloc, abort
#include <string.h>           // strlen

bool tk_has_token(const tk_token *t, size_t n, size_t pos) {
    (void)t;
    return pos < n;
}

// a parse error located at token `pos`: "line:col: msg" from the token's stamped position.
// At/past end-of-input, use the last token's location. Unstamped (line 0) → bare message.
tk_error tk_err_at(const tk_token *t, size_t n, size_t pos, const char *msg) {
    uint32_t line = 0, col = 0;
    if (n > 0) { size_t i = pos < n ? pos : n - 1; line = t[i].line; col = t[i].col; }
    if (line == 0) return tk_error_make(msg);
    size_t len = strlen(msg) + 32;
    char *buf = malloc(len); if (!buf) abort();
    snprintf(buf, len, "%u:%u: %s", line, col, msg);
    return tk_error_make(buf);
}

bool tk_is_kind_at(const tk_token *t, size_t n, size_t pos, tk_token_kind k) {
    if (!tk_has_token(t, n, pos)) { return false; }
    return t[pos].kind == k;
}

bool tk_is_sep(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_SEMICOLON) || tk_is_kind_at(t, n, pos, TK_TOKEN_NEWLINE);
}

// a name position: an identifier OR the `type` contextual keyword (a keyword only in
// declaration position; an ordinary name as a field/member). Both carry the name in `.text`.
bool tk_is_name_at(const tk_token *t, size_t n, size_t pos) {
    return tk_is_kind_at(t, n, pos, TK_TOKEN_IDENT) || tk_is_kind_at(t, n, pos, TK_TOKEN_TYPE);
}

size_t tk_skip_seps(const tk_token *t, size_t n, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (!tk_is_sep(t, n, p)) { break; }
        p += 1;
    }
    return p;
}
