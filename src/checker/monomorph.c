// src/checker/monomorph.c — (S4b crumb 2c) the monomorphization pass (monomorph.tks twin).
//
// Stamps a concrete copy of each generic function per distinct instantiation and rewrites
// every generic call to the stamped instance's mangled name, so codegen / the VM / the test
// runner receive a tree with ONLY concrete functions. A program with no generic function is
// returned byte-identical (the no-op guard), keeping the non-generic corpus unperturbed.
//
// FUNCTIONAL-vs-IMPERATIVE divergence (the same the typer notes): the Teko walk RETURNS a
// rewritten node + the discovered instantiations (Teko has no out-params); the C twin threads
// a growable inst list via an out-pointer. The behavior is identical.
#include "monomorph.h"
#include "resolve.h"   // tk_subst_type, tk_unify, tk_type_param_table, tk_resolve_type, tk_is_type_param
#include "../parser/ast.h"
#include "../text/text.h"
#include <string.h>
#include <stdlib.h>

// ── small allocation / string helpers ──────────────────────────────────────────────────
static tk_type *mono_box_type(tk_type t) { tk_type *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }
static bool mono_name_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }

// a fresh tk_str copy of a C string (whole-compile-lifetime; tk_alloc — M.5 arena-style).
static tk_str mono_cstr(const char *s) {
    size_t n = strlen(s);
    tk_byte *buf = tk_alloc(n ? n : 1); if (!buf) abort();
    if (n) memcpy(buf, s, n);
    return (tk_str){ .ptr = buf, .len = n };
}
// a ++ b — a fresh owned tk_str (tk_str_concat twin, local to keep the DAG tight).
static tk_str mono_concat(tk_str a, tk_str b) {
    size_t n = a.len + b.len;
    tk_byte *buf = tk_alloc(n ? n : 1); if (!buf) abort();
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ .ptr = buf, .len = n };
}

// the surface spelling of a prim kind (mirror of resolve.c::prim_name — re-derived locally;
// resolve.c's is static). Used to mangle a prim type-arg ("i64", "u8", "f64", "bool", …).
static const char *mono_prim_name(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   return "u8";   case TK_PRIM_U16:  return "u16";
        case TK_PRIM_U32:  return "u32";  case TK_PRIM_U64:  return "u64";
        case TK_PRIM_U128: return "u128";
        case TK_PRIM_I8:   return "i8";   case TK_PRIM_I16:  return "i16";
        case TK_PRIM_I32:  return "i32";  case TK_PRIM_I64:  return "i64";
        case TK_PRIM_I128: return "i128";
        case TK_PRIM_F16:  return "f16";  case TK_PRIM_F32:  return "f32";
        case TK_PRIM_F64:  return "f64";
        case TK_PRIM_BOOL: return "bool";
    }
    return "prim";
}

// ── name mangling ────────────────────────────────────────────────────────────────────────
// a type → its symbol fragment for the mangled instance name. (monomorph.tks: mono_type_mangle.)
static tk_str mono_type_mangle(tk_type t) {
    switch (t.tag) {
        case TK_TYPE_PRIM:     return mono_cstr(mono_prim_name(t.as.prim));
        case TK_TYPE_STR:      return mono_cstr("str");
        case TK_TYPE_BYTE:     return mono_cstr("byte");
        case TK_TYPE_CHAR:     return mono_cstr("char");
        case TK_TYPE_NAMED:    return t.as.named.name;
        case TK_TYPE_SLICE:    { tk_str e = mono_type_mangle(*t.as.slice.element);   return mono_concat(mono_cstr("slice_"), e); }
        case TK_TYPE_OPTIONAL: { tk_str e = mono_type_mangle(*t.as.optional.inner); return mono_concat(mono_cstr("opt_"),   e); }
        case TK_TYPE_ERROR:    return mono_cstr("error");
        case TK_TYPE_VOID:     return mono_cstr("void");
        case TK_TYPE_PTR:      return t.as.ptr.inner ? mono_concat(mono_cstr("ptr_"), mono_type_mangle(*t.as.ptr.inner)) : mono_cstr("ptr");
        case TK_TYPE_UPTR:     return mono_cstr("uptr");
        case TK_TYPE_REF:      return mono_concat(mono_cstr("ref_"), mono_type_mangle(*t.as.ref.inner));   // (MEM-1b)
        case TK_TYPE_VARIANT:  return mono_cstr("variant");
        case TK_TYPE_FUNC:     return mono_cstr("func");
    }
    return mono_cstr("type");
}

// the mangled instance name `<bare>__g__<arg0>[__<arg1>...]`. (monomorph.tks: mono_mangle_name.)
static tk_str mono_mangle_name(tk_str bare, tk_str *type_params, size_t n_tps, tk_subst s) {
    tk_str out = mono_concat(bare, mono_cstr("__g__"));
    for (size_t i = 0; i < n_tps; i += 1) {
        if (i > 0) out = mono_concat(out, mono_cstr("__"));
        tk_type tp = (tk_type){ .tag = TK_TYPE_NAMED, .as.named = { type_params[i] } };
        tk_type ct = tk_subst_type(tp, s);
        out = mono_concat(out, mono_type_mangle(ct));
    }
    return out;
}

// ── the inst queue (growable) ──────────────────────────────────────────────────────────
typedef struct { tk_str fn_name; tk_str mangled; tk_subst s; } tk_mono_inst;
typedef struct { tk_mono_inst *ptr; size_t len, cap; } tk_mono_list;

static void mono_list_push(tk_mono_list *l, tk_mono_inst x) {
    if (l->len == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        l->ptr = tk_realloc0(l->ptr, nc * sizeof *l->ptr); if (!l->ptr) abort();
        l->cap = nc;
    }
    l->ptr[l->len++] = x;
}
static bool mono_seen(tk_mono_list *l, tk_str mangled) {
    for (size_t i = 0; i < l->len; i += 1) if (mono_name_eq(l->ptr[i].mangled, mangled)) return true;
    return false;
}

// find a type-param's bound concrete type in a Subst's parallel name/type arrays.
static tk_type *mono_subst_find(tk_subst s, tk_str name) {
    for (size_t i = 0; i < s.n_bind; i += 1) if (mono_name_eq(s.names[i], name)) return &s.types[i];
    return NULL;
}

// ── semantic Type → syntactic TypeExpr ──────────────────────────────────────────────────
static tk_type_expr mono_named_texpr(const char *name) {
    tk_segment *segs = NULL; size_t n = 0;
    tk_segs_push(&segs, &n, (tk_segment){ .name = mono_cstr(name) });
    return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = segs, .len = n }, .args = NULL, .args_len = 0 } };
}
static tk_type_expr mono_named_texpr_str(tk_str name) {
    tk_segment *segs = NULL; size_t n = 0;
    tk_segs_push(&segs, &n, (tk_segment){ .name = name });
    return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = segs, .len = n }, .args = NULL, .args_len = 0 } };
}
static tk_type_expr type_to_texpr(tk_type t);
tk_type_expr tk_type_to_texpr(tk_type t) { return type_to_texpr(t); }
static tk_type_expr type_to_texpr(tk_type t) {
    switch (t.tag) {
        case TK_TYPE_PRIM:     return mono_named_texpr(mono_prim_name(t.as.prim));
        case TK_TYPE_STR:      return mono_named_texpr("str");
        case TK_TYPE_BYTE:     return mono_named_texpr("byte");
        case TK_TYPE_CHAR:     return mono_named_texpr("char");
        case TK_TYPE_ERROR:    return mono_named_texpr("error");
        case TK_TYPE_NAMED:    return mono_named_texpr_str(t.as.named.name);
        case TK_TYPE_SLICE:    return (tk_type_expr){ .tag = TK_TEXPR_SLICE, .as.slice = { tk_box_type(type_to_texpr(*t.as.slice.element)) } };
        case TK_TYPE_OPTIONAL: return (tk_type_expr){ .tag = TK_TEXPR_OPTIONAL, .as.optional = { tk_box_type(type_to_texpr(*t.as.optional.inner)) } };
        case TK_TYPE_PTR: {
            if (t.as.ptr.inner == NULL) return mono_named_texpr("ptr");
            tk_segment *segs = NULL; size_t ns = 0;
            tk_segs_push(&segs, &ns, (tk_segment){ .name = mono_cstr("ptr") });
            tk_type_expr *args = NULL; size_t na = 0;
            tk_types_push(&args, &na, type_to_texpr(*t.as.ptr.inner));
            return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = segs, .len = ns }, .args = args, .args_len = na } };
        }
        case TK_TYPE_UPTR:     return mono_named_texpr("uptr");
        case TK_TYPE_REF: {   // (MEM-1b) Ref<T> → NamedType{ Ref, [inner] }
            tk_segment *segs = NULL; size_t ns = 0;
            tk_segs_push(&segs, &ns, (tk_segment){ .name = mono_cstr("Ref") });
            tk_type_expr *args = NULL; size_t na = 0;
            tk_types_push(&args, &na, type_to_texpr(*t.as.ref.inner));
            return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = segs, .len = ns }, .args = args, .args_len = na } };
        }
        case TK_TYPE_VOID:     return mono_named_texpr("void");
        case TK_TYPE_VARIANT:  return mono_named_texpr("variant");
        case TK_TYPE_FUNC: {   // (W10a) reconstruct a FunctionType `(A, B) -> R` (not a placeholder name)
            tk_type_expr *ps = NULL; size_t np = 0;
            for (size_t i = 0; i < t.as.func.nparams; i += 1)
                tk_types_push(&ps, &np, type_to_texpr(t.as.func.params[i]));
            tk_type_expr *ret = tk_box_type(type_to_texpr(*t.as.func.ret));
            return (tk_type_expr){ .tag = TK_TEXPR_FUNC, .as.func = { .params = ps, .nparams = np, .ret = ret } };
        }
    }
    return mono_named_texpr("type");
}

// substitute the type-params in a SYNTACTIC TypeExpr through a Subst. (monomorph.tks: subst_typeexpr.)
static tk_type_expr subst_typeexpr(tk_type_expr te, tk_subst s) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            if (te.as.named.path.len == 1) {
                tk_str nm = te.as.named.path.segments[0].name;
                if (tk_is_type_param(nm, s.params, s.n_params)) {
                    tk_type *ct = mono_subst_find(s, nm);
                    if (ct) return type_to_texpr(*ct);
                }
            }
            return te;
        }
        case TK_TEXPR_SLICE:
            return (tk_type_expr){ .tag = TK_TEXPR_SLICE, .as.slice = { tk_box_type(subst_typeexpr(*te.as.slice.element, s)) } };
        case TK_TEXPR_OPTIONAL:
            return (tk_type_expr){ .tag = TK_TEXPR_OPTIONAL, .as.optional = { tk_box_type(subst_typeexpr(*te.as.optional.inner, s)) } };
        case TK_TEXPR_UNION: {
            tk_type_expr *ms = NULL; size_t n = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1) tk_types_push(&ms, &n, subst_typeexpr(te.as.uni.members[i], s));
            return (tk_type_expr){ .tag = TK_TEXPR_UNION, .as.uni = { ms, n } };
        }
        case TK_TEXPR_FUNC: {   // (W10a) substitute through params + return
            tk_type_expr *ps = NULL; size_t np = 0;
            for (size_t i = 0; i < te.as.func.nparams; i += 1) tk_types_push(&ps, &np, subst_typeexpr(te.as.func.params[i], s));
            tk_type_expr *ret = te.as.func.ret ? tk_box_type(subst_typeexpr(*te.as.func.ret, s)) : NULL;
            return (tk_type_expr){ .tag = TK_TEXPR_FUNC, .as.func = { .params = ps, .nparams = np, .ret = ret } };
        }
    }
    return te;
}

// ── generic-fn lookup ────────────────────────────────────────────────────────────────────
static const tk_tfunction *find_generic_fn(tk_tprogram prog, tk_str name) {
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag == TK_TITEM_FUNCTION) {
            const tk_tfunction *f = &prog.items[i].as.function;
            if (f->n_type_params > 0 && mono_name_eq(f->name, name)) return f;
        }
    }
    return NULL;
}

// ── the total rewrite walk ───────────────────────────────────────────────────────────────
// forward declarations (mutual recursion).
static bool mono_texpr(tk_texpr e, tk_subst s, tk_tprogram prog, tk_type_table table, tk_texpr *out, tk_mono_list *insts, tk_error *err);
static bool mono_block(tk_tstatement *stmts, size_t n, tk_subst s, tk_tprogram prog, tk_type_table table, tk_tstatement **out, size_t *nout, tk_mono_list *insts, tk_error *err);
static bool mono_tstmt(tk_tstatement st, tk_subst s, tk_tprogram prog, tk_type_table table, tk_tstatement *out, tk_mono_list *insts, tk_error *err);

static tk_texpr *mono_box(tk_texpr t) { tk_texpr *p = tk_alloc(sizeof *p); if (!p) abort(); *p = t; return p; }

static bool mono_texpr(tk_texpr e, tk_subst s, tk_tprogram prog, tk_type_table table, tk_texpr *out, tk_mono_list *insts, tk_error *err) {
    tk_texpr r = e;
    r.type = tk_subst_type(e.type, s);
    switch (e.tag) {
        case TK_TEXPR_NUMBER: case TK_TEXPR_VAR: case TK_TEXPR_STR: case TK_TEXPR_BYTE:
        case TK_TEXPR_CHAR:
        case TK_TEXPR_BOOL: case TK_TEXPR_NULL: case TK_TEXPR_PATH:
            break;   // leaves — only the type is substituted
        case TK_TEXPR_BINARY: {
            tk_texpr l, rr;
            if (!mono_texpr(*e.as.binary.left, s, prog, table, &l, insts, err)) return false;
            if (!mono_texpr(*e.as.binary.right, s, prog, table, &rr, insts, err)) return false;
            r.as.binary.left = mono_box(l); r.as.binary.right = mono_box(rr);
            break;
        }
        case TK_TEXPR_UNARY: {
            tk_texpr o;
            if (!mono_texpr(*e.as.unary.operand, s, prog, table, &o, insts, err)) return false;
            r.as.unary.operand = mono_box(o);
            break;
        }
        case TK_TEXPR_COMPARE: {
            tk_texpr f;
            if (!mono_texpr(*e.as.compare.first, s, prog, table, &f, insts, err)) return false;
            r.as.compare.first = mono_box(f);
            tk_tcmp_term *rest = e.as.compare.nrest ? tk_alloc(e.as.compare.nrest * sizeof *rest) : NULL;
            for (size_t i = 0; i < e.as.compare.nrest; i += 1) {
                tk_texpr o;
                if (!mono_texpr(*e.as.compare.rest[i].operand, s, prog, table, &o, insts, err)) return false;
                rest[i] = (tk_tcmp_term){ .op = e.as.compare.rest[i].op, .operand = mono_box(o) };
            }
            r.as.compare.rest = rest;
            break;
        }
        case TK_TEXPR_CALL: {
            tk_texpr *args = e.as.call.nargs ? tk_alloc(e.as.call.nargs * sizeof *args) : NULL;
            for (size_t i = 0; i < e.as.call.nargs; i += 1) {
                if (!mono_texpr(e.as.call.args[i], s, prog, table, &args[i], insts, err)) return false;
            }
            r.as.call.args = args;
            tk_path callee = e.as.call.callee;
            tk_str bare = callee.len ? callee.segments[callee.len - 1].name : (tk_str){ .ptr = (const tk_byte *)"", .len = 0 };
            const tk_tfunction *gf = find_generic_fn(prog, bare);
            if (gf) {
                tk_type_table ptable = tk_type_param_table(gf->type_params, gf->n_type_params, mono_cstr(""), table);
                tk_subst sub = { .params = gf->type_params, .n_params = gf->n_type_params, .names = NULL, .types = NULL, .n_bind = 0 };
                for (size_t i = 0; i < e.as.call.nargs; i += 1) {   // nargs == gf->nparams (arity checked at typing); bound by `args` so args[i] is in range
                    tk_type_result pat = tk_resolve_type(gf->params[i].type_ann, ptable);
                    if (!pat.ok) { *err = pat.as.error; return false; }
                    tk_subst_result u = tk_unify(pat.as.value, args[i].type, sub, table);
                    if (!u.ok) { *err = u.as.error; return false; }
                    sub = u.as.value;
                }
                tk_str mangled = mono_mangle_name(bare, gf->type_params, gf->n_type_params, sub);
                // rewrite the callee's last segment to the mangled name (keep the leading segments).
                tk_segment *segs = NULL; size_t ns = 0;
                for (size_t i = 0; i < callee.len; i += 1) {
                    tk_segment sg = (i == callee.len - 1) ? (tk_segment){ .name = mangled } : callee.segments[i];
                    tk_segs_push(&segs, &ns, sg);
                }
                r.as.call.callee = (tk_path){ .segments = segs, .len = ns };
                mono_list_push(insts, (tk_mono_inst){ .fn_name = bare, .mangled = mangled, .s = sub });
            }
            break;
        }
        case TK_TEXPR_IF: {
            tk_texpr c;
            if (!mono_texpr(*e.as.if_expr.cond, s, prog, table, &c, insts, err)) return false;
            r.as.if_expr.cond = mono_box(c);
            if (!mono_block(e.as.if_expr.then_blk, e.as.if_expr.nthen, s, prog, table, &r.as.if_expr.then_blk, &r.as.if_expr.nthen, insts, err)) return false;
            if (e.as.if_expr.has_else) {
                if (!mono_block(e.as.if_expr.else_blk, e.as.if_expr.nelse, s, prog, table, &r.as.if_expr.else_blk, &r.as.if_expr.nelse, insts, err)) return false;
            } else { r.as.if_expr.else_blk = NULL; r.as.if_expr.nelse = 0; }
            break;
        }
        case TK_TEXPR_MATCH: {
            tk_texpr sub;
            if (!mono_texpr(*e.as.match_expr.subject, s, prog, table, &sub, insts, err)) return false;
            r.as.match_expr.subject = mono_box(sub);
            tk_tarm *arms = e.as.match_expr.narms ? tk_alloc(e.as.match_expr.narms * sizeof *arms) : NULL;
            for (size_t i = 0; i < e.as.match_expr.narms; i += 1) {
                tk_tarm a = e.as.match_expr.arms[i];
                if (a.has_when) {
                    tk_texpr g;
                    if (!mono_texpr(*a.guard, s, prog, table, &g, insts, err)) return false;
                    a.guard = mono_box(g);
                }
                if (!mono_block(a.body, a.nbody, s, prog, table, &a.body, &a.nbody, insts, err)) return false;
                arms[i] = a;
            }
            r.as.match_expr.arms = arms;
            break;
        }
        case TK_TEXPR_CAST: {
            tk_texpr i;
            if (!mono_texpr(*e.as.cast.expr, s, prog, table, &i, insts, err)) return false;
            r.as.cast.expr = mono_box(i);
            break;
        }
        case TK_TEXPR_FIELD_ACCESS: {
            tk_texpr rc;
            if (!mono_texpr(*e.as.field_access.receiver, s, prog, table, &rc, insts, err)) return false;
            r.as.field_access.receiver = mono_box(rc);
            break;
        }
        case TK_TEXPR_SAFE_FIELD_ACCESS: {
            tk_texpr rc;
            if (!mono_texpr(*e.as.safe_field_access.receiver, s, prog, table, &rc, insts, err)) return false;
            r.as.safe_field_access.receiver = mono_box(rc);
            break;
        }
        case TK_TEXPR_COALESCE: {
            tk_texpr l, rr;
            if (!mono_texpr(*e.as.coalesce.left, s, prog, table, &l, insts, err)) return false;
            if (!mono_texpr(*e.as.coalesce.right, s, prog, table, &rr, insts, err)) return false;
            r.as.coalesce.left = mono_box(l); r.as.coalesce.right = mono_box(rr);
            break;
        }
        case TK_TEXPR_STRUCT_INIT: {
            tk_texpr *vals = e.as.struct_init.nfields ? tk_alloc(e.as.struct_init.nfields * sizeof *vals) : NULL;
            for (size_t i = 0; i < e.as.struct_init.nfields; i += 1)
                if (!mono_texpr(e.as.struct_init.field_vals[i], s, prog, table, &vals[i], insts, err)) return false;
            r.as.struct_init.field_vals = vals;
            break;
        }
        case TK_TEXPR_INDEX: {
            tk_texpr rc, ix;
            if (!mono_texpr(*e.as.index.receiver, s, prog, table, &rc, insts, err)) return false;
            if (!mono_texpr(*e.as.index.index, s, prog, table, &ix, insts, err)) return false;
            r.as.index.receiver = mono_box(rc); r.as.index.index = mono_box(ix);
            break;
        }
        case TK_TEXPR_INTERP: {
            tk_texpr *holes = e.as.interp.nholes ? tk_alloc(e.as.interp.nholes * sizeof *holes) : NULL;
            tk_tinterp_spec *specs = (e.as.interp.specs && e.as.interp.nholes)
                ? tk_alloc(e.as.interp.nholes * sizeof *specs) : NULL;
            for (size_t i = 0; i < e.as.interp.nholes; i += 1) {
                if (!mono_texpr(e.as.interp.holes[i], s, prog, table, &holes[i], insts, err)) return false;
                if (specs && e.as.interp.specs) {
                    specs[i] = e.as.interp.specs[i];  // kind + static_spec copied as-is
                    if (specs[i].kind == TK_FSPEC_DYNAMIC && specs[i].ndyn_args > 0) {
                        tk_texpr *da = tk_alloc(specs[i].ndyn_args * sizeof *da); if (!da) abort();
                        for (size_t k = 0; k < specs[i].ndyn_args; k++)
                            if (!mono_texpr(specs[i].dyn_args[k], s, prog, table, &da[k], insts, err)) { return false; }
                        specs[i].dyn_args = da;
                    }
                }
            }
            r.as.interp.holes = holes;
            r.as.interp.specs = specs;
            break;
        }
        case TK_TEXPR_IN: {
            tk_texpr l;
            if (!mono_texpr(*e.as.in_expr.lhs, s, prog, table, &l, insts, err)) return false;
            r.as.in_expr.lhs = mono_box(l);
            tk_texpr *elems = e.as.in_expr.nelems ? tk_alloc(e.as.in_expr.nelems * sizeof *elems) : NULL;
            for (size_t i = 0; i < e.as.in_expr.nelems; i += 1)
                if (!mono_texpr(e.as.in_expr.elems[i], s, prog, table, &elems[i], insts, err)) return false;
            r.as.in_expr.elems = elems;
            break;
        }
        case TK_TEXPR_ARRAY: {
            tk_texpr *elems = e.as.array.nelements ? tk_alloc(e.as.array.nelements * sizeof *elems) : NULL;
            for (size_t i = 0; i < e.as.array.nelements; i += 1)
                if (!mono_texpr(e.as.array.elements[i], s, prog, table, &elems[i], insts, err)) return false;
            r.as.array.elements = elems;
            // is_spread carries over unchanged (already an array of the right length).
            break;
        }
        case TK_TEXPR_LAMBDA: {   // (W10) substitute through param/capture/ret types + mono the body
            tk_tlambda lam = e.as.lambda;
            tk_tlambda_param *np = lam.nparams ? tk_alloc(lam.nparams * sizeof *np) : NULL;
            for (size_t i = 0; i < lam.nparams; i += 1) np[i] = (tk_tlambda_param){ lam.params[i].name, tk_subst_type(lam.params[i].type, s) };
            tk_tcapture *nc = lam.ncaptures ? tk_alloc(lam.ncaptures * sizeof *nc) : NULL;
            for (size_t i = 0; i < lam.ncaptures; i += 1) nc[i] = (tk_tcapture){ lam.captures[i].name, tk_subst_type(lam.captures[i].type, s), lam.captures[i].by_ref };
            tk_tstatement *nb = lam.nbody ? tk_alloc(lam.nbody * sizeof *nb) : NULL;
            for (size_t i = 0; i < lam.nbody; i += 1) if (!mono_tstmt(lam.body[i], s, prog, table, &nb[i], insts, err)) return false;
            r.as.lambda.params = np; r.as.lambda.captures = nc; r.as.lambda.body = nb; r.as.lambda.ret = tk_subst_type(lam.ret, s);
            break;
        }
    }
    *out = r;
    return true;
}

static bool mono_tstmt(tk_tstatement st, tk_subst s, tk_tprogram prog, tk_type_table table, tk_tstatement *out, tk_mono_list *insts, tk_error *err) {
    tk_tstatement r = st;
    switch (st.tag) {
        case TK_TSTMT_BINDING: {
            tk_texpr v;
            if (!mono_texpr(st.as.binding.value, s, prog, table, &v, insts, err)) return false;
            r.as.binding.value = v; r.as.binding.bound = tk_subst_type(st.as.binding.bound, s);
            break;
        }
        case TK_TSTMT_ASSIGN: {
            tk_texpr v;
            if (!mono_texpr(st.as.assign.value, s, prog, table, &v, insts, err)) return false;
            r.as.assign.value = v; r.as.assign.bound = tk_subst_type(st.as.assign.bound, s);
            break;
        }
        case TK_TSTMT_RETURN: {
            if (st.as.ret.has_value) {
                tk_texpr v;
                if (!mono_texpr(st.as.ret.value, s, prog, table, &v, insts, err)) return false;
                r.as.ret.value = v;
            }
            break;
        }
        case TK_TSTMT_LOOP:
            if (!mono_block(st.as.loop_stmt.body, st.as.loop_stmt.nbody, s, prog, table, &r.as.loop_stmt.body, &r.as.loop_stmt.nbody, insts, err)) return false;
            break;
        case TK_TSTMT_BREAK: case TK_TSTMT_CONTINUE:
            break;
        case TK_TSTMT_EXPR: {
            tk_texpr v;
            if (!mono_texpr(st.as.expr_stmt.expr, s, prog, table, &v, insts, err)) return false;
            r.as.expr_stmt.expr = v;
            break;
        }
        case TK_TSTMT_DEFER:
            if (!mono_block(st.as.defer_stmt.body, st.as.defer_stmt.nbody, s, prog, table, &r.as.defer_stmt.body, &r.as.defer_stmt.nbody, insts, err)) return false;
            break;
    }
    *out = r;
    return true;
}

static bool mono_block(tk_tstatement *stmts, size_t n, tk_subst s, tk_tprogram prog, tk_type_table table, tk_tstatement **out, size_t *nout, tk_mono_list *insts, tk_error *err) {
    tk_tstatement *o = n ? tk_alloc(n * sizeof *o) : NULL;
    for (size_t i = 0; i < n; i += 1) {
        if (!mono_tstmt(stmts[i], s, prog, table, &o[i], insts, err)) return false;
    }
    *out = o; *nout = n;
    return true;
}

// ── the driver ───────────────────────────────────────────────────────────────────────────
// does `kept` already hold a type-decl named `name`? (dedup for appended generic instances.)
static bool mono_kept_has_type(tk_titem *kept, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1)
        if (kept[i].tag == TK_TITEM_TYPE_DECL && mono_name_eq(kept[i].as.type_decl.name, name)) return true;
    return false;
}

tk_tprogram_result tk_monomorphize(tk_tprogram prog, tk_type_table table) {
    // NO-OP GUARD: no generic function → return UNCHANGED (byte-identical for the non-generic corpus).
    bool any_generic = false;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag == TK_TITEM_FUNCTION  && it.as.function.n_type_params > 0)  any_generic = true;
        if (it.tag == TK_TITEM_TYPE_DECL && it.as.type_decl.n_type_params > 0) any_generic = true;   // (S4) generic type-decl
    }
    if (!any_generic) return (tk_tprogram_result){ .ok = true, .as.value = prog };

    tk_subst empty_s = { .params = NULL, .n_params = 0, .names = NULL, .types = NULL, .n_bind = 0 };
    tk_error err = (tk_error){ 0 };

    // PHASE 1 — rewrite non-generic items; drop generic fns; collect seed insts.
    tk_titem *kept = NULL; size_t nkept = 0, capkept = 0;
    tk_mono_list queue = { 0 };
    #define KEEP(IT) do { if (nkept == capkept) { capkept = capkept ? capkept * 2 : 16; kept = tk_realloc0(kept, capkept * sizeof *kept); if (!kept) abort(); } kept[nkept++] = (IT); } while (0)

    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag == TK_TITEM_FUNCTION) {
            tk_tfunction f = it.as.function;
            if (f.n_type_params > 0) {
                // drop the generic template (stamped copies replace it)
            } else {
                tk_tstatement *body; size_t nbody;
                if (!mono_block(f.body, f.nbody, empty_s, prog, table, &body, &nbody, &queue, &err))
                    return (tk_tprogram_result){ .ok = false, .as.error = err };
                f.body = body; f.nbody = nbody;
                KEEP(((tk_titem){ .tag = TK_TITEM_FUNCTION, .as.function = f }));
            }
        } else if (it.tag == TK_TITEM_STATEMENT) {
            tk_tstatement st;
            if (!mono_tstmt(it.as.statement, empty_s, prog, table, &st, &queue, &err))
                return (tk_tprogram_result){ .ok = false, .as.error = err };
            KEEP(((tk_titem){ .tag = TK_TITEM_STATEMENT, .as.statement = st }));
        } else if (it.tag == TK_TITEM_TYPE_DECL) {
            // (S4) DROP a generic type-decl TEMPLATE; its concrete instances are appended below.
            if (it.as.type_decl.n_type_params == 0) KEEP(it);
        } else {
            KEEP(it);   // UseDecl pass through
        }
    }

    // PHASE 2 — fixpoint stamping. The queue grows as a stamped body discovers further insts;
    // `stamped` (mangled-name ledger) dedups. The ceiling bounds polymorphic recursion (M.1).
    const size_t CEILING = 5000;
    tk_mono_list stamped = { 0 };
    for (size_t qi = 0; qi < queue.len; qi += 1) {
        tk_mono_inst inst = queue.ptr[qi];
        if (mono_seen(&stamped, inst.mangled)) continue;
        if (stamped.len >= CEILING)
            return (tk_tprogram_result){ .ok = false, .as.error = tk_error_make("monomorphization exceeded the instantiation ceiling (5000) — likely unbounded polymorphic recursion") };
        mono_list_push(&stamped, inst);
        const tk_tfunction *gf = find_generic_fn(prog, inst.fn_name);
        if (!gf) return (tk_tprogram_result){ .ok = false, .as.error = tk_error_named("internal: monomorph could not find generic fn", inst.fn_name) };
        // STAMP: concrete param annotations, substituted return type, body rewritten through the Subst.
        tk_param *nparams = gf->nparams ? tk_alloc(gf->nparams * sizeof *nparams) : NULL;
        for (size_t pi = 0; pi < gf->nparams; pi += 1)
            nparams[pi] = (tk_param){ .name = gf->params[pi].name, .type_ann = subst_typeexpr(gf->params[pi].type_ann, inst.s), .is_params = gf->params[pi].is_params };
        tk_tstatement *body; size_t nbody;
        if (!mono_block(gf->body, gf->nbody, inst.s, prog, table, &body, &nbody, &queue, &err))
            return (tk_tprogram_result){ .ok = false, .as.error = err };
        tk_tfunction sf = *gf;
        sf.name = inst.mangled;
        sf.type_params = NULL; sf.n_type_params = 0;
        sf.params = nparams;   // nparams count unchanged
        sf.return_type = tk_subst_type(gf->return_type, inst.s);
        sf.body = body; sf.nbody = nbody;
        sf.is_test = false; sf.is_extern = false;
        sf.c_symbol = (tk_str){ .ptr = (const tk_byte *)"", .len = 0 };
        sf.from_lib = (tk_str){ .ptr = (const tk_byte *)"", .len = 0 };
        KEEP(((tk_titem){ .tag = TK_TITEM_FUNCTION, .as.function = sf }));
    }

    (void)mono_box_type;   // (reserved helper; keeps the parallel with the .tks type boxing)
    // (S4 type-generics) APPEND concrete type-decl INSTANCES stamped by the instantiation pass (the
    // `__g__`-marked table entries), so codegen emits the concrete struct/variant. Dedup vs kept.
    tk_type_decl *insts2 = NULL; size_t ninsts2 = 0;
    tk_table_generic_instances(table, &insts2, &ninsts2);
    for (size_t ii = 0; ii < ninsts2; ii += 1) {
        if (mono_kept_has_type(kept, nkept, insts2[ii].name)) continue;
        KEEP(((tk_titem){ .tag = TK_TITEM_TYPE_DECL, .as.type_decl = insts2[ii] }));
    }
    #undef KEEP
    return (tk_tprogram_result){ .ok = true, .as.value = { .items = kept, .nitems = nkept } };
}
