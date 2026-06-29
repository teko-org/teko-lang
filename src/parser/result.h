// src/parser/result.h — the parser's result TYPES + token-cursor predicate
// DECLARATIONS, the C23 mirror of parser/result.tks (+ parse_file's Parsed* family,
// cursor.tks, optokens.tks).
//
// TYPE-ONLY / declarations-only header (B0c part 1). Every parser returns the parsed
// thing + where parsing resumed (`next`). The cursor predicate BODIES live in
// parser/parser.c (B0c part 2 — next lote), NOT here.
//
// The C cursor model carries the token slice explicitly as (t, n, pos): a `tk_token`
// pointer, its length, and the current index. (The Teko side passes a `[]Token` whose
// `.len` is implicit; the C mirror makes both explicit.)
#ifndef TK_PARSER_RESULT_H
#define TK_PARSER_RESULT_H

#include "ast.h"            // the AST node types every Parsed* wraps
#include "../lexer/token.h" // tk_token, tk_token_kind
#include "../core.h"        // TK_RESULT, tk_error

#include <stdbool.h>
#include <stddef.h>         // size_t

// =========================================================================
// The Parsed* result family (parser/result.tks). Each carries the parsed node (a
// single node uses `node`; a list uses a descriptive field) + the resume index `next`.
// =========================================================================
typedef struct { tk_expr      node;       size_t next; } tk_parsed;          // an expression
typedef struct { tk_statement node;       size_t next; } tk_parsed_stmt;     // a statement
typedef struct { tk_type_expr node;       size_t next; size_t pending_gt; } tk_parsed_type;     // a type expr; pending_gt = extra `>` left by a compound `>>` close (S4 nested generics)
typedef struct { tk_type_expr *args; size_t nargs; size_t next; size_t pending_gt; } tk_parsed_type_args; // (W9.4) a `<T1, T2, …>` arg list at a construction/pattern site; pending_gt = extra `>` left by a `>>` close
typedef struct { tk_decl      node;       size_t next; } tk_parsed_decl;     // a top-level declaration (R-main)
typedef struct { tk_statement *statements; size_t n; size_t next; } tk_parsed_block;  // a `{ … }` block
typedef struct { tk_type_body node;       size_t next; } tk_parsed_body;     // a struct/enum/variant body
typedef struct { tk_pattern   node;       size_t next; } tk_parsed_pattern;  // a match pattern
typedef struct { tk_arm       node;       size_t next; } tk_parsed_arm;      // a match arm
typedef struct { tk_arm      *arms;   size_t n_arms;   size_t next; } tk_parsed_arms;   // a `{ arm; … }` arm list
typedef struct { tk_expr     *args;   size_t n_args;   size_t next; } tk_parsed_args;   // call arguments
typedef struct { tk_param    *params; size_t n_params; size_t next; } tk_parsed_params; // function parameters
typedef struct { tk_bind_target node; size_t next; } tk_parsed_target;       // a binding target
typedef struct { tk_str      *names;  size_t n_names;  size_t next; } tk_parsed_names;  // a `{ … }` field-name list
typedef struct { tk_path      node;       size_t next; } tk_parsed_path;     // an expression path a::b::c
typedef struct { tk_field    *fields; size_t n_fields; size_t next; } tk_parsed_fields; // a struct field list
typedef struct { tk_array_elem *elems; size_t n_elems; size_t next; } tk_parsed_array_elems; // array literal elements (with spread support)
typedef struct { bool has_when; tk_expr guard;        size_t next; } tk_guard;          // an optional `when`
typedef struct { bool has_type; tk_type_expr type_ann; size_t next; } tk_annotation;    // an optional `: T`

// --- file-level results (parse_file.tks: ParsedUse/ParsedUses/ParsedMainFile + module) ---
typedef struct { tk_use_decl  node;  size_t next; }              tk_parsed_use;        // one `use`
typedef struct { tk_use_decl *uses;  size_t n_uses; size_t next; } tk_parsed_uses;     // a `use` header
typedef struct { tk_main_file node;  size_t next; }              tk_parsed_main_file;  // a parsed main.tks
typedef struct { tk_module    node;  size_t next; }              tk_parsed_module;     // a parsed module

// result stamps (Parsed* | error)
TK_RESULT(tk_parsed,            tk_parsed_result);
TK_RESULT(tk_parsed_stmt,       tk_parsed_stmt_result);
TK_RESULT(tk_parsed_type,       tk_parsed_type_result);
TK_RESULT(tk_parsed_type_args,  tk_parsed_type_args_result);
TK_RESULT(tk_parsed_decl,       tk_parsed_decl_result);
TK_RESULT(tk_parsed_block,      tk_parsed_block_result);
TK_RESULT(tk_parsed_body,       tk_parsed_body_result);
TK_RESULT(tk_parsed_pattern,    tk_parsed_pattern_result);
TK_RESULT(tk_parsed_arm,        tk_parsed_arm_result);
TK_RESULT(tk_parsed_arms,       tk_parsed_arms_result);
TK_RESULT(tk_parsed_args,       tk_parsed_args_result);
TK_RESULT(tk_parsed_params,     tk_parsed_params_result);
TK_RESULT(tk_parsed_target,     tk_parsed_target_result);
TK_RESULT(tk_parsed_names,      tk_parsed_names_result);
TK_RESULT(tk_parsed_path,       tk_parsed_path_result);
TK_RESULT(tk_parsed_fields,     tk_parsed_fields_result);
TK_RESULT(tk_parsed_array_elems, tk_parsed_array_elems_result);
TK_RESULT(tk_guard,             tk_guard_result);
TK_RESULT(tk_annotation,        tk_annotation_result);
TK_RESULT(tk_parsed_use,        tk_parsed_use_result);
TK_RESULT(tk_parsed_uses,       tk_parsed_uses_result);
TK_RESULT(tk_parsed_main_file,  tk_parsed_main_file_result);
TK_RESULT(tk_parsed_module,     tk_parsed_module_result);

// (Token-cursor predicate declarations moved to cursor.h / optokens.h — their
//  same-name C pairs of cursor.tks / optokens.tks.)

#endif // TK_PARSER_RESULT_H
