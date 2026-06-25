// src/emit/tkb_frame.c
#include "tkb_frame.h"

tk_bytes tk_write_u64(tk_bytes b, uint64_t x) {
    for (int k = 0; k < 8; k += 1) { b = tk_bytes_push(b, (tk_byte)(x & 0xFF)); x >>= 8; }
    return b;
}

static tk_bytes append_bytes(tk_bytes dst, tk_bytes src) {
    for (size_t i = 0; i < src.len; i += 1) dst = tk_bytes_push(dst, src.ptr[i]);
    return dst;
}

// --- collect every string (mutates the table via intern) ---

static void collect_type(tk_strtable *t, tk_type ty);
static void collect_type_list(tk_strtable *t, const tk_type *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_type(t, xs[i]);
}
static void collect_type(tk_strtable *t, tk_type ty) {
    switch (ty.tag) {
        case TK_TYPE_NAMED:   tk_st_intern(t, ty.as.named.name); break;
        case TK_TYPE_SLICE:   collect_type(t, *ty.as.slice.element); break;
        case TK_TYPE_VARIANT: collect_type_list(t, ty.as.variant.members, ty.as.variant.len); break;
        case TK_TYPE_FUNC:    collect_type_list(t, ty.as.func.params, ty.as.func.nparams);
                              collect_type(t, *ty.as.func.ret); break;
        default: break;
    }
}
static void collect(tk_strtable *t, const tk_texpr *te) {
    collect_type(t, te->type);
    switch (te->tag) {
        case TK_TEXPR_VAR: tk_st_intern(t, te->as.var.name); break;
        case TK_TEXPR_STR: tk_st_intern(t, te->as.str.text); break;
        case TK_TEXPR_BINARY: collect(t, te->as.binary.left); collect(t, te->as.binary.right); break;
        case TK_TEXPR_UNARY:  collect(t, te->as.unary.operand); break;
        case TK_TEXPR_COMPARE:
            collect(t, te->as.compare.first);
            for (size_t i = 0; i < te->as.compare.nrest; i += 1) collect(t, te->as.compare.rest[i].operand);
            break;
        case TK_TEXPR_CALL:
            for (size_t i = 0; i < te->as.call.callee.len; i += 1) tk_st_intern(t, te->as.call.callee.segments[i].name);
            for (size_t i = 0; i < te->as.call.nargs; i += 1) collect(t, &te->as.call.args[i]);
            break;
        case TK_TEXPR_CAST: collect(t, te->as.cast.expr); break;                 // S1a
        case TK_TEXPR_FIELD_ACCESS:                                              // S1b
            collect(t, te->as.field_access.receiver);
            tk_st_intern(t, te->as.field_access.field);                          // CRITICAL: intern the field name
            break;
        case TK_TEXPR_INTERP:                                                   // $"…{expr}…": intern each piece, recurse holes
            for (size_t i = 0; i < te->as.interp.npieces; i += 1) tk_st_intern(t, te->as.interp.pieces[i]);
            for (size_t i = 0; i < te->as.interp.nholes; i += 1) collect(t, &te->as.interp.holes[i]);
            break;
        default: break;
    }
}

tk_bytes tk_serialize(const tk_texpr *te) {
    tk_strtable table = tk_st_empty();
    collect(&table, te);
    tk_bytes body = tk_write_texpr(tk_write_strtable((tk_bytes){0}, table), table, te);
    uint64_t h = tk_fnv1a(body.ptr, body.len);
    tk_bytes out = {0};
    out = tk_write_u8(out, (tk_byte)'T'); out = tk_write_u8(out, (tk_byte)'K');
    out = tk_write_u8(out, (tk_byte)'B'); out = tk_write_u8(out, 0);
    out = tk_write_u32(out, 1);                  // version 1
    out = tk_write_u64(out, h);                  // FNV-1a of the body
    return append_bytes(out, body);
}
