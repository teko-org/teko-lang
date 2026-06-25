// src/checker/resolve.c
#include "resolve.h"
#include <string.h>
#include <stdlib.h>

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}
// box a Type onto the heap — the compiler-managed indirection, made concrete.
static tk_type *box(tk_type t) {
    tk_type *p = malloc(sizeof *p);
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
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("unknown type") };
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
                if (!m.ok) { free(members); return m; }
                // M.1/rule 2 & 3: a variant member must be a COMPLETE type — never
                // `void` (not a value) and never nullable (`T?` — disjoint domain).
                if (m.as.value.tag == TK_TYPE_VOID) {
                    free(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be `void`") };
                }
                if (m.as.value.tag == TK_TYPE_OPTIONAL) {
                    free(members);
                    return (tk_type_result){ .ok = false, .as.error = tk_error_make("a variant member may not be nullable (`T?`) — use `T | …` and mark the whole type `?`") };
                }
                members = realloc(members, (n + 1) * sizeof *members);
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
