// src/emit/tkb_write.c
#include "tkb_write.h"

// prim_byte / kind_byte: the enum's ordinal byte — E7 (the enum→int cast).
static tk_byte prim_byte(tk_prim_kind k) { return (tk_byte)k; }   // [E7]
static tk_byte kind_byte(tk_token_kind k) { return (tk_byte)k; }  // [E7]

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
        case TK_TEXPR_IF:    return tk_write_u8(b, 8);   // reserved — typed if/match needs a stmt serializer (later); read rejects (M.1)
        case TK_TEXPR_MATCH: return tk_write_u8(b, 9);   // reserved — idem (MethodCall has no typed node)
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
    }
    return b;
}
