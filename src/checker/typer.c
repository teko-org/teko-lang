// src/checker/typer.c   (namespace 'teko::checker')
// The statement / function / item / program half of the typed pass (the evolved
// check_* producers — C1). The expression-typing half (tk_typer_expr + the
// per-expr-kind helpers, predicates, cast/range machinery, field access, and the
// if/match VALUE forms) lives in expr.c; the two TUs see each other via expr.h
// (tk_typer_expr, shared helpers, annotated_literal_reason) and typer_internal.h
// (tk_type_block ↔ tk_type_arm / tk_tblock_type / tk_exhaustive). Pairs typer.tks.
#include "typer.h"
#include "expr.h"            // tk_typer_expr, shared helpers (tk_box/tk_xok/…), annotated_literal_reason
#include "typer_internal.h"  // tk_type_block (this TU defines it), tk_type_arm, tk_tblock_type, tk_exhaustive
#include "collect.h"         // tk_collect, tk_collected_result
#include "check_modules.h"   // tk_check_modules (W-vis-enforce — module-system pass)
#include "initanalysis.h"    // tk_analyze_program (Phase 5 — init analysis)
#include "monomorph.h"       // tk_monomorphize (S4b — generic instantiation, runs last)
#include <string.h>          // memcmp (loop-label comparison)

// short aliases for local use (terse, matches the original site spellings).
#define box   tk_box
#define void_t tk_void_t
#define is_bool tk_is_bool
#define xok   tk_xok
#define xerr  tk_xerr
#define xferr tk_xferr

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tarm,       tk_tarm_list)
TK_LIST(tk_tstatement, tk_tstmt_list)
TK_LIST(tk_titem,      tk_titem_list)

// forward decls (the if/match STATEMENT forms; mutual recursion with type_block).
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);

// ---- a typed block: thread the env, collect typed statements (shared via typer_internal.h) ----
tk_typed_block_result tk_type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    tk_env cur = env;
    tk_tstmt_list out = tk_tstmt_list_empty();
    for (size_t i = 0; i < n; i += 1) {
        // W5a — a block's TRAILING `if`/`match` is its VALUE (B.20): type it via the VALUE
        // form (tk_typer_expr → type_if/type_match) so the block yields the branch type. A
        // trailing `if` WITHOUT `else` can't be a value (no value when false) → stays a
        // statement; `match` is exhaustive so it always yields. Non-tail if/match → statement.
        bool tail = (i + 1 == n);
        if (tail && stmts[i].tag == TK_STMT_EXPR &&
            ((stmts[i].as.expr_stmt.expr.tag == TK_EXPR_IF && stmts[i].as.expr_stmt.expr.as.if_expr.has_else)
             || stmts[i].as.expr_stmt.expr.tag == TK_EXPR_MATCH)) {
            tk_texpr_result te = tk_typer_expr(stmts[i].as.expr_stmt.expr, cur, table);
            if (!te.ok) return (tk_typed_block_result){ .ok = false, .as.error = te.as.error };
            tk_tstatement node = { .tag = TK_TSTMT_EXPR, .as.expr_stmt = { te.as.value } };
            out = tk_tstmt_list_push(out, node);   // a value expr binds nothing → env unchanged
            continue;
        }
        tk_typed_stmt_result ts = tk_type_statement(stmts[i], cur, table);
        if (!ts.ok) return (tk_typed_block_result){ .ok = false, .as.error = ts.as.error };
        out = tk_tstmt_list_push(out, ts.as.value.node);
        cur = ts.as.value.env;
    }
    return (tk_typed_block_result){ .ok = true, .as.value = { .stmts = out.ptr, .n = out.len, .env = cur } };
}

// ---- if as a STATEMENT (value discarded → void) ----
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    tk_typed_block_result tb = tk_type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_tstatement *eb_stmts = tb.as.value.stmts; size_t eb_n = 0;   // gated by has_else
    if (f.has_else) {
        tk_typed_block_result eb = tk_type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
        eb_stmts = eb.as.value.stmts; eb_n = eb.as.value.n;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = void_t(), .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, f.has_else, eb_stmts, eb_n } });
}

// ---- match as a STATEMENT (value discarded → void) ----
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    tk_tarm_list arms = tk_tarm_list_empty();
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_tarm_result ai = tk_type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type, table)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = void_t(), .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- statements ----
static tk_typed_stmt_result sok(tk_tstatement node, tk_env env) { return (tk_typed_stmt_result){ .ok = true, .as.value = { node, env } }; }
static tk_typed_stmt_result sfail(tk_error e)   { return (tk_typed_stmt_result){ .ok = false, .as.error = e }; }
static tk_typed_stmt_result smsg(const char *m) { return sfail(tk_error_make(m)); }

static bool assignable_to(tk_type from, tk_type to, tk_type_table table);   // fwd (defined below; B.14)

static tk_typed_stmt_result type_binding(tk_binding b, tk_env env, tk_type_table table) {
    // (S4) give a struct-literal value its EXPECTED (annotation) type so a generic constructor
    // `Box { … }` under `: Box<i64>` targets the concrete instance (annotation-driven).
    tk_texpr_result v;
    if (b.has_type) {
        tk_type_result at = tk_resolve_type(b.type_ann, table); if (!at.ok) return sfail(at.as.error);
        v = tk_type_value_expected(b.value, at.as.value, env, table);
    } else {
        v = tk_typer_expr(b.value, env, table);
    }
    if (!v.ok) return sfail(v.as.error);
    tk_type bound = v.as.value.type;
    if (tk_type_is_void(&bound)) return smsg("cannot bind a `void` expression — it yields no value (M.1)");
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return sfail(a.as.error);
        if (!assignable_to(v.as.value.type, a.as.value, table)) {   // B.14 widening (case → variant) OR C6: a fitting literal adopts T (leaf stays i64)
            const char *why = annotated_literal_reason(b.value, a.as.value);
            if (why != NULL)   // (C1.8) expected = the annotation, actual = the bound value's type
                return sfail(tk_error_types(tk_error_make(why),
                                            tk_type_render(a.as.value), tk_type_render(v.as.value.type)));
        }
        bound = a.as.value;
        // (#4) annotation-directed element type: a sentinel empty list/array (`teko::list::empty()`
        // / `[]`) ADOPTS the annotation's concrete slice type, so codegen sees the element WITHOUT
        // back-inference. Mirrors type_assign's target-adopt and emit_as's wrap.
        if (v.as.value.type.tag == TK_TYPE_SLICE && v.as.value.type.as.slice.element == NULL
            && bound.tag == TK_TYPE_SLICE && bound.as.slice.element != NULL)
            v.as.value.type = bound;
    }
    // (#4) REJECT an untyped empty collection. The element type MUST be fixed at the declaration —
    // by the binding annotation (`: []T`) or by typed initial elements (a non-empty literal / a
    // push that fixes the element). A bare `empty()`/`[]` whose element is still the sentinel is an
    // error: no silent element-less slice, no codegen back-inference (collections ruling #4).
    if (bound.tag == TK_TYPE_SLICE && bound.as.slice.element == NULL)
        return smsg("an empty list needs a known element type; annotate the binding (e.g. `mut xs: []i32 = teko::list::empty()`)");
    tk_tstatement node = { .tag = TK_TSTMT_BINDING, .as.binding = { b.kind, b.target, bound, v.as.value } };
    if (b.target.tag == TK_BIND_SIMPLE)
        return sok(node, tk_env_define(env, b.target.as.simple.name, bound, tk_bind_is_mut(b.kind)));
    return sok(node, env);   // [destructuring binding: refinement]
}

static tk_typed_stmt_result type_assign(tk_assign a, tk_env env, tk_type_table table) {
    tk_binding_result tb = tk_env_lookup_binding(env, a.name); if (!tb.ok) return sfail(tb.as.error);
    // (MEM-1b-ii) `r.value op= x` — write THROUGH a reference. The handle `r` need NOT be `mut` (a
    // `Ref<T>` is a const handle granting WRITE to its mutable target — R2); the type written is T.
    if (a.deref) {
        if (tb.as.value.type.tag != TK_TYPE_REF)
            return smsg("`.value` assignment requires a reference (`Ref<T>`) on the left");
        tk_type inner = *tb.as.value.type.as.ref.inner;
        tk_texpr_result dv = tk_typer_expr(a.value, env, table); if (!dv.ok) return sfail(dv.as.error);
        if (!assignable_to(dv.as.value.type, inner, table)) {
            if (!tk_literal_adopts(dv.as.value, inner))
                return sfail(tk_error_types(tk_error_make("assigned value does not match the referenced type"),
                                            tk_type_render(inner), tk_type_render(dv.as.value.type)));
            dv.as.value.type = inner;   // a fitting literal adopts T
        }
        tk_tstatement dn = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, inner, dv.as.value, true } };
        return sok(dn, env);
    }
    if (!tb.as.value.is_mut) return smsg("cannot assign to immutable binding — declare it `mut` (B.21)");
    tk_texpr_result v = tk_typer_expr(a.value, env, table); if (!v.ok) return sfail(v.as.error);
    // The value must WIDEN into the target's type (B.14 case→variant, T→T?) OR be a fitting literal
    // that adopts it (C6) — same rule as an annotated binding. On success the value adopts the target
    // type so a downstream pass (and codegen's wrap) sees the slot type, not the bare case.
    tk_type target = tb.as.value.type;
    if (!assignable_to(v.as.value.type, target, table)) {
        if (!tk_literal_adopts(v.as.value, target))   // (C1.8) expected = target, actual = value
            return sfail(tk_error_types(tk_error_make("assigned value does not match the target type"),
                                        tk_type_render(target), tk_type_render(v.as.value.type)));
    }
    // (#4) a sentinel empty list/array ADOPTS the target's concrete slice type so codegen sees the
    // element. OTHERWISE keep the value's NATURAL (case / T) type and store the target as `bound` —
    // codegen's emit_as then wraps the value into the target at the assign site (case→variant, T→T?,
    // a fitting literal). Mirrors type_binding (the binding kept its case type; the assign clobbered
    // it, which made a `node = OptionalType{…}` emit the case's fields under the variant's C type).
    if (v.as.value.type.tag == TK_TYPE_SLICE && v.as.value.type.as.slice.element == NULL
        && target.tag == TK_TYPE_SLICE && target.as.slice.element != NULL)
        v.as.value.type = target;
    tk_tstatement node = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, target, v.as.value, false } };
    return sok(node, env);   // mut rule enforced (B.21)
}

static tk_typed_stmt_result type_return(tk_return r, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_typer_expr(r.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_RETURN, .as.ret = { r.has_value, v.as.value } };
    return sok(node, env);   // value gated by has_value (B.20); return-type match enforced by tk_type_function's check_returns (C5)
}

static tk_typed_stmt_result type_loop(tk_loop_stmt l, tk_env env, tk_type_table table) {
    tk_typed_block_result tb = tk_type_block(l.body, l.nbody, env, table); if (!tb.ok) return sfail(tb.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_LOOP, .as.loop_stmt = { .label = l.label, .body = tb.as.value.stmts, .nbody = tb.as.value.n } };
    return sok(node, env);   // body bindings stay block-local
}

static tk_typed_stmt_result type_exprstmt(tk_expr_stmt es, tk_env env, tk_type_table table) {
    tk_texpr_result te;
    if (es.expr.tag == TK_EXPR_IF)         te = type_if_stmt(es.expr.as.if_expr, env, table);
    else if (es.expr.tag == TK_EXPR_MATCH) te = type_match_stmt(es.expr.as.match_expr, env, table);
    else                                   te = tk_typer_expr(es.expr, env, table);
    if (!te.ok) return sfail(te.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_EXPR, .as.expr_stmt = { te.as.value } };
    return sok(node, env);
}

tk_typed_stmt_result tk_type_statement(tk_statement s, tk_env env, tk_type_table table) {
    switch (s.tag) {
        case TK_STMT_BINDING:  return type_binding(s.as.binding, env, table);
        case TK_STMT_ASSIGN:   return type_assign(s.as.assign, env, table);
        case TK_STMT_RETURN:   return type_return(s.as.ret, env, table);
        case TK_STMT_LOOP:     return type_loop(s.as.loop_stmt, env, table);
        case TK_STMT_BREAK:    return sok((tk_tstatement){ .tag = TK_TSTMT_BREAK,    .as.jump = { .label = s.as.jump.label } }, env);
        case TK_STMT_CONTINUE: return sok((tk_tstatement){ .tag = TK_TSTMT_CONTINUE, .as.jump = { .label = s.as.jump.label } }, env);
        case TK_STMT_EXPR:     return type_exprstmt(s.as.expr_stmt, env, table);
        case TK_STMT_DEFER: {
            // Validate: defer block may not directly contain return, break, or nested defer.
            for (size_t i = 0; i < s.as.defer_stmt.nbody; i += 1) {
                tk_statement inner = s.as.defer_stmt.body[i];
                if (inner.tag == TK_STMT_RETURN)
                    return (tk_typed_stmt_result){ .ok = false, .as.error = tk_error_make("defer block may not return") };
                if (inner.tag == TK_STMT_BREAK)
                    return (tk_typed_stmt_result){ .ok = false, .as.error = tk_error_make("defer block may not break") };
                if (inner.tag == TK_STMT_DEFER)
                    return (tk_typed_stmt_result){ .ok = false, .as.error = tk_error_make("nested defer is not allowed") };
            }
            // Type-check the body in void context; bindings are scoped inside (don't leak env).
            tk_typed_block_result tb = tk_type_block(s.as.defer_stmt.body, s.as.defer_stmt.nbody, env, table);
            if (!tb.ok) return sfail(tb.as.error);
            tk_tstatement node = { .tag = TK_TSTMT_DEFER, .as.defer_stmt = { .body = tb.as.value.stmts, .nbody = tb.as.value.n } };
            return sok(node, env);
        }
    }
    return smsg("unknown statement");
}

// ---- items + program ----
static tk_type function_return(tk_function f, tk_type_table table) {
    if (!f.has_return) return void_t();    // no `-> ret` ⇒ returns no value (M.3 — void)
    tk_type_result r = tk_resolve_type(f.return_type, table);
    return r.ok ? r.as.value : void_t();   // collect validated signatures; a bad annotation surfaces there
}

// ---- C5: return / final-expr vs the declared return type (see the Teko twin; NULL = ok) ----
// B.14 — variant member inclusion + present-wrap. The widening rule is now the single source of
// truth in resolve.c (tk_widens_into), shared with the match/`if` arm JOIN (tk_type_join) so the
// return check and arm unification can never drift apart.
static bool assignable_to(tk_type from, tk_type to, tk_type_table table) {
    return tk_widens_into(from, to, table);
}
// (C1.8) the return-/trailing-value checks return a tk_error rather than a bare const char*, so a
// type mismatch can carry expected (the declared return type) vs actual (the produced value's type).
// CONVENTION: `.message == NULL` means "ok" (the old NULL-is-ok contract, now on the struct). The
// non-mismatch diagnostics (bare `return` in a value-returning fn) carry only a message — no types.
static inline tk_error ret_ok(void)            { return tk_error_make(NULL); }
static inline bool     ret_is_ok(tk_error e)   { return e.message == NULL; }

static tk_error check_returns(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table);   // fwd (mutual)
static tk_error check_returns_inexpr(const tk_texpr *e, tk_type ret, tk_type_table table) {
    if (e->tag == TK_TEXPR_IF) {
        tk_error t = check_returns(e->as.if_expr.then_blk, e->as.if_expr.nthen, ret, table); if (!ret_is_ok(t)) return t;
        return check_returns(e->as.if_expr.else_blk, e->as.if_expr.nelse, ret, table);
    }
    if (e->tag == TK_TEXPR_MATCH) {   // arm bodies are blocks now — a `return` inside one is checked (B.20)
        for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
            tk_error t = check_returns(e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody, ret, table);
            if (!ret_is_ok(t)) return t;
        }
    }
    return ret_ok();
}
static tk_error check_return_stmt(const tk_tstatement *s, tk_type ret, tk_type_table table) {
    switch (s->tag) {
        case TK_TSTMT_RETURN:
            if (s->as.ret.has_value) {
                if (assignable_to(s->as.ret.value.type, ret, table) || tk_literal_adopts(s->as.ret.value, ret))
                    return ret_ok();   // a fitting int/float/byte literal adopts the return type (C6)
                // (C1.8) expected = declared return type, actual = the `return e` value's type
                return tk_error_types(tk_error_make("return value does not match the function's declared return type"),
                                      tk_type_render(ret), tk_type_render(s->as.ret.value.type));
            }
            return tk_type_is_void(&ret) ? ret_ok()
                 : tk_error_make("bare `return` in a function that declares a value-returning (non-void) return type");
        case TK_TSTMT_LOOP: return check_returns(s->as.loop_stmt.body, s->as.loop_stmt.nbody, ret, table);
        case TK_TSTMT_EXPR: return check_returns_inexpr(&s->as.expr_stmt.expr, ret, table);
        default:            return ret_ok();
    }
}
static tk_error check_returns(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table) {
    for (size_t i = 0; i < n; i += 1) { tk_error e = check_return_stmt(&stmts[i], ret, table); if (!ret_is_ok(e)) return e; }
    return ret_ok();
}
static tk_error check_trailing_value(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table) {
    if (n == 0) return ret_ok();
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag != TK_TSTMT_EXPR) return ret_ok();   // trailing loop/if/match → no claim (guard)
    if (tk_texpr_diverges(&last->as.expr_stmt.expr)) return ret_ok();   // a trailing panic/exit yields no value (M.3)
    if (assignable_to(last->as.expr_stmt.expr.type, ret, table) || tk_literal_adopts(last->as.expr_stmt.expr, ret))
        return ret_ok();   // a fitting trailing literal adopts the return type (C6)
    // (C1.8) expected = declared return type, actual = the trailing expression's type
    return tk_error_types(tk_error_make("the function's final expression does not match its declared return type"),
                          tk_type_render(ret), tk_type_render(last->as.expr_stmt.expr.type));
}

// ---- W5-cf-2: loop-label validation (the Teko twin checks the same; NULL = ok) ----
// A `break NAME`/`continue NAME` must name an ENCLOSING loop; a bare `break`/`continue` must
// be inside some loop; and all loop labels within one body must be DISTINCT, so codegen's C
// goto-labels (tk_lbl_<NAME>_break / _cont) cannot collide. `scope` is the chain of enclosing
// loop labels; `seen`/`nseen` accumulate every label in this body for the uniqueness check.
#define TK_MAX_LABELS 512
typedef struct lbl_scope { tk_str label; const struct lbl_scope *parent; } lbl_scope;
static bool lbl_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }
static bool lbl_enclosing(const lbl_scope *s, tk_str label) {
    for (; s; s = s->parent) if (lbl_eq(s->label, label)) return true;
    return false;
}
static const char *check_labels(const tk_tstatement *stmts, size_t n, const lbl_scope *scope,
                                bool in_loop, tk_str *seen, size_t *nseen);
static const char *check_labels_inexpr(const tk_texpr *e, const lbl_scope *scope, bool in_loop,
                                       tk_str *seen, size_t *nseen) {
    if (e->tag == TK_TEXPR_IF) {
        const char *t = check_labels(e->as.if_expr.then_blk, e->as.if_expr.nthen, scope, in_loop, seen, nseen); if (t) return t;
        return check_labels(e->as.if_expr.else_blk, e->as.if_expr.nelse, scope, in_loop, seen, nseen);
    }
    if (e->tag == TK_TEXPR_MATCH)   // arm bodies are blocks now — walk each arm block (B.20)
        for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
            const char *t = check_labels(e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody, scope, in_loop, seen, nseen); if (t) return t;
        }
    return NULL;
}
static const char *check_labels(const tk_tstatement *stmts, size_t n, const lbl_scope *scope,
                                bool in_loop, tk_str *seen, size_t *nseen) {
    for (size_t i = 0; i < n; i += 1) {
        const tk_tstatement *s = &stmts[i];
        switch (s->tag) {
            case TK_TSTMT_LOOP: {
                tk_str L = s->as.loop_stmt.label;
                if (L.len > 0) {
                    for (size_t k = 0; k < *nseen; k += 1)
                        if (lbl_eq(seen[k], L)) return "duplicate loop label within the same body";
                    if (*nseen < TK_MAX_LABELS) { seen[*nseen] = L; *nseen += 1; }
                }
                lbl_scope frame = { .label = L, .parent = scope };
                const char *t = check_labels(s->as.loop_stmt.body, s->as.loop_stmt.nbody, &frame, true, seen, nseen);
                if (t) return t;
                break;
            }
            case TK_TSTMT_BREAK:
            case TK_TSTMT_CONTINUE: {
                tk_str L = s->as.jump.label;
                if (L.len == 0) { if (!in_loop) return "`break`/`continue` outside a loop"; }
                else if (!lbl_enclosing(scope, L)) return "`break`/`continue` to an unknown loop label (no enclosing loop carries that label)";
                break;
            }
            case TK_TSTMT_EXPR: {
                const char *t = check_labels_inexpr(&s->as.expr_stmt.expr, scope, in_loop, seen, nseen); if (t) return t;
                break;
            }
            default: break;
        }
    }
    return NULL;
}

// C7.1a: an `extern` param/return type must be a primitive (int/float/bool) or `byte` — the
// only shapes that marshal 1:1 across the C ABI. `ptr`/`uptr` join next; `void` is allowed for
// the RETURN only (handled by the caller). Mirrors typer.tks extern_type_ok.
// C7.1a: is `t` legal across the extern boundary? prims/byte/ptr/uptr, OR an `extern type`
// opaque handle (a Named whose decl is an ExternBody) — but NOT an ordinary Teko struct/enum/
// variant (those would be unsupported by-value aggregate FFI). `void` handled by the caller.
// C7.2: `from "teko_rt"` functions use Teko's internal ABI (tk_str/tk_ffi_sres/etc.) and may
// declare str/[]str/str|error/error?/i32 — those are whitelisted via the `teko_rt` bypass.
static bool str_eq(tk_str a, const char *b) {
    size_t n = strlen(b);
    return a.len == n && memcmp(a.ptr, b, n) == 0;
}
static bool extern_type_ok(tk_type t, tk_type_table table) {
    if (t.tag == TK_TYPE_PRIM || t.tag == TK_TYPE_BYTE
        || t.tag == TK_TYPE_PTR  || t.tag == TK_TYPE_UPTR) return true;
    if (t.tag == TK_TYPE_NAMED) {
        tk_decl_result d = tk_type_table_find(table, t.as.named.name);
        return d.ok && d.as.value.body.tag == TK_BODY_EXTERN;
    }
    return false;
}
// C7.2: teko_rt speaks Teko's own internal ABI — str/[]str/variant/optional are legal there.
static bool teko_rt_type_ok(tk_type t) {
    if (t.tag == TK_TYPE_STR || t.tag == TK_TYPE_VOID) return true;
    if (t.tag == TK_TYPE_PRIM || t.tag == TK_TYPE_BYTE
        || t.tag == TK_TYPE_PTR  || t.tag == TK_TYPE_UPTR) return true;
    if (t.tag == TK_TYPE_SLICE)    return true;   // []str, []byte, etc.
    if (t.tag == TK_TYPE_VARIANT)  return true;   // str | error
    if (t.tag == TK_TYPE_OPTIONAL) return true;   // error?
    return false;
}

tk_tfunction_result tk_type_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    tk_type_table tbl = tk_type_param_table(f.type_params, f.n_type_params, (tk_str){0}, table);   // (S4) opaque type-params in scope
    bool is_teko_rt = f.is_extern && str_eq(f.from_lib, "teko_rt");   // C7.2: bypass for Teko's own runtime
    for (size_t i = 0; i < f.nparams; i += 1) {           // params immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, tbl);
        if (!pt.ok) return (tk_tfunction_result){ .ok = false, .as.error = pt.as.error };
        if (f.is_extern && !is_teko_rt && !extern_type_ok(pt.as.value, table)) {
            return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make("an `extern` function parameter must be a primitive (int/float/bool), `byte`, `ptr`, `uptr`, or an `extern type` handle (C7.1a)") };
        }
        if (f.is_extern && is_teko_rt && !teko_rt_type_ok(pt.as.value)) {
            return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make("a `teko_rt` extern function parameter must be a primitive, `str`, slice, variant, or optional (C7.2)") };
        }
        local = tk_env_define(local, f.params[i].name, pt.as.value, false);
    }
    tk_type ret = function_return(f, tbl);
    if (f.is_extern) {
        // a bodyless foreign declaration: no body to check / return-analyze. Validate the
        // return marshals (void = no value is fine; else prim/byte only for now).
        bool ret_ok = is_teko_rt ? teko_rt_type_ok(ret) || ret.tag == TK_TYPE_VOID
                                 : (ret.tag == TK_TYPE_VOID) || extern_type_ok(ret, table);
        if (!ret_ok) {
            return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make("an `extern` function return must be a primitive (int/float/bool), `byte`, `ptr`, `uptr`, an `extern type` handle, or absent (C7.1a)") };
        }
        tk_tfunction ef = { .name = f.name, .type_params = f.type_params, .n_type_params = f.n_type_params, .params = f.params, .nparams = f.nparams,
                            .return_type = ret, .body = NULL, .nbody = 0,
                            .vis = f.vis, .has_doc = f.has_doc, .doc = f.doc, .is_test = f.is_test,
                            .is_extern = true, .c_symbol = f.c_symbol, .from_lib = f.from_lib };
        return (tk_tfunction_result){ .ok = true, .as.value = ef };
    }
    tk_typed_block_result tb = tk_type_block(f.body, f.nbody, local, tbl);
    if (!tb.ok) return (tk_tfunction_result){ .ok = false, .as.error = tb.as.error };
    { tk_error e = check_returns(tb.as.value.stmts, tb.as.value.n, ret, table);        // C5: each `return e` matches
      if (!ret_is_ok(e)) return (tk_tfunction_result){ .ok = false, .as.error = e }; }   // (C1.8) carries expected/actual
    { tk_error e = check_trailing_value(tb.as.value.stmts, tb.as.value.n, ret, table); // C5: trailing value (when present)
      if (!ret_is_ok(e)) return (tk_tfunction_result){ .ok = false, .as.error = e }; }   // (C1.8) carries expected/actual
    { tk_str seen[TK_MAX_LABELS]; size_t nseen = 0;                                  // W5-cf-2: loop labels resolve + are unique
      const char *why = check_labels(tb.as.value.stmts, tb.as.value.n, NULL, false, seen, &nseen);
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tfunction tf = { .name = f.name, .type_params = f.type_params, .n_type_params = f.n_type_params, .params = f.params, .nparams = f.nparams,
                        .return_type = ret, .body = tb.as.value.stmts, .nbody = tb.as.value.n,
                        .vis = f.vis, .has_doc = f.has_doc, .doc = f.doc, .is_test = f.is_test };
    return (tk_tfunction_result){ .ok = true, .as.value = tf };
}

tk_titem_result tk_type_item(tk_item item, tk_env env, tk_type_table table) {
    switch (item.tag) {
        case TK_ITEM_FUNCTION: {
            tk_tfunction_result tf = tk_type_function(item.as.function, env, table);
            if (!tf.ok) return (tk_titem_result){ .ok = false, .as.error = tf.as.error };
            tf.as.value.namespace = item.namespace;   // (#49) carry the declaring ns for codegen name mangling
            tf.as.value.file = item.file;             // (E3) source provenance for the .tsym symbol map
            tf.as.value.line = item.as.function.line;
            tf.as.value.col  = item.as.function.col;
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_FUNCTION, .as.function = tf.as.value } };
        }
        case TK_ITEM_TYPE_DECL: return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_TYPE_DECL, .as.type_decl = item.as.type_decl } };
        case TK_ITEM_USE:       return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_USE, .as.use_decl = item.as.use_decl } };
        case TK_ITEM_STATEMENT: {
            tk_typed_stmt_result ts = tk_type_statement(item.as.statement, env, table);
            if (!ts.ok) return (tk_titem_result){ .ok = false, .as.error = ts.as.error };
            return (tk_titem_result){ .ok = true, .as.value = { .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node } };
        }
    }
    return (tk_titem_result){ .ok = false, .as.error = tk_error_make("unknown item") };
}

// (C1.8) surface an inner checker error at the program level. Builds the located message string
// (tk_diag_at — file:line:col baked in, the FALLBACK the driver prints when it can't read the file)
// AND sets the STRUCTURED file/line/col on the error so the driver's renderer can read the source
// line + draw a caret. expected/actual carried by the inner error are PRESERVED (not dropped when
// re-wrapping), so a type mismatch still renders "expected X, found Y" after surfacing.
static tk_error surface_at(tk_str file, uint32_t line, uint32_t col, tk_error inner) {
    tk_error e = tk_error_make(tk_diag_at(file, line, col, inner.message));   // located string (fallback)
    e = tk_error_at(e, file.ptr ? (const char *)file.ptr : NULL, line, col);  // structured position (renderer)
    e = tk_error_types(e, inner.expected, inner.actual);                      // preserve expected/actual (C1.8)
    e.severity = inner.severity;
    return e;
}

// C7.10 — dep-aware variant of tk_type_program. Pre-seeds the collected environment from
// dep_prog, then collects and type-checks `program`. Dep items are prepended to the result
// TProgram so subsequent passes (codegen, VM) see the full program. Mirrors typer.tks::type_program_with_deps.
tk_tprogram_result tk_type_program_with_deps(tk_program program, tk_tprogram dep_prog) {
    // Seed from dep (adds dep type decls + fn signatures).
    tk_collected_result seed_r = tk_seed_from_dep(dep_prog, tk_type_table_empty(), tk_env_empty());
    if (!seed_r.ok) return (tk_tprogram_result){ .ok = false, .as.error = seed_r.as.error };

    // Collect from the project on top of the seed.
    tk_collected_result c = tk_collect_with_seed(program, seed_r.as.value);
    if (!c.ok) return (tk_tprogram_result){ .ok = false, .as.error = c.as.error };

    // W-vis-enforce: only the project's items (dep was checked when built).
    { const char *why = tk_check_modules(program, c.as.value.types);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }
    // (S4 type-generics) stamp concrete decls for written generic uses before typing (no-op when none).
    tk_type_table types = tk_instantiate_types(program, c.as.value.types);

    tk_titem_list items = tk_titem_list_empty();
    tk_tstmt_list mainbody = tk_tstmt_list_empty();
    tk_env cur = c.as.value.env;

    for (size_t i = 0; i < program.len; i += 1) {
        tk_item it = program.items[i];
        cur.cur_ns = it.namespace;
        uint32_t line = it.tag == TK_ITEM_FUNCTION  ? it.as.function.line
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.line : 0;
        uint32_t col  = it.tag == TK_ITEM_FUNCTION  ? it.as.function.col
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.col : 0;
        if (it.tag == TK_ITEM_STATEMENT) {
            tk_typed_stmt_result ts = tk_type_statement(it.as.statement, cur, types);
            if (!ts.ok) return (tk_tprogram_result){ .ok = false, .as.error = surface_at(it.file, line, col, ts.as.error) };
            cur = ts.as.value.env;
            items = tk_titem_list_push(items, (tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node });
            mainbody = tk_tstmt_list_push(mainbody, ts.as.value.node);
            continue;
        }
        tk_env ienv = c.as.value.env; ienv.cur_ns = it.namespace;
        tk_titem_result ti = tk_type_item(it, ienv, types);
        if (!ti.ok) return (tk_tprogram_result){ .ok = false, .as.error = surface_at(it.file, line, col, ti.as.error) };
        items = tk_titem_list_push(items, ti.as.value);
    }
    { tk_str seen[TK_MAX_LABELS]; size_t nseen = 0;
      const char *why = check_labels(mainbody.ptr, mainbody.len, NULL, false, seen, &nseen);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }

    // Prepend dep items to the result (dep + project together).
    tk_titem_list all = tk_titem_list_empty();
    for (size_t i = 0; i < dep_prog.nitems; i += 1)
        all = tk_titem_list_push(all, dep_prog.items[i]);
    for (size_t i = 0; i < items.len; i += 1)
        all = tk_titem_list_push(all, items.ptr[i]);
    tk_free0(items.ptr);

    tk_tprogram tp = { .items = all.ptr, .nitems = all.len };
    { tk_error ae = tk_analyze_program(tp);
      if (ae.message) return (tk_tprogram_result){ .ok = false, .as.error = ae }; }
    // (S4b) MONOMORPHIZE the full (dep + project) tree — see tk_type_program. No-op when no generics.
    return tk_monomorphize(tp, types);
}

tk_tprogram_result tk_type_program(tk_program program) {
    tk_collected_result c = tk_collect(program);
    if (!c.ok) return (tk_tprogram_result){ .ok = false, .as.error = c.as.error };
    // W-vis-enforce: enforce the module system (namespace qualification + pub/exp) over the
    // whole flattened program before typing — fail loud on any cross-namespace violation (M.1).
    { const char *why = tk_check_modules(program, c.as.value.types);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }
    // (S4 type-generics) stamp concrete decls for written generic uses before typing (no-op when none).
    tk_type_table types = tk_instantiate_types(program, c.as.value.types);
    tk_titem_list items = tk_titem_list_empty();
    tk_tstmt_list mainbody = tk_tstmt_list_empty();   // loose top-level statements (the virtual-main), for label validation
    // Thread the env across LOOSE top-level statements (the virtual-main): a `let a`
    // must enter scope for the statements that follow it (mirrors type_block's env
    // threading). Non-statement items (functions/types/uses) are typed against the
    // collected env and do not advance it.
    tk_env cur = c.as.value.env;
    for (size_t i = 0; i < program.len; i += 1) {
        tk_item it = program.items[i];
        cur.cur_ns = it.namespace;   // (#41) the namespace being type-checked — drives same-ns call resolution
        // W-loc-2: the item's source file + the decl's line:col, so a type error in the
        // flattened program resolves to file:line:col (loose statements carry no loc → file only).
        uint32_t line = it.tag == TK_ITEM_FUNCTION  ? it.as.function.line
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.line : 0;
        uint32_t col  = it.tag == TK_ITEM_FUNCTION  ? it.as.function.col
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.col : 0;
        if (it.tag == TK_ITEM_STATEMENT) {
            tk_typed_stmt_result ts = tk_type_statement(it.as.statement, cur, types);
            if (!ts.ok) {   // (C1-POS) prefer the failing expr's own position; the item's is the fallback
                uint32_t el = ts.as.error.line ? ts.as.error.line : line;
                uint32_t ec = ts.as.error.line ? ts.as.error.col  : col;
                // (C1.8) located message string AND structured file/line/col + preserved expected/actual
                return (tk_tprogram_result){ .ok = false, .as.error = surface_at(it.file, el, ec, ts.as.error) };
            }
            cur = ts.as.value.env;   // advance scope for subsequent statements
            items = tk_titem_list_push(items, (tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node });
            mainbody = tk_tstmt_list_push(mainbody, ts.as.value.node);
            continue;
        }
        tk_env ienv = c.as.value.env; ienv.cur_ns = it.namespace;   // (#41) resolve the body's calls in the item's ns
        tk_titem_result ti = tk_type_item(it, ienv, types);
        if (!ti.ok) {   // (C1-POS) prefer the failing expr's own position; the item's is the fallback
            uint32_t el = ti.as.error.line ? ti.as.error.line : line;
            uint32_t ec = ti.as.error.line ? ti.as.error.col  : col;
            // (C1.8) located message string AND structured file/line/col + preserved expected/actual
            return (tk_tprogram_result){ .ok = false, .as.error = surface_at(it.file, el, ec, ti.as.error) };
        }
        items = tk_titem_list_push(items, ti.as.value);
    }
    { tk_str seen[TK_MAX_LABELS]; size_t nseen = 0;   // W5-cf-2: validate labels in the virtual-main
      const char *why = check_labels(mainbody.ptr, mainbody.len, NULL, false, seen, &nseen);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tprogram tp = { .items = items.ptr, .nitems = items.len };
    // Phase 5 — init analysis: unused-local ERROR + unused-private-fn WARNING (initanalysis.c).
    // Runs last, over the fully typed program; prints warnings, returns the first hard error.
    { tk_error ae = tk_analyze_program(tp);
      if (ae.message) return (tk_tprogram_result){ .ok = false, .as.error = ae }; }
    // (S4b) MONOMORPHIZE — stamp concrete copies of generic fns + rewrite generic calls, so
    // codegen/VM/tests receive a tree with only concrete functions. No-op (byte-identical) when
    // the program has no generic function. Runs AFTER analyze so the original generic fn + its
    // call are seen by init analysis (no false unused-private-fn warning).
    return tk_monomorphize(tp, types);
}
