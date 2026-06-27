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

#endif // TK_CHECK_COLLECT_H
