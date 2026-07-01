// src/checker/revalidate.c
#include "revalidate.h"
#include "check.h"   // tk_check_result
#include "expr.h"    // is_bool/is_integer/is_float/is_numeric — single defn shared (no dup; #self-host)

bool tk_cast_ok(tk_type from, tk_type to);   // from typer.c — re-derive cast legality (C2)

static tk_check_result cok(void)         { return (tk_check_result){ .ok = true }; }
static tk_check_result cfail(const char *m) { return (tk_check_result){ .ok = false, .error = tk_error_make(m) }; }
static tk_type         prim(tk_prim_kind k) { return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k }; }
static bool op_is_shift(tk_token_kind op) { return op == TK_TOKEN_SHL || op == TK_TOKEN_SHR; }
static bool op_is_arith(tk_token_kind op) {   // + - * / — legal on floats AND ints (B.38)
    return op == TK_TOKEN_PLUS || op == TK_TOKEN_MINUS ||
           op == TK_TOKEN_STAR || op == TK_TOKEN_SLASH;
}
static bool op_is_arith_bitwise(tk_token_kind op) {   // %, & | ^ — integer-only
    return op == TK_TOKEN_PERCENT ||
           op == TK_TOKEN_AMP || op == TK_TOKEN_PIPE || op == TK_TOKEN_CARET;
}

static tk_check_result node_is(tk_type stored, tk_type expected) {
    return tk_type_eq(&stored, &expected) ? cok()
         : cfail("corrupt typed tree: a node's type does not match its derivation");
}

tk_check_result tk_validate_texpr(const tk_texpr *te) {
    switch (te->tag) {
        case TK_TEXPR_NUMBER:   // a literal leaf adopts its default (B.38): f64 if float, else i64
            return node_is(te->type, prim(te->as.number.is_float ? TK_PRIM_F64 : TK_PRIM_I64));
        case TK_TEXPR_STR:    return node_is(te->type, (tk_type){ .tag = TK_TYPE_STR });
        case TK_TEXPR_BYTE:   return node_is(te->type, (tk_type){ .tag = TK_TYPE_BYTE });
        case TK_TEXPR_CHAR:   return node_is(te->type, (tk_type){ .tag = TK_TYPE_CHAR });   // c'x' — `.type` is char
        case TK_TEXPR_BOOL:   return node_is(te->type, prim(TK_PRIM_BOOL));   // true/false (LEGISLATION §75)
        case TK_TEXPR_NULL:                                          // null — its type is the inferred `T?` (REBOOT §202)
            if (te->type.tag != TK_TYPE_OPTIONAL) return cfail("corrupt: `null` typed as a non-optional");
            return cok();
        case TK_TEXPR_VAR:    return cok();                          // env-dependent → trust
        case TK_TEXPR_PATH:   return cok();                          // Enum::Member — enum/ordinal resolved by the checker → trust
        case TK_TEXPR_CALL: {
            for (size_t i = 0; i < te->as.call.nargs; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.call.args[i]);
                if (!r.ok) return r;
            }
            return cok();
        }
        case TK_TEXPR_BINARY: {
            tk_check_result l = tk_validate_texpr(te->as.binary.left);  if (!l.ok) return l;
            tk_check_result r = tk_validate_texpr(te->as.binary.right); if (!r.ok) return r;
            tk_type lt = te->as.binary.left->type, rt = te->as.binary.right->type;
            tk_token_kind op = te->as.binary.op;
            if (op_is_shift(op)) {
                if (!is_integer(lt) || !is_integer(rt)) return cfail("corrupt: shift operands not integer");
                return node_is(te->type, lt);
            }
            if (op_is_arith(op)) {   // + - * / — numeric (int or float), same-type (B.22/B.38)
                if (!is_numeric(lt)) return cfail("corrupt: arithmetic operand not numeric");
                if (!tk_type_eq(&lt, &rt)) return cfail("corrupt: binary operands differ (no promotion)");
                return node_is(te->type, lt);
            }
            if (op_is_arith_bitwise(op)) {   // %, & | ^ — integer-only
                if (!is_integer(lt)) return cfail("corrupt: bitwise/remainder operand not integer");
                if (!tk_type_eq(&lt, &rt)) return cfail("corrupt: binary operands differ (no promotion)");
                return node_is(te->type, lt);
            }
            return cfail("corrupt: unknown binary operator");
        }
        case TK_TEXPR_UNARY: {
            tk_check_result o = tk_validate_texpr(te->as.unary.operand); if (!o.ok) return o;
            tk_type ot = te->as.unary.operand->type;
            if (te->as.unary.op == TK_TOKEN_BANG) {
                if (!is_bool(ot)) return cfail("corrupt: `!` operand not bool");
                return node_is(te->type, prim(TK_PRIM_BOOL));
            }
            if (te->as.unary.op == TK_TOKEN_MINUS) {   // negation: numeric (int or float — B.38)
                if (!is_numeric(ot)) return cfail("corrupt: `-` operand not numeric");
                return node_is(te->type, ot);
            }
            if (!is_integer(ot)) return cfail("corrupt: `~` operand not integer");   // bitwise NOT — int-only
            return node_is(te->type, ot);
        }
        case TK_TEXPR_COMPARE: {
            tk_check_result f = tk_validate_texpr(te->as.compare.first); if (!f.ok) return f;
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) {
                tk_check_result r = tk_validate_texpr(te->as.compare.rest[i].operand);
                if (!r.ok) return r;
            }
            return node_is(te->type, prim(TK_PRIM_BOOL));
        }
        case TK_TEXPR_IF:
        case TK_TEXPR_MATCH:
            return cok();   // [block-bearing: deep revalidation with program-level — E6-2]
        case TK_TEXPR_CAST: {
            tk_check_result e = tk_validate_texpr(te->as.cast.expr); if (!e.ok) return e;
            if (!tk_cast_ok(te->as.cast.expr->type, te->type))      // RE-PROVE the cast (M.3)
                return cfail("corrupt: illegal cast in typed tree");
            return cok();
        }
        case TK_TEXPR_FIELD_ACCESS: {
            tk_check_result r = tk_validate_texpr(te->as.field_access.receiver); if (!r.ok) return r;
            if (te->as.field_access.receiver->type.tag != TK_TYPE_NAMED)   // struct-receiver invariant
                return cfail("corrupt: field access on a non-struct receiver");
            return cok();
        }
        case TK_TEXPR_SAFE_FIELD_ACCESS: {   // recv?.field (REBOOT §203): optional receiver, optional result
            tk_check_result r = tk_validate_texpr(te->as.safe_field_access.receiver); if (!r.ok) return r;
            if (te->as.safe_field_access.receiver->type.tag != TK_TYPE_OPTIONAL)
                return cfail("corrupt: safe field access on a non-optional receiver");
            if (te->type.tag != TK_TYPE_OPTIONAL)
                return cfail("corrupt: safe field access result is not optional");
            return cok();   // the field-type-table layer is env-dependent → trust (like TFieldAccess)
        }
        case TK_TEXPR_COALESCE: {   // a ?? b (REBOOT §203): optional left; result is the unwrapped/optional type
            tk_check_result l = tk_validate_texpr(te->as.coalesce.left);  if (!l.ok) return l;
            tk_check_result r = tk_validate_texpr(te->as.coalesce.right); if (!r.ok) return r;
            if (te->as.coalesce.left->type.tag != TK_TYPE_OPTIONAL)
                return cfail("corrupt: `??` left operand is not optional");
            return cok();   // result is `T` (unwrapped) or `T?`; both are re-derived in the typer
        }
        case TK_TEXPR_STRUCT_INIT: {   // Name { f = v, … } (W4a): named-struct (or builtin `error`) type + each field value valid
            if (te->type.tag != TK_TYPE_NAMED && te->type.tag != TK_TYPE_ERROR)
                return cfail("corrupt: struct literal not typed as a named/error type");
            for (size_t i = 0; i < te->as.struct_init.nfields; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.struct_init.field_vals[i]);
                if (!r.ok) return r;
            }
            return cok();   // field-type matching is env/table-dependent → re-derived in the typer
        }
        case TK_TEXPR_INDEX: {   // recv[index] (W5-idx): str/slice receiver, integer index
            tk_check_result r = tk_validate_texpr(te->as.index.receiver); if (!r.ok) return r;
            tk_check_result i = tk_validate_texpr(te->as.index.index);    if (!i.ok) return i;
            tk_type rt = te->as.index.receiver->type;
            if (rt.tag != TK_TYPE_STR && rt.tag != TK_TYPE_SLICE)
                return cfail("corrupt: subscript on a non-indexable receiver");
            if (!is_integer(te->as.index.index->type))
                return cfail("corrupt: subscript index is not an integer");
            return cok();   // the element type is re-derived in the typer (byte / slice element)
        }
        case TK_TEXPR_INTERP: {   // $"…{expr}…": str result; each hole valid
            if (te->type.tag != TK_TYPE_STR)
                return cfail("corrupt: interpolation not typed as a str");
            for (size_t i = 0; i < te->as.interp.nholes; i += 1) {
                tk_check_result h = tk_validate_texpr(&te->as.interp.holes[i]); if (!h.ok) return h;
                // walk dyn_args in format specs
                if (te->as.interp.specs && te->as.interp.specs[i].kind == TK_FSPEC_DYNAMIC) {
                    for (size_t k = 0; k < te->as.interp.specs[i].ndyn_args; k++) {
                        tk_check_result d = tk_validate_texpr(&te->as.interp.specs[i].dyn_args[k]);
                        if (!d.ok) return d;
                    }
                }
            }
            return cok();
        }
        case TK_TEXPR_IN: {   // <lhs> in [ … ] (Phase 2): bool result; lhs + each element valid
            tk_check_result l = tk_validate_texpr(te->as.in_expr.lhs); if (!l.ok) return l;
            for (size_t i = 0; i < te->as.in_expr.nelems; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.in_expr.elems[i]); if (!r.ok) return r;
            }
            return node_is(te->type, prim(TK_PRIM_BOOL));
        }
        case TK_TEXPR_ARRAY: {   // [ e0, e1, … ] (Increment B+): []T result; each element valid
            for (size_t i = 0; i < te->as.array.nelements; i += 1) {
                tk_check_result r = tk_validate_texpr(&te->as.array.elements[i]); if (!r.ok) return r;
            }
            return cok();
        }
        case TK_TEXPR_LAMBDA: return cok();   // (W10) the closure body was checked at type_lambda; trust it here
    }
    return cfail("corrupt: unknown typed expression");
}
