// src/emit/tkb_write.c
#include "tkb_write.h"

// prim_byte / kind_byte: the enum's ordinal byte — E7 (the enum→int cast).
static tk_byte prim_byte(tk_prim_kind k) { return (tk_byte)k; }   // [E7]
static tk_byte kind_byte(tk_token_kind k) { return (tk_byte)k; }  // [E7]
static tk_byte bindkind_byte(tk_bind_kind k) { return (tk_byte)k; }  // (C7.16) Let/Mut/Const → ordinal byte

static tk_bytes write_tstatements(tk_bytes b, tk_strtable t, const tk_tstatement *xs, size_t n);  // (C7.16) fwd (mutual with tk_write_texpr)

static tk_bytes write_types(tk_bytes b, tk_strtable t, const tk_type *xs, size_t n) {
    b = tk_write_u32(b, (uint32_t)n);
    for (size_t i = 0; i < n; i += 1) b = tk_write_type(b, t, xs[i]);
    return b;
}

tk_bytes tk_write_type(tk_bytes b, tk_strtable t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_PRIM:    return tk_write_u8(tk_write_u8(b, 0), prim_byte(ty.as.prim));
        case TK_TYPE_BYTE:    return tk_write_u8(b, 1);
        case TK_TYPE_STR:     return tk_write_u8(b, 2);
        case TK_TYPE_ERROR:   return tk_write_u8(b, 3);
        case TK_TYPE_VOID:    return tk_write_u8(b, 4);   // tag 4 = void (was Unit — B.37)
        case TK_TYPE_SLICE:   return tk_write_type(tk_write_u8(b, 5), t, *ty.as.slice.element);
        case TK_TYPE_NAMED:   return tk_write_u32(tk_write_u8(b, 6), tk_st_find(t, ty.as.named.name));
        case TK_TYPE_VARIANT: return write_types(tk_write_u8(b, 7), t, ty.as.variant.members, ty.as.variant.len);
        case TK_TYPE_FUNC:
            b = write_types(tk_write_u8(b, 8), t, ty.as.func.params, ty.as.func.nparams);
            return tk_write_type(b, t, *ty.as.func.ret);
        case TK_TYPE_OPTIONAL: return tk_write_type(tk_write_u8(b, 9), t, *ty.as.optional.inner);  // tag 9 = T? (B.37)
        case TK_TYPE_PTR:     return tk_write_u8(b, 10);  // (C7.1a) tag 10 = ptr
        case TK_TYPE_UPTR:    return tk_write_u8(b, 11);  // (C7.1a) tag 11 = uptr
    }
    return b;
}

static tk_bytes write_path(tk_bytes b, tk_strtable t, tk_path p) {
    b = tk_write_u32(b, (uint32_t)p.len);
    for (size_t i = 0; i < p.len; i += 1) b = tk_write_u32(b, tk_st_find(t, p.segments[i].name));
    return b;
}

tk_bytes tk_write_texpr(tk_bytes b, tk_strtable t, const tk_texpr *te) {
    b = tk_write_type(b, t, te->type);                   // every node carries its type
    switch (te->tag) {
        case TK_TEXPR_NUMBER: {
            // N1/N2: the number node now carries {is_float, __int128 value, double fval}.
            // Serialize: tag, is_float (u8), the 128-bit value as two u64 (hi then lo),
            // then the double's IEEE-754 bits as a u64. Read is the exact inverse.
            b = tk_write_u8(tk_write_u8(b, 0), (tk_byte)(te->as.number.is_float ? 1 : 0));
            unsigned __int128 uv = (unsigned __int128)te->as.number.value;
            b = tk_write_u64(b, (uint64_t)(uv >> 64));      // hi
            b = tk_write_u64(b, (uint64_t)uv);              // lo
            uint64_t fbits;
            __builtin_memcpy(&fbits, &te->as.number.fval, sizeof fbits);
            return tk_write_u64(b, fbits);
        }
        case TK_TEXPR_VAR:    return tk_write_u32(tk_write_u8(b, 1), tk_st_find(t, te->as.var.name));
        case TK_TEXPR_STR:    return tk_write_u32(tk_write_u8(b, 2), tk_st_find(t, te->as.str.text));
        case TK_TEXPR_BYTE:   return tk_write_u8(tk_write_u8(b, 3), te->as.byte.value);
        case TK_TEXPR_BINARY:
            b = tk_write_u8(tk_write_u8(b, 4), kind_byte(te->as.binary.op));
            b = tk_write_texpr(b, t, te->as.binary.left);
            return tk_write_texpr(b, t, te->as.binary.right);
        case TK_TEXPR_UNARY:
            b = tk_write_u8(tk_write_u8(b, 5), kind_byte(te->as.unary.op));
            return tk_write_texpr(b, t, te->as.unary.operand);
        case TK_TEXPR_COMPARE: {
            b = tk_write_texpr(tk_write_u8(b, 6), t, te->as.compare.first);
            b = tk_write_u32(b, (uint32_t)te->as.compare.nrest);
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) {
                b = tk_write_u8(b, kind_byte(te->as.compare.rest[i].op));
                b = tk_write_texpr(b, t, te->as.compare.rest[i].operand);
            }
            return b;
        }
        case TK_TEXPR_CALL: {
            b = write_path(tk_write_u8(b, 7), t, te->as.call.callee);
            b = tk_write_u32(b, (uint32_t)te->as.call.nargs);
            for (size_t i = 0; i < te->as.call.nargs; i += 1) b = tk_write_texpr(b, t, &te->as.call.args[i]);
            return b;
        }
        case TK_TEXPR_IF:                                                        // (C7.16) cond + then_blk + has_else + else_blk
            b = tk_write_texpr(tk_write_u8(b, 8), t, te->as.if_expr.cond);
            b = write_tstatements(b, t, te->as.if_expr.then_blk, te->as.if_expr.nthen);
            b = tk_write_u8(b, (tk_byte)(te->as.if_expr.has_else ? 1 : 0));
            return write_tstatements(b, t, te->as.if_expr.else_blk, te->as.if_expr.nelse);
        case TK_TEXPR_MATCH: return tk_write_u8(b, 9);   // reserved — TMatchExpr arms need the pattern codec (next C7.16 slice); read rejects (M.1)
        case TK_TEXPR_CAST:                                                      // S1a — payload = inner expr (target rides te->type)
            return tk_write_texpr(tk_write_u8(b, 10), t, te->as.cast.expr);
        case TK_TEXPR_FIELD_ACCESS:                                             // S1b — receiver THEN field index
            b = tk_write_texpr(tk_write_u8(b, 11), t, te->as.field_access.receiver);
            return tk_write_u32(b, tk_st_find(t, te->as.field_access.field));
        case TK_TEXPR_BOOL:                                                    // W2 — bool literal
            return tk_write_u8(tk_write_u8(b, 12), (tk_byte)(te->as.boolean.value ? 1 : 0));
        case TK_TEXPR_NULL:    return tk_write_u8(b, 13);                       // W2 — null literal (no payload)
        case TK_TEXPR_SAFE_FIELD_ACCESS:                                       // W2 — recv?.field
            b = tk_write_texpr(tk_write_u8(b, 14), t, te->as.safe_field_access.receiver);
            return tk_write_u32(b, tk_st_find(t, te->as.safe_field_access.field));
        case TK_TEXPR_COALESCE:                                                // W2 — left ?? right
            b = tk_write_texpr(tk_write_u8(b, 15), t, te->as.coalesce.left);
            return tk_write_texpr(b, t, te->as.coalesce.right);
        case TK_TEXPR_STRUCT_INIT:                                             // W4a — Name { f = v, … }
            b = tk_write_u32(tk_write_u8(b, 16), (uint32_t)te->as.struct_init.nfields);
            for (size_t i = 0; i < te->as.struct_init.nfields; i += 1) {
                b = tk_write_u32(b, tk_st_find(t, te->as.struct_init.field_names[i]));   // field name (interned)
                b = tk_write_texpr(b, t, &te->as.struct_init.field_vals[i]);             // field value
            }
            return b;
        case TK_TEXPR_INDEX:                                                   // W5-idx — recv[index]: receiver THEN index
            b = tk_write_texpr(tk_write_u8(b, 17), t, te->as.index.receiver);
            return tk_write_texpr(b, t, te->as.index.index);
        case TK_TEXPR_INTERP:                                                 // $"…{expr}…": npieces, each piece (interned), then nholes, each hole
            b = tk_write_u32(tk_write_u8(b, 18), (uint32_t)te->as.interp.npieces);
            for (size_t i = 0; i < te->as.interp.npieces; i += 1)
                b = tk_write_u32(b, tk_st_find(t, te->as.interp.pieces[i]));   // piece text (interned)
            b = tk_write_u32(b, (uint32_t)te->as.interp.nholes);
            for (size_t i = 0; i < te->as.interp.nholes; i += 1)
                b = tk_write_texpr(b, t, &te->as.interp.holes[i]);            // hole value
            return b;
        case TK_TEXPR_PATH:                                                  // value-level Enum::Member — enum name, member, ordinal
            b = tk_write_u8(b, 19);
            b = tk_write_u32(b, tk_st_find(t, te->as.path.enum_name));
            b = tk_write_u32(b, tk_st_find(t, te->as.path.member));
            return tk_write_u64(b, (uint64_t)te->as.path.ordinal);
        case TK_TEXPR_IN:                                                    // Phase 2 — <expr> in [ … ]: lhs THEN nelems (u64) THEN each elem
            b = tk_write_texpr(tk_write_u8(b, 20), t, te->as.in_expr.lhs);
            b = tk_write_u64(b, (uint64_t)te->as.in_expr.nelems);
            for (size_t i = 0; i < te->as.in_expr.nelems; i += 1) b = tk_write_texpr(b, t, &te->as.in_expr.elems[i]);
            return b;
        case TK_TEXPR_ARRAY:                                                 // Increment B+ — [ e0, … ]: nelements (u64) THEN each element
            b = tk_write_u64(tk_write_u8(b, 21), (uint64_t)te->as.array.nelements);
            for (size_t i = 0; i < te->as.array.nelements; i += 1) b = tk_write_texpr(b, t, &te->as.array.elements[i]);
            return b;
    }
    return b;
}

// (C7.16) BindTarget: tag 0 SimpleName (interned name) | tag 1 DestructurePattern (count + names).
static tk_bytes write_bindtarget(tk_bytes b, tk_strtable t, tk_bind_target bt) {
    if (bt.tag == TK_BIND_SIMPLE) return tk_write_u32(tk_write_u8(b, 0), tk_st_find(t, bt.as.simple.name));
    b = tk_write_u32(tk_write_u8(b, 1), (uint32_t)bt.as.destructure.nnames);
    for (size_t i = 0; i < bt.as.destructure.nnames; i += 1) b = tk_write_u32(b, tk_st_find(t, bt.as.destructure.names[i]));
    return b;
}

// (C7.16) a TStatement: u8 tag + payload. Inverse of tk_read_tstmt.
static tk_bytes write_tstatement(tk_bytes b, tk_strtable t, const tk_tstatement *s) {
    switch (s->tag) {
        case TK_TSTMT_BINDING:                                               // bindkind (u8) + target + bound (Type) + value (TExpr)
            b = write_bindtarget(tk_write_u8(tk_write_u8(b, 0), bindkind_byte(s->as.binding.kind)), t, s->as.binding.target);
            b = tk_write_type(b, t, s->as.binding.bound);
            return tk_write_texpr(b, t, &s->as.binding.value);
        case TK_TSTMT_ASSIGN:                                                // name (u32) + op (u8) + bound (Type) + value (TExpr)
            b = tk_write_u8(tk_write_u32(tk_write_u8(b, 1), tk_st_find(t, s->as.assign.name)), kind_byte(s->as.assign.op));
            b = tk_write_type(b, t, s->as.assign.bound);
            return tk_write_texpr(b, t, &s->as.assign.value);
        case TK_TSTMT_RETURN:                                                // has_value (u8) + value (TExpr)
            return tk_write_texpr(tk_write_u8(tk_write_u8(b, 2), (tk_byte)(s->as.ret.has_value ? 1 : 0)), t, &s->as.ret.value);
        case TK_TSTMT_LOOP:                                                  // label (u32) + body ([]TStatement)
            return write_tstatements(tk_write_u32(tk_write_u8(b, 3), tk_st_find(t, s->as.loop_stmt.label)), t, s->as.loop_stmt.body, s->as.loop_stmt.nbody);
        case TK_TSTMT_BREAK:    return tk_write_u32(tk_write_u8(b, 4), tk_st_find(t, s->as.jump.label));
        case TK_TSTMT_CONTINUE: return tk_write_u32(tk_write_u8(b, 5), tk_st_find(t, s->as.jump.label));
        case TK_TSTMT_EXPR:     return tk_write_texpr(tk_write_u8(b, 6), t, &s->as.expr_stmt.expr);
    }
    return b;
}

// (C7.16) a []TStatement: u64 count, then each. Inverse of tk_read_tstmts.
static tk_bytes write_tstatements(tk_bytes b, tk_strtable t, const tk_tstatement *xs, size_t n) {
    b = tk_write_u64(b, (uint64_t)n);
    for (size_t i = 0; i < n; i += 1) b = write_tstatement(b, t, &xs[i]);
    return b;
}
