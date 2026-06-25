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
    tk_texpr_result v = tk_typer_expr(b.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_type bound = v.as.value.type;
    if (tk_type_is_void(&bound)) return smsg("cannot bind a `void` expression — it yields no value (M.1)");
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return sfail(a.as.error);
        if (!assignable_to(v.as.value.type, a.as.value, table)) {   // B.14 widening (case → variant) OR C6: a fitting literal adopts T (leaf stays i64)
            const char *why = annotated_literal_reason(b.value, a.as.value);
            if (why != NULL) return smsg(why);
        }
        bound = a.as.value;
    }
    tk_tstatement node = { .tag = TK_TSTMT_BINDING, .as.binding = { b.kind, b.target, bound, v.as.value } };
    if (b.target.tag == TK_BIND_SIMPLE)
        return sok(node, tk_env_define(env, b.target.as.simple.name, bound, tk_bind_is_mut(b.kind)));
    return sok(node, env);   // [destructuring binding: refinement]
}

static tk_typed_stmt_result type_assign(tk_assign a, tk_env env, tk_type_table table) {
    tk_binding_result tb = tk_env_lookup_binding(env, a.name); if (!tb.ok) return sfail(tb.as.error);
    if (!tb.as.value.is_mut) return smsg("cannot assign to immutable binding — declare it `mut` (B.21)");
    tk_texpr_result v = tk_typer_expr(a.value, env, table); if (!v.ok) return sfail(v.as.error);
    if (!tk_type_eq(&tb.as.value.type, &v.as.value.type)) {
        if (!tk_literal_adopts(v.as.value, tb.as.value.type)) return smsg("assigned value does not match the target type");
        v.as.value.type = tb.as.value.type;   // a fitting literal adopts the target's type (C6)
    }
    tk_tstatement node = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, v.as.value } };
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
static bool assignable_to(tk_type from, tk_type to, tk_type_table table) {   // B.14 — variant member inclusion
    if (tk_type_eq(&from, &to)) return true;
    // PRESENT-WRAP (REBOOT_PLAN §202): a `T` widens to `T?` (and to `T??`-collapsed `T?`).
    // The destination is `T?`; the source is a plain `T` (or itself a `T?` whose inner is T,
    // already covered by tk_type_eq). Mirrors the variant case→variant widening below; codegen
    // performs the PRESENT wrap via emit_as. A bare null (sentinel optional) is handled by
    // tk_type_eq (it unifies with any optional).
    if (to.tag == TK_TYPE_OPTIONAL && to.as.optional.inner != NULL) {
        if (tk_type_eq(&from, to.as.optional.inner)) return true;        // T → T?
        if (from.tag == TK_TYPE_OPTIONAL) return true;                   // U? / sentinel → T? (eq/sentinel)
    }
    tk_type tv = tk_expand_variant(to, table);   // a NAMED variant → its TK_TYPE_VARIANT (so cases widen in)
    if (tv.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < tv.as.variant.len; i += 1)
            if (tk_type_eq(&from, &tv.as.variant.members[i])) return true;
    return false;
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table);   // fwd (mutual)
static const char *check_returns_inexpr(const tk_texpr *e, tk_type ret, tk_type_table table) {
    if (e->tag == TK_TEXPR_IF) {
        const char *t = check_returns(e->as.if_expr.then_blk, e->as.if_expr.nthen, ret, table); if (t) return t;
        return check_returns(e->as.if_expr.else_blk, e->as.if_expr.nelse, ret, table);
    }
    if (e->tag == TK_TEXPR_MATCH) {   // arm bodies are blocks now — a `return` inside one is checked (B.20)
        for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
            const char *t = check_returns(e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody, ret, table);
            if (t) return t;
        }
    }
    return NULL;
}
static const char *check_return_stmt(const tk_tstatement *s, tk_type ret, tk_type_table table) {
    switch (s->tag) {
        case TK_TSTMT_RETURN:
            if (s->as.ret.has_value)
                return (assignable_to(s->as.ret.value.type, ret, table)
                        || tk_literal_adopts(s->as.ret.value, ret)) ? NULL   // a fitting int/float/byte literal adopts the return type (C6)
                     : "return value does not match the function's declared return type";
            return tk_type_is_void(&ret) ? NULL
                 : "bare `return` in a function that declares a value-returning (non-void) return type";
        case TK_TSTMT_LOOP: return check_returns(s->as.loop_stmt.body, s->as.loop_stmt.nbody, ret, table);
        case TK_TSTMT_EXPR: return check_returns_inexpr(&s->as.expr_stmt.expr, ret, table);
        default:            return NULL;
    }
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table) {
    for (size_t i = 0; i < n; i += 1) { const char *e = check_return_stmt(&stmts[i], ret, table); if (e) return e; }
    return NULL;
}
static const char *check_trailing_value(const tk_tstatement *stmts, size_t n, tk_type ret, tk_type_table table) {
    if (n == 0) return NULL;
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag != TK_TSTMT_EXPR) return NULL;   // trailing loop/if/match → no claim (guard)
    if (tk_texpr_diverges(&last->as.expr_stmt.expr)) return NULL;   // a trailing panic/exit yields no value (M.3)
    return (assignable_to(last->as.expr_stmt.expr.type, ret, table)
            || tk_literal_adopts(last->as.expr_stmt.expr, ret)) ? NULL   // a fitting trailing literal adopts the return type (C6)
         : "the function's final expression does not match its declared return type";
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

tk_tfunction_result tk_type_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    for (size_t i = 0; i < f.nparams; i += 1) {           // params immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return (tk_tfunction_result){ .ok = false, .as.error = pt.as.error };
        local = tk_env_define(local, f.params[i].name, pt.as.value, false);
    }
    tk_type ret = function_return(f, table);
    tk_typed_block_result tb = tk_type_block(f.body, f.nbody, local, table);
    if (!tb.ok) return (tk_tfunction_result){ .ok = false, .as.error = tb.as.error };
    { const char *why = check_returns(tb.as.value.stmts, tb.as.value.n, ret, table);        // C5: each `return e` matches
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    { const char *why = check_trailing_value(tb.as.value.stmts, tb.as.value.n, ret, table); // C5: trailing value (when present)
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    { tk_str seen[TK_MAX_LABELS]; size_t nseen = 0;                                  // W5-cf-2: loop labels resolve + are unique
      const char *why = check_labels(tb.as.value.stmts, tb.as.value.n, NULL, false, seen, &nseen);
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tfunction tf = { .name = f.name, .params = f.params, .nparams = f.nparams,
                        .return_type = ret, .body = tb.as.value.stmts, .nbody = tb.as.value.n,
                        .vis = f.vis, .has_doc = f.has_doc, .doc = f.doc };
    return (tk_tfunction_result){ .ok = true, .as.value = tf };
}

tk_titem_result tk_type_item(tk_item item, tk_env env, tk_type_table table) {
    switch (item.tag) {
        case TK_ITEM_FUNCTION: {
            tk_tfunction_result tf = tk_type_function(item.as.function, env, table);
            if (!tf.ok) return (tk_titem_result){ .ok = false, .as.error = tf.as.error };
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

tk_tprogram_result tk_type_program(tk_program program) {
    tk_collected_result c = tk_collect(program);
    if (!c.ok) return (tk_tprogram_result){ .ok = false, .as.error = c.as.error };
    // W-vis-enforce: enforce the module system (namespace qualification + pub/exp) over the
    // whole flattened program before typing — fail loud on any cross-namespace violation (M.1).
    { const char *why = tk_check_modules(program, c.as.value.types);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_titem_list items = tk_titem_list_empty();
    tk_tstmt_list mainbody = tk_tstmt_list_empty();   // loose top-level statements (the virtual-main), for label validation
    // Thread the env across LOOSE top-level statements (the virtual-main): a `let a`
    // must enter scope for the statements that follow it (mirrors type_block's env
    // threading). Non-statement items (functions/types/uses) are typed against the
    // collected env and do not advance it.
    tk_env cur = c.as.value.env;
    for (size_t i = 0; i < program.len; i += 1) {
        tk_item it = program.items[i];
        // W-loc-2: the item's source file + the decl's line:col, so a type error in the
        // flattened program resolves to file:line:col (loose statements carry no loc → file only).
        uint32_t line = it.tag == TK_ITEM_FUNCTION  ? it.as.function.line
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.line : 0;
        uint32_t col  = it.tag == TK_ITEM_FUNCTION  ? it.as.function.col
                      : it.tag == TK_ITEM_TYPE_DECL ? it.as.type_decl.col : 0;
        if (it.tag == TK_ITEM_STATEMENT) {
            tk_typed_stmt_result ts = tk_type_statement(it.as.statement, cur, c.as.value.types);
            if (!ts.ok) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(tk_diag_at(it.file, line, col, ts.as.error.message)) };
            cur = ts.as.value.env;   // advance scope for subsequent statements
            items = tk_titem_list_push(items, (tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node });
            mainbody = tk_tstmt_list_push(mainbody, ts.as.value.node);
            continue;
        }
        tk_titem_result ti = tk_type_item(it, c.as.value.env, c.as.value.types);
        if (!ti.ok) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(tk_diag_at(it.file, line, col, ti.as.error.message)) };
        items = tk_titem_list_push(items, ti.as.value);
    }
    { tk_str seen[TK_MAX_LABELS]; size_t nseen = 0;   // W5-cf-2: validate labels in the virtual-main
      const char *why = check_labels(mainbody.ptr, mainbody.len, NULL, false, seen, &nseen);
      if (why) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(why) }; }
    return (tk_tprogram_result){ .ok = true, .as.value = { .items = items.ptr, .nitems = items.len } };
}
