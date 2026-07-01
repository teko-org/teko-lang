// src/parser/parse_stmt.h   (namespace 'teko::parser')
//
// Statement + block parsing, the C23 mirror of parser/parse_stmt.tks. tk_parse_statement
// and tk_parse_block are public entries; no_type (the Unit placeholder TypeExpr) is
// shared with parse_decl (function return / param defaults). parse_annotation /
// parse_bind_target / parse_binding / parse_assign stay file-local in parse_stmt.c.
#ifndef TK_PARSER_PARSE_STMT_H
#define TK_PARSER_PARSE_STMT_H

#include "result.h"   // tk_parsed_stmt_result, tk_parsed_block_result
#include "type.h"     // tk_type_expr (no_type's return)
#include "../lexer/token.h"

#include <stddef.h>

tk_parsed_stmt_result  tk_parse_statement(const tk_token *t, size_t n, size_t pos);
tk_parsed_block_result tk_parse_block    (const tk_token *t, size_t n, size_t pos);
tk_type_expr           no_type(void);   // the empty (Unit) TypeExpr — shared with parse_decl
tk_expr                no_expr(void);   // (2026-07-01 DEFARGS) a placeholder Expr — shared with parse_decl

#endif // TK_PARSER_PARSE_STMT_H
