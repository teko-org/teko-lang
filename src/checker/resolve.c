// src/checker/resolve.c
#include "resolve.h"
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
        case TK_TYPE_PTR:   return dup_cstr("ptr");    // (C7.1a) opaque FFI pointer
        case TK_TYPE_UPTR:  return dup_cstr("uptr");   // (C7.1a) opaque word-size unsigned
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
        tk_type t = { .tag = TK_TYPE_NAMED, .as.named.name = name };
        return (tk_type_result){ .ok = true, .as.value = t };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_named("unknown type", name) };
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
    tk_type tv = tk_expand_variant(to, table);   // a NAMED variant → its members (so cases widen in)
    if (tv.tag == TK_TYPE_VARIANT)
        for (size_t i = 0; i < tv.as.variant.len; i += 1)
            if (widens_into_at(from, tv.as.variant.members[i], table, depth - 1)) return true;
    return false;
}
bool tk_widens_into(tk_type from, tk_type to, tk_type_table table) {
    return widens_into_at(from, to, table, 16);   // variant nesting is shallow; 16 is a safe backstop
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
bool tk_type_join(tk_type a, tk_type b, tk_type_table table, tk_type *out) {
    if (tk_type_eq(&a, &b))            { *out = a; return true; }
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
            return resolve_named(te.as.named.path, table);
        case TK_TEXPR_SLICE: {
            tk_type_result el = tk_resolve_type(*te.as.slice.element, table);
            if (!el.ok) return el;
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
                members = tk_realloc0(members, (n + 1) * sizeof *members);
                if (!members) abort();
                members[n] = m.as.value; n += 1;
            }
            tk_type t = { .tag = TK_TYPE_VARIANT, .as.variant = { members, n } };
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
            if (in.as.value.tag == TK_TYPE_OPTIONAL)
                return in;   // `T??` collapses to `T?`
            tk_type t = { .tag = TK_TYPE_OPTIONAL, .as.optional.inner = box(in.as.value) };
            return (tk_type_result){ .ok = true, .as.value = t };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("bad type expr") };
}
