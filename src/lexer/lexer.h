// src/lexer/lexer.h — teko::lexer public interface, the C23 mirror of lexer.tks.
//
// The scanner entry mirrors Teko's `tokenize(source: str) -> []Token | error`.
// The token list is a TK_LIST over tk_token; on success the parser consumes
// `result.value.ptr` / `result.value.len` as its `const tk_token *t, size_t n`.
#ifndef TK_LEXER_LEXER_H
#define TK_LEXER_LEXER_H

#include "../core.h"        // TK_LIST, TK_RESULT, tk_error
#include "../text/text.h"   // tk_str, tk_byte
#include "token.h"          // tk_token, tk_token_kind

// the token list — teko::list realized over tk_token (DAG: lexer → core).
TK_LIST(tk_token, tk_tokens);

// `[]Token | error` — the result of tokenize.
TK_RESULT(tk_tokens, tk_tokens_result);

// tokenize — the main scan loop (lexer.tks `tokenize`). Returns the token list or
// the first lex error.
tk_tokens_result tk_tokenize(tk_str source);

// --- literal decoders (parse_lit.tks `lit_int`/`lit_byte`) ---
// A Number token's text (decimal digits with `_` separators) → i64.
int64_t tk_lit_int(tk_str text);
// A Byte token's text is the already-decoded octet (the lexer resolved it).
tk_byte tk_lit_byte(tk_str text);

#endif // TK_LEXER_LEXER_H
