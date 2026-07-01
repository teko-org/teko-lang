// src/emit/header.c — E-emit-a, the `.tkh` HEADER-BUILDING DRIVER. Mirrors header.tks.
// Walk a CHECKED typed program, keep only the EXPORTED (`vis == exp`) items, build their
// export signatures, assemble a tk_header, then (emit_program) emit it via tk_emit_tkh.
// Reuses tk_resolve_type; adds no codec primitive (M.5).
#include "header.h"
#include <stdlib.h>

// growable lists for the collected exports (teko::list realized — file-local stamps).
TK_LIST(tk_sigparam, tk_sigparam_list)
TK_LIST(tk_sigfield, tk_sigfield_list)
TK_LIST(tk_fnsig,    tk_fnsig_list)
TK_LIST(tk_tyexport, tk_tyexport_list)

// --- a function's export signature: each param's annotation resolved (M.3 — exact) ---
static tk_header_result hdr_err(const char *m) { return (tk_header_result){ .ok = false, .as.error = tk_error_make(m) }; }

typedef struct { bool ok; union { tk_fnsig value; tk_error error; } as; } fnsig_result;

static fnsig_result build_fnsig(tk_tfunction f, tk_type_table table) {
    tk_sigparam_list params = tk_sigparam_list_empty();
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, table);
        if (!pt.ok) return (fnsig_result){ .ok = false, .as.error = pt.as.error };
        params = tk_sigparam_list_push(params, (tk_sigparam){ .name = f.params[i].name, .type = pt.as.value });
    }
    // ret is already a resolved Type on the TFunction (return_type); docs are carried.
    tk_fnsig sig = { .name = f.name, .params = params.ptr, .nparams = params.len,
                     .ret = f.return_type, .has_doc = f.has_doc, .doc = f.doc };
    return (fnsig_result){ .ok = true, .as.value = sig };
}

// --- a type's export interface, by shape (B.14). Per shape, exactly one list is meaningful. ---
typedef struct { bool ok; union { tk_tyexport value; tk_error error; } as; } tyexport_result;

static tyexport_result build_tyexport(tk_type_decl d, tk_type_table table) {
    switch (d.body.tag) {
        case TK_BODY_STRUCT: {
            tk_struct_body sb = d.body.as.struct_body;
            tk_sigfield_list fields = tk_sigfield_list_empty();
            for (size_t i = 0; i < sb.n_fields; i += 1) {
                tk_type_result ft = tk_resolve_type(sb.fields[i].type_ann, table);
                if (!ft.ok) return (tyexport_result){ .ok = false, .as.error = ft.as.error };
                fields = tk_sigfield_list_push(fields, (tk_sigfield){ .name = sb.fields[i].name, .type = ft.as.value });
            }
            tk_tyexport e = { .name = d.name, .shape = TK_TY_STRUCT,
                              .fields = fields.ptr, .nfields = fields.len,
                              .members = NULL, .nmembers = 0, .cases = NULL, .ncases = 0,
                              .has_doc = d.has_doc, .doc = d.doc };
            return (tyexport_result){ .ok = true, .as.value = e };
        }
        case TK_BODY_ENUM: {
            tk_enum_body eb = d.body.as.enum_body;
            tk_tyexport e = { .name = d.name, .shape = TK_TY_ENUM,
                              .fields = NULL, .nfields = 0,
                              .members = eb.members, .nmembers = eb.n_members,
                              .cases = NULL, .ncases = 0,
                              .has_doc = d.has_doc, .doc = d.doc };
            return (tyexport_result){ .ok = true, .as.value = e };
        }
        case TK_BODY_VARIANT: {
            // resolve the union's syntactic type_expr → a semantic Variant; its members are the cases.
            tk_type_result rt = tk_resolve_type(d.body.as.variant_body.type_expr, table);
            if (!rt.ok) return (tyexport_result){ .ok = false, .as.error = rt.as.error };
            if (rt.as.value.tag != TK_TYPE_VARIANT)
                return (tyexport_result){ .ok = false, .as.error = tk_error_make("an exported variant's body did not resolve to a union") };
            tk_tyexport e = { .name = d.name, .shape = TK_TY_VARIANT,
                              .fields = NULL, .nfields = 0, .members = NULL, .nmembers = 0,
                              .cases = rt.as.value.as.variant.members, .ncases = rt.as.value.as.variant.len,
                              .has_doc = d.has_doc, .doc = d.doc };
            return (tyexport_result){ .ok = true, .as.value = e };
        }
        case TK_BODY_ALIAS:
            // Exporting a TRANSPARENT alias in the `.tkh` is a LATER gap (the header has no
            // alias shape yet). An honest stop — never reached by the pub-only validation path.
            return (tyexport_result){ .ok = false, .as.error = tk_error_make("exporting a type alias in the header is not yet supported") };
        case TK_BODY_EXTERN:
            // (C7.1a) exporting an `extern type` opaque handle in the `.tkh` is a LATER gap. Honest stop.
            return (tyexport_result){ .ok = false, .as.error = tk_error_make("exporting an extern type in the header is not yet supported") };
        case TK_BODY_FLAGS:
            // (C8.4) exporting a `flags` type in the `.tkh` is deferred to C8.4. Honest stop.
            return (tyexport_result){ .ok = false, .as.error = tk_error_make("exporting a flags type in the header is not yet supported") };
        case TK_BODY_CLASS: {
            // (W10b.CLASS increment 1) same field-export shape as a struct — reuses TK_TY_STRUCT
            // since the C layout is identical too (no inheritance/statics surfaced yet).
            tk_class_body cb = d.body.as.class_body;
            tk_sigfield_list fields = tk_sigfield_list_empty();
            for (size_t i = 0; i < cb.n_fields; i += 1) {
                tk_type_result ft = tk_resolve_type(cb.fields[i].type_ann, table);
                if (!ft.ok) return (tyexport_result){ .ok = false, .as.error = ft.as.error };
                fields = tk_sigfield_list_push(fields, (tk_sigfield){ .name = cb.fields[i].name, .type = ft.as.value });
            }
            tk_tyexport e = { .name = d.name, .shape = TK_TY_STRUCT,
                              .fields = fields.ptr, .nfields = fields.len,
                              .members = NULL, .nmembers = 0, .cases = NULL, .ncases = 0,
                              .has_doc = d.has_doc, .doc = d.doc };
            return (tyexport_result){ .ok = true, .as.value = e };
        }
    }
    return (tyexport_result){ .ok = false, .as.error = tk_error_make("unknown type body shape") };
}

// --- THE DRIVER: walk the typed program, keep only `exp` items, build the Header ---
tk_header_result tk_build_header(tk_tprogram prog, tk_type_table table) {
    tk_tyexport_list types = tk_tyexport_list_empty();
    tk_fnsig_list    fns   = tk_fnsig_list_empty();
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        switch (it.tag) {
            case TK_TITEM_FUNCTION:
                if (it.as.function.vis == TK_VIS_EXP) {   // only `exp` reaches the .tkh (pub is project-internal)
                    fnsig_result s = build_fnsig(it.as.function, table);
                    if (!s.ok) return hdr_err(s.as.error.message);
                    fns = tk_fnsig_list_push(fns, s.as.value);
                }
                break;
            case TK_TITEM_TYPE_DECL:
                if (it.as.type_decl.vis == TK_VIS_EXP) {   // only `exp` reaches the .tkh
                    tyexport_result t = build_tyexport(it.as.type_decl, table);
                    if (!t.ok) return hdr_err(t.as.error.message);
                    types = tk_tyexport_list_push(types, t.as.value);
                }
                break;
            case TK_TITEM_USE:       break;   // not part of the exported surface — skip
            case TK_TITEM_STATEMENT: break;   // a loose top-level statement is never exported — skip
        }
    }
    tk_header h = { .types = types.ptr, .ntypes = types.len, .fns = fns.ptr, .nfns = fns.len };
    return (tk_header_result){ .ok = true, .as.value = h };
}

// --- the check → header → `.tkh` emission path: build the Header, then emit it (M.4) ---
// A header is ALWAYS emitted, even with NO exports: an empty surface yields an empty Header
// (0 types, 0 fns) and a well-formed `.tkh` that honestly states "nothing is exported" (M.3).
tk_bytes_result tk_emit_program(tk_tprogram prog, tk_type_table table) {
    tk_header_result h = tk_build_header(prog, table);
    if (!h.ok) return (tk_bytes_result){ .ok = false, .as.error = h.as.error };
    return (tk_bytes_result){ .ok = true, .as.value = tk_emit_tkh(&h.as.value) };
}
