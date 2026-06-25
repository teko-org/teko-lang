// src/checker/match.c
#include "match.h"
#include <string.h>

// from typer.c — the typed expression pass + the struct field resolver
// (forward-declared, both non-static, to avoid a match↔typer cycle).
tk_texpr_result tk_typer_expr(tk_expr e, tk_env env, tk_type_table table);
tk_type_result  field_type(tk_struct_body sb, tk_str field, tk_type_table table);

static tk_env_result eok(tk_env e)     { return (tk_env_result){ .ok = true,  .as.value = e }; }
static tk_env_result efail(tk_error e) { return (tk_env_result){ .ok = false, .as.error = e }; }

// env | error: validate a pattern, extend the env.
tk_env_result tk_check_pattern(tk_pattern p, tk_type subject, tk_env env, tk_type_table table) {
    switch (p.tag) {
        case TK_PAT_WILDCARD: return eok(env);
        case TK_PAT_BIND: {
            tk_type_result ct = resolve_named(p.as.bind.type_name, table); // see resolve.c
            if (!ct.ok) return efail(ct.as.error);
            return eok(tk_env_define(env, p.as.bind.binding, ct.as.value, false));
        }
        case TK_PAT_LITERAL: {
            tk_texpr_result lt = tk_typer_expr(p.as.literal.value, env, table);
            if (!lt.ok) return efail(lt.as.error);
            if (!tk_type_eq(&lt.as.value.type, &subject)) return efail(tk_error_make("literal pattern does not match the subject type"));
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
            if (!tk_type_eq(&lo.as.value.type, &subject)) return efail(tk_error_make("range lower bound does not match the subject type"));
            if (!tk_type_eq(&hi.as.value.type, &subject)) return efail(tk_error_make("range upper bound does not match the subject type"));
            if (!(subject.tag == TK_TYPE_PRIM && tk_prim_is_int(subject.as.prim))) return efail(tk_error_make("range pattern requires an integer subject (B.38 — not a float)"));
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
// does pattern `p` name case `name` — directly (Bind/Field) or via a bare Alt option?
static bool pattern_names(tk_pattern p, tk_str name) {
    if (p.tag == TK_PAT_BIND)
        return name_eq(p.as.bind.type_name.segments[p.as.bind.type_name.len - 1].name, name);
    if (p.tag == TK_PAT_FIELD)
        return name_eq(p.as.field.type_name.segments[p.as.field.type_name.len - 1].name, name);
    if (p.tag == TK_PAT_ALT) {
        for (size_t i = 0; i < p.as.alt.n_options; i += 1) {
            tk_pattern opt = p.as.alt.options[i];
            if (opt.tag == TK_PAT_BIND  && name_eq(opt.as.bind.type_name.segments[opt.as.bind.type_name.len - 1].name, name)) return true;
            if (opt.tag == TK_PAT_FIELD && name_eq(opt.as.field.type_name.segments[opt.as.field.type_name.len - 1].name, name)) return true;
        }
    }
    return false;
}
// is `name` covered by some UNGUARDED arm?
static bool some_arm_names(tk_arm *arms, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1)
        if (!arms[i].has_when && pattern_names(arms[i].pattern, name)) return true;
    return false;
}
bool tk_exhaustive(tk_arm *arms, size_t n, tk_type subject, tk_type_table table) {
    if (has_wildcard(arms, n)) return true;
    tk_type sv = tk_expand_variant(subject, table);   // a NAMED variant → its TK_TYPE_VARIANT cases (B.14/B.15)
    if (sv.tag != TK_TYPE_VARIANT) return false;
    for (size_t i = 0; i < sv.as.variant.len; i += 1) {
        tk_type mem = sv.as.variant.members[i];
        if (mem.tag != TK_TYPE_NAMED) return false;
        if (!some_arm_names(arms, n, mem.as.named.name)) return false;
    }
    return true;
}
