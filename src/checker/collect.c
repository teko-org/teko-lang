// src/checker/collect.c
#include "collect.h"
#include "tast.h"   // tk_tprogram, tk_titem, TK_TITEM_TYPE_DECL (C7.12)
#include <string.h> // memcpy (collect_str_concat)
#include <stdio.h>  // snprintf — (W10b.IF) conformance error messages

// a ++ b — a fresh owned tk_str (local to keep the DAG tight; mirrors monomorph.c::mono_concat).
static tk_str collect_str_concat(tk_str a, tk_str b) {
    size_t len = a.len + b.len;
    tk_byte *buf = tk_alloc(len ? len : 1); if (!buf) abort();
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ .ptr = buf, .len = len };
}

static tk_type_table collect_types(tk_item *items, size_t n) {
    tk_type_table table = tk_type_table_empty();
    for (size_t i = 0; i < n; i += 1) {
        if (items[i].tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl td = items[i].as.type_decl;
            // tag the reg with the declaring namespace (A3 provenance on the item) + the
            // decl's visibility — the inputs to W-vis-enforce's module-system pass.
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = items[i].namespace, .vis = td.vis, .decl = td });
        }
    }
    return table;
}

static tk_type_result func_type(tk_function f, tk_type_table table) {
    tk_type_table tbl = tk_type_param_table(f.type_params, f.n_type_params, (tk_str){0}, table);   // (S4) opaque type-params in scope
    tk_type *params = NULL; size_t n = 0;
    tk_str *param_names = NULL;   // DEFARGS (2026-07-01)
    tk_expr *defaults = NULL; size_t ndefaults = 0;
    size_t n_required = f.nparams;   // the first has_default param's index; TRAILING-ONLY (rule A) means every param after it also defaults
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, tbl);
        if (!pt.ok) { tk_free0(params); return pt; }
        params = tk_realloc0(params, (n + 1) * sizeof *params);
        if (!params) abort();
        params[n] = pt.as.value; n += 1;
        param_names = tk_realloc0(param_names, n * sizeof *param_names);
        if (!param_names) abort();
        param_names[n - 1] = f.params[i].name;
        if (f.params[i].has_default) {
            if (n_required == f.nparams) { n_required = i; }
            defaults = tk_realloc0(defaults, (ndefaults + 1) * sizeof *defaults);
            if (!defaults) abort();
            defaults[ndefaults] = f.params[i].default_expr; ndefaults += 1;
        }
    }
    // No `-> ret` ⇒ a void return (M.3) — f.return_type is the empty `no_type()` placeholder
    // (a NAMED type-expr with an empty path), which must NOT be resolved (it is not a value type).
    // Mirrors typer.c's function typer (`if (!f.has_return) return void_t()`).
    tk_type_result ret = f.has_return
        ? tk_resolve_type(f.return_type, tbl)
        : (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_VOID } };
    if (!ret.ok) { tk_free0(params); return ret; }
    tk_type *rp = tk_alloc(sizeof *rp); if (!rp) abort(); *rp = ret.as.value;
    bool is_variadic = f.nparams > 0 && f.params[f.nparams - 1].is_params;
    tk_type t = { .tag = TK_TYPE_FUNC, .as.func = { params, n, rp, is_variadic, param_names, n_required, defaults, ndefaults } };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// (OOP A1, 2026-07-01) a struct METHOD's signature as a FuncType. Mirrors func_type, except the
// RECEIVER param (has_type=false, only ever index 0) resolves to Named{struct_name} — its type
// is IMPLICIT (the enclosing struct), never written by the user, so it can't go through
// tk_resolve_type like an ordinary annotated param.
static tk_type_result method_func_type(tk_function f, tk_str struct_name, tk_type_table table) {
    tk_type_table tbl = tk_type_param_table(f.type_params, f.n_type_params, (tk_str){0}, table);
    tk_type *params = NULL; size_t n = 0;
    tk_str *param_names = NULL;
    tk_expr *defaults = NULL; size_t ndefaults = 0;
    size_t n_required = f.nparams;
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type pt;
        if (!f.params[i].has_type) {
            pt = (tk_type){ .tag = TK_TYPE_NAMED, .as.named = { struct_name } };
        } else {
            tk_type_result ptr = tk_resolve_type(f.params[i].type_ann, tbl);
            if (!ptr.ok) { tk_free0(params); return ptr; }
            pt = ptr.as.value;
        }
        params = tk_realloc0(params, (n + 1) * sizeof *params);
        if (!params) abort();
        params[n] = pt; n += 1;
        param_names = tk_realloc0(param_names, n * sizeof *param_names);
        if (!param_names) abort();
        param_names[n - 1] = f.params[i].name;
        if (f.params[i].has_default) {
            if (n_required == f.nparams) { n_required = i; }
            defaults = tk_realloc0(defaults, (ndefaults + 1) * sizeof *defaults);
            if (!defaults) abort();
            defaults[ndefaults] = f.params[i].default_expr; ndefaults += 1;
        }
    }
    tk_type_result ret = f.has_return
        ? tk_resolve_type(f.return_type, tbl)
        : (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_VOID } };
    if (!ret.ok) { tk_free0(params); return ret; }
    tk_type *rp = tk_alloc(sizeof *rp); if (!rp) abort(); *rp = ret.as.value;
    bool is_variadic = f.nparams > 0 && f.params[f.nparams - 1].is_params;
    tk_type t = { .tag = TK_TYPE_FUNC, .as.func = { params, n, rp, is_variadic, param_names, n_required, defaults, ndefaults } };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// C7.12 — reconstruct a TypeTable from a typed program's pass-through TypeDecl items.
// Used by the package backend in driver.c. The namespace is set to "" — sufficient for
// resolve_type; W-vis-enforce is not invoked by the header emitter.
tk_type_table tk_type_table_of(tk_tprogram prog) {
    tk_type_table table = tk_type_table_empty();
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag == TK_TITEM_TYPE_DECL) {
            tk_type_decl td = prog.items[i].as.type_decl;
            tk_str empty = { .ptr = (const unsigned char *)"", .len = 0 };
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = empty, .vis = td.vis, .decl = td });
        }
    }
    return table;
}

// (W10b.CLASS increment 2) look up a class's own tk_class_body by name (mirror of
// collect.tks::find_class_body). ok=false carries the error.
tk_classbody_result tk_find_class_body(tk_str name, tk_type_table table) {
    tk_decl_result decl = tk_type_table_find(table, name);
    if (!decl.ok) return (tk_classbody_result){ .ok = false, .as.error = tk_error_named("unknown base class", name) };
    if (decl.as.value.body.tag != TK_BODY_CLASS) return (tk_classbody_result){ .ok = false, .as.error = tk_error_named("is not a class", name) };
    return (tk_classbody_result){ .ok = true, .as.value = decl.as.value.body.as.class_body };
}

// GUARD cyclic class inheritance BEFORE any recursive base-chain walk (tk_effective_class_fields/
// _methods/validate_class_decl) would recurse forever and crash (stack overflow). Walk `name`'s base
// chain with a visited-set of the root→here PATH; a class name reappearing is a cycle, returned as
// an honest error. Uses only the NON-recursive tk_find_class_body lookup, so it terminates even on a
// cycle. A base that is unknown/not-a-class STOPS the walk (validate_class_decl surfaces that error).
// Mirror of collect.tks::class_inheritance_acyclic.
static tk_type_result class_inheritance_acyclic(tk_str name, tk_type_table table) {
    tk_str *seen = tk_alloc(sizeof *seen); if (!seen) abort();
    seen[0] = name; size_t n_seen = 1;
    tk_str cur = name;
    for (;;) {
        tk_classbody_result cb = tk_find_class_body(cur, table);
        if (!cb.ok) break;
        if (!cb.as.value.has_base) break;
        tk_str b = cb.as.value.base_name;
        for (size_t i = 0; i < n_seen; i += 1) {
            if (tk_str_eq(seen[i], b)) {
                size_t len = b.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "cyclic class inheritance: '%.*s' inherits from itself (directly or transitively)", (int)b.len, (const char *)b.ptr);
                return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
        tk_str *seen2 = tk_alloc((n_seen + 1) * sizeof *seen2); if (!seen2) abort();   // seen ++ [b]
        for (size_t i = 0; i < n_seen; i += 1) seen2[i] = seen[i];
        seen2[n_seen] = b;
        seen = seen2; n_seen += 1;
        cur = b;
    }
    return (tk_type_result){ .ok = true };
}

// (W10b.CLASS increment 2) a class's EFFECTIVE fields: the base's effective fields (recursively)
// followed by its own. Mirror of collect.tks::effective_class_fields.
tk_fieldsvec_result tk_effective_class_fields(tk_class_body cb, tk_type_table table) {
    tk_field *base = NULL; size_t n_base = 0;
    if (cb.has_base) {
        tk_classbody_result base_cb = tk_find_class_body(cb.base_name, table);
        if (!base_cb.ok) return (tk_fieldsvec_result){ .ok = false, .as.error = base_cb.as.error };
        tk_fieldsvec_result r = tk_effective_class_fields(base_cb.as.value, table);
        if (!r.ok) return r;
        base = r.as.value.ptr; n_base = r.as.value.len;
    }
    tk_field *out = NULL; size_t n_out = 0;
    for (size_t i = 0; i < n_base; i += 1) tk_fields_push(&out, &n_out, base[i]);
    for (size_t i = 0; i < cb.n_fields; i += 1) tk_fields_push(&out, &n_out, cb.fields[i]);
    return (tk_fieldsvec_result){ .ok = true, .as.value = { .ptr = out, .len = n_out } };
}

// (W10b.CLASS increment 2) a class's EFFECTIVE methods: the base's effective methods
// (recursively), each REPLACED by an `override` of the same name if this class declares one,
// plus this class's own NEW methods appended. Mirror of collect.tks::effective_class_methods.
tk_methodsvec_result tk_effective_class_methods(tk_class_body cb, tk_type_table table) {
    tk_function *base = NULL; size_t n_base = 0;
    if (cb.has_base) {
        tk_classbody_result base_cb = tk_find_class_body(cb.base_name, table);
        if (!base_cb.ok) return (tk_methodsvec_result){ .ok = false, .as.error = base_cb.as.error };
        tk_methodsvec_result r = tk_effective_class_methods(base_cb.as.value, table);
        if (!r.ok) return r;
        base = r.as.value.ptr; n_base = r.as.value.len;
    }
    tk_function *out = NULL; size_t n_out = 0;
    for (size_t i = 0; i < n_base; i += 1) {
        tk_function m = base[i];
        for (size_t j = 0; j < cb.n_methods; j += 1) {
            if (tk_str_eq(cb.methods[j].name, m.name)) { m = cb.methods[j]; break; }
        }
        tk_functions_push(&out, &n_out, m);
    }
    for (size_t k = 0; k < cb.n_methods; k += 1) {
        bool is_new = true;
        for (size_t bi = 0; bi < n_base; bi += 1) {
            if (tk_str_eq(base[bi].name, cb.methods[k].name)) { is_new = false; break; }
        }
        if (is_new) tk_functions_push(&out, &n_out, cb.methods[k]);
    }
    return (tk_methodsvec_result){ .ok = true, .as.value = { .ptr = out, .len = n_out } };
}

// (W10b.CLASS residual — intern visibility) is `class_name` the same as, or a (transitive)
// SUBCLASS of, `ancestor_name`? Mirror of collect.tks::is_subclass_of.
bool tk_is_subclass_of(tk_str class_name, tk_str ancestor_name, tk_type_table table) {
    if (tk_str_eq(class_name, ancestor_name)) return true;
    tk_classbody_result cb = tk_find_class_body(class_name, table);
    if (!cb.ok) return false;
    if (!cb.as.value.has_base) return false;
    return tk_is_subclass_of(cb.as.value.base_name, ancestor_name, table);
}

// (W10b.CLASS residual — intern visibility) find field/method declaring class + reach, walking
// `class_name`'s own members first, then its base chain. Mirror of
// collect.tks::find_field_owner / find_method_owner.
tk_member_owner_result tk_find_field_owner(tk_str class_name, tk_type_table table, tk_str field_name) {
    tk_classbody_result cb = tk_find_class_body(class_name, table);
    if (!cb.ok) return (tk_member_owner_result){ .ok = false, .as.error = cb.as.error };
    for (size_t i = 0; i < cb.as.value.n_fields; i += 1) {
        if (tk_str_eq(cb.as.value.fields[i].name, field_name)) {
            tk_member_owner mo = { .declaring_class = class_name, .vis = cb.as.value.fields[i].vis, .is_intern = cb.as.value.fields[i].is_intern };
            return (tk_member_owner_result){ .ok = true, .as.value = mo };
        }
    }
    if (cb.as.value.has_base) return tk_find_field_owner(cb.as.value.base_name, table, field_name);
    return (tk_member_owner_result){ .ok = false, .as.error = tk_error_named("internal: field not found in class chain", field_name) };
}
tk_member_owner_result tk_find_method_owner(tk_str class_name, tk_type_table table, tk_str method_name) {
    tk_classbody_result cb = tk_find_class_body(class_name, table);
    if (!cb.ok) return (tk_member_owner_result){ .ok = false, .as.error = cb.as.error };
    for (size_t i = 0; i < cb.as.value.n_methods; i += 1) {
        if (tk_str_eq(cb.as.value.methods[i].name, method_name)) {
            tk_member_owner mo = { .declaring_class = class_name, .vis = cb.as.value.methods[i].vis, .is_intern = cb.as.value.methods[i].is_intern };
            return (tk_member_owner_result){ .ok = true, .as.value = mo };
        }
    }
    if (cb.as.value.has_base) return tk_find_method_owner(cb.as.value.base_name, table, method_name);
    return (tk_member_owner_result){ .ok = false, .as.error = tk_error_named("internal: method not found in class chain", method_name) };
}

// (W10b.CLASS residual — intern visibility) is `owner`'s member reachable from code whose OWN
// declaring class is `accessor_type` (empty — not inside any class method)? Mirror of
// collect.tks::member_accessible.
bool tk_member_accessible(tk_member_owner owner, tk_str accessor_type, tk_type_table table) {
    if (owner.vis == TK_VIS_PUB || owner.vis == TK_VIS_EXP) return true;
    // TK_VIS_PRIVATE (the class default)
    if (accessor_type.len == 0) return false;
    if (tk_str_eq(accessor_type, owner.declaring_class)) return true;
    if (owner.is_intern) return tk_is_subclass_of(accessor_type, owner.declaring_class, table);
    return false;
}

// (W10b.CLASS residual — intern visibility) recover the bare CLASS NAME from a method's
// registered pseudo-namespace ("<owning-ns>::<ClassName>", or bare "<ClassName>" at the top
// level — collect.c's method_ns convention) — the class name is always the LAST "::"-segment.
// Mirror of collect.tks::class_name_from_method_ns.
tk_str tk_class_name_from_method_ns(tk_str ns) {
    size_t cut = 0;
    for (size_t i = 0; i + 1 < ns.len; i += 1)
        if (ns.ptr[i] == ':' && ns.ptr[i + 1] == ':') cut = i + 2;
    return (tk_str){ ns.ptr + cut, ns.len - cut };
}

// (W10b.CLASS increment 2) validate ONE class's inheritance/override declarations. Mirror of
// collect.tks::validate_class_decl.
static tk_type_result validate_class_decl(tk_class_body cb, tk_type_table table) {
    if (cb.has_base) {
        tk_decl_result base_decl = tk_type_table_find(table, cb.base_name);
        if (!base_decl.ok) return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown base class", cb.base_name) };
        if (base_decl.as.value.body.tag != TK_BODY_CLASS) return (tk_type_result){ .ok = false, .as.error = tk_error_named("is not a class", cb.base_name) };
        if (base_decl.as.value.body.as.class_body.kind == TK_CLASS_SEALED)
            return (tk_type_result){ .ok = false, .as.error = tk_error_named("cannot inherit from a sealed class (only abstract/virtual classes can be a base)", cb.base_name) };
    }
    tk_function *base_methods = NULL; size_t n_base_methods = 0;
    if (cb.has_base) {
        tk_classbody_result base_cb = tk_find_class_body(cb.base_name, table);
        if (!base_cb.ok) return (tk_type_result){ .ok = false, .as.error = base_cb.as.error };
        tk_methodsvec_result r = tk_effective_class_methods(base_cb.as.value, table);
        if (!r.ok) return (tk_type_result){ .ok = false, .as.error = r.as.error };
        base_methods = r.as.value.ptr; n_base_methods = r.as.value.len;
    }
    for (size_t i = 0; i < cb.n_methods; i += 1) {
        tk_function m = cb.methods[i];
        if (!m.is_override) continue;
        bool found = false; tk_function bm = {0};
        for (size_t j = 0; j < n_base_methods; j += 1) {
            if (tk_str_eq(base_methods[j].name, m.name)) { found = true; bm = base_methods[j]; break; }
        }
        if (!found) return (tk_type_result){ .ok = false, .as.error = tk_error_named("has no matching base method to override", m.name) };
        bool overridable = bm.is_abstract || bm.is_virtual || bm.is_override;
        if (!overridable) return (tk_type_result){ .ok = false, .as.error = tk_error_named("overrides a base method that is not abstract/virtual", m.name) };
        if (bm.nparams != m.nparams) return (tk_type_result){ .ok = false, .as.error = tk_error_named("override must have the same parameter count as the base method", m.name) };
    }
    return (tk_type_result){ .ok = true };
}

// (W10b.IF) look up an interface body by name — error if the name is unknown or not an interface.
// Mirror of collect.tks::find_interface_body. LOCAL result type (used only here + conformance).
typedef struct { bool ok; union { tk_interface_body value; tk_error error; } as; } ifacebody_result;
static ifacebody_result find_interface_body(tk_str name, tk_type_table table) {
    tk_decl_result decl = tk_type_table_find(table, name);
    if (!decl.ok) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "unknown interface: %.*s", (int)name.len, (const char *)name.ptr);
        return (ifacebody_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    if (decl.as.value.body.tag != TK_BODY_INTERFACE) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "%.*s is not an interface", (int)name.len, (const char *)name.ptr);
        return (ifacebody_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    return (ifacebody_result){ .ok = true, .as.value = decl.as.value.body.as.interface_body };
}

// (W10b.IF) an interface's EFFECTIVE methods: (transitively) every extended interface's methods,
// then its own. Each `extends` name must itself be an interface (find_interface_body enforces it).
// `seen` = the interface names on the current root→here PATH — a name reappearing on the path is a
// CYCLE, rejected as an honest error instead of recursing forever (a diamond via two DIFFERENT
// branches is fine — `seen` tracks only the current path). Mirror of
// collect.tks::effective_interface_methods.
static tk_methodsvec_result effective_interface_methods(tk_interface_body ib, tk_type_table table, const tk_str *seen, size_t n_seen) {
    tk_function *out = NULL; size_t n_out = 0;
    for (size_t ei = 0; ei < ib.n_extends; ei += 1) {
        tk_str ename = ib.extends[ei];
        for (size_t si = 0; si < n_seen; si += 1) {
            if (tk_str_eq(seen[si], ename)) {
                size_t len = ename.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "cyclic interface `extends`: '%.*s' extends itself (directly or transitively)", (int)ename.len, (const char *)ename.ptr);
                return (tk_methodsvec_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
        ifacebody_result base_ib = find_interface_body(ename, table);
        if (!base_ib.ok) return (tk_methodsvec_result){ .ok = false, .as.error = base_ib.as.error };
        tk_str *seen2 = tk_alloc((n_seen + 1) * sizeof *seen2); if (!seen2) abort();   // seen ++ [ename]
        for (size_t i = 0; i < n_seen; i += 1) seen2[i] = seen[i];
        seen2[n_seen] = ename;
        tk_methodsvec_result bm = effective_interface_methods(base_ib.as.value, table, seen2, n_seen + 1);
        if (!bm.ok) return bm;
        for (size_t bi = 0; bi < bm.as.value.len; bi += 1) tk_functions_push(&out, &n_out, bm.as.value.ptr[bi]);
    }
    for (size_t mi = 0; mi < ib.n_methods; mi += 1) tk_functions_push(&out, &n_out, ib.methods[mi]);
    return (tk_methodsvec_result){ .ok = true, .as.value = { .ptr = out, .len = n_out } };
}

// (W10b.IF) does implementing method `impl_m` satisfy interface-required method `req`? Same NON-
// receiver param types (both skip their 1st untyped receiver param) + same return type. Any type
// that fails to resolve makes it a non-match. Mirror of collect.tks::method_sig_matches.
static bool method_sig_matches(tk_function req, tk_function impl_m, tk_type_table table) {
    size_t rstart = (req.nparams > 0 && !req.params[0].has_type) ? 1 : 0;
    size_t istart = (impl_m.nparams > 0 && !impl_m.params[0].has_type) ? 1 : 0;
    // instance/static parity: an INSTANCE interface method (has a receiver) must be satisfied by an
    // INSTANCE method, not a same-named STATIC one (rstart/istart are 1 iff a receiver is present).
    if (rstart != istart) return false;
    if (req.nparams - rstart != impl_m.nparams - istart) return false;
    for (size_t k = 0; rstart + k < req.nparams; k += 1) {
        tk_type_result rt = tk_resolve_type(req.params[rstart + k].type_ann, table);
        if (!rt.ok) return false;
        tk_type_result it = tk_resolve_type(impl_m.params[istart + k].type_ann, table);
        if (!it.ok) return false;
        if (!tk_type_eq(&rt.as.value, &it.as.value)) return false;
    }
    if (req.has_return != impl_m.has_return) return false;
    if (req.has_return) {
        tk_type_result rr = tk_resolve_type(req.return_type, table);
        if (!rr.ok) return false;
        tk_type_result ir = tk_resolve_type(impl_m.return_type, table);
        if (!ir.ok) return false;
        if (!tk_type_eq(&rr.as.value, &ir.as.value)) return false;
    }
    return true;
}

// (W10b.IF) THE CONFORMANCE CHECK. `members_all_public` = true for a STRUCT (members all-public,
// so the AST `vis` is irrelevant); false for a CLASS (a class method must genuinely be `pub`/`exp`
// to satisfy the public interface contract). Mirror of collect.tks::check_conformance.
static tk_type_result check_conformance(tk_str type_name, const tk_function *own_methods, size_t n_own,
                                        const tk_str *implements, size_t n_impl, tk_type_table table, bool members_all_public) {
    for (size_t ii = 0; ii < n_impl; ii += 1) {
        tk_str iname = implements[ii];
        ifacebody_result ib = find_interface_body(iname, table);
        if (!ib.ok) return (tk_type_result){ .ok = false, .as.error = ib.as.error };
        tk_methodsvec_result req_methods = effective_interface_methods(ib.as.value, table, &iname, 1);   // seed cycle-path with the interface's own name
        if (!req_methods.ok) return (tk_type_result){ .ok = false, .as.error = req_methods.as.error };
        for (size_t ri = 0; ri < req_methods.as.value.len; ri += 1) {
            tk_function req = req_methods.as.value.ptr[ri];
            bool found = false;
            for (size_t mi = 0; mi < n_own; mi += 1) {
                if (tk_str_eq(own_methods[mi].name, req.name)) {
                    if (!method_sig_matches(req, own_methods[mi], table)) {
                        size_t len = type_name.len + req.name.len + iname.len + 80; char *buf = tk_alloc(len); if (!buf) abort();
                        snprintf(buf, len, "'%.*s' method '%.*s' does not match interface '%.*s's signature",
                                 (int)type_name.len, (const char *)type_name.ptr, (int)req.name.len, (const char *)req.name.ptr, (int)iname.len, (const char *)iname.ptr);
                        return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                    }
                    if (!members_all_public && !(own_methods[mi].vis == TK_VIS_PUB || own_methods[mi].vis == TK_VIS_EXP)) {
                        size_t len = type_name.len + req.name.len + iname.len + 80; char *buf = tk_alloc(len); if (!buf) abort();
                        snprintf(buf, len, "'%.*s' method '%.*s' must be `pub` to satisfy interface '%.*s'",
                                 (int)type_name.len, (const char *)type_name.ptr, (int)req.name.len, (const char *)req.name.ptr, (int)iname.len, (const char *)iname.ptr);
                        return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                    }
                    found = true; break;
                }
            }
            if (!found) {
                size_t len = type_name.len + iname.len + req.name.len + 80; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "'%.*s' does not implement interface '%.*s' — missing method '%.*s'",
                         (int)type_name.len, (const char *)type_name.ptr, (int)iname.len, (const char *)iname.ptr, (int)req.name.len, (const char *)req.name.ptr);
                return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
    }
    return (tk_type_result){ .ok = true };
}

// (MEM Step 0, R4) ESCAPE GATE — a reference cannot be a struct FIELD, variant member, slice
// element, or alias target. tk_resolve_type already rejects a Reference inside a slice/optional/
// union (R4 there), so this catches the BARE-position cases: `struct { f: Ref<i64> }` and a
// transparent alias `type R = Ref<i64>`. Runs once per program over every declared type body.
// Mirror of collect.tks::validate_type_decls. Returns ok=true on success; ok=false carries the error.
static tk_type_result validate_type_decls(tk_type_table table) {
    for (size_t i = 0; i < table.len; i += 1) {
        tk_type_decl decl = table.ptr[i].decl;
        // (S4) the decl's own generic type-params, opaque, in scope — so a generic body field `T`
        // resolves to Named{T} (not "unknown type"); mirrors func_type. A `Ref<T>` field is still
        // rejected by R1 (its inner is a Named, not a scalar Prim).
        tk_type_table tbl = tk_type_param_table(decl.type_params, decl.n_type_params, (tk_str){0}, table);
        tk_type_body body = decl.body;
        if (body.tag == TK_BODY_STRUCT) {
            for (size_t f = 0; f < body.as.struct_body.n_fields; f += 1) {
                tk_type_result r = tk_resolve_type(body.as.struct_body.fields[f].type_ann, tbl);
                if (!r.ok) return r;
                if (r.as.value.tag == TK_TYPE_REF)
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            }
            tk_type_result cf = check_conformance(decl.name, body.as.struct_body.methods, body.as.struct_body.n_methods,   // (W10b.IF) struct members all-public
                                                  body.as.struct_body.implements, body.as.struct_body.n_implements, table, true);
            if (!cf.ok) return cf;
        } else if (body.tag == TK_BODY_ALIAS) {
            tk_type_result r = tk_resolve_type(body.as.alias_body.alias, tbl);
            if (!r.ok) return r;
            if (r.as.value.tag == TK_TYPE_REF)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
        } else if (body.tag == TK_BODY_CLASS) {   // (W10b.CLASS) same R4 escape-gate as a struct's fields
            tk_type_result acyc = class_inheritance_acyclic(decl.name, table);   // guard cyclic inheritance BEFORE any recursive base-chain walk
            if (!acyc.ok) return acyc;
            for (size_t f = 0; f < body.as.class_body.n_fields; f += 1) {
                tk_type_result r = tk_resolve_type(body.as.class_body.fields[f].type_ann, tbl);
                if (!r.ok) return r;
                if (r.as.value.tag == TK_TYPE_REF)
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            }
            tk_type_result vr = validate_class_decl(body.as.class_body, table);   // (W10b.CLASS increment 2)
            if (!vr.ok) return vr;
            // (W10b.IF) a class conforms via its EFFECTIVE methods (inherited base methods count).
            tk_methodsvec_result eff = tk_effective_class_methods(body.as.class_body, table);
            if (!eff.ok) return (tk_type_result){ .ok = false, .as.error = eff.as.error };
            tk_type_result cf = check_conformance(decl.name, eff.as.value.ptr, eff.as.value.len,   // class methods need real `pub`
                                                  body.as.class_body.implements, body.as.class_body.n_implements, table, false);
            if (!cf.ok) return cf;
        } else if (body.tag == TK_BODY_INTERFACE) {
            tk_interface_body ibb = body.as.interface_body;
            // (W10b.IF) OWN methods must be BODYLESS signatures — a pure contract, no default bodies.
            for (size_t mi = 0; mi < ibb.n_methods; mi += 1) {
                if (ibb.methods[mi].nbody > 0) {
                    tk_str nm = ibb.methods[mi].name;
                    size_t len = nm.len + 112; char *buf = tk_alloc(len); if (!buf) abort();
                    snprintf(buf, len, "interface method '%.*s' must be a bodyless signature (an interface is a pure contract — no default bodies)", (int)nm.len, (const char *)nm.ptr);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                }
            }
            // its `extends` names must each BE an interface (transitively) and not form a cycle
            // (seed the cycle-path with this interface's own name).
            tk_str dn = decl.name;
            tk_methodsvec_result em = effective_interface_methods(ibb, table, &dn, 1);
            if (!em.ok) return (tk_type_result){ .ok = false, .as.error = em.as.error };
            // no two EFFECTIVE methods may share a name with a DIFFERENT signature — a redeclaration
            // conflict, caught HERE (at the interface) rather than later at an implementer.
            for (size_t a = 0; a < em.as.value.len; a += 1) {
                for (size_t b = a + 1; b < em.as.value.len; b += 1) {
                    if (tk_str_eq(em.as.value.ptr[a].name, em.as.value.ptr[b].name) && !method_sig_matches(em.as.value.ptr[a], em.as.value.ptr[b], table)) {
                        tk_str nm = em.as.value.ptr[a].name;
                        size_t len = dn.len + nm.len + 112; char *buf = tk_alloc(len); if (!buf) abort();
                        snprintf(buf, len, "interface '%.*s' has conflicting signatures for method '%.*s' (an extended interface declares it differently)", (int)dn.len, (const char *)dn.ptr, (int)nm.len, (const char *)nm.ptr);
                        return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                    }
                }
            }
        }
    }
    return (tk_type_result){ .ok = true };
}

tk_collected_result tk_collect(tk_program program) {
    tk_type_table table = collect_types(program.items, program.len);
    tk_type_result vr = validate_type_decls(table);   // (MEM Step 0, R4)
    if (!vr.ok) return (tk_collected_result){ .ok = false, .as.error = vr.as.error };
    tk_env env = tk_env_empty();
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_FUNCTION) {
            tk_function f = program.items[i].as.function;
            tk_type_result ft = func_type(f, table);
            if (!ft.ok) return (tk_collected_result){ .ok = false, .as.error = ft.as.error };
            env = tk_env_define_fn(env, f.name, ft.as.value, program.items[i].namespace);   // #41: carry the fn's ns
        } else if (program.items[i].tag == TK_ITEM_TYPE_DECL && program.items[i].as.type_decl.body.tag == TK_BODY_STRUCT) {
            // (OOP A1) register each struct's methods under "<owning-ns>::<StructName>" (bare
            // StructName at the top level) — tk_env_lookup_call's qualifier match compares only
            // the LAST segment, so `StructName::method(…)` resolves through the SAME #41
            // machinery as any other qualified call, with zero new resolution code.
            tk_type_decl td = program.items[i].as.type_decl;
            tk_str owning_ns = program.items[i].namespace;
            tk_str method_ns = owning_ns.len == 0 ? td.name : collect_str_concat(collect_str_concat(owning_ns, (tk_str){ .ptr = (const tk_byte *)"::", .len = 2 }), td.name);
            for (size_t mi = 0; mi < td.body.as.struct_body.n_methods; mi += 1) {
                tk_function mf = td.body.as.struct_body.methods[mi];
                tk_type_result mft = method_func_type(mf, td.name, table);
                if (!mft.ok) return (tk_collected_result){ .ok = false, .as.error = mft.as.error };
                env = tk_env_define_fn(env, mf.name, mft.as.value, method_ns);
            }
        } else if (program.items[i].tag == TK_ITEM_TYPE_DECL && program.items[i].as.type_decl.body.tag == TK_BODY_CLASS) {
            // (W10b.CLASS) same registration as a struct method — the receiver is still
            // Named{class_name} (by-value); a Ref<T> receiver is a later increment. Registers the
            // EFFECTIVE (base-inherited + own/overridden) methods (increment 2), so an
            // inherited-but-not-overridden method is callable as DerivedClass::method(…) too.
            tk_type_decl td = program.items[i].as.type_decl;
            tk_str owning_ns = program.items[i].namespace;
            tk_str method_ns = owning_ns.len == 0 ? td.name : collect_str_concat(collect_str_concat(owning_ns, (tk_str){ .ptr = (const tk_byte *)"::", .len = 2 }), td.name);
            tk_methodsvec_result eff = tk_effective_class_methods(td.body.as.class_body, table);
            if (!eff.ok) return (tk_collected_result){ .ok = false, .as.error = eff.as.error };
            for (size_t mi = 0; mi < eff.as.value.len; mi += 1) {
                tk_function mf = eff.as.value.ptr[mi];
                tk_type_result mft = method_func_type(mf, td.name, table);
                if (!mft.ok) return (tk_collected_result){ .ok = false, .as.error = mft.as.error };
                env = tk_env_define_fn(env, mf.name, mft.as.value, method_ns);
            }
        }
    }
    tk_collected c = { .types = table, .env = env };
    return (tk_collected_result){ .ok = true, .as.value = c };
}

// C7.10 — collect with a pre-seeded (table, env). Adds the project's type decls and function
// signatures on top of the seed. Mirrors collect.tks::collect_with_seed.
tk_collected_result tk_collect_with_seed(tk_program program, tk_collected seed) {
    tk_type_table table = seed.types;
    tk_env env = seed.env;

    // Pass 1: add project's type decls to the table.
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl td = program.items[i].as.type_decl;
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = program.items[i].namespace,
                .vis = td.vis, .decl = td });
        }
    }

    tk_type_result vr = validate_type_decls(table);   // (MEM Step 0, R4)
    if (!vr.ok) return (tk_collected_result){ .ok = false, .as.error = vr.as.error };

    // Pass 2: add project's function signatures to the env.
    for (size_t i = 0; i < program.len; i += 1) {
        if (program.items[i].tag == TK_ITEM_FUNCTION) {
            tk_function f = program.items[i].as.function;
            tk_type_result ft = func_type(f, table);
            if (!ft.ok) return (tk_collected_result){ .ok = false, .as.error = ft.as.error };
            env = tk_env_define_fn(env, f.name, ft.as.value, program.items[i].namespace);
        } else if (program.items[i].tag == TK_ITEM_TYPE_DECL && program.items[i].as.type_decl.body.tag == TK_BODY_STRUCT) {
            // (OOP A1) mirror tk_collect's struct-method registration
            tk_type_decl td = program.items[i].as.type_decl;
            tk_str owning_ns = program.items[i].namespace;
            tk_str method_ns = owning_ns.len == 0 ? td.name : collect_str_concat(collect_str_concat(owning_ns, (tk_str){ .ptr = (const tk_byte *)"::", .len = 2 }), td.name);
            for (size_t mi = 0; mi < td.body.as.struct_body.n_methods; mi += 1) {
                tk_function mf = td.body.as.struct_body.methods[mi];
                tk_type_result mft = method_func_type(mf, td.name, table);
                if (!mft.ok) return (tk_collected_result){ .ok = false, .as.error = mft.as.error };
                env = tk_env_define_fn(env, mf.name, mft.as.value, method_ns);
            }
        } else if (program.items[i].tag == TK_ITEM_TYPE_DECL && program.items[i].as.type_decl.body.tag == TK_BODY_CLASS) {
            // (W10b.CLASS) mirrors tk_collect's class-method registration (increment 2: registers
            // the EFFECTIVE, base-inherited methods too)
            tk_type_decl td = program.items[i].as.type_decl;
            tk_str owning_ns = program.items[i].namespace;
            tk_str method_ns = owning_ns.len == 0 ? td.name : collect_str_concat(collect_str_concat(owning_ns, (tk_str){ .ptr = (const tk_byte *)"::", .len = 2 }), td.name);
            tk_methodsvec_result eff = tk_effective_class_methods(td.body.as.class_body, table);
            if (!eff.ok) return (tk_collected_result){ .ok = false, .as.error = eff.as.error };
            for (size_t mi = 0; mi < eff.as.value.len; mi += 1) {
                tk_function mf = eff.as.value.ptr[mi];
                tk_type_result mft = method_func_type(mf, td.name, table);
                if (!mft.ok) return (tk_collected_result){ .ok = false, .as.error = mft.as.error };
                env = tk_env_define_fn(env, mf.name, mft.as.value, method_ns);
            }
        }
    }

    return (tk_collected_result){ .ok = true, .as.value = (tk_collected){ .types = table, .env = env } };
}

// C7.10 — pre-seed the collection environment from an already-typed dep TProgram.
// Pass 1: insert type declarations (so function param types can resolve against them).
// Pass 2: register function signatures using the merged type table.
// Mirrors collect.tks::seed_from_dep exactly.
tk_collected_result tk_seed_from_dep(tk_tprogram dep, tk_type_table table, tk_env env) {
    tk_str empty_ns = { .ptr = (const unsigned char *)"", .len = 0 };

    // Pass 1: type decls → type table (carry the dep's namespace).
    for (size_t i = 0; i < dep.nitems; i += 1) {
        if (dep.items[i].tag == TK_TITEM_TYPE_DECL) {
            tk_type_decl td = dep.items[i].as.type_decl;
            // TypeDecl items in a TProgram have no wrapping namespace field; use "".
            table = tk_type_table_push(table, (tk_type_reg){
                .name = td.name, .namespace = empty_ns, .vis = td.vis, .decl = td });
        }
    }

    // Pass 2: function signatures → env (param types resolved against the merged table).
    for (size_t j = 0; j < dep.nitems; j += 1) {
        if (dep.items[j].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = dep.items[j].as.function;

        // Build param types by resolving the param type annotations against `table`.
        tk_type_table dtbl = tk_type_param_table(f.type_params, f.n_type_params, (tk_str){0}, table);   // (S4) no-op for concrete deps
        tk_type *params = NULL; size_t np = 0;
        tk_str *param_names = NULL;   // DEFARGS (2026-07-01)
        tk_expr *defaults = NULL; size_t ndefaults = 0;
        size_t n_required = f.nparams;
        bool ok = true;
        tk_error err = {0};
        for (size_t k = 0; k < f.nparams; k += 1) {
            tk_type_result pt = tk_resolve_type(f.params[k].type_ann, dtbl);
            if (!pt.ok) { ok = false; err = pt.as.error; break; }
            params = tk_realloc0(params, (np + 1) * sizeof *params);
            params[np++] = pt.as.value;
            param_names = tk_realloc0(param_names, np * sizeof *param_names);
            param_names[np - 1] = f.params[k].name;
            if (f.params[k].has_default) {
                if (n_required == f.nparams) { n_required = k; }
                defaults = tk_realloc0(defaults, (ndefaults + 1) * sizeof *defaults);
                defaults[ndefaults] = f.params[k].default_expr; ndefaults += 1;
            }
        }
        if (!ok) { tk_free0(params); return (tk_collected_result){ .ok = false, .as.error = err }; }

        // The return type is already resolved in TFunction.return_type — use it directly.
        tk_type *rp = tk_alloc(sizeof *rp); if (!rp) abort();
        *rp = f.return_type;
        bool is_variadic = f.nparams > 0 && f.params[f.nparams - 1].is_params;
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { params, np, rp, is_variadic, param_names, n_required, defaults, ndefaults } };
        env = tk_env_define_fn(env, f.name, ft, f.namespace);
    }

    return (tk_collected_result){ .ok = true, .as.value = (tk_collected){ .types = table, .env = env } };
}
