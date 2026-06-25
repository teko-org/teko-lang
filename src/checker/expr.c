// src/checker/expr.c   (namespace 'teko::checker')
// The expression-typing half of the typed pass (the evolved check_expr family):
// leaves, operators (B.22), call, cast (C2), field access (C3), and the if/match
// VALUE forms (B.20 / B.15). Pairs expr.tks. The statement/function/item/program
// typers live in typer.c — the two TUs see each other via expr.h + typer_internal.h.
#include "expr.h"
#include "typer_internal.h"   // tk_type_block (statement side, for if/match bodies)
#include <string.h>           // memcmp (string-span compares)

// shared from match.c (E5b-2), promoted to non-static for reuse:
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
// (tk_exhaustive is declared in typer_internal.h)

// ---- shared small helpers (one definition here; expr.h declares them for typer.c) ----
tk_texpr      *tk_box(tk_texpr t) { tk_texpr *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }
tk_type        tk_prim_t(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
tk_type        tk_void_t(void)           { return (tk_type){ .tag = TK_TYPE_VOID }; }
bool           tk_is_bool(tk_type t)     { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
tk_texpr_result tk_xok(tk_texpr t)     { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
tk_texpr_result tk_xerr(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }
tk_texpr_result tk_xferr(tk_error e)   { return (tk_texpr_result){ .ok = false, .as.error = e }; }

// short aliases for local use (terse, matches the original site spellings).
// NOTE: no `prim` alias — it would clobber the `.as.prim` union member; call tk_prim_t directly.
#define box   tk_box
#define xok   tk_xok
#define xerr  tk_xerr
#define xferr tk_xferr

// ---- file-local predicates (B.22 core — see expr.tks) ----
static bool is_bool(tk_type t)      { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
static bool is_integer(tk_type t)   { return t.tag == TK_TYPE_PRIM && tk_prim_is_int(t.as.prim); }
static bool is_float(tk_type t)     { return t.tag == TK_TYPE_PRIM && tk_prim_is_float(t.as.prim); }
// B.38 / §5: floats join ints in the native numeric set. "numeric" = int OR float
// (bool is NOT numeric). Arithmetic (+ - * /) and comparisons accept either family;
// bitwise/shift stay integer-only (no float bit-twiddling — see type_binary).
static bool is_numeric(tk_type t)   { return is_integer(t) || is_float(t); }
static bool is_comparable(tk_type a, tk_type b) {
    if (is_integer(a) && is_integer(b)) return true;   // any int ⋚ any int (B.22 compare)
    if (is_float(a)   && is_float(b))   return tk_type_eq(&a, &b);   // floats: same width only
    return tk_type_eq(&a, &b);
}
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
// the strictly-arithmetic ops (+ - * /), legal on floats AND ints; bitwise/% are int-only.
static bool op_is_arith(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS ||
           op == TK_TOKEN_STAR || op == TK_TOKEN_SLASH;
}
static bool op_is_arith_bitwise(tk_token_kind op) {
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS || op == TK_TOKEN_STAR ||
           op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

// growable lists for the typed children (teko::list realized — file-local stamps).
TK_LIST(tk_tcmp_term,  tk_tcmp_list)
TK_LIST(tk_texpr,      tk_texpr_list)
TK_LIST(tk_tarm,       tk_tarm_list)

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
    if (tk_type_is_void(&lt) || tk_type_is_void(&rt)) return xerr("a `void` expression cannot be an operand (M.1)");
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return xerr("shift needs integer operands (no float bit-shifts — B.38)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith(b.op)) {
        // + - * / over the native numeric set (B.38): ints AND floats. Same-type only
        // (B.22 — no promotion, no mixed int/float; cross-family needs an explicit `to`).
        if (!is_numeric(lt)) return xerr("arithmetic needs a numeric operand (int or float)");
        if (!tk_type_eq(&lt, &rt)) return xerr("operands must be the same type (no promotion, no mixed int/float — B.22; use `to`)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith_bitwise(b.op)) {   // the bitwise/% remainder (%, & | ^) — integer-only
        if (!is_integer(lt)) return xerr("bitwise/remainder needs an integer (not a float — B.38)");
        if (!tk_type_eq(&lt, &rt)) return xerr("operands must be the same type (no promotion — B.22)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (b.op == TK_TOKEN_ANDAND || b.op == TK_TOKEN_OROR) {   // logical && / || — bool operands, bool result, short-circuit (matches C + the VM)
        if (!is_bool(lt) || !is_bool(rt)) return xerr("logical `&&`/`||` need bool operands");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = tk_prim_t(TK_PRIM_BOOL), .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    return xerr("not a binary operator");
}

static tk_texpr_result type_unary(tk_unary u, tk_env env, tk_type_table table) {
    tk_texpr_result o = tk_typer_expr(*u.operand, env, table); if (!o.ok) return o;
    tk_type ot = o.as.value.type;
    if (tk_type_is_void(&ot)) return xerr("a `void` expression cannot be an operand (M.1)");
    if (u.op == TK_TOKEN_MINUS) {   // negation: any numeric (int or float — B.38)
        if (!is_numeric(ot)) return xerr("unary `-` needs a numeric operand (int or float)");
        return xok((tk_texpr){ .tag = TK_TEXPR_UNARY, .type = ot, .as.unary = { u.op, box(o.as.value) } });
    }
    if (u.op == TK_TOKEN_TILDE) {   // bitwise complement: integer-only (no float bit-flip — B.38)
        if (!is_integer(ot)) return xerr("unary `~` needs an integer (not a float — B.38)");
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
    if (tk_type_is_void(&prev)) return xerr("a `void` expression cannot be an operand (M.1)");
    tk_tcmp_list terms = tk_tcmp_list_empty();
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_texpr_result cur = tk_typer_expr(*c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (tk_type_is_void(&cur.as.value.type)) return xerr("a `void` expression cannot be an operand (M.1)");
        if (!is_comparable(prev, cur.as.value.type)) return xerr("operands are not comparable");
        terms = tk_tcmp_list_push(terms, (tk_tcmp_term){ c.rest[i].op, box(cur.as.value) });
        prev = cur.as.value.type;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE, .type = tk_prim_t(TK_PRIM_BOOL),
                           .as.compare = { box(f.as.value), terms.ptr, terms.len } });
}

static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table) {
    tk_str name = c.callee.segments[c.callee.len - 1].name;
    tk_type_result ftr = tk_env_lookup(env, name);   // user functions resolve first;
    if (!ftr.ok) ftr = tk_builtin_fn(name);          // injected, non-shadowable stdlib is the fallback
    if (!ftr.ok) return xferr(ftr.as.error);
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return xerr("not a function");
    if (c.nargs != ft.as.func.nparams) return xerr("wrong number of arguments");
    tk_texpr_list args = tk_texpr_list_empty();
    for (size_t i = 0; i < c.nargs; i += 1) {
        tk_texpr_result a = tk_typer_expr(c.args[i], env, table); if (!a.ok) return a;
        if (tk_type_is_void(&a.as.value.type)) return xerr("a `void` expression cannot be passed as an argument (M.1)");
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
// the unsigned 128-bit upper bounds, one per unsigned width (the carrier is a SIGNED
// __int128, so a >= 0 guard precedes the bound compare — a negative literal never fits
// an unsigned type). u128 max (2^128-1) exceeds the carrier, so its bound is the carrier
// max and the only gate is non-negativity (B.38 — fail-early range, M.1).
static unsigned __int128 u_max(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return (unsigned __int128)0xFFu;
        case TK_PRIM_U16:  return (unsigned __int128)0xFFFFu;
        case TK_PRIM_U32:  return (unsigned __int128)0xFFFFFFFFu;
        case TK_PRIM_U64:  return (unsigned __int128)0xFFFFFFFFFFFFFFFFull;
        default:           return ~(unsigned __int128)0;   // U128: full width
    }
}
// the signed range [min,max] per signed width, as __int128 (all signed widths up to and
// including i128 fit the carrier — i128 spans the carrier exactly).
static __int128 s_min(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8:   return (__int128)INT64_C(-128);
        case TK_PRIM_I16:  return (__int128)INT64_C(-32768);
        case TK_PRIM_I32:  return (__int128)INT64_C(-2147483648);
        case TK_PRIM_I64:  return (__int128)INT64_MIN;
        default:           return (__int128)1 << 127;   // I128 min = -2^127
    }
}
static __int128 s_max(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8:   return (__int128)INT64_C(127);
        case TK_PRIM_I16:  return (__int128)INT64_C(32767);
        case TK_PRIM_I32:  return (__int128)INT64_C(2147483647);
        case TK_PRIM_I64:  return (__int128)INT64_MAX;
        default:           return ~((__int128)1 << 127);   // I128 max = 2^127-1
    }
}
// does an INTEGER literal `v` fit the integer prim `k`? (B.38 — extended to 128-bit.)
// floats are handled separately (float_fits); bool is never a literal target here.
static bool value_fits(__int128 v, tk_prim_kind k) {
    if (tk_prim_is_float(k)) return false;   // an int literal does not fit a float type (N1/N2)
    if (tk_prim_is_signed(k)) return v >= s_min(k) && v <= s_max(k);
    if (tk_prim_is_int(k))    return v >= 0 && (unsigned __int128)v <= u_max(k);
    return false;                            // BOOL — guarded by the caller
}
// does a FLOAT literal fit the float prim `k`? Seed ruling (§5): a float constant is
// accepted into ANY float width — precision/range loss of float CONSTANTS is permissive
// for now (the runtime/codegen rounding-loss note in §5 still applies). NOTE: tightening
// f16/f32 over-range to a compile error is deferred (no fail-early for float consts yet).
static bool float_fits(double v, tk_prim_kind k) { (void)v; return tk_prim_is_float(k); }
// C6: an annotated binding whose value type ≠ T. A numeric literal adopts T if it fits (leaf stays
// i64 — Side D); a non-literal mismatch or out-of-range literal is rejected. NULL = ok.
// Used by the typed type_binding in typer.c (reuses value_fits).
const char *annotated_literal_reason(tk_expr value, tk_type ann) {
    if (value.tag != TK_EXPR_NUMBER || ann.tag != TK_TYPE_PRIM) return "value type does not match annotation";
    if (value.as.number.is_float) {   // a float literal (3.14, 1.5e3) — adopts a float annotation only (N2)
        if (!tk_prim_is_float(ann.as.prim)) return "a float literal cannot be annotated as an integer type (B.38)";
        if (!float_fits(value.as.number.fval, ann.as.prim)) return "float literal out of range for the annotated type (M.1 — fail early)";
        return NULL;
    }
    // an integer literal — adopts an integer annotation only; never a float type (N1).
    if (tk_prim_is_float(ann.as.prim)) return "an integer literal cannot be annotated as a float type (write it with a `.` — B.38)";
    if (!value_fits(value.as.number.value, ann.as.prim)) return "literal out of range for the annotated type (M.1 — fail early)";
    return NULL;
}
// is `from -> to` a DEFINED conversion? NULL = yes; else the M.3 barrier message. Any numeric ->
// any numeric is defined (B.38 — int↔int incl 128, int↔float, float↔float; the fit/loss is the
// runtime/codegen guard's: trunc toward zero, overflow/NaN/∞ → panic, §5). Only Bool / non-numeric
// are rejected. byte casts AS u8 (B.36 "byte = u8 newtype"): the effective prim kind for range/cast.
// false for bool / non-numeric (no cast kind); true with *out set otherwise — floats DO yield a kind
// (they are numeric). M.5 — shared by cast_reason + the constant-range check in type_cast.
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
    return NULL;                                                // any numeric -> any numeric (B.38; byte AS u8; floats incl)
}
bool tk_cast_ok(tk_type from, tk_type to) { return cast_reason(from, to) == NULL; }

static tk_texpr_result type_cast(tk_cast c, tk_env env, tk_type_table table) {
    tk_texpr_result inner = tk_typer_expr(*c.expr, env, table); if (!inner.ok) return inner;
    if (tk_type_is_void(&inner.as.value.type)) return xerr("a `void` expression cannot be cast (M.1)");
    tk_type_result tgt = tk_resolve_type(c.target, table);     if (!tgt.ok) return xferr(tgt.as.error);
    const char *why = cast_reason(inner.as.value.type, tgt.as.value);
    if (why != NULL) return xerr(why);
    // fail early (M.1): a constant literal already out of the target's range is a compile error.
    // The target's effective kind comes from cast_kind, so `… to byte` checks the U8 range (0..255).
    // Only the int-literal → int-target case is statically range-checkable here: an integer
    // constant whose value cannot fit the target integer is rejected now (B.38, incl 128-bit).
    // Float→int / int→float / float→float constant conversions are runtime-guarded (trunc toward
    // zero, overflow/NaN/∞ → panic — §5); the checker permits them and leaves the fit to codegen/VM.
    tk_prim_kind ck;
    if (c.expr->tag == TK_EXPR_NUMBER && !c.expr->as.number.is_float
        && cast_kind(tgt.as.value, &ck) && tk_prim_is_int(ck)
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
    // `.len` on a `str` or a slice (`[]T`) -> u64 (W5-idx). This precedes the struct-field
    // path: a str/slice has no struct body, so `len` is a built-in length, not a field.
    if ((recv.as.value.type.tag == TK_TYPE_STR || recv.as.value.type.tag == TK_TYPE_SLICE)
        && tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"len", 3 })) {
        return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = tk_prim_t(TK_PRIM_U64),
                               .as.field_access = { box(recv.as.value), fa.field } });
    }
    if (recv.as.value.type.tag != TK_TYPE_NAMED) return xerr("field access requires a struct receiver");
    tk_decl_result decl = tk_type_table_find(table, recv.as.value.type.as.named.name);
    if (!decl.ok) return xerr("unknown type for field access");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("type is not a struct (no fields)");
    tk_type_result ft = field_type(decl.as.value.body.as.struct_body, fa.field, table);
    if (!ft.ok) return xerr("no such field");
    return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = ft.as.value,
                           .as.field_access = { box(recv.as.value), fa.field } });
}

// ---- subscript index `recv[index]` (W5-idx) ----
// The receiver is a `str` (-> byte) or a slice `[]T` (-> element type T); the index must be
// an integer prim. str indexing/.len is fully lowered in both backends; slice values can't
// be constructed yet, so a slice index type-checks here but is an honest stop in VM/codegen.
static tk_texpr_result type_index(tk_index ix, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_typer_expr(*ix.receiver, env, table); if (!recv.ok) return recv;
    tk_texpr_result idx  = tk_typer_expr(*ix.index,    env, table); if (!idx.ok)  return idx;
    if (!is_integer(idx.as.value.type))
        return xerr("a subscript index must be an integer");
    tk_type rt = recv.as.value.type;
    if (rt.tag == TK_TYPE_STR)
        return xok((tk_texpr){ .tag = TK_TEXPR_INDEX, .type = (tk_type){ .tag = TK_TYPE_BYTE },
                               .as.index = { box(recv.as.value), box(idx.as.value) } });
    if (rt.tag == TK_TYPE_SLICE)
        return xok((tk_texpr){ .tag = TK_TEXPR_INDEX, .type = *rt.as.slice.element,
                               .as.index = { box(recv.as.value), box(idx.as.value) } });
    return xerr("cannot index a value of this type");
}

// ---- nullability nodes (LEGISLATION §75 booleans; REBOOT_PLAN §202/§203 null/?./??) ----
// box a tk_type onto the heap (the optional's inner) — like resolve.c's box, local here.
static tk_type *tk_box_type_val(tk_type t) { tk_type *p = malloc(sizeof *p); if (!p) abort(); *p = t; return p; }

// `recv?.field` (REBOOT_PLAN §203): the receiver must be an OPTIONAL of a named struct;
// the result is the field's type made optional — null propagates. The struct-field layer
// is the same one type_field_access uses; if the inner is not a known struct we honest-error
// (M.3) rather than crash.
static tk_texpr_result type_safe_field_access(tk_safe_field_access sfa, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_typer_expr(*sfa.receiver, env, table); if (!recv.ok) return recv;
    tk_type rt = recv.as.value.type;
    if (rt.tag != TK_TYPE_OPTIONAL)
        return xerr("safe field access `?.` needs an optional receiver (`T?`) — use `.` for a non-optional (REBOOT_PLAN §203)");
    tk_type inner = *rt.as.optional.inner;
    if (inner.tag != TK_TYPE_NAMED)
        return xerr("safe field access on a non-struct optional is not yet supported (the struct-field layer is pending — M.3)");
    tk_decl_result decl = tk_type_table_find(table, inner.as.named.name);
    if (!decl.ok) return xerr("unknown type for safe field access");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("type is not a struct (no fields)");
    tk_type_result ft = field_type(decl.as.value.body.as.struct_body, sfa.field, table);
    if (!ft.ok) return xerr("no such field");
    // the result is `(field-type)?` — null-propagating; an already-optional field stays as-is.
    tk_type result = ft.as.value.tag == TK_TYPE_OPTIONAL ? ft.as.value
                   : (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional.inner = tk_box_type_val(ft.as.value) };
    return xok((tk_texpr){ .tag = TK_TEXPR_SAFE_FIELD_ACCESS, .type = result,
                           .as.safe_field_access = { box(recv.as.value), sfa.field } });
}

// `a ?? b` (REBOOT_PLAN §203): the left must be an OPTIONAL `T?`; the right is `T` (the
// fallback, result type `T`) or itself `T?` (result stays `T?`). A bare `null` on the right
// is typed against the left's optional (the one place null gets its context — REBOOT §202).
static tk_texpr_result type_coalesce(tk_coalesce co, tk_env env, tk_type_table table) {
    tk_texpr_result l = tk_typer_expr(*co.left, env, table); if (!l.ok) return l;
    tk_type lt = l.as.value.type;
    if (lt.tag != TK_TYPE_OPTIONAL)
        return xerr("`??` needs an optional left operand (`T?`) — its right is the fallback (REBOOT_PLAN §203)");
    tk_type unwrapped = *lt.as.optional.inner;   // the `T` behind the `T?`
    // RIGHT arm. A bare `null` adopts the left's optional type (`x ?? null` ⇒ `T?`); otherwise
    // type it normally and require it to be `T` (result `T`) or `T?` (result `T?`).
    if (co.right->tag == TK_EXPR_NULL) {
        tk_texpr rnull = { .tag = TK_TEXPR_NULL, .type = lt, .as.null_lit = { 0 } };
        return xok((tk_texpr){ .tag = TK_TEXPR_COALESCE, .type = lt,
                               .as.coalesce = { box(l.as.value), box(rnull) } });
    }
    tk_texpr_result r = tk_typer_expr(*co.right, env, table); if (!r.ok) return r;
    tk_type rt = r.as.value.type;
    if (tk_type_is_void(&rt)) return xerr("a `void` expression cannot be the `??` fallback (M.1)");
    if (tk_type_eq(&rt, &unwrapped))   // `T? ?? T` ⇒ T (the common, unwrapping case)
        return xok((tk_texpr){ .tag = TK_TEXPR_COALESCE, .type = unwrapped,
                               .as.coalesce = { box(l.as.value), box(r.as.value) } });
    if (tk_type_eq(&rt, &lt))          // `T? ?? T?` ⇒ T? (fallback is itself optional)
        return xok((tk_texpr){ .tag = TK_TEXPR_COALESCE, .type = lt,
                               .as.coalesce = { box(l.as.value), box(r.as.value) } });
    return xerr("the `??` fallback must be `T` or `T?` for the left's optional `T?` (REBOOT_PLAN §203)");
}

// ---- struct literal `Name { f = v, … }` (W4a) ----
// Resolve the named struct, require EXACTLY the declared fields (once each — M.1), check
// per-field value types, and emit the typed children in the struct's DECLARED order (so the
// VM and codegen lower identically). A numeric literal adopts a fitting field prim type
// (parity with annotated bindings).
static tk_texpr_result type_struct_lit(tk_struct_lit sl, tk_env env, tk_type_table table) {
    tk_type_result nt = resolve_named(sl.type_path, table);
    if (!nt.ok) return xferr(nt.as.error);
    // The builtin `error` is the one non-NAMED constructible type — `error { message = <str> }`
    // (B.1: error-as-value, rep { message: str }). It is built-in, not a user struct.
    if (nt.as.value.tag == TK_TYPE_ERROR) {
        tk_str msg_field = { (const tk_byte *)"message", 7 };
        if (sl.nfields != 1 || !tk_str_eq(sl.field_names[0], msg_field))
            return xerr("`error` is constructed as `error { message = <str> }` (one `message` field)");
        tk_texpr_result vt = tk_typer_expr(sl.field_vals[0], env, table);
        if (!vt.ok) return vt;
        if (vt.as.value.type.tag != TK_TYPE_STR)
            return xerr("`error`'s `message` must be a `str`");
        tk_str  *names = malloc(sizeof *names); if (!names) abort(); names[0] = msg_field;
        tk_texpr *vals = malloc(sizeof *vals);  if (!vals)  abort(); vals[0]  = vt.as.value;
        return xok((tk_texpr){ .tag = TK_TEXPR_STRUCT_INIT, .type = (tk_type){ .tag = TK_TYPE_ERROR },
                               .as.struct_init = { names, vals, 1 } });
    }
    if (nt.as.value.tag != TK_TYPE_NAMED) return xerr("a struct literal requires a named struct type");
    tk_decl_result decl = tk_type_table_find(table, nt.as.value.as.named.name);
    if (!decl.ok) return xerr("unknown type in a struct literal");
    if (decl.as.value.body.tag != TK_BODY_STRUCT) return xerr("struct-literal target is not a struct");
    tk_struct_body sb = decl.as.value.body.as.struct_body;
    if (sl.nfields != sb.n_fields) return xerr("a struct literal must set exactly the declared fields (count mismatch)");

    tk_str  *names = malloc((sb.n_fields ? sb.n_fields : 1) * sizeof *names); if (!names) abort();
    tk_texpr *vals = malloc((sb.n_fields ? sb.n_fields : 1) * sizeof *vals); if (!vals) abort();
    for (size_t d = 0; d < sb.n_fields; d += 1) {
        tk_str fname = sb.fields[d].name;
        size_t found = sl.nfields, hits = 0;          // locate the provided value for this declared field
        for (size_t i = 0; i < sl.nfields; i += 1)
            if (tk_str_eq(sl.field_names[i], fname)) { found = i; hits += 1; }
        if (hits == 0) { free(names); free(vals); return xerr("a struct literal is missing a declared field"); }
        if (hits > 1)  { free(names); free(vals); return xerr("a struct literal sets a field more than once"); }
        tk_type_result ft = tk_resolve_type(sb.fields[d].type_ann, table);
        if (!ft.ok) { free(names); free(vals); return xferr(ft.as.error); }
        tk_texpr_result vt = tk_typer_expr(sl.field_vals[found], env, table);
        if (!vt.ok) { free(names); free(vals); return vt; }
        tk_texpr val = vt.as.value;
        // literal adaptation: a fitting numeric literal adopts the field's prim type.
        if (val.tag == TK_TEXPR_NUMBER && ft.as.value.tag == TK_TYPE_PRIM && !tk_type_eq(&val.type, &ft.as.value)) {
            bool fits = val.as.number.is_float
                ? tk_prim_is_float(ft.as.value.as.prim)
                : (tk_prim_is_int(ft.as.value.as.prim) && value_fits(val.as.number.value, ft.as.value.as.prim));
            if (fits) val.type = ft.as.value;
        }
        if (!tk_type_eq(&val.type, &ft.as.value)) {
            free(names); free(vals);
            return xerr("a struct-literal field value does not match the field's declared type");
        }
        names[d] = fname; vals[d] = val;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_STRUCT_INIT, .type = nt.as.value,
                           .as.struct_init = { names, vals, sb.n_fields } });
}

// ---- string interpolation `$"…{expr}…"` (self-host parity) ----
// Type each hole; SCOPE the hole types to what the corpus needs: a `str` hole passes through,
// an INTEGER hole (any int prim, incl u64) converts to its decimal text, ANY OTHER hole type
// is a clean error (no general to-string — M.3). The result type is `str`. The typed pieces
// are carried verbatim (already decoded by the parser); each typed hole carries its own type
// so VM/codegen know str-passthrough vs int→str.
static tk_texpr_result type_interp(tk_interp in, tk_env env, tk_type_table table) {
    tk_texpr *holes = NULL;
    if (in.nholes > 0) { holes = malloc(in.nholes * sizeof *holes); if (!holes) abort(); }
    for (size_t i = 0; i < in.nholes; i += 1) {
        tk_texpr_result h = tk_typer_expr(in.holes[i], env, table);
        if (!h.ok) { free(holes); return h; }
        tk_type ht = h.as.value.type;
        if (ht.tag != TK_TYPE_STR && !is_integer(ht)) {
            free(holes);
            return xerr("interpolation hole must be a str or an integer");
        }
        holes[i] = h.as.value;
    }
    // carry the decoded pieces verbatim (the parser already resolved escapes).
    tk_str *pieces = NULL;
    if (in.npieces > 0) { pieces = malloc(in.npieces * sizeof *pieces); if (!pieces) abort();
        for (size_t i = 0; i < in.npieces; i += 1) pieces[i] = in.pieces[i]; }
    return xok((tk_texpr){ .tag = TK_TEXPR_INTERP, .type = (tk_type){ .tag = TK_TYPE_STR },
                           .as.interp = { pieces, in.npieces, holes, in.nholes } });
}

// forward decls for the if/match VALUE forms (mutual recursion: expr ↔ block).
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table);

// ---- the expression dispatch (the evolved check_expr) ----
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table) {
    switch (e.tag) {
        case TK_EXPR_NUMBER: {
            // N1/N2 (B.38): the literal leaf adopts a DEFAULT here — i64 for an integer literal,
            // f64 for a float literal. An annotation/expected type re-adopts a fitting literal
            // at the binding/cast site (annotated_literal_reason / type_cast), which is where the
            // full int set (incl u128/i128) and the float set (f16/f32) come into play.
            tk_number n = e.as.number;
            tk_prim_kind k = n.is_float ? TK_PRIM_F64 : TK_PRIM_I64;
            return xok((tk_texpr){ .tag = TK_TEXPR_NUMBER, .type = tk_prim_t(k),
                                   .as.number = { n.is_float, n.value, n.fval } });
        }
        case TK_EXPR_STR:    return xok((tk_texpr){ .tag = TK_TEXPR_STR,  .type = (tk_type){ .tag = TK_TYPE_STR },  .as.str  = { e.as.str.text } });
        case TK_EXPR_BYTE:   return xok((tk_texpr){ .tag = TK_TEXPR_BYTE, .type = (tk_type){ .tag = TK_TYPE_BYTE }, .as.byte = { e.as.byte.value } });
        case TK_EXPR_BOOL:   // `true`/`false` — the bool prim (LEGISLATION §75)
            return xok((tk_texpr){ .tag = TK_TEXPR_BOOL, .type = tk_prim_t(TK_PRIM_BOOL),
                                   .as.boolean = { e.as.boolean.value } });
        case TK_EXPR_NULL:
            // A bare `null` is ambiguous — its optional inner is inferred from context
            // (REBOOT_PLAN §202). The only internal site with that context is `x ?? null`,
            // handled in type_coalesce. Elsewhere (a binding without `: T?`, a loose
            // statement) the type is unknown, so we honest-error (M.3) instead of guessing.
            return xerr("`null` needs a known optional type from context (e.g. `x ?? null`) — REBOOT_PLAN §202");
        case TK_EXPR_VAR:    return type_var(e.as.var, env);
        case TK_EXPR_BINARY: return type_binary(e.as.binary, env, table);
        case TK_EXPR_UNARY:  return type_unary(e.as.unary, env, table);
        case TK_EXPR_COMPARE:return type_compare(e.as.compare, env, table);
        case TK_EXPR_CALL:   return type_call(e.as.call, env, table);
        case TK_EXPR_IF:     return type_if(e.as.if_expr, env, table);
        case TK_EXPR_MATCH:  return type_match(e.as.match_expr, env, table);
        case TK_EXPR_FIELD_ACCESS: return type_field_access(e.as.field_access, env, table);
        case TK_EXPR_SAFE_FIELD_ACCESS: return type_safe_field_access(e.as.safe_field_access, env, table);
        case TK_EXPR_COALESCE:     return type_coalesce(e.as.coalesce, env, table);
        case TK_EXPR_CAST:         return type_cast(e.as.cast, env, table);
        case TK_EXPR_METHOD_CALL:  return xerr("method typing is deferred (B.29 / M.4)");
        case TK_EXPR_PATH:         return xerr("path-expr typing pending (Enum::Member)");
        case TK_EXPR_STRUCT_LIT:   return type_struct_lit(e.as.struct_lit, env, table);   // W4a
        case TK_EXPR_INDEX:        return type_index(e.as.index, env, table);            // W5-idx
        case TK_EXPR_INTERP:       return type_interp(e.as.interp, env, table);          // $"…{expr}…"
    }
    return xerr("unknown expression");
}

// ---- the value-type a typed block yields (shared with typer.c's if/match stmt forms) ----
tk_type tk_tblock_type(tk_tstatement *stmts, size_t n) {
    if (n == 0) return tk_void_t();
    if (stmts[n - 1].tag == TK_TSTMT_EXPR) return stmts[n - 1].as.expr_stmt.expr.type;
    return tk_void_t();
}

// ---- does a typed block DIVERGE (exit via return/break/continue on every path)? ----
// A block diverges when its trailing statement is a return/break/continue, OR a trailing
// `if` (with else) / `match` whose every branch / arm diverges. Such a block yields no
// trailing value, so match-as-value unification (and the if value form) skips it (B.20).
bool tk_tblock_diverges(const tk_tstatement *stmts, size_t n) {
    if (n == 0) return false;
    const tk_tstatement *last = &stmts[n - 1];
    switch (last->tag) {
        case TK_TSTMT_RETURN:
        case TK_TSTMT_BREAK:
        case TK_TSTMT_CONTINUE:
            return true;
        case TK_TSTMT_EXPR: {
            const tk_texpr *x = &last->as.expr_stmt.expr;
            if (x->tag == TK_TEXPR_IF) {
                if (!x->as.if_expr.has_else) return false;   // the false path falls through
                return tk_tblock_diverges(x->as.if_expr.then_blk, x->as.if_expr.nthen)
                    && tk_tblock_diverges(x->as.if_expr.else_blk, x->as.if_expr.nelse);
            }
            if (x->tag == TK_TEXPR_MATCH) {
                if (x->as.match_expr.narms == 0) return false;
                for (size_t i = 0; i < x->as.match_expr.narms; i += 1)
                    if (!tk_tblock_diverges(x->as.match_expr.arms[i].body, x->as.match_expr.arms[i].nbody))
                        return false;
                return true;
            }
            return false;
        }
        default: return false;
    }
}

// ---- if as a VALUE ----
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table) {
    tk_texpr_result c = tk_typer_expr(*f.cond, env, table); if (!c.ok) return c;
    if (!is_bool(c.as.value.type)) return xerr("an `if` condition must be a bool");
    if (!f.has_else)               return xerr("an `if` used as a value needs an `else`");
    tk_typed_block_result tb = tk_type_block(f.then_blk, f.nthen, env, table); if (!tb.ok) return xferr(tb.as.error);
    tk_typed_block_result eb = tk_type_block(f.else_blk, f.nelse, env, table); if (!eb.ok) return xferr(eb.as.error);
    tk_type tt = tk_tblock_type(tb.as.value.stmts, tb.as.value.n);
    tk_type et = tk_tblock_type(eb.as.value.stmts, eb.as.value.n);
    if (!tk_type_eq(&tt, &et)) return xerr("the `if` branches have different types");
    return xok((tk_texpr){ .tag = TK_TEXPR_IF, .type = tt, .as.if_expr = {
        box(c.as.value), tb.as.value.stmts, tb.as.value.n, true, eb.as.value.stmts, eb.as.value.n } });
}

// ---- one typed arm (shared with typer.c's match STATEMENT form) ----
// The arm body is a STATEMENT BLOCK, typed exactly like an `if` then/else branch (B.20):
// the pattern's bindings extend the env, then the block is typed via tk_type_block. The
// arm's VALUE type is the block's trailing-value type (tk_tblock_type), unless the block
// DIVERGES (return/break/continue) — then it contributes no value (type_match skips it).
tk_tarm_result tk_type_arm(tk_arm a, tk_type subject, tk_env env, tk_type_table table) {
    tk_env_result e2 = tk_check_pattern(a.pattern, subject, env, table);
    if (!e2.ok) return (tk_tarm_result){ .ok = false, .as.error = e2.as.error };
    tk_typed_block_result body = tk_type_block(a.body, a.nbody, e2.as.value, table);
    if (!body.ok) return (tk_tarm_result){ .ok = false, .as.error = body.as.error };
    tk_texpr *guard = NULL;
    if (a.has_when) {
        tk_texpr_result g = tk_typer_expr(a.guard, e2.as.value, table);
        if (!g.ok) return (tk_tarm_result){ .ok = false, .as.error = g.as.error };
        if (!is_bool(g.as.value.type)) return (tk_tarm_result){ .ok = false, .as.error = tk_error_make("a `when` guard must be a bool") };
        guard = box(g.as.value);
    }
    return (tk_tarm_result){ .ok = true, .as.value = { .pattern = a.pattern, .has_when = a.has_when,
        .guard = guard, .body = body.as.value.stmts, .nbody = body.as.value.n } };
}

// ---- match as a VALUE ----
// Each arm body is a typed BLOCK. The match's value type is the common trailing-value type
// of the NON-diverging arms (B.20): a diverging arm (`error as e => return e`) yields no
// value, so it is SKIPPED in unification. If ALL arms diverge the match type is void.
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table) {
    tk_texpr_result s = tk_typer_expr(*m.subject, env, table); if (!s.ok) return s;
    if (m.narms == 0) return xerr("a `match` needs at least one arm");
    tk_tarm_list arms = tk_tarm_list_empty();
    bool have_type = false;
    tk_type first = tk_void_t();
    for (size_t i = 0; i < m.narms; i += 1) {
        tk_tarm_result ai = tk_type_arm(m.arms[i], s.as.value.type, env, table); if (!ai.ok) return xferr(ai.as.error);
        arms = tk_tarm_list_push(arms, ai.as.value);
        if (tk_tblock_diverges(ai.as.value.body, ai.as.value.nbody)) continue;   // skip: no value
        tk_type t = tk_tblock_type(ai.as.value.body, ai.as.value.nbody);
        if (!have_type) { first = t; have_type = true; }
        else if (!tk_type_eq(&t, &first)) return xerr("the `match` arms have different types");
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type, table)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    tk_type result = have_type ? first : tk_void_t();   // all arms diverge → void
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = result, .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}
