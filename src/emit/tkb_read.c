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
    }
    r->ok = false; return (tk_type){ .tag = TK_TYPE_VOID };
}

// tk_texpr_result now provided by checker/tast.h (the canonical home — C1).
static tk_texpr_result texpr_ok(tk_texpr t)  { return (tk_texpr_result){ .ok = true,  .as.value = t }; }
static tk_texpr_result texpr_err(const char *m) { return (tk_texpr_result){ .ok = false, .as.error = tk_error_make(m) }; }

static tk_token_kind kind_of(uint8_t b) { return (tk_token_kind)b; }   // [E7: byte→enum]
static tk_texpr *boxe(tk_texpr t) { tk_texpr *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }

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
        case 1: e.tag = TK_TEXPR_VAR;    e.as.var.name  = tk_read_str(r, t); return e;
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
    }
    r->ok = false; return e;
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
