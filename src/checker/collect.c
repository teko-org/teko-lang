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

// (#109 W1) `ref_ns` = the namespace the function is declared in (the referencing namespace for its
// param/return type annotations); threaded to tk_resolve_type, unused until W2's R0-R5 rules.
static tk_type_result func_type(tk_function f, tk_type_table table, tk_str ref_ns) {
    tk_type_table tbl = tk_type_param_table(f.type_params, f.n_type_params, f.type_constraints, (tk_str){0}, table);   // (S4) opaque type-params in scope
    tk_type *params = NULL; size_t n = 0;
    tk_str *param_names = NULL;   // DEFARGS (2026-07-01)
    tk_expr *defaults = NULL; size_t ndefaults = 0;
    size_t n_required = f.nparams;   // the first has_default param's index; TRAILING-ONLY (rule A) means every param after it also defaults
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type_result pt = tk_resolve_type(f.params[i].type_ann, tbl, ref_ns);
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
        ? tk_resolve_type(f.return_type, tbl, ref_ns)
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
// (#109 W1) `ref_ns` = the method's declaring namespace; threaded to tk_resolve_type, unused until W2.
tk_type_result tk_method_func_type(tk_function f, tk_str struct_name, tk_type_table table, tk_str ref_ns) {
    tk_type_table tbl = tk_type_param_table(f.type_params, f.n_type_params, f.type_constraints, (tk_str){0}, table);
    tk_type *params = NULL; size_t n = 0;
    tk_str *param_names = NULL;
    tk_expr *defaults = NULL; size_t ndefaults = 0;
    size_t n_required = f.nparams;
    for (size_t i = 0; i < f.nparams; i += 1) {
        tk_type pt;
        if (!f.params[i].has_type) {
            { tk_str bn = tk_name_last_segment(struct_name); pt = (tk_type){ .tag = TK_TYPE_NAMED, .as.named = { tk_qualify(type_ns_of(table, bn), bn) } }; }   /* (#109 W3) NORMALIZE from the bare last-segment (bare from collect OR canonical from dispatch): real class → its ns; type-param `T` (absent) → "" → BARE */
        } else {
            tk_type_result ptr = tk_resolve_type(f.params[i].type_ann, tbl, ref_ns);
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
        ? tk_resolve_type(f.return_type, tbl, ref_ns)
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
    tk_decl_result decl = tk_type_table_find(table, name, (tk_str){0});   // (#109 W1) class-body lookup on a resolved name — no referencing ns
    if (!decl.ok) return (tk_classbody_result){ .ok = false, .as.error = tk_error_named("unknown base class", name) };
    if (decl.as.value.body.tag != TK_BODY_CLASS) return (tk_classbody_result){ .ok = false, .as.error = tk_error_woven1("", name, " is not a class") };
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
    return (tk_member_owner_result){ .ok = false, .as.error = tk_error_woven2("internal: field '", field_name, "' not found in ", class_name, "'s class chain") };
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
    return (tk_member_owner_result){ .ok = false, .as.error = tk_error_woven2("internal: method '", method_name, "' not found in ", class_name, "'s class chain") };
}

// (W10b.CLASS residual — intern visibility) is `owner`'s member reachable from code whose OWN
// declaring class is `accessor_type` (empty — not inside any class method)? Mirror of
// collect.tks::member_accessible.
bool tk_member_accessible(tk_member_owner owner, tk_str accessor_type, tk_type_table table) {
    if (owner.vis == TK_VIS_PUB || owner.vis == TK_VIS_EXP) return true;
    // TK_VIS_PRIVATE (the class default)
    if (accessor_type.len == 0) return false;
    // (#109 W3) `accessor_type` (env.owner_type) and `owner.declaring_class` may differ in
    // bare-vs-canonical spelling (the receiver's type is canonicalized via tk_qualify(); the
    // method's declaring class flows bare from find_method_owner) — compare by bare last-segment
    // so a class's own method reaches its own private members.
    if (tk_str_eq(tk_name_last_segment(accessor_type), tk_name_last_segment(owner.declaring_class))) return true;
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

// (DEFARGS rule E, 2026-07-02) does any param in `contract_params` that OWNS a default (has_default)
// get RESTATED (has_default again, same or different value) by the corresponding param in
// `impl_params`? The contract (interface method / base virtual-abstract method) owns a named
// param's default; an implementing/overriding method may only supply the type, never restate the
// `= …` (soundness under dispatch — see teko-default-args-named-call, ruling E.i). Both param lists
// are assumed already length-matched by the caller (method_sig_matches / the override arity check).
// Returns the OFFENDING param's name, or an empty tk_str if no restatement is found. Mirror of
// collect.tks::default_restated_param.
static tk_str default_restated_param(const tk_param *contract_params, size_t n_contract, const tk_param *impl_params, size_t n_impl) {
    size_t n = n_contract < n_impl ? n_contract : n_impl;
    for (size_t k = 0; k < n; k += 1) {
        if (contract_params[k].has_default && impl_params[k].has_default) return impl_params[k].name;
    }
    return (tk_str){0};
}

// (W10b.CLASS increment 2) validate ONE class's inheritance/override declarations. Mirror of
// collect.tks::validate_class_decl.
static tk_type_result validate_class_decl(tk_class_body cb, tk_type_table table) {
    if (cb.has_base) {
        tk_decl_result base_decl = tk_type_table_find(table, cb.base_name, (tk_str){0});   // (#109 W1) base-class lookup on a resolved name — no referencing ns
        if (!base_decl.ok) return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown base class", cb.base_name) };
        if (base_decl.as.value.body.tag != TK_BODY_CLASS) return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("", cb.base_name, " is not a class") };
        if (base_decl.as.value.body.as.class_body.kind == TK_CLASS_SEALED)
            return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("cannot inherit from `", cb.base_name, "` — it is sealed (only `abstract`/`virtual` classes can be a base)") };
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
        if (!found) return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("'", m.name, "' has no matching base method to override") };
        bool overridable = bm.is_abstract || bm.is_virtual || bm.is_override;
        if (!overridable) return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("'", m.name, "' overrides a base method that is not `abstract`/`virtual`") };
        if (bm.nparams != m.nparams) return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("'", m.name, "' override must have the same parameter count as the base method") };
        tk_str restated = default_restated_param(bm.params, bm.nparams, m.params, m.nparams);
        if (restated.len > 0) {
            size_t len = m.name.len + restated.len + cb.base_name.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
            snprintf(buf, len, "'%.*s' override must not restate the default for '%.*s' — the base method '%.*s::%.*s' owns its default",
                     (int)m.name.len, (const char *)m.name.ptr, (int)restated.len, (const char *)restated.ptr,
                     (int)cb.base_name.len, (const char *)cb.base_name.ptr, (int)m.name.len, (const char *)m.name.ptr);
            return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
        }
    }
    return (tk_type_result){ .ok = true };
}

// (W10b.IF) look up an interface DECL by a SOURCE-WRITTEN name — error if unknown or not an
// interface. (#109 order fix) `ref_ns` = the REFERENCING namespace (the decl whose `implements`/
// `extends` name is being resolved): the name rides the R0-R5 resolver (tk_resolve_name_ref),
// never the namespace-blind table scan — discovery (readdir) order differs across platforms, and
// a bare name colliding with a same-named type in ANOTHER namespace picked whichever the scan met
// first (CI: linux-arm64 found teko::emit::Reader the struct before teko::io::Reader). The hit
// carries the WINNING namespace so extends recursion and cycle identity stay namespace-exact.
// Mirror of collect.tks::find_interface_decl (IfaceHit).
typedef struct { bool ok; union { struct { tk_type_decl decl; tk_str ns; } value; tk_error error; } as; } ifacehit_result;
static ifacehit_result find_interface_decl(tk_str name, tk_type_table table, tk_str ref_ns) {
    tk_decl_result decl = tk_resolve_name_ref(name, table, ref_ns);
    if (decl.ok) {
        if (decl.as.value.body.tag != TK_BODY_INTERFACE) {
            size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
            snprintf(buf, len, "%.*s is not an interface", (int)name.len, (const char *)name.ptr);
            return (ifacehit_result){ .ok = false, .as.error = tk_error_make(buf) };
        }
        return (ifacehit_result){ .ok = true, .as.value = { decl.as.value, tk_resolved_name_ns(name, table, ref_ns) } };
    }
    // (dual-convention) the CANONICAL-keyed table (codegen/vm type_table_of: canonical name +
    // ns "") cannot satisfy the source R-rules — probe the QUALIFIED form exactly instead
    // (an exact/qualified match is unique post-W0, never an order-dependent bare scan).
    tk_decl_result qd = tk_type_table_find(table, tk_qualify(ref_ns, name), (tk_str){0});
    if (!qd.ok) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "unknown interface: %.*s", (int)name.len, (const char *)name.ptr);
        return (ifacehit_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    if (qd.as.value.body.tag != TK_BODY_INTERFACE) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "%.*s is not an interface", (int)name.len, (const char *)name.ptr);
        return (ifacehit_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    return (ifacehit_result){ .ok = true, .as.value = { qd.as.value, tk_name_qualifier(qd.as.value.name) } };
}

// (W10b.IF) an interface's EFFECTIVE methods: (transitively) every extended interface's methods,
// then its own. Each `extends` name must itself be an interface (find_interface_decl enforces it).
// `iface_ns` = the namespace of the interface DECLARING these `extends` (bare names resolve there).
// `seen` = the interfaces' CANONICAL names on the current root→here PATH — a decl reappearing on
// the path is a CYCLE, rejected as an honest error instead of recursing forever (a diamond via two
// DIFFERENT branches is fine — `seen` tracks only the current path). Mirror of
// collect.tks::effective_interface_methods.
static tk_methodsvec_result effective_interface_methods(tk_interface_body ib, tk_str iface_ns, tk_type_table table, const tk_str *seen, size_t n_seen) {
    tk_function *out = NULL; size_t n_out = 0;
    for (size_t ei = 0; ei < ib.n_extends; ei += 1) {
        tk_str ename = ib.extends[ei];
        // (#109) resolve the extended name FIRST (from the declaring interface's namespace); the
        // cycle identity is the QUALIFIED form of the winning (ns, bare-name) pair, and the
        // recursion resolves the base's own extends from ITS namespace.
        ifacehit_result hit = find_interface_decl(ename, table, iface_ns);
        if (!hit.ok) return (tk_methodsvec_result){ .ok = false, .as.error = hit.as.error };
        tk_str key = tk_qualify(hit.as.value.ns, tk_name_last_segment(hit.as.value.decl.name));
        for (size_t si = 0; si < n_seen; si += 1) {
            if (tk_str_eq(seen[si], key)) {
                size_t len = ename.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "cyclic interface `extends`: '%.*s' extends itself (directly or transitively)", (int)ename.len, (const char *)ename.ptr);
                return (tk_methodsvec_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
        tk_str *seen2 = tk_alloc((n_seen + 1) * sizeof *seen2); if (!seen2) abort();   // seen ++ [qualified key]
        for (size_t i = 0; i < n_seen; i += 1) seen2[i] = seen[i];
        seen2[n_seen] = key;
        tk_methodsvec_result bm = effective_interface_methods(hit.as.value.decl.body.as.interface_body, hit.as.value.ns, table, seen2, n_seen + 1);
        if (!bm.ok) return bm;
        for (size_t bi = 0; bi < bm.as.value.len; bi += 1) tk_functions_push(&out, &n_out, bm.as.value.ptr[bi]);
    }
    for (size_t mi = 0; mi < ib.n_methods; mi += 1) tk_functions_push(&out, &n_out, ib.methods[mi]);
    return (tk_methodsvec_result){ .ok = true, .as.value = { .ptr = out, .len = n_out } };
}

// (W10b.D3) an interface's effective methods BY NAME — the PUBLIC dispatch surface. The checker
// (interface-receiver method typing), codegen (vtable emission), and the plan's slot scheme all
// read THIS list in THIS order (extends-transitive methods first, then own) — the method's index
// here IS its vtable slot. Callers pass a RESOLVED name (a canonical Named.name or a validated
// interface), so the probe is the namespace-blind tk_type_table_find; the declaring ns (for the
// extends walk) is recovered via type_ns_of. Mirror of collect.tks::iface_methods_by_name.
tk_methodsvec_result tk_iface_methods_by_name(tk_str iface, tk_type_table table) {
    tk_decl_result decl = tk_type_table_find(table, iface, (tk_str){0});
    if (!decl.ok) {
        size_t len = iface.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "unknown interface: %.*s", (int)iface.len, (const char *)iface.ptr);
        return (tk_methodsvec_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    if (decl.as.value.body.tag != TK_BODY_INTERFACE) {
        size_t len = iface.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "%.*s is not an interface", (int)iface.len, (const char *)iface.ptr);
        return (tk_methodsvec_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    // declaring ns: the table entry's (typing convention) or the canonical name's qualifier
    // (codegen/vm convention — entries carry canonical name + ns "").
    tk_str ns = type_ns_of(table, iface);
    if (ns.len == 0) ns = tk_name_qualifier(decl.as.value.name);
    tk_str seed = tk_qualify(ns, tk_name_last_segment(decl.as.value.name));
    return effective_interface_methods(decl.as.value.body.as.interface_body, ns, table, &seed, 1);
}

// (#98) the base class's VIRTUAL METHOD TABLE — its effective methods (codegen's vtable slot
// order). Mirror of collect.tks::base_vtable_methods.
tk_methodsvec_result tk_base_vtable_methods(tk_str base, tk_type_table table) {
    tk_classbody_result cb = tk_find_class_body(base, table);
    if (!cb.ok) return (tk_methodsvec_result){ .ok = false, .as.error = cb.as.error };
    return tk_effective_class_methods(cb.as.value, table);
}

// (#98) the vtable SLOT of `method` in `base`'s virtual method table, or !ok if `method` isn't an
// effective base method (a subclass-only method — stays a DIRECT call). Mirror of
// collect.tks::base_vtable_slot.
tk_slot_result tk_base_vtable_slot(tk_str base, tk_str method, tk_type_table table) {
    tk_methodsvec_result ms = tk_base_vtable_methods(base, table);
    if (!ms.ok) return (tk_slot_result){ .ok = false, .as.error = ms.as.error };
    for (size_t i = 0; i < ms.as.value.len; i += 1)
        if (tk_str_eq(ms.as.value.ptr[i].name, method)) return (tk_slot_result){ .ok = true, .as.value = (uint32_t)i };
    return (tk_slot_result){ .ok = false, .as.error = tk_error_woven1("`", method, "` is not a virtual method of the base class") };
}

// (W10b.D3) `tk_is_interface_name` / `tk_type_conforms_to` live in resolve.c (the widening rule
// tk_widens_into needs them, and resolve sits BELOW collect in the module DAG — DIP).

// (W10b.IF) does implementing method `impl_m` satisfy interface-required method `req`? Same NON-
// receiver param types (both skip their 1st untyped receiver param) + same return type. Any type
// that fails to resolve makes it a non-match. Mirror of collect.tks::method_sig_matches.
static bool method_sig_matches(tk_function req, tk_function impl_m, tk_type_table table, tk_str ref_ns) {
    size_t rstart = (req.nparams > 0 && !req.params[0].has_type) ? 1 : 0;
    size_t istart = (impl_m.nparams > 0 && !impl_m.params[0].has_type) ? 1 : 0;
    // instance/static parity: an INSTANCE interface method (has a receiver) must be satisfied by an
    // INSTANCE method, not a same-named STATIC one (rstart/istart are 1 iff a receiver is present).
    if (rstart != istart) return false;
    if (req.nparams - rstart != impl_m.nparams - istart) return false;
    for (size_t k = 0; rstart + k < req.nparams; k += 1) {
        tk_type_result rt = tk_resolve_type(req.params[rstart + k].type_ann, table, ref_ns);   // (#109 W2) resolve source-ref types in the declaring namespace
        if (!rt.ok) return false;
        tk_type_result it = tk_resolve_type(impl_m.params[istart + k].type_ann, table, ref_ns);   // (#109 W2) resolve source-ref types in the declaring namespace
        if (!it.ok) return false;
        if (!tk_type_eq(&rt.as.value, &it.as.value)) return false;
    }
    if (req.has_return != impl_m.has_return) return false;
    if (req.has_return) {
        tk_type_result rr = tk_resolve_type(req.return_type, table, ref_ns);   // (#109 W2) resolve source-ref types in the declaring namespace
        if (!rr.ok) return false;
        tk_type_result ir = tk_resolve_type(impl_m.return_type, table, ref_ns);   // (#109 W2) resolve source-ref types in the declaring namespace
        if (!ir.ok) return false;
        if (!tk_type_eq(&rr.as.value, &ir.as.value)) return false;
    }
    return true;
}

// (W10b.IF) THE CONFORMANCE CHECK. `members_all_public` = true for a STRUCT (members all-public,
// so the AST `vis` is irrelevant); false for a CLASS (a class method must genuinely be `pub`/`exp`
// to satisfy the public interface contract). Mirror of collect.tks::check_conformance.
static tk_type_result check_conformance(tk_str type_name, tk_str type_ns, const tk_function *own_methods, size_t n_own,
                                        const tk_str *implements, size_t n_impl, tk_type_table table, bool members_all_public) {
    for (size_t ii = 0; ii < n_impl; ii += 1) {
        tk_str iname = implements[ii];
        // (#109 order fix) `iname` is the SOURCE name from the `implements` list — resolve it
        // from the CONFORMING type's namespace, and walk its extends from the FOUND decl's own ns.
        ifacehit_result ihit = find_interface_decl(iname, table, type_ns);
        if (!ihit.ok) return (tk_type_result){ .ok = false, .as.error = ihit.as.error };
        tk_str iseed = tk_qualify(ihit.as.value.ns, tk_name_last_segment(ihit.as.value.decl.name));
        tk_methodsvec_result req_methods = effective_interface_methods(ihit.as.value.decl.body.as.interface_body, ihit.as.value.ns, table, &iseed, 1);   // seed cycle-path with the interface's qualified identity
        if (!req_methods.ok) return (tk_type_result){ .ok = false, .as.error = req_methods.as.error };
        for (size_t ri = 0; ri < req_methods.as.value.len; ri += 1) {
            tk_function req = req_methods.as.value.ptr[ri];
            bool found = false;
            for (size_t mi = 0; mi < n_own; mi += 1) {
                if (tk_str_eq(own_methods[mi].name, req.name)) {
                    if (!method_sig_matches(req, own_methods[mi], table, type_ns)) {
                        size_t len = type_name.len + req.name.len + iname.len + 80; char *buf = tk_alloc(len); if (!buf) abort();
                        snprintf(buf, len, "'%.*s' method '%.*s' does not match interface '%.*s's signature",
                                 (int)type_name.len, (const char *)type_name.ptr, (int)req.name.len, (const char *)req.name.ptr, (int)iname.len, (const char *)iname.ptr);
                        return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                    }
                    {
                        tk_str restated = default_restated_param(req.params, req.nparams, own_methods[mi].params, own_methods[mi].nparams);
                        if (restated.len > 0) {
                            size_t len = type_name.len + req.name.len + restated.len + iname.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                            snprintf(buf, len, "'%.*s' method '%.*s' must not restate the default for '%.*s' — interface '%.*s' owns its default",
                                     (int)type_name.len, (const char *)type_name.ptr, (int)req.name.len, (const char *)req.name.ptr,
                                     (int)restated.len, (const char *)restated.ptr, (int)iname.len, (const char *)iname.ptr);
                            return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                        }
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

// ── (TR0, 2026-07-02) TRAITS — the derivation FOLD (mirror of collect.tks) ─────────────────────

// look up a trait body by name — error if the name is unknown or not a trait.
typedef struct { bool ok; union { tk_trait_body value; tk_error error; } as; } traitbody_result;
static traitbody_result find_trait_body(tk_str name, tk_type_table table) {
    tk_decl_result decl = tk_type_table_find(table, name, (tk_str){0});   // (#109 W1) trait-body lookup on a resolved name — no referencing ns
    if (!decl.ok) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "unknown trait: %.*s", (int)name.len, (const char *)name.ptr);
        return (traitbody_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    if (decl.as.value.body.tag != TK_BODY_TRAIT) {
        size_t len = name.len + 32; char *buf = tk_alloc(len); if (!buf) abort();
        snprintf(buf, len, "%.*s is not a trait", (int)name.len, (const char *)name.ptr);
        return (traitbody_result){ .ok = false, .as.error = tk_error_make(buf) };
    }
    return (traitbody_result){ .ok = true, .as.value = decl.as.value.body.as.trait_body };
}

// one recorded derivation: `type_name` derived `trait_name` (requirements checked after ALL
// derivers fold, against the FOLDED table). Mirror of collect.tks::TraitDerive.
typedef struct { tk_str type_name; tk_str trait_name; } trait_derive;

static bool fields_have_name(const tk_field *fields, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1) if (tk_str_eq(fields[i].name, name)) return true;
    return false;
}
static bool methods_have_name(const tk_function *methods, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1) if (tk_str_eq(methods[i].name, name)) return true;
    return false;
}

typedef struct { bool ok; union { struct { tk_field *fields; size_t n_fields; tk_function *methods; size_t n_methods; } value; tk_error error; } as; } folded_members_result;

// fold `trait_names`' members into ONE deriver — see collect.tks::fold_trait_members for the
// full rule-by-rule rationale (field collisions error; a deriver-defined method IS the override;
// two traits providing the same bodied name with no override = error; bodyless = requirement,
// checked later; folded field vis obeys the deriver's kind; folded methods are `pub`).
static folded_members_result fold_trait_members(tk_str type_name, const tk_field *own_fields, size_t n_own_fields,
                                                const tk_function *own_methods, size_t n_own_methods,
                                                const tk_str *trait_names, size_t n_traits, tk_type_table table, bool into_class) {
    tk_field *fields = NULL; size_t nf = 0;
    for (size_t i = 0; i < n_own_fields; i += 1) tk_fields_push(&fields, &nf, own_fields[i]);
    tk_function *methods = NULL; size_t nm = 0;
    for (size_t i = 0; i < n_own_methods; i += 1) tk_functions_push(&methods, &nm, own_methods[i]);
    for (size_t i = 0; i < n_traits; i += 1) {
        tk_str tname = trait_names[i];
        for (size_t di = 0; di < i; di += 1) {
            if (tk_str_eq(trait_names[di], tname)) {
                size_t len = type_name.len + tname.len + 48; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "'%.*s' derives trait '%.*s' more than once",
                         (int)type_name.len, (const char *)type_name.ptr, (int)tname.len, (const char *)tname.ptr);
                return (folded_members_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
        traitbody_result tb = find_trait_body(tname, table);
        if (!tb.ok) return (folded_members_result){ .ok = false, .as.error = tb.as.error };
        for (size_t fi = 0; fi < tb.as.value.n_fields; fi += 1) {
            tk_field tf = tb.as.value.fields[fi];
            if (fields_have_name(fields, nf, tf.name)) {
                size_t len = tname.len + tf.name.len + type_name.len + 112; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "trait '%.*s' field '%.*s' collides with an existing field on '%.*s' — fields cannot be overridden; rename it or do not co-derive",
                         (int)tname.len, (const char *)tname.ptr, (int)tf.name.len, (const char *)tf.name.ptr, (int)type_name.len, (const char *)type_name.ptr);
                return (folded_members_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
            tk_fields_push(&fields, &nf, (tk_field){ .name = tf.name, .type_ann = tf.type_ann,
                .vis = into_class ? TK_VIS_PRIVATE : TK_VIS_PUB, .is_intern = false });
        }
        for (size_t mi = 0; mi < tb.as.value.n_methods; mi += 1) {
            tk_function tm = tb.as.value.methods[mi];
            if (tm.nbody == 0) continue;   // a bodyless method is a REQUIREMENT — not folded here
            if (methods_have_name(own_methods, n_own_methods, tm.name)) {
                // the deriver defines this name itself — its method IS the override.
            } else if (methods_have_name(methods, nm, tm.name)) {
                size_t len = tm.name.len * 2 + type_name.len + 112; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "method '%.*s' is provided by more than one derived trait — '%.*s' must define its own '%.*s' to resolve the conflict",
                         (int)tm.name.len, (const char *)tm.name.ptr, (int)type_name.len, (const char *)type_name.ptr, (int)tm.name.len, (const char *)tm.name.ptr);
                return (folded_members_result){ .ok = false, .as.error = tk_error_make(buf) };
            } else {
                tk_function m = tm;
                m.vis = TK_VIS_PUB;
                m.is_test = false; m.is_extern = false;
                m.c_symbol = (tk_str){0}; m.from_lib = (tk_str){0};
                m.is_intern = false; m.is_abstract = false; m.is_virtual = false; m.is_override = false;
                tk_functions_push(&methods, &nm, m);
            }
        }
    }
    return (folded_members_result){ .ok = true, .as.value = { .fields = fields, .n_fields = nf, .methods = methods, .n_methods = nm } };
}

// the bodyless-REQUIREMENT check, against the FOLDED table — see collect.tks. A class's
// INHERITED methods can satisfy; a requirement shared by several traits is satisfied once.
static tk_type_result check_trait_requirements(const trait_derive *derives, size_t n_derives, tk_type_table table) {
    for (size_t i = 0; i < n_derives; i += 1) {
        trait_derive d = derives[i];
        traitbody_result tb = find_trait_body(d.trait_name, table);
        if (!tb.ok) return (tk_type_result){ .ok = false, .as.error = tb.as.error };
        tk_decl_result decl = tk_type_table_find(table, d.type_name, (tk_str){0});   // (#109 W1) trait-deriver lookup on a resolved name — no referencing ns
        if (!decl.ok) return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("internal: trait deriver '", d.type_name, "' vanished from the type table") };
        const tk_function *own = NULL; size_t n_own = 0;
        if (decl.as.value.body.tag == TK_BODY_STRUCT) {
            own = decl.as.value.body.as.struct_body.methods; n_own = decl.as.value.body.as.struct_body.n_methods;
        } else if (decl.as.value.body.tag == TK_BODY_CLASS) {
            tk_methodsvec_result eff = tk_effective_class_methods(decl.as.value.body.as.class_body, table);
            if (!eff.ok) return (tk_type_result){ .ok = false, .as.error = eff.as.error };
            own = eff.as.value.ptr; n_own = eff.as.value.len;
        }
        for (size_t ri = 0; ri < tb.as.value.n_methods; ri += 1) {
            tk_function req = tb.as.value.methods[ri];
            if (req.nbody != 0) continue;
            bool found = false;
            for (size_t mi = 0; mi < n_own; mi += 1) {
                if (tk_str_eq(own[mi].name, req.name)) {
                    if (!method_sig_matches(req, own[mi], table, type_ns_of(table, d.type_name))) {
                        size_t len = d.type_name.len + req.name.len + d.trait_name.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                        snprintf(buf, len, "'%.*s' method '%.*s' does not match trait '%.*s's required signature",
                                 (int)d.type_name.len, (const char *)d.type_name.ptr, (int)req.name.len, (const char *)req.name.ptr, (int)d.trait_name.len, (const char *)d.trait_name.ptr);
                        return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                    }
                    found = true; break;
                }
            }
            if (!found) {
                size_t len = d.type_name.len + d.trait_name.len + req.name.len + 96; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "'%.*s' does not satisfy trait '%.*s' — missing required method '%.*s'",
                         (int)d.type_name.len, (const char *)d.type_name.ptr, (int)d.trait_name.len, (const char *)d.trait_name.ptr, (int)req.name.len, (const char *)req.name.ptr);
                return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
        }
    }
    return (tk_type_result){ .ok = true };
}

// (TR0) THE DERIVATION FOLD — see collect.tks::fold_traits (the .tks twin carries the full
// design comment). NO-OP (the program returned unchanged) when no trait is declared.
tk_fold_traits_result tk_fold_traits(tk_program program) {
    tk_type_table table = collect_types(program.items, program.len);
    bool any = false;
    for (size_t t = 0; t < table.len; t += 1) {
        if (table.ptr[t].decl.body.tag == TK_BODY_TRAIT) { any = true; break; }
    }
    if (!any) return (tk_fold_traits_result){ .ok = true, .as.value = program };
    tk_item *items = NULL; size_t n_items = 0;
    trait_derive *derives = NULL; size_t n_derives = 0;
    for (size_t i = 0; i < program.len; i += 1) {
        tk_item it = program.items[i];
        if (it.tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl td = it.as.type_decl;
            if (td.body.tag == TK_BODY_STRUCT || td.body.tag == TK_BODY_CLASS) {
                const tk_str *impls = td.body.tag == TK_BODY_STRUCT ? td.body.as.struct_body.implements : td.body.as.class_body.implements;
                size_t n_impls = td.body.tag == TK_BODY_STRUCT ? td.body.as.struct_body.n_implements : td.body.as.class_body.n_implements;
                tk_str *traits = NULL; size_t n_traits = 0;
                tk_str *ifaces = NULL; size_t n_ifaces = 0;
                for (size_t k = 0; k < n_impls; k += 1) {
                    tk_decl_result dr = tk_resolve_name_ref(impls[k], table, it.namespace);   // (#109 order fix) SOURCE name — R0-R5 from the declaring item's namespace, never by scan order
                    bool is_trait = dr.ok && dr.as.value.body.tag == TK_BODY_TRAIT;
                    tk_str **dst = is_trait ? &traits : &ifaces;
                    size_t *ndst = is_trait ? &n_traits : &n_ifaces;
                    *dst = tk_realloc0(*dst, (*ndst + 1) * sizeof **dst); if (!*dst) abort();
                    (*dst)[*ndst] = impls[k]; *ndst += 1;
                }
                if (n_traits > 0) {
                    bool into_class = td.body.tag == TK_BODY_CLASS;
                    const tk_field *own_fields = into_class ? td.body.as.class_body.fields : td.body.as.struct_body.fields;
                    size_t n_own_fields = into_class ? td.body.as.class_body.n_fields : td.body.as.struct_body.n_fields;
                    const tk_function *own_methods = into_class ? td.body.as.class_body.methods : td.body.as.struct_body.methods;
                    size_t n_own_methods = into_class ? td.body.as.class_body.n_methods : td.body.as.struct_body.n_methods;
                    folded_members_result fm = fold_trait_members(td.name, own_fields, n_own_fields, own_methods, n_own_methods,
                                                                  traits, n_traits, table, into_class);
                    if (!fm.ok) return (tk_fold_traits_result){ .ok = false, .as.error = fm.as.error };
                    if (into_class) {
                        td.body.as.class_body.fields = fm.as.value.fields;    td.body.as.class_body.n_fields = fm.as.value.n_fields;
                        td.body.as.class_body.methods = fm.as.value.methods;  td.body.as.class_body.n_methods = fm.as.value.n_methods;
                        td.body.as.class_body.implements = ifaces;            td.body.as.class_body.n_implements = n_ifaces;
                    } else {
                        td.body.as.struct_body.fields = fm.as.value.fields;   td.body.as.struct_body.n_fields = fm.as.value.n_fields;
                        td.body.as.struct_body.methods = fm.as.value.methods; td.body.as.struct_body.n_methods = fm.as.value.n_methods;
                        td.body.as.struct_body.implements = ifaces;           td.body.as.struct_body.n_implements = n_ifaces;
                    }
                    it.as.type_decl = td;
                    for (size_t si = 0; si < n_traits; si += 1) {
                        derives = tk_realloc0(derives, (n_derives + 1) * sizeof *derives); if (!derives) abort();
                        derives[n_derives] = (trait_derive){ .type_name = td.name, .trait_name = traits[si] }; n_derives += 1;
                    }
                }
            }
        }
        items = tk_realloc0(items, (n_items + 1) * sizeof *items); if (!items) abort();
        items[n_items] = it; n_items += 1;
    }
    tk_program folded_prog = { .items = items, .len = n_items };
    tk_type_table folded = collect_types(items, n_items);
    tk_type_result rq = check_trait_requirements(derives, n_derives, folded);
    if (!rq.ok) return (tk_fold_traits_result){ .ok = false, .as.error = rq.as.error };
    return (tk_fold_traits_result){ .ok = true, .as.value = folded_prog };
}

// (#109 W0) `""` (top-level) reads as `<top-level>` in a duplicate-type diagnostic — mirrors
// collect.tks::check_no_duplicate_types's `if ns == "" { "<top-level>" } else { ns }` ternary.
static const char *ns_label_ptr(tk_str ns) { return ns.len == 0 ? "<top-level>" : (const char *)ns.ptr; }
static int ns_label_len(tk_str ns) { return ns.len == 0 ? (int)strlen("<top-level>") : (int)ns.len; }

// (#109 W0, tightened) is `reg` a monomorphization-STAMPED generic instance? The shared
// tk_name_is_g_instance is a naive `__g__` substring scan, and the lexer accepts `__g__` inside
// ordinary identifiers — so a genuine USER type named e.g. `Foo__g__Bar` must NOT slip through
// the duplicate ban on the marker alone. A registration is exempt only when ALL of:
//   (a) the name carries the `__g__` infix,
//   (b) the registration namespace is "" (stamped instances ALWAYS register with empty ns —
//       resolve.c::tk_instantiate_types), and
//   (c) the prefix before the FIRST `__g__` names a declared type WITH type_params somewhere
//       in the table (a real generic base — `Foo__g__Bar` with no generic `Foo` fails this).
// LOCAL predicate on purpose: tk_name_is_g_instance keeps its broad substring semantics for the
// mono pass (which only ever sees names IT stamped); only the W0 ban needs the strict form.
// Mirror of collect.tks::w0_is_stamped_generic_reg.
static bool w0_is_stamped_generic_reg(tk_type_reg reg, tk_type_table table) {
    if (reg.namespace.len != 0) return false;
    tk_str name = reg.name;
    size_t pos = name.len;   // index of the FIRST `__g__`, or name.len when absent
    for (size_t i = 0; i + 5 <= name.len; i += 1) {
        if (name.ptr[i] == '_' && name.ptr[i + 1] == '_' && name.ptr[i + 2] == 'g' && name.ptr[i + 3] == '_' && name.ptr[i + 4] == '_') { pos = i; break; }
    }
    if (pos == name.len) return false;   // no `__g__` infix at all
    if (pos == 0) return false;          // an empty base prefix can never name a generic decl
    tk_str base = { .ptr = name.ptr, .len = pos };
    for (size_t j = 0; j < table.len; j += 1) {
        if (table.ptr[j].decl.n_type_params > 0 && tk_str_eq(table.ptr[j].name, base)) return true;
    }
    return false;
}

// (#109 W0) fail-loud duplicate-registration ban — the first hardening step of the
// namespace-aware type table redesign. Two errors, both O(n^2) over the collected table
// (small — mirrors the interface-conflict scan below):
//   1. same (namespace, name) registered twice = a real duplicate declaration.
//   2. same BARE name registered under two DIFFERENT namespaces = TEMPORARILY banned (today's
//      flat, namespace-blind lookups (tk_type_table_find et al.) would silently conflate the two —
//      W3 lifts this ban once resolution becomes namespace-aware).
// EXEMPT: stamped generic-instance entries (w0_is_stamped_generic_reg — empty ns + a real
// generic base behind the `__g__` infix) — monomorphization legitimately stamps many
// `Base__g__<mangle>` instances that share the "" namespace bucket with every other stamped
// instance; that's not a user-facing collision, and stamping already dedups by exact mangled
// name (resolve.c::tk_instantiate_types's "already stamped" check).
// Mirror of collect.tks::check_no_duplicate_types.
static tk_type_result check_no_duplicate_types(tk_type_table table) {
    for (size_t i = 0; i < table.len; i += 1) {
        if (w0_is_stamped_generic_reg(table.ptr[i], table)) continue;
        for (size_t j = i + 1; j < table.len; j += 1) {
            if (w0_is_stamped_generic_reg(table.ptr[j], table)) continue;
            if (!tk_str_eq(table.ptr[i].name, table.ptr[j].name)) continue;
            tk_str nm = table.ptr[i].name;
            tk_str ns1 = table.ptr[i].namespace, ns2 = table.ptr[j].namespace;
            if (tk_str_eq(ns1, ns2)) {
                size_t len = nm.len + ns_label_len(ns1) + 64;
                char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "duplicate type '%.*s' in namespace '%.*s'",
                         (int)nm.len, (const char *)nm.ptr, ns_label_len(ns1), ns_label_ptr(ns1));
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
    tk_type_result dup = check_no_duplicate_types(table);   // (#109 W0)
    if (!dup.ok) return dup;
    for (size_t i = 0; i < table.len; i += 1) {
        tk_type_decl decl = table.ptr[i].decl;
        tk_str ref_ns = table.ptr[i].namespace;   // (#109 W1) ref_ns = the type decl's declaring namespace (referencing ns for its field annotations)
        // (S4) the decl's own generic type-params, opaque, in scope — so a generic body field `T`
        // resolves to Named{T} (not "unknown type"); mirrors func_type. A `Ref<T>` field is still
        // rejected by R1 (its inner is a Named, not a scalar Prim).
        tk_type_table tbl = tk_type_param_table(decl.type_params, decl.n_type_params, decl.type_constraints, (tk_str){0}, table);
        tk_type_body body = decl.body;
        if (body.tag == TK_BODY_STRUCT) {
            for (size_t f = 0; f < body.as.struct_body.n_fields; f += 1) {
                tk_type_result r = tk_resolve_type(body.as.struct_body.fields[f].type_ann, tbl, ref_ns);
                if (!r.ok) return r;
                if (r.as.value.tag == TK_TYPE_REF)
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            }
            tk_type_result cf = check_conformance(decl.name, ref_ns, body.as.struct_body.methods, body.as.struct_body.n_methods,   // (W10b.IF) struct members all-public
                                                  body.as.struct_body.implements, body.as.struct_body.n_implements, table, true);
            if (!cf.ok) return cf;
        } else if (body.tag == TK_BODY_ALIAS) {
            tk_type_result r = tk_resolve_type(body.as.alias_body.alias, tbl, ref_ns);
            if (!r.ok) return r;
            if (r.as.value.tag == TK_TYPE_REF)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
        } else if (body.tag == TK_BODY_CLASS) {   // (W10b.CLASS) same R4 escape-gate as a struct's fields
            tk_type_result acyc = class_inheritance_acyclic(decl.name, table);   // guard cyclic inheritance BEFORE any recursive base-chain walk
            if (!acyc.ok) return acyc;
            for (size_t f = 0; f < body.as.class_body.n_fields; f += 1) {
                tk_type_result r = tk_resolve_type(body.as.class_body.fields[f].type_ann, tbl, ref_ns);
                if (!r.ok) return r;
                if (r.as.value.tag == TK_TYPE_REF)
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            }
            tk_type_result vr = validate_class_decl(body.as.class_body, table);   // (W10b.CLASS increment 2)
            if (!vr.ok) return vr;
            // (W10b.IF) a class conforms via its EFFECTIVE methods (inherited base methods count).
            tk_methodsvec_result eff = tk_effective_class_methods(body.as.class_body, table);
            if (!eff.ok) return (tk_type_result){ .ok = false, .as.error = eff.as.error };
            tk_type_result cf = check_conformance(decl.name, ref_ns, eff.as.value.ptr, eff.as.value.len,   // class methods need real `pub`
                                                  body.as.class_body.implements, body.as.class_body.n_implements, table, false);
            if (!cf.ok) return cf;
        } else if (body.tag == TK_BODY_TRAIT) {
            // (TR0) a trait is a struct-shaped bundle: field types resolve like a struct's (same
            // R4 escape-gate). Generic traits + the class dispatch markers are OUTSIDE TR0's unit.
            if (decl.n_type_params > 0) {
                tk_str dn = decl.name;
                size_t len = dn.len + 80; char *buf = tk_alloc(len); if (!buf) abort();
                snprintf(buf, len, "a generic trait ('%.*s<…>') is not supported (TR0 — a trait is a concrete bundle)", (int)dn.len, (const char *)dn.ptr);
                return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
            }
            for (size_t f = 0; f < body.as.trait_body.n_fields; f += 1) {
                tk_type_result r = tk_resolve_type(body.as.trait_body.fields[f].type_ann, tbl, ref_ns);
                if (!r.ok) return r;
                if (r.as.value.tag == TK_TYPE_REF)
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            }
            for (size_t tmi = 0; tmi < body.as.trait_body.n_methods; tmi += 1) {
                tk_function m = body.as.trait_body.methods[tmi];
                if (m.is_abstract || m.is_virtual || m.is_override || m.is_intern) {
                    size_t len = m.name.len + 160; char *buf = tk_alloc(len); if (!buf) abort();
                    snprintf(buf, len, "trait method '%.*s' may not be `abstract`/`virtual`/`override`/`intern` — a bodyless trait method is the requirement, and a deriver-defined method is the override (TR0)", (int)m.name.len, (const char *)m.name.ptr);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
                }
            }
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
            tk_str dn = tk_qualify(ref_ns, tk_name_last_segment(decl.name));
            tk_methodsvec_result em = effective_interface_methods(ibb, ref_ns, table, &dn, 1);
            if (!em.ok) return (tk_type_result){ .ok = false, .as.error = em.as.error };
            // no two EFFECTIVE methods may share a name with a DIFFERENT signature — a redeclaration
            // conflict, caught HERE (at the interface) rather than later at an implementer.
            for (size_t a = 0; a < em.as.value.len; a += 1) {
                for (size_t b = a + 1; b < em.as.value.len; b += 1) {
                    if (tk_str_eq(em.as.value.ptr[a].name, em.as.value.ptr[b].name) && !method_sig_matches(em.as.value.ptr[a], em.as.value.ptr[b], table, type_ns_of(table, dn))) {
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
            tk_type_result ft = func_type(f, table, program.items[i].namespace);   // (#109 W1) ref_ns = the fn's declaring namespace
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
                tk_type_result mft = tk_method_func_type(mf, td.name, table, owning_ns);   // (#109 W1) ref_ns = the type's declaring namespace
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
                tk_type_result mft = tk_method_func_type(mf, td.name, table, owning_ns);   // (#109 W1) ref_ns = the type's declaring namespace
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
            tk_type_result ft = func_type(f, table, program.items[i].namespace);   // (#109 W1) ref_ns = the fn's declaring namespace
            if (!ft.ok) return (tk_collected_result){ .ok = false, .as.error = ft.as.error };
            env = tk_env_define_fn(env, f.name, ft.as.value, program.items[i].namespace);
        } else if (program.items[i].tag == TK_ITEM_TYPE_DECL && program.items[i].as.type_decl.body.tag == TK_BODY_STRUCT) {
            // (OOP A1) mirror tk_collect's struct-method registration
            tk_type_decl td = program.items[i].as.type_decl;
            tk_str owning_ns = program.items[i].namespace;
            tk_str method_ns = owning_ns.len == 0 ? td.name : collect_str_concat(collect_str_concat(owning_ns, (tk_str){ .ptr = (const tk_byte *)"::", .len = 2 }), td.name);
            for (size_t mi = 0; mi < td.body.as.struct_body.n_methods; mi += 1) {
                tk_function mf = td.body.as.struct_body.methods[mi];
                tk_type_result mft = tk_method_func_type(mf, td.name, table, owning_ns);   // (#109 W1) ref_ns = the type's declaring namespace
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
                tk_type_result mft = tk_method_func_type(mf, td.name, table, owning_ns);   // (#109 W1) ref_ns = the type's declaring namespace
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
        tk_type_table dtbl = tk_type_param_table(f.type_params, f.n_type_params, f.type_constraints, (tk_str){0}, table);   // (S4) no-op for concrete deps
        tk_type *params = NULL; size_t np = 0;
        tk_str *param_names = NULL;   // DEFARGS (2026-07-01)
        tk_expr *defaults = NULL; size_t ndefaults = 0;
        size_t n_required = f.nparams;
        bool ok = true;
        tk_error err = {0};
        for (size_t k = 0; k < f.nparams; k += 1) {
            tk_type_result pt = tk_resolve_type(f.params[k].type_ann, dtbl, f.namespace);   // (#109 W1) ref_ns = the dep fn's declaring namespace
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
