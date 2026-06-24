// src/checker/scope.h — the checker's value environment + injected types.
#ifndef TK_CHECK_SCOPE_H
#define TK_CHECK_SCOPE_H

#include "type.h"
#include "../core.h"          // TK_LIST, TK_RESULT
#include "../parser/ast.h"    // tk_bind_kind (the parser AST owns the binding kinds)

typedef struct { tk_str name; tk_type type; bool is_mut; } tk_val_binding;  // is_mut — B.21
TK_LIST(tk_val_binding, tk_env);        // a flat list; later bindings shadow earlier

TK_RESULT(tk_type, tk_type_result);            // Type | error
TK_RESULT(tk_val_binding, tk_binding_result);  // ValBinding | error (carries is_mut — B.21)
TK_RESULT(tk_env, tk_env_result);              // Env | error (env-threading; used by match.c + the typed pass)

tk_env            tk_env_define(tk_env env, tk_str name, tk_type t, bool is_mut);
tk_binding_result tk_env_lookup_binding(tk_env env, tk_str name);  // the whole binding (mut guard)
tk_type_result    tk_env_lookup(tk_env env, tk_str name);          // the type (thin wrapper)
tk_type_result    tk_builtin_type(tk_str name);
tk_type_result    tk_builtin_fn(tk_str name);       // injected, non-shadowable stdlib fns (print/println, teko::assert::*)
bool              tk_bind_is_mut(tk_bind_kind k);   // k == TK_BIND_MUT (Let/Const immutable — B.21)

#endif // TK_CHECK_SCOPE_H
