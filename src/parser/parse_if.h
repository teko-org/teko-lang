// src/parser/parse_if.h   (namespace 'teko::parser')
//
// `if`/`else` parsing, the C23 mirror of parser/parse_if.tks. if/else IS an expression
// (B.20); parse_atom (parse_expr) dispatches here, so parse_if is extern.
#ifndef TK_PARSER_PARSE_IF_H
#define TK_PARSER_PARSE_IF_H

#include "result.h"   // tk_parsed_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_result parse_if(const tk_token *t, size_t n, size_t pos);

// A branch body beginning at `pos`: a braced block `{ … }`, OR one bracketless statement
// on the NEXT line (separator-gated). The bracketless form lowers to a 1-element block, so
// every consumer treats it like a braced block. SHARED with match-arm bodies (parse_arm.c)
// — an arm body is identical to an `if` then/else branch (B.20). `ctx` is the error shown
// when `pos` is neither a brace nor a separator.
tk_parsed_block_result tk_parse_if_body(const tk_token *t, size_t n, size_t pos, const char *ctx);

#endif // TK_PARSER_PARSE_IF_H
