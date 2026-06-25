// src/checker/resolve.h — type resolution + the user-type registry.
#ifndef TK_CHECK_RESOLVE_H
#define TK_CHECK_RESOLVE_H

#include "type.h"
#include "scope.h"
#include "../parser/ast.h"   // tk_type_expr, tk_type_decl, tk_path, … (the parser's AST)

// A user-type registry entry: the type's name, the NAMESPACE it was declared in (A3
// provenance — for W-vis-enforce cross-namespace checks), its VISIBILITY (private/pub/exp),
// and the declaration itself. namespace/vis drive the module-system enforcement pass.
typedef struct { tk_str name; tk_str namespace; tk_visibility vis; tk_type_decl decl; } tk_type_reg;
TK_LIST(tk_type_reg, tk_type_table);

TK_RESULT(tk_type_decl, tk_decl_result);   // TypeDecl | error

// Build a checker error "<msg>: <name>" — names the offending symbol (M.3 — clarify, never vague).
tk_error tk_error_named(const char *msg, tk_str name);

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name);
tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table);
tk_type_result resolve_named(tk_path path, tk_type_table table);   // shared with match.c (C7)
// B.14 — a NAMED type that refers to a `variant` decl → its expanded TK_TYPE_VARIANT (members
// stay NAMED, so it terminates); anything else is returned unchanged. Lets assignability and
// exhaustiveness see a named variant's cases without changing the nominal representation.
tk_type tk_expand_variant(tk_type t, tk_type_table table);

#endif // TK_CHECK_RESOLVE_H
