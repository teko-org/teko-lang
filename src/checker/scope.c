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
    // PERSISTENT extend (NOT the linear TK_LIST push, which reallocs/writes the buffer
    // IN PLACE): copy into a FRESH buffer so the caller's env is never mutated, moved, or
    // freed. The checker reuses one env value NON-LINEARLY — sibling `match` arms each
    // extend the same base env (tk_check_pattern), and sibling functions each extend the
    // same collected env (tk_type_function) — so a mutating push would dangle every other
    // holder's `.ptr` after a realloc → heap corruption ("pointer being freed was not
    // allocated"). Copy-on-extend keeps each branch's env independent. O(n) per define;
    // the bootstrap stays small (M.5), and this matches the functional `.tks` env twin.
    size_t n = env.len;
    tk_val_binding *buf = tk_alloc((n + 1) * sizeof *buf);
    if (buf == NULL) { abort(); }
    if (n != 0) { memcpy(buf, env.ptr, n * sizeof *buf); }
    buf[n] = (tk_val_binding){ .name = name, .type = t, .is_mut = is_mut, .ns = (tk_str){0} };  // local: no ns (#41)
    return (tk_env){ .ptr = buf, .len = n + 1, .cap = n + 1, .cur_ns = env.cur_ns };
}

// (#41) define a TOP-LEVEL FUNCTION binding carrying its declaring namespace, so an unqualified
// call resolves to a same-namespace function (not a global bare-name collision). Same copy-on-extend.
tk_env tk_env_define_fn(tk_env env, tk_str name, tk_type t, tk_str ns) {
    size_t n = env.len;
    tk_val_binding *buf = tk_alloc((n + 1) * sizeof *buf);
    if (buf == NULL) { abort(); }
    if (n != 0) { memcpy(buf, env.ptr, n * sizeof *buf); }
    buf[n] = (tk_val_binding){ .name = name, .type = t, .is_mut = false, .ns = ns };
    return (tk_env){ .ptr = buf, .len = n + 1, .cap = n + 1, .cur_ns = env.cur_ns };
}

bool tk_bind_is_mut(tk_bind_kind k) { return k == TK_BIND_MUT; }   // Let/Const immutable (B.21)

// (#41) the LAST `::`-segment of a namespace ("teko::lexer" -> "lexer"), for matching a qualified
// call's immediate qualifier against a binding's declaring namespace.
static tk_str ns_last_seg(tk_str ns) {
    size_t cut = 0;
    for (size_t i = 0; i + 1 < ns.len; i += 1)
        if (ns.ptr[i] == ':' && ns.ptr[i + 1] == ':') cut = i + 2;
    return (tk_str){ ns.ptr + cut, ns.len - cut };
}

// (#41) NAMESPACE-AWARE call resolution. UNQUALIFIED call `name`: a LOCAL (ns empty) or a function
// in the CURRENT namespace; never another namespace's same-named function. QUALIFIED `q::…::name`:
// a function whose declaring namespace's last segment equals the immediate qualifier `q`. Innermost
// first (locals shadow). Returns the function/value type.
tk_type_result tk_env_lookup_call(tk_env env, tk_path callee) {
    tk_str name = callee.segments[callee.len - 1].name;
    bool qualified = callee.len > 1;
    tk_str qual = qualified ? callee.segments[callee.len - 2].name : (tk_str){0};
    for (size_t i = env.len; i > 0; i -= 1) {
        tk_val_binding b = env.ptr[i - 1];
        if (!name_eq(b.name, name)) continue;
        if (!qualified) {
            if (b.ns.len == 0 || name_eq(b.ns, env.cur_ns)) return (tk_type_result){ .ok = true, .as.value = b.type };
        } else {
            if (b.ns.len != 0 && name_eq(ns_last_seg(b.ns), qual)) return (tk_type_result){ .ok = true, .as.value = b.type };
        }
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("undefined name") };
}

// (#49) the resolved target's declaring NAMESPACE for a call (same scan as tk_env_lookup_call),
// or the empty str if the callee is a LOCAL (ns empty) or resolves to nothing (a builtin). Codegen
// mangles a user call by this namespace; an empty ns means "use the builtin call-map / bare name".
tk_str tk_env_call_ns(tk_env env, tk_path callee) {
    tk_str name = callee.segments[callee.len - 1].name;
    bool qualified = callee.len > 1;
    tk_str qual = qualified ? callee.segments[callee.len - 2].name : (tk_str){0};
    for (size_t i = env.len; i > 0; i -= 1) {
        tk_val_binding b = env.ptr[i - 1];
        if (!name_eq(b.name, name)) continue;
        if (!qualified) {
            if (b.ns.len == 0 || name_eq(b.ns, env.cur_ns)) return b.ns;
        } else {
            if (b.ns.len != 0 && name_eq(ns_last_seg(b.ns), qual)) return b.ns;
        }
    }
    return (tk_str){0};
}

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
    else if (name_is(name, "u128"))  t = prim(TK_PRIM_U128);   // native set (B.38)
    else if (name_is(name, "i8"))    t = prim(TK_PRIM_I8);
    else if (name_is(name, "i16"))   t = prim(TK_PRIM_I16);
    else if (name_is(name, "i32"))   t = prim(TK_PRIM_I32);
    else if (name_is(name, "i64"))   t = prim(TK_PRIM_I64);
    else if (name_is(name, "i128"))  t = prim(TK_PRIM_I128);   // native set (B.38)
    else if (name_is(name, "f16"))   t = prim(TK_PRIM_F16);    // native floats (B.38)
    else if (name_is(name, "f32"))   t = prim(TK_PRIM_F32);    // native floats (B.38)
    else if (name_is(name, "f64"))   t = prim(TK_PRIM_F64);    // native floats (B.38)
    else if (name_is(name, "bool"))  t = prim(TK_PRIM_BOOL);
    else if (name_is(name, "byte"))  t = (tk_type){ .tag = TK_TYPE_BYTE };
    else if (name_is(name, "char"))  t = (tk_type){ .tag = TK_TYPE_CHAR };   // a UTF-8 codepoint (distinct from byte / []byte)
    else if (name_is(name, "str"))   t = (tk_type){ .tag = TK_TYPE_STR };
    else if (name_is(name, "error")) t = (tk_type){ .tag = TK_TYPE_ERROR };
    else if (name_is(name, "void"))  t = (tk_type){ .tag = TK_TYPE_VOID };   // (C7.2) explicit void return annotation in extern fn declarations
    else if (name_is(name, "ptr"))   t = (tk_type){ .tag = TK_TYPE_PTR };    // (C7.1a) opaque FFI pointer
    else if (name_is(name, "uptr"))  t = (tk_type){ .tag = TK_TYPE_UPTR };   // (C7.1a) opaque word-size unsigned
    else return (tk_type_result){ .ok = false, .as.error = tk_error_make("not a built-in type") };
    return (tk_type_result){ .ok = true, .as.value = t };
}

// The injected stdlib (non-shadowable): teko::print / teko::println are part of the
// language surface, not imported — so type_call falls back here when env lookup fails.
// Both are (str) -> void. The func type's params/ret are pointers, so they point at
// immutable static singletons (whole-compile lifetime — they are never mutated).
tk_type_result tk_builtin_fn(tk_str name) {
    static tk_type str_t  = { .tag = TK_TYPE_STR };       // a (str) parameter
    static tk_type bool_t = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_BOOL };  // a (bool) parameter
    static tk_type void_t = { .tag = TK_TYPE_VOID };      // the void return (M.3 — no value)
    static tk_type str2_t[2] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_STR } };  // (str, str)
    static tk_type slice_str_t = { .tag = TK_TYPE_SLICE, .as.slice = { .element = &str_t } };  // []str — `concat`'s single variadic param (2026-07-01)
    // self-host str/byte STDLIB surface — the unqualified helpers the corpus calls (the C twins are
    // tk_str_slice/tk_str_concat/… in text.h + teko_rt). All return `str`; recognized like print
    // (resolved by last path segment). NON-generic, fixed signatures (M.5).
    static tk_type byte_t = { .tag = TK_TYPE_BYTE };
    static tk_type u64_t  = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 };
    static tk_type i64_t  = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 };
    static tk_type f64_t  = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_F64 };
    static tk_type byte_elem = { .tag = TK_TYPE_BYTE };
    static tk_type bytes_t = { .tag = TK_TYPE_SLICE, .as.slice.element = &byte_elem };  // []byte
    static tk_type char_elem = { .tag = TK_TYPE_CHAR };
    static tk_type char_slice_t = { .tag = TK_TYPE_SLICE, .as.slice.element = &char_elem };  // []char
    static tk_type char_t = { .tag = TK_TYPE_CHAR };   // char (for char_at/to_lower/to_upper return + char-param builtins)
    // (str, i64) — char_at / str_slice_chars first two params
    static tk_type str_i64_p[2] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 } };
    // (str, i64, i64) — str_slice_chars params
    static tk_type str_i64_i64_p[3] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 } };
    static tk_type slice_p[3] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 } };
    // str3_t REMOVED (2026-07-01) — was only used by str_concat3/concat3, both removed.
    static tk_type str_u64_p[2] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 } };  // (str, u64)
    // teko::str::last_index_of -> `u64 | error` (a found index, or error when absent)
    static tk_type u64_or_err_m[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 }, { .tag = TK_TYPE_ERROR } };
    static tk_type u64_or_err = { .tag = TK_TYPE_VARIANT, .as.variant = { u64_or_err_m, 2 } };
    // str_from_utf8 -> `str | error` (ROUND 0 / B.36 — the validated bytes->str door)
    static tk_type str_or_err_m[2] = { { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_ERROR } };
    static tk_type str_or_err = { .tag = TK_TYPE_VARIANT, .as.variant = { str_or_err_m, 2 } };
    // VM-internal arithmetic FFI: sign-aware int div/rem over the i128 carrier, float div.
    static tk_type i128_t = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I128 };
    static tk_type divrem_p[3] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I128 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I128 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_BOOL } };
    static tk_type f64x2_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_F64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_F64 } };
    static tk_type i128_bool_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I128 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_BOOL } };
    // error diagnostic adornment builtins (E2): err_loc(error,u32,u32)->error ; err_typed(error,str,str)->error.
    static tk_type error_t   = { .tag = TK_TYPE_ERROR };
    static tk_type err_loc_p[3]   = { { .tag = TK_TYPE_ERROR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 } };
    static tk_type err_typed_p[3] = { { .tag = TK_TYPE_ERROR }, { .tag = TK_TYPE_STR }, { .tag = TK_TYPE_STR } };
    #define TK_EFN(P, N) (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_FUNC, .as.func = { .params = (P), .nparams = (N), .ret = &error_t } } }
    if (name_is(name, "err_loc"))   return TK_EFN(err_loc_p, 3);     // (error, u32, u32) -> error
    if (name_is(name, "err_typed")) return TK_EFN(err_typed_p, 3);   // (error, str, str) -> error
    #undef TK_EFN
    #define TK_BFN(P, N) (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_FUNC, .as.func = { .params = (P), .nparams = (N), .ret = &str_t } } }
    if (name_is(name, "slice"))        return TK_BFN(slice_p, 3);   // slice(str, u64, u64) -> str
    if (name_is(name, "str") || name_is(name, "str_of_bytes")) return TK_BFN(&bytes_t, 1);   // ([]byte) -> str
    if (name_is(name, "one_byte"))     return TK_BFN(&byte_t, 1);   // (byte) -> str
    if (name_is(name, "str_concat"))   return TK_BFN(str2_t, 2);    // (str, str) -> str — internal 2-arg primitive, unchanged
    // "str_concat3" REMOVED (2026-07-01) — superseded by the variadic "concat" below.
    if (name_is(name, "i64_to_str"))   return TK_BFN(&i64_t, 1);    // (i64) -> str
    if (name_is(name, "u64_to_str"))   return TK_BFN(&u64_t, 1);    // (u64) -> str
    if (name_is(name, "ftoa"))         return TK_BFN(&f64_t, 1);    // (f64) -> str
    if (name_is(name, "f64_g17"))      return TK_BFN(&f64_t, 1);    // teko::fmt::f64_g17(f64) -> str (host float renderer FFI bottom)
    // teko::str::* — the self-host string stdlib the corpus calls namespaced (resolved by last
    // segment, like the underscored twins above; the C runtime twins are tk_str_concat/slice/…).
    // "concat"/"concat3" are matched by BARE last-segment name (see the callers of
    // scope_builtin_fn / tk_env_lookup_call), so ANY namespace prefix ending in these names
    // resolves identically — this is what makes `teko::string::concat(a, b)` (the LEGISLATED
    // public API per TEKO_LEGISLATION.md's "`+` never concatenates" rule: string building is
    // `string::concat` / interpolation `$"..."`, never `+`) and `teko::str::concat(a, b)` (the
    // spelling the corpus already uses pervasively) two names for the same builtin dispatch.
    // No separate registration is needed for the `string::` spelling — this comment documents
    // the mechanism so the legislated name is discoverable here, not just inferable.
    // "concat" (== the LEGISLATED string::concat) is the ONE public variadic form (2026-07-01,
    // `concat3` removed): `concat(params pieces: []str) -> str` — ONE param, itself a []str, and
    // `.variadic = true` so the checker's params call-site desugar (expr.c/typer.tks) packs N
    // trailing str args into a []str automatically, or passes an existing []str straight through.
    if (name_is(name, "concat"))       return (tk_type_result){ .ok = true, .as.value = (tk_type){ .tag = TK_TYPE_FUNC,
        .as.func = { .params = &slice_str_t, .nparams = 1, .ret = &str_t, .variadic = true } } };
    if (name_is(name, "slice_to"))     return TK_BFN(str_u64_p, 2);   // str::slice_to(str, u64) -> str
    if (name_is(name, "slice_from"))   return TK_BFN(str_u64_p, 2);   // str::slice_from(str, u64) -> str
    #undef TK_BFN
    if (name_is(name, "len")) {                                       // str::len(str) -> u64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &u64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "bytes_of_str")) {                             // bytes_of_str(str) -> []byte
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &bytes_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "str_from_utf8")) {                            // str_from_utf8([]byte) -> str | error
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &bytes_t, .nparams = 1, .ret = &str_or_err } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "chars")) {                                     // str::chars(str) -> []char
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &char_slice_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "len_chars")) {                                 // str::len_chars(str) -> i64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &i64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // --- ROUND 0: UTF-8 codepoint operations ---
    if (name_is(name, "char_at")) {                                   // char_at(str, i64) -> char
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = str_i64_p, .nparams = 2, .ret = &char_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "str_slice_chars")) {                           // str_slice_chars(str, i64, i64) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = str_i64_i64_p, .nparams = 3, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "is_alpha")) {                                  // is_alpha(char) -> bool
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &char_t, .nparams = 1, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "is_digit")) {                                  // is_digit(char) -> bool
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &char_t, .nparams = 1, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "is_space")) {                                  // is_space(char) -> bool
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &char_t, .nparams = 1, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "to_lower")) {                                  // to_lower(char) -> char
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &char_t, .nparams = 1, .ret = &char_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "to_upper")) {                                  // to_upper(char) -> char
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &char_t, .nparams = 1, .ret = &char_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "ends_with")) {                                 // str::ends_with(str, str) -> bool
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = str2_t, .nparams = 2, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "last_index_of")) {                             // str::last_index_of(str, str) -> u64 | error
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = str2_t, .nparams = 2, .ret = &u64_or_err } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "contains")) {                                  // str::contains(str, str) -> bool
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = str2_t, .nparams = 2, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // teko::float::parse(str) -> f64 — no namespace file exists for teko::float; keep as seed.
    if (name_is(name, "parse")) {                                     // teko::float::parse(str) -> f64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &f64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // NOTE: teko::io::*, teko::env::*, teko::fs::*, teko::process::* seeds have been REMOVED (C7.3).
    // Those host surfaces are declared in src/io/io.tks, src/env/env.tks, src/fs/fs.tks,
    // src/process/process.tks and are collected into the checker env via the normal discover/collect
    // pass. The env lookup at type_call (expr.c line 242) finds them BEFORE this fallback fires.
    if (name_is(name, "os")) {                                        // teko::os() -> str — C7.1f
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // teko::abort — the host abort FFI bottom (() -> void). The runtime's panic lowers to it.
    if (name_is(name, "abort")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // D3 — test-coverage sink (host side-channel): cov_reset()/cov_mark(u64) -> void; cov_distinct() -> u64.
    if (name_is(name, "cov_reset")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_mark")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u64_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_distinct")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &u64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_is_marked")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u64_t, .nparams = 1, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // D3-branch — branch-coverage sink.
    if (name_is(name, "cov_branches_on")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &bool_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_branch_reset") || name_is(name, "cov_leave")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_enter")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u64_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_branch")) {
        static tk_type covbr_p[3] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 } };
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = covbr_p, .nparams = 3, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_branch_hit")) {
        static tk_type covbh_p[4] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 } };
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = covbh_p, .nparams = 4, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // D3-line — line-coverage sink.
    if (name_is(name, "cov_lines_on")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &bool_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_line_reset")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_line")) {
        static tk_type u32_t = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 };
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u32_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "cov_line_hit")) {
        static tk_type covlh_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U32 } };
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = covlh_p, .nparams = 2, .ret = &bool_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // VM-internal arithmetic FFI (sign-aware over the i128 carrier) + the F3 panic helpers the VM
    // and runtime call via the reserved `teko::` root (teko::runtime mirrors these in Teko).
    if (name_is(name, "div") || name_is(name, "rem")) {              // (i128, i128, bool) -> i128
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = divrem_p, .nparams = 3, .ret = &i128_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fdiv")) {                                     // (f64, f64) -> f64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = f64x2_p, .nparams = 2, .ret = &f64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "panic_div0") || name_is(name, "panic_oob") || name_is(name, "panic_cast") || name_is(name, "panic_overflow")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = NULL, .nparams = 0, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // math / bit-pattern FFI: floor(f64)->f64 ; int_to_float(i128,bool)->f64 ; f64<->bits.
    if (name_is(name, "floor")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &f64_t, .nparams = 1, .ret = &f64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "int_to_float")) {
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = i128_bool_p, .nparams = 2, .ret = &f64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "f64_from_bits")) {                            // (u64) -> f64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u64_t, .nparams = 1, .ret = &f64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "f64_bits")) {                                 // (f64) -> u64
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &f64_t, .nparams = 1, .ret = &u64_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // teko::assert — injected testing assertions (canonical: src/assert/assert.tks).
    // Resolved by last segment, like print (type_call looks up the LAST path seg). The
    // seed subset is NON-generic: equals/not_equals/is_error/is_ok from the roadmap need
    // GENERICS or the result/error types and are NOT expressible yet — DEFERRED (M.3).
    if (name_is(name, "is_true") || name_is(name, "is_false")) {  // (bool) -> void
        tk_type ft = { .tag = TK_TYPE_FUNC,
                       .as.func = { .params = &bool_t, .nparams = 1, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "str_contains")) {                          // (str, str) -> void
        tk_type ft = { .tag = TK_TYPE_FUNC,
                       .as.func = { .params = str2_t, .nparams = 2, .ret = &void_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // (C7.1a) marshalling primitives — teko::mem: move aggregates across the FFI boundary.
    static tk_type ptr_t = { .tag = TK_TYPE_PTR };
    if (name_is(name, "as_ptr") || name_is(name, "as_cstr")) {     // (str) -> ptr
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &str_t, .nparams = 1, .ret = &ptr_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "str_from_cstr")) {                          // (ptr) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &ptr_t, .nparams = 1, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "bytes_from_ptr")) {                         // (ptr, u64) -> []byte
        static tk_type bfp_p[2] = { { .tag = TK_TYPE_PTR }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 } };
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = bfp_p, .nparams = 2, .ret = &bytes_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    // ROUND 0 format helpers — intercepted by the VM (try_builtin_call) and native (tk_fmt_*).
    // Signatures mirror teko_rt.tks exp fn declarations; resolved by last segment like ftoa.
    static tk_type f64_i64_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_F64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 } };
    static tk_type f64_str_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_F64 }, { .tag = TK_TYPE_STR } };
    static tk_type i64_str_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 }, { .tag = TK_TYPE_STR } };
    static tk_type u64_str_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 }, { .tag = TK_TYPE_STR } };
    static tk_type i64_i64_p[2] = { { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 }, { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_I64 } };
    if (name_is(name, "fmt_f") || name_is(name, "fmt_e") || name_is(name, "fmt_g") ||
        name_is(name, "fmt_n_f") || name_is(name, "fmt_p")) {       // (f64, i64) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = f64_i64_p, .nparams = 2, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_d")) {                                    // (i64, i64) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = i64_i64_p, .nparams = 2, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_x_upper") || name_is(name, "fmt_x_lower") || name_is(name, "fmt_b")) { // (u64) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &u64_t, .nparams = 1, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_n_i")) {                                  // (i64) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = &i64_t, .nparams = 1, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_dyn_f64")) {                              // (f64, str) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = f64_str_p, .nparams = 2, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_dyn_i64")) {                              // (i64, str) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = i64_str_p, .nparams = 2, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    if (name_is(name, "fmt_dyn_u64")) {                              // (u64, str) -> str
        tk_type ft = { .tag = TK_TYPE_FUNC, .as.func = { .params = u64_str_p, .nparams = 2, .ret = &str_t } };
        return (tk_type_result){ .ok = true, .as.value = ft };
    }
    return (tk_type_result){ .ok = false, .as.error = tk_error_make("not a built-in function") };
}
