// src/parser/parse_decl.h   (namespace 'teko::parser')
//
// Declaration parsing, the C23 mirror of parser/parse_decl.tks: functions, type decls,
// and the module entry. tk_parse_function / tk_parse_type_decl / tk_parse_module are the
// public entries; parse_params / parse_fields / parse_type_body / parse_decl stay
// file-local in parse_decl.c.
#ifndef TK_PARSER_PARSE_DECL_H
#define TK_PARSER_PARSE_DECL_H

#include "result.h"   // tk_parsed_decl_result, tk_parsed_module_result
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_decl_result   tk_parse_function (const tk_token *t, size_t n, size_t pos, bool is_test, tk_str os_guard, bool allow_receiver);   // OOP A1 (2026-07-01) — allow_receiver=true ONLY for a struct-body method
tk_parsed_decl_result   tk_parse_type_decl(const tk_token *t, size_t n, size_t pos);
tk_parsed_module_result tk_parse_module   (const tk_token *t, size_t n, size_t pos);

#endif // TK_PARSER_PARSE_DECL_H
