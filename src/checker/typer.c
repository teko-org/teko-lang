// src/checker/typer.c — each check_* EVOLVED into a type_* producing the typed node
// (C1: every expression node, statement and item). Predicates are re-declared static
// (the per-file convention of expr.c/ctrl.c/match.c); the pattern + exhaustiveness
// logic is shared from match.c (promoted to non-static for the typed pass).
#include "typer.h"
#include "collect.h"   // tk_collect, tk_collected_result
#include <string.h>    // memcmp (string-span compares)

// shared from match.c (E5b-2), promoted to non-static for reuse:
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
bool          tk_exhaustive(tk_arm *arms, size_t n, tk_type subject);

static tk_texpr *box(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }
static tk_type prim(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
static tk_type unit_t(void)         { return (tk_type){ .tag = TK_TYPE_UNIT }; }
static bool is_bool(tk_type t)      { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t)   { return t.tag == TK_TYPE_PRIM && t.as.prim != TK_PRIM_BOOL; }
static bool is_comparable(tk_type a, tk_type b) { if (is_integer(a) && is_integer(b)) return true; return tk_type_eq(&a, &b); }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}
static tk_texpr_result xok(tk_texpr t)     { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
static tk_texpr_result xerr(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }
static tk_texpr_result xferr(tk_error e)   { return (tk_texpr_result){ .ok = false, .as.error = e }; }

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tcmp_term,  tk_tcmp_list)
TK_LIST(tk_texpr,      tk_texpr_list)
TK_LIST(tk_tarm,       tk_tarm_list)
TK_LIST(tk_tstatement, tk_tstmt_list)
TK_LIST(tk_titem,      tk_titem_list)

// forward decls (mutual recursion: expr ↔ block ↔ statement).
static tk_typed_block_result type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table);
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table);
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table);

// ---- leaves ----
static tk_texpr_result type_var(tk_var v, tk_env env) {
    tk_type_result t = tk_env_lookup(env, v.name); if (!t.ok) return xferr(t.as.error);
    return xok((tk_texpr){ .tag = TK_TEXPR_VAR, .type = t.as.value, .as.var = { v.name } });
}

// ---- operators (same B.22 regimes as check_binary/unary/compare) ----
static tk_texpr_result type_binary(tk_binary b, tk_env env, tk_type_table table) {
    tk_texpr_result l = tk_typer_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_texpr_result r = tk_typer_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value.type, rt = r.as.value.type;
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return xerr("shift needs integer operands");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith_bitwise(b.op)) {
        if (!is_integer(lt)) return xerr("arithmetic/bitwise needs an integer");
        if (!tk_type_eq(&lt, &rt)) return xerr("operands must be the same type (no promotion — B.22)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    return xerr("not a binary operator");
}

static tk_texpr_result type_unary(tk_unary u, tk_env env, tk_type_table table) {
    tk_texpr_result o = tk_typer_expr(*u.operand, env, table); if (!o.ok) return o;
    tk_type ot = o.as.value.type;
    if (u.op == TK_TOKEN_MINUS || u.op == TK_TOKEN_TILDE) {
        if (!is_integer(ot)) return xerr("unary -/~ needs an integer");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    if (u.op == TK_TOKEN_BANG) {
        if (!is_bool(ot)) return xerr("! needs a bool");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    return xerr("not a unary operator");
}

static tk_texpr_result type_compare(tk_compare c, tk_env env, tk_type_table table) {
    tk_texpr_result f = tk_typer_expr(*c.first, env, table); if (!f.ok) return f;
    tk_type prev = f.as.value.type;
    tk_tcmp_list terms = tk_tcmp_list_empty();
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_texpr_result cur = tk_typer_expr(*c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (!is_comparable(prev, cur.as.value.type)) return xerr("operands are not comparable");
        terms = tk_tcmp_list_push(terms, (tk_tcmp_term){ c.rest[i].op, box(cur.as.value) });
        prev = cur.as.value.type;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE, .type = prim(TK_PRIM_BOOL),
                           .as.compare = { box(f.as.value), terms.ptr, terms.len } });
}

static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table) {
    tk_str name = c.callee.segments[c.callee.len - 1].name;
    tk_type_result ftr = tk_env_lookup(env, name); if (!ftr.ok) return xferr(ftr.as.error);
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return xerr("not a function");
    if (c.nargs != ft.as.func.nparams) return xerr("wrong number of arguments");
    tk_texpr_list args = tk_texpr_list_empty();
    for (size_t i = 0; i < c.nargs; i += 1) {
        tk_texpr_result a = tk_typer_expr(c.args[i], env, table); if (!a.ok) return a;
        if (!tk_type_eq(&a.as.value.type, &ft.as.func.params[i])) return xerr("argument type mismatch");
        args = tk_texpr_list_push(args, a.as.value);
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = *ft.as.func.ret,
                           .as.call = { c.callee, args.ptr, args.len } });
}

// ---- cast `to` (C2): a DEFINED conversion is allowed; loss is caught, never silent (M.1) ----
// Two-pronged: a constant out-of-range operand → compile error here; a runtime value's loss →
// codegen guard (debug-panic / release-defined, like overflow), deferred (M.4). Bool/non-numeric
// undefined. (Redefines the early compile-forbid rule — see the Redefinitions Index.)
static bool value_fits(int64_t v, tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return v >= 0 && v <= 255;
        case TK_PRIM_U16:  return v >= 0 && v <= 65535;
        case TK_PRIM_U32:  return v >= 0 && v <= 4294967295;
        case TK_PRIM_U64:  return v >= 0;                         // i64 max < u64 max
        case TK_PRIM_I8:   return v >= -128 && v <= 127;
        case TK_PRIM_I16:  return v >= -32768 && v <= 32767;
        case TK_PRIM_I32:  return v >= -2147483648 && v <= 2147483647;
        case TK_PRIM_I64:  return true;                          // v is already an i64
        case TK_PRIM_BOOL: return false;                         // guarded by the caller
    }
    return false;
}
// C6: an annotated binding whose value type ≠ T. A numeric literal adopts T if it fits (leaf stays
// i64 — Side D); a non-literal mismatch or out-of-range literal is rejected. NULL = ok.
// Used by the typed type_binding (reuses value_fits).
const char *annotated_literal_reason(tk_expr value, tk_type ann) {
    if (value.tag != TK_EXPR_NUMBER || ann.tag != TK_TYPE_PRIM) return "value type does not match annotation";
    if (!value_fits(value.as.number.value, ann.as.prim)) return "literal out of range for the annotated type (M.1 — fail early)";
    return NULL;
}
// is `from -> to` a DEFINED conversion? NULL = yes; else the M.3 barrier message. Any integer ->
// any integer is defined (B — the loss is runtime/codegen's). Only Bool / non-numeric are rejected.
// byte casts AS u8 (B.36 "byte = u8 newtype"): the effective prim kind for range/cast rules.
// false for bool / non-numeric (no cast kind); true with *out set otherwise. M.5 — shared by
// cast_reason + the constant-range check in type_cast.
static bool cast_kind(tk_type t, tk_prim_kind *out) {
    if (t.tag == TK_TYPE_PRIM) {
        if (t.as.prim == TK_PRIM_BOOL) return false;
        *out = t.as.prim;
        return true;
    }
    if (t.tag == TK_TYPE_BYTE) { *out = TK_PRIM_U8; return true; }
    return false;
}
static const char *cast_reason(tk_type from, tk_type to) {
    if (tk_type_eq(&from, &to)) return NULL;                     // same type — a no-op
    if ((from.tag == TK_TYPE_PRIM && from.as.prim == TK_PRIM_BOOL)
        || (to.tag == TK_TYPE_PRIM && to.as.prim == TK_PRIM_BOOL))
        return "bool casts are not defined in the seed";        // bool on either end — distinct message (C2)
    tk_prim_kind kf, kt;
    if (!cast_kind(from, &kf)) return "cast not defined for this type in the seed (Named/Str/Slice/… — pending)";
    if (!cast_kind(to,   &kt)) return "cast not defined: a primitive to a non-primitive type";
    return NULL;                                                // any integer/byte -> any integer/byte (B; byte AS u8)
}
bool tk_cast_ok(tk_type from, tk_type to) { return cast_reason(from, to) == NULL; }

static tk_texpr_result type_cast(tk_cast c, tk_env env, tk_type_table table) {
    tk_texpr_result inner = tk_typer_expr(*c.expr, env, table); if (!inner.ok) return inner;
    tk_type_result tgt = tk_resolve_type(c.target, table);     if (!tgt.ok) return xferr(tgt.as.error);
    const char *why = cast_reason(inner.as.value.type, tgt.as.value);
    if (why != NULL) return xerr(why);
    // fail early (M.1): a constant literal already out of the target's range is a compile error.
    // The target's effective kind comes from cast_kind, so `… to byte` checks the U8 range (0..255).
    tk_prim_kind ck;
    if (c.expr->tag == TK_EXPR_NUMBER && cast_kind(tgt.as.value, &ck)
        && !value_fits(c.expr->as.number.value, ck))
        return xerr("constant out of range for the cast target (M.1 — fail early)");
    return xok((tk_texpr){ .tag = TK_TEXPR_CAST, .type = tgt.as.value, .as.cast = { box(inner.as.value) } });
}

// ---- field access `x.field` (C3): read a struct field; `.type` is the field's resolved type ----
static bool tk_str_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }

// non-static: shared with match.c (the FieldPattern case forward-declares it — C7a).
tk_type_result field_type(tk_struct_body sb, tk_str field, tk_type_table table) {
    for (size_t i = 0; i < sb.n_fields; i += 1)
        if (tk_str_eq(sb.fields[i].name, field)) return tk_resolve_type(sb.fields[i].type_ann, table);
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("no such field") };
}

static tk_texpr_result type_field_access(tk_field_access fa, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_typer_expr(*fa.receiver, env, table); if (!recv.ok) return recv;
    if (recv.as.value.type.tag != TK_TYPE_NAMED) return xerr("field access requires a struct receiver");
    tk_decl_result decl = tk_type_table_find(table, recv.as.value.type.as.named.name);
    if (!decl.ok) return xerr("unknown type for field access");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("type is not a struct (no fields)");
    tk_type_result ft = field_type(decl.as.value.body.as.struct_body, fa.field, table);
    if (!ft.ok) return xerr("no such field");
    return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = ft.as.value,
                           .as.field_access = { box(recv.as.value), fa.field } });
}

// ---- the expression dispatch (the evolved check_expr) ----
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table) {
    switch (e.tag) {
        case TK_EXPR_NUMBER: return xok((tk_texpr){ .tag = TK_TEXPR_NUMBER, .type = prim(TK_PRIM_I64), .as.number = { e.as.number.value } });
        case TK_EXPR_STR:    return xok((tk_texpr){ .tag = TK_TEXPR_STR,  .type = (tk_type){ .tag = TK_TYPE_STR },  .as.str  = { e.as.str.text } });
        case TK_EXPR_BYTE:   return xok((tk_texpr){ .tag = TK_TEXPR_BYTE, .type = (tk_type){ .tag = TK_TYPE_BYTE }, .as.byte = { e.as.byte.value } });
        case TK_EXPR_VAR:    return type_var(e.as.var, env);
        case TK_EXPR_BINARY: return type_binary(e.as.binary, env, table);
        case TK_EXPR_UNARY:  return type_unary(e.as.unary, env, table);
        case TK_EXPR_COMPARE:return type_compare(e.as.compare, env, table);
        case TK_EXPR_CALL:   return type_call(e.as.call, env, table);
        case TK_EXPR_IF:     return type_if(e.as.if_expr, env, table);
        case TK_EXPR_MATCH:  return type_match(e.as.match_expr, env, table);
        case TK_EXPR_FIELD_ACCESS: return type_field_access(e.as.field_access, env, table);
        case TK_EXPR_CAST:         return type_cast(e.as.cast, env, table);
        case TK_EXPR_METHOD_CALL:  return xerr("method typing is deferred (B.29 / M.4)");
        case TK_EXPR_PATH:         return xerr("path-expr typing pending (Enum::Member)");
    }
    return xerr("unknown expression");
}

// ---- the value-type a typed block yields ----
static tk_type tblock_type(tk_tstatement *stmts, size_t n) {
    if (n == 0) return unit_t();
    if (stmts[n - 1].tag == TK_TSTMT_EXPR) return stmts[n - 1].as.expr_stmt.expr.type;
    return unit_t();
}

// ---- a typed block: thread the env, collect typed statements ----
static tk_typed_block_result type_block(tk_statement *stmts, size_t n, tk_env env, tk_type_table table) {
    tk_env cur = env;
    tk_tstmt_list out = tk_tstmt_list_empty();
    for (size_t i = 0; i < n; i += 1) {
        tk_typed_stmt_result ts = tk_type_statement(stmts[i], cur, table);
        if (!ts.ok) return (tk_typed_block_result){ .ok = false, .as.error = ts.as.error };
        out = tk_tstmt_list_push(out, ts.as.value.node);
        cur = ts.as.value.env;
    }
    return (tk_typed_block_result){ .ok = true, .as.value = { .stmts = out.ptr, .n = out.len, .env = cur } };
}

// ---- if as a VALUE ----
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    if (!f.has_else)               return xerr("an `if` used as a value needs an `else`");
    tk_typed_block_result tb = type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_typed_block_result eb = type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
    tk_type tt = tblock_type(tb.as.value.stmts, tb.as.value.n);
    tk_type et = tblock_type(eb.as.value.stmts, eb.as.value.n);
    if (!tk_type_eq(&tt, &et)) return xerr("the `if` branches have different types");
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = tt, .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, true, eb.as.value.stmts, eb.as.value.n } });
}

// ---- if as a STATEMENT (value discarded → Unit) ----
static tk_texpr_result type_if_stmt(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    tk_typed_block_result tb = type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_tstatement *eb_stmts = tb.as.value.stmts; size_t eb_n = 0;   // gated by has_else
    if (f.has_else) {
        tk_typed_block_result eb = type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
        eb_stmts = eb.as.value.stmts; eb_n = eb.as.value.n;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = unit_t(), .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, f.has_else, eb_stmts, eb_n } });
}

// ---- one typed arm ----
static tk_tarm_result type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table) {
    tk_env_result e2 = tk_check_pattern(a.pattern, subject, env, table);
    if (!e2.ok) return (tk_tarm_result){ .ok = false, .as.error = e2.as.error };
    tk_texpr_result body = tk_typer_expr(a.body, e2.as.value, table);
    if (!body.ok) return (tk_tarm_result){ .ok = false, .as.error = body.as.error };
    tk_texpr *bodyp = box(body.as.value);
    tk_texpr *guard = bodyp;                         // gated by has_when (placeholder reuses body)
    if (a.has_when) {
        tk_texpr_result g = tk_typer_expr(a.guard, e2.as.value, table);
        if (!g.ok) return (tk_tarm_result){ .ok = false, .as.error = g.as.error };
        if (!is_bool(g.as.value.type)) return (tk_tarm_result){ .ok = false, .as.error = tk_error_make("a `when` guard must be a bool") };
        guard = box(g.as.value);
    }
    return (tk_tarm_result){ .ok = true, .as.value = { .pattern = a.pattern, .has_when = a.has_when, .guard = guard, .body = bodyp } };
}

// ---- match as a VALUE ----
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    if (m.narms == 0) return xerr("a `match` needs at least one arm");
    tk_tarm_result a0 = type_arm(m.arms[0], s.as.value.type, env, table); if (!a0.ok) return xferr(a0.as.error);
    tk_type first = a0.as.value.body->type;
    tk_tarm_list arms = tk_tarm_list_empty();
    arms = tk_tarm_list_push(arms, a0.as.value);
    for (size_t i = 1; i < m.narms; i += 1) {
        tk_tarm_result ai = type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        if (!tk_type_eq(&ai.as.value.body->type, &first)) return xerr("the `match` arms have different types");
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = first, .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- match as a STATEMENT (value discarded → Unit) ----
static tk_texpr_result type_match_stmt(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    tk_tarm_list arms = tk_tarm_list_empty();
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_tarm_result ai = type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        arms = tk_tarm_list_push(arms, ai.as.value);
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = unit_t(), .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}

// ---- statements ----
static tk_typed_stmt_result sok(tk_tstatement node, tk_env env) { return (tk_typed_stmt_result){ .ok = true, .as.value = { node, env } }; }
static tk_typed_stmt_result sfail(tk_error e)   { return (tk_typed_stmt_result){ .ok = false, .as.error = e }; }
static tk_typed_stmt_result smsg(const char *m) { return sfail(tk_error_make(m)); }

static tk_typed_stmt_result type_binding(tk_binding b, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_typer_expr(b.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_type bound = v.as.value.type;
    if (b.has_type) {
        tk_type_result a = tk_resolve_type(b.type_ann, table); if (!a.ok) return sfail(a.as.error);
        if (!tk_type_eq(&v.as.value.type, &a.as.value)) {       // C6: a fitting literal adopts T (leaf stays i64)
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
    if (!tk_type_eq(&tb.as.value.type, &v.as.value.type)) return smsg("assigned value does not match the target type");
    tk_tstatement node = { .tag = TK_TSTMT_ASSIGN, .as.assign = { a.name, a.op, v.as.value } };
    return sok(node, env);   // mut rule enforced (B.21)
}

static tk_typed_stmt_result type_return(tk_return r, tk_env env, tk_type_table table) {
    tk_texpr_result v = tk_typer_expr(r.value, env, table); if (!v.ok) return sfail(v.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_RETURN, .as.ret = { r.has_value, v.as.value } };
    return sok(node, env);   // value gated by has_value (B.20); return-type match enforced by tk_type_function's check_returns (C5)
}

static tk_typed_stmt_result type_loop(tk_loop_stmt l, tk_env env, tk_type_table table) {
    tk_typed_block_result tb = type_block(l.body, l.nbody, env, table); if (!tb.ok) return sfail(tb.as.error);
    tk_tstatement node = { .tag = TK_TSTMT_LOOP, .as.loop_stmt = { tb.as.value.stmts, tb.as.value.n } };
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
        case TK_STMT_BREAK:    return sok((tk_tstatement){ .tag = TK_TSTMT_BREAK }, env);
        case TK_STMT_CONTINUE: return sok((tk_tstatement){ .tag = TK_TSTMT_CONTINUE }, env);
        case TK_STMT_EXPR:     return type_exprstmt(s.as.expr_stmt, env, table);
    }
    return smsg("unknown statement");
}

// ---- items + program ----
static tk_type function_return(tk_function f, tk_type_table table) {
    if (!f.has_return) return unit_t();
    tk_type_result r = tk_resolve_type(f.return_type, table);
    return r.ok ? r.as.value : unit_t();   // collect validated signatures; a bad annotation surfaces there
}

// ---- C5: return / final-expr vs the declared return type (see the Teko twin; NULL = ok) ----
static bool assignable_to(tk_type from, tk_type to) {   // B.14 — variant member inclusion
    if (tk_type_eq(&from, &to)) return true;
    if (to.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < to.as.variant.len; i += 1)
            if (tk_type_eq(&from, &to.as.variant.members[i])) return true;
    return false;
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret);   // fwd (mutual)
static const char *check_returns_inexpr(const tk_texpr *e, tk_type ret) {
    if (e->tag == TK_TEXPR_IF) {
        const char *t = check_returns(e->as.if_expr.then_blk, e->as.if_expr.nthen, ret); if (t) return t;
        return check_returns(e->as.if_expr.else_blk, e->as.if_expr.nelse, ret);
    }
    return NULL;   // match-arm returns await the divergence item
}
static const char *check_return_stmt(const tk_tstatement *s, tk_type ret) {
    switch (s->tag) {
        case TK_TSTMT_RETURN:
            if (s->as.ret.has_value)
                return assignable_to(s->as.ret.value.type, ret) ? NULL
                     : "return value does not match the function's declared return type";
            return assignable_to(unit_t(), ret) ? NULL
                 : "bare `return` in a function that declares a non-Unit return type";
        case TK_TSTMT_LOOP: return check_returns(s->as.loop_stmt.body, s->as.loop_stmt.nbody, ret);
        case TK_TSTMT_EXPR: return check_returns_inexpr(&s->as.expr_stmt.expr, ret);
        default:            return NULL;
    }
}
static const char *check_returns(const tk_tstatement *stmts, size_t n, tk_type ret) {
    for (size_t i = 0; i < n; i += 1) { const char *e = check_return_stmt(&stmts[i], ret); if (e) return e; }
    return NULL;
}
static const char *check_trailing_value(const tk_tstatement *stmts, size_t n, tk_type ret) {
    if (n == 0) return NULL;
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag != TK_TSTMT_EXPR) return NULL;   // trailing loop/if/match → no claim (guard)
    return assignable_to(last->as.expr_stmt.expr.type, ret) ? NULL
         : "the function's final expression does not match its declared return type";
}

tk_tfunction_result tk_type_function(tk_function f, tk_env env, tk_type_table table) {
    tk_env local = env;
    for (size_t i = 0; i < f.nparams; i += 1) {           // params immutable (B.21)
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return (tk_tfunction_result){ .ok = false, .as.error = pt.as.error };
        local = tk_env_define(local, f.params[i].name, pt.as.value, false);
    }
    tk_type ret = function_return(f, table);
    tk_typed_block_result tb = type_block(f.body, f.nbody, local, table);
    if (!tb.ok) return (tk_tfunction_result){ .ok = false, .as.error = tb.as.error };
    { const char *why = check_returns(tb.as.value.stmts, tb.as.value.n, ret);        // C5: each `return e` matches
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    { const char *why = check_trailing_value(tb.as.value.stmts, tb.as.value.n, ret); // C5: trailing value (when present)
      if (why) return (tk_tfunction_result){ .ok = false, .as.error = tk_error_make(why) }; }
    tk_tfunction tf = { .name = f.name, .params = f.params, .nparams = f.nparams,
                        .return_type = ret, .body = tb.as.value.stmts, .nbody = tb.as.value.n,
                        .is_exp = f.is_exp, .has_doc = f.has_doc, .doc = f.doc };
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
    tk_titem_list items = tk_titem_list_empty();
    // Thread the env across LOOSE top-level statements (the virtual-main): a `let a`
    // must enter scope for the statements that follow it (mirrors type_block's env
    // threading). Non-statement items (functions/types/uses) are typed against the
    // collected env and do not advance it.
    tk_env cur = c.as.value.env;
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_STATEMENT) {
            tk_typed_stmt_result ts = tk_type_statement(program.items[i].as.statement, cur, c.as.value.types);
            if (!ts.ok) return (tk_tprogram_result){ .ok = false, .as.error = ts.as.error };
            cur = ts.as.value.env;   // advance scope for subsequent statements
            items = tk_titem_list_push(items, (tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = ts.as.value.node });
            continue;
        }
        tk_titem_result ti = tk_type_item(program.items[i], c.as.value.env, c.as.value.types);
        if (!ti.ok) return (tk_tprogram_result){ .ok = false, .as.error = ti.as.error };
        items = tk_titem_list_push(items, ti.as.value);
    }
    return (tk_tprogram_result){ .ok = true, .as.value = { .items = items.ptr, .nitems = items.len } };
}
