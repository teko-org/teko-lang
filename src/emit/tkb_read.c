// src/emit/tkb_read.c
#include "tkb_read.h"
#include "tkb_buf.h"   // tk_fnv1a (hash check on the .tkb body)
#include <stdlib.h>

uint8_t tk_read_u8(tk_reader *r) {
    if (r->pos >= r->len) { r->ok = false; return 0; }
    return r->data[r->pos++];
}
uint32_t tk_read_u32(tk_reader *r) {
    uint32_t a = tk_read_u8(r), b = tk_read_u8(r), c = tk_read_u8(r), d = tk_read_u8(r);
    return a | (b << 8) | (c << 16) | (d << 24);
}
uint64_t tk_read_u64(tk_reader *r) {
    uint64_t lo = tk_read_u32(r), hi = tk_read_u32(r);
    return lo | (hi << 32);
}
tk_str tk_read_str(tk_reader *r, tk_strs t) {
    uint32_t i = tk_read_u32(r);
    if (i >= t.len) { r->ok = false; return (tk_str){ NULL, 0 }; }
    return t.ptr[i];
}
tk_strs tk_read_strtable(tk_reader *r) {
    uint32_t n = tk_read_u32(r);
    tk_str *xs = tk_alloc(n * sizeof *xs); if (n && !xs) abort();
    for (uint32_t i = 0; i < n; i += 1) {
        uint32_t len = tk_read_u32(r);
        tk_byte *bytes = tk_alloc(len ? len : 1); if (!bytes) abort();
        for (uint32_t j = 0; j < len; j += 1) bytes[j] = tk_read_u8(r);
        xs[i] = (tk_str){ bytes, len };
    }
    return (tk_strs){ xs, n };
}
// inverse of prim_byte (= the prim's ENUM ORDINAL, written by tkb_write.c's prim_byte).
// Tier-1 (plan §5 [N1/N2]): the ordinals match type.h's tk_prim_kind exactly —
// U8..U128 (0..4), I8..I128 (5..9), F16/F32/F64 (10..12), BOOL (13). New tags were
// APPENDED within their family so older .tkb prim bytes stay stable.
static tk_prim_kind prim_of(uint8_t b) {
    switch (b) {
        case 0:  return TK_PRIM_U8;   case 1:  return TK_PRIM_U16;  case 2:  return TK_PRIM_U32;
        case 3:  return TK_PRIM_U64;  case 4:  return TK_PRIM_U128;
        case 5:  return TK_PRIM_I8;   case 6:  return TK_PRIM_I16;  case 7:  return TK_PRIM_I32;
        case 8:  return TK_PRIM_I64;  case 9:  return TK_PRIM_I128;
        case 10: return TK_PRIM_F16;  case 11: return TK_PRIM_F32;  case 12: return TK_PRIM_F64;
        default: return TK_PRIM_BOOL;
    }
}
static tk_type *box(tk_type t) { tk_type *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }

tk_type tk_read_type(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    switch (tag) {
        case 0: return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = prim_of(tk_read_u8(r)) };
        case 1: return (tk_type){ .tag = TK_TYPE_BYTE };
        case 2: return (tk_type){ .tag = TK_TYPE_STR };
        case 3: return (tk_type){ .tag = TK_TYPE_ERROR };
        case 4: return (tk_type){ .tag = TK_TYPE_VOID };   // tag 4 = void (was Unit — B.37)
        case 5: return (tk_type){ .tag = TK_TYPE_SLICE, .as.slice.element = box(tk_read_type(r, t)) };
        case 6: return (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = tk_read_str(r, t) };
        case 7: {
            uint32_t n = tk_read_u32(r); tk_type *m = tk_alloc(n * sizeof *m); if (n && !m) abort();
            for (uint32_t i = 0; i < n; i += 1) m[i] = tk_read_type(r, t);
            return (tk_type){ .tag = TK_TYPE_VARIANT, .as.variant = { m, n } };
        }
        case 8: {
            uint32_t n = tk_read_u32(r); tk_type *p = tk_alloc(n * sizeof *p); if (n && !p) abort();
            for (uint32_t i = 0; i < n; i += 1) p[i] = tk_read_type(r, t);
            tk_type ret = tk_read_type(r, t);
            return (tk_type){ .tag = TK_TYPE_FUNC, .as.func = { p, n, box(ret) } };
        }
        case 9: return (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional.inner = box(tk_read_type(r, t)) };  // tag 9 = T? (B.37)
        case 10: return (tk_type){ .tag = TK_TYPE_PTR, .as.ptr.inner = NULL };   // tag 10 = opaque ptr (S-mem)
        case 11: return (tk_type){ .tag = TK_TYPE_UPTR };   // (C7.1a) tag 11 = uptr
        case 12: return (tk_type){ .tag = TK_TYPE_PTR, .as.ptr.inner = box(tk_read_type(r, t)) };   // tag 12 = ptr<T> (S-mem)
        case 13: return (tk_type){ .tag = TK_TYPE_REF, .as.ref.inner = box(tk_read_type(r, t)) };   // (MEM-1b) tag 13 = ref<T>
        case 14: return (tk_type){ .tag = TK_TYPE_CHAR };   // (UTF-8 increment 1) tag 14 = char
    }
    r->ok = false; return (tk_type){ .tag = TK_TYPE_VOID };
}

// tk_texpr_result now provided by checker/tast.h (the canonical home — C1).
static tk_texpr_result texpr_ok(tk_texpr t)  { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
static tk_texpr_result texpr_err(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }

static tk_token_kind kind_of(uint8_t b) { return (tk_token_kind)b; }   // [E7: byte→enum]
static tk_texpr *boxe(tk_texpr t) { tk_texpr *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }
static tk_tstatement *read_tstmts(tk_reader *r, tk_strs t, size_t *out_n);   // (C7.16) fwd (mutual with tk_read_texpr)
static tk_tarm *read_tarms(tk_reader *r, tk_strs t, size_t *out_n);          // (C7.16) fwd (mutual with tk_read_texpr — match arms)

tk_texpr tk_read_texpr(tk_reader *r, tk_strs t) {
    tk_type ty = tk_read_type(r, t);
    uint8_t tag = tk_read_u8(r);
    tk_texpr e = { .type = ty };
    switch (tag) {
        case 0: {
            // N1/N2 inverse of tk_write_texpr's NUMBER: is_float (u8), the 128-bit value
            // as two u64 (hi then lo), then the double's IEEE-754 bits as a u64.
            e.tag = TK_TEXPR_NUMBER;
            e.as.number.is_float = (tk_read_u8(r) != 0);
            uint64_t hi = tk_read_u64(r), lo = tk_read_u64(r);
            e.as.number.value = (__int128)(((unsigned __int128)hi << 64) | (unsigned __int128)lo);
            uint64_t fbits = tk_read_u64(r);
            __builtin_memcpy(&e.as.number.fval, &fbits, sizeof fbits);
            return e;
        }
        case 1: e.tag = TK_TEXPR_VAR;    e.as.var.name  = tk_read_str(r, t); e.as.var.is_func = tk_read_u8(r) != 0; e.as.var.func_ns = tk_read_str(r, t); return e;   // (W10a) name, is_func, func_ns
        case 2: e.tag = TK_TEXPR_STR;    e.as.str.text  = tk_read_str(r, t); return e;
        case 3: e.tag = TK_TEXPR_BYTE;   e.as.byte.value = tk_read_u8(r); return e;
        case 4: e.tag = TK_TEXPR_BINARY; e.as.binary.op = kind_of(tk_read_u8(r));
                e.as.binary.left = boxe(tk_read_texpr(r, t)); e.as.binary.right = boxe(tk_read_texpr(r, t)); return e;
        case 5: e.tag = TK_TEXPR_UNARY;  e.as.unary.op = kind_of(tk_read_u8(r));
                e.as.unary.operand = boxe(tk_read_texpr(r, t)); return e;
        case 6: {
            e.tag = TK_TEXPR_COMPARE; e.as.compare.first = boxe(tk_read_texpr(r, t));
            uint32_t n = tk_read_u32(r); tk_tcmp_term *ts = tk_alloc(n * sizeof *ts); if (n && !ts) abort();
            for (uint32_t i = 0; i < n; i += 1) { ts[i].op = kind_of(tk_read_u8(r)); ts[i].operand = boxe(tk_read_texpr(r, t)); }
            e.as.compare.rest = ts; e.as.compare.nrest = n; return e;
        }
        case 7: {
            e.tag = TK_TEXPR_CALL;
            uint32_t np = tk_read_u32(r); tk_segment *segs = tk_alloc(np * sizeof *segs); if (np && !segs) abort();
            for (uint32_t i = 0; i < np; i += 1) segs[i].name = tk_read_str(r, t);
            e.as.call.callee = (tk_path){ segs, np };
            uint32_t na = tk_read_u32(r); tk_texpr *as = tk_alloc(na * sizeof *as); if (na && !as) abort();
            for (uint32_t i = 0; i < na; i += 1) as[i] = tk_read_texpr(r, t);
            e.as.call.args = as; e.as.call.nargs = na; return e;
        }
        case 8:                                                                 /* (C7.16) IF: cond + then_blk + has_else + else_blk */
            e.tag = TK_TEXPR_IF;
            e.as.if_expr.cond     = boxe(tk_read_texpr(r, t));
            e.as.if_expr.then_blk = read_tstmts(r, t, &e.as.if_expr.nthen);
            e.as.if_expr.has_else = (tk_read_u8(r) != 0);
            e.as.if_expr.else_blk = read_tstmts(r, t, &e.as.if_expr.nelse);
            return e;
        case 9:                                                                 /* (C7.16) MATCH: subject + arms */
            e.tag = TK_TEXPR_MATCH;
            e.as.match_expr.subject = boxe(tk_read_texpr(r, t));
            e.as.match_expr.arms = read_tarms(r, t, &e.as.match_expr.narms);
            return e;
        case 10:                                                                /* S1a — Cast: target rides ty; read inner */
            e.tag = TK_TEXPR_CAST;
            e.as.cast.expr = boxe(tk_read_texpr(r, t));
            return e;
        case 11: {                                                              /* S1b — FieldAccess: receiver THEN field */
            e.tag = TK_TEXPR_FIELD_ACCESS;
            e.as.field_access.receiver = boxe(tk_read_texpr(r, t));
            e.as.field_access.field    = tk_read_str(r, t);
            return e;
        }
        case 12: e.tag = TK_TEXPR_BOOL; e.as.boolean.value = (tk_read_u8(r) != 0); return e;   // W2 — bool literal
        case 13: e.tag = TK_TEXPR_NULL; return e;                                               // W2 — null literal
        case 14:                                                                /* W2 — recv?.field */
            e.tag = TK_TEXPR_SAFE_FIELD_ACCESS;
            e.as.safe_field_access.receiver = boxe(tk_read_texpr(r, t));
            e.as.safe_field_access.field    = tk_read_str(r, t);
            return e;
        case 15:                                                                /* W2 — left ?? right */
            e.tag = TK_TEXPR_COALESCE;
            e.as.coalesce.left  = boxe(tk_read_texpr(r, t));
            e.as.coalesce.right = boxe(tk_read_texpr(r, t));
            return e;
        case 16: {                                                              /* W4a — Name { f = v, … } */
            e.tag = TK_TEXPR_STRUCT_INIT;
            uint32_t nf = tk_read_u32(r);
            tk_str   *names = tk_alloc((nf ? nf : 1) * sizeof *names); if (!names) abort();
            tk_texpr *vals  = tk_alloc((nf ? nf : 1) * sizeof *vals);  if (!vals)  abort();
            for (uint32_t i = 0; i < nf; i += 1) { names[i] = tk_read_str(r, t); vals[i] = tk_read_texpr(r, t); }
            e.as.struct_init.field_names = names; e.as.struct_init.field_vals = vals; e.as.struct_init.nfields = nf;
            return e;
        }
        case 17:                                                                /* W5-idx — recv[index]: receiver THEN index */
            e.tag = TK_TEXPR_INDEX;
            e.as.index.receiver = boxe(tk_read_texpr(r, t));
            e.as.index.index    = boxe(tk_read_texpr(r, t));
            return e;
        case 18: {                                                              /* $"…{expr}…": npieces, each piece, then nholes, each hole */
            e.tag = TK_TEXPR_INTERP;
            uint32_t np = tk_read_u32(r);
            tk_str *pieces = tk_alloc((np ? np : 1) * sizeof *pieces); if (!pieces) abort();
            for (uint32_t i = 0; i < np; i += 1) pieces[i] = tk_read_str(r, t);
            uint32_t nh = tk_read_u32(r);
            tk_texpr *holes = tk_alloc((nh ? nh : 1) * sizeof *holes); if (!holes) abort();
            for (uint32_t i = 0; i < nh; i += 1) holes[i] = tk_read_texpr(r, t);
            e.as.interp.pieces = pieces; e.as.interp.npieces = np;
            e.as.interp.holes  = holes;  e.as.interp.nholes  = nh;
            return e;
        }
        case 19:                                                                /* value-level Enum::Member — enum name, member, ordinal */
            e.tag = TK_TEXPR_PATH;
            e.as.path.enum_name = tk_read_str(r, t);
            e.as.path.member    = tk_read_str(r, t);
            e.as.path.ordinal   = tk_read_u64(r);
            return e;
        case 20: {                                                              /* Phase 2 — <expr> in [ … ]: lhs THEN nelems (u64) THEN each elem */
            e.tag = TK_TEXPR_IN;
            e.as.in_expr.lhs = boxe(tk_read_texpr(r, t));
            uint64_t ne = tk_read_u64(r); tk_texpr *es = tk_alloc((ne ? ne : 1) * sizeof *es); if (!es) abort();
            for (uint64_t i = 0; i < ne; i += 1) es[i] = tk_read_texpr(r, t);
            e.as.in_expr.elems = es; e.as.in_expr.nelems = (size_t)ne;
            return e;
        }
        case 21: {                                                              /* C6.7 — [ e0, e1, … ]: nelements (u64) THEN for each: is_spread (u8) + elem (TExpr) */
            e.tag = TK_TEXPR_ARRAY;
            uint64_t na = tk_read_u64(r);
            tk_texpr *es = tk_alloc((na ? na : 1) * sizeof *es); if (!es) abort();
            bool *sp = tk_alloc((na ? na : 1) * sizeof *sp); if (!sp) abort();
            for (uint64_t i = 0; i < na; i += 1) {
                sp[i] = (tk_read_u8(r) != 0);
                es[i] = tk_read_texpr(r, t);
            }
            e.as.array.elements = es; e.as.array.nelements = (size_t)na; e.as.array.is_spread = sp;
            return e;
        }
        case 23: e.tag = TK_TEXPR_CHAR; e.as.char_lit.bytes = tk_read_str(r, t); return e;   // (UTF-8 increment 1) tag 23 = char literal
    }
    r->ok = false; return e;
}

// (C7.16) BindTarget: tag 0 SimpleName | tag 1 DestructurePattern. Inverse of write_bindtarget.
static tk_bind_target read_bindtarget(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_bind_target bt = {0};
    if (tag == 0) { bt.tag = TK_BIND_SIMPLE; bt.as.simple.name = tk_read_str(r, t); return bt; }
    bt.tag = TK_BIND_DESTRUCTURE;
    uint32_t n = tk_read_u32(r);
    tk_str *names = tk_alloc((n ? n : 1) * sizeof *names); if (!names) abort();
    for (uint32_t i = 0; i < n; i += 1) names[i] = tk_read_str(r, t);
    bt.as.destructure.names = names; bt.as.destructure.nnames = (size_t)n;
    return bt;
}

// (C7.16) a TStatement: u8 tag + payload. Inverse of write_tstatement.
static tk_tstatement read_tstmt(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_tstatement s = {0};
    switch (tag) {
        case 0:
            s.tag = TK_TSTMT_BINDING;
            s.as.binding.kind   = (tk_bind_kind)tk_read_u8(r);
            s.as.binding.target = read_bindtarget(r, t);
            s.as.binding.bound  = tk_read_type(r, t);
            s.as.binding.value  = tk_read_texpr(r, t);
            return s;
        case 1:
            s.tag = TK_TSTMT_ASSIGN;
            s.as.assign.name  = tk_read_str(r, t);
            s.as.assign.op    = kind_of(tk_read_u8(r));
            s.as.assign.bound = tk_read_type(r, t);
            s.as.assign.value = tk_read_texpr(r, t);
            s.as.assign.deref = (tk_read_u8(r) != 0);   // (MEM-1b-ii)
            return s;
        case 2:
            s.tag = TK_TSTMT_RETURN;
            s.as.ret.has_value = (tk_read_u8(r) != 0);
            s.as.ret.value     = tk_read_texpr(r, t);
            return s;
        case 3:
            s.tag = TK_TSTMT_LOOP;
            s.as.loop_stmt.label = tk_read_str(r, t);
            s.as.loop_stmt.body  = read_tstmts(r, t, &s.as.loop_stmt.nbody);
            return s;
        case 4: s.tag = TK_TSTMT_BREAK;    s.as.jump.label = tk_read_str(r, t); return s;
        case 5: s.tag = TK_TSTMT_CONTINUE; s.as.jump.label = tk_read_str(r, t); return s;
        case 6: s.tag = TK_TSTMT_EXPR;     s.as.expr_stmt.expr = tk_read_texpr(r, t); return s;
        case 7:                             // TDeferStmt (C7.18): body ([]TStatement)
            s.tag = TK_TSTMT_DEFER;
            s.as.defer_stmt.body = read_tstmts(r, t, &s.as.defer_stmt.nbody);
            return s;
    }
    r->ok = false; return s;
}

// (C7.16) a []TStatement: u64 count, then each. Inverse of write_tstatements.
static tk_tstatement *read_tstmts(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_tstatement *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = read_tstmt(r, t);
    *out_n = (size_t)n;
    return xs;
}

// ============================================================================
// (C7.16) PROGRAM FRAMING readers (mirror tkb_read.tks). In-place reader; values by value.
// ============================================================================
static tk_path read_path_c(tk_reader *r, tk_strs t) {
    uint32_t n = tk_read_u32(r);
    tk_segment *segs = tk_alloc((n ? n : 1) * sizeof *segs); if (!segs) abort();
    for (uint32_t i = 0; i < n; i += 1) segs[i].name = tk_read_str(r, t);
    return (tk_path){ segs, n };
}
static tk_type_expr read_typeexpr(tk_reader *r, tk_strs t);   // fwd (recursive)
static tk_type_expr *read_typeexprs(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_type_expr *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = read_typeexpr(r, t);
    *out_n = (size_t)n; return xs;
}
static tk_type_expr read_typeexpr(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_type_expr te = {0};
    switch (tag) {
        case 0: te.tag = TK_TEXPR_NAMED;    te.as.named.path = read_path_c(r, t); return te;
        case 1: te.tag = TK_TEXPR_SLICE;    te.as.slice.element = tk_box_type(read_typeexpr(r, t)); return te;
        case 2: te.tag = TK_TEXPR_UNION;    te.as.uni.members = read_typeexprs(r, t, &te.as.uni.len); return te;
        case 3: te.tag = TK_TEXPR_OPTIONAL; te.as.optional.inner = tk_box_type(read_typeexpr(r, t)); return te;
        case 4: {   // (W10a) tag 4: params then ret
            te.tag = TK_TEXPR_FUNC;
            te.as.func.params = read_typeexprs(r, t, &te.as.func.nparams);
            te.as.func.ret = tk_box_type(read_typeexpr(r, t));
            return te;
        }
    }
    r->ok = false; return te;
}
static tk_param *read_params(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_param *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) { xs[i].name = tk_read_str(r, t); xs[i].type_ann = read_typeexpr(r, t); xs[i].is_params = false; }   // .tkb is FunctionType-only (closures) today — params is source-only, never serialized
    *out_n = (size_t)n; return xs;
}
static tk_field *read_fields(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_field *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) { xs[i].name = tk_read_str(r, t); xs[i].type_ann = read_typeexpr(r, t); }
    *out_n = (size_t)n; return xs;
}
static tk_str *read_strs(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_str *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = tk_read_str(r, t);
    *out_n = (size_t)n; return xs;
}
static tk_type_body read_typebody(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_type_body tb = {0};
    switch (tag) {
        case 0: tb.tag = TK_BODY_STRUCT;  tb.as.struct_body.fields = read_fields(r, t, &tb.as.struct_body.n_fields); return tb;
        case 1: tb.tag = TK_BODY_ENUM;    tb.as.enum_body.members = read_strs(r, t, &tb.as.enum_body.n_members); return tb;
        case 2: tb.tag = TK_BODY_VARIANT; tb.as.variant_body.type_expr = read_typeexpr(r, t); return tb;
        case 3: tb.tag = TK_BODY_ALIAS;   tb.as.alias_body.alias = read_typeexpr(r, t); return tb;
        case 4: tb.tag = TK_BODY_EXTERN;  return tb;
        case 5: {                                                                  // (C8.6) names + power-of-2 values: n(u64), then for each name_idx(u32)+hi(u64)+lo(u64)
            tb.tag = TK_BODY_FLAGS;
            uint64_t n = tk_read_u64(r);
            tk_str *members = tk_alloc((n ? n : 1) * sizeof *members); if (!members) abort();
            unsigned __int128 *values = tk_alloc((n ? n : 1) * sizeof *values); if (!values) abort();
            for (uint64_t i = 0; i < n; i += 1) {
                members[i] = tk_read_str(r, t);
                uint64_t hi = tk_read_u64(r), lo = tk_read_u64(r);
                values[i] = ((unsigned __int128)hi << 64) | (unsigned __int128)lo;
            }
            tb.as.flags_body.members = members;
            tb.as.flags_body.n_members = (size_t)n;
            tb.as.flags_body.values = values;
            return tb;
        }
        case 6: {   // (W10b.CLASS) fields only — methods not yet serialized (same gap as struct methods)
            tb.tag = TK_BODY_CLASS;
            tb.as.class_body.kind = TK_CLASS_SEALED;
            tb.as.class_body.has_base = false;
            tb.as.class_body.base_name = (tk_str){0};
            tb.as.class_body.has_base_binding = false;
            tb.as.class_body.base_binding_name = (tk_str){0};
            tb.as.class_body.implements = NULL; tb.as.class_body.n_implements = 0;
            tb.as.class_body.fields = read_fields(r, t, &tb.as.class_body.n_fields);
            tb.as.class_body.methods = NULL; tb.as.class_body.n_methods = 0;
            return tb;
        }
    }
    r->ok = false; return tb;
}
static tk_type_decl read_typedecl(tk_reader *r, tk_strs t) {
    tk_type_decl d = {0};
    d.name = tk_read_str(r, t);
    d.body = read_typebody(r, t);
    d.vis = (tk_visibility)tk_read_u8(r);
    d.has_doc = (tk_read_u8(r) != 0);
    d.doc = tk_read_str(r, t);
    d.line = tk_read_u32(r);
    d.col = tk_read_u32(r);
    return d;
}
static tk_use_decl read_usedecl(tk_reader *r, tk_strs t) {
    tk_use_decl u = {0};
    u.path = read_path_c(r, t);
    u.has_alias = (tk_read_u8(r) != 0);
    u.alias = tk_read_str(r, t);
    return u;
}
static tk_tfunction read_tfunction(tk_reader *r, tk_strs t) {
    tk_tfunction f = {0};
    f.name = tk_read_str(r, t);
    f.params = read_params(r, t, &f.nparams);
    f.return_type = tk_read_type(r, t);
    f.body = read_tstmts(r, t, &f.nbody);
    f.vis = (tk_visibility)tk_read_u8(r);
    f.has_doc = (tk_read_u8(r) != 0);
    f.doc = tk_read_str(r, t);
    f.namespace = tk_read_str(r, t);
    f.file = tk_read_str(r, t);
    f.line = tk_read_u32(r);
    f.col = tk_read_u32(r);
    f.is_test = (tk_read_u8(r) != 0);
    f.is_extern = (tk_read_u8(r) != 0);
    f.c_symbol = tk_read_str(r, t);
    f.from_lib = tk_read_str(r, t);
    return f;
}
static tk_titem read_titem(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_titem it = {0};
    switch (tag) {
        case 0: it.tag = TK_TITEM_FUNCTION;  it.as.function  = read_tfunction(r, t); return it;
        case 1: it.tag = TK_TITEM_TYPE_DECL; it.as.type_decl = read_typedecl(r, t); return it;
        case 2: it.tag = TK_TITEM_USE;       it.as.use_decl  = read_usedecl(r, t); return it;
        case 3: it.tag = TK_TITEM_STATEMENT; it.as.statement = read_tstmt(r, t); return it;
    }
    r->ok = false; return it;
}
static tk_titem *read_titems(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_titem *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = read_titem(r, t);
    *out_n = (size_t)n; return xs;
}

// ============================================================================
// (C7.16) MATCH FRAMING readers — pattern-expr / Pattern / TArm.
// ============================================================================
static tk_expr read_pexpr(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_expr e = {0};
    switch (tag) {
        case 0:
            e.tag = TK_EXPR_NUMBER;
            e.as.number.is_float = (tk_read_u8(r) != 0);
            { uint64_t hi = tk_read_u64(r), lo = tk_read_u64(r);
              e.as.number.value = (__int128)(((unsigned __int128)hi << 64) | (unsigned __int128)lo);
              uint64_t fb = tk_read_u64(r); __builtin_memcpy(&e.as.number.fval, &fb, sizeof fb); }
            return e;
        case 1: e.tag = TK_EXPR_STR;  e.as.str.text = tk_read_str(r, t); return e;
        case 2: e.tag = TK_EXPR_BYTE; e.as.byte.value = tk_read_u8(r); return e;
    }
    r->ok = false; return e;
}
static tk_pattern read_pattern(tk_reader *r, tk_strs t);   // fwd (recursive via Alt)
static tk_pattern *read_patterns(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_pattern *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = read_pattern(r, t);
    *out_n = (size_t)n; return xs;
}
static tk_pattern read_pattern(tk_reader *r, tk_strs t) {
    uint8_t tag = tk_read_u8(r);
    tk_pattern p = {0};
    switch (tag) {
        case 0: p.tag = TK_PAT_LITERAL; p.as.literal.value = read_pexpr(r, t); return p;
        case 1: p.tag = TK_PAT_RANGE;   p.as.range.lo = read_pexpr(r, t); p.as.range.hi = read_pexpr(r, t); return p;
        case 2: p.tag = TK_PAT_ALT;     p.as.alt.options = read_patterns(r, t, &p.as.alt.n_options); return p;
        case 3:
            p.tag = TK_PAT_BIND;
            p.as.bind.type_name = read_path_c(r, t);
            p.as.bind.has_binding = (tk_read_u8(r) != 0);
            p.as.bind.binding = tk_read_str(r, t);
            p.as.bind.is_slice = (tk_read_u8(r) != 0);
            if (tk_read_u8(r) != 0) p.as.bind.slice_type = tk_box_type(read_typeexpr(r, t));
            else p.as.bind.slice_type = NULL;
            return p;
        case 4:
            p.tag = TK_PAT_FIELD;
            p.as.field.type_name = read_path_c(r, t);
            p.as.field.fields = read_strs(r, t, &p.as.field.n_fields);
            return p;
        case 5: p.tag = TK_PAT_WILDCARD; return p;
        case 6: p.tag = TK_PAT_NULL; return p;
    }
    r->ok = false; return p;
}
static tk_tarm read_tarm(tk_reader *r, tk_strs t) {
    tk_tarm a = {0};
    a.pattern = read_pattern(r, t);
    a.has_when = (tk_read_u8(r) != 0);
    if (a.has_when) a.guard = boxe(tk_read_texpr(r, t));
    else a.guard = NULL;
    a.body = read_tstmts(r, t, &a.nbody);
    return a;
}
static tk_tarm *read_tarms(tk_reader *r, tk_strs t, size_t *out_n) {
    uint64_t n = tk_read_u64(r);
    tk_tarm *xs = tk_alloc((n ? n : 1) * sizeof *xs); if (!xs) abort();
    for (uint64_t i = 0; i < n; i += 1) xs[i] = read_tarm(r, t);
    *out_n = (size_t)n; return xs;
}

tk_texpr_result tk_deserialize(const tk_byte *data, size_t len) {
    tk_reader r = { data, len, 0, true };
    if (tk_read_u8(&r) != 'T' || tk_read_u8(&r) != 'K' || tk_read_u8(&r) != 'B' || tk_read_u8(&r) != 0)
        return texpr_err("not a .tkb (bad magic)");
    if (tk_read_u32(&r) != 1) return texpr_err("unsupported .tkb version");
    uint64_t stored = tk_read_u64(&r);
    if (len < 16) return texpr_err("truncated .tkb header");
    if (tk_fnv1a(data + 16, len - 16) != stored) return texpr_err(".tkb altered or corrupt (hash mismatch)");
    tk_strs table = tk_read_strtable(&r);
    tk_texpr te = tk_read_texpr(&r, table);
    if (!r.ok) return texpr_err("truncated/corrupt .tkb");
    return texpr_ok(te);
}

// (C7.16) THE PROGRAM ENTRY: verify header + VERSION 2 + hash, then read the typed PROGRAM.
tk_tprogram_result tk_deserialize_program(const tk_byte *data, size_t len) {
    tk_reader r = { data, len, 0, true };
    if (tk_read_u8(&r) != 'T' || tk_read_u8(&r) != 'K' || tk_read_u8(&r) != 'B' || tk_read_u8(&r) != 0)
        return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make("not a .tkb (bad magic)") };
    if (tk_read_u32(&r) != 2) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make("unsupported .tkb program version") };
    uint64_t stored = tk_read_u64(&r);
    if (len < 16) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make("truncated .tkb header") };
    if (tk_fnv1a(data + 16, len - 16) != stored)
        return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make(".tkb altered or corrupt (hash mismatch)") };
    tk_strs table = tk_read_strtable(&r);
    size_t n; tk_titem *items = read_titems(&r, table, &n);
    if (!r.ok) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make("truncated/corrupt .tkb (program)") };
    return (tk_tprogram_result){ .ok = true, .as.value = (tk_tprogram){ items, n } };
}
