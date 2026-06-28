// src/checker/expr.h   (namespace 'teko::checker')
// The expression-typing surface (the evolved check_expr family). Pairs expr.c.
#ifndef TK_CHECK_EXPR_H
#define TK_CHECK_EXPR_H

#include "tast.h"
#include "resolve.h"   // tk_type_table

// the expression dispatch — types an Expr → TExpr (renamed from tk_type_expr to
// avoid clashing with the AST type tk_type_expr/TypeExpr).
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table);

// (S4) type a struct literal with an EXPECTED type (a VOID-tag sentinel = none) so a generic
// constructor `Box { … }` retargets to the concrete instance from the annotation. type_binding
// passes the annotation; the expr dispatch passes VOID. Mirror of typer.tks::type_struct_lit.
tk_texpr_result tk_type_struct_lit(tk_struct_lit sl, tk_type expected, tk_env env, tk_type_table table);

// (S4) type an expr flowing into an EXPECTED type — routes a struct literal through tk_type_struct_lit
// with that expected type (generic constructor → concrete instance), else types normally. Mirror of
// typer.tks::type_value_expected. Used by type_binding + struct-lit field values (nested generics).
tk_texpr_result tk_type_value_expected(tk_expr e, tk_type expected, tk_env env, tk_type_table table);

// cast legality (C2) — exposed so the counter-validation (revalidate.c) re-derives it.
bool tk_cast_ok(tk_type from, tk_type to);

// ---- shared small helpers (one definition, in expr.c) — used by BOTH TUs (M.5) ----
tk_texpr      *tk_box(tk_texpr t);       // heap-copy a TExpr (children)
tk_type        tk_prim_t(tk_prim_kind k);
tk_type        tk_void_t(void);   // the `void` return marker (M.3 — no value)
bool           tk_is_bool(tk_type t);
// numeric-class predicates (B.38) — single definition in expr.c, shared across the checker
// (revalidate.c reuses them; the `.tks` twin defines them once in teko::checker, in expr.tks).
bool           is_bool(tk_type t);
bool           is_integer(tk_type t);
bool           is_float(tk_type t);
bool           is_numeric(tk_type t);
tk_texpr_result tk_xok(tk_texpr t);
tk_texpr_result tk_xerr(const char *m);
tk_texpr_result tk_xferr(tk_error e);

// C6 — an annotated binding whose value type ≠ T (reuses value_fits). NULL = ok.
// Lives with the cast/range machinery in expr.c; consumed by typer.c's type_binding.
const char *annotated_literal_reason(tk_expr value, tk_type ann);
// C6 (extended) — a fitting numeric literal adopts the destination type (return/trailing/binding);
// int-literal → a prim it fits or `byte` (=u8); float-literal → a fitting float prim. Over the TYPED node.
bool tk_literal_adopts(tk_texpr e, tk_type to);

#endif // TK_CHECK_EXPR_H
