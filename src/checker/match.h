// src/checker/match.h — the shared pattern-checking + exhaustiveness helpers (B.15),
// reused by the typed layer (typer.c) which forward-declares them to avoid a cycle.
#ifndef TK_CHECK_MATCH_H
#define TK_CHECK_MATCH_H

#include "type.h"
#include "scope.h"
#include "resolve.h"
#include "tast.h"             // tk_texpr_result (match.c threads the typed-expr result)
#include "../parser/ast.h"   // tk_pattern, tk_arm, … (the parser's AST)

tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
bool          tk_exhaustive(tk_arm *arms, size_t n, tk_type subject, tk_type_table table, tk_str ref_ns);   // (#109 W2) ref_ns = the match's enclosing namespace

#endif // TK_CHECK_MATCH_H
