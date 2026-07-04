// src/checker/scope.h — the checker's value environment + injected types.
#ifndef TK_CHECK_SCOPE_H
#define TK_CHECK_SCOPE_H

#include "type.h"
#include "../core.h"          // TK_LIST, TK_RESULT
#include "../parser/ast.h"    // tk_bind_kind (the parser AST owns the binding kinds)

// A value binding: a name + its type. `ns` is the DEFINING namespace of a top-level FUNCTION
// (empty for locals / params / match-binds), used by namespace-aware CALL resolution (#41).
typedef struct { tk_str name; tk_type type; bool is_mut; tk_str ns; } tk_val_binding;  // is_mut — B.21
// The env: a flat binding list (later bindings shadow earlier) PLUS the namespace currently being
// type-checked — so an UNQUALIFIED call resolves to a same-namespace function rather than a global
// bare-name collision (#41). Defined manually (not TK_LIST) so it can carry cur_ns.
// `owner_type` (W10b.CLASS) — the DECLARING class of the method body CURRENTLY being typed (empty
// — not inside any class method). See scope.tks's Env doc for the full rationale.
// (#148) TWO-SEGMENT env: `base`/`base_len` hold the SEALED collected globals (immutable after
// tk_env_seal — shared by every fork, never re-copied), `ptr`/`len` the per-scope locals. The
// copy-on-extend in tk_env_define now copies only the LOCAL segment (a few dozen entries), not
// the whole global table (measured 1.3 GB of ~287 KB whole-env copies on a self-build). Lookups
// scan locals innermost-first, then base innermost-first (locals shadow globals).
typedef struct { tk_val_binding *base; size_t base_len; tk_val_binding *ptr; size_t len; size_t cap; tk_str cur_ns; tk_str owner_type; } tk_env;
static inline tk_env tk_env_empty(void) { return (tk_env){ .base = NULL, .base_len = 0, .ptr = NULL, .len = 0, .cap = 0, .cur_ns = (tk_str){0}, .owner_type = (tk_str){0} }; }
static inline tk_env tk_env_with_owner(tk_env env, tk_str owner) { env.owner_type = owner; return env; }
tk_env tk_env_seal(tk_env env);   // (#148) move accumulated bindings into the immutable base (collect→type boundary)

TK_RESULT(tk_type, tk_type_result);            // Type | error
TK_RESULT(tk_val_binding, tk_binding_result);  // ValBinding | error (carries is_mut — B.21)
TK_RESULT(tk_env, tk_env_result);              // Env | error (env-threading; used by match.c + the typed pass)

tk_env            tk_env_define(tk_env env, tk_str name, tk_type t, bool is_mut);   // a LOCAL (ns empty)
tk_env            tk_env_define_fn(tk_env env, tk_str name, tk_type t, tk_str ns);  // a top-level function (#41)
tk_binding_result tk_env_lookup_binding(tk_env env, tk_str name);  // the whole binding (mut guard)
tk_type_result    tk_env_lookup(tk_env env, tk_str name);          // the type (thin wrapper)
tk_type_result    tk_env_lookup_call(tk_env env, tk_path callee);  // namespace-aware CALL resolution (#41)
tk_str            tk_env_call_ns(tk_env env, tk_path callee);      // the resolved target's namespace ("" if none) — for codegen name mangling (#49)
tk_type_result    tk_builtin_type(tk_str name);
tk_type_result    tk_builtin_fn(tk_str name);       // injected, non-shadowable stdlib fns (print/println, teko::assert::*)
bool              tk_bind_is_mut(tk_bind_kind k);   // k == TK_BIND_MUT (Let/Const immutable — B.21)

#endif // TK_CHECK_SCOPE_H
