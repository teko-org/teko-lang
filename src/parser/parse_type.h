// src/parser/parse_type.h   (namespace 'teko::parser')
//
// Type-expression parsing, the C23 mirror of parser/parse_type.tks. tk_parse_type is
// the public entry; parse_type_primary is shared (parse_cast in parse_expr targets a
// type-PRIMARY). parse_named / parse_slice stay file-local in parse_type.c.
#ifndef TK_PARSER_PARSE_TYPE_H
#define TK_PARSER_PARSE_TYPE_H

#include "result.h"   // tk_parsed_type_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_type_result parse_type_primary(const tk_token *t, size_t n, size_t pos);  // a type PRIMARY (named or []T)
tk_parsed_type_result tk_parse_type     (const tk_token *t, size_t n, size_t pos);  // a full type (unions via `|`)
// (W9.4) parse a generic type-ARGUMENT list `<T1, T2, …>` — `pos` is at the `<`. Returns ok=false to
// SIGNAL BACKTRACK when it is not a closed list (so a bare `<` stays available as comparison). Shared
// by the struct-construction and match-bind-pattern sites; handles the `>>` (Shr) split via pending_gt.
tk_parsed_type_args_result tk_parse_type_args(const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_TYPE_H
