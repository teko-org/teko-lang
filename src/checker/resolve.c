// src/checker/resolve.c
#include "resolve.h"
#include "../parser/ast.h"   // tk_box_type, tk_types_push, tk_fields_push (instantiation pass)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>     // snprintf — named error messages

// Build a checker error "<msg>: <name>" — names the offending symbol (M.3 — clarify, never vague).
tk_error tk_error_named(const char *msg, tk_str name) {
    size_t len = strlen(msg) + name.len + 4;
    char *buf = tk_alloc(len); if (!buf) abort();
    snprintf(buf, len, "%s: %.*s", msg, (int)name.len, (const char *)name.ptr);
    return tk_error_make(buf);
}

// (#121) Byte-identical twin-message builders. The self-hosted `.tks` engine composes its
// diagnostics via `$"…{x}…"` interpolation, so a name is woven INTO the sentence (e.g.
// `field 'w' is private to Widget`) rather than tacked on as `"…: w"`. These helpers let the
// C engine emit the SAME rendered string, closing the wording divergences (issue #121). The
// campaign leans on byte-identical messages as parity evidence, so the C twin must match the
// canonical `.tks` text exactly. Buffers are whole-compile-lifetime (tk_alloc — arena-style).

// One woven name: "<a>%.*s<b>" — e.g. tk_err_1("field '", field, "' is private to …") after a
// second weave, or a single-name sentence in isolation.
tk_error tk_error_woven1(const char *a, tk_str n1, const char *b) {
    size_t len = strlen(a) + n1.len + strlen(b) + 1;
    char *buf = tk_alloc(len); if (!buf) abort();
    int m = snprintf(buf, len, "%s%.*s%s", a, (int)n1.len, (const char *)n1.ptr, b);
    if (m < 0 || (size_t)m >= len) abort();
    return tk_error_make(buf);
}

// Two woven names: "<a>%.*s<b>%.*s<c>" — e.g. `field 'w' is private to Widget`
// = tk_error_woven2("field '", field, "' is private to ", cls, "").
tk_error tk_error_woven2(const char *a, tk_str n1, const char *b, tk_str n2, const char *c) {
    size_t len = strlen(a) + n1.len + strlen(b) + n2.len + strlen(c) + 1;
    char *buf = tk_alloc(len); if (!buf) abort();
    int m = snprintf(buf, len, "%s%.*s%s%.*s%s",
                     a, (int)n1.len, (const char *)n1.ptr,
                     b, (int)n2.len, (const char *)n2.ptr, c);
    if (m < 0 || (size_t)m >= len) abort();
    return tk_error_make(buf);
}

// (S4) tiny string builders for name mangling (whole-compile-lifetime; tk_alloc — arena-style).
static tk_str rt_cstr(const char *s) {
    size_t n = strlen(s);
    tk_byte *buf = tk_alloc(n ? n : 1); if (!buf) abort();
    if (n) memcpy(buf, s, n);
    return (tk_str){ .ptr = buf, .len = n };
}
static tk_str rt_concat(tk_str a, tk_str b) {
    size_t n = a.len + b.len;
    tk_byte *buf = tk_alloc(n ? n : 1); if (!buf) abort();
    if (a.len) memcpy(buf, a.ptr, a.len);
    if (b.len) memcpy(buf + a.len, b.ptr, b.len);
    return (tk_str){ .ptr = buf, .len = n };
}

// (C1.8) the surface spelling of a prim kind — the same names tk_builtin_type accepts (scope.c),
// so a rendered mismatch reads in the user's own vocabulary ("i32", "f64", "bool", …).
static const char *prim_name(tk_prim_kind k) {
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
    return "<prim>";
}

// (C1.8) a fresh whole-compile-lifetime copy of a NUL-terminated string (tk_alloc; no free in the
// bootstrap — M.5 arena-style). Used to hand the static prim/marker spellings back as owned strings
// so callers (the renderer's recursive composers) treat every result uniformly.
static char *dup_cstr(const char *s) {
    size_t n = strlen(s);
    char *out = tk_alloc(n + 1); if (!out) abort();
    memcpy(out, s, n + 1);
    return out;
}

// (C1.8) render a semantic type to a human string for diagnostics. Recursion builds slice/optional/
// variant bottom-up; each result is a fresh tk_alloc'd, whole-compile-lifetime string. See resolve.h.
const char *tk_type_render(tk_type t) {
    switch (t.tag) {
        case TK_TYPE_PRIM:  return dup_cstr(prim_name(t.as.prim));
        case TK_TYPE_BYTE:  return dup_cstr("byte");
        case TK_TYPE_CHAR:  return dup_cstr("char");
        case TK_TYPE_STR:   return dup_cstr("str");
        case TK_TYPE_ERROR: return dup_cstr("error");
        case TK_TYPE_VOID:  return dup_cstr("void");
        case TK_TYPE_NAMED: {
            // a nominal user type — its declared name verbatim.
            size_t n = t.as.named.name.len;
            char *out = tk_alloc(n + 1); if (!out) abort();
            if (n != 0) memcpy(out, t.as.named.name.ptr, n);
            out[n] = '\0';
            return out;
        }
        case TK_TYPE_SLICE: {
            // []<elem>; a SENTINEL slice (element == NULL, the untyped `empty()`) renders "[]_".
            const char *el = t.as.slice.element ? tk_type_render(*t.as.slice.element) : "_";
            size_t cap = strlen(el) + 3;   // "[]" + el + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "[]%s", el);
            return out;
        }
        case TK_TYPE_OPTIONAL: {
            // <inner>?; a SENTINEL optional (inner == NULL, a bare `null`) renders "_?".
            const char *in = t.as.optional.inner ? tk_type_render(*t.as.optional.inner) : "_";
            size_t cap = strlen(in) + 2;   // in + "?" + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "%s?", in);
            return out;
        }
        case TK_TYPE_VARIANT: {
            // "A | B | …" — join each member with " | ". Two-pass: size, then fill.
            if (t.as.variant.len == 0) return dup_cstr("<empty variant>");
            const char *sep = " | ";
            size_t seplen = strlen(sep);
            size_t cap = 1;   // NUL
            const char **parts = tk_alloc(t.as.variant.len * sizeof *parts); if (!parts) abort();
            for (size_t i = 0; i < t.as.variant.len; i += 1) {
                parts[i] = tk_type_render(t.as.variant.members[i]);
                cap += strlen(parts[i]);
                if (i + 1 < t.as.variant.len) cap += seplen;
            }
            char *out = tk_alloc(cap); if (!out) abort();
            char *p = out;
            for (size_t i = 0; i < t.as.variant.len; i += 1) {
                size_t n = strlen(parts[i]);
                memcpy(p, parts[i], n); p += n;
                if (i + 1 < t.as.variant.len) { memcpy(p, sep, seplen); p += seplen; }
            }
            *p = '\0';
            tk_free0(parts);
            return out;
        }
        case TK_TYPE_FUNC: {
            // a function type — uncommon in user-facing mismatch messages, but render it honestly
            // as "(p0, p1, …) -> ret" rather than a placeholder (M.3 — show what we know).
            const char *ret = t.as.func.ret ? tk_type_render(*t.as.func.ret) : "void";
            const char **ps = NULL;
            size_t cap = strlen("() -> ") + strlen(ret) + 1;
            if (t.as.func.nparams > 0) {
                ps = tk_alloc(t.as.func.nparams * sizeof *ps); if (!ps) abort();
                for (size_t i = 0; i < t.as.func.nparams; i += 1) {
                    ps[i] = tk_type_render(t.as.func.params[i]);
                    cap += strlen(ps[i]);
                    if (i + 1 < t.as.func.nparams) cap += 2;   // ", "
                }
            }
            char *out = tk_alloc(cap); if (!out) abort();
            char *p = out;
            *p++ = '(';
            for (size_t i = 0; i < t.as.func.nparams; i += 1) {
                size_t n = strlen(ps[i]);
                memcpy(p, ps[i], n); p += n;
                if (i + 1 < t.as.func.nparams) { memcpy(p, ", ", 2); p += 2; }
            }
            memcpy(p, ") -> ", 5); p += 5;
            size_t rlen = strlen(ret);
            memcpy(p, ret, rlen); p += rlen;
            *p = '\0';
            if (ps) tk_free0(ps);
            return out;
        }
        case TK_TYPE_PTR: {   // ptr<T> human render; NULL inner = opaque ptr → "ptr"
            if (t.as.ptr.inner == NULL) return dup_cstr("ptr");
            const char *in = tk_type_render(*t.as.ptr.inner);
            size_t cap = strlen(in) + 6;   // "ptr<" + in + ">" + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "ptr<%s>", in);
            return out;
        }
        case TK_TYPE_UPTR:  return dup_cstr("uptr");   // (C7.1a) opaque word-size unsigned
        case TK_TYPE_REF: {   // (MEM-1b) Ref<T> human render (inner never NULL)
            const char *in = tk_type_render(*t.as.ref.inner);
            size_t cap = strlen(in) + 6;   // "Ref<" + in + ">" + NUL
            char *out = tk_alloc(cap); if (!out) abort();
            snprintf(out, cap, "Ref<%s>", in);
            return out;
        }
    }
    return dup_cstr("<type>");
}

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}
// box a Type onto the heap — the compiler-managed indirection, made concrete.
static tk_type *box(tk_type t) {
    tk_type *p = tk_alloc(sizeof *p);
    if (!p) abort();
    *p = t;
    return p;
}

tk_decl_result tk_type_table_find(tk_type_table table, tk_str name) {
    for (size_t i = 0; i < table.len; i += 1) {
        if (name_eq(table.ptr[i].name, name)) {
            return (tk_decl_result){ .ok = true, .as.value = table.ptr[i].decl };
        }
    }
    return (tk_decl_result){ .ok = false, .as.error = tk_error_make("not a user type") };
}

// (W10b.D2, issue #99) is the constraint EXACTLY a single INTERFACE atom `<T: I>`? If so, yields I's
// name; NULL otherwise (compound A&B/A|B, a variant/struct/trait atom, or none — all stay opaque).
// The interface, monomorphized to the concrete type at each call site, drives the D3 vtable dispatch.
// (resolve.tks: constraint_single_interface.) `c` may be NULL (a caller with no constraints array).
static const tk_str *constraint_single_interface(const tk_constraint_expr *c, tk_type_table table) {
    if (!c) return NULL;
    if (c->tag == TK_CONSTRAINT_ATOM && tk_is_interface_name(c->as.atom.name, table)) return &c->as.atom.name;
    return NULL;   // And/Or/None → not a single interface atom
}

// (S4) extend the table with generic type-params as OPAQUE nominal types (an unconstrained T has
// no members/operators → operating on it fails at the definition; no reflection). Prepended so a
// type-param shadows a same-named user type. Empty → unchanged. (resolve.tks: type_param_table.)
//
// (W10b.D2, issue #99) `type_constraints` is PARALLEL to `type_params` (same length; may be NULL when
// a caller has none). A SINGLE INTERFACE ATOM `<T: I>` registers the type-param with an interface-
// mirroring body (`extends = [I]`, no own methods) instead of the opaque ExternBody, so `Named{T}.m()`
// resolves through the EXISTING D3 dispatch (expr.c::type_method_call redirects to `I` for callee/
// vtable, keeping the receiver's `Named{T}` for monomorphization). Every other shape stays opaque.
tk_type_table tk_type_param_table(tk_str *type_params, size_t n_type_params, tk_constraint_expr *type_constraints, tk_str ns, tk_type_table table) {
    if (n_type_params == 0) return table;
    tk_type_table tbl = tk_type_table_empty();
    for (size_t i = 0; i < n_type_params; i += 1) {
        // namespace = the USING namespace so the W-vis-enforce check (check_modules) treats the
        // type-param as a LOCAL type, not a bare cross-namespace reference.
        tk_type_body body = { .tag = TK_BODY_EXTERN, .as.extern_body = { 0 } };
        const tk_str *iface = type_constraints ? constraint_single_interface(&type_constraints[i], table) : NULL;
        if (iface) {
            tk_str *ext = tk_alloc(sizeof *ext); if (!ext) abort(); ext[0] = *iface;
            body = (tk_type_body){ .tag = TK_BODY_INTERFACE,
                                   .as.interface_body = { .extends = ext, .n_extends = 1, .methods = NULL, .n_methods = 0 } };
        }
        tk_type_decl d = { .name = type_params[i], .type_params = NULL, .n_type_params = 0, .type_constraints = NULL,
                           .body = body,
                           .vis = TK_VIS_PRIVATE, .has_doc = false, .doc = (tk_str){0}, .line = 0, .col = 0 };
        tbl = tk_type_table_push(tbl, (tk_type_reg){ .name = type_params[i], .namespace = ns, .vis = TK_VIS_PRIVATE, .decl = d });
    }
    for (size_t j = 0; j < table.len; j += 1) { tbl = tk_type_table_push(tbl, table.ptr[j]); }
    return tbl;
}

// ── (S4) generics inference: subst + unify (resolve.tks twins) ───────────────────────────────────
static tk_type *tk_clone_type(tk_type t) { tk_type *p = tk_alloc(sizeof *p); *p = t; return p; }
bool tk_is_type_param(tk_str name, tk_str *params, size_t np) {
    for (size_t i = 0; i < np; i += 1) if (name_eq(params[i], name)) return true;
    return false;
}
static tk_type *subst_find_c(tk_subst s, tk_str name) {
    for (size_t i = 0; i < s.n_bind; i += 1) if (name_eq(s.names[i], name)) return &s.types[i];
    return NULL;
}
static bool type_has_void_sentinel_c(tk_type t) {
    if (t.tag == TK_TYPE_VOID) return true;
    if (t.tag == TK_TYPE_SLICE)    return t.as.slice.element->tag == TK_TYPE_VOID;
    if (t.tag == TK_TYPE_OPTIONAL) return t.as.optional.inner->tag == TK_TYPE_VOID;
    return false;
}
tk_type tk_subst_type(tk_type t, tk_subst s) {
    switch (t.tag) {
        case TK_TYPE_NAMED: { tk_type *b = subst_find_c(s, t.as.named.name); return b ? *b : t; }
        // A SENTINEL slice/optional (the untyped `empty()` / bare `null`) carries a NULL element/
        // inner pointer (the C twin of the Teko `Void` marker — type.tks). It substitutes to ITSELF
        // (Void → Void on the Teko side); return it unchanged rather than dereferencing NULL. This
        // matters now that monomorph.c walks EVERY node's type, sentinels included (S4b).
        case TK_TYPE_SLICE: { if (!t.as.slice.element) return t; tk_type e = tk_subst_type(*t.as.slice.element, s); return (tk_type){ .tag = TK_TYPE_SLICE, .as.slice = { tk_clone_type(e) } }; }
        case TK_TYPE_OPTIONAL: { if (!t.as.optional.inner) return t; tk_type e = tk_subst_type(*t.as.optional.inner, s); return (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional = { tk_clone_type(e) } }; }
        case TK_TYPE_REF: { tk_type e = tk_subst_type(*t.as.ref.inner, s); return (tk_type){ .tag = TK_TYPE_REF, .as.ref = { tk_clone_type(e) } }; }   // (MEM-1b) ref<T> substitutes inner
        case TK_TYPE_VARIANT: {
            size_t n = t.as.variant.len; tk_type *ms = tk_alloc((n ? n : 1) * sizeof *ms);
            for (size_t i = 0; i < n; i += 1) ms[i] = tk_subst_type(t.as.variant.members[i], s);
            return (tk_type){ .tag = TK_TYPE_VARIANT, .as.variant = { ms, n } };
        }
        case TK_TYPE_FUNC: {
            size_t n = t.as.func.nparams; tk_type *ps = n ? tk_alloc(n * sizeof *ps) : NULL;
            for (size_t i = 0; i < n; i += 1) ps[i] = tk_subst_type(t.as.func.params[i], s);
            tk_type ret = tk_subst_type(*t.as.func.ret, s);
            return (tk_type){ .tag = TK_TYPE_FUNC, .as.func = { ps, n, tk_clone_type(ret) } };
        }
        default: return t;   // Prim, Byte, Str, Error, Void, Ptr, Uptr
    }
}
tk_subst_result tk_unify(tk_type pattern, tk_type arg, tk_subst s, tk_type_table table) {
    (void)table;
    switch (pattern.tag) {
        case TK_TYPE_NAMED: {
            if (!tk_is_type_param(pattern.as.named.name, s.params, s.n_params)) return (tk_subst_result){ .ok = true, .as.value = s };
            if (type_has_void_sentinel_c(arg))
                return (tk_subst_result){ .ok = false, .as.error = tk_error_woven1("cannot infer type parameter ", pattern.as.named.name, " from an untyped empty slice / null — annotate the argument") };
            // (MEM Step 0, B) ESCAPE GATE — a reference can NEVER be the INFERRED binding for a type
            // parameter: `id(r)` where r: Ref<i64> would stamp `int64_t * id__g__ref_i64(int64_t *)`,
            // returning a raw reference that outlives its target. Mirror of the written-arg gate in
            // resolve_generic_inst (A); same message.
            if (arg.tag == TK_TYPE_REF)
                return (tk_subst_result){ .ok = false, .as.error = tk_error_make("a reference cannot be a generic type argument (it would be stored/escape)") };
            tk_type *ex = subst_find_c(s, pattern.as.named.name);
            if (ex) {
                if (tk_type_eq(ex, &arg)) return (tk_subst_result){ .ok = true, .as.value = s };
                return (tk_subst_result){ .ok = false, .as.error = tk_error_woven1("type parameter ", pattern.as.named.name, " inferred as conflicting types") };
            }
            size_t n = s.n_bind;
            tk_str  *nn = tk_alloc((n + 1) * sizeof *nn);
            tk_type *nt = tk_alloc((n + 1) * sizeof *nt);
            // copy the existing bindings, then append. The list push on the .tks side handles this;
            // here the explicit guard tells the analyzer that n>0 implies the arrays exist (a Subst
            // with n_bind>0 always carries allocated names/types — empty() is the only NULL case).
            if (s.names != NULL && s.types != NULL)
                for (size_t i = 0; i < n; i += 1) { nn[i] = s.names[i]; nt[i] = s.types[i]; }
            nn[n] = pattern.as.named.name; nt[n] = arg;
            return (tk_subst_result){ .ok = true, .as.value = { .params = s.params, .n_params = s.n_params, .names = nn, .types = nt, .n_bind = n + 1 } };
        }
        case TK_TYPE_SLICE:
            if (arg.tag == TK_TYPE_SLICE) return tk_unify(*pattern.as.slice.element, *arg.as.slice.element, s, table);
            return (tk_subst_result){ .ok = true, .as.value = s };
        case TK_TYPE_OPTIONAL:
            if (arg.tag == TK_TYPE_OPTIONAL) return tk_unify(*pattern.as.optional.inner, *arg.as.optional.inner, s, table);
            return (tk_subst_result){ .ok = true, .as.value = s };
        case TK_TYPE_REF:   // (MEM-1b)
            if (arg.tag == TK_TYPE_REF) return tk_unify(*pattern.as.ref.inner, *arg.as.ref.inner, s, table);
            return (tk_subst_result){ .ok = true, .as.value = s };
        default:
            return (tk_subst_result){ .ok = true, .as.value = s };
    }
}
void tk_collect_sig_type_params(tk_type t, tk_type_table table, tk_str **names, size_t *n) {
    switch (t.tag) {
        case TK_TYPE_NAMED: {
            tk_decl_result d = tk_type_table_find(table, t.as.named.name);
            if (d.ok) return;                                          // a user type
            if (tk_is_type_param(t.as.named.name, *names, *n)) return;  // dedup
            *names = tk_realloc0(*names, (*n + 1) * sizeof **names);
            (*names)[*n] = t.as.named.name; *n += 1;
            return;
        }
        case TK_TYPE_SLICE:    tk_collect_sig_type_params(*t.as.slice.element, table, names, n); return;
        case TK_TYPE_OPTIONAL: tk_collect_sig_type_params(*t.as.optional.inner, table, names, n); return;
        case TK_TYPE_REF:      tk_collect_sig_type_params(*t.as.ref.inner, table, names, n); return;   // (MEM-1b)
        case TK_TYPE_VARIANT:  for (size_t i = 0; i < t.as.variant.len; i += 1) tk_collect_sig_type_params(t.as.variant.members[i], table, names, n); return;
        case TK_TYPE_FUNC:
            for (size_t i = 0; i < t.as.func.nparams; i += 1) tk_collect_sig_type_params(t.as.func.params[i], table, names, n);
            tk_collect_sig_type_params(*t.as.func.ret, table, names, n); return;
        default: return;
    }
}

// non-static: shared with match.c (the typed pattern checker resolves case/struct names — C7).
tk_type_result resolve_named(tk_path path, tk_type_table table) {
    if (path.len == 0)   // M.1: an empty path is an internal invariant break — an honest error, never a crash
        return (tk_type_result){ .ok = false, .as.error = tk_error_make("internal: empty type path (a void/missing type used where a value type is required)") };
    tk_str name = path.segments[path.len - 1].name;       // seed: last segment
    tk_type_result bt = tk_builtin_type(name);            // u8…u64, byte, str, error
    if (bt.ok) return bt;
    tk_decl_result ut = tk_type_table_find(table, name);  // a user type
    if (ut.ok) {
        // A TRANSPARENT alias `type Name = <type-expr>` resolves THROUGH to the aliased type
        // (self-host parity): `TypeTable = []TypeReg` → a SLICE, `Foo = Circle` → its NAMED
        // struct, etc. struct/enum/variant decls stay NOMINAL (a bare NAMED of `name`).
        // A trivial self-alias chain (`type A = B; type B = A`) is broken by a depth bound:
        // tk_resolve_type → resolve_named re-enters here, so a runaway chain is finite-bounded.
        if (ut.as.value.body.tag == TK_BODY_ALIAS) {
            static int alias_depth = 0;
            if (alias_depth > 64)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("type alias resolves cyclically (self-referential alias chain)") };
            alias_depth += 1;
            tk_type_result r = tk_resolve_type(ut.as.value.body.as.alias_body.alias, table);
            alias_depth -= 1;
            return r;
        }
        // (TR0) a trait NEVER reaches a value position — it is a derivable bundle, not a type
        // for variables/params/returns/fields. Trait-typed DYNAMIC values arrive with TR1.
        if (ut.as.value.body.tag == TK_BODY_TRAIT) {
            size_t len = name.len + 128; char *buf = tk_alloc(len); if (!buf) abort();
            snprintf(buf, len, "trait '%.*s' is not a value type — derive it from a struct or class instead (trait-typed dynamic values arrive with TR1)", (int)name.len, (const char *)name.ptr);
            return (tk_type_result){ .ok = false, .as.error = tk_error_make(buf) };
        }
        // (W10b.D3) an interface IS a value type now — a contract-typed value (data + vtable fat
        // pointer, the tk_closure-shaped rep). It resolves NOMINALLY like any other named type;
        // the upcast/dispatch rules live in tk_widens_into + the method-call typer.
        tk_type t = { .tag = TK_TYPE_NAMED, .as.named.name = name };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown type", name) };
}

// (S4) a concrete type → its symbol fragment for a mangled generic-instance name. Mirror of
// resolve.tks::type_mangle. A SENTINEL slice/optional (NULL element/inner) mangles its hole as
// "void" (the C twin of the Teko `Void`-marker element), never dereferencing NULL.
tk_str tk_type_mangle(tk_type t) {
    switch (t.tag) {
        case TK_TYPE_PRIM:     return rt_cstr(prim_name(t.as.prim));
        case TK_TYPE_STR:      return rt_cstr("str");
        case TK_TYPE_BYTE:     return rt_cstr("byte");
        case TK_TYPE_CHAR:     return rt_cstr("char");
        case TK_TYPE_NAMED:    return t.as.named.name;
        case TK_TYPE_SLICE:    return rt_concat(rt_cstr("slice_"),
                                   t.as.slice.element  ? tk_type_mangle(*t.as.slice.element)  : rt_cstr("void"));
        case TK_TYPE_OPTIONAL: return rt_concat(rt_cstr("opt_"),
                                   t.as.optional.inner ? tk_type_mangle(*t.as.optional.inner) : rt_cstr("void"));
        case TK_TYPE_ERROR:    return rt_cstr("error");
        case TK_TYPE_VOID:     return rt_cstr("void");
        case TK_TYPE_PTR:      return t.as.ptr.inner ? rt_concat(rt_cstr("ptr_"), tk_type_mangle(*t.as.ptr.inner)) : rt_cstr("ptr");
        case TK_TYPE_UPTR:     return rt_cstr("uptr");
        case TK_TYPE_REF:      return rt_concat(rt_cstr("ref_"), tk_type_mangle(*t.as.ref.inner));   // (MEM-1b)
        case TK_TYPE_VARIANT:  return rt_cstr("variant");
        case TK_TYPE_FUNC:     return rt_cstr("func");
    }
    return rt_cstr("type");
}

// (S4) the mangled name of a generic INSTANCE: `<base>__g__<arg0>[__<arg1>…]`. Mirror of
// resolve.tks::generic_inst_name.
tk_str tk_generic_inst_name(tk_str base, tk_type *args, size_t nargs) {
    tk_str out = rt_concat(base, rt_cstr("__g__"));
    for (size_t i = 0; i < nargs; i += 1) {
        if (i > 0) out = rt_concat(out, rt_cstr("__"));
        out = rt_concat(out, tk_type_mangle(args[i]));
    }
    return out;
}

// (S4) `Box<i64>` → `Named{Box__g__i64}`: resolve the args, validate the generic decl's arity.
// Mirror of resolve.tks::resolve_generic_inst.
static tk_type_result resolve_generic_inst(tk_path path, tk_type_expr *args, size_t nargs, tk_type_table table) {
    tk_str name = path.segments[path.len - 1].name;
    // (S-mem) builtin generic `ptr<T>` → `Ptr{inner}`; `ptr<void>` ≡ opaque ptr → NULL inner.
    if (name.len == 3 && memcmp(name.ptr, "ptr", 3) == 0) {
        if (nargs != 1) return (tk_type_result){ .ok = false, .as.error = tk_error_make("`ptr<T>` takes exactly one type argument") };
        tk_type_result in = tk_resolve_type(args[0], table);
        if (!in.ok) return in;
        tk_type t = { .tag = TK_TYPE_PTR, .as.ptr.inner = (in.as.value.tag == TK_TYPE_VOID) ? NULL : box(in.as.value) };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    // (MEM-1b) compiler-known generic struct `Ref<T>` → the safe reference `Ref{inner}` (the safe
    // surface OVER raw `ptr<T>`). Capitalized. Never null/opaque; `Ref<void>` rejected, `Ref` needs an arg.
    if (name.len == 3 && memcmp(name.ptr, "Ref", 3) == 0) {
        if (nargs != 1) return (tk_type_result){ .ok = false, .as.error = tk_error_make("`Ref<T>` takes exactly one type argument") };
        tk_type_result in = tk_resolve_type(args[0], table);
        if (!in.ok) return in;
        if (in.as.value.tag == TK_TYPE_VOID) return (tk_type_result){ .ok = false, .as.error = tk_error_make("`Ref<void>` is invalid — void is not a value (M.3)") };
        // (W9a) ESCAPE GATE — the inner must be a VALUE TYPE. Teko has NO field/index assignment
        // (value-functional), so a non-scalar ref is ONLY ever mutated by WHOLE-`.value` REPLACEMENT
        // (`r.value = newWhole` — native `*r = newWhole` struct/slice copy; VM cell_set), and read by
        // `r.value` (native `(*r)`; VM cell_get). Under the cell-IDENTITY model both are observationally
        // identical in VM and native, exactly like scalars. So PRIM, BYTE, STR, NAMED (struct/enum/
        // variant), SLICE, OPTIONAL and VARIANT are all allowed. REJECT only the NON-values / aliasing
        // hazards: VOID (above — not a value), REF (no ref-to-ref), PTR/UPTR (raw FFI addresses), FUNC.
        switch (in.as.value.tag) {
            case TK_TYPE_REF:  return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot target another reference (`Ref<Ref<T>>` is invalid)") };
            case TK_TYPE_PTR:  return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot target a raw pointer (`Ref<ptr<T>>` is invalid — raw FFI addresses are not safe ref targets)") };
            case TK_TYPE_UPTR: return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot target a raw pointer (`Ref<uptr>` is invalid — raw FFI addresses are not safe ref targets)") };
            case TK_TYPE_FUNC: return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot target a function (`Ref<(…)->…>` is invalid)") };
            default: break;
        }
        tk_type t = { .tag = TK_TYPE_REF, .as.ref.inner = box(in.as.value) };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    tk_type *argtypes = nargs ? tk_alloc(nargs * sizeof *argtypes) : NULL;
    for (size_t i = 0; i < nargs; i += 1) {
        tk_type_result a = tk_resolve_type(args[i], table);
        if (!a.ok) return a;
        // (MEM Step 0, A) ESCAPE GATE — a reference can NEVER be a generic type ARGUMENT: carrying a
        // `Ref<T>` into a user generic would stamp it into a struct field / return position (e.g.
        // `Box<Ref<i64>>` → `struct { value: int64_t * }` returned by value) and escape. The template
        // itself is validated pre-instantiation (R1–R5); the only way a Reference enters a stamped
        // decl/fn is via a type-arg — blocked here (written) and in tk_unify (inferred).
        if (a.as.value.tag == TK_TYPE_REF)
            return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be a generic type argument (it would be stored/escape)") };
        argtypes[i] = a.as.value;
    }
    tk_decl_result d = tk_type_table_find(table, name);
    if (!d.ok)
        return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown generic type", name) };
    if (d.as.value.n_type_params == 0)
        return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("type `", name, "` is not generic but was given type arguments") };
    if (d.as.value.n_type_params != nargs)
        return (tk_type_result){ .ok = false, .as.error = tk_error_woven1("generic type `", name, "` expects a different number of type arguments") };
    tk_type t = { .tag = TK_TYPE_NAMED, .as.named.name = tk_generic_inst_name(name, argtypes, nargs) };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// ── (S4) type-generic INSTANTIATION pass — mirror of resolve.tks ─────────────────────────────────
// substitute type-parameter NAMES with their argument type-exprs throughout a (syntactic) type-expr.
static tk_type_expr subst_texpr_names(tk_type_expr te, tk_str *params, size_t nparams, tk_type_expr *args, size_t nargs) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            if (te.as.named.path.len == 1 && te.as.named.args_len == 0) {
                tk_str nm = te.as.named.path.segments[0].name;
                for (size_t k = 0; k < nparams; k += 1)
                    if (k < nargs && name_eq(params[k], nm)) return args[k];
            }
            tk_type_expr *na = NULL; size_t n2 = 0;
            for (size_t j = 0; j < te.as.named.args_len; j += 1)
                tk_types_push(&na, &n2, subst_texpr_names(te.as.named.args[j], params, nparams, args, nargs));
            return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = te.as.named.path, .args = na, .args_len = n2 } };
        }
        case TK_TEXPR_SLICE:
            if (!te.as.slice.element) return te;
            return (tk_type_expr){ .tag = TK_TEXPR_SLICE, .as.slice = { .element = tk_box_type(subst_texpr_names(*te.as.slice.element, params, nparams, args, nargs)) } };
        case TK_TEXPR_OPTIONAL:
            if (!te.as.optional.inner) return te;
            return (tk_type_expr){ .tag = TK_TEXPR_OPTIONAL, .as.optional = { .inner = tk_box_type(subst_texpr_names(*te.as.optional.inner, params, nparams, args, nargs)) } };
        case TK_TEXPR_UNION: {
            tk_type_expr *ms = NULL; size_t nm = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1)
                tk_types_push(&ms, &nm, subst_texpr_names(te.as.uni.members[i], params, nparams, args, nargs));
            return (tk_type_expr){ .tag = TK_TEXPR_UNION, .as.uni = { .members = ms, .len = nm } };
        }
        case TK_TEXPR_FUNC: {   // (W10a) recurse into params + return
            tk_type_expr *nps = NULL; size_t np = 0;
            for (size_t i = 0; i < te.as.func.nparams; i += 1)
                tk_types_push(&nps, &np, subst_texpr_names(te.as.func.params[i], params, nparams, args, nargs));
            tk_type_expr *nr = te.as.func.ret ? tk_box_type(subst_texpr_names(*te.as.func.ret, params, nparams, args, nargs)) : NULL;
            return (tk_type_expr){ .tag = TK_TEXPR_FUNC, .as.func = { .params = nps, .nparams = np, .ret = nr } };
        }
    }
    return te;
}

// substitute the type-params throughout a type-decl BODY (struct fields / variant / alias).
static tk_type_body subst_body_names(tk_type_body body, tk_str *params, size_t nparams, tk_type_expr *args, size_t nargs) {
    switch (body.tag) {
        case TK_BODY_STRUCT: {
            tk_field *nf = NULL; size_t n = 0;
            for (size_t i = 0; i < body.as.struct_body.n_fields; i += 1)
                tk_fields_push(&nf, &n, (tk_field){ .name = body.as.struct_body.fields[i].name,
                    .type_ann = subst_texpr_names(body.as.struct_body.fields[i].type_ann, params, nparams, args, nargs) });
            // methods/implements dropped on a stamped generic instance (mirrors resolve.tks — the
            // generic template's methods/implements aren't carried to the monomorphized body); NULL/0
            // are explicit for parity with the .tks `teko::list::empty()` (designated-init already 0s them).
            return (tk_type_body){ .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = nf, .n_fields = n, .methods = NULL, .n_methods = 0, .implements = NULL, .n_implements = 0 } };
        }
        case TK_BODY_VARIANT:
            return (tk_type_body){ .tag = TK_BODY_VARIANT, .as.variant_body = { .type_expr = subst_texpr_names(body.as.variant_body.type_expr, params, nparams, args, nargs) } };
        case TK_BODY_ALIAS:
            return (tk_type_body){ .tag = TK_BODY_ALIAS, .as.alias_body = { .alias = subst_texpr_names(body.as.alias_body.alias, params, nparams, args, nargs) } };
        default: return body;
    }
}

// collect every generic-type USE (a NamedType carrying args) reachable in a type-expr — innermost first.
static void collect_texpr_insts(tk_type_expr te, tk_type_expr **acc, size_t *n) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:
            for (size_t i = 0; i < te.as.named.args_len; i += 1) collect_texpr_insts(te.as.named.args[i], acc, n);
            if (te.as.named.args_len > 0) tk_types_push(acc, n, te);
            break;
        case TK_TEXPR_SLICE:    if (te.as.slice.element)  collect_texpr_insts(*te.as.slice.element, acc, n); break;
        case TK_TEXPR_OPTIONAL: if (te.as.optional.inner) collect_texpr_insts(*te.as.optional.inner, acc, n); break;
        case TK_TEXPR_UNION:    for (size_t i = 0; i < te.as.uni.len; i += 1) collect_texpr_insts(te.as.uni.members[i], acc, n); break;
        case TK_TEXPR_FUNC:     // (W10a) recurse into params + return
            for (size_t i = 0; i < te.as.func.nparams; i += 1) collect_texpr_insts(te.as.func.params[i], acc, n);
            if (te.as.func.ret) collect_texpr_insts(*te.as.func.ret, acc, n);
            break;
    }
}
static void collect_body_insts(tk_type_body body, tk_type_expr **acc, size_t *n) {
    switch (body.tag) {
        case TK_BODY_STRUCT:  for (size_t i = 0; i < body.as.struct_body.n_fields; i += 1) collect_texpr_insts(body.as.struct_body.fields[i].type_ann, acc, n); break;
        case TK_BODY_VARIANT: collect_texpr_insts(body.as.variant_body.type_expr, acc, n); break;
        case TK_BODY_ALIAS:   collect_texpr_insts(body.as.alias_body.alias, acc, n); break;
        default: break;
    }
}
static void collect_stmts_insts(tk_statement *stmts, size_t ns, tk_type_expr **acc, size_t *n);
static void collect_expr_insts(tk_expr e, tk_type_expr **acc, size_t *n);
static void collect_pattern_insts(tk_pattern p, tk_type_expr **acc, size_t *n);

// (W9.4) collect the generic instance a construction-site / bind-pattern type-arg list names: synthesize
// the NamedType `<path><args>` and run it through collect_texpr_insts (which pushes it AND recurses into
// the args), so `Foo<i64>{…}` / `Foo<i64> as x` get `Foo__g__i64` stamped exactly like an annotation.
static void collect_site_args(tk_path path, tk_type_expr *args, size_t nargs, tk_type_expr **acc, size_t *n) {
    if (nargs == 0) return;
    tk_type_expr gte = { .tag = TK_TEXPR_NAMED, .as.named = { .path = path, .args = args, .args_len = nargs } };
    collect_texpr_insts(gte, acc, n);
}

// (W9.4) walk an expression for construction-site type-args (`Foo<i64>{…}`) and nested patterns.
static void collect_expr_insts(tk_expr e, tk_type_expr **acc, size_t *n) {
    switch (e.tag) {
        case TK_EXPR_STRUCT_LIT:
            collect_site_args(e.as.struct_lit.type_path, e.as.struct_lit.type_args, e.as.struct_lit.nargs, acc, n);
            for (size_t i = 0; i < e.as.struct_lit.nfields; i += 1) collect_expr_insts(e.as.struct_lit.field_vals[i], acc, n);
            break;
        case TK_EXPR_BINARY:   collect_expr_insts(*e.as.binary.left, acc, n); collect_expr_insts(*e.as.binary.right, acc, n); break;
        case TK_EXPR_UNARY:    collect_expr_insts(*e.as.unary.operand, acc, n); break;
        case TK_EXPR_COMPARE:
            collect_expr_insts(*e.as.compare.first, acc, n);
            for (size_t i = 0; i < e.as.compare.nrest; i += 1) collect_expr_insts(*e.as.compare.rest[i].operand, acc, n);
            break;
        case TK_EXPR_CALL:     for (size_t i = 0; i < e.as.call.nargs; i += 1) collect_expr_insts(e.as.call.args[i], acc, n); break;
        case TK_EXPR_IF:
            collect_expr_insts(*e.as.if_expr.cond, acc, n);
            collect_stmts_insts(e.as.if_expr.then_blk, e.as.if_expr.nthen, acc, n);
            if (e.as.if_expr.has_else) collect_stmts_insts(e.as.if_expr.else_blk, e.as.if_expr.nelse, acc, n);
            break;
        case TK_EXPR_MATCH:
            collect_expr_insts(*e.as.match_expr.subject, acc, n);
            for (size_t i = 0; i < e.as.match_expr.narms; i += 1) {
                tk_arm a = e.as.match_expr.arms[i];
                collect_pattern_insts(a.pattern, acc, n);
                if (a.has_when) collect_expr_insts(a.guard, acc, n);
                collect_stmts_insts(a.body, a.nbody, acc, n);
            }
            break;
        case TK_EXPR_FIELD_ACCESS:      collect_expr_insts(*e.as.field_access.receiver, acc, n); break;
        case TK_EXPR_SAFE_FIELD_ACCESS: collect_expr_insts(*e.as.safe_field_access.receiver, acc, n); break;
        case TK_EXPR_COALESCE:          collect_expr_insts(*e.as.coalesce.left, acc, n); collect_expr_insts(*e.as.coalesce.right, acc, n); break;
        case TK_EXPR_METHOD_CALL:
            collect_expr_insts(*e.as.method_call.receiver, acc, n);
            for (size_t i = 0; i < e.as.method_call.nargs; i += 1) collect_expr_insts(e.as.method_call.args[i], acc, n);
            break;
        case TK_EXPR_SAFE_METHOD_CALL:   // (NP-OOP, issue #116) recv?.method(args)
            collect_expr_insts(*e.as.safe_method_call.receiver, acc, n);
            for (size_t i = 0; i < e.as.safe_method_call.nargs; i += 1) collect_expr_insts(e.as.safe_method_call.args[i], acc, n);
            break;
        case TK_EXPR_CAST:     collect_expr_insts(*e.as.cast.expr, acc, n); break;
        case TK_EXPR_INDEX:    collect_expr_insts(*e.as.index.receiver, acc, n); collect_expr_insts(*e.as.index.index, acc, n); break;
        case TK_EXPR_INTERP:
            for (size_t i = 0; i < e.as.interp.nholes; i += 1) {
                collect_expr_insts(e.as.interp.holes[i], acc, n);
                if (e.as.interp.specs && e.as.interp.specs[i].kind == TK_FSPEC_DYNAMIC)
                    for (size_t k = 0; k < e.as.interp.specs[i].ndyn_args; k++)
                        collect_expr_insts(e.as.interp.specs[i].dyn_args[k], acc, n);
            }
            break;
        case TK_EXPR_IN:
            collect_expr_insts(*e.as.in_expr.lhs, acc, n);
            for (size_t i = 0; i < e.as.in_expr.nelems; i += 1) collect_expr_insts(e.as.in_expr.elems[i], acc, n);
            break;
        case TK_EXPR_ARRAY:    for (size_t i = 0; i < e.as.array.nelements; i += 1) collect_expr_insts(*e.as.array.elements[i].expr, acc, n); break;
        default: break;   // leaves (NUMBER/VAR/STR/BYTE/BOOL/NULL/PATH) carry no type-args
    }
}

// (W9.4) walk a pattern for bind-pattern type-args (`Foo<i64> as x`, incl alt options).
static void collect_pattern_insts(tk_pattern p, tk_type_expr **acc, size_t *n) {
    if (p.tag == TK_PAT_BIND && !p.as.bind.is_slice)
        collect_site_args(p.as.bind.type_name, p.as.bind.type_args, p.as.bind.nargs, acc, n);
    else if (p.tag == TK_PAT_ALT)
        for (size_t i = 0; i < p.as.alt.n_options; i += 1) collect_pattern_insts(p.as.alt.options[i], acc, n);
}

static void collect_stmt_insts(tk_statement s, tk_type_expr **acc, size_t *n) {
    switch (s.tag) {
        case TK_STMT_BINDING:
            if (s.as.binding.has_type) collect_texpr_insts(s.as.binding.type_ann, acc, n);
            collect_expr_insts(s.as.binding.value, acc, n);
            break;
        case TK_STMT_ASSIGN:  collect_expr_insts(s.as.assign.value, acc, n); if (s.as.assign.kind == TK_ASSIGN_FIELD && s.as.assign.target != NULL) collect_expr_insts(*s.as.assign.target, acc, n); break;   // (#88) the FIELD LHS receiver may name generic instances
        case TK_STMT_RETURN:  if (s.as.ret.has_value) collect_expr_insts(s.as.ret.value, acc, n); break;
        case TK_STMT_EXPR:    collect_expr_insts(s.as.expr_stmt.expr, acc, n); break;
        case TK_STMT_LOOP:    collect_stmts_insts(s.as.loop_stmt.body, s.as.loop_stmt.nbody, acc, n); break;
        case TK_STMT_DEFER:   collect_stmts_insts(s.as.defer_stmt.body, s.as.defer_stmt.nbody, acc, n); break;
        default: break;
    }
}
static void collect_stmts_insts(tk_statement *stmts, size_t ns, tk_type_expr **acc, size_t *n) {
    for (size_t i = 0; i < ns; i += 1) collect_stmt_insts(stmts[i], acc, n);
}
static void collect_item_insts(tk_item it, tk_type_expr **acc, size_t *n) {
    switch (it.tag) {
        case TK_ITEM_FUNCTION: {
            tk_function f = it.as.function;
            for (size_t i = 0; i < f.nparams; i += 1) collect_texpr_insts(f.params[i].type_ann, acc, n);
            if (f.has_return) collect_texpr_insts(f.return_type, acc, n);
            collect_stmts_insts(f.body, f.nbody, acc, n);
            break;
        }
        case TK_ITEM_TYPE_DECL: collect_body_insts(it.as.type_decl.body, acc, n); break;
        case TK_ITEM_STATEMENT: collect_stmt_insts(it.as.statement, acc, n); break;
        default: break;
    }
}

// THE PASS: stamp concrete decls for written generic uses into the table (dedup by mangled name),
// scanning each stamped body for transitive instantiations until the worklist drains. No-op (the
// table is returned unchanged) when no generic type is used. Mirror of resolve.tks::instantiate_types.
static tk_type_table normalize_table_instances(tk_type_table table);   // (W9.4) forward decl (defined below)
tk_type_table tk_instantiate_types(tk_program program, tk_type_table table) {
    tk_type_expr *work = NULL; size_t nwork = 0;
    for (size_t i = 0; i < program.len; i += 1) collect_item_insts(program.items[i], &work, &nwork);
    if (nwork == 0) return table;
    tk_type_table tbl = table;
    for (size_t wi = 0; wi < nwork; wi += 1) {
        tk_type_expr te = work[wi];
        if (te.tag != TK_TEXPR_NAMED || te.as.named.args_len == 0) continue;
        tk_str name = te.as.named.path.segments[te.as.named.path.len - 1].name;
        tk_type *argtypes = te.as.named.args_len ? tk_alloc(te.as.named.args_len * sizeof *argtypes) : NULL;
        bool ok = true;
        for (size_t a = 0; a < te.as.named.args_len; a += 1) {
            tk_type_result r = tk_resolve_type(te.as.named.args[a], tbl);
            if (!r.ok) { ok = false; break; }
            argtypes[a] = r.as.value;
        }
        if (!ok) continue;
        tk_str mangled = tk_generic_inst_name(name, argtypes, te.as.named.args_len);
        if (tk_type_table_find(tbl, mangled).ok) continue;   // already stamped
        tk_decl_result gd = tk_type_table_find(tbl, name);
        if (!gd.ok) continue;                                // unknown base → reported at typing
        tk_type_decl gen = gd.as.value;
        if (gen.n_type_params != te.as.named.args_len) continue;   // arity error surfaces at resolve
        tk_type_body nbody = subst_body_names(gen.body, gen.type_params, gen.n_type_params, te.as.named.args, te.as.named.args_len);
        tk_type_decl stamped = { .name = mangled, .type_params = NULL, .n_type_params = 0, .body = nbody,
                                 .vis = gen.vis, .has_doc = false, .doc = (tk_str){0}, .line = gen.line, .col = gen.col };
        tbl = tk_type_table_push(tbl, (tk_type_reg){ .name = mangled, .namespace = (tk_str){0}, .vis = gen.vis, .decl = stamped });
        collect_body_insts(nbody, &work, &nwork);   // transitive instantiations in the stamped body
    }
    // (W9.4) normalize generic-INSTANCE references in the stamped bodies to bare stamped names, so
    // codegen / VM / TKB (reading the syntactic body) agree with the resolved value types.
    return normalize_table_instances(tbl);
}

// does `name` carry the `__g__` generic-instance infix? Mirror of resolve.tks::name_is_g_instance.
bool tk_name_is_g_instance(tk_str name) {
    if (name.len < 5) return false;
    for (size_t i = 0; i + 5 <= name.len; i += 1)
        if (name.ptr[i] == '_' && name.ptr[i+1] == '_' && name.ptr[i+2] == 'g' && name.ptr[i+3] == '_' && name.ptr[i+4] == '_')
            return true;
    return false;
}

// (S4) the concrete generic-type INSTANCE decls stamped into the table (`__g__`-marked entries), for
// the mono pass to emit as program items. Out-pointer list (Teko returns a slice). Mirror of
// resolve.tks::table_generic_instances.
void tk_table_generic_instances(tk_type_table table, tk_type_decl **out, size_t *n) {
    *out = NULL; *n = 0;
    for (size_t i = 0; i < table.len; i += 1) {
        if (tk_name_is_g_instance(table.ptr[i].name) && table.ptr[i].decl.n_type_params == 0) {
            *out = tk_realloc0(*out, (*n + 1) * sizeof **out); if (!*out) abort();
            (*out)[*n] = table.ptr[i].decl; *n += 1;
        }
    }
}

// (W9.4) NORMALIZE generic-INSTANCE references in a (syntactic) type-expr to the BARE stamped name.
// A `Base<args>` reference where `Base` resolves to a stamped `Base__g__<mangle>` user generic
// instance is rewritten to a bare `NAMED{[Base__g__mangle]}` with NO args, so codegen / VM / TKB
// (which read the SYNTACTIC declaration body) carry the SAME stamped name the RESOLVED value type
// carries. A type-PARAMETER (`T`, args_len 0) is NOT rewritten; builtin `ptr<T>`/`Ref<T>` (which
// resolve to PTR/REF, not a NAMED instance) keep their shape with args normalized recursively.
// NO-OP when the expr has no generic-instance reference. Mirror of resolve.tks::normalize_inst_texpr.
static tk_type_expr normalize_inst_texpr(tk_type_expr te, tk_type_table table) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            if (te.as.named.args_len > 0) {
                tk_type_result r = tk_resolve_type(te, table);
                if (r.ok && r.as.value.tag == TK_TYPE_NAMED && tk_name_is_g_instance(r.as.value.as.named.name)) {
                    tk_segment *segs = NULL; size_t ns = 0;
                    tk_segs_push(&segs, &ns, (tk_segment){ .name = r.as.value.as.named.name });
                    return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = { .segments = segs, .len = ns }, .args = NULL, .args_len = 0 } };
                }
            }
            tk_type_expr *na = NULL; size_t n2 = 0;
            for (size_t j = 0; j < te.as.named.args_len; j += 1)
                tk_types_push(&na, &n2, normalize_inst_texpr(te.as.named.args[j], table));
            return (tk_type_expr){ .tag = TK_TEXPR_NAMED, .as.named = { .path = te.as.named.path, .args = na, .args_len = n2 } };
        }
        case TK_TEXPR_SLICE:
            if (!te.as.slice.element) return te;
            return (tk_type_expr){ .tag = TK_TEXPR_SLICE, .as.slice = { .element = tk_box_type(normalize_inst_texpr(*te.as.slice.element, table)) } };
        case TK_TEXPR_OPTIONAL:
            if (!te.as.optional.inner) return te;
            return (tk_type_expr){ .tag = TK_TEXPR_OPTIONAL, .as.optional = { .inner = tk_box_type(normalize_inst_texpr(*te.as.optional.inner, table)) } };
        case TK_TEXPR_UNION: {
            tk_type_expr *ms = NULL; size_t nm = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1)
                tk_types_push(&ms, &nm, normalize_inst_texpr(te.as.uni.members[i], table));
            return (tk_type_expr){ .tag = TK_TEXPR_UNION, .as.uni = { .members = ms, .len = nm } };
        }
        case TK_TEXPR_FUNC: {   // (W10a) normalize params + return
            tk_type_expr *nps = NULL; size_t np = 0;
            for (size_t i = 0; i < te.as.func.nparams; i += 1)
                tk_types_push(&nps, &np, normalize_inst_texpr(te.as.func.params[i], table));
            tk_type_expr *nr = te.as.func.ret ? tk_box_type(normalize_inst_texpr(*te.as.func.ret, table)) : NULL;
            return (tk_type_expr){ .tag = TK_TEXPR_FUNC, .as.func = { .params = nps, .nparams = np, .ret = nr } };
        }
    }
    return te;
}

// (W9.4) normalize generic-instance references throughout a type-decl BODY. Mirror of normalize_inst_body.
static tk_type_body normalize_inst_body(tk_type_body body, tk_type_table table) {
    switch (body.tag) {
        case TK_BODY_STRUCT: {
            tk_field *nf = NULL; size_t n = 0;
            for (size_t i = 0; i < body.as.struct_body.n_fields; i += 1)
                tk_fields_push(&nf, &n, (tk_field){ .name = body.as.struct_body.fields[i].name,
                    .type_ann = normalize_inst_texpr(body.as.struct_body.fields[i].type_ann, table) });
            // methods/implements explicit NULL/0 for parity with the .tks (designated-init already 0s them).
            return (tk_type_body){ .tag = TK_BODY_STRUCT, .as.struct_body = { .fields = nf, .n_fields = n, .methods = NULL, .n_methods = 0, .implements = NULL, .n_implements = 0 } };
        }
        case TK_BODY_VARIANT:
            return (tk_type_body){ .tag = TK_BODY_VARIANT, .as.variant_body = { .type_expr = normalize_inst_texpr(body.as.variant_body.type_expr, table) } };
        case TK_BODY_ALIAS:
            return (tk_type_body){ .tag = TK_BODY_ALIAS, .as.alias_body = { .alias = normalize_inst_texpr(body.as.alias_body.alias, table) } };
        default: return body;
    }
}

// (W9.4) normalize generic-instance references throughout a type-decl. Used by the typer to rewrite
// BOTH the stamped instances (in the table) AND every non-generic decl item (e.g. `Wrap` whose
// variant member is `Gen<i64>`). NO-OP when the body has no generic-instance reference. Mirror of
// resolve.tks::normalize_inst_decl.
tk_type_decl tk_normalize_inst_decl(tk_type_decl d, tk_type_table table) {
    return (tk_type_decl){ .name = d.name, .type_params = d.type_params, .n_type_params = d.n_type_params,
                           .type_constraints = d.type_constraints,
                           .body = normalize_inst_body(d.body, table), .vis = d.vis,
                           .has_doc = d.has_doc, .doc = d.doc, .line = d.line, .col = d.col };
}

// (W9.4) normalize EVERY stamped generic-instance decl's body in the table, so tk_table_generic_instances
// (the mono pass emits these as program items) carries bare stamped names. Non-`__g__` entries pass
// through unchanged (templates + ordinary decls; ordinary decls are normalized as program ITEMS by the
// typer). Mirror of resolve.tks::normalize_table_instances.
static tk_type_table normalize_table_instances(tk_type_table table) {
    tk_type_table out = (tk_type_table){0};
    for (size_t i = 0; i < table.len; i += 1) {
        if (tk_name_is_g_instance(table.ptr[i].name)) {
            tk_type_decl nd = tk_normalize_inst_decl(table.ptr[i].decl, table);
            out = tk_type_table_push(out, (tk_type_reg){ .name = table.ptr[i].name, .namespace = table.ptr[i].namespace, .vis = table.ptr[i].vis, .decl = nd });
        } else {
            out = tk_type_table_push(out, table.ptr[i]);
        }
    }
    return out;
}

// A NAMED type referring to a `variant` decl → its expanded TK_TYPE_VARIANT. A variant decl's
// body IS a union type-expr (A | B | …), so tk_resolve_type on it yields the VARIANT with its
// members; the members resolve to NAMED (resolve_named keeps user types nominal), so this
// terminates and does not recurse into nested named variants. Anything else returns unchanged.
tk_type tk_expand_variant(tk_type t, tk_type_table table) {
    if (t.tag != TK_TYPE_NAMED) return t;
    tk_decl_result d = tk_type_table_find(table, t.as.named.name);
    if (!d.ok || d.as.value.body.tag != TK_BODY_VARIANT) return t;
    tk_type_result ex = tk_resolve_type(d.as.value.body.as.variant_body.type_expr, table);
    return ex.ok ? ex.as.value : t;
}

// (#41-followon) Does `from` WIDEN into `to`? The single source of truth for value-position
// widening, shared by: the return/trailing-value check (assignable_to delegates here) AND the
// match/`if` arm JOIN (via tk_type_join). The rules (B.14 + present-wrap, REBOOT_PLAN §202):
//   • exact type equality,
//   • a `T` into a `T?` (and a `U?`/sentinel into a `T?`),
//   • a variant MEMBER into the (named-expanded) variant — TRANSITIVELY: a member that is itself
//     a variant is expanded too, so a case of a NESTED variant widens in (`Named` → `Type` →
//     `Type | error`). Depth-bounded so a pathological self-referential variant cannot loop.
static bool widens_into_at(tk_type from, tk_type to, tk_type_table table, int depth) {
    if (tk_type_eq(&from, &to)) return true;
    if (depth <= 0) return false;
    // PRESENT-WRAP first (before variant decomposition): a whole `T` (even a named variant like
    // `TypeExpr`) wraps into `T?`. Checking this before the variant-from split keeps `TypeExpr →
    // TypeExpr?` a single present-wrap, not a member-by-member (which would wrongly fail).
    if (to.tag == TK_TYPE_OPTIONAL && to.as.optional.inner != NULL) {
        if (widens_into_at(from, *to.as.optional.inner, table, depth - 1)) return true;   // T (or a case of T) → T?
        if (from.tag == TK_TYPE_OPTIONAL) return true;              // U?/sentinel → T?
    }
    // A VARIANT `from` widens into `to` iff EVERY member widens in — a sub-variant into a wider
    // variant (`TExpr | NotListBuiltin` → `TExpr | NotListBuiltin | error`). Checked before the
    // member-of-`to` scan so a variant source is decomposed, not matched whole.
    tk_type fv = tk_expand_variant(from, table);
    if (fv.tag == TK_TYPE_VARIANT) {
        for (size_t i = 0; i < fv.as.variant.len; i += 1)
            if (!widens_into_at(fv.as.variant.members[i], to, table, depth - 1)) return false;
        return true;
    }
    // COVARIANT value slices: `[]A` widens into `[]B` iff A widens into B (value/copy semantics —
    // sound; the sentinel/NULL element is already accepted by tk_type_eq above). So `[]Str`
    // (push(empty(), str_t)) fits a `[]Type` field — the element is a case of the variant.
    if (from.tag == TK_TYPE_SLICE && to.tag == TK_TYPE_SLICE
        && from.as.slice.element != NULL && to.as.slice.element != NULL)
        return widens_into_at(*from.as.slice.element, *to.as.slice.element, table, depth - 1);
    // (W10b.D3) UPCAST — a CLASS instance widens into a contract (interface) it NOMINALLY
    // conforms to: `let s: Shape = circle`. Classes only (reference semantics ride the object
    // pointer into the fat-pointer `data` field); a STRUCT is pure value data — no stable
    // address to dispatch on — so it does NOT widen (its conformance still serves `<T: I>`).
    if (from.tag == TK_TYPE_NAMED && to.tag == TK_TYPE_NAMED
        && tk_is_class_name(from.as.named.name, table)
        && tk_is_interface_name(to.as.named.name, table)
        && tk_type_conforms_to(from.as.named.name, to.as.named.name, table))
        return true;
    // (#98) A CLASS instance ALSO widens into an ANCESTOR class (`let a: Animal = dog`) — the
    // Sub→Base upcast, reusing the SAME fat pointer (data + the base's virtual vtable). Nominal:
    // `from` must be a (transitive) subclass of `to`, and `to` must be a polymorphic base
    // (`abstract`/`virtual`) — a `sealed` class can never be a base, so it never widens into.
    if (from.tag == TK_TYPE_NAMED && to.tag == TK_TYPE_NAMED
        && !name_eq(from.as.named.name, to.as.named.name)
        && tk_is_class_name(from.as.named.name, table)
        && tk_is_polymorphic_base(to.as.named.name, table)
        && tk_subclass_reaches(from.as.named.name, to.as.named.name, table, 64))
        return true;
    tk_type tv = tk_expand_variant(to, table);   // a NAMED variant → its members (so cases widen in)
    if (tv.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < tv.as.variant.len; i += 1)
            if (widens_into_at(from, tv.as.variant.members[i], table, depth - 1)) return true;
    return false;
}
bool tk_widens_into(tk_type from, tk_type to, tk_type_table table) {
    return widens_into_at(from, to, table, 16);   // variant nesting is shallow; 16 is a safe backstop
}

// ── (W10b.D3) nominal conformance — the interface-value upcast / constraint gates ────────────────
// These live HERE (not collect.c) because tk_widens_into needs them and resolve sits below
// collect in the module DAG. "Contract" wording: the mechanism is per-(type, contract) and
// agnostic to which decl kind the contract came from (today `interface`; traits reuse it later).

// is `name` declared as an interface? Mirror of resolve.tks::is_interface_name.
bool tk_is_interface_name(tk_str name, tk_type_table table) {
    tk_decl_result d = tk_type_table_find(table, name);
    return d.ok && d.as.value.body.tag == TK_BODY_INTERFACE;
}

// is `name` declared as a class? Mirror of resolve.tks::is_class_name.
bool tk_is_class_name(tk_str name, tk_type_table table) {
    tk_decl_result d = tk_type_table_find(table, name);
    return d.ok && d.as.value.body.tag == TK_BODY_CLASS;
}

// (#98) is `name` a POLYMORPHIC BASE — a class that MAY be inherited (`abstract`/`virtual`, never
// `sealed`)? See resolve.tks::is_polymorphic_base for the rationale.
bool tk_is_polymorphic_base(tk_str name, tk_type_table table) {
    tk_decl_result d = tk_type_table_find(table, name);
    return d.ok && d.as.value.body.tag == TK_BODY_CLASS
        && d.as.value.body.as.class_body.kind != TK_CLASS_SEALED;
}

// (#98) does class `sub` reach ANCESTOR class `want` through the `base_name` chain, transitively?
// Hop-bounded (cyclic inheritance is decl-rejected). Mirror of resolve.tks::subclass_reaches.
bool tk_subclass_reaches(tk_str sub, tk_str want, tk_type_table table, int depth) {
    if (depth <= 0) return false;
    tk_decl_result d = tk_type_table_find(table, sub);
    if (!d.ok || d.as.value.body.tag != TK_BODY_CLASS) return false;
    tk_class_body cb = d.as.value.body.as.class_body;
    if (!cb.has_base) return false;
    if (name_eq(cb.base_name, want)) return true;
    return tk_subclass_reaches(cb.base_name, want, table, depth - 1);
}

// (TR0) is `name` declared as a trait? Mirror of resolve.tks::is_trait_name — used by the honest
// stops (trait as a constraint atom / instantiation target); the fold splits the `&`-list itself.
bool tk_is_trait_name(tk_str name, tk_type_table table) {
    tk_decl_result d = tk_type_table_find(table, name);
    return d.ok && d.as.value.body.tag == TK_BODY_TRAIT;
}

// does contract `sub` reach contract `want` through `extends`, transitively? Depth-bounded so a
// (decl-rejected) cyclic `extends` can never loop here. Mirror of resolve.tks::iface_extends_reaches.
static bool iface_extends_reaches(tk_str sub, tk_str want, tk_type_table table, int depth) {
    if (depth <= 0) return false;
    tk_decl_result d = tk_type_table_find(table, sub);
    if (!d.ok || d.as.value.body.tag != TK_BODY_INTERFACE) return false;
    tk_interface_body ib = d.as.value.body.as.interface_body;
    for (size_t i = 0; i < ib.n_extends; i += 1) {
        if (name_eq(ib.extends[i], want)) return true;
        if (iface_extends_reaches(ib.extends[i], want, table, depth - 1)) return true;
    }
    return false;
}

// NOMINAL conformance: does class/struct `name` implement contract `iface` — via its OWN
// `implements` list, an ANCESTOR class's (the class-inheritance chain), or a listed contract
// that `extends` `iface` transitively? DECLARED conformance only (nominal, closed-world) — a
// type that merely HAPPENS to have the methods does not conform. Hop-bounded like the extends
// walk (cyclic inheritance is decl-rejected; the bound keeps this total regardless). Mirror of
// resolve.tks::type_conforms_to.
bool tk_type_conforms_to(tk_str name, tk_str iface, tk_type_table table) {
    tk_str cur = name;
    for (int hops = 0; hops < 64; hops += 1) {
        tk_decl_result d = tk_type_table_find(table, cur);
        if (!d.ok) return false;
        const tk_str *impls; size_t n_impls;
        bool has_base = false; tk_str base = { NULL, 0 };
        if (d.as.value.body.tag == TK_BODY_CLASS) {
            impls = d.as.value.body.as.class_body.implements;
            n_impls = d.as.value.body.as.class_body.n_implements;
            has_base = d.as.value.body.as.class_body.has_base;
            base = d.as.value.body.as.class_body.base_name;
        } else if (d.as.value.body.tag == TK_BODY_STRUCT) {
            impls = d.as.value.body.as.struct_body.implements;
            n_impls = d.as.value.body.as.struct_body.n_implements;
        } else {
            return false;
        }
        for (size_t i = 0; i < n_impls; i += 1) {
            if (name_eq(impls[i], iface)) return true;
            if (iface_extends_reaches(impls[i], iface, table, 64)) return true;
        }
        if (!has_base) return false;
        cur = base;
    }
    return false;
}

// (#83) Does `t` carry a SENTINEL anywhere (a Slice with element == NULL, or an Optional with
// inner == NULL)? `tk_type_eq` treats a sentinel as equal to ANY concrete peer (§ above), which
// is right for ASSIGNABILITY but wrong for JOIN's "equal → return `a`" shortcut: if `a` is the
// (less informative) sentinel side and `b` is concrete, blindly returning `a` silently discards
// the concrete element type a sibling array-literal entry established (`[[], [1]]`'s inner `[]`
// joined against `[1]`'s `[]i64` — see tk_type_join). Recurses through Slice/Optional so a nested
// sentinel (`[][]NULL`) is still detected. Mirror: resolve.tks::type_has_sentinel.
bool tk_type_has_sentinel(const tk_type *t) {
    if (t->tag == TK_TYPE_SLICE)    return t->as.slice.element == NULL    || tk_type_has_sentinel(t->as.slice.element);
    if (t->tag == TK_TYPE_OPTIONAL) return t->as.optional.inner == NULL   || tk_type_has_sentinel(t->as.optional.inner);
    return false;
}

// (#41-followon) Collect `t`'s members into out[] for a union JOIN: an INLINE variant contributes
// its members (flattened); anything else (a named type/case, prim, …) contributes itself. Deduped
// by type_eq. Named variants stay NOMINAL (consistent with how `Type | error` keeps `Type` named).
#define TK_JOIN_MAX 64
static size_t union_collect(tk_type *out, size_t n, tk_type t, tk_type_table table) {
    if (t.tag == TK_TYPE_VARIANT) {
        for (size_t i = 0; i < t.as.variant.len; i += 1)
            n = union_collect(out, n, t.as.variant.members[i], table);
        return n;
    }
    for (size_t i = 0; i < n; i += 1) if (tk_type_eq(&out[i], &t)) return n;   // dedup
    if (n < TK_JOIN_MAX) out[n++] = t;
    return n;
}

// (#41-followon) The JOIN (least-upper-bound) of two arm/branch types, for match/`if` value
// unification (B.20). Symmetric widening: if either side widens into the other, the WIDER one is
// the result (`error` and `Type | error` join to `Type | error`). Equal types join to themselves.
// Returns false when neither widens into the other ("the arms have different types"). The result
// is written to *out only on success.
//
// (#83) "Equal" per tk_type_eq ALSO holds sentinel-vs-concrete (`[]NULL == []i64`, permissive by
// design — see tk_type_eq's TK_TYPE_SLICE/TK_TYPE_OPTIONAL cases). Blindly returning `a` in that
// branch discards a concrete element type when the SENTINEL happens to be the first operand: an
// array literal like `[[], [1]]` unifies its element type by folding `tk_type_join` left-to-right
// over `[]`'s `[]NULL` then `[1]`'s `[]i64`, and `a` (the accumulator) was the sentinel — the join
// silently kept `[]NULL`, so codegen later saw an untyped empty slice with no contextual element
// (issue #83; native honest-stopped while the VM, which ignores element types, ran fine). Prefer
// the CONCRETE side when the two are sentinel-equal but not tk_type_has_sentinel-equal.
bool tk_type_join(tk_type a, tk_type b, tk_type_table table, tk_type *out) {
    if (tk_type_eq(&a, &b)) {
        bool a_sentinel = tk_type_has_sentinel(&a);
        bool b_sentinel = tk_type_has_sentinel(&b);
        *out = (a_sentinel && !b_sentinel) ? b : a;   // prefer the concrete side
        return true;
    }
    if (tk_widens_into(a, b, table))   { *out = b; return true; }   // a is a case of b → b
    if (tk_widens_into(b, a, table))   { *out = a; return true; }   // b is a case of a → a
    // SIBLINGS (neither a case of the other): the join is the UNION variant `a | b` — the branch
    // values are bare cases that each widen into this union, which in turn widens into any wider
    // declared return variant. (Anonymous; native codegen lowers it via inline-union support.)
    tk_type tmp[TK_JOIN_MAX]; size_t n = 0;
    n = union_collect(tmp, n, a, table);
    n = union_collect(tmp, n, b, table);
    tk_type *m = tk_alloc(n * sizeof *m); if (!m) abort();
    for (size_t i = 0; i < n; i += 1) m[i] = tmp[i];
    *out = (tk_type){ .tag = TK_TYPE_VARIANT, .as.variant = { m, n } };
    return true;   // always joinable now (worst case: an explicit union)
}

tk_type_result tk_resolve_type(tk_type_expr te, tk_type_table table) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:
            // A plain name resolves as before; generic type-ARGUMENTS `Box<i64>` (S4) resolve to the
            // nominal instance `Named{Box__g__i64}` (the concrete decl is stamped by the pass / mono).
            if (te.as.named.args_len == 0)
                return resolve_named(te.as.named.path, table);
            return resolve_generic_inst(te.as.named.path, te.as.named.args, te.as.named.args_len, table);
        case TK_TEXPR_SLICE: {
            tk_type_result el = tk_resolve_type(*te.as.slice.element, table);
            if (!el.ok) return el;
            // (MEM Step 0, R4) ESCAPE GATE — a reference cannot be a collection element.
            if (el.as.value.tag == TK_TYPE_REF)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
            tk_type t = { .tag = TK_TYPE_SLICE, .as.slice.element = box(el.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_UNION: {
            tk_type *members = NULL; size_t n = 0;
            for (size_t i = 0; i < te.as.uni.len; i += 1) {
                tk_type_result m = tk_resolve_type(te.as.uni.members[i], table);
                if (!m.ok) { tk_free0(members); return m; }
                // M.1/rule 2 & 3: a variant member must be a COMPLETE type — never
                // `void` (not a value) and never nullable (`T?` — disjoint domain).
                if (m.as.value.tag == TK_TYPE_VOID) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be `void`") };
                }
                if (m.as.value.tag == TK_TYPE_OPTIONAL) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be nullable (`T?`) — use `T | …` and mark the whole type `?`") };
                }
                // (MEM Step 0, R4) ESCAPE GATE — a reference cannot be a variant member.
                if (m.as.value.tag == TK_TYPE_REF) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be stored in a struct/variant/collection") };
                }
                // (W10b.D3) an interface (contract) variant member is an honest stop: the VM's
                // untagged values discriminate a match arm by the CONCRETE class name, so a
                // `Shape | error` subject would silently diverge between the engines (native
                // has a tag, the VM does not). Plain interface-typed values work — bind first.
                if (m.as.value.tag == TK_TYPE_NAMED && tk_is_interface_name(m.as.value.as.named.name, table)) {
                    tk_free0(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("an interface cannot be a variant member yet — bind the interface value on its own (no `I | error` unions)") };
                }
                members = tk_realloc0(members, (n + 1) * sizeof *members);
                if (!members) abort();
                members[n] = m.as.value; n += 1;
            }
            tk_type t = { .tag = TK_TYPE_VARIANT, .as.variant = { members, n } };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_FUNC: {
            // (W10a) FunctionType `(A, B) -> R` → TK_TYPE_FUNC{ params; ret }. The return MAY be
            // `void` (a function value can return nothing); params must be COMPLETE value types
            // (void/ref rejected — a ref cannot escape, R4; void is not a value, M.3).
            size_t np = te.as.func.nparams; tk_type *ps = np ? tk_alloc(np * sizeof *ps) : NULL;
            for (size_t i = 0; i < np; i += 1) {
                tk_type_result p = tk_resolve_type(te.as.func.params[i], table);
                if (!p.ok) { tk_free0(ps); return p; }
                if (p.as.value.tag == TK_TYPE_VOID) { tk_free0(ps); return (tk_type_result){ .ok = false, .as.error = tk_error_make("a function type parameter may not be `void` (M.3)") }; }
                if (p.as.value.tag == TK_TYPE_REF)  { tk_free0(ps); return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be a function-type parameter (a ref cannot escape, R4)") }; }
                ps[i] = p.as.value;
            }
            tk_type_result rt = tk_resolve_type(*te.as.func.ret, table);
            if (!rt.ok) { tk_free0(ps); return rt; }
            tk_type t = { .tag = TK_TYPE_FUNC, .as.func = { ps, np, box(rt.as.value) } };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
        case TK_TEXPR_OPTIONAL: {
            // OptionalType `T?` → TK_TYPE_OPTIONAL{ inner } (REBOOT_PLAN §202). The
            // variant-member-not-nullable rule (above, UNION case) still rejects an
            // optional as a variant member; here we just build the nullable type.
            tk_type_result in = tk_resolve_type(*te.as.optional.inner, table);
            if (!in.ok) return in;
            // an optional of optional collapses (`T??` == `T?`), and `void?` is illegal
            // (void is not a value — M.3); both guarded for honesty.
            if (in.as.value.tag == TK_TYPE_VOID)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("`void` cannot be made optional (`void?`) — void is not a value (M.3)") };
            // (MEM Step 0, R2) ESCAPE GATE — a reference is NEVER null, so `Ref<T>?` is rejected.
            if (in.as.value.tag == TK_TYPE_REF)
                return (tk_type_result){ .ok = false, .as.error = tk_error_make("a reference cannot be nullable (a ref is never null, R2)") };
            if (in.as.value.tag == TK_TYPE_OPTIONAL)
                return in;   // `T??` collapses to `T?`
            tk_type t = { .tag = TK_TYPE_OPTIONAL, .as.optional.inner = box(in.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("bad type expr") };
}
