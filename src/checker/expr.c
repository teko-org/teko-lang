// src/checker/expr.c   (namespace 'teko::checker')
// The expression-typing half of the typed pass (the evolved check_expr family):
// leaves, operators (B.22), call, cast (C2), field access (C3), and the if/match
// VALUE forms (B.20 / B.15). Pairs expr.tks. The statement/function/item/program
// typers live in typer.c — the two TUs see each other via expr.h + typer_internal.h.
#include "expr.h"
#include "typer_internal.h"   // tk_type_block (statement side, for if/match bodies)
#include "../parser/parse_stmt.h"   // no_expr (DEFARGS placeholder, 2026-07-01)
#include "collect.h"          // tk_effective_class_fields/methods (W10b.CLASS increment 2)
#include <string.h>           // memcmp (string-span compares)

// shared from match.c (E5b-2), promoted to non-static for reuse:
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table);
// (tk_exhaustive is declared in typer_internal.h)

// ---- shared small helpers (one definition here; expr.h declares them for typer.c) ----
tk_texpr      *tk_box(tk_texpr t) { tk_texpr *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }
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

// ---- forward declarations for is_flags_named (defined below, used in type_binary/type_unary) ----
static bool is_flags_named(tk_type t, tk_type_table table);  // (C8.3) fwd — defined at line ~463; used before it

// ---- file-local predicates (B.22 core — see expr.tks) ----
bool is_bool(tk_type t)      { return t.tag == TK_TYPE_PRIM && t.as.prim == TK_PRIM_BOOL; }
bool is_integer(tk_type t)   { return t.tag == TK_TYPE_PRIM && tk_prim_is_int(t.as.prim); }
bool is_float(tk_type t)     { return t.tag == TK_TYPE_PRIM && tk_prim_is_float(t.as.prim); }
// B.38 / §5: floats join ints in the native numeric set. "numeric" = int OR float
// (bool is NOT numeric). Arithmetic (+ - * /) and comparisons accept either family;
// bitwise/shift stay integer-only (no float bit-twiddling — see type_binary).
bool is_numeric(tk_type t)   { return is_integer(t) || is_float(t); }
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
    tk_binding_result b = tk_env_lookup_binding(env, v.name); if (!b.ok) return xferr(b.as.error);
    // (W10a) a bare reference to a top-level FUNCTION used as a VALUE: only tk_env_define_fn sets a
    // non-empty ns, so `ns != "" && type is Func` uniquely identifies a fn reference (a local of
    // closure type has ns=""). It lowers to a tk_closure literal; a plain local emits its name.
    bool is_fn = b.as.value.ns.len > 0 && b.as.value.type.tag == TK_TYPE_FUNC;
    return xok((tk_texpr){ .tag = TK_TEXPR_VAR, .type = b.as.value.type, .as.var = { v.name, is_fn, b.as.value.ns } });
}

// ---- operators (same B.22 regimes as check_binary/unary/compare) ----
static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table);   // forward (defined below)

// (2026-07-01) `~` — binary string concat, arity-polymorphic with unary `~` (bitwise NOT).
// Additive precedence, NOT commutative (honest — Governing Law M.3, the same reasoning `+` was
// rejected for concat: TEKO_LEGISLATION.md "`+` never concatenates").
//
// Flattens a LEFT-ASSOCIATIVE `~` chain (`a ~ b ~ c ~ …`) into a flat piece list, so `a ~ b ~ c`
// desugars to ONE `teko::string::concat(a, b, c)` call — not nested 2-arg calls — reusing the
// params-variadic `concat` (2026-07-01, PR #33).
static void flatten_tilde_chain(tk_expr e, tk_expr **acc, size_t *nacc) {
    if (e.tag == TK_EXPR_BINARY && e.as.binary.op == TK_TOKEN_TILDE) {
        flatten_tilde_chain(*e.as.binary.left, acc, nacc);
        flatten_tilde_chain(*e.as.binary.right, acc, nacc);
        return;
    }
    tk_exprs_push(acc, nacc, e);
}

// Constant-fold adjacent STR-LITERAL pieces at compile time (e.g. `x ~ "a" ~ "b" ~ y` folds
// `"a" ~ "b"` into ONE literal `"ab"` first). If EVERY piece folds into one literal, the whole
// `~` chain collapses to that single literal — zero runtime concat calls.
static tk_str tilde_concat_lit(tk_str a, tk_str b) {
    size_t n = a.len + b.len;
    tk_byte *buf = tk_alloc(n ? n : 1); if (!buf) abort();
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ buf, n };
}
// (2026-07-01) `$"no holes"` / `$@"""no holes"""` — interpolated syntax with ZERO holes (the
// `verbatim` escaping flag is already resolved into `pieces` by parse time, so a 0-hole Interp
// is indistinguishable from a plain literal here). Rare in practice, but foldable — max
// compile-time optimization per the user's explicit ask.
static bool tilde_piece_lit_text(tk_expr e, tk_str *out) {
    if (e.tag == TK_EXPR_STR) { *out = e.as.str.text; return true; }
    if (e.tag == TK_EXPR_INTERP && e.as.interp.nholes == 0 && e.as.interp.npieces == 1) {
        *out = e.as.interp.pieces[0]; return true;
    }
    return false;
}
static void fold_tilde_pieces(tk_expr *pieces, size_t npieces, tk_expr **out, size_t *nout) {
    size_t i = 0;
    while (i < npieces) {
        tk_str t0;
        if (tilde_piece_lit_text(pieces[i], &t0)) {
            tk_str merged = t0;
            size_t j = i + 1;
            tk_str tj;
            while (j < npieces && tilde_piece_lit_text(pieces[j], &tj)) {
                merged = tilde_concat_lit(merged, tj);
                j += 1;
            }
            tk_exprs_push(out, nout, (tk_expr){ .tag = TK_EXPR_STR, .line = pieces[i].line, .col = pieces[i].col, .as.str = { merged } });
            i = j;
        } else {
            tk_exprs_push(out, nout, pieces[i]);
            i += 1;
        }
    }
}
static tk_texpr_result type_tilde_chain(tk_binary b, tk_env env, tk_type_table table) {
    tk_expr *raw = NULL; size_t nraw = 0;
    flatten_tilde_chain((tk_expr){ .tag = TK_EXPR_BINARY, .as.binary = b }, &raw, &nraw);
    tk_expr *folded = NULL; size_t nfolded = 0;
    fold_tilde_pieces(raw, nraw, &folded, &nfolded);
    if (nfolded == 1) { return tk_typer_expr(folded[0], env, table); }   // whole chain was literals — a single literal, no runtime call
    // (fix, 2026-07-01) `segs` MUST outlive this function: the returned tk_texpr's callee path
    // keeps this pointer (type_call copies c.callee BY VALUE, but a tk_path's `.segments` stays a
    // pointer) and is read much later at emit time. A stack-local array here dangles once this
    // function returns -- undetected by earlier single-function tests, but corrupted by ANY
    // further type-checking stack activity (e.g. a second function's calls), producing garbage
    // segment names at codegen. Heap-allocate so the path survives past this call.
    static const tk_segment tilde_segs_lit[3] = { { .name = { (const tk_byte *)"teko", 4 } }, { .name = { (const tk_byte *)"string", 6 } }, { .name = { (const tk_byte *)"concat", 6 } } };
    tk_segment *segs = tk_alloc_copy(tilde_segs_lit, sizeof tilde_segs_lit);
    tk_call call = { .callee = { .segments = segs, .len = 3 }, .args = folded, .nargs = nfolded };
    // `concat`'s own arg-widening (type_call, below) already requires every piece to be `str` —
    // a non-str `~` operand fails there with a clear message, no separate check needed here.
    return type_call(call, env, table);
}

static tk_texpr_result type_binary(tk_binary b, tk_env env, tk_type_table table) {
    // `~` is desugared WHOLESALE (chain-flattened + literal-folded) BEFORE the generic
    // operand-typing below — it never reaches a plain TK_TEXPR_BINARY node (codegen/VM see an
    // ordinary TCall or a str literal, nothing new to handle, mirroring how `params` needed no
    // engine changes).
    if (b.op == TK_TOKEN_TILDE) { return type_tilde_chain(b, env, table); }
    tk_texpr_result l = tk_typer_expr(*b.left,  env, table); if (!l.ok) return l;
    tk_texpr_result r = tk_typer_expr(*b.right, env, table); if (!r.ok) return r;
    tk_type lt = l.as.value.type, rt = r.as.value.type;
    if (tk_type_is_void(&lt) || tk_type_is_void(&rt)) return xerr("a `void` expression cannot be an operand (M.1)");
    // C6 — a fitting numeric LITERAL operand adopts the OTHER operand's type (e.g. `line + 1`,
    // `n % 10`): the non-literal operand's type wins; update the literal's typed node to match.
    if (!tk_type_eq(&lt, &rt)) {
        if (l.as.value.tag == TK_TEXPR_NUMBER && tk_literal_adopts(l.as.value, rt)) { l.as.value.type = rt; lt = rt; }
        else if (r.as.value.tag == TK_TEXPR_NUMBER && tk_literal_adopts(r.as.value, lt)) { r.as.value.type = lt; rt = lt; }
    }
    if (op_is_shift(b.op)) {
        if (!is_integer(lt) || !is_integer(rt)) return xerr("shift needs integer operands (no float bit-shifts — B.38)");
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith(b.op)) {
        // + - * / over the native numeric set (B.38): ints AND floats. Same-type only
        // (B.22 — no promotion, no mixed int/float; cross-family needs an explicit `to`).
        if (!is_numeric(lt)) return xerr("arithmetic needs a numeric operand (int or float)");
        if (!tk_type_eq(&lt, &rt))   // (C1.8) expected = left's type, actual = right's
            return xferr(tk_error_types(tk_error_make("operands must be the same type (no promotion, no mixed int/float — B.22; use `to`)"),
                                        tk_type_render(lt), tk_type_render(rt)));
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
    }
    if (op_is_arith_bitwise(b.op)) {   // the bitwise/% remainder (%, & | ^) — integer-only (or same-flags for & | ^)
        // (C8.3) flags bitwise: & | ^ on two values of the SAME flags type → result is that flags type.
        // % (remainder) is NOT allowed on flags (arithmetic).
        if ((b.op == TK_TOKEN_AMP || b.op == TK_TOKEN_PIPE || b.op == TK_TOKEN_CARET)
            && is_flags_named(lt, table)) {
            if (!tk_type_eq(&lt, &rt))
                return xferr(tk_error_types(tk_error_make("flags bitwise operands must be the same flags type"),
                                            tk_type_render(lt), tk_type_render(rt)));
            return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = lt, .as.binary = { b.op, box(l.as.value), box(r.as.value) } });
        }
        if (!is_integer(lt)) return xerr("bitwise/remainder needs an integer (not a float — B.38)");
        if (!tk_type_eq(&lt, &rt))   // (C1.8) expected = left's type, actual = right's
            return xferr(tk_error_types(tk_error_make("operands must be the same type (no promotion — B.22)"),
                                        tk_type_render(lt), tk_type_render(rt)));
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
    if (u.op == TK_TOKEN_TILDE) {   // bitwise complement: integer-only OR flags (C8.3) — no float bit-flip (B.38)
        if (!is_integer(ot) && !is_flags_named(ot, table)) return xerr("unary `~` needs an integer or a flags type (not a float — B.38)");
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
    tk_texpr first_val = f.as.value;
    tk_type prev = first_val.type;
    if (tk_type_is_void(&prev)) return xerr("a `void` expression cannot be an operand (M.1)");
    tk_tcmp_list terms = tk_tcmp_list_empty();
    for (size_t i = 0; i < c.nrest; i += 1) {
        tk_texpr_result cur = tk_typer_expr(*c.rest[i].operand, env, table); if (!cur.ok) return cur;
        if (tk_type_is_void(&cur.as.value.type)) return xerr("a `void` expression cannot be an operand (M.1)");
        // numeric-literal adoption (parity with binary ops): a literal operand adopts the OTHER's
        // numeric type so `c < 0x20` (byte vs int-literal) and `0x20 < c` compare. byte = u8, so a
        // fitting int literal adopts it via tk_literal_adopts (cast_kind treats byte AS u8).
        if (cur.as.value.tag == TK_TEXPR_NUMBER && tk_literal_adopts(cur.as.value, prev)) {
            cur.as.value.type = prev;
        } else if (i == 0 && first_val.tag == TK_TEXPR_NUMBER && tk_literal_adopts(first_val, cur.as.value.type)) {
            first_val.type = cur.as.value.type; prev = first_val.type;
        }
        if (!is_comparable(prev, cur.as.value.type)) return xerr("operands are not comparable");
        terms = tk_tcmp_list_push(terms, (tk_tcmp_term){ c.rest[i].op, box(cur.as.value) });
        prev = cur.as.value.type;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE, .type = tk_prim_t(TK_PRIM_BOOL),
                           .as.compare = { box(first_val), terms.ptr, terms.len } });
}

// heap a tk_type (the slice element pointer). Local twin of resolve.c's box.
static tk_type *box_type(tk_type t) { tk_type *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }

// segment-name == C literal (terse; tk_str_eq is defined later for resolved comparisons).
static bool seg_lit(tk_str s, const char *lit) {
    size_t n = 0; while (lit[n]) n += 1;
    return s.len == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

// teko::list::empty / teko::list::push — GENERIC builtins (no `.tks` module), special-cased
// like print/assert. Recognized by the LAST-2 path segments `list::empty` / `list::push`,
// rooted at `teko` (or bare `list::*`). The element type is INFERRED (B.8/Slice):
//   * empty()        -> []<sentinel> (TK_TYPE_SLICE, element == NULL) — unifies with any slice
//   * push(s, x)     -> []typeof(x); `s` must be a slice (sentinel or matching element)
// Returns true and sets *out when the path named one (typed accordingly).
static bool type_list_builtin(tk_call c, tk_env env, tk_type_table table, tk_texpr_result *out) {
    if (c.callee.len < 2) return false;
    tk_str last = c.callee.segments[c.callee.len - 1].name;
    tk_str prev = c.callee.segments[c.callee.len - 2].name;
    if (!seg_lit(prev, "list")) return false;
    bool rooted = (c.callee.len == 2) || seg_lit(c.callee.segments[0].name, "teko");
    if (!rooted) return false;

    if (seg_lit(last, "empty")) {
        if (c.nargs != 0) { *out = xerr("teko::list::empty expects no arguments"); return true; }
        // SENTINEL slice: element == NULL (unifies with any concrete slice via tk_type_eq).
        tk_type st = { .tag = TK_TYPE_SLICE, .as.slice.element = NULL };
        *out = xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = st, .as.call = { c.callee, NULL, 0 } });
        return true;
    }
    if (seg_lit(last, "push")) {
        if (c.nargs != 2) { *out = xerr("teko::list::push expects two arguments (slice, item)"); return true; }
        tk_texpr_result s = tk_typer_expr(c.args[0], env, table); if (!s.ok) { *out = s; return true; }
        tk_texpr_result x = tk_typer_expr(c.args[1], env, table); if (!x.ok) { *out = x; return true; }
        if (s.as.value.type.tag != TK_TYPE_SLICE) { *out = xerr("teko::list::push first argument must be a slice (`[]T`)"); return true; }
        if (tk_type_is_void(&x.as.value.type)) { *out = xerr("teko::list::push item cannot be a `void` expression (M.1)"); return true; }
        // A SENTINEL base (bare `empty()`) is fixed BY the item — the result element is the item's
        // type. A CONCRETE base keeps its element; the item must WIDEN into it (a case into a variant
        // element — so a `[]Type` list accepts heterogeneous cases: str, u64, …) OR adopt it (literal).
        tk_type elem;
        if (s.as.value.type.as.slice.element == NULL) {
            elem = x.as.value.type;
        } else {
            elem = *s.as.value.type.as.slice.element;
            if (!tk_widens_into(x.as.value.type, elem, table) && !tk_literal_adopts(x.as.value, elem)) {
                *out = xerr("teko::list::push item type does not match the slice element type"); return true;
            }
        }
        tk_type st = { .tag = TK_TYPE_SLICE, .as.slice.element = box_type(elem) };
        // (#57) CONCRETIZE a SENTINEL base (a bare `empty()`) to []elem so codegen can EMIT it.
        // The VM ignores a slice's element type, but codegen's emit_list_empty REQUIRES it — a
        // `push(empty(), x)` left the inner empty() at the element-less sentinel, so native codegen
        // failed ("empty() needs a known slice type"). A push onto []elem has a []elem base; the
        // pushed item fixes the element, and nested pushes fix each base in turn.
        if (s.as.value.type.as.slice.element == NULL) s.as.value.type = st;
        tk_texpr *args = tk_alloc(2 * sizeof *args); if (!args) abort();
        args[0] = s.as.value; args[1] = x.as.value;
        *out = xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = st, .as.call = { c.callee, args, 2 } });
        return true;
    }
    return false;
}

// (2026-07-01) call-site desugar for a `params`-variadic function `ft`. Two shapes:
//   passthrough — exactly nparams raw args AND the last one is already typed `[]T`
//     (T = the params element type) → pass through unchanged, no re-wrapping.
//   pack — otherwise, trailing args (0 or more) beyond the fixed prefix collapse into ONE
//     synthetic array-literal `tk_expr` (reusing array-literal codegen), so the rest of
//     type_call sees an ordinary nparams-arity call.
typedef struct { bool ok; tk_expr *args; size_t nargs; tk_error error; } tk_pack_result;
static tk_pack_result pack_variadic_args(tk_type ft, tk_expr *args, size_t nargs, tk_env env, tk_type_table table) {
    size_t nfixed = ft.as.func.nparams - 1;
    tk_type elem_ty = ft.as.func.params[nfixed].as.slice.element ? *ft.as.func.params[nfixed].as.slice.element : tk_void_t();
    if (nargs == ft.as.func.nparams) {
        tk_texpr_result last = tk_typer_expr(args[nfixed], env, table);
        if (!last.ok) return (tk_pack_result){ .ok = false, .error = last.as.error };
        tk_type wanted = (tk_type){ .tag = TK_TYPE_SLICE, .as.slice = { .element = &elem_ty } };
        if (tk_type_eq(&last.as.value.type, &wanted)) return (tk_pack_result){ .ok = true, .args = args, .nargs = nargs };
    }
    if (nargs < nfixed) return (tk_pack_result){ .ok = false, .error = tk_error_make("wrong number of arguments") };
    tk_array_elem *elems = NULL; size_t nelems = 0;
    for (size_t i = nfixed; i < nargs; i += 1) {
        tk_array_elems_push(&elems, &nelems, (tk_array_elem){ .is_spread = false, .expr = tk_box_expr(args[i]) });
    }
    tk_expr synth = { .tag = TK_EXPR_ARRAY, .as.array = { .elements = elems, .nelements = nelems } };
    tk_expr *out = tk_alloc((nfixed + 1) * sizeof *out); if (!out) abort();
    for (size_t j = 0; j < nfixed; j += 1) out[j] = args[j];
    out[nfixed] = synth;
    return (tk_pack_result){ .ok = true, .args = out, .nargs = nfixed + 1 };
}

// (2026-07-01 DEFARGS) call-site resolution for a NON-variadic function `ft`: named-arg-to-index
// lookup + default-fill for omitted trailing params. Produces a positional tk_expr array of
// EXACTLY ft.as.func.nparams entries, in declaration order. NOTE: intentionally NOT composed
// with params/variadic (pack_variadic_args, above) yet -- type_call only calls this for
// !ft.as.func.variadic. Builds the result via a single forward pass (a plain C array with normal
// index writes works fine in C, unlike the .tks mirror which has no index-assignment).
static tk_pack_result resolve_defargs(tk_type ft, tk_expr *args, tk_str *arg_names, size_t nargs) {
    bool any_named = false;
    for (size_t ai = 0; ai < nargs; ai += 1) { if (arg_names && arg_names[ai].len != 0) { any_named = true; break; } }
    if (!any_named && nargs == ft.as.func.nparams) return (tk_pack_result){ .ok = true, .args = args, .nargs = nargs };   // fast identity path
    if (any_named && ft.as.func.param_names == NULL) {
        return (tk_pack_result){ .ok = false, .error = tk_error_make("this function cannot be called with named arguments (only a plain, named fn declaration supports named-call -- not a closure value or builtin)") };
    }
    size_t npos = 0;
    while (npos < nargs && (arg_names == NULL || arg_names[npos].len == 0)) { npos += 1; }
    if (npos > ft.as.func.nparams) return (tk_pack_result){ .ok = false, .error = tk_error_make("wrong number of arguments") };
    for (size_t vk = npos; vk < nargs; vk += 1) {
        tk_str nm = arg_names[vk];
        bool matched = false; size_t fidx = 0;
        for (; fidx < ft.as.func.nparams; fidx += 1) { if (tk_str_eq(ft.as.func.param_names[fidx], nm)) { matched = true; break; } }
        if (!matched) return (tk_pack_result){ .ok = false, .error = tk_error_named("unknown named argument", nm) };
        if (fidx < npos) return (tk_pack_result){ .ok = false, .error = tk_error_named("named argument was already provided positionally", nm) };
    }
    tk_expr *resolved = tk_alloc(ft.as.func.nparams * sizeof *resolved); if (!resolved) abort();
    for (size_t idx = 0; idx < ft.as.func.nparams; idx += 1) {
        if (idx < npos) {
            resolved[idx] = args[idx];
        } else {
            bool found = false; tk_expr found_expr = no_expr();
            for (size_t k = npos; k < nargs; k += 1) {
                if (tk_str_eq(arg_names[k], ft.as.func.param_names[idx])) { found = true; found_expr = args[k]; break; }
            }
            if (found) {
                resolved[idx] = found_expr;
            } else if (idx < ft.as.func.n_required) {
                return (tk_pack_result){ .ok = false, .error = tk_error_named("missing required argument", ft.as.func.param_names[idx]) };
            } else {
                resolved[idx] = ft.as.func.defaults[idx - ft.as.func.n_required];
            }
        }
    }
    return (tk_pack_result){ .ok = true, .args = resolved, .nargs = ft.as.func.nparams };
}

// (OOP A1, 2026-07-01) `recv.method(args)` — an INSTANCE struct-method dot-call (type_method_call
// twin). Desugars into an ordinary `StructName::method(recv, ...args)` tk_call and delegates to
// type_call wholesale (reuses arity/defargs/generics/namespace-mangling for free). The receiver
// is typed ONCE here just to discover which struct/method is targeted; type_call re-types it (and
// every arg) as part of its normal argument pass. Flags-typed receivers keep the PRE-EXISTING
// "deferred" error for anything type_flags_method doesn't itself already handle (unchanged).
static tk_texpr_result type_method_call(tk_method_call mc, tk_env env, tk_type_table table) {
    if (!mc.receiver) return xerr("internal: a MethodCall must have a receiver");   // SAST guard — the parser always sets it; defensive against a null field
    tk_texpr_result recv_r = tk_typer_expr(*mc.receiver, env, table);
    if (!recv_r.ok) return recv_r;
    tk_type recv_t = recv_r.as.value.type;
    if (recv_t.tag != TK_TYPE_NAMED) return xerr("method typing is deferred (B.29 / M.4)");
    tk_str struct_name = recv_t.as.named.name;
    tk_decl_result td = tk_type_table_find(table, struct_name);
    if (!td.ok) return xerr("method typing is deferred (B.29 / M.4)");
    // (W10b.CLASS) a class's instance dot-call reuses this SAME desugar — only the methods
    // list's source differs; a class's methods are otherwise typed exactly like a struct's.
    // Increment 2: the EFFECTIVE (base-inherited + overridden) methods, so an inherited method
    // is callable through a derived instance too.
    tk_function *methods; size_t n_methods;
    if (td.as.value.body.tag == TK_BODY_STRUCT) {
        methods = td.as.value.body.as.struct_body.methods;
        n_methods = td.as.value.body.as.struct_body.n_methods;
    } else if (td.as.value.body.tag == TK_BODY_CLASS) {
        tk_methodsvec_result eff = tk_effective_class_methods(td.as.value.body.as.class_body, table);
        if (!eff.ok) return xferr(eff.as.error);
        methods = eff.as.value.ptr; n_methods = eff.as.value.len;
    } else {
        return xerr("method typing is deferred (B.29 / M.4)");
    }
    bool found = false; tk_function mfn = {0};
    for (size_t i = 0; i < n_methods; i += 1) {
        if (tk_str_eq(methods[i].name, mc.method)) { found = true; mfn = methods[i]; break; }
    }
    if (!found) return xferr(tk_error_named("no such method on struct", mc.method));
    bool is_instance = mfn.nparams > 0 && !mfn.params[0].has_type;
    if (!is_instance) return xferr(tk_error_named("static method — call it as StructName::method(…), not recv.method(…)", mc.method));
    // (W10b.CLASS residual — intern visibility) a private (default) method is reachable only
    // from its OWN declaring class's code, or — if `intern` — a subclass's too. Struct methods
    // stay all-public (W10b.0.A) — this check only fires for a class receiver.
    if (td.as.value.body.tag == TK_BODY_CLASS) {
        tk_member_owner_result owner = tk_find_method_owner(struct_name, table, mc.method);
        if (!owner.ok) return xferr(owner.as.error);
        if (!tk_member_accessible(owner.as.value, env.owner_type, table))
            return xferr(tk_error_named("method is private to its declaring class", mc.method));
    }
    tk_segment *segs = tk_alloc(2 * sizeof *segs); if (!segs) abort();
    segs[0] = (tk_segment){ .name = struct_name };
    segs[1] = (tk_segment){ .name = mc.method };
    tk_expr *cargs = tk_alloc((mc.nargs + 1) * sizeof *cargs); if (!cargs) abort();
    cargs[0] = *mc.receiver;
    for (size_t i = 0; i < mc.nargs; i += 1) cargs[i + 1] = mc.args[i];
    tk_str *cnames = tk_alloc((mc.nargs + 1) * sizeof *cnames); if (!cnames) abort();
    for (size_t i = 0; i <= mc.nargs; i += 1) cnames[i] = (tk_str){0};   // an instance dot-call is always all-positional
    tk_call synthetic = { .callee = { .segments = segs, .len = 2 }, .args = cargs, .nargs = mc.nargs + 1, .arg_names = cnames };
    return type_call(synthetic, env, table);
}

static tk_texpr_result type_call(tk_call c, tk_env env, tk_type_table table) {
    tk_texpr_result lb;
    if (type_list_builtin(c, env, table, &lb)) return lb;   // teko::list::empty / push (generic)
    tk_str name = c.callee.segments[c.callee.len - 1].name;
    // panic / exit — injected GLOBAL diverging builtins (legislator's ruling — NO `never` type).
    // panic(error | str) = the message; exit(<integer>) = the status code. Both terminate the
    // program; the call's STATIC type is void, but tk_texpr_diverges accepts it in any value
    // position (type_coalesce / tk_tblock_diverges). Recognized UNQUALIFIED (`exit`) OR via the
    // reserved root (`teko::exit`), and only when the namespace-aware lookup finds NOTHING — so a
    // same-namespace `fn panic` (the runtime's own) resolves to ITSELF (below), while every other
    // namespace gets the injected global (the runtime defines `panic`, so #41's env contains it).
    bool teko_rooted = c.callee.len == 2 && seg_lit(c.callee.segments[0].name, "teko");
    if ((c.callee.len == 1 || teko_rooted) && !tk_env_lookup_call(env, c.callee).ok) {
        bool is_panic = seg_lit(name, "panic");
        if (is_panic || seg_lit(name, "exit")) {
            if (c.nargs != 1) return xerr(is_panic ? "panic expects one argument (an `error` or a `str`)"
                                                    : "exit expects one argument (an integer status code)");
            tk_texpr_result a = tk_typer_expr(c.args[0], env, table); if (!a.ok) return a;
            if (is_panic) {
                if (a.as.value.type.tag != TK_TYPE_STR && a.as.value.type.tag != TK_TYPE_ERROR)
                    return xerr("panic's argument must be an `error` or a `str`");
            } else {
                if (!(a.as.value.type.tag == TK_TYPE_PRIM && tk_prim_is_int(a.as.value.type.as.prim)))
                    return xerr("exit's argument must be an integer status code");
            }
            tk_texpr_list args = tk_texpr_list_empty(); args = tk_texpr_list_push(args, a.as.value);
            return xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = (tk_type){ .tag = TK_TYPE_VOID },
                                   .as.call = { c.callee, args.ptr, args.len } });
        }
    }
    tk_type_result ftr = tk_env_lookup_call(env, c.callee);   // (#41) namespace-aware: a local or a
                                                              // same-namespace fn (unqualified) / the qualified ns
    bool in_scope = ftr.ok;   // (W10a) found in the env (a local/param or a user fn) — NOT a builtin fallback
    // (#49) the resolved target's namespace drives the mangled C name. Only a USER function (found
    // in the env) carries one; a builtin (fallback below) leaves it empty → codegen's call-map path.
    tk_str call_ns = ftr.ok ? tk_env_call_ns(env, c.callee) : (tk_str){0};
    if (!ftr.ok) ftr = tk_builtin_fn(name);          // injected, non-shadowable stdlib is the fallback
    if (!ftr.ok) return (tk_texpr_result){ .ok = false, .as.error = tk_error_named("unknown function", name) };
    tk_type ft = ftr.as.value;
    if (ft.tag != TK_TYPE_FUNC) return xerr("not a function");
    // (W10b.CLASS residual — intern visibility) a STATIC call (`ClassName::method(…)`) is
    // reachable under the SAME rule as an instance dot-call — check it here too, routed through
    // the resolved call namespace. `call_ns` names a real class only for a class method; any
    // other qualified/unqualified call (an ordinary namespaced function) is a silent no-op
    // (tk_find_class_body errors → skipped).
    if (call_ns.len > 0) {
        tk_str cls = tk_class_name_from_method_ns(call_ns);
        tk_classbody_result cbr = tk_find_class_body(cls, table);
        if (cbr.ok) {
            tk_member_owner_result owner = tk_find_method_owner(cls, table, name);
            if (!owner.ok) return xferr(owner.as.error);
            if (!tk_member_accessible(owner.as.value, env.owner_type, table))
                return xferr(tk_error_named("method is private to its declaring class", name));
        }
    }
    tk_expr *dargs = c.args; size_t ndargs = c.nargs;
    if (ft.as.func.variadic) {
        tk_pack_result pr = pack_variadic_args(ft, c.args, c.nargs, env, table);
        if (!pr.ok) return xferr(pr.error);
        dargs = pr.args; ndargs = pr.nargs;
    } else {
        tk_pack_result pr = resolve_defargs(ft, c.args, c.arg_names, c.nargs);
        if (!pr.ok) return xferr(pr.error);
        dargs = pr.args; ndargs = pr.nargs;
    }
    if (ndargs != ft.as.func.nparams) return xerr("wrong number of arguments");
    // Type every argument first (void rejected); strict-check / generic-infer below.
    tk_texpr_list args = tk_texpr_list_empty();
    for (size_t i = 0; i < ndargs; i += 1) {
        // (W10) a closure-literal ARG whose parameter is a concrete function type infers its
        // un-annotated params from that target (ruling 3); other args type normally.
        bool lam_target = dargs[i].tag == TK_EXPR_LAMBDA && ft.as.func.params[i].tag == TK_TYPE_FUNC;
        tk_texpr_result a = lam_target ? tk_type_value_expected(dargs[i], ft.as.func.params[i], env, table)
                                       : tk_typer_expr(dargs[i], env, table);
        if (!a.ok) return a;
        if (tk_type_is_void(&a.as.value.type)) return xerr("a `void` expression cannot be passed as an argument (M.1)");
        args = tk_texpr_list_push(args, a.as.value);
    }
    // (S4) GENERIC callee — the sig carries type-param Named's: INFER them from the arg types and
    // substitute into the return type for the call's result. The monomorph pass (post-typer) stamps
    // the concrete instance + rewrites the callee. A type-param that appears only in the return type
    // cannot be inferred from args (M.2 no-guessing) → honest error.
    tk_str *all_tps = NULL; size_t n_all = 0;
    tk_collect_sig_type_params(ft, table, &all_tps, &n_all);
    if (n_all > 0) {
        tk_str *param_tps = NULL; size_t n_pt = 0;
        for (size_t i = 0; i < ft.as.func.nparams; i += 1) tk_collect_sig_type_params(ft.as.func.params[i], table, &param_tps, &n_pt);
        for (size_t i = 0; i < n_all; i += 1)
            if (!tk_is_type_param(all_tps[i], param_tps, n_pt)) {
                tk_free0(all_tps); tk_free0(param_tps);
                return xferr(tk_error_named("cannot infer type parameter (it appears only in the return type; annotate the call)", all_tps[i]));
            }
        tk_subst s = { .params = param_tps, .n_params = n_pt, .names = NULL, .types = NULL, .n_bind = 0 };
        for (size_t i = 0; i < args.len; i += 1) {   // args.len == nparams (arity checked above); bound by the list so args.ptr[i] is in range
            tk_subst_result u = tk_unify(ft.as.func.params[i], args.ptr[i].type, s, table);
            if (!u.ok) { tk_free0(all_tps); tk_free0(param_tps); return xferr(u.as.error); }
            s = u.as.value;
        }
        tk_type result = tk_subst_type(*ft.as.func.ret, s);
        tk_texpr_result ret = xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = result, .as.call = { c.callee, args.ptr, args.len, call_ns } });
        tk_free0(all_tps); tk_free0(param_tps);
        return ret;
    }
    // NON-generic: each argument must WIDEN into the parameter's type (B.14 case→variant, T→T?) OR be
    // a fitting literal that adopts it (C6) — same rule as binding/return/assign (single source of truth).
    for (size_t i = 0; i < args.len; i += 1) {
        tk_type pt = ft.as.func.params[i];
        // (MEM-1b-ii) AUTO-REF: a `ref<T>` param accepts a `mut` lvalue of the pointed type T — the
        // compiler takes the reference (codegen emits `&`). Requires a `mut` variable. A value already
        // of type `ref<T>` takes the normal widen path below.
        if (pt.tag == TK_TYPE_REF && !tk_widens_into(args.ptr[i].type, pt, table)
            && tk_type_eq(&args.ptr[i].type, pt.as.ref.inner)) {
            if (args.ptr[i].tag != TK_TEXPR_VAR)
                return xerr("a `ref<T>` argument must be a mutable variable (a `mut` binding) — only an lvalue can be referenced");
            tk_binding_result b = tk_env_lookup_binding(env, args.ptr[i].as.var.name);
            if (!b.ok) return xerr("auto-ref requires a mutable variable (a `mut` binding)");
            if (!b.as.value.is_mut)
                return xerr("cannot take a reference to an immutable binding — declare it `mut` (a reference needs a mutable target)");
            continue;   // keep arg type = T; emit_call emits `&` (param is ref<T>)
        }
        if (!tk_widens_into(args.ptr[i].type, pt, table)) {
            if (!tk_literal_adopts(args.ptr[i], pt))
                return xferr(tk_error_types(tk_error_make("argument type mismatch"),
                                            tk_type_render(pt), tk_type_render(args.ptr[i].type)));
            args.ptr[i].type = pt;   // a fitting numeric literal ADOPTS the param type (leaf retyped) — C6
        } else if (tk_expand_variant(pt, table).tag != TK_TYPE_VARIANT
                   && !(pt.tag == TK_TYPE_OPTIONAL && args.ptr[i].type.tag != TK_TYPE_OPTIONAL)) {
            // A non-variant widen (empty()→[]T, exact, already-optional→T?) ADOPTS the param type; a
            // case→VARIANT widen KEEPS its case type so emit_call wraps it into the variant rep
            // (emit_as). A bare T→T? widen (param OPTIONAL, arg NOT optional) ALSO keeps its narrow
            // type, so codegen/VM see a non-optional value and present-wrap it (uniform with field/
            // return positions). An already-optional arg (U?→T?) stays adopt — never double-wrapped.
            args.ptr[i].type = pt;
        }
    }
    // (W10a) a CLOSURE call: the callee resolved IN-scope to a function type whose binding is a LOCAL
    // (call_ns empty — only a top-level fn carries one). It is a `tk_closure` VALUE, so codegen calls
    // through `((R(*)(A,B))f.fn)(args)` using the Func type (ft) for the cast.
    bool is_closure = in_scope && call_ns.len == 0;
    return xok((tk_texpr){ .tag = TK_TEXPR_CALL, .type = *ft.as.func.ret,
                           .as.call = { c.callee, args.ptr, args.len, call_ns, is_closure, ft } });
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
        default:           return (__int128)INT64_MIN * ((__int128)1 << 64);   // I128 min = -2^127
    }
}
static __int128 s_max(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8:   return (__int128)INT64_C(127);
        case TK_PRIM_I16:  return (__int128)INT64_C(32767);
        case TK_PRIM_I32:  return (__int128)INT64_C(2147483647);
        case TK_PRIM_I64:  return (__int128)INT64_MAX;
        default:           return ~((__int128)INT64_MIN * ((__int128)1 << 64));   // I128 max = 2^127-1
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
    // a NEGATIVE numeric literal `-N` (unary minus over a NUMBER) — adopts a SIGNED int annotation
    // that N fits, or a float annotation for `-3.14` (parse_lit's `mut d: i128 = -1`).
    if (value.tag == TK_EXPR_UNARY && value.as.unary.op == TK_TOKEN_MINUS
        && value.as.unary.operand != NULL && value.as.unary.operand->tag == TK_EXPR_NUMBER) {
        if (ann.tag != TK_TYPE_PRIM) return "value type does not match annotation";
        tk_number n = value.as.unary.operand->as.number;
        if (n.is_float) {
            if (!tk_prim_is_float(ann.as.prim)) return "a float literal cannot be annotated as an integer type (B.38)";
            return float_fits(n.fval, ann.as.prim) ? NULL : "float literal out of range for the annotated type (M.1 — fail early)";
        }
        if (tk_prim_is_float(ann.as.prim)) return "an integer literal cannot be annotated as a float type (write it with a `.` — B.38)";
        if (!tk_prim_is_signed(ann.as.prim)) return "a negative literal cannot be annotated as an unsigned type (M.1)";
        return value_fits(n.value, ann.as.prim) ? NULL : "literal out of range for the annotated type (M.1 — fail early)";
    }
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

bool tk_tblock_diverges(const tk_tstatement *stmts, size_t n);   // fwd (defined below; used by the match/if recursion)

// the trailing value-expr of a typed block, or NULL if it does not end in a value expression.
static const tk_texpr *tblock_trailing(const tk_tstatement *stmts, size_t n) {
    if (n == 0) return NULL;
    if (stmts[n - 1].tag != TK_TSTMT_EXPR) return NULL;
    return &stmts[n - 1].as.expr_stmt.expr;
}

// C6 (extended) — a fitting numeric LITERAL adopts the destination type at a value position
// (return value / trailing expr / binding / arg): an int literal → an int prim it fits, OR `byte`
// (byte = u8, B.36); a float literal → a float prim it fits. Reuses cast_kind (byte AS u8) +
// value_fits / float_fits — the same range rules as a binding annotation or a `… to T` target.
// RECURSES through `match`/`if`: a compound whose every NON-diverging arm/branch trailing value
// adopts `to` itself adopts `to` (`fn f() -> u32 { match k { … => 8; … } }` — each arm fits u32).
bool tk_literal_adopts(tk_texpr e, tk_type to) {
    if (e.tag == TK_TEXPR_NUMBER) {
        tk_prim_kind k;
        if (!cast_kind(to, &k)) return false;   // `to` must be numeric (a prim, or byte→u8)
        if (e.as.number.is_float) return tk_prim_is_float(k) && float_fits(e.as.number.fval, k);
        return tk_prim_is_int(k) && value_fits(e.as.number.value, k);
    }
    if (e.tag == TK_TEXPR_MATCH) {
        bool any = false;
        for (size_t i = 0; i < e.as.match_expr.narms; i += 1) {
            const tk_tarm *arm = &e.as.match_expr.arms[i];
            if (tk_tblock_diverges(arm->body, arm->nbody)) continue;   // no value
            const tk_texpr *t = tblock_trailing(arm->body, arm->nbody);
            if (t == NULL || !tk_literal_adopts(*t, to)) return false;
            any = true;
        }
        return any;   // ≥1 contributing arm, all adopt
    }
    if (e.tag == TK_TEXPR_IF) {
        if (!e.as.if_expr.has_else) return false;
        bool any = false;
        if (!tk_tblock_diverges(e.as.if_expr.then_blk, e.as.if_expr.nthen)) {
            const tk_texpr *t = tblock_trailing(e.as.if_expr.then_blk, e.as.if_expr.nthen);
            if (t == NULL || !tk_literal_adopts(*t, to)) return false;
            any = true;
        }
        if (!tk_tblock_diverges(e.as.if_expr.else_blk, e.as.if_expr.nelse)) {
            const tk_texpr *t = tblock_trailing(e.as.if_expr.else_blk, e.as.if_expr.nelse);
            if (t == NULL || !tk_literal_adopts(*t, to)) return false;
            any = true;
        }
        return any;
    }
    // an ARRAY LITERAL adopts a `[]E` slot when EVERY non-spread element adopts E (element-wise;
    // `[0x68,…]` adopts `[]byte`). Spread elements are already `[]T` — skip adoption for them.
    // An empty `[]` adopts any slice.
    if (e.tag == TK_TEXPR_ARRAY) {
        if (to.tag != TK_TYPE_SLICE || to.as.slice.element == NULL) return false;
        for (size_t i = 0; i < e.as.array.nelements; i += 1) {
            if (e.as.array.is_spread && e.as.array.is_spread[i]) continue;  // spread: already []T, skip
            if (!tk_literal_adopts(e.as.array.elements[i], *to.as.slice.element)) return false;
        }
        return true;
    }
    return false;
}

// (E7) is `t` a NAMED type whose declaration is an `enum`?
static bool is_enum_named(tk_type t, tk_type_table table) {
    if (t.tag != TK_TYPE_NAMED) return false;
    tk_decl_result d = tk_type_table_find(table, t.as.named.name);
    return d.ok && d.as.value.body.tag == TK_BODY_ENUM;
}
// (C8.3) is `t` a NAMED type whose declaration is a `flags`?
static bool is_flags_named(tk_type t, tk_type_table table) {
    if (t.tag != TK_TYPE_NAMED) return false;
    tk_decl_result d = tk_type_table_find(table, t.as.named.name);
    return d.ok && d.as.value.body.tag == TK_BODY_FLAGS;
}
// (E7) an INTEGER cast endpoint: an int prim, or `byte` (= u8). Floats/bool excluded.
static bool is_int_cast_end(tk_type t) {
    if (t.tag == TK_TYPE_BYTE) return true;
    return t.tag == TK_TYPE_PRIM && tk_prim_is_int(t.as.prim);
}

// (UTF-8 increment 1) `char to <int>` — the EXPLICIT decode of a codepoint to its scalar value.
// Allowed for u32 (the natural codepoint width), and trivially for u64/i64 (which hold any
// codepoint). The REVERSE (`<int> to char`, an encode) is a later increment.
static bool is_char_decode_target(tk_type t) {
    return t.tag == TK_TYPE_PRIM &&
           (t.as.prim == TK_PRIM_U32 || t.as.prim == TK_PRIM_U64 || t.as.prim == TK_PRIM_I64);
}

static tk_texpr_result type_cast(tk_cast c, tk_env env, tk_type_table table) {
    tk_texpr_result inner = tk_typer_expr(*c.expr, env, table); if (!inner.ok) return inner;
    if (tk_type_is_void(&inner.as.value.type)) return xerr("a `void` expression cannot be cast (M.1)");
    tk_type_result tgt = tk_resolve_type(c.target, table);     if (!tgt.ok) return xferr(tgt.as.error);
    // (UTF-8 increment 1) `char to u32`/u64/i64 — decode the codepoint to its scalar value. char is
    // NOT a numeric cast endpoint (cast_kind rejects it), so this pair is handled here; the result
    // type is the target. `<int> to char` (encode) is NOT allowed yet. (Mirrors typer.tks type_cast.)
    if (inner.as.value.type.tag == TK_TYPE_CHAR) {
        if (is_char_decode_target(tgt.as.value))
            return xok((tk_texpr){ .tag = TK_TEXPR_CAST, .type = tgt.as.value, .as.cast = { box(inner.as.value) } });
        return xerr("a `char` may only be cast to u32/u64/i64 (its codepoint value); other casts are not defined");
    }
    // (E7) enum ↔ integer ORDINAL cast: an enum value → its ordinal integer, and an integer → the
    // enum member (the `(tk_byte)k` / `(tk_token_kind)b` the .tkb serializer relies on). cast_reason
    // is prim-only, so handle this pair here (it has the type table to detect an enum); the result
    // type is the target. Any OTHER enum cast stays rejected by cast_reason below.
    tk_type ft = inner.as.value.type, tt = tgt.as.value;
    bool e7 = (is_enum_named(ft, table) && is_int_cast_end(tt))
           || (is_int_cast_end(ft) && is_enum_named(tt, table));
    if (!e7) {
        const char *why = cast_reason(ft, tt);
        if (why != NULL) return xerr(why);
    }
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
// tk_str_eq is declared in text.h (via type.h) and implemented in teko_rt.c.

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
    // error diagnostic adornments (E2): the built-in `error` value exposes its carrier fields as
    // readable fields — message/file/expected/actual -> str, line/col -> u32. error has no struct
    // body, so (like str/slice `.len`) this precedes the struct-field path.
    if (recv.as.value.type.tag == TK_TYPE_ERROR) {
        if (tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"message",  7 }) ||
            tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"file",     4 }) ||
            tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"expected", 8 }) ||
            tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"actual",   6 })) {
            return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = (tk_type){ .tag = TK_TYPE_STR },
                                   .as.field_access = { box(recv.as.value), fa.field } });
        }
        if (tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"line", 4 }) ||
            tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"col",  3 })) {
            return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = tk_prim_t(TK_PRIM_U32),
                                   .as.field_access = { box(recv.as.value), fa.field } });
        }
        return xerr("no such field on error (message/file/expected/actual: str; line/col: u32)");
    }
    // (MEM-1b-ii) `Ref<T>.value` — the deref: reading `r.value` yields the referenced T. A Reference
    // exposes ONLY `.value`; any other field is an error.
    if (recv.as.value.type.tag == TK_TYPE_REF) {
        if (tk_str_eq(fa.field, (tk_str){ (const tk_byte *)"value", 5 })) {
            return xok((tk_texpr){ .tag = TK_TEXPR_FIELD_ACCESS, .type = *recv.as.value.type.as.ref.inner,
                                   .as.field_access = { box(recv.as.value), fa.field } });
        }
        return xerr("a reference (`Ref<T>`) exposes only `.value` (the referenced value)");
    }
    if (recv.as.value.type.tag != TK_TYPE_NAMED) return xerr("field access requires a struct receiver");
    tk_decl_result decl = tk_type_table_find(table, recv.as.value.type.as.named.name);
    if (!decl.ok) return xerr("unknown type for field access");
    // (W10b.CLASS) a class's fields are read exactly like a struct's; increment 2 uses the
    // EFFECTIVE (base-inherited) field set.
    tk_struct_body fa_sb;
    bool fa_is_class = decl.as.value.body.tag == TK_BODY_CLASS;
    if (decl.as.value.body.tag == TK_BODY_STRUCT) {
        fa_sb = decl.as.value.body.as.struct_body;
    } else if (fa_is_class) {
        tk_fieldsvec_result eff = tk_effective_class_fields(decl.as.value.body.as.class_body, table);
        if (!eff.ok) return xferr(eff.as.error);
        fa_sb = (tk_struct_body){ .fields = eff.as.value.ptr, .n_fields = eff.as.value.len, .methods = NULL, .n_methods = 0 };
    } else {
        return xerr("type is not a struct (no fields)");
    }
    tk_type_result ft = field_type(fa_sb, fa.field, table);
    if (!ft.ok) return xerr("no such field");
    // (W10b.CLASS residual — intern visibility) a private (default) field is reachable only
    // from its OWN declaring class's code, or — if `intern` — a subclass's too.
    if (fa_is_class) {
        tk_member_owner_result owner = tk_find_field_owner(recv.as.value.type.as.named.name, table, fa.field);
        if (!owner.ok) return xferr(owner.as.error);
        if (!tk_member_accessible(owner.as.value, env.owner_type, table))
            return xferr(tk_error_named("field is private to its declaring class", fa.field));
    }
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
    if (rt.tag == TK_TYPE_SLICE) {
        if (rt.as.slice.element == NULL)   // sentinel/untyped empty slice — element unknown (M.1 fail-loud, not a null-deref)
            return xerr("cannot index an untyped empty slice; annotate the element type (e.g. `mut xs: []i32 = teko::list::empty()`)");
        return xok((tk_texpr){ .tag = TK_TEXPR_INDEX, .type = *rt.as.slice.element,
                               .as.index = { box(recv.as.value), box(idx.as.value) } });
    }
    return xerr("cannot index a value of this type");
}

// ---- nullability nodes (LEGISLATION §75 booleans; REBOOT_PLAN §202/§203 null/?./??) ----
// box a tk_type onto the heap (the optional's inner) — like resolve.c's box, local here.
static tk_type *tk_box_type_val(tk_type t) { tk_type *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }

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
    // (W10b.CLASS) a class's fields are read exactly like a struct's; increment 2 uses the
    // EFFECTIVE (base-inherited) field set.
    tk_struct_body sfa_sb;
    bool sfa_is_class = decl.as.value.body.tag == TK_BODY_CLASS;
    if (decl.as.value.body.tag == TK_BODY_STRUCT) {
        sfa_sb = decl.as.value.body.as.struct_body;
    } else if (sfa_is_class) {
        tk_fieldsvec_result eff = tk_effective_class_fields(decl.as.value.body.as.class_body, table);
        if (!eff.ok) return xferr(eff.as.error);
        sfa_sb = (tk_struct_body){ .fields = eff.as.value.ptr, .n_fields = eff.as.value.len, .methods = NULL, .n_methods = 0 };
    } else {
        return xerr("type is not a struct (no fields)");
    }
    tk_type_result ft = field_type(sfa_sb, sfa.field, table);
    if (!ft.ok) return xerr("no such field");
    // (W10b.CLASS residual — intern visibility) a private (default) field is reachable only
    // from its OWN declaring class's code, or — if `intern` — a subclass's too.
    if (sfa_is_class) {
        tk_member_owner_result owner = tk_find_field_owner(inner.as.named.name, table, sfa.field);
        if (!owner.ok) return xferr(owner.as.error);
        if (!tk_member_accessible(owner.as.value, env.owner_type, table))
            return xferr(tk_error_named("field is private to its declaring class", sfa.field));
    }
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
    // A DIVERGING fallback (panic/exit) never produces a value — accept it for any left type;
    // the result is the unwrapped `T` (the present-case value). (No `never` type — legislator.)
    if (tk_texpr_diverges(&r.as.value))
        return xok((tk_texpr){ .tag = TK_TEXPR_COALESCE, .type = unwrapped,
                               .as.coalesce = { box(l.as.value), box(r.as.value) } });
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
// is `mname` a generic INSTANCE of `base` (begins with base + the `__g__` infix)? Mirror of
// typer.tks::name_has_generic_base — confirms a generic literal matches its annotated instance.
static bool name_has_generic_base(tk_str mname, tk_str base) {
    if (mname.len < base.len + 5) return false;
    if (base.len && memcmp(mname.ptr, base.ptr, base.len) != 0) return false;
    return memcmp(mname.ptr + base.len, "__g__", 5) == 0;
}

// `expected` is the binding/return type the literal flows into (a VOID sentinel = none). It retargets
// a GENERIC constructor `Box { … }` to the concrete instance from the annotation (S4, annotation-driven).
tk_texpr_result tk_type_struct_lit(tk_struct_lit sl, tk_type expected, tk_env env, tk_type_table table) {
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
        tk_str  *names = tk_alloc(sizeof *names); if (!names) abort(); names[0] = msg_field;
        tk_texpr *vals = tk_alloc(sizeof *vals);  if (!vals)  abort(); vals[0]  = vt.as.value;
        return xok((tk_texpr){ .tag = TK_TEXPR_STRUCT_INIT, .type = (tk_type){ .tag = TK_TYPE_ERROR },
                               .as.struct_init = { names, vals, 1 } });
    }
    if (nt.as.value.tag != TK_TYPE_NAMED) return xerr("a struct literal requires a named struct type");
    tk_str name = nt.as.value.as.named.name;
    tk_decl_result decl = tk_type_table_find(table, name);
    if (!decl.ok) return xerr("unknown type in a struct literal");
    // (W9.4) explicit construction-site type-args `Foo<i64>{ … }` resolve to the concrete instance
    // `Foo__g__i64` directly — NO annotation required. This OVERRIDES `expected`: build the synthetic
    // generic type-expr and resolve it (the same path as an annotation), which validates arity and
    // rejects a non-generic-with-args. The instance decl is stamped by tk_instantiate_types (fed from
    // the construction site). If the annotation is ANOTHER instance of the SAME generic base, the two
    // must name the same instance; if it is a wider type (a variant the instance widens into), the
    // ordinary binding-level widening check below handles it (so `let w: Wrap = Gen<i64>{…}` is fine).
    if (sl.nargs > 0) {
        tk_type_expr gte = { .tag = TK_TEXPR_NAMED, .as.named = { .path = sl.type_path, .args = sl.type_args, .args_len = sl.nargs } };
        tk_type_result git = tk_resolve_type(gte, table);
        if (!git.ok) return xferr(git.as.error);
        if (git.as.value.tag != TK_TYPE_NAMED) return xerr("explicit type-arguments did not resolve to a struct instance");
        if (expected.tag == TK_TYPE_NAMED && name_has_generic_base(expected.as.named.name, name)
            && !tk_str_eq(expected.as.named.name, git.as.value.as.named.name))
            return xerr("the explicit type-arguments at construction disagree with the annotated type");
        expected = git.as.value;   // retarget below as if annotated
    }
    // (S4) constructing a GENERIC struct → retarget to the concrete instance named by `expected`
    // (the annotation OR the W9.4 construction-site type-args), so `Box { … }` under `: Box<i64>`
    // (or `Box<i64> { … }`) builds `Box__g__i64` (fields T→i64).
    if (decl.as.value.n_type_params > 0) {
        if (expected.tag != TK_TYPE_NAMED)
            return xerr("cannot infer the type arguments of a generic struct here — annotate it (e.g. `let x: Box<…> = …`)");
        tk_str mname = expected.as.named.name;
        if (!name_has_generic_base(mname, name)) return xerr("struct literal does not match the annotated type");
        decl = tk_type_table_find(table, mname);
        if (!decl.ok) return xerr("internal: generic instance was not stamped");
        name = mname;
    }
    // (W10b.CLASS) `Name { … }` also constructs a class instance — a class's fields are typed
    // identically to a struct's (increment 2: the EFFECTIVE, base-inherited fields too). An
    // `abstract` class cannot be instantiated directly. The "who may construct me" restriction
    // (a class's literal legal only inside its own static factory) is a LATER increment.
    tk_field *sb_fields; size_t sb_n_fields;
    if (decl.as.value.body.tag == TK_BODY_STRUCT) {
        sb_fields = decl.as.value.body.as.struct_body.fields; sb_n_fields = decl.as.value.body.as.struct_body.n_fields;
    } else if (decl.as.value.body.tag == TK_BODY_CLASS) {
        if (decl.as.value.body.as.class_body.kind == TK_CLASS_ABSTRACT)
            return xferr(tk_error_named("is abstract and cannot be instantiated directly", name));
        tk_fieldsvec_result eff = tk_effective_class_fields(decl.as.value.body.as.class_body, table);
        if (!eff.ok) return xferr(eff.as.error);
        sb_fields = eff.as.value.ptr; sb_n_fields = eff.as.value.len;
    } else {
        return xerr("struct-literal target is not a struct");
    }
    if (sl.nfields != sb_n_fields) return xerr("a struct literal must set exactly the declared fields (count mismatch)");

    tk_str  *names = tk_alloc((sb_n_fields ? sb_n_fields : 1) * sizeof *names); if (!names) abort();
    tk_texpr *vals = tk_alloc((sb_n_fields ? sb_n_fields : 1) * sizeof *vals); if (!vals) abort();
    for (size_t d = 0; d < sb_n_fields; d += 1) {
        tk_str fname = sb_fields[d].name;
        size_t found = sl.nfields, hits = 0;          // locate the provided value for this declared field
        for (size_t i = 0; i < sl.nfields; i += 1)
            if (tk_str_eq(sl.field_names[i], fname)) { found = i; hits += 1; }
        if (hits == 0) { tk_free0(names); tk_free0(vals); return xerr("a struct literal is missing a declared field"); }
        if (hits > 1)  { tk_free0(names); tk_free0(vals); return xerr("a struct literal sets a field more than once"); }
        tk_type_result ft = tk_resolve_type(sb_fields[d].type_ann, table);
        if (!ft.ok) { tk_free0(names); tk_free0(vals); return xferr(ft.as.error); }
        // thread the field's type as EXPECTED so a nested generic constructor (`value = Box { … }`)
        // targets its concrete instance (S4). Non-struct-lit values type exactly as before.
        tk_texpr_result vt = tk_type_value_expected(sl.field_vals[found], ft.as.value, env, table);
        if (!vt.ok) { tk_free0(names); tk_free0(vals); return vt; }
        tk_texpr val = vt.as.value;
        // The field value must WIDEN into the field's declared type (exact, a variant CASE into a
        // variant-typed field — keep the case type so codegen's emit_as wraps it — or `T` into a
        // `T?`); OR be a fitting numeric LITERAL that ADOPTS the field type (incl a `byte` field =
        // u8 — `lo = 0x80`), in which case the leaf is retyped. Same rules as binding/arg adoption.
        if (tk_widens_into(val.type, ft.as.value, table)) {
            /* widened — keep val.type (the case) for codegen wrapping */
            // (#4) a sentinel empty list ADOPTS the field's declared slice type (the field decl is
            // the known element source), so codegen emits the concrete element — no back-inference.
            if (val.type.tag == TK_TYPE_SLICE && val.type.as.slice.element == NULL
                && ft.as.value.tag == TK_TYPE_SLICE && ft.as.value.as.slice.element != NULL)
                val.type = ft.as.value;
        } else if (tk_literal_adopts(val, ft.as.value)) {
            val.type = ft.as.value;   // a fitting literal adopts the field's type (leaf retyped)
        } else {
            tk_free0(names); tk_free0(vals);
            // (C1.8) expected = the field's declared type, actual = the provided value's type
            return xferr(tk_error_types(tk_error_make("a struct-literal field value does not match the field's declared type"),
                                        tk_type_render(ft.as.value), tk_type_render(val.type)));
        }
        names[d] = fname; vals[d] = val;
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_STRUCT_INIT,
                           .type = (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = name },
                           .as.struct_init = { names, vals, sb_n_fields } });
}

// (W10) free-variable collection for a closure body — mirror of typer.tks lam_collect_*. Collects the
// names READ (`refs`) and BOUND internally (`bound` = inner lets + nested-lambda params).
typedef struct { tk_str *p; size_t n; } tk_sset;
static bool sset_has(const tk_sset *s, tk_str x) { for (size_t i = 0; i < s->n; i += 1) if (tk_str_eq(s->p[i], x)) return true; return false; }
static void sset_add(tk_sset *s, tk_str x) { if (sset_has(s, x)) return; s->p = tk_realloc0(s->p, (s->n + 1) * sizeof *s->p); if (!s->p) abort(); s->p[s->n++] = x; }
static void lam_collect_expr(const tk_expr *e, tk_sset *refs, tk_sset *bound);
static void lam_collect_block(const tk_statement *stmts, size_t n, tk_sset *refs, tk_sset *bound);
static void lam_collect_stmt(const tk_statement *s, tk_sset *refs, tk_sset *bound) {
    switch (s->tag) {
        case TK_STMT_BINDING:
            lam_collect_expr(&s->as.binding.value, refs, bound);
            if (s->as.binding.target.tag == TK_BIND_SIMPLE) sset_add(bound, s->as.binding.target.as.simple.name);
            else for (size_t i = 0; i < s->as.binding.target.as.destructure.nnames; i += 1) sset_add(bound, s->as.binding.target.as.destructure.names[i]);
            break;
        case TK_STMT_ASSIGN: lam_collect_expr(&s->as.assign.value, refs, bound); break;
        case TK_STMT_RETURN: if (s->as.ret.has_value) lam_collect_expr(&s->as.ret.value, refs, bound); break;
        case TK_STMT_LOOP:   lam_collect_block(s->as.loop_stmt.body, s->as.loop_stmt.nbody, refs, bound); break;
        case TK_STMT_EXPR:   lam_collect_expr(&s->as.expr_stmt.expr, refs, bound); break;
        case TK_STMT_DEFER:  lam_collect_block(s->as.defer_stmt.body, s->as.defer_stmt.nbody, refs, bound); break;
        default: break;   // BREAK / CONTINUE
    }
}
static void lam_collect_block(const tk_statement *stmts, size_t n, tk_sset *refs, tk_sset *bound) {
    for (size_t i = 0; i < n; i += 1) lam_collect_stmt(&stmts[i], refs, bound);
}
static void lam_collect_expr(const tk_expr *e, tk_sset *refs, tk_sset *bound) {
    switch (e->tag) {
        case TK_EXPR_VAR: sset_add(refs, e->as.var.name); break;
        case TK_EXPR_BINARY: lam_collect_expr(e->as.binary.left, refs, bound); lam_collect_expr(e->as.binary.right, refs, bound); break;
        case TK_EXPR_UNARY: lam_collect_expr(e->as.unary.operand, refs, bound); break;
        case TK_EXPR_COMPARE: lam_collect_expr(e->as.compare.first, refs, bound); for (size_t i = 0; i < e->as.compare.nrest; i += 1) lam_collect_expr(e->as.compare.rest[i].operand, refs, bound); break;
        case TK_EXPR_CALL: if (e->as.call.callee.len == 1) sset_add(refs, e->as.call.callee.segments[0].name); for (size_t i = 0; i < e->as.call.nargs; i += 1) lam_collect_expr(&e->as.call.args[i], refs, bound); break;
        case TK_EXPR_IF: lam_collect_expr(e->as.if_expr.cond, refs, bound); lam_collect_block(e->as.if_expr.then_blk, e->as.if_expr.nthen, refs, bound); if (e->as.if_expr.has_else) lam_collect_block(e->as.if_expr.else_blk, e->as.if_expr.nelse, refs, bound); break;
        case TK_EXPR_MATCH: lam_collect_expr(e->as.match_expr.subject, refs, bound); for (size_t i = 0; i < e->as.match_expr.narms; i += 1) lam_collect_block(e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody, refs, bound); break;
        case TK_EXPR_FIELD_ACCESS: lam_collect_expr(e->as.field_access.receiver, refs, bound); break;
        case TK_EXPR_SAFE_FIELD_ACCESS: lam_collect_expr(e->as.safe_field_access.receiver, refs, bound); break;
        case TK_EXPR_COALESCE: lam_collect_expr(e->as.coalesce.left, refs, bound); lam_collect_expr(e->as.coalesce.right, refs, bound); break;
        case TK_EXPR_METHOD_CALL: lam_collect_expr(e->as.method_call.receiver, refs, bound); for (size_t i = 0; i < e->as.method_call.nargs; i += 1) lam_collect_expr(&e->as.method_call.args[i], refs, bound); break;
        case TK_EXPR_STRUCT_LIT: for (size_t i = 0; i < e->as.struct_lit.nfields; i += 1) lam_collect_expr(&e->as.struct_lit.field_vals[i], refs, bound); break;
        case TK_EXPR_INDEX: lam_collect_expr(e->as.index.receiver, refs, bound); lam_collect_expr(e->as.index.index, refs, bound); break;
        case TK_EXPR_INTERP: for (size_t i = 0; i < e->as.interp.nholes; i += 1) lam_collect_expr(&e->as.interp.holes[i], refs, bound); break;
        case TK_EXPR_IN: lam_collect_expr(e->as.in_expr.lhs, refs, bound); for (size_t i = 0; i < e->as.in_expr.nelems; i += 1) lam_collect_expr(&e->as.in_expr.elems[i], refs, bound); break;
        case TK_EXPR_ARRAY: for (size_t i = 0; i < e->as.array.nelements; i += 1) lam_collect_expr(e->as.array.elements[i].expr, refs, bound); break;
        case TK_EXPR_LAMBDA: for (size_t i = 0; i < e->as.lambda.nparams; i += 1) sset_add(bound, e->as.lambda.params[i].name); lam_collect_block(e->as.lambda.body, e->as.lambda.nbody, refs, bound); break;
        default: break;   // leaves
    }
}

// (W10) type a closure literal — mirror of typer.tks::type_lambda. `expected` (a Func when known)
// drives un-annotated param inference (ruling 3) and the return type; captures = free vars resolving
// to enclosing LOCALS (ns ""), by-copy unless the captured var is a `Ref<T>` (by_ref).
static tk_texpr_result type_lambda(tk_lambda lam, tk_type expected, tk_env env, tk_type_table table) {
    tk_type *exp_params = NULL; size_t n_exp = 0;
    if (expected.tag == TK_TYPE_FUNC) { exp_params = expected.as.func.params; n_exp = expected.as.func.nparams; }
    tk_tlambda_param *tparams = NULL; size_t ntp = 0;
    tk_env cenv = env;
    for (size_t i = 0; i < lam.nparams; i += 1) {
        tk_type pt;
        if (lam.params[i].has_type) { tk_type_result r = tk_resolve_type(lam.params[i].type_ann, table); if (!r.ok) return xferr(r.as.error); pt = r.as.value; }
        else if (i < n_exp) pt = exp_params[i];
        else return xerr("cannot infer the type of a closure parameter — annotate it or give the closure a target type");
        tparams = tk_realloc0(tparams, (ntp + 1) * sizeof *tparams); if (!tparams) abort();
        tparams[ntp++] = (tk_tlambda_param){ lam.params[i].name, pt };
        cenv = tk_env_define(cenv, lam.params[i].name, pt, false);
    }
    tk_typed_block_result tb = tk_type_block(lam.body, lam.nbody, cenv, table); if (!tb.ok) return xferr(tb.as.error);
    tk_type ret = (expected.tag == TK_TYPE_FUNC) ? *expected.as.func.ret : tk_tblock_type(tb.as.value.stmts, tb.as.value.n);
    tk_sset refs = {0}, bound = {0};
    for (size_t i = 0; i < lam.nparams; i += 1) sset_add(&bound, lam.params[i].name);
    lam_collect_block(lam.body, lam.nbody, &refs, &bound);
    tk_tcapture *caps = NULL; size_t ncap = 0;
    for (size_t k = 0; k < refs.n; k += 1) {
        if (sset_has(&bound, refs.p[k])) continue;
        tk_binding_result b = tk_env_lookup_binding(env, refs.p[k]);
        if (b.ok && b.as.value.ns.len == 0) {
            caps = tk_realloc0(caps, (ncap + 1) * sizeof *caps); if (!caps) abort();
            caps[ncap++] = (tk_tcapture){ refs.p[k], b.as.value.type, b.as.value.type.tag == TK_TYPE_REF };
        }
    }
    tk_free0(refs.p); tk_free0(bound.p);
    tk_type *ftp = ntp ? tk_alloc(ntp * sizeof *ftp) : NULL;
    for (size_t i = 0; i < ntp; i += 1) ftp[i] = tparams[i].type;
    tk_type *rp = tk_alloc(sizeof *rp); if (!rp) abort(); *rp = ret;
    tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { ftp, ntp, rp } };
    tk_tlambda tl = { tparams, ntp, tb.as.value.stmts, tb.as.value.n, caps, ncap, ret, 0 };
    return xok((tk_texpr){ .tag = TK_TEXPR_LAMBDA, .type = ft, .as.lambda = tl });
}

// Type an expression flowing into a known EXPECTED type: a struct literal is given that type (so a
// generic constructor targets its concrete instance); anything else types normally. Mirror of
// typer.tks::type_value_expected — used at the binding level AND for struct-lit field values (nested).
tk_texpr_result tk_type_value_expected(tk_expr e, tk_type expected, tk_env env, tk_type_table table) {
    if (e.tag == TK_EXPR_STRUCT_LIT) return tk_type_struct_lit(e.as.struct_lit, expected, env, table);
    if (e.tag == TK_EXPR_LAMBDA)     return type_lambda(e.as.lambda, expected, env, table);   // (W10) target → param inference
    return tk_typer_expr(e, env, table);
}

// Returns true for float primitives (for interp hole dispatch).
static bool is_float_prim(tk_type t) {
    return t.tag == TK_TYPE_PRIM && (t.as.prim == TK_PRIM_F32 || t.as.prim == TK_PRIM_F64);
}
// Returns true for hole types that have a natural no-spec string form.
static bool is_interp_formattable(tk_type t) {
    if (t.tag == TK_TYPE_STR || t.tag == TK_TYPE_BYTE ||
        t.tag == TK_TYPE_CHAR || t.tag == TK_TYPE_ERROR) return true;
    if (is_bool(t)) return true;
    if (is_integer(t) || is_float_prim(t)) return true;
    return false;
}
// Returns true for hole types that support numeric format specs (F/D/X/E/N/G/B/P).
static bool is_spec_numeric(tk_type t) { return is_integer(t) || is_float_prim(t); }

// ---- string interpolation `$"…{expr}…"` (self-host parity) ----
// Type each hole; expanded in ROUND 0 to accept: str, int, float, bool, byte, char, error.
// Format specs ($"{x:F2}" / $"{x:[fmt]}") validate the spec against the hole type.
// Named types with a spec → honest-stop (to_string() requires OOP, not yet implemented).
// The result type is `str`.
static tk_texpr_result type_interp(tk_interp in, tk_env env, tk_type_table table) {
    tk_texpr        *holes  = NULL;
    tk_tinterp_spec *tspecs = NULL;
    if (in.nholes > 0) {
        holes  = tk_alloc(in.nholes * sizeof *holes);  if (!holes)  abort();
        tspecs = tk_alloc(in.nholes * sizeof *tspecs); if (!tspecs) abort();
    }
    for (size_t i = 0; i < in.nholes; i += 1) {
        tk_texpr_result h = tk_typer_expr(in.holes[i], env, table);
        if (!h.ok) { tk_free0(holes); tk_free0(tspecs); return h; }
        tk_type ht = h.as.value.type;
        tk_format_spec *fs = (in.specs != NULL) ? &in.specs[i] : NULL;
        tk_fspec_kind fkind = fs ? fs->kind : TK_FSPEC_NONE;
        // Determine what's allowed.
        if (!is_interp_formattable(ht)) {
            if (ht.tag == TK_TYPE_NAMED || ht.tag == TK_TYPE_VARIANT) {
                tk_free0(holes); tk_free0(tspecs);
                return xerr("interpolation: user-defined types require to_string() method (not yet implemented; use explicit conversion)");
            }
            tk_free0(holes); tk_free0(tspecs);
            return xerr("interpolation hole type has no string representation");
        }
        // Validate format spec against hole type.
        tk_tinterp_spec ts = { TK_FSPEC_NONE, { NULL, 0 }, NULL, 0 };
        if (fkind == TK_FSPEC_STATIC) {
            if (!is_spec_numeric(ht)) {
                tk_free0(holes); tk_free0(tspecs);
                return xerr("format spec (F/D/X/…) only applies to numeric types (int or float)");
            }
            ts.kind = TK_FSPEC_STATIC; ts.static_spec = fs->static_spec;
        } else if (fkind == TK_FSPEC_DYNAMIC) {
            if (!is_spec_numeric(ht)) {
                tk_free0(holes); tk_free0(tspecs);
                return xerr("dynamic format spec only applies to numeric types (to_string() on custom types requires OOP)");
            }
            tk_texpr *dargs = NULL;
            if (fs->ndyn_args > 0) { dargs = tk_alloc(fs->ndyn_args * sizeof *dargs); if (!dargs) abort(); }
            for (size_t k = 0; k < fs->ndyn_args; k++) {
                tk_texpr_result da = tk_typer_expr(fs->dyn_args[k], env, table);
                if (!da.ok) { tk_free0(dargs); tk_free0(holes); tk_free0(tspecs); return da; }
                if (da.as.value.type.tag != TK_TYPE_STR) {
                    tk_free0(dargs); tk_free0(holes); tk_free0(tspecs);
                    return xerr("dynamic format spec argument must be a str");
                }
                dargs[k] = da.as.value;
            }
            ts.kind = TK_FSPEC_DYNAMIC; ts.dyn_args = dargs; ts.ndyn_args = fs->ndyn_args;
        }
        holes[i]  = h.as.value;
        tspecs[i] = ts;
    }
    // carry the decoded pieces verbatim (the parser already resolved escapes).
    tk_str *pieces = NULL;
    if (in.npieces > 0) { pieces = tk_alloc(in.npieces * sizeof *pieces); if (!pieces) abort();
        for (size_t i = 0; i < in.npieces; i += 1) pieces[i] = in.pieces[i]; }
    return xok((tk_texpr){ .tag = TK_TEXPR_INTERP, .type = (tk_type){ .tag = TK_TYPE_STR },
                           .as.interp = { pieces, in.npieces, holes, in.nholes, tspecs } });
}

// <expr> in [ e0, e1, … ] (Phase 2) — membership test. Type the lhs ONCE; require each element
// be COMPARABLE to the lhs (the SAME is_comparable rule used by `==`/compare — B.22), since the
// runtime is `lhs == e0 || lhs == e1 || …`. The empty set `x in []` is allowed (result is still
// bool — always false). The node's `.type` is bool. C twin's mirror: type_in in typer.tks.
static tk_texpr_result type_in(tk_in n, tk_env env, tk_type_table table) {
    tk_texpr_result lhs = tk_typer_expr(*n.lhs, env, table); if (!lhs.ok) return lhs;
    tk_type lt = lhs.as.value.type;
    if (tk_type_is_void(&lt)) return xerr("a `void` expression cannot be an operand (M.1)");
    // (MEM Step 0, R4) ESCAPE GATE — `in` has its own element path (it does NOT build an ArrayLit),
    // so the literal-form gate doesn't reach it. A reference may not be an `in` operand (lhs or set
    // element): the [a, b] set is a value-position collection of refs. Mirror in typer.tks::type_in.
    if (tk_type_contains_ref(&lt)) return xerr("a reference cannot be stored in a struct/variant/collection");
    tk_texpr *elems = NULL;
    if (n.nelems > 0) {
        elems = tk_alloc(n.nelems * sizeof *elems); if (!elems) abort();
        // Loop nested inside the allocation guard (not a sibling `if`+`for` pair) so the
        // analyzer can see `elems` is non-NULL for every index this loop actually reaches.
        for (size_t i = 0; i < n.nelems; i += 1) {
            tk_texpr_result e = tk_typer_expr(n.elems[i], env, table);
            if (!e.ok) { tk_free0(elems); return e; }
            if (tk_type_is_void(&e.as.value.type)) {
                tk_free0(elems);
                return xerr("a `void` expression cannot be an operand (M.1)");
            }
            if (tk_type_contains_ref(&e.as.value.type)) {
                tk_free0(elems);
                return xerr("a reference cannot be stored in a struct/variant/collection");
            }
            if (!is_comparable(lt, e.as.value.type)) {
                tk_free0(elems);
                return xerr("`in` element is not comparable to the left-hand operand");
            }
            elems[i] = e.as.value;
        }
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_IN, .type = tk_prim_t(TK_PRIM_BOOL),
                           .as.in_expr = { box(lhs.as.value), elems, n.nelems } });
}

// `[ e0, e1, … ]` (Increment B+): type every element, UNIFY to a common element type T (widening),
// result `[]T`. An empty `[]` yields the SENTINEL `[]void` (like teko::list::empty()) so a binding
// annotation adopts it. Spread elements (`..xs`) must type as `[]T`; their element type T is widened
// into the array's unified element type. C twin's mirror: type_array_lit in typer.tks.
static tk_texpr_result type_array_lit(tk_array_lit a, tk_env env, tk_type_table table) {
    tk_texpr  *elems    = NULL;
    bool      *spreads  = NULL;
    tk_type et = (tk_type){ .tag = TK_TYPE_VOID };   // sentinel for an empty `[]`
    if (a.nelements > 0) {
        elems   = tk_alloc(a.nelements * sizeof *elems);   if (!elems)   abort();
        spreads = tk_alloc(a.nelements * sizeof *spreads); if (!spreads) abort();
        // Loop nested inside the SAME allocation guard (not a sibling `if`+`for` pair) so the
        // analyzer can see `elems`/`spreads` are non-NULL for every index this loop reaches.
        for (size_t i = 0; i < a.nelements; i += 1) {
            tk_texpr_result e = tk_typer_expr(*a.elements[i].expr, env, table);
            if (!e.ok) { tk_free0(elems); tk_free0(spreads); return e; }
            if (a.elements[i].is_spread) {
                // spread element: expr must be a `[]T` slice; contribute T as the element type
                if (e.as.value.type.tag != TK_TYPE_SLICE) {
                    tk_free0(elems); tk_free0(spreads);
                    return xerr("a spread element (`..xs`) must be a slice (`[]T`)");
                }
                if (e.as.value.type.as.slice.element == NULL) {
                    tk_free0(elems); tk_free0(spreads);
                    return xerr("a spread element cannot be an untyped empty slice");
                }
                tk_type spread_elem = *e.as.value.type.as.slice.element;
                if (tk_type_is_void(&et)) { et = spread_elem; }   // first contributor
                else {
                    tk_type j;
                    if (!tk_type_join(et, spread_elem, table, &j)) {
                        tk_free0(elems); tk_free0(spreads);
                        return xerr("spread element type does not match the array element type");
                    }
                    et = j;
                }
            } else {
                // plain element: original logic
                if (tk_type_is_void(&e.as.value.type)) { tk_free0(elems); tk_free0(spreads); return xerr("a `void` expression cannot be an array element (M.1)"); }
                if (tk_type_is_void(&et)) { et = e.as.value.type; }   // first contributor
                else { tk_type j; if (!tk_type_join(et, e.as.value.type, table, &j)) { tk_free0(elems); tk_free0(spreads); return xerr("array elements have different types"); } et = j; }
            }
            elems[i]   = e.as.value;
            spreads[i] = a.elements[i].is_spread;
        }
    }
    // (MEM Step 0, R4) ESCAPE GATE — an INFERRED element type may not carry a reference. The
    // annotated form `let xs: []Ref<T> = …` is already rejected in resolve, but the inferred
    // element here (e.g. `[r, r]`) only surfaces now. Also rejects `a in [a, b]` (its `[a, b]`).
    if (a.nelements != 0 && tk_type_contains_ref(&et)) {
        tk_free0(elems); tk_free0(spreads);
        return xerr("a reference cannot be stored in a struct/variant/collection");
    }
    // an EMPTY `[]` is the SENTINEL slice (element == NULL, like teko::list::empty()) — it unifies
    // with any concrete slice via a binding annotation. A non-empty array carries its joined element.
    tk_type st;
    if (a.nelements == 0) {
        st = (tk_type){ .tag = TK_TYPE_SLICE, .as.slice.element = NULL };
    } else {
        tk_type *ep = tk_alloc(sizeof *ep); if (!ep) abort(); *ep = et;
        st = (tk_type){ .tag = TK_TYPE_SLICE, .as.slice.element = ep };
    }
    return xok((tk_texpr){ .tag = TK_TEXPR_ARRAY, .type = st, .as.array = { elems, a.nelements, spreads } });
}

// forward decls for the if/match VALUE forms (mutual recursion: expr ↔ block).
static tk_texpr_result type_if(tk_if_expr f, tk_env env, tk_type_table table);
static tk_texpr_result type_match(tk_match_expr m, tk_env env, tk_type_table table);

// (C8.3) ---- flags helper method calls: recv.has/all/any/none/add/remove(mask) ----
// Lowered to synthetic TAST nodes at checker time:
//   add(mask)    -> recv | mask        (TK_TEXPR_BINARY, flags type)
//   remove(mask) -> recv & ~mask       (TK_TEXPR_BINARY with nested UNARY, flags type)
//   has(mask)    -> (recv & mask) == mask  (TK_TEXPR_COMPARE, bool)
//   all(mask)    -> synonym of has
//   any(mask)    -> (recv & mask) != 0_flags  ... lowered via TK_TEXPR_COMPARE
//   none(mask)   -> (recv & mask) == 0_flags
// For any/none the "0" value problem: we emit a synthetic call node `teko::flags::<method>`
// so codegen can lower it properly. add/remove are lowered now; bool-returning ones are
// emitted as synthetic calls with a `teko::flags` namespace for the codegen crumb to lower.
static tk_texpr_result type_flags_method(tk_method_call mc, tk_env env, tk_type_table table) {
    tk_texpr_result recv = tk_typer_expr(*mc.receiver, env, table);
    if (!recv.ok) return recv;
    tk_type rt = recv.as.value.type;
    if (!is_flags_named(rt, table))
        return xerr("method call on a non-flags receiver (flags methods: has/all/any/none/add/remove)");
    if (mc.nargs != 1)
        return xerr("flags methods take exactly one argument (the mask)");
    tk_texpr_result mask = tk_typer_expr(mc.args[0], env, table);
    if (!mask.ok) return mask;
    if (!tk_type_eq(&rt, &mask.as.value.type))
        return xferr(tk_error_types(tk_error_make("flags method argument must be the same flags type as the receiver"),
                                    tk_type_render(rt), tk_type_render(mask.as.value.type)));

    bool is_has  = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"has",    3 });
    bool is_all  = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"all",    3 });
    bool is_any  = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"any",    3 });
    bool is_none = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"none",   4 });
    bool is_add  = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"add",    3 });
    bool is_rem  = tk_str_eq(mc.method, (tk_str){ (const tk_byte *)"remove", 6 });

    if (!is_has && !is_all && !is_any && !is_none && !is_add && !is_rem)
        return xerr("unknown flags method (has/all/any/none/add/remove)");

    // add(mask) -> recv | mask
    if (is_add)
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = rt,
                               .as.binary = { TK_TOKEN_PIPE, box(recv.as.value), box(mask.as.value) } });

    // remove(mask) -> recv & ~mask
    if (is_rem) {
        tk_texpr notmask = { .tag = TK_TEXPR_UNARY, .type = rt,
                             .as.unary = { TK_TOKEN_TILDE, box(mask.as.value) } };
        return xok((tk_texpr){ .tag = TK_TEXPR_BINARY, .type = rt,
                               .as.binary = { TK_TOKEN_AMP, box(recv.as.value), box(notmask) } });
    }

    // has/all/any/none: lower to (recv & mask) as the inner binary, then emit a TK_TEXPR_COMPARE.
    // has/all: (recv & mask) == mask  → compare[first=(recv&mask), rest=[{==, mask_copy}]]
    // any:     (recv & mask) != mask  [codegen sees != and knows: any = not none]
    //          actually: any = (recv & mask) != 0_same_type — but we cannot express "zero of a
    //          flags type" as a literal here (flags are NAMED, not integers at this level).
    //          We lower any/none identically to has/none with a special synthetic comparison
    //          using a fabricated ZERO node (TK_TEXPR_NUMBER with value 0, type = flags — the
    //          codegen/VM will see the flags NAMED type and cast 0 accordingly).
    // none:    (recv & mask) == 0_same_type
    tk_texpr andexpr = { .tag = TK_TEXPR_BINARY, .type = rt,
                         .as.binary = { TK_TOKEN_AMP, box(recv.as.value), box(mask.as.value) } };

    if (is_has || is_all) {
        // (recv & mask) == mask  — compare with mask (a second typed copy of the mask arg)
        tk_texpr mask2 = mask.as.value;   // a value copy — safe (no heap ownership issue at checker level)
        tk_tcmp_term *rest = tk_alloc(sizeof *rest); if (!rest) abort();
        rest[0] = (tk_tcmp_term){ .op = TK_TOKEN_EQEQ, .operand = box(mask2) };
        return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE,
                               .type = tk_prim_t(TK_PRIM_BOOL),
                               .as.compare = { box(andexpr), rest, 1 } });
    }

    // any/none: compare (recv & mask) against a zero-valued flags literal.
    // Represent zero as a TK_TEXPR_NUMBER (value 0, type = flags NAMED). Codegen for the C
    // backend will emit the cast to the flags enum type; the VM lowering reads the ordinal.
    // This is the same pattern as enum ordinal casts (E7).
    tk_texpr zero = { .tag = TK_TEXPR_NUMBER, .type = rt, .as.number = { .is_float = false, .value = 0 } };
    tk_token_kind cmp_op = is_none ? TK_TOKEN_EQEQ : TK_TOKEN_NE;
    tk_tcmp_term *rest = tk_alloc(sizeof *rest); if (!rest) abort();
    rest[0] = (tk_tcmp_term){ .op = cmp_op, .operand = box(zero) };
    return xok((tk_texpr){ .tag = TK_TEXPR_COMPARE,
                           .type = tk_prim_t(TK_PRIM_BOOL),
                           .as.compare = { box(andexpr), rest, 1 } });
}

// ---- the expression dispatch (the evolved check_expr) ----
// ---- `Enum::Member` as a VALUE (value-level enum paths) ----
// The path is the enum type (all but the last segment) + the member (last segment). Resolve the
// enum decl, verify it is an `enum` and the member exists, and type the node as the NAMED enum —
// carrying the resolved enum name + member ordinal so both backends lower without re-lookup.
static tk_texpr_result type_path_expr(tk_path_expr pe, tk_type_table table) {
    tk_path p = pe.path;
    if (p.len < 2) return xerr("a path expression must name an enum or flags member (`Type::Member`)");
    tk_str member = p.segments[p.len - 1].name;
    tk_path type_path = { .segments = p.segments, .len = p.len - 1 };   // the type = path minus the member
    tk_type_result et = resolve_named(type_path, table);
    if (!et.ok) return xferr(et.as.error);
    if (et.as.value.tag != TK_TYPE_NAMED) return xerr("`Type::Member` requires a named enum or flags type");
    tk_decl_result decl = tk_type_table_find(table, et.as.value.as.named.name);
    if (!decl.ok) return xerr("unknown type in a `Type::Member` path");
    if (decl.as.value.body.tag == TK_BODY_ENUM) {
        tk_enum_body eb = decl.as.value.body.as.enum_body;
        for (size_t i = 0; i < eb.n_members; i += 1)
            if (tk_str_eq(eb.members[i], member))
                return xok((tk_texpr){ .tag = TK_TEXPR_PATH, .type = et.as.value,
                                       .as.path = { et.as.value.as.named.name, member, (uint64_t)i } });
        return xerr("no such member in the enum");
    }
    if (decl.as.value.body.tag == TK_BODY_FLAGS) {
        // (C8.3) flags member: ordinal = member index; the value is 1 << i (power-of-2).
        // Max 128 members enforced here (u128 cap); ordinal encodes the BIT INDEX (not the value).
        tk_flags_body fb = decl.as.value.body.as.flags_body;
        if (fb.n_members > 128)
            return xerr("flags type has more than 128 members (u128 overflow)");
        for (size_t i = 0; i < fb.n_members; i += 1)
            if (tk_str_eq(fb.members[i], member))
                return xok((tk_texpr){ .tag = TK_TEXPR_PATH, .type = et.as.value,
                                       .as.path = { et.as.value.as.named.name, member, (uint64_t)i } });
        return xerr("no such member in the flags type");
    }
    return xerr("`Type::Member` requires an `enum` or `flags` type");
}

// (C1-POS) inner dispatch — the raw switch. The public tk_typer_expr (below) wraps this to
// copy each node's source position onto the typed node AND to attach the expr's position to
// any error that does not already carry a finer (inner-expr) one.
static tk_texpr_result type_dispatch(tk_expr e, tk_env env, tk_type_table table) {
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
        case TK_EXPR_CHAR:   return xok((tk_texpr){ .tag = TK_TEXPR_CHAR, .type = (tk_type){ .tag = TK_TYPE_CHAR }, .as.char_lit = { e.as.char_lit.bytes } });
        case TK_EXPR_BOOL:   // `true`/`false` — the bool prim (LEGISLATION §75)
            return xok((tk_texpr){ .tag = TK_TEXPR_BOOL, .type = tk_prim_t(TK_PRIM_BOOL),
                                   .as.boolean = { e.as.boolean.value } });
        case TK_EXPR_NULL:
            // A bare `null` types to the SENTINEL optional (TK_TYPE_OPTIONAL with inner == NULL
            // — REBOOT_PLAN §202): it unifies with ANY concrete `T?` (tk_type_eq) and is
            // assignable to any optional slot (assignable_to). The CONCRETE inner is supplied by
            // the destination — a binding annotation `: T?`, a function return `-> T?`, the other
            // match/`if` arm, or `x ?? null` — exactly like a variant case widening into a
            // variant slot. The VM lowers it to NONE (untyped); codegen wraps it to the slot's
            // optional type via emit_as. The sentinel never reaches codegen's emit_type alone.
            return xok((tk_texpr){ .tag = TK_TEXPR_NULL,
                                   .type = (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional.inner = NULL },
                                   .as.null_lit = { 0 } });
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
        case TK_EXPR_METHOD_CALL:  {
            // (C8.3) flags helper methods are typed here; (OOP A1, 2026-07-01) struct instance
            // methods dispatch to type_method_call; anything else remains deferred.
            tk_method_call mc = e.as.method_call;
            if (mc.receiver) {
                tk_texpr_result recv_probe = tk_typer_expr(*mc.receiver, env, table);
                if (recv_probe.ok && is_flags_named(recv_probe.as.value.type, table))
                    return type_flags_method(mc, env, table);
            }
            return type_method_call(mc, env, table);
        }
        case TK_EXPR_PATH:         return type_path_expr(e.as.path, table);   // Enum::Member as a value
        case TK_EXPR_STRUCT_LIT:   return tk_type_struct_lit(e.as.struct_lit, (tk_type){ .tag = TK_TYPE_VOID }, env, table);   // W4a (no expected in expr position)
        case TK_EXPR_INDEX:        return type_index(e.as.index, env, table);            // W5-idx
        case TK_EXPR_INTERP:       return type_interp(e.as.interp, env, table);          // $"…{expr}…"
        case TK_EXPR_IN:           return type_in(e.as.in_expr, env, table);            // <expr> in [ … ] (Phase 2)
        case TK_EXPR_ARRAY:        return type_array_lit(e.as.array, env, table);       // [ e0, e1, … ] (Increment B+)
        case TK_EXPR_LAMBDA:       return type_lambda(e.as.lambda, (tk_type){ .tag = TK_TYPE_VOID }, env, table);   // (W10) no target → un-annotated params error (ruling 3)
    }
    return xerr("unknown expression");
}

// (C1-POS/E1) public entry — wrap the dispatch to thread source positions. On success, copy
// the untyped node's line/col onto the typed node so later passes (and the renderer) can point
// at it. On failure, stamp the error with this expr's position UNLESS it already carries one —
// the innermost (most specific) expr that set it wins, so `f(g(x))` blames the inner `g(x)`/`x`.
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table) {
    tk_texpr_result r = type_dispatch(e, env, table);
    if (r.ok) {
        r.as.value.line = e.line; r.as.value.col = e.col;
    } else if (r.as.error.line == 0) {
        r.as.error.line = e.line; r.as.error.col = e.col;
    }
    return r;
}

// ---- the value-type a typed block yields (shared with typer.c's if/match stmt forms) ----
tk_type tk_tblock_type(tk_tstatement *stmts, size_t n) {
    if (n == 0) return tk_void_t();
    if (stmts[n - 1].tag == TK_TSTMT_EXPR) return stmts[n - 1].as.expr_stmt.expr.type;
    return tk_void_t();
}

// ---- a DIVERGING expression: a call to the injected global builtins `panic` / `exit` ----
// (legislator's ruling — NO `never` type). Such a call terminates the whole program, so it
// produces no value and may stand in for ANY type at a value position (`x ?? panic(…)`, a
// return, a binding RHS). Recognized structurally: an unqualified `panic`/`exit` call.
bool tk_texpr_diverges(const tk_texpr *e) {
    if (e->tag == TK_TEXPR_CALL) {
        if (e->as.call.callee.len != 1) return false;   // the GLOBAL builtins are unqualified
        tk_str last = e->as.call.callee.segments[0].name;
        return seg_lit(last, "panic") || seg_lit(last, "exit");
    }
    // A `match` whose EVERY arm diverges (each body returns/panics) yields no value and never falls
    // through — so a function ending in it satisfies any declared return type (`eval_field_access`'s
    // match-of-all-returns). Likewise an `if`/`else` where both branches diverge.
    if (e->tag == TK_TEXPR_MATCH) {
        if (e->as.match_expr.narms == 0) return false;
        for (size_t i = 0; i < e->as.match_expr.narms; i += 1)
            if (!tk_tblock_diverges(e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody)) return false;
        return true;
    }
    if (e->tag == TK_TEXPR_IF) {
        if (!e->as.if_expr.has_else) return false;
        return tk_tblock_diverges(e->as.if_expr.then_blk, e->as.if_expr.nthen)
            && tk_tblock_diverges(e->as.if_expr.else_blk, e->as.if_expr.nelse);
    }
    return false;
}

// ---- does a typed block DIVERGE (exit via return/break/continue on every path)? ----
// A block diverges when its trailing statement is a return/break/continue, a trailing
// panic/exit call, OR a trailing `if` (with else) / `match` whose every branch / arm diverges.
// Such a block yields no trailing value, so match-as-value unification (and the if value form)
// skips it (B.20).
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
            if (tk_texpr_diverges(x)) return true;   // a trailing panic/exit call diverges
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
    // literal cross-adoption: a numeric-literal branch adopts the OTHER branch's numeric type, so
    // `if c { u8_expr } else { 64 }` is u8 (not the union u8 | i64). Mirrors the compare/arg rule.
    const tk_texpr *tl = tblock_trailing(tb.as.value.stmts, tb.as.value.n);
    const tk_texpr *el = tblock_trailing(eb.as.value.stmts, eb.as.value.n);
    if (tl && tl->tag == TK_TEXPR_NUMBER && tk_literal_adopts(*tl, et)) tt = et;
    else if (el && el->tag == TK_TEXPR_NUMBER && tk_literal_adopts(*el, tt)) et = tt;
    tk_type joined;
    if (!tk_type_join(tt, et, table, &joined)) return xerr("the `if` branches have different types");   // widen (a case joins into its variant)
    tt = joined;
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
        else if (!tk_type_join(first, t, table, &first)) return xerr("the `match` arms have different types");   // widen (a case joins into its variant)
    }
    if (!tk_exhaustive(m.arms, m.narms, s.as.value.type, table)) return xerr("non-exhaustive `match` (cover all cases or add `_`)");
    tk_type result = have_type ? first : tk_void_t();   // all arms diverge → void
    return xok((tk_texpr){ .tag = TK_TEXPR_MATCH, .type = result, .as.match_expr = { box(s.as.value), arms.ptr, arms.len } });
}
