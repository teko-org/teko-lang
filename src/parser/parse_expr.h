// src/parser/parse_expr.h   (namespace 'teko::parser')
//
// Expression parsing, the C23 mirror of parser/parse_expr.tks. Only the public entry
// tk_parse_expr crosses TU boundaries; the whole precedence ladder (parse_or → … →
// parse_atom) stays file-local in parse_expr.c.
#ifndef TK_PARSER_PARSE_EXPR_H
#define TK_PARSER_PARSE_EXPR_H

#include "result.h"   // tk_parsed_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_result tk_parse_expr(const tk_token *t, size_t n, size_t pos);
// the if/match SCRUTINEE form: a trailing `{` opens the block, not a struct literal, so
// top-level struct literals are suppressed (W4a — the struct-literal/block disambiguation).
tk_parsed_result tk_parse_expr_no_struct(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_EXPR_H
