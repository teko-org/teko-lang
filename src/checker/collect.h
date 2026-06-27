// src/checker/collect.h — pass 1: collect top-level names (forward references).
#ifndef TK_CHECK_COLLECT_H
#define TK_CHECK_COLLECT_H
#include "resolve.h"
#include "tast.h"   // tk_tprogram — needed by tk_type_table_of (C7.12)

typedef struct { tk_type_table types; tk_env env; } tk_collected;
TK_RESULT(tk_collected, tk_collected_result);

tk_collected_result tk_collect(tk_program program);

// C7.12 — reconstruct a TypeTable from a typed program's pass-through TypeDecl items.
// Used by the package backend in driver.c (which has a tk_tprogram, not a tk_program).
// The namespace is set to "" — sufficient for resolve_type; W-vis-enforce is not called.
tk_type_table tk_type_table_of(tk_tprogram prog);

// C7.10 — pre-seed the collection environment from an already-typed dep TProgram.
// Inserts the dep's type declarations into `table` and its function signatures into `env`.
// Returns the updated {types, env} — the caller passes these into the project's collect().
// The dep's bodies are NOT re-typed (they were verified when the dep was built).
tk_collected_result tk_seed_from_dep(tk_tprogram dep, tk_type_table table, tk_env env);

// C7.10 — collect with a pre-seeded (table, env). Adds the project's type decls + function
// signatures on top of the seed. Mirrors collect.tks::collect_with_seed.
tk_collected_result tk_collect_with_seed(tk_program program, tk_collected seed);

#endif // TK_CHECK_COLLECT_H
