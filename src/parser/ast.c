// src/parser/ast.c   (namespace 'teko::parser')
//
// C-only realization of teko::list::push / compiler-managed boxing for the parser AST
// (no Teko pair — scaffolding). The Teko side has no separate ast FUNCTION file: ast.tks
// is types-only, and these helpers are the C cost of no-generics / no-managed-boxing.
// They pair structurally with ast.h / ast.tks.
//
// The list-append helpers `(T** ptr, size_t* n, T x)` grow-append, mirroring
// teko::list::push (allocation failure PANICS — M.5). Each push reallocs to the exact
// `n + 1` (the seed never frees these arenas — B0). The box helpers heap a node so a
// recursive parent can point at it (the compiler-managed indirection of the Teko side).
#include "ast.h"

#include <string.h>   // memcpy — for the box helpers

// ===================================================================
// List-append helpers (ast.h prototypes).
// ===================================================================
#define TK_PUSH_BODY(T)                                          \
    do {                                                         \
        size_t m = *n;                                           \
        T *np = tk_realloc0(*xs, (m + 1) * sizeof(T));               \
        if (np == NULL) { abort(); }                            \
        np[m] = item;                                            \
        *xs = np;                                                \
        *n  = m + 1;                                             \
    } while (0)

void tk_exprs_push       (tk_expr **xs,        size_t *n, tk_expr        item) { TK_PUSH_BODY(tk_expr); }
void tk_array_elems_push (tk_array_elem **xs,  size_t *n, tk_array_elem  item) { TK_PUSH_BODY(tk_array_elem); }
void tk_stmts_push (tk_statement **xs, size_t *n, tk_statement item) { TK_PUSH_BODY(tk_statement); }
void tk_pats_push  (tk_pattern **xs,   size_t *n, tk_pattern   item) { TK_PUSH_BODY(tk_pattern); }
void tk_arms_push  (tk_arm **xs,       size_t *n, tk_arm       item) { TK_PUSH_BODY(tk_arm); }
void tk_params_push(tk_param **xs,     size_t *n, tk_param     item) { TK_PUSH_BODY(tk_param); }
void tk_lambda_params_push(tk_lambda_param **xs, size_t *n, tk_lambda_param item) { TK_PUSH_BODY(tk_lambda_param); }   // (W10)
void tk_fields_push(tk_field **xs,     size_t *n, tk_field     item) { TK_PUSH_BODY(tk_field); }
void tk_functions_push(tk_function **xs, size_t *n, tk_function item) { TK_PUSH_BODY(tk_function); }   // (OOP A1, 2026-07-01) struct-body methods
void tk_segs_push  (tk_segment **xs,   size_t *n, tk_segment   item) { TK_PUSH_BODY(tk_segment); }
void tk_strvec_push(tk_str **xs,       size_t *n, tk_str       item) { TK_PUSH_BODY(tk_str); }
void tk_types_push (tk_type_expr **xs, size_t *n, tk_type_expr item) { TK_PUSH_BODY(tk_type_expr); }
void tk_terms_push (tk_cmp_term **xs,  size_t *n, tk_cmp_term  item) { TK_PUSH_BODY(tk_cmp_term); }
void tk_decls_push (tk_decl **xs,      size_t *n, tk_decl      item) { TK_PUSH_BODY(tk_decl); }
void tk_uses_push  (tk_use_decl **xs,  size_t *n, tk_use_decl  item) { TK_PUSH_BODY(tk_use_decl); }
void tk_constraints_push(tk_constraint_expr **xs, size_t *n, tk_constraint_expr item) { TK_PUSH_BODY(tk_constraint_expr); }   // (W11/S6)

#undef TK_PUSH_BODY

// ===================================================================
// box helpers — heap a node so a recursive parent can point at it.
// ===================================================================
tk_expr *tk_box_expr(tk_expr e) {
    tk_expr *p = tk_alloc(sizeof(tk_expr));
    if (p == NULL) { abort(); }
    memcpy(p, &e, sizeof(tk_expr));
    return p;
}
tk_type_expr *tk_box_type(tk_type_expr t) {
    tk_type_expr *p = tk_alloc(sizeof(tk_type_expr));
    if (p == NULL) { abort(); }
    memcpy(p, &t, sizeof(tk_type_expr));
    return p;
}
tk_constraint_expr *tk_box_constraint(tk_constraint_expr c) {   // (W11/S6)
    tk_constraint_expr *p = tk_alloc(sizeof(tk_constraint_expr));
    if (p == NULL) { abort(); }
    memcpy(p, &c, sizeof(tk_constraint_expr));
    return p;
}
