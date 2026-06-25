// src/parser/cursor.h   (namespace 'teko::parser')
//
// Token-cursor predicate DECLARATIONS, the C23 mirror of parser/cursor.tks. The C
// cursor carries the token slice explicitly as (t, n, pos): a tk_token pointer, its
// length, and the current index.
#ifndef TK_PARSER_CURSOR_H
#define TK_PARSER_CURSOR_H

#include "../lexer/token.h"   // tk_token, tk_token_kind

#include <stdbool.h>
#include <stddef.h>           // size_t

bool   tk_has_token (const tk_token *t, size_t n, size_t pos);                  // is there a token at pos?
bool   tk_is_kind_at(const tk_token *t, size_t n, size_t pos, tk_token_kind k); // has_token + kind compare
bool   tk_is_sep    (const tk_token *t, size_t n, size_t pos);                  // `;` or a newline (B.17)
size_t tk_skip_seps (const tk_token *t, size_t n, size_t pos);                  // skip a run of separators
// a NAME position: an identifier, OR a CONTEXTUAL keyword (`type`/`to`) usable as a
// field/param/binding name or a value/path. These are keywords only in their own slot
// (`type Name = …`, `x to T`); as a name they are ordinary identifiers — the corpus relies
// on this (`.type` 162+, `cast_check(from, to)`). The token's `.text` carries the name.
bool   tk_is_name_at(const tk_token *t, size_t n, size_t pos);

// a parse error LOCATED at token `pos` — formats "line:col: msg" from the token's stamped
// position (the file is prepended later, by the driver/assemble that knows it). Falls back
// to a bare message if the position is unstamped. This is how parse errors carry line:col.
tk_error tk_err_at(const tk_token *t, size_t n, size_t pos, const char *msg);

#endif // TK_PARSER_CURSOR_H
