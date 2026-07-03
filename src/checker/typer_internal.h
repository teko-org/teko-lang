// src/checker/typer_internal.h   (namespace 'teko::checker')
// Cross-TU glue between expr.c and typer.c (same-namespace visibility, realized
// as C headers). NOT a public surface — only the two checker typing TUs include it.
#ifndef TK_CHECK_TYPER_INTERNAL_H
#define TK_CHECK_TYPER_INTERNAL_H

#include "tast.h"
#include "resolve.h"   // tk_type_table

// a typed block: thread the env, collect typed statements. Defined in typer.c
// (statement side); expr.c's type_if/type_match/type_arm call back into it for
// branch/arm bodies.
tk_typed_block_result tk_type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);

// the value-type a typed block yields (last stmt's, if an ExprStmt; else void).
// Defined in expr.c; typer.c's if/match STATEMENT forms reuse it.
tk_type tk_tblock_type(tk_tstatement *stmts, size_t n);

// Does a typed block DIVERGE — i.e. exit via return/break/continue on ALL paths so it
// yields NO trailing value? (Its trailing statement is a return/break/continue, or a
// trailing if/match whose every branch/arm diverges.) Match-as-value unification skips
// diverging arms (B.20). Defined in expr.c.
bool tk_tblock_diverges(const tk_tstatement *stmts, size_t n);
bool tk_texpr_diverges(const tk_texpr *e);   // a panic/exit call (diverges — legislator's ruling: no `never` type)

// one typed match arm (pattern extends env; `when` guard bool; body typed).
// Defined in expr.c (helper of the match VALUE form); typer.c's match STATEMENT
// form reuses it. Shared from match.c too: tk_exhaustive / tk_check_pattern.
tk_tarm_result tk_type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table);
bool           tk_exhaustive(tk_arm *arms, size_t n, tk_type subject, tk_type_table table, tk_str ref_ns);   // (#109 W2) ref_ns = the match's enclosing namespace

#endif // TK_CHECK_TYPER_INTERNAL_H
