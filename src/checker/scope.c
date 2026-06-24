// src/checker/scope.c
#include "scope.h"
#include <string.h>

static bool name_eq(tk_str n, tk_str m) {
    return n.len == m.len && memcmp(n.ptr, m.ptr, n.len) == 0;
}
static bool name_is(tk_str n, const char *lit) {
    size_t L = strlen(lit);
    return n.len == L && memcmp(n.ptr, lit, L) == 0;
}
static tk_type prim(tk_prim_kind k) {
    return (tk_type){ .tag = TK_TYPE_PRIM, .as.prim = k };
}

tk_env tk_env_define(tk_env env, tk_str name, tk_type t, bool is_mut) {
    return tk_env_push(env, (tk_val_binding){ .name = name, .type = t, .is_mut = is_mut });
}

bool tk_bind_is_mut(tk_bind_kind k) { return k == TK_BIND_MUT; }   // Let/Const immutable (B.21)

tk_binding_result tk_env_lookup_binding(tk_env env, tk_str name) {
    for (size_t i = env.len; i > 0; i -= 1) {        // innermost (most recent) first
        tk_val_binding b = env.ptr[i - 1];
        if (name_eq(b.name, name)) return (tk_binding_result){ .ok = true, .as.value = b };
    }
    return (tk_binding_result){ .ok = false, .as.error = tk_error_make("undefined name") };
}

tk_type_result tk_env_lookup(tk_env env, tk_str name) {       // the type — thin wrapper
    tk_binding_result r = tk_env_lookup_binding(env, name);
    if (!r.ok) return (tk_type_result){ .ok = false, .as.error = r.as.error };
    return (tk_type_result){ .ok = true, .as.value = r.as.value.type };
}

tk_type_result tk_builtin_type(tk_str name) {
    tk_type t;
    if      (name_is(name, "u8"))    t = prim(TK_PRIM_U8);
    else if (name_is(name, "u16"))   t = prim(TK_PRIM_U16);
    else if (name_is(name, "u32"))   t = prim(TK_PRIM_U32);
    else if (name_is(name, "u64"))   t = prim(TK_PRIM_U64);
    else if (name_is(name, "i8"))    t = prim(TK_PRIM_I8);
    else if (name_is(name, "i16"))   t = prim(TK_PRIM_I16);
    else if (name_is(name, "i32"))   t = prim(TK_PRIM_I32);
    else if (name_is(name, "i64"))   t = prim(TK_PRIM_I64);
    else if (name_is(name, "bool"))  t = prim(TK_PRIM_BOOL);
    else if (name_is(name, "byte"))  t = (tk_type){ .tag = TK_TYPE_BYTE };
    else if (name_is(name, "str"))   t = (tk_type){ .tag = TK_TYPE_STR };
    else if (name_is(name, "error")) t = (tk_type){ .tag = TK_TYPE_ERROR };
    else return (tk_type_result){ .ok = false, .as.error = tk_error_make("not a built-in type") };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// The injected stdlib (non-shadowable): teko::print / teko::println are part of the
// language surface, not imported — so type_call falls back here when env lookup fails.
// Both are (str) -> Unit. The func type's params/ret are pointers, so they point at
// immutable static singletons (whole-compile lifetime — they are never mutated).
tk_type_result tk_builtin_fn(tk_str name) {
    static tk_type str_t  = { .tag = TK_TYPE_STR };       // a (str) parameter
    static tk_type bool_t = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_BOOL };  // a (bool) parameter
    static tk_type unit_t = { .tag = TK_TYPE_UNIT };      // the Unit return
    static tk_type str2_t[2] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_STR } };  // (str, str)
    if (name_is(name, "print") || name_is(name, "println")) {
        tk_type ft = { .tag = TK_TYPE_FUNC,
                       .as.func = { .params = &str_t, .nparams = 1, .ret = &unit_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // teko::assert — injected testing assertions (canonical: src/assert/assert.tks).
    // Resolved by last segment, like print (type_call looks up the LAST path seg). The
    // seed subset is NON-generic: equals/not_equals/is_error/is_ok from the roadmap need
    // GENERICS or the result/error types and are NOT expressible yet — DEFERRED (M.3).
    if (name_is(name, "is_true") || name_is(name, "is_false")) {  // (bool) -> Unit
        tk_type ft = { .tag = TK_TYPE_FUNC,
                       .as.func = { .params = &bool_t, .nparams = 1, .ret = &unit_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "str_contains")) {                          // (str, str) -> Unit
        tk_type ft = { .tag = TK_TYPE_FUNC,
                       .as.func = { .params = str2_t, .nparams = 2, .ret = &unit_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("not a built-in function") };
}
