// src/emit/tkb_frame.c
#include "tkb_frame.h"

// tk_write_u64 is defined ONCE, in tkb_buf.c (lo u32 then hi u32 — the same LE byte run). This
// file used to carry an identical-output duplicate; removed (one definition, no duplicate symbol).
// tkb_frame.h still declares it (it composes the writers); the definition links from tkb_buf.c.

static tk_bytes append_bytes(tk_bytes dst, tk_bytes src) {
    for (size_t i = 0; i < src.len; i += 1) dst = tk_bytes_push(dst, src.ptr[i]);
    return dst;
}

// --- collect every string (mutates the table via intern) ---

static void collect_type(tk_strtable *t, tk_type ty);
static void collect_tstmts(tk_strtable *t, const tk_tstatement *xs, size_t n);   // (C7.16) fwd (mutual with collect)
static void collect_tarms(tk_strtable *t, const tk_tarm *xs, size_t n);          // (C7.16) fwd (mutual with collect — match arms)
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
        case TK_TEXPR_VAR: tk_st_intern(t, te->as.var.name); tk_st_intern(t, te->as.var.func_ns); break;   // (W10a) intern func_ns too
        case TK_TEXPR_STR: tk_st_intern(t, te->as.str.text); break;
        case TK_TEXPR_CHAR: tk_st_intern(t, te->as.char_lit.bytes); break;   // (UTF-8 increment 1) intern the codepoint's UTF-8 bytes
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
        // (C7.16) the kinds previously dropped to `default` — their strings/sub-exprs MUST be interned
        // or the writer's st_find returns the not-found sentinel and the node fails to round-trip.
        case TK_TEXPR_SAFE_FIELD_ACCESS:
            collect(t, te->as.safe_field_access.receiver);
            tk_st_intern(t, te->as.safe_field_access.field);
            break;
        case TK_TEXPR_COALESCE:
            collect(t, te->as.coalesce.left); collect(t, te->as.coalesce.right);
            break;
        case TK_TEXPR_STRUCT_INIT:
            for (size_t i = 0; i < te->as.struct_init.nfields; i += 1) {
                tk_st_intern(t, te->as.struct_init.field_names[i]);
                collect(t, &te->as.struct_init.field_vals[i]);
            }
            break;
        case TK_TEXPR_INDEX:
            collect(t, te->as.index.receiver); collect(t, te->as.index.index);
            break;
        case TK_TEXPR_IN:
            collect(t, te->as.in_expr.lhs);
            for (size_t i = 0; i < te->as.in_expr.nelems; i += 1) collect(t, &te->as.in_expr.elems[i]);
            break;
        case TK_TEXPR_ARRAY:
            // C6.7: each element carries an is_spread flag (bool *is_spread parallel to elements[]).
            // The flag is a plain byte in the wire format — no string to intern, just recurse the expr.
            for (size_t i = 0; i < te->as.array.nelements; i += 1) collect(t, &te->as.array.elements[i]);
            break;
        case TK_TEXPR_PATH:
            tk_st_intern(t, te->as.path.enum_name); tk_st_intern(t, te->as.path.member);
            break;
        case TK_TEXPR_IF:                                                       // (C7.16) cond + both statement blocks
            collect(t, te->as.if_expr.cond);
            collect_tstmts(t, te->as.if_expr.then_blk, te->as.if_expr.nthen);
            collect_tstmts(t, te->as.if_expr.else_blk, te->as.if_expr.nelse);
            break;
        case TK_TEXPR_MATCH:                                                    // (C7.16) subject + arms
            collect(t, te->as.match_expr.subject);
            collect_tarms(t, te->as.match_expr.arms, te->as.match_expr.narms);
            break;
        default: break;
    }
}

// (C7.16) collect a BindTarget's name(s).
static void collect_bindtarget(tk_strtable *t, tk_bind_target bt) {
    if (bt.tag == TK_BIND_SIMPLE) { tk_st_intern(t, bt.as.simple.name); return; }
    for (size_t i = 0; i < bt.as.destructure.nnames; i += 1) tk_st_intern(t, bt.as.destructure.names[i]);
}

// (C7.16) collect a TStatement's strings (mirrors write_tstatement).
static void collect_tstmt(tk_strtable *t, const tk_tstatement *s) {
    switch (s->tag) {
        case TK_TSTMT_BINDING:  collect_bindtarget(t, s->as.binding.target); collect_type(t, s->as.binding.bound); collect(t, &s->as.binding.value); break;
        case TK_TSTMT_ASSIGN:   tk_st_intern(t, s->as.assign.name); collect_type(t, s->as.assign.bound); collect(t, &s->as.assign.value); break;
        case TK_TSTMT_RETURN:   collect(t, &s->as.ret.value); break;
        case TK_TSTMT_LOOP:     tk_st_intern(t, s->as.loop_stmt.label); collect_tstmts(t, s->as.loop_stmt.body, s->as.loop_stmt.nbody); break;
        case TK_TSTMT_BREAK:
        case TK_TSTMT_CONTINUE: tk_st_intern(t, s->as.jump.label); break;
        case TK_TSTMT_EXPR:     collect(t, &s->as.expr_stmt.expr); break;
        case TK_TSTMT_DEFER:    collect_tstmts(t, s->as.defer_stmt.body, s->as.defer_stmt.nbody); break;
    }
}

// (C7.16) collect a []TStatement.
static void collect_tstmts(tk_strtable *t, const tk_tstatement *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_tstmt(t, &xs[i]);
}

// ============================================================================
// (C7.16) PROGRAM FRAMING collect pass (mirror tkb_frame.tks).
// ============================================================================
static void collect_typeexpr(tk_strtable *t, tk_type_expr te);   // fwd (recursive)
static void collect_typeexprs(tk_strtable *t, const tk_type_expr *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_typeexpr(t, xs[i]);
}
static void collect_typeexpr(tk_strtable *t, tk_type_expr te) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:    for (size_t i = 0; i < te.as.named.path.len; i += 1) tk_st_intern(t, te.as.named.path.segments[i].name); break;
        case TK_TEXPR_SLICE:    collect_typeexpr(t, *te.as.slice.element); break;
        case TK_TEXPR_UNION:    collect_typeexprs(t, te.as.uni.members, te.as.uni.len); break;
        case TK_TEXPR_OPTIONAL: collect_typeexpr(t, *te.as.optional.inner); break;
        case TK_TEXPR_FUNC:     collect_typeexprs(t, te.as.func.params, te.as.func.nparams); if (te.as.func.ret) collect_typeexpr(t, *te.as.func.ret); break;   // (W10a)
    }
}
static void collect_params(tk_strtable *t, const tk_param *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) { tk_st_intern(t, xs[i].name); collect_typeexpr(t, xs[i].type_ann); }
}
static void collect_fields(tk_strtable *t, const tk_field *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) { tk_st_intern(t, xs[i].name); collect_typeexpr(t, xs[i].type_ann); }
}
static void collect_typebody(tk_strtable *t, tk_type_body tb) {
    switch (tb.tag) {
        case TK_BODY_STRUCT:  collect_fields(t, tb.as.struct_body.fields, tb.as.struct_body.n_fields); break;
        case TK_BODY_ENUM:    for (size_t i = 0; i < tb.as.enum_body.n_members; i += 1) tk_st_intern(t, tb.as.enum_body.members[i]); break;
        case TK_BODY_FLAGS:   for (size_t i = 0; i < tb.as.flags_body.n_members; i += 1) tk_st_intern(t, tb.as.flags_body.members[i]); break;
        case TK_BODY_VARIANT: collect_typeexpr(t, tb.as.variant_body.type_expr); break;
        case TK_BODY_ALIAS:   collect_typeexpr(t, tb.as.alias_body.alias); break;
        case TK_BODY_EXTERN:  break;
        case TK_BODY_CLASS:   collect_fields(t, tb.as.class_body.fields, tb.as.class_body.n_fields); break;   // (W10b.CLASS) methods not yet collected, same pre-existing gap as struct methods
        case TK_BODY_INTERFACE: for (size_t i = 0; i < tb.as.interface_body.n_extends; i += 1) tk_st_intern(t, tb.as.interface_body.extends[i]); break;   // (W10b.IF) extends names only — method sigs not serialized (same gap as struct/class methods)
    }
}
static void collect_typedecl(tk_strtable *t, tk_type_decl d) {
    tk_st_intern(t, d.name); collect_typebody(t, d.body); tk_st_intern(t, d.doc);
}
static void collect_usedecl(tk_strtable *t, tk_use_decl u) {
    for (size_t i = 0; i < u.path.len; i += 1) tk_st_intern(t, u.path.segments[i].name);
    tk_st_intern(t, u.alias);
}
static void collect_tfunction(tk_strtable *t, const tk_tfunction *f) {
    tk_st_intern(t, f->name);
    collect_params(t, f->params, f->nparams);
    collect_type(t, f->return_type);
    collect_tstmts(t, f->body, f->nbody);
    tk_st_intern(t, f->doc); tk_st_intern(t, f->namespace); tk_st_intern(t, f->file);
    tk_st_intern(t, f->c_symbol); tk_st_intern(t, f->from_lib);
}
static void collect_titem(tk_strtable *t, const tk_titem *it) {
    switch (it->tag) {
        case TK_TITEM_FUNCTION:  collect_tfunction(t, &it->as.function); break;
        case TK_TITEM_TYPE_DECL: collect_typedecl(t, it->as.type_decl); break;
        case TK_TITEM_USE:       collect_usedecl(t, it->as.use_decl); break;
        case TK_TITEM_STATEMENT: collect_tstmt(t, &it->as.statement); break;
    }
}
static void collect_program(tk_strtable *t, const tk_tprogram *prog) {
    for (size_t i = 0; i < prog->nitems; i += 1) collect_titem(t, &prog->items[i]);
}

// (C7.16) MATCH FRAMING collect — pattern-expr / Pattern / TArm.
static void collect_pexpr(tk_strtable *t, const tk_expr *e) {
    if (e->tag == TK_EXPR_STR) tk_st_intern(t, e->as.str.text);   // Number/ByteLit carry no string
}
static void collect_pattern(tk_strtable *t, const tk_pattern *p);   // fwd (recursive via Alt)
static void collect_patterns(tk_strtable *t, const tk_pattern *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_pattern(t, &xs[i]);
}
static void collect_pattern(tk_strtable *t, const tk_pattern *p) {
    switch (p->tag) {
        case TK_PAT_LITERAL: collect_pexpr(t, &p->as.literal.value); break;
        case TK_PAT_RANGE:   collect_pexpr(t, &p->as.range.lo); collect_pexpr(t, &p->as.range.hi); break;
        case TK_PAT_ALT:     collect_patterns(t, p->as.alt.options, p->as.alt.n_options); break;
        case TK_PAT_BIND:
            for (size_t i = 0; i < p->as.bind.type_name.len; i += 1) tk_st_intern(t, p->as.bind.type_name.segments[i].name);
            tk_st_intern(t, p->as.bind.binding);
            if (p->as.bind.slice_type != NULL) collect_typeexpr(t, *p->as.bind.slice_type);
            break;
        case TK_PAT_FIELD:
            for (size_t i = 0; i < p->as.field.type_name.len; i += 1) tk_st_intern(t, p->as.field.type_name.segments[i].name);
            for (size_t i = 0; i < p->as.field.n_fields; i += 1) tk_st_intern(t, p->as.field.fields[i]);
            break;
        case TK_PAT_WILDCARD: case TK_PAT_NULL: break;
    }
}
static void collect_tarm(tk_strtable *t, const tk_tarm *a) {
    collect_pattern(t, &a->pattern);
    if (a->has_when) collect(t, a->guard);
    collect_tstmts(t, a->body, a->nbody);
}
static void collect_tarms(tk_strtable *t, const tk_tarm *xs, size_t n) {
    for (size_t i = 0; i < n; i += 1) collect_tarm(t, &xs[i]);
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

// (C7.16) serialize a whole typed PROGRAM. Same frame, VERSION 2 (program tree). Keystone for .tkl.
tk_bytes tk_serialize_program(const tk_tprogram *prog) {
    tk_strtable table = tk_st_empty();
    collect_program(&table, prog);
    tk_bytes body = tk_write_program(tk_write_strtable((tk_bytes){0}, table), table, prog);
    uint64_t h = tk_fnv1a(body.ptr, body.len);
    tk_bytes out = {0};
    out = tk_write_u8(out, (tk_byte)'T'); out = tk_write_u8(out, (tk_byte)'K');
    out = tk_write_u8(out, (tk_byte)'B'); out = tk_write_u8(out, 0);
    out = tk_write_u32(out, 2);                  // version 2 = program tree
    out = tk_write_u64(out, h);
    return append_bytes(out, body);
}
