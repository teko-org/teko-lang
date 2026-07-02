// src/checker/check_modules.c — W-vis-enforce. The C23 mirror of check_modules.tks.
//
// A dedicated pass over the flattened program: it reads each item's source namespace
// (A3 provenance) directly, so it threads NO namespace context through the typing chain.
// It builds the file-local `use` alias map from the `use`-items and walks every decl's
// TYPE references, enforcing the namespace-qualification + `pub`/`exp` rules.
#include "check_modules.h"
#include "scope.h"            // tk_builtin_type (predefined types are never namespaced)
#include <string.h>
#include <stdlib.h>
#include <stdio.h>            // snprintf (located diagnostics — W-loc-2)

// "file:line:col: msg" with absent parts omitted (M.3 — show what we know). Fresh string.
const char *tk_diag_at(tk_str file, uint32_t line, uint32_t col, const char *msg) {
    size_t cap = file.len + strlen(msg) + 48;
    char *buf = tk_alloc(cap); if (!buf) abort();
    if (file.len > 0 && line > 0)
        snprintf(buf, cap, "%.*s:%u:%u: %s", (int)file.len, (const char *)file.ptr, line, col, msg);
    else if (file.len > 0)
        snprintf(buf, cap, "%.*s: %s", (int)file.len, (const char *)file.ptr, msg);
    else if (line > 0)
        snprintf(buf, cap, "%u:%u: %s", line, col, msg);
    else { tk_free0(buf); return msg; }
    return buf;
}

static bool seq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static bool is_builtin_name(tk_str name) { return tk_builtin_type(name).ok; }

// an alias binding: within namespace `ns`, the name `alias` refers to namespace `target`.
typedef struct { tk_str ns; tk_str alias; tk_str target; } alias_bind;
typedef struct { alias_bind *ptr; size_t len; size_t cap; } aliases;

static aliases aliases_push(aliases a, alias_bind b) {
    if (a.len == a.cap) {
        size_t nc = a.cap ? a.cap * 2 : 8;
        alias_bind *np = tk_realloc0(a.ptr, nc * sizeof *np); if (!np) abort();
        a.ptr = np; a.cap = nc;
    }
    a.ptr[a.len++] = b; return a;
}

// join the first `n` segments of a path with "::" into a fresh tk_str (the seed leaks it —
// whole-compile lifetime, M.5). n = path.len → the full namespace; n = path.len-1 → prefix.
static tk_str join_n(tk_path p, size_t n) {
    if (n == 0) return (tk_str){ NULL, 0 };
    size_t total = 0;
    for (size_t i = 0; i < n; i += 1) { total += p.segments[i].name.len; if (i + 1 < n) total += 2; }
    tk_byte *buf = tk_alloc(total ? total : 1); if (!buf) abort();
    size_t o = 0;
    for (size_t i = 0; i < n; i += 1) {
        memcpy(buf + o, p.segments[i].name.ptr, p.segments[i].name.len); o += p.segments[i].name.len;
        if (i + 1 < n) { buf[o] = ':'; buf[o + 1] = ':'; o += 2; }
    }
    return (tk_str){ buf, total };
}

static aliases build_aliases(tk_program prog) {
    aliases a = { 0 };
    for (size_t i = 0; i < prog.len; i += 1) {
        if (prog.items[i].tag != TK_ITEM_USE) continue;
        tk_use_decl u = prog.items[i].as.use_decl;
        if (u.path.len == 0) continue;
        tk_str alias  = u.has_alias ? u.alias : u.path.segments[u.path.len - 1].name;  // last seg = implicit alias (B.32)
        tk_str target = join_n(u.path, u.path.len);                                     // the full absolute namespace
        a = aliases_push(a, (alias_bind){ .ns = prog.items[i].namespace, .alias = alias, .target = target });
    }
    return a;
}

static tk_str alias_target(aliases a, tk_str ref_ns, tk_str alias) {
    for (size_t i = 0; i < a.len; i += 1)
        if (seq(a.ptr[i].ns, ref_ns) && seq(a.ptr[i].alias, alias)) return a.ptr[i].target;
    return (tk_str){ NULL, 0 };
}

static const tk_type_reg *find_in_ns(tk_type_table t, tk_str name, tk_str ns) {
    for (size_t i = 0; i < t.len; i += 1)
        if (seq(t.ptr[i].name, name) && seq(t.ptr[i].namespace, ns)) return &t.ptr[i];
    return NULL;
}
static bool name_exists_anywhere(tk_type_table t, tk_str name) {
    for (size_t i = 0; i < t.len; i += 1) if (seq(t.ptr[i].name, name)) return true;
    return false;
}

// check ONE Named type reference (its path) used inside namespace `ref_ns`.
static const char *check_named(tk_path path, tk_str ref_ns, tk_type_table table, aliases al) {
    if (path.len == 0) return NULL;
    tk_str last = path.segments[path.len - 1].name;
    if (is_builtin_name(last)) return NULL;                       // u8…/str/byte/error: never namespaced

    if (path.len == 1) {                                          // BARE
        if (find_in_ns(table, last, ref_ns)) return NULL;         // same-namespace type — bare is correct
        if (name_exists_anywhere(table, last))
            return "bare cross-namespace type reference — qualify it (`ns::Type`, with `use teko::ns`) or use the absolute path";
        return NULL;                                              // unknown name → the normal resolver reports it
    }

    // QUALIFIED / ABSOLUTE: resolve the target namespace.
    tk_str target_ns;
    if (path.len == 2) {                                          // single-segment prefix = a `use` alias
        target_ns = alias_target(al, ref_ns, path.segments[0].name);
        if (target_ns.ptr == NULL)
            return "qualified type reference with an unbound alias — add `use teko::<ns>` (the prefix binds nothing)";
    } else {
        target_ns = join_n(path, path.len - 1);                  // absolute: teko::a::b::Type → "teko::a::b"
    }

    const tk_type_reg *reg = find_in_ns(table, last, target_ns);
    if (reg) {
        if (!seq(target_ns, ref_ns) && reg->vis < TK_VIS_PUB)    // cross-namespace requires pub/exp (B.9)
            return "cross-namespace reference to a private type — mark the type `pub` (or `exp`)";
        return NULL;
    }
    if (name_exists_anywhere(table, last))
        return "qualified type reference names the wrong namespace — the type is declared elsewhere";
    return NULL;                                                  // unknown → the normal resolver reports it
}

static const char *check_texpr(tk_type_expr te, tk_str ref_ns, tk_type_table table, aliases al) {
    switch (te.tag) {
        case TK_TEXPR_NAMED:    return check_named(te.as.named.path, ref_ns, table, al);
        case TK_TEXPR_SLICE:    return check_texpr(*te.as.slice.element, ref_ns, table, al);
        case TK_TEXPR_OPTIONAL: return check_texpr(*te.as.optional.inner, ref_ns, table, al);
        case TK_TEXPR_UNION:
            for (size_t i = 0; i < te.as.uni.len; i += 1) {
                const char *e = check_texpr(te.as.uni.members[i], ref_ns, table, al);
                if (e) return e;
            }
            return NULL;
        case TK_TEXPR_FUNC:   // (W10a) check every param + the return
            for (size_t i = 0; i < te.as.func.nparams; i += 1) {
                const char *e = check_texpr(te.as.func.params[i], ref_ns, table, al);
                if (e) return e;
            }
            return te.as.func.ret ? check_texpr(*te.as.func.ret, ref_ns, table, al) : NULL;
    }
    return NULL;
}

const char *tk_check_modules(tk_program prog, tk_type_table table) {
    aliases al = build_aliases(prog);
    for (size_t i = 0; i < prog.len; i += 1) {
        tk_item it = prog.items[i];
        tk_str ns = it.namespace;
        const char *e = NULL;
        uint32_t line = 0, col = 0;
        if (it.tag == TK_ITEM_FUNCTION) {
            tk_function f = it.as.function;
            line = f.line; col = f.col;
            tk_type_table ftbl = tk_type_param_table(f.type_params, f.n_type_params, ns, table);   // (S4) type-params as local types of the using ns
            for (size_t p = 0; p < f.nparams && !e; p += 1)
                e = check_texpr(f.params[p].type_ann, ns, ftbl, al);
            if (!e && f.has_return) e = check_texpr(f.return_type, ns, ftbl, al);
        } else if (it.tag == TK_ITEM_TYPE_DECL) {
            tk_type_decl d = it.as.type_decl;
            line = d.line; col = d.col;
            if (d.body.tag == TK_BODY_STRUCT) {
                for (size_t k = 0; k < d.body.as.struct_body.n_fields && !e; k += 1)
                    e = check_texpr(d.body.as.struct_body.fields[k].type_ann, ns, table, al);
            } else if (d.body.tag == TK_BODY_VARIANT) {
                e = check_texpr(d.body.as.variant_body.type_expr, ns, table, al);
            } else if (d.body.tag == TK_BODY_ALIAS) {
                e = check_texpr(d.body.as.alias_body.alias, ns, table, al);   // an alias's RHS type-expr
            } else if (d.body.tag == TK_BODY_CLASS) {
                // (W10b.CLASS) mirrors struct's field-only scope (methods unchecked here, same as struct methods today)
                for (size_t k = 0; k < d.body.as.class_body.n_fields && !e; k += 1)
                    e = check_texpr(d.body.as.class_body.fields[k].type_ann, ns, table, al);
            } else if (d.body.tag == TK_BODY_INTERFACE) {
                // (W10b.IF) validate each method signature's TYPED params + return type-exprs
                tk_interface_body ib = d.body.as.interface_body;
                for (size_t mi = 0; mi < ib.n_methods && !e; mi += 1) {
                    tk_function m = ib.methods[mi];
                    for (size_t pi = 0; pi < m.nparams && !e; pi += 1)
                        if (m.params[pi].has_type) e = check_texpr(m.params[pi].type_ann, ns, table, al);
                    if (!e && m.has_return) e = check_texpr(m.return_type, ns, table, al);
                }
            }
            // enum body: member names only; extern body (C7.1a): opaque handle — neither has type references to check
        }
        if (e) { tk_free0(al.ptr); return tk_diag_at(it.file, line, col, e); }   // W-loc-2: file:line:col
    }
    tk_free0(al.ptr);
    return NULL;
}
