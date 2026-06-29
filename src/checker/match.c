// src/checker/match.c
#include "match.h"
#include <string.h>

// from typer.c — the typed expression pass + the struct field resolver
// (forward-declared, both non-static, to avoid a match↔typer cycle).
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table);
tk_type_result  field_type(tk_struct_body sb, tk_str field, tk_type_table table);
bool tk_literal_adopts(tk_texpr e, tk_type to);   // from expr.c — numeric-literal adoption (byte/int)

static tk_env_result eok(tk_env e)     { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result efail(tk_error e) { return (tk_env_result){ .ok = false, .as.error = e }; }

static bool name_eq(tk_str a, tk_str b);   // fwd (defined with the exhaustiveness helpers below)
static tk_type_result bind_pattern_type(tk_pattern p, tk_type_table table);   // fwd (W9.4 — used by tk_check_pattern)

// ENUM-subject pattern check (C7b): a bare/qualified path pattern names a MEMBER of the enum —
// enum members carry NO data, so they bind nothing. `U8` and `PrimKind::U8` both name member "U8"
// (resolved by the LAST path segment, like the `Type::Member` enum-value form in expr.c). `_`
// matches anything; an Alt (`U8 | U16`) checks each option; `as x` binds the whole enum value.
static bool enum_has_member(tk_enum_body eb, tk_str name) {
    for (size_t i = 0; i < eb.n_members; i += 1) if (name_eq(eb.members[i], name)) return true;
    return false;
}
static tk_env_result check_enum_pattern(tk_pattern p, tk_type subject, tk_enum_body eb, tk_env env, tk_type_table table) {
    switch (p.tag) {
        case TK_PAT_WILDCARD: return eok(env);
        case TK_PAT_BIND: {
            if (p.as.bind.is_slice) return efail(tk_error_make("a slice pattern cannot match an enum value"));
            tk_str name = p.as.bind.type_name.segments[p.as.bind.type_name.len - 1].name;
            if (!enum_has_member(eb, name)) return efail(tk_error_named("not a member of the enum", name));
            if (p.as.bind.has_binding) return eok(tk_env_define(env, p.as.bind.binding, subject, false));   // `as x` — the enum value
            return eok(env);   // bare member — binds nothing
        }
        case TK_PAT_ALT: {
            for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
                tk_pattern opt = p.as.alt.options[i];
                if (opt.tag == TK_PAT_BIND && opt.as.bind.has_binding)
                    return efail(tk_error_make("an alternative (`|`) cannot bind; use a separate arm"));
                tk_env_result r = check_enum_pattern(opt, subject, eb, env, table);
                if (!r.ok) return efail(r.as.error);
            }
            return eok(env);   // binds nothing
        }
        default: return efail(tk_error_make("an enum subject needs member patterns (`U8`) or `_`"));
    }
}

// env | error: validate a pattern, extend the env.
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table) {
    // OPTIONAL subject `T?` (REBOOT_PLAN §202): `null` matches the NONE case (binds nothing);
    // any OTHER pattern matches the PRESENT case and is checked against the inner `T`.
    if (subject.tag == TK_TYPE_OPTIONAL) {
        if (p.tag == TK_PAT_NULL) return eok(env);   // NONE — binds nothing
        return tk_check_pattern(p, *subject.as.optional.inner, env, table);   // PRESENT — check vs inner
    }
    // ENUM subject (C7b): member patterns, not type binds. Detected before the generic switch so a
    // bare `U8` is not mis-resolved as a type name (resolve_named → "unknown type: U8").
    if (subject.tag == TK_TYPE_NAMED) {
        tk_decl_result d = tk_type_table_find(table, subject.as.named.name);
        if (d.ok && d.as.value.body.tag == TK_BODY_ENUM)
            return check_enum_pattern(p, subject, d.as.value.body.as.enum_body, env, table);
    }
    switch (p.tag) {
        case TK_PAT_NULL:
            return efail(tk_error_make("`null` pattern requires an optional subject (`T?`) — REBOOT_PLAN §202"));
        case TK_PAT_WILDCARD: return eok(env);
        case TK_PAT_BIND: {
            // `[]T as x` resolves the slice TYPE-expr; a path bind resolves the named/builtin case.
            // (W9.4) `Foo<i64> as x` carries explicit type-args → bind_pattern_type resolves the
            // concrete instance `Foo__g__i64`, so the binding has the stamped type (fields visible).
            tk_type_result ct = bind_pattern_type(p, table);
            if (!ct.ok) return efail(ct.as.error);
            if (!p.as.bind.has_binding) return eok(env);   // bare case / `as _` discard — binds nothing
            return eok(tk_env_define(env, p.as.bind.binding, ct.as.value, false));
        }
        case TK_PAT_LITERAL: {
            tk_texpr_result lt = tk_typer_expr(p.as.literal.value, env, table);
            if (!lt.ok) return efail(lt.as.error);
            // a numeric literal pattern adopts the subject's numeric type, so `match b { 0xE0 => … }`
            // matches a `byte` subject (byte = u8) — parity with the value-position literal adoption.
            if (!tk_type_eq(&lt.as.value.type, &subject) && !tk_literal_adopts(lt.as.value, subject))
                return efail(tk_error_make("literal pattern does not match the subject type"));
            return eok(env);
        }
        case TK_PAT_FIELD: {
            // C7a: variant axis `Type { f; g }` — resolve to a struct, bind each field IMMUTABLE (B.21).
            tk_type_result nt = resolve_named(p.as.field.type_name, table);
            if (!nt.ok) return efail(nt.as.error);
            if (nt.as.value.tag != TK_TYPE_NAMED) return efail(tk_error_make("field pattern requires a struct type"));
            tk_decl_result decl = tk_type_table_find(table, nt.as.value.as.named.name);
            if (!decl.ok) return efail(tk_error_make("unknown type in field pattern"));
            if (decl.as.value.body.tag != TK_BODY_STRUCT) return efail(tk_error_make("type is not a struct (no fields)"));
            tk_struct_body sb = decl.as.value.body.as.struct_body;
            tk_env e2 = env;
            for (size_t i = 0; i < p.as.field.n_fields; i += 1) {
                tk_type_result ft = field_type(sb, p.as.field.fields[i], table);
                if (!ft.ok) return efail(ft.as.error);
                e2 = tk_env_define(e2, p.as.field.fields[i], ft.as.value, false);   // B.21
            }
            return eok(e2);
        }
        case TK_PAT_RANGE: {
            // C7a: `lo ..= hi` — both bounds type_eq the subject, AND the subject is an integer (M.1/M.2).
            tk_texpr_result lo = tk_typer_expr(p.as.range.lo, env, table);
            if (!lo.ok) return efail(lo.as.error);
            tk_texpr_result hi = tk_typer_expr(p.as.range.hi, env, table);
            if (!hi.ok) return efail(hi.as.error);
            // bounds adopt the subject's numeric type (so `0xC2..=0xDF` matches a `byte` subject).
            if (!tk_type_eq(&lo.as.value.type, &subject) && !tk_literal_adopts(lo.as.value, subject)) return efail(tk_error_make("range lower bound does not match the subject type"));
            if (!tk_type_eq(&hi.as.value.type, &subject) && !tk_literal_adopts(hi.as.value, subject)) return efail(tk_error_make("range upper bound does not match the subject type"));
            // an integer subject — an int prim OR `byte` (= u8); a float range is rejected (B.38).
            if (!((subject.tag == TK_TYPE_PRIM && tk_prim_is_int(subject.as.prim)) || subject.tag == TK_TYPE_BYTE))
                return efail(tk_error_make("range pattern requires an integer subject (B.38 — not a float)"));
            return eok(env);   // binds nothing
        }
        case TK_PAT_ALT: {
            // C7a: `a | b | …` — each option checked; NO option may bind (settled axis rule).
            for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
                tk_pattern opt = p.as.alt.options[i];
                if (opt.tag == TK_PAT_BIND && opt.as.bind.has_binding)
                    return efail(tk_error_make("an alternative (`|`) cannot bind; use a separate arm"));
                if (opt.tag == TK_PAT_FIELD)
                    return efail(tk_error_make("an alternative (`|`) cannot bind; use a separate arm"));
                tk_env_result r = tk_check_pattern(opt, subject, env, table);   // recurse; discard env — Alt binds nothing
                if (!r.ok) return efail(r.as.error);
            }
            return eok(env);   // binds nothing
        }
    }
    return eok(env);
}

// --- exhaustiveness (B.15) ---

// an UNGUARDED `_` covers everything; a guarded `_ when g` does NOT (B.15 — `when` excluded).
static bool has_wildcard(tk_arm *arms, size_t n) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && arms[i].pattern.tag == TK_PAT_WILDCARD) return true;
    return false;
}
static bool name_eq(tk_str a, tk_str b) { return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0); }
// Resolve the type a BIND pattern denotes: `[]T as x` → the slice type, else the named/builtin path.
// (W9.4) `Foo<i64> as x` carries explicit type-args → resolve the concrete instance `Foo__g__i64`
// (validates arity / rejects non-generic-with-args), so exhaustiveness/binding use the stamped type.
static tk_type_result bind_pattern_type(tk_pattern p, tk_type_table table) {
    if (p.as.bind.is_slice) return tk_resolve_type(*p.as.bind.slice_type, table);
    if (p.as.bind.nargs > 0) {
        tk_type_expr gte = { .tag = TK_TEXPR_NAMED, .as.named = { .path = p.as.bind.type_name, .args = p.as.bind.type_args, .args_len = p.as.bind.nargs } };
        return tk_resolve_type(gte, table);
    }
    return resolve_named(p.as.bind.type_name, table);
}

// (W9.4) the case-name a single bind names: the resolved INSTANCE name `Gen__g__i64` when it carries
// explicit type-args, else the bare last path segment — so exhaustiveness over a `variant Gen<i64> | …`
// covers the `Gen<i64> as x` arm by the member's mangled name. Mirror of match.tks::bind_case_name.
static tk_str bind_case_name(tk_bind_pattern bp, tk_type_table table) {
    if (bp.nargs > 0) {
        tk_type_expr gte = { .tag = TK_TEXPR_NAMED, .as.named = { .path = bp.type_name, .args = bp.type_args, .args_len = bp.nargs } };
        tk_type_result r = tk_resolve_type(gte, table);
        if (r.ok && r.as.value.tag == TK_TYPE_NAMED) return r.as.value.as.named.name;
    }
    return bp.type_name.segments[bp.type_name.len - 1].name;
}

// does pattern `p` name case `name` — directly (Bind/Field) or via a bare Alt option?
// A slice bind (`[]T as x`) names NO nominal case (its type_name is empty) — it is covered by
// some_arm_covers_type instead, so it is skipped here (and the empty-path read is avoided).
static bool pattern_names(tk_pattern p, tk_str name, tk_type_table table) {
    if (p.tag == TK_PAT_BIND)
        return !p.as.bind.is_slice && name_eq(bind_case_name(p.as.bind, table), name);
    if (p.tag == TK_PAT_FIELD)
        return name_eq(p.as.field.type_name.segments[p.as.field.type_name.len - 1].name, name);
    if (p.tag == TK_PAT_ALT) {
        for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
            tk_pattern opt = p.as.alt.options[i];
            if (opt.tag == TK_PAT_BIND  && !opt.as.bind.is_slice && name_eq(bind_case_name(opt.as.bind, table), name)) return true;
            if (opt.tag == TK_PAT_FIELD && name_eq(opt.as.field.type_name.segments[opt.as.field.type_name.len - 1].name, name)) return true;
        }
    }
    return false;
}
// Does some UNGUARDED arm cover variant member `mem` by resolved-TYPE equality? This covers the
// non-nominal members of a `T | error` style variant — prim/byte/str/slice/error — where the
// arm is a bind whose resolved type equals the member (e.g. `[]byte as o` ⇒ []byte, `error as e`
// ⇒ error, `i64 as v` ⇒ i64). NAMED (nominal) members keep the name-based some_arm_names path.
static bool some_arm_covers_type(tk_arm *arms, size_t n, tk_type mem, tk_type_table table) {
    for (size_t i = 0; i < n; i += 1) {
        if (arms[i].has_when) continue;
        tk_pattern p = arms[i].pattern;
        if (p.tag == TK_PAT_BIND) {
            tk_type_result rt = bind_pattern_type(p, table);
            if (rt.ok && tk_type_eq(&rt.as.value, &mem)) return true;
        } else if (p.tag == TK_PAT_ALT) {
            for (size_t j = 0; j < p.as.alt.n_options; j += 1) {
                tk_pattern opt = p.as.alt.options[j];
                if (opt.tag != TK_PAT_BIND) continue;
                tk_type_result rt = bind_pattern_type(opt, table);
                if (rt.ok && tk_type_eq(&rt.as.value, &mem)) return true;
            }
        }
    }
    return false;
}
// is `name` covered by some UNGUARDED arm?
static bool some_arm_names(tk_arm *arms, size_t n, tk_str name, tk_type_table table) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && pattern_names(arms[i].pattern, name, table)) return true;
    return false;
}
// is the NONE case (a `null` pattern) covered by some UNGUARDED arm?
static bool some_arm_is_null(tk_arm *arms, size_t n) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && arms[i].pattern.tag == TK_PAT_NULL) return true;
    return false;
}
// is the PRESENT case of an optional `T?` covered? `inner` is the `T`. The present case is
// covered by some UNGUARDED non-`null` pattern that is exhaustive over the inner: a wildcard,
// a bare bind (`T as x` / `T`) over the inner, OR (recursively) the inner's own exhaustiveness
// when the inner is a variant. We re-run tk_exhaustive over the non-null arms with the inner
// subject, but a bare BIND over a SCALAR inner does count as present-covering here.
static bool present_case_covered(tk_arm *arms, size_t n, tk_type inner, tk_type_table table) {
    // A bare bind/wildcard arm (un-`null`, unguarded) covers the whole present value.
    for (size_t i = 0; i < n; i += 1) {
        if (arms[i].has_when) continue;
        tk_pattern p = arms[i].pattern;
        if (p.tag == TK_PAT_WILDCARD) return true;
        if (p.tag == TK_PAT_BIND && p.as.bind.type_name.len > 0) return true;   // `T as x` / `T` binds the present value
    }
    // Otherwise fall back to ordinary exhaustiveness over the inner (e.g. a variant inner).
    return tk_exhaustive(arms, n, inner, table);
}
bool tk_exhaustive(tk_arm *arms, size_t n, tk_type subject, tk_type_table table) {
    if (has_wildcard(arms, n)) return true;
    if (subject.tag == TK_TYPE_NAMED) {   // an ENUM subject is exhaustive iff EVERY member is named (C7b)
        tk_decl_result d = tk_type_table_find(table, subject.as.named.name);
        if (d.ok && d.as.value.body.tag == TK_BODY_ENUM) {
            tk_enum_body eb = d.as.value.body.as.enum_body;
            for (size_t i = 0; i < eb.n_members; i += 1)
                if (!some_arm_names(arms, n, eb.members[i], table)) return false;
            return true;
        }
    }
    if (subject.tag == TK_TYPE_OPTIONAL) {   // `T?` — exhaustive iff `null` AND the present case are covered (REBOOT §202)
        return some_arm_is_null(arms, n) && present_case_covered(arms, n, *subject.as.optional.inner, table);
    }
    tk_type sv = tk_expand_variant(subject, table);   // a NAMED variant → its TK_TYPE_VARIANT cases (B.14/B.15)
    if (sv.tag != TK_TYPE_VARIANT) return false;
    for (size_t i = 0; i < sv.as.variant.len; i += 1) {
        tk_type mem = sv.as.variant.members[i];
        // NAMED (nominal) members match by name; non-nominal members (prim/byte/str/slice/error —
        // the `T | error` idiom) match by resolved-type equality against a bind arm.
        if (mem.tag == TK_TYPE_NAMED) {
            if (!some_arm_names(arms, n, mem.as.named.name, table)) return false;
        } else {
            if (!some_arm_covers_type(arms, n, mem, table)) return false;
        }
    }
    return true;
}
