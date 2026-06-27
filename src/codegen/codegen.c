// src/codegen/codegen.c   (namespace 'teko::codegen')
// F2 BACKEND implementation: tk_tprogram -> C source text.
//
// Lowering design (M0 = integer arithmetic + let + return -> running native binary):
//   * types  : tk_type -> C type text (U8..U64 -> uintN_t, I8..I64 -> intN_t,
//              Bool -> bool, byte -> uint8_t, void -> void). Everything else =
//              "not yet supported".
//   * exprs  : Number -> literal; Var -> identifier; Binary -> (L op R);
//              Unary -> (op X); Compare -> chained && of adjacent C comparisons;
//              Cast -> ((Ctype)(inner)); Call -> name(args…); FieldAccess ->
//              (recv.field). if/match AS A VALUE = not yet supported.
//   * stmts  : Binding -> Ctype x = v; Assign -> x op= v; Return -> early exit in
//              main / `return v;` in a fn; ExprStmt -> expr; Loop -> while(1){…};
//              Break/Continue -> break;/continue;. match-stmt = not yet supported.
//   * program: each tk_tfunction -> a C function; the loose top-level statements (the
//              VIRTUAL-MAIN) -> int main(void){ … return 0; } with early-return.
//
// COVERAGE RULE (M.3): unsupported nodes fail with a clear message; never emit broken
// C, never crash.
#include "codegen.h"

#include "../lexer/token.h"   // tk_token_kind operator kinds
#include "../parser/ast.h"    // tk_bind_kind, tk_bind_target, tk_path

#include <stdlib.h>           // malloc/realloc/free
#include <string.h>           // memcpy, strlen
#include <stdio.h>            // snprintf
#include <inttypes.h>         // PRId64

// =========================================================================
// A growable C-source buffer. Allocation failure PANICS (M.5).
// =========================================================================
typedef struct { char *ptr; size_t len; size_t cap; } cbuf;

static void cbuf_reserve(cbuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;   // +1 for the NUL
    size_t ncap = b->cap == 0 ? 256 : b->cap;
    while (b->len + extra + 1 > ncap) ncap *= 2;
    char *np = tk_realloc0(b->ptr, ncap);
    if (np == NULL) abort();
    b->ptr = np;
    b->cap = ncap;
}

static void cb(cbuf *b, const char *s) {        // append a C string
    size_t n = strlen(s);
    cbuf_reserve(b, n);
    memcpy(b->ptr + b->len, s, n);
    b->len += n;
    b->ptr[b->len] = '\0';
}

static void cb_str(cbuf *b, tk_str s) {         // append a tk_str span (raw bytes)
    cbuf_reserve(b, s.len);
    memcpy(b->ptr + b->len, s.ptr, s.len);
    b->len += s.len;
    b->ptr[b->len] = '\0';
}

static void cb_i64(cbuf *b, int64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%" PRId64, v);
    cb(b, tmp);
}

// Append a uint64_t as DECIMAL text (no printf %llu reliance issues; 20 digits max).
static void cb_u64_dec(cbuf *b, uint64_t v) {
    char tmp[24];
    int i = (int)sizeof tmp;
    tmp[--i] = '\0';
    if (v == 0) { tmp[--i] = '0'; cb(b, &tmp[i]); return; }
    while (v != 0) { tmp[--i] = (char)('0' + (unsigned)(v % 10)); v /= 10; }
    cb(b, &tmp[i]);
}

// Emit an `unsigned __int128` MAGNITUDE as a C constant EXPRESSION of type
// `unsigned __int128`. A 128-bit value has NO C literal form: a decimal token wider than
// `unsigned long long` is rejected by the C frontend BEFORE any cast applies. So when the
// value exceeds 64 bits we BUILD it from two 64-bit halves via shifts:
//   (((unsigned __int128)<hi>ULL << 64) | (unsigned __int128)<lo>ULL)
// Values that fit 64 bits stay a plain `<dec>ULL` for readability.
static void cb_u128_c_expr(cbuf *b, unsigned __int128 v) {
    uint64_t lo = (uint64_t)v;
    uint64_t hi = (uint64_t)(v >> 64);
    if (hi == 0) {
        cb(b, "(unsigned __int128)"); cb_u64_dec(b, lo); cb(b, "ULL");
        return;
    }
    cb(b, "(((unsigned __int128)"); cb_u64_dec(b, hi); cb(b, "ULL << 64) | (unsigned __int128)");
    cb_u64_dec(b, lo); cb(b, "ULL)");
}

// Emit a signed __int128 magnitude+sign as a C constant expression. For the most-negative
// value, negating in __int128 would overflow, so the magnitude is derived via unsigned
// wrap: `-(unsigned)v` yields the exact magnitude in all cases. The leading `-` rides
// outside the unsigned-128 expression (applied after the value is reconstructed).
static void cb_i128(cbuf *b, __int128 v) {
    if (v < 0) {
        cb(b, "-(");
        cb_u128_c_expr(b, -(unsigned __int128)v);
        cb(b, ")");
    } else {
        cb_u128_c_expr(b, (unsigned __int128)v);
    }
}

// Render a double with full round-trippable precision (%.17g guarantees an exact
// round-trip for IEEE-754 binary64). Used for float literal emission.
static void cb_f64_literal(cbuf *b, double v) {
    char tmp[40];
    snprintf(tmp, sizeof tmp, "%.17g", v);
    cb(b, tmp);
}

// =========================================================================
// Error plumbing — the FIRST error wins and short-circuits the whole walk.
// Helpers return bool (true == ok); the failed message lands in `*err`.
// =========================================================================
static bool fail_node(const char **err, const char *msg) { *err = msg; return false; }

// Compare a tk_str span (no NUL terminator) to a C string literal.
static bool seg_is(tk_str s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.ptr, lit, n) == 0;
}

// Append `s` C-escaped, INSIDE an already-open C string literal. The bytes are raw
// (may contain NUL, quotes, newlines) — see TK_TEXPR_STR. We never rely on length here;
// the caller emits the explicit byte count separately (M.1).
static void cb_cstr_escaped(cbuf *b, tk_str s) {
    char tmp[8];
    for (size_t i = 0; i < s.len; i += 1) {
        unsigned char c = s.ptr[i];
        switch (c) {
            case '\\': cb(b, "\\\\"); break;
            case '"':  cb(b, "\\\"");  break;
            case '\n': cb(b, "\\n"); break;
            case '\t': cb(b, "\\t"); break;
            case '\r': cb(b, "\\r"); break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    // 3-digit octal — fixed width dodges the hex "run-on digit" hazard.
                    snprintf(tmp, sizeof tmp, "\\%03o", c);
                    cb(b, tmp);
                } else {
                    tmp[0] = (char)c; tmp[1] = '\0';
                    cb(b, tmp);
                }
                break;
        }
    }
}

// =========================================================================
// Types -> C type text.
// =========================================================================
static bool emit_prim(cbuf *b, tk_prim_kind k, const char **err) {
    switch (k) {
        case TK_PRIM_U8:   cb(b, "uint8_t");           return true;
        case TK_PRIM_U16:  cb(b, "uint16_t");          return true;
        case TK_PRIM_U32:  cb(b, "uint32_t");          return true;
        case TK_PRIM_U64:  cb(b, "uint64_t");          return true;
        case TK_PRIM_U128: cb(b, "unsigned __int128"); return true;
        case TK_PRIM_I8:   cb(b, "int8_t");            return true;
        case TK_PRIM_I16:  cb(b, "int16_t");           return true;
        case TK_PRIM_I32:  cb(b, "int32_t");           return true;
        case TK_PRIM_I64:  cb(b, "int64_t");           return true;
        case TK_PRIM_I128: cb(b, "__int128");          return true;
        case TK_PRIM_F16:  cb(b, "_Float16");          return true;
        case TK_PRIM_F32:  cb(b, "float");             return true;
        case TK_PRIM_F64:  cb(b, "double");            return true;
        case TK_PRIM_BOOL: cb(b, "bool");              return true;
    }
    return fail_node(err, "codegen: unknown primitive type not yet supported");
}

// =========================================================================
// W4c — NAME MANGLING for named aggregate types.
// A named aggregate `T` in namespace `N` -> the C identifier
//   tk_t_<N with :: -> __>__<T>      (for the bare root ns, just tk_t_<T>).
// ONE helper used by BOTH the type-decl emitter and emit_type's TK_TYPE_NAMED case so a
// declaration and its references always agree. NOTE: the semantic `tk_type` NAMED only
// carries the BARE name (resolve.c drops the namespace); the typed item carries no
// namespace either. So in practice both call sites pass an EMPTY namespace and mangle by
// bare name alone — consistent for decl + ref, which is all that's required (the prompt's
// "keep it simple and consistent"). The namespace parameter is honored (`::` -> `__`)
// for the day the typed item carries provenance.
static void mangle_type_name(cbuf *b, tk_str namespace, tk_str name) {
    cb(b, "tk_t_");
    if (namespace.len > 0) {
        for (size_t i = 0; i < namespace.len; i += 1) {
            // collapse a "::" pair into "__"; copy any other byte verbatim.
            if (i + 1 < namespace.len && namespace.ptr[i] == ':' && namespace.ptr[i + 1] == ':') {
                cb(b, "__"); i += 1; continue;
            }
            char one[2] = { (char)namespace.ptr[i], '\0' };
            cb(b, one);
        }
        cb(b, "__");
    }
    cb_str(b, name);
}

// =========================================================================
// W5b — the PROGRAM stash + variant-decl lookup (mirrors the VM's g_prog).
// The VM needs no wrap: its struct value already carries the case name. The C value
// model is a `tag + union as` (emit_type_decl), so a case value that flows into a
// variant-typed slot must be WRAPPED into that representation. To synthesize the wrap
// (and to compute a pattern's tag/field), codegen must answer two questions about the
// declared types: given a NAMED type, is it a variant? and which variant member is a
// given case? Both are answered by scanning the stashed program's TYPE_DECL items —
// using the SAME tag/field spellings as emit_type_decl so wrap + decl agree byte-for-byte.
// =========================================================================
static tk_tprogram g_cg_prog;   // set once at the top of tk_emit_c (mirror of vm.c's g_prog)
// The CURRENT function's return type — set at the top of emit_function. A `return` inside a
// VALUE-form match/if (a GNU statement-expression, reached via emit_expr which carries no
// ret_type) wraps its value into THIS type via emit_as, so `return error{…}` in a diverging arm
// of a `T | error` function lands in the tk_u_ variant, not as a bare member.
static tk_type g_cg_ret_type;

// Forward decl — variant_member_name (the member's bare last path segment; the union
// field name + the source of the UPPERCASE tag suffix) is defined with the type-decl
// emitter below, but the W5b variant scans above need it too.
static tk_str variant_member_name(tk_type_expr m);

// tk_str span equality (two raw spans). The codegen has `seg_is` (span vs C literal);
// this is the span-vs-span twin used by the variant-decl scans.
static bool cg_name_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

// The bare last segment of a path (cases/types are matched by their final identifier,
// exactly like the VM's path_last and the checker's pattern_names).
static tk_str cg_path_last(tk_path p) {
    return p.len ? p.segments[p.len - 1].name : (tk_str){ NULL, 0 };
}

// Find the TYPE_DECL for a VARIANT named `name` (its body is a union of named members),
// or NULL if there is no such variant decl. Used to decide whether a NAMED slot is a
// variant (→ wrap) and to enumerate a variant's members.
static const tk_type_decl *cg_find_variant_decl(tk_str name) {
    for (size_t i = 0; i < g_cg_prog.nitems; i += 1) {
        if (g_cg_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        const tk_type_decl *d = &g_cg_prog.items[i].as.type_decl;
        if (d->body.tag != TK_BODY_VARIANT) continue;
        if (cg_name_eq(d->name, name)) return d;
    }
    return NULL;
}

// Is the NAMED type `name` a (declared) variant?
static bool cg_named_is_variant(tk_str name) { return cg_find_variant_decl(name) != NULL; }

// Is the NAMED type `name` a (declared) ENUM? An enum value is a plain C enum constant (no tag +
// union), so a `match` over it tests `subj == TK_E_<ENUM>_<MEMBER>`, not the variant `.tag` scheme.
static bool cg_named_is_enum(tk_str name) {
    for (size_t i = 0; i < g_cg_prog.nitems; i += 1) {
        if (g_cg_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        const tk_type_decl *dd = &g_cg_prog.items[i].as.type_decl;
        if (cg_name_eq(dd->name, name)) return dd->body.tag == TK_BODY_ENUM;
    }
    return false;
}

// Any TYPE_DECL (struct/variant/enum/alias) named `name`, or NULL.
static const tk_type_decl *cg_find_decl(tk_str name) {
    for (size_t i = 0; i < g_cg_prog.nitems; i += 1) {
        if (g_cg_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        const tk_type_decl *d = &g_cg_prog.items[i].as.type_decl;
        if (cg_name_eq(d->name, name)) return d;
    }
    return NULL;
}

// The top-level FUNCTION `name` in namespace `ns` (for call-arg wrapping — emit_call needs the
// callee's parameter types to wrap a bare case arg into a variant param via emit_as).
static const tk_tfunction *cg_find_function(tk_str ns, tk_str name) {
    for (size_t i = 0; i < g_cg_prog.nitems; i += 1) {
        if (g_cg_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        const tk_tfunction *f = &g_cg_prog.items[i].as.function;
        if (cg_name_eq(f->name, name) && cg_name_eq(f->namespace, ns)) return f;
    }
    return NULL;
}

// Is `name` a builtin scalar (prim/byte/str/error), i.e. carries no user decl?
static bool cg_is_prim_name(tk_str name) {
    static const char *prims[] = { "u8","u16","u32","u64","u128","i8","i16","i32","i64",
                                   "i128","f16","f32","f64","bool","byte","str","error" };
    for (size_t i = 0; i < sizeof prims / sizeof *prims; i += 1)
        if (seg_is(name, prims[i])) return true;
    return false;
}

// =========================================================================
// AUTO-BOX recursive value-type back-edges. A struct field whose (named) type can reach BACK
// to the enclosing type through BY-VALUE embedding (struct field / optional inner / variant
// member / inline-union member — a SLICE is a pointer, not a by-value edge) closes an
// infinite-size cycle in C. Such a field is emitted as a heap POINTER (tk_alloc) instead of
// an inline value: construction mallocs+copies the child, field-access derefs. This mirrors
// the hand-written C twin (`tk_texpr *left`) and rides the S0 tk_alloc seam (the S2 arena swap
// stays mechanical). The VM is unaffected (it boxes every value already).
// =========================================================================
// A VISITED set of type names (a stack buffer — the corpus has few dozen named types). The
// reachability walk dedups against it so a CYCLIC type graph is explored in O(V+E) per query
// (a naive depth-bounded DFS revisits exponentially on cycles → codegen hangs).
typedef struct { tk_str *names; size_t len; size_t cap; } cg_nameset;
static bool cg_nameset_seen(cg_nameset *s, tk_str n) {
    for (size_t i = 0; i < s->len; i += 1) if (cg_name_eq(s->names[i], n)) return true;
    return false;
}
static void cg_nameset_mark(cg_nameset *s, tk_str n) { if (s->len < s->cap) s->names[s->len++] = n; }

static bool cg_name_reaches_byvalue(tk_str from, tk_str to, cg_nameset *seen);

// Does this type-expr's BY-VALUE content reach the named type `to`?
static bool cg_te_reaches_byvalue(tk_type_expr te, tk_str to, cg_nameset *seen) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            tk_str y = te.as.named.path.segments[te.as.named.path.len - 1].name;
            if (cg_is_prim_name(y)) return false;
            if (cg_name_eq(y, to)) return true;
            return cg_name_reaches_byvalue(y, to, seen);
        }
        case TK_TEXPR_OPTIONAL: return cg_te_reaches_byvalue(*te.as.optional.inner, to, seen);   // opt embeds inner by value
        case TK_TEXPR_UNION:
            for (size_t i = 0; i < te.as.uni.len; i += 1)
                if (cg_te_reaches_byvalue(te.as.uni.members[i], to, seen)) return true;
            return false;
        case TK_TEXPR_SLICE: return false;   // a slice is a pointer — not a by-value edge
        default: return false;
    }
}
// Does named type `from` reach named type `to` through by-value edges? (visited-set guarded.)
static bool cg_name_reaches_byvalue(tk_str from, tk_str to, cg_nameset *seen) {
    if (cg_nameset_seen(seen, from)) return false;   // already explored this node
    cg_nameset_mark(seen, from);
    const tk_type_decl *d = cg_find_decl(from);
    if (d == NULL) return false;
    if (d->body.tag == TK_BODY_STRUCT) {
        for (size_t i = 0; i < d->body.as.struct_body.n_fields; i += 1)
            if (cg_te_reaches_byvalue(d->body.as.struct_body.fields[i].type_ann, to, seen)) return true;
        return false;
    }
    if (d->body.tag == TK_BODY_VARIANT) {
        tk_type_expr vt = d->body.as.variant_body.type_expr;
        if (vt.tag == TK_TEXPR_UNION)
            for (size_t i = 0; i < vt.as.uni.len; i += 1)
                if (cg_te_reaches_byvalue(vt.as.uni.members[i], to, seen)) return true;
        return false;
    }
    return false;   // enum/alias — no by-value aggregate edges
}
// Is the struct `sname`'s field of declared type `fte` a recursive back-edge (→ box as pointer)?
// A NAMED field `Y` or an OPTIONAL field `Y?` is boxed iff its by-value content reaches BACK to
// `sname` (closing a cycle): `Y` → `tk_t_Y *`, `Y?` → `tk_opt_Y *`. A SLICE field is already a
// pointer (never boxed). An inline-UNION field is left unboxed (none close a cycle in the corpus;
// the topo pass flags it honestly if one ever does).
static bool cg_field_boxed(tk_str sname, tk_type_expr fte) {
    if (fte.tag != TK_TEXPR_NAMED && fte.tag != TK_TEXPR_OPTIONAL) return false;
    tk_str buf[512]; cg_nameset seen = { buf, 0, 512 };
    return cg_te_reaches_byvalue(fte, sname, &seen);   // does the field's by-value content reach back?
}

// Look up the declared type_expr of field `fname` in struct named `sname` from g_cg_prog.
// Returns true + sets *out if found; false if the struct or field is absent.
static bool cg_find_struct_field_type(tk_str sname, tk_str fname, tk_type_expr *out) {
    for (size_t i = 0; i < g_cg_prog.nitems; i += 1) {
        if (g_cg_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        const tk_type_decl *d = &g_cg_prog.items[i].as.type_decl;
        if (d->body.tag != TK_BODY_STRUCT) continue;
        if (!cg_name_eq(d->name, sname)) continue;
        for (size_t j = 0; j < d->body.as.struct_body.n_fields; j += 1) {
            if (cg_name_eq(d->body.as.struct_body.fields[j].name, fname)) {
                *out = d->body.as.struct_body.fields[j].type_ann;
                return true;
            }
        }
    }
    return false;
}

// Does variant decl `d` have a member whose bare name is `member`? (The member name is
// the union FIELD name and the UPPERCASE tag suffix — emit_type_decl spelling.)
static bool cg_variant_has_member(const tk_type_decl *d, tk_str member) {
    tk_type_expr vt = d->body.as.variant_body.type_expr;
    if (vt.tag != TK_TEXPR_UNION) return false;
    for (size_t i = 0; i < vt.as.uni.len; i += 1) {
        tk_str mn = variant_member_name(vt.as.uni.members[i]);
        if (mn.ptr != NULL && cg_name_eq(mn, member)) return true;
    }
    return false;
}

// OPTIONAL `T?` C REPRESENTATION (REBOOT_PLAN §202). Each distinct optional inner type maps to
// a generated struct `tk_opt_<innerMangle>` = `{ bool present; <innerCtype> value; }`. The
// mangle suffix is a deterministic function of the inner type so the typedef and every use
// agree. `error` lowers to its message `tk_str` (no separate error C value type), so `error?`
// is `tk_opt_error`. The distinct optional types are collected + emitted in tk_emit_c's prelude.
static bool cg_opt_mangle(cbuf *b, tk_type inner, const char **err);   // fwd
static bool cg_variant_typename(cbuf *b, tk_type v, const char **err);          // fwd (B-cg2)
static bool cg_member_key(cbuf *b, tk_type m, const char **err);                // fwd (B-cg2)
static bool cg_member_key_texpr(cbuf *b, tk_type_expr m, const char **err);     // fwd (B-cg2)
static bool cg_opt_mangle_texpr(cbuf *b, tk_type_expr te, const char **err);    // fwd (B-cg2)
static bool cg_opt_typename(cbuf *b, tk_type inner, const char **err) {
    cb(b, "tk_opt_");
    return cg_opt_mangle(b, inner, err);
}

// SLICE value layer (fixed+copy): a `[]T` lowers to the generated `tk_slice_<elemMangle>`
// struct `{ <elemC> *ptr; uint64_t len; }` (same shape as tk_str). Reuses the optional
// element mangle for the suffix, so decl + every use agree byte-for-byte.
static bool cg_slice_typename(cbuf *b, tk_type elem, const char **err) {
    cb(b, "tk_slice_");
    return cg_opt_mangle(b, elem, err);
}

static bool emit_type(cbuf *b, tk_type t, const char **err) {
    switch (t.tag) {
        case TK_TYPE_PRIM: return emit_prim(b, t.as.prim, err);
        case TK_TYPE_BYTE: cb(b, "uint8_t"); return true;
        // VOID is the return-only marker (M.3): a `-> void` function lowers to C `void`.
        // The checker (Z2a) forbids void as a value/binding/variant member, so reaching
        // here is always a Func.ret position.
        case TK_TYPE_VOID:    cb(b, "void");   return true;
        case TK_TYPE_STR:     cb(b, "tk_str"); return true;
        // OPTIONAL (T?) — the generated `tk_opt_<innerMangle>` struct (REBOOT_PLAN §202). The
        // sentinel optional (a bare `null`, inner == NULL) never reaches here on its own: it is
        // always wrapped to a concrete slot type via emit_as.
        case TK_TYPE_OPTIONAL:
            if (t.as.optional.inner == NULL)
                return fail_node(err, "codegen: a bare `null` needs a known optional type from context (internal)");
            return cg_opt_typename(b, *t.as.optional.inner, err);
        // SLICE (`[]T`) — the generated tk_slice_<elem> struct. The sentinel/untyped slice
        // (element == NULL, from teko::list::empty()) never reaches here on its own: emit_as
        // wraps it to a concrete slot type, exactly like a bare `null` optional.
        case TK_TYPE_SLICE:
            if (t.as.slice.element == NULL)
                return fail_node(err, "codegen: an untyped empty slice needs a known element type from context (internal)");
            return cg_slice_typename(b, *t.as.slice.element, err);
        // W4c — a named aggregate references its mangled typedef name (decl + ref agree
        // via mangle_type_name). The semantic NAMED carries only the bare name.
        case TK_TYPE_NAMED:   mangle_type_name(b, (tk_str){ NULL, 0 }, t.as.named.name); return true;
        // VARIANT (`A | B | …`) — the generated `tk_u_<keys>` tagged-union struct (B-cg2).
        // Its typedef is stamped in the prelude (cg_emit_optional_typedefs) from every distinct
        // variant the program uses. A NAMED variant decl is reached via TK_TYPE_NAMED, not here.
        case TK_TYPE_VARIANT: return cg_variant_typename(b, t, err);
        // `error` lowers to its message str (no separate error C value type — REBOOT §202).
        case TK_TYPE_ERROR:   cb(b, "tk_str"); return true;
        case TK_TYPE_FUNC:    return fail_node(err, "codegen: function type not yet supported");
    }
    return fail_node(err, "codegen: unknown type not yet supported");
}

// The deterministic mangle SUFFIX for an optional's inner type (used in tk_opt_<suffix>).
// Names are C-identifier-safe (prim keywords, "str"/"byte"/"error", a named type's bare name,
// and "opt_" + nested for `T??`). Mirrors emit_type's coverage.
static bool cg_opt_mangle(cbuf *b, tk_type inner, const char **err) {
    switch (inner.tag) {
        case TK_TYPE_PRIM:
            switch (inner.as.prim) {
                case TK_PRIM_U8:  cb(b, "u8");  return true;   case TK_PRIM_U16: cb(b, "u16"); return true;
                case TK_PRIM_U32: cb(b, "u32"); return true;   case TK_PRIM_U64: cb(b, "u64"); return true;
                case TK_PRIM_U128:cb(b, "u128");return true;   case TK_PRIM_I8:  cb(b, "i8");  return true;
                case TK_PRIM_I16: cb(b, "i16"); return true;   case TK_PRIM_I32: cb(b, "i32"); return true;
                case TK_PRIM_I64: cb(b, "i64"); return true;   case TK_PRIM_I128:cb(b, "i128");return true;
                case TK_PRIM_F16: cb(b, "f16"); return true;   case TK_PRIM_F32: cb(b, "f32"); return true;
                case TK_PRIM_F64: cb(b, "f64"); return true;   case TK_PRIM_BOOL:cb(b, "bool");return true;
            }
            return fail_node(err, "codegen: unknown prim in optional inner");
        case TK_TYPE_BYTE:  cb(b, "byte");  return true;
        case TK_TYPE_STR:   cb(b, "str");   return true;
        case TK_TYPE_ERROR: cb(b, "error"); return true;
        case TK_TYPE_NAMED: cb_str(b, inner.as.named.name); return true;
        case TK_TYPE_OPTIONAL:
            if (inner.as.optional.inner == NULL) return fail_node(err, "codegen: nested bare-null optional (internal)");
            cb(b, "opt_"); return cg_opt_mangle(b, *inner.as.optional.inner, err);
        default: return fail_node(err, "codegen: optional inner type not yet supported");
    }
}

// =========================================================================
// B-cg2 — VARIANT codegen (the `T | error` sum types the corpus uses pervasively).
//
// A variant member's C-IDENTIFIER-SAFE KEY: the spelling used for BOTH the tag constant
// (TK_TAG_<V>_<UPPER key>) and the union field name (.as.<key>). It extends cg_opt_mangle
// to cover the SLICE member (`slice_<elem>`), so EVERY member kind maps to a key (prim →
// "i32"/"u64"/…, byte → "byte", str → "str", error → "error", named → bare name, slice →
// "slice_<elem>", optional → "opt_<inner>"). The key for a member equals the key the
// optional/slice typedef machinery already uses, so decl + ref + wrap agree byte-for-byte.
// =========================================================================
// A variant member KEY becomes a C struct FIELD identifier (the union member name). A prim
// member like `bool` would yield the invalid field `bool bool;` (C23 keyword) — so a key that
// collides with a C keyword gets a trailing `_`. Applied to the two lowercase key emitters so
// the union field, the construction `.as.<key>`, and the destructure agree; the UPPERCASE tag
// constant (a separate emitter) is unaffected (TK_TAG_..._BOOL is fine).
static bool cg_is_c_keyword(tk_str s) {
    static const char *kw[] = { "bool","int","char","short","long","float","double","void",
        "unsigned","signed","const","volatile","register","auto","static","struct","union",
        "enum","return","default","goto","break","continue","case","switch","do","for","while",
        "if","else","sizeof","typedef","extern","inline","restrict" };
    for (size_t i = 0; i < sizeof kw / sizeof *kw; i += 1) {
        size_t n = strlen(kw[i]);
        if (s.len == n && memcmp(s.ptr, kw[i], n) == 0) return true;
    }
    return false;
}

// Emit a user IDENTIFIER (variable / parameter / binding name), escaping a C keyword with a
// trailing `_` so e.g. a parameter named `signed` becomes `signed_` (invalid `bool signed`
// otherwise). Applied at the definition AND every use so they agree.
static void cb_ident(cbuf *b, tk_str name) {
    cb_str(b, name);
    if (cg_is_c_keyword(name)) cb(b, "_");
}

// (#49) a top-level user FUNCTION's C name = its namespace mangled (`::` → `_`) + `__` + the bare
// name, so same-named functions across namespaces (and libc clashes like `div`) never collide.
// An empty namespace (a local/builtin) → the bare (keyword-escaped) name. Def and call agree by
// using this for BOTH the definition/prototype and the resolved call (TCall.call_ns).
static void cb_fn_name(cbuf *b, tk_str ns, tk_str name) {
    if (ns.len != 0) {
        for (size_t i = 0; i < ns.len; i += 1) {
            char one[2] = { ns.ptr[i] == ':' ? '_' : (char)ns.ptr[i], '\0' };
            cb(b, one);
        }
        cb(b, "__");
    }
    cb_ident(b, name);
}

static bool cg_member_key(cbuf *b, tk_type m, const char **err) {
    if (m.tag == TK_TYPE_SLICE) {
        if (m.as.slice.element == NULL)
            return fail_node(err, "codegen: an untyped slice variant member needs a known element type (internal)");
        cb(b, "slice_");
        return cg_opt_mangle(b, *m.as.slice.element, err);
    }
    cbuf k = { NULL, 0, 0 };
    if (!cg_opt_mangle(&k, m, err)) { tk_free0(k.ptr); return false; }
    tk_str ks = { (const tk_byte *)k.ptr, k.len };
    cb_str(b, ks);
    if (cg_is_c_keyword(ks)) cb(b, "_");
    tk_free0(k.ptr);
    return true;
}

// The deterministic C type NAME of an INLINE (anonymous) variant `A | B | …`: `tk_u_` then
// each member's key joined by `_`, in SOURCE ORDER (so the name is a pure function of the
// member list — decl, every signature/field reference, and every wrap agree). The matching
// typedef (tag enum + tag/union struct, SAME shape as a named variant) is emitted ONCE in
// the prelude by the collection pass.
static bool cg_variant_typename(cbuf *b, tk_type v, const char **err) {
    cb(b, "tk_u");
    for (size_t i = 0; i < v.as.variant.len; i += 1) {
        cb(b, "_");
        if (!cg_member_key(b, v.as.variant.members[i], err)) return false;
    }
    return true;
}

// Emit the C TYPE for any variant-shaped resolved type: a NAMED variant decl → its mangled
// `tk_t_<Name>`; an inline `A | B | …` → its `tk_u_<keys>`. (A NAMED non-variant aggregate
// reaches emit_type's NAMED arm, never here.)
static bool cg_emit_variant_ctype(cbuf *b, tk_type v, const char **err) {
    if (v.tag == TK_TYPE_NAMED) { mangle_type_name(b, (tk_str){ NULL, 0 }, v.as.named.name); return true; }
    return cg_variant_typename(b, v, err);
}

// =========================================================================
// W4c — a SYNTACTIC type-expr (parser tk_type_expr) -> C type text. Needed for type-decl
// member positions (struct field annotations, variant member types), where the typed item
// carries the SYNTACTIC body, not the resolved `tk_type`. A NAMED type-expr's last path
// segment is either a built-in name (u8..f64/bool/byte/str -> its C type) or a user type
// (-> its mangled typedef). Slice/union/optional members are honest barriers (later waves).
// The deterministic optional-mangle SUFFIX for a SYNTACTIC inner type-expr (the struct-field /
// member position twin of cg_opt_mangle). Must agree byte-for-byte with cg_opt_mangle so a
// field's `T?` type references the SAME tk_opt_<suffix> the prelude declares.
static bool cg_opt_mangle_texpr(cbuf *b, tk_type_expr te, const char **err) {
    if (te.tag == TK_TEXPR_NAMED) {
        tk_path p = te.as.named.path;
        tk_str last = p.segments[p.len - 1].name;
        cb_str(b, last);   // prim keyword / str / byte / error / a named type's bare name — all C-safe
        return true;
    }
    if (te.tag == TK_TEXPR_OPTIONAL) { cb(b, "opt_"); return cg_opt_mangle_texpr(b, *te.as.optional.inner, err); }
    if (te.tag == TK_TEXPR_SLICE)    { cb(b, "slice_"); return cg_opt_mangle_texpr(b, *te.as.slice.element, err); }
    return fail_node(err, "codegen: optional inner type-expr not yet supported");
}

// The SYNTACTIC twin of cg_variant_typename — the inline-union `tk_u_<keys>` name from a
// UnionType type-expr (a signature/field position). Each member's key via cg_opt_mangle_texpr,
// which agrees byte-for-byte with cg_member_key's resolved-type keys, so a `T | error` in a
// signature references the SAME tk_u_… the prelude declares from its resolved type.
static bool cg_variant_typename_texpr(cbuf *b, tk_type_expr te, const char **err) {
    cb(b, "tk_u");
    for (size_t i = 0; i < te.as.uni.len; i += 1) {
        cb(b, "_");
        if (!cg_opt_mangle_texpr(b, te.as.uni.members[i], err)) return false;
    }
    return true;
}

static bool emit_type_expr(cbuf *b, tk_type_expr te, const char **err) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            tk_path p = te.as.named.path;
            tk_str last = p.segments[p.len - 1].name;
            if      (seg_is(last, "u8"))    { cb(b, "uint8_t");           return true; }
            else if (seg_is(last, "u16"))   { cb(b, "uint16_t");          return true; }
            else if (seg_is(last, "u32"))   { cb(b, "uint32_t");          return true; }
            else if (seg_is(last, "u64"))   { cb(b, "uint64_t");          return true; }
            else if (seg_is(last, "u128"))  { cb(b, "unsigned __int128"); return true; }
            else if (seg_is(last, "i8"))    { cb(b, "int8_t");            return true; }
            else if (seg_is(last, "i16"))   { cb(b, "int16_t");           return true; }
            else if (seg_is(last, "i32"))   { cb(b, "int32_t");           return true; }
            else if (seg_is(last, "i64"))   { cb(b, "int64_t");           return true; }
            else if (seg_is(last, "i128"))  { cb(b, "__int128");          return true; }
            else if (seg_is(last, "f16"))   { cb(b, "_Float16");          return true; }
            else if (seg_is(last, "f32"))   { cb(b, "float");             return true; }
            else if (seg_is(last, "f64"))   { cb(b, "double");            return true; }
            else if (seg_is(last, "bool"))  { cb(b, "bool");              return true; }
            else if (seg_is(last, "byte"))  { cb(b, "uint8_t");           return true; }
            else if (seg_is(last, "str"))   { cb(b, "tk_str");            return true; }
            else if (seg_is(last, "error")) { cb(b, "tk_str"); return true; }   // error → its message str (REBOOT §202)
            // a TRANSPARENT alias (`type Name = <type-expr>`) emits NO C type of its own — resolve
            // through to the aliased type-expr at every use site (e.g. a `TypeTable` field = []TypeReg
            // → tk_slice_TypeReg). Matches the checker, which resolves aliases transparently.
            {
                const tk_type_decl *ad = cg_find_decl(last);
                if (ad != NULL && ad->body.tag == TK_BODY_ALIAS)
                    return emit_type_expr(b, ad->body.as.alias_body.alias, err);
            }
            // a user-defined named aggregate -> its mangled typedef name (matches emit_type).
            mangle_type_name(b, (tk_str){ NULL, 0 }, last);
            return true;
        }
        // SLICE `[]T` in a SIGNATURE / field position → the generated `tk_slice_<elem>` struct
        // (same name cg_slice_typename produces from the resolved type, so decl + ref agree; the
        // typedef is stamped in the prelude from the function's return/body usage). W-backend.
        case TK_TEXPR_SLICE:    cb(b, "tk_slice_"); return cg_opt_mangle_texpr(b, *te.as.slice.element, err);
        // INLINE union `A | B | …` in a signature/field position → the generated tk_u_<keys>
        // tagged-union struct (B-cg2); the typedef is stamped in the prelude from its resolved type.
        case TK_TEXPR_UNION:    return cg_variant_typename_texpr(b, te, err);
        case TK_TEXPR_OPTIONAL: {
            // `T?` field/member position → the generated tk_opt_<innerMangle> struct (REBOOT §202).
            cb(b, "tk_opt_");
            return cg_opt_mangle_texpr(b, *te.as.optional.inner, err);
        }
    }
    return fail_node(err, "codegen: unknown type expression not yet supported");
}

// =========================================================================
// Operator token kind -> C operator text.
// =========================================================================
static const char *binop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_PLUS:    return "+";
        case TK_TOKEN_MINUS:   return "-";
        case TK_TOKEN_STAR:    return "*";
        case TK_TOKEN_SLASH:   return "/";
        case TK_TOKEN_PERCENT: return "%";
        case TK_TOKEN_AMP:     return "&";
        case TK_TOKEN_PIPE:    return "|";
        case TK_TOKEN_CARET:   return "^";
        case TK_TOKEN_SHL:     return "<<";
        case TK_TOKEN_SHR:     return ">>";
        case TK_TOKEN_ANDAND:  return "&&";
        case TK_TOKEN_OROR:    return "||";
        default:               return NULL;   // unsupported binary op
    }
}

// =========================================================================
// F3 GUARDS (M.1 — fail loud): the prim helpers that pick the right runtime
// guard. Integer prims only; bool is never a guard target.
// =========================================================================
// These mirror the type.h predicates (tk_prim_is_*) but stay local so the F3 logic is
// self-contained; they are now exhaustive over the Tier-1 prim set (incl. 128 + floats).
static bool cg_prim_is_float(tk_prim_kind k) { return tk_prim_is_float(k); }
static bool cg_prim_is_int(tk_prim_kind k)   { return tk_prim_is_int(k); }   // NOT bool, NOT float
static bool cg_prim_is_signed_int(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8: case TK_PRIM_I16: case TK_PRIM_I32:
        case TK_PRIM_I64: case TK_PRIM_I128: return true;
        default: return false;
    }
}
// rank: bit-width ordinal (8/16/32/64/128) for narrowing analysis (ints only).
static int cg_int_width(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   case TK_PRIM_I8:   return 8;
        case TK_PRIM_U16:  case TK_PRIM_I16:  return 16;
        case TK_PRIM_U32:  case TK_PRIM_I32:  return 32;
        case TK_PRIM_U64:  case TK_PRIM_I64:  return 64;
        case TK_PRIM_U128: case TK_PRIM_I128: return 128;
        default: return 0;
    }
}
// The tag ("u8".."i128", "f16".."f64") naming a div/mod helper, e.g. tk_div_u32 /
// tk_div_f64. Returns NULL for bool (never a guard target).
static const char *prim_div_tag(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:  return "u8";   case TK_PRIM_U16: return "u16";
        case TK_PRIM_U32: return "u32";  case TK_PRIM_U64: return "u64";
        case TK_PRIM_U128: return "u128";
        case TK_PRIM_I8:  return "i8";   case TK_PRIM_I16: return "i16";
        case TK_PRIM_I32: return "i32";  case TK_PRIM_I64: return "i64";
        case TK_PRIM_I128: return "i128";
        case TK_PRIM_F16: return "f16";  case TK_PRIM_F32: return "f32";
        case TK_PRIM_F64: return "f64";
        case TK_PRIM_BOOL: return NULL;
    }
    return NULL;
}
// The integer-target tag ("u8".."i128") used to name a cast helper. NULL for non-ints.
static const char *prim_int_tag(tk_prim_kind k) {
    return cg_prim_is_int(k) ? prim_div_tag(k) : NULL;
}
// The float-source suffix ("f16"/"f32"/"f64") for a float->int cast helper. NULL otherwise.
static const char *prim_float_tag(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_F16: return "f16";
        case TK_PRIM_F32: return "f32";
        case TK_PRIM_F64: return "f64";
        default: return NULL;
    }
}

// An INT->INT cast src->dst may lose data (needs a runtime guard) when SOME value of the
// source type falls outside the destination's representable range. Conservative:
// if we can't prove the source range ⊆ dest range, guard. (Floats handled separately.)
static bool cast_may_lose(tk_prim_kind src, tk_prim_kind dst) {
    bool ss = cg_prim_is_signed_int(src), ds = cg_prim_is_signed_int(dst);
    int sw = cg_int_width(src), dw = cg_int_width(dst);
    if (ss == ds)          return sw > dw;          // same signedness: only true narrowing loses
    if (!ss && ds)         return sw >= dw;         // u->i: dest loses one bit of magnitude at equal width
    /* ss && !ds */        return true;             // i->u: negatives never fit
}

static const char *unop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_MINUS: return "-";
        case TK_TOKEN_TILDE: return "~";
        case TK_TOKEN_BANG:  return "!";
        default:             return NULL;
    }
}

static const char *cmpop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_EQEQ: return "==";
        case TK_TOKEN_NE:   return "!=";
        case TK_TOKEN_LT:   return "<";
        case TK_TOKEN_LE:   return "<=";
        case TK_TOKEN_GT:   return ">";
        case TK_TOKEN_GE:   return ">=";
        default:            return NULL;
    }
}

static const char *assignop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_ASSIGN:    return "=";
        case TK_TOKEN_PLUSEQ:    return "+=";
        case TK_TOKEN_MINUSEQ:   return "-=";
        case TK_TOKEN_STAREQ:    return "*=";
        case TK_TOKEN_SLASHEQ:   return "/=";
        case TK_TOKEN_PERCENTEQ: return "%=";
        case TK_TOKEN_AMPEQ:     return "&=";
        case TK_TOKEN_PIPEEQ:    return "|=";
        case TK_TOKEN_CARETEQ:   return "^=";
        case TK_TOKEN_SHLEQ:     return "<<=";
        case TK_TOKEN_SHREQ:     return ">>=";
        default:                 return NULL;
    }
}

// =========================================================================
// W5a — `if`-as-a-VALUE + IMPLICIT trailing-return (differential parity with the VM).
// =========================================================================
// Forward decls — the W5a value/tail machinery is mutually recursive with emit_expr (an
// `if`-value branch contains statements, a statement contains an `if`-value expression).
// `ret_type` is the ENCLOSING function's return type (W5b): it lets a `return`/implicit
// tail-return WRAP a case value into a variant-typed return slot (emit_as). It is `void`
// in contexts where no variant wrap can apply (virtual-main, if-value sub-expressions).
// a diverging call (the global `panic`/`exit`) — unqualified; mirrors the checker's
// tk_texpr_diverges. In a VALUE position it lowers to `(<call>, (T){0})`: the call is _Noreturn
// (tk_panic_str/tk_exit), so the zero-of-T is unreachable but keeps the C `?:`/expr well-typed.
static bool cg_expr_diverges(const tk_texpr *e) {
    if (e->tag != TK_TEXPR_CALL || e->as.call.callee.len != 1) return false;
    tk_str last = e->as.call.callee.segments[0].name;
    return seg_is(last, "panic") || seg_is(last, "exit");
}

static bool emit_expr(cbuf *b, const tk_texpr *e, const char **err);
static bool emit_stmt(cbuf *b, const tk_tstatement *s, bool in_main,
                      tk_type ret_type, const char *indent, const char **err);
// W5b — emit `value` into a slot whose EXPECTED type is `expected`. When `expected` is a
// (named) variant and `value`'s type is one of its case members, WRAP the value into the
// variant's `tag + union` representation; otherwise emit the value plainly. (The VM needs
// no wrap because its struct value already carries the case name — this synthesizes the
// C wrap, completing the W4c-deferred variant-value layer.)
static bool emit_as(cbuf *b, tk_type expected, const tk_texpr *value, const char **err);
// Host-FFI lifting (Phase 7): a builtin whose Teko return is `T|error` / `error?` / `[]str`
// is emitted as a statement-expression that calls a fixed-ABI runtime primitive (teko_rt.h)
// and lifts the result into the program's generated result type (e->type). Defined after the
// variant-wrap helpers it leans on; the CALL case dispatches to it by builtin name.
enum cg_ffi_kind { CG_FFI_SRES, CG_FFI_URES, CG_FFI_SLRES, CG_FFI_U64RES, CG_FFI_ARGS, CG_FFI_RUN };
static bool emit_host_ffi(cbuf *b, int kind, const char *rtfn, const tk_texpr *e, const char **err);
// W5b — a `match` lowered to a GNU statement-expression (the VALUE form).
static bool emit_match_value(cbuf *b, const tk_texpr *e, const char **err);
// W5b — a `match` in TAIL position (each arm body `return`s) and in STATEMENT position
// (each arm body runs for effect). `ret_type` threads the enclosing fn return type so a
// tail arm body that yields a case value into a variant return slot is wrapped (emit_as).
static bool emit_match_tail(cbuf *b, const tk_texpr *e, bool in_main,
                            tk_type ret_type, const char *indent, const char **err);
static bool emit_match_stmt(cbuf *b, const tk_texpr *e, bool in_main, tk_type ret_type, const char *indent, const char **err);
// Arm bodies are STATEMENT BLOCKS (B.20); these tail/block emitters lower them. Forward-
// declared here so the match value form (emit_arm_value) can route a diverging arm.
static bool emit_block(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                       tk_type ret_type, const char *indent, const char **err);
static bool emit_block_tail(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                            tk_type ret_type, const char *indent, const char **err);
static bool emit_exprstmt_tail(cbuf *b, const tk_texpr *x, bool in_main,
                               tk_type ret_type, const char *indent, const char **err);
static bool emit_if_stmt(cbuf *b, const tk_texpr *e, bool in_main,
                         tk_type ret_type, const char *indent, const char **err);
// cb_upper is defined with the type-decl emitter below; the W5b tag-constant emission
// (here) needs it too.
static void cb_upper(cbuf *b, tk_str s);

// Emit ONE branch statement in VALUE position: its trailing value is assigned to `sink`.
//   * an EXPR-statement carrying a value -> `<sink> = <expr>;` (this is the branch's value);
//   * a RETURN / break / continue inside a branch DIVERGES — but an `if` used as a SUB-
//     expression cannot express that as a value, so it is an honest barrier here, EXACTLY
//     matching the VM (tk_vm_eval_expr's TK_TEXPR_IF stops on a divergent branch). A trailing
//     `if` in STATEMENT/TAIL position takes the direct-control-flow path (emit_exprstmt_tail),
//     which DOES carry return/break/continue — so this restriction only bites the sub-expr use.
//   * anything else (void expr-stmt, loop, binding, assign) -> emitted normally (no value).
static bool emit_stmt_value(cbuf *b, const tk_tstatement *s, tk_type slot, const char *sink,
                            const char *indent, const char **err) {
    if (s->tag == TK_TSTMT_RETURN || s->tag == TK_TSTMT_BREAK || s->tag == TK_TSTMT_CONTINUE)
        return fail_node(err, "codegen: control flow inside an `if` used as a sub-expression not yet supported");
    if (s->tag == TK_TSTMT_EXPR && s->as.expr_stmt.expr.type.tag != TK_TYPE_VOID) {
        cb(b, indent);
        cb(b, sink); cb(b, " = ");
        // wrap the branch value into the if's RESULT type (`slot`) — a bare case / `[]case` / null
        // lands in the variant/optional/slice result, exactly like a match arm's sink (emit_as).
        if (!emit_as(b, slot, &s->as.expr_stmt.expr, err)) return false;
        cb(b, ";\n");
        return true;
    }
    // A non-value statement in an if-value branch carries no variant return slot: pass
    // void ret_type (emit_as never wraps for void).
    return emit_stmt(b, s, /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, indent, err);
}

// Emit a BRANCH block (the `if`-value then/else): all but the last statement normally, the
// LAST via emit_stmt_value (its trailing value -> `sink`). An empty branch yields nothing
// (the checker forbids a value-typed empty branch).
static bool emit_branch_value(cbuf *b, const tk_tstatement *body, size_t n, tk_type slot,
                              const char *sink, const char *indent, const char **err) {
    for (size_t i = 0; i + 1 < n; i += 1)
        if (!emit_stmt(b, &body[i], /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, indent, err)) return false;
    if (n > 0)
        if (!emit_stmt_value(b, &body[n - 1], slot, sink, indent, err)) return false;
    return true;
}

// emit_if_value — lower an `if` USED AS A SUB-VALUE to a GNU statement-expression:
//   ({ <Ctype> _tkN; if (<cond>) { <then…> } else { <else…> } _tkN; })
// Each branch assigns its trailing value to `_tkN`. `<Ctype>` is the if-node's resolved
// branch type. clang/gcc accept stmt-exprs under the host cc (the codebase already targets
// cc -std=c23). The temp name `_tk<buflen>` uses the CURRENT buffer length as a purely
// functional uniquifier: every append grows the buffer, and a NESTED statement-expr is
// emitted only after the outer one has appended bytes, so its name strictly differs.
// (VM parity: tk_vm_eval_expr's TK_TEXPR_IF runs exec_if and yields the taken branch's
// trailing NORMAL value; a divergent branch is an honest stop — see emit_stmt_value.)
static bool emit_if_value(cbuf *b, const tk_texpr *e, const char **err) {
    if (!e->as.if_expr.has_else)
        return fail_node(err, "codegen: if-without-else used as a value not yet supported");
    // Freeze a unique temp name (buffer length is about to change as we append).
    char tmp[32];
    snprintf(tmp, sizeof tmp, "_tk%zu", (size_t)b->len);
    cb(b, "({ ");
    if (!emit_type(b, e->type, err)) return false;
    cb(b, " "); cb(b, tmp); cb(b, "; if (");
    if (!emit_expr(b, e->as.if_expr.cond, err)) return false;
    cb(b, ") {\n");
    if (!emit_branch_value(b, e->as.if_expr.then_blk, e->as.if_expr.nthen, e->type, tmp, "    ", err))
        return false;
    cb(b, "} else {\n");
    if (!emit_branch_value(b, e->as.if_expr.else_blk, e->as.if_expr.nelse, e->type, tmp, "    ", err))
        return false;
    cb(b, "} "); cb(b, tmp); cb(b, "; })");
    return true;
}

// =========================================================================
// Expressions -> C expression text.
// =========================================================================
static bool emit_expr(cbuf *b, const tk_texpr *e, const char **err) {
    switch (e->tag) {
        case TK_TEXPR_NUMBER: {
            // A numeric literal adopted to `byte` via literal-adoption (e.g. `c < 0x20` where
            // c: byte). Byte is not TK_TYPE_PRIM — emit as ((uint8_t)<value>).
            if (e->type.tag == TK_TYPE_BYTE) {
                cb(b, "((uint8_t)"); cb_i128(b, e->as.number.value); cb(b, ")");
                return true;
            }
            // The node's resolved prim decides width / float-kind (N1/N2). A non-prim
            // number type is a checker bug, but be honest rather than mis-emit (M.3).
            if (e->type.tag != TK_TYPE_PRIM)
                return fail_node(err, "codegen: numeric literal with a non-primitive type not yet supported");
            tk_prim_kind k = e->type.as.prim;
            if (e->as.number.is_float) {
                // Float literal: emit the double with round-trippable precision, then make
                // the C literal the right type. f64 plain; f32 cast `(float)`; f16 cast
                // `(_Float16)`. (Casting beats suffixes for exactness across widths.)
                switch (k) {
                    case TK_PRIM_F64: cb_f64_literal(b, e->as.number.fval); return true;
                    case TK_PRIM_F32:
                        cb(b, "(float)("); cb_f64_literal(b, e->as.number.fval); cb(b, ")");
                        return true;
                    case TK_PRIM_F16:
                        cb(b, "(_Float16)("); cb_f64_literal(b, e->as.number.fval); cb(b, ")");
                        return true;
                    default:
                        return fail_node(err, "codegen: float literal with a non-float type not yet supported");
                }
            }
            // Integer literal: emit the decimal value, cast to the prim's C type so the
            // literal carries the right width/signedness (e.g. a u128 literal is wider
            // than any C integer literal can spell on its own).
            cb(b, "((");
            if (!emit_prim(b, k, err)) return false;
            cb(b, ")");
            cb_i128(b, e->as.number.value);
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_VAR:
            cb_ident(b, e->as.var.name);
            return true;

        case TK_TEXPR_BINARY: {
            tk_token_kind bop = e->as.binary.op;
            // F3 (M.1): `/` and `%` go through a checked runtime helper that PANICS on a
            // zero divisor instead of UB/SIGFPE. Single-eval: each operand emitted once,
            // passed by value. The helper width comes from the node's result prim.
            if (bop == TK_TOKEN_SLASH || bop == TK_TOKEN_PERCENT) {
                if (e->type.tag != TK_TYPE_PRIM)
                    return fail_node(err, "codegen: division/modulo on a non-primitive type not yet supported");
                tk_prim_kind pk = e->type.as.prim;
                // `%` is invalid on floats (no C float modulo operator). Ruling: error.
                if (bop == TK_TOKEN_PERCENT && cg_prim_is_float(pk))
                    return fail_node(err, "codegen: modulo on a float type is not allowed");
                const char *tag = prim_div_tag(pk);
                if (tag == NULL)   // bool — never an arithmetic target
                    return fail_node(err, "codegen: division/modulo on a non-numeric type not yet supported");
                // Float `/` goes through tk_div_f64/f32/f16 (ruling §5: float ÷0 PANICS,
                // parity with int). Int `/`,`%` go through tk_div_*/tk_mod_* as before.
                cb(b, bop == TK_TOKEN_SLASH ? "tk_div_" : "tk_mod_");
                cb(b, tag);
                cb(b, "(");
                if (!emit_expr(b, e->as.binary.left, err)) return false;
                cb(b, ", ");
                if (!emit_expr(b, e->as.binary.right, err)) return false;
                cb(b, ")");
                return true;
            }
            // NOTE (out of scope): +,-,* stay plain C — overflow guarding is DEFERRED to
            // build profiles (panic-debug / wrap-release), which don't exist yet.
            const char *op = binop_c(bop);
            if (op == NULL) return fail_node(err, "codegen: binary operator not yet supported");
            cb(b, "(");
            if (!emit_expr(b, e->as.binary.left, err)) return false;
            cb(b, " "); cb(b, op); cb(b, " ");
            if (!emit_expr(b, e->as.binary.right, err)) return false;
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_UNARY: {
            const char *op = unop_c(e->as.unary.op);
            if (op == NULL) return fail_node(err, "codegen: unary operator not yet supported");
            cb(b, "("); cb(b, op);
            if (!emit_expr(b, e->as.unary.operand, err)) return false;
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_COMPARE: {
            // chained a<b<c -> (a<b) && (b<c) over ADJACENT operands.
            size_t nrest = e->as.compare.nrest;
            if (nrest == 0) {   // a lone subject is degenerate; emit it bare
                return emit_expr(b, e->as.compare.first, err);
            }
            cb(b, "(");
            const tk_texpr *prev = e->as.compare.first;
            for (size_t i = 0; i < nrest; i += 1) {
                tk_tcmp_term term = e->as.compare.rest[i];
                const char *op = cmpop_c(term.op);
                if (op == NULL) return fail_node(err, "codegen: comparison operator not yet supported");
                if (i > 0) cb(b, " && ");
                // str equality/inequality lowers to tk_str_eq (a tk_str is a {ptr,len} struct — C
                // `==` is invalid on it). `a == b` → tk_str_eq(a,b); `a != b` → !tk_str_eq(a,b).
                bool str_cmp = (prev->type.tag == TK_TYPE_STR || term.operand->type.tag == TK_TYPE_STR)
                               && (term.op == TK_TOKEN_EQEQ || term.op == TK_TOKEN_NE);
                if (str_cmp) {
                    cb(b, "(");
                    if (term.op == TK_TOKEN_NE) cb(b, "!");
                    cb(b, "tk_str_eq(");
                    if (!emit_expr(b, prev, err)) return false;
                    cb(b, ", ");
                    if (!emit_expr(b, term.operand, err)) return false;
                    cb(b, "))");
                } else {
                    cb(b, "(");
                    if (!emit_expr(b, prev, err)) return false;
                    cb(b, " "); cb(b, op); cb(b, " ");
                    if (!emit_expr(b, term.operand, err)) return false;
                    cb(b, ")");
                }
                prev = term.operand;
            }
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_CAST: {
            // F3 (M.1): `x to T` is checked at runtime where a value could fail to fit T:
            //   * int -> int  : range-check IFF the cast may narrow (tk_to_<dst>_s/_u).
            //   * float -> int : ALWAYS checked — truncate toward zero, panic on
            //                    NaN/inf/out-of-range (tk_to_<dst>_from_<fsrc>). (Ruling §5.)
            //   * int -> float : widening; plain C cast (no panic — float covers the range
            //                    even when it loses low-order precision, which is allowed).
            //   * float -> float : f64->f32->f16 narrowing — plain C cast; precision loss
            //                    is allowed (ruling §5).
            // Single-eval: the operand is emitted exactly once in every path.
            const tk_texpr *inner = e->as.cast.expr;
            bool prim_both = e->type.tag == TK_TYPE_PRIM && inner->type.tag == TK_TYPE_PRIM;
            tk_prim_kind dst = prim_both ? e->type.as.prim : TK_PRIM_BOOL;
            tk_prim_kind src = prim_both ? inner->type.as.prim : TK_PRIM_BOOL;

            // float -> int: always guard (truncate + panic on NaN/inf/out-of-range).
            if (prim_both && cg_prim_is_float(src) && cg_prim_is_int(dst)) {
                const char *dtag = prim_int_tag(dst);
                const char *fsrc = prim_float_tag(src);
                cb(b, "tk_to_"); cb(b, dtag); cb(b, "_from_"); cb(b, fsrc); cb(b, "(");
                if (!emit_expr(b, inner, err)) return false;
                cb(b, ")");
                return true;
            }
            // int -> int: range-checked iff it may narrow.
            if (prim_both && cg_prim_is_int(src) && cg_prim_is_int(dst)
                && cast_may_lose(src, dst)) {
                const char *dtag = prim_int_tag(dst);
                // Carrier picked by SOURCE signedness so it holds the source losslessly:
                //   signed source -> __int128 carrier ("_s"); unsigned -> unsigned __int128 ("_u").
                cb(b, "tk_to_"); cb(b, dtag);
                cb(b, cg_prim_is_signed_int(src) ? "_s(" : "_u(");
                if (!emit_expr(b, inner, err)) return false;
                cb(b, ")");
                return true;
            }
            // Everything else (int->float widen, float->float, widening int->int,
            // same-type) is a lossless-or-precision-allowed plain C cast.
            cb(b, "((");
            if (!emit_type(b, e->type, err)) return false;   // target rides the node's .type
            cb(b, ")(");
            if (!emit_expr(b, inner, err)) return false;
            cb(b, "))");
            return true;
        }

        case TK_TEXPR_CALL: {
            // callee path -> C identifier joined by "__" (single-segment in M0).
            tk_path p = e->as.call.callee;
            // E2 (native, DEGRADED): err_loc/err_typed adorn an error VALUE with diagnostic
            // position/types. Native `error` is lowered to its message tk_str (the error-STRUCT
            // representation is a Phase-6 follow-on), so natively these are NO-OPs returning the
            // error (arg0) UNCHANGED — the program keeps the message; expr-position precision is
            // the VM's (full) vs native's (degraded to the item position). Mirrors codegen.tks.
            if (p.len >= 1) {
                tk_str pe = p.segments[p.len - 1].name;
                if (seg_is(pe, "err_loc") || seg_is(pe, "err_typed")) {
                    cb(b, "("); if (!emit_expr(b, &e->as.call.args[0], err)) return false; cb(b, ")");
                    return true;
                }
                // Phase 3 — `str`/`str_of_bytes` (([]byte) -> str). A []byte lowers to the generated
                // struct `tk_slice_byte {uint8_t*,uint64_t}`, a DISTINCT C type from `tk_str
                // {const tk_byte*,size_t}` — passing one for the other is a C constraint violation. So
                // BRIDGE the slice value to a tk_str at the call site (single-eval via a temp), then
                // tk_str_of_bytes COPIES it into a fresh owned str. (`str` here is the call-builtin.)
                if ((p.len == 1 || seg_is(p.segments[0].name, "teko"))
                    && (seg_is(pe, "str") || seg_is(pe, "str_of_bytes")) && e->as.call.nargs == 1) {
                    char sb[40]; snprintf(sb, sizeof sb, "_sb%zu", (size_t)b->len);
                    cb(b, "({ ");
                    if (!emit_type(b, e->as.call.args[0].type, err)) return false;   // tk_slice_byte
                    cb(b, " "); cb(b, sb); cb(b, " = ");
                    if (!emit_expr(b, &e->as.call.args[0], err)) return false;
                    cb(b, "; tk_str_of_bytes((tk_str){ (const tk_byte *)"); cb(b, sb);
                    cb(b, ".ptr, "); cb(b, sb); cb(b, ".len }); })");
                    return true;
                }
            }
            // teko::list::empty / push — the SLICE (collection) builtins, FIXED+COPY. `empty()`
            // (sentinel) is normally wrapped by emit_as into a concrete slot literal; reaching
            // here directly means a context-less empty (honest error). `push(base, item)` lowers
            // to an inline copy-append GNU stmt-expr (alloc len+1 via malloc/abort — M.1 — copy
            // the old elements, append a COPY of item, yield the fresh tk_slice_<elem>).
            if (p.len >= 2 && seg_is(p.segments[0].name, "teko")
                           && seg_is(p.segments[p.len - 2].name, "list")) {
                tk_str llast = p.segments[p.len - 1].name;
                if (seg_is(llast, "empty")) {
                    if (e->type.tag == TK_TYPE_SLICE && e->type.as.slice.element != NULL) {
                        cb(b, "("); if (!cg_slice_typename(b, *e->type.as.slice.element, err)) return false;
                        cb(b, "){ .ptr = 0, .len = 0 }");
                        return true;
                    }
                    return fail_node(err, "codegen: teko::list::empty() needs a known slice type from context (annotate the binding)");
                }
                if (seg_is(llast, "push")) {
                    tk_type st = e->type;   // the push result type is []elem
                    if (st.tag != TK_TYPE_SLICE)
                        return fail_node(err, "codegen: teko::list::push result is not a slice (internal)");
                    // type_assign may have overwritten the call's type to the sentinel when
                    // assigning push(xs, item) back to a sentinel-typed variable xs. Recover
                    // the element from args[0] (concretized by type_list_builtin to []elem).
                    tk_type inferred_elem;
                    if (st.as.slice.element == NULL && e->as.call.nargs == 2) {
                        tk_type base = e->as.call.args[0].type;
                        if (base.tag == TK_TYPE_SLICE && base.as.slice.element != NULL) {
                            inferred_elem = *base.as.slice.element;
                            st.as.slice.element = &inferred_elem;
                        } else {
                            inferred_elem = e->as.call.args[1].type;
                            if (!(inferred_elem.tag == TK_TYPE_SLICE && inferred_elem.as.slice.element == NULL))
                                st.as.slice.element = &inferred_elem;
                        }
                    }
                    if (st.as.slice.element == NULL)
                        return fail_node(err, "codegen: teko::list::push result is not a concrete slice (internal)");
                    tk_type elem = *st.as.slice.element;
                    // `teko::list::push(base, item)` → the AMORTIZED runtime grow (tk_slice_push):
                    //   ({ tk_slice_T _sb = <base>; T _si = <item>; uint64_t _sl;
                    //      T *_sp = (T*)tk_slice_push(_sb.ptr, _sb.len, &_si, sizeof(T), &_sl);
                    //      (tk_slice_T){ .ptr = _sp, .len = _sl }; })
                    // Geometric growth with an alias-safe live-tail cache (value semantics preserved)
                    // — O(1) amortized vs the old malloc+copy-every-push O(n) (→ O(n²) on a buffer).
                    char bN[40], iN[40], pN[40], lN[40];
                    snprintf(bN, sizeof bN, "_sb%zu", (size_t)b->len);
                    snprintf(iN, sizeof iN, "_si%zu", (size_t)b->len + 1);
                    snprintf(pN, sizeof pN, "_sp%zu", (size_t)b->len + 2);
                    snprintf(lN, sizeof lN, "_sl%zu", (size_t)b->len + 3);
                    cb(b, "({ ");
                    if (!cg_slice_typename(b, elem, err)) return false; cb(b, " "); cb(b, bN); cb(b, " = ");
                    // Use emit_as so a sentinel empty() arg is lowered to the concrete empty literal.
                    { tk_type base_t = { .tag = TK_TYPE_SLICE, .as.slice.element = &elem };
                      if (!emit_as(b, base_t, &e->as.call.args[0], err)) return false; } cb(b, "; ");
                    if (!emit_type(b, elem, err)) return false; cb(b, " "); cb(b, iN); cb(b, " = ");
                    if (!emit_as(b, elem, &e->as.call.args[1], err)) return false; cb(b, "; ");
                    cb(b, "uint64_t "); cb(b, lN); cb(b, "; ");
                    if (!emit_type(b, elem, err)) return false; cb(b, " *"); cb(b, pN);
                    cb(b, " = ("); if (!emit_type(b, elem, err)) return false;
                    cb(b, " *)tk_slice_push("); cb(b, bN); cb(b, ".ptr, "); cb(b, bN); cb(b, ".len, &"); cb(b, iN);
                    cb(b, ", sizeof("); if (!emit_type(b, elem, err)) return false; cb(b, "), &"); cb(b, lN); cb(b, "); (");
                    if (!cg_slice_typename(b, elem, err)) return false;
                    cb(b, "){ .ptr = "); cb(b, pN); cb(b, ", .len = "); cb(b, lN); cb(b, " }; })");
                    return true;
                }
                return fail_node(err, "codegen: this teko::list builtin not yet supported (only empty/push — fixed+copy)");
            }
            // Host-FFI bottoms with a VARIANT/OPTIONAL or argv return: lifted from a fixed-ABI
            // runtime result into the program's result type (the runtime can't name the generated
            // sum/optional structs — teko_rt.h). Intercepted BY NAME (these names are not user-
            // shadowed: the only definitions are pure FFI forwarders), ahead of the plain map.
            if (p.len >= 1) {
                tk_str l = p.segments[p.len - 1].name;
                bool addr = (p.len == 1) || seg_is(p.segments[0].name, "teko");
                if (addr) {
                    if (seg_is(l, "read_file"))     return emit_host_ffi(b, CG_FFI_SRES,   "tk_rt_read_file",     e, err);
                    if (seg_is(l, "var"))           return emit_host_ffi(b, CG_FFI_SRES,   "tk_rt_getenv",        e, err);
                    if (seg_is(l, "write_file"))    return emit_host_ffi(b, CG_FFI_URES,   "tk_rt_write_file",    e, err);
                    if (seg_is(l, "chdir"))         return emit_host_ffi(b, CG_FFI_URES,   "tk_rt_chdir",         e, err);
                    if (seg_is(l, "mkdir"))         return emit_host_ffi(b, CG_FFI_URES,   "tk_rt_mkdir",         e, err);
                    if (seg_is(l, "cwd"))           return emit_host_ffi(b, CG_FFI_SRES,   "tk_rt_getcwd",        e, err);
                    if (seg_is(l, "set_var"))       return emit_host_ffi(b, CG_FFI_URES,   "tk_rt_setenv",        e, err);
                    if (seg_is(l, "list_dir"))      return emit_host_ffi(b, CG_FFI_SLRES,  "tk_rt_list_dir",      e, err);
                    if (seg_is(l, "last_index_of")) return emit_host_ffi(b, CG_FFI_U64RES, "tk_rt_last_index_of", e, err);
                    if (seg_is(l, "args"))          return emit_host_ffi(b, CG_FFI_ARGS,   "tk_rt_args",          e, err);
                    if (seg_is(l, "run"))           return emit_host_ffi(b, CG_FFI_RUN,    "tk_rt_run",           e, err);
                }
            }
            // Non-shadowable built-ins: `print`/`println`, either bare or under `teko`.
            const char *builtin = NULL;
            if (p.len >= 1) {
                tk_str last = p.segments[p.len - 1].name;
                bool addressable = (p.len == 1) || seg_is(p.segments[0].name, "teko");
                if (addressable) {
                    if      (seg_is(last, "print"))   builtin = "tk_print";
                    else if (seg_is(last, "println")) builtin = "tk_println";
                    else if (seg_is(last, "panic"))   builtin = "tk_panic_str";   // panic(str) — diverges (no `never` type)
                    else if (seg_is(last, "exit"))    builtin = "tk_exit";        // exit(<int>) — diverges
                    // host output FFI bottoms (scope.c) — (str) -> void. write=stdout; e*=stderr.
                    else if (seg_is(last, "write"))    builtin = "tk_write";
                    else if (seg_is(last, "ewrite"))   builtin = "tk_ewrite";
                    else if (seg_is(last, "eprint"))   builtin = "tk_eprint";
                    else if (seg_is(last, "eprintln")) builtin = "tk_eprintln";
                    else if (seg_is(last, "parse"))    builtin = "tk_float_parse"; // teko::float::parse(str) -> f64
                    // arithmetic FFI over the i128 carrier (sign-aware) + float bit-patterns — args
                    // flow through the generic loop; these have plain scalar returns. (`div`/`rem`
                    // mangle to tk_* so they never clash with libc's div/rem.)
                    else if (seg_is(last, "div"))          builtin = "tk_div";          // (i128,i128,bool) -> i128
                    else if (seg_is(last, "rem"))          builtin = "tk_rem";          // (i128,i128,bool) -> i128
                    else if (seg_is(last, "fdiv"))         builtin = "tk_fdiv";         // (f64,f64) -> f64
                    else if (seg_is(last, "int_to_float")) builtin = "tk_int_to_float"; // (i128,bool) -> f64
                    else if (seg_is(last, "f64_bits"))     builtin = "tk_f64_bits";     // (f64) -> u64
                    else if (seg_is(last, "f64_from_bits"))builtin = "tk_f64_from_bits";// (u64) -> f64
                    // D3 — test-coverage sink (host side-channel): the VM marks executed production fns.
                    else if (seg_is(last, "cov_reset"))    builtin = "tk_cov_reset";    // () -> void
                    else if (seg_is(last, "cov_mark"))     builtin = "tk_cov_mark";     // (u64) -> void
                    else if (seg_is(last, "cov_distinct")) builtin = "tk_cov_distinct"; // () -> u64
                    else if (seg_is(last, "cov_is_marked"))   builtin = "tk_cov_is_marked";   // (u64) -> bool
                    // D3-branch — branch-coverage sink (only records when ON; off by default).
                    else if (seg_is(last, "cov_branches_on")) builtin = "tk_cov_branches_on"; // (bool) -> void
                    else if (seg_is(last, "cov_branch_reset"))builtin = "tk_cov_branch_reset";// () -> void
                    else if (seg_is(last, "cov_enter"))       builtin = "tk_cov_enter";       // (u64) -> void
                    else if (seg_is(last, "cov_leave"))       builtin = "tk_cov_leave";       // () -> void
                    else if (seg_is(last, "cov_branch"))      builtin = "tk_cov_branch";      // (u32,u32,u64) -> void
                    else if (seg_is(last, "cov_branch_hit"))  builtin = "tk_cov_branch_hit";  // (u64,u32,u32,u64) -> bool
                    else if (seg_is(last, "cov_lines_on"))    builtin = "tk_cov_lines_on";    // (bool) -> void
                    else if (seg_is(last, "cov_line_reset"))  builtin = "tk_cov_line_reset";  // () -> void
                    else if (seg_is(last, "cov_line"))        builtin = "tk_cov_line";        // (u32) -> void
                    else if (seg_is(last, "cov_line_hit"))    builtin = "tk_cov_line_hit";    // (u64,u32) -> bool
                    // diverging runtime panic helpers (the corpus calls these by bare name)
                    else if (seg_is(last, "panic_div0"))     builtin = "tk_panic_div0";
                    else if (seg_is(last, "panic_oob"))      builtin = "tk_panic_oob";
                    else if (seg_is(last, "panic_cast"))     builtin = "tk_panic_cast";
                    else if (seg_is(last, "panic_overflow")) builtin = "tk_panic_overflow";
                    // str/byte STDLIB surface (Phase 3) — the unqualified helpers the corpus calls,
                    // recognized by the checker (scope.c tk_builtin_fn) and lowered to their tk_ runtime
                    // twins (teko_rt). These return a fresh tk_str (malloc + tk_panic on OOM, M.1) and
                    // their args flow unchanged through the generic arg loop below. (`str`/`str_of_bytes`
                    // are NOT here: a []byte lowers to the generated struct tk_slice_byte, a DISTINCT C
                    // type from tk_str, so they are bridged specially ABOVE. In a CALL `str` is this
                    // builtin, not the str TYPE — that's emit_type.)
                    else if (seg_is(last, "one_byte"))    builtin = "tk_one_byte";       // (byte) -> str
                    else if (seg_is(last, "str_concat"))  builtin = "tk_str_concat";     // (str, str) -> str
                    else if (seg_is(last, "str_concat3")) builtin = "tk_str_concat3";    // (str, str, str) -> str
                    else if (seg_is(last, "concat"))      builtin = "tk_str_concat";     // teko::str::concat
                    else if (seg_is(last, "concat3"))     builtin = "tk_str_concat3";    // teko::str::concat3
                    else if (seg_is(last, "slice"))       builtin = "tk_str_slice";      // str::slice(str,u64,u64) -> str
                    else if (seg_is(last, "slice_to"))    builtin = "tk_str_slice_to";   // (str,u64) -> str
                    else if (seg_is(last, "slice_from"))  builtin = "tk_str_slice_from"; // (str,u64) -> str
                    else if (seg_is(last, "len"))         builtin = "tk_str_len";        // (str) -> u64
                    else if (seg_is(last, "ends_with"))   builtin = "tk_str_ends_with";  // (str,str) -> bool
                    else if (seg_is(last, "contains"))    builtin = "tk_str_contains";   // (str,str) -> bool
                    // (last_index_of returns u64|error → lifted by emit_host_ffi above, not here)
                    else if (seg_is(last, "i64_to_str"))  builtin = "tk_i64_to_str";     // (i64) -> str
                    else if (seg_is(last, "u64_to_str"))  builtin = "tk_u64_to_str";     // (u64) -> str
                    else if (seg_is(last, "ftoa"))        builtin = "tk_ftoa";           // (f64) -> str
                    else if (seg_is(last, "f64_g17"))     builtin = "tk_f64_g17";        // (f64) -> str (host float renderer)
                    // (err_loc/err_typed handled DEGRADED at the top of this CALL case — native
                    //  error is message-only; the error-struct representation is a Phase-6 follow-on.)
                }
            }
            if (builtin != NULL) {
                cb(b, builtin);
            } else if (e->as.call.call_ns.len != 0) {
                // (#49) a resolved USER call → the SAME namespace-mangled C name emit_function_sig
                // gives the definition (teko::checker::type_eq → teko__checker__type_eq), so def +
                // call agree and same-named functions across namespaces never collide.
                cb_fn_name(b, e->as.call.call_ns, p.segments[p.len - 1].name);
            } else {
                // No resolved namespace (a local fn-value, or a name not carried) → the bare
                // (keyword-escaped) last segment.
                cb_ident(b, p.segments[p.len - 1].name);
            }
            // A resolved USER call: wrap each arg into its parameter type (emit_as) so a bare case
            // value (e.g. `Prim{…}` passed where a `Type` variant is expected) lands in the variant
            // rep — the arg keeps its case type (type_call no longer clobbers widened args). For a
            // NAMED param only (variant/struct/enum); other param shapes emit plainly, as before.
            const tk_tfunction *cf = (e->as.call.call_ns.len != 0)
                ? cg_find_function(e->as.call.call_ns, p.segments[p.len - 1].name) : NULL;
            cb(b, "(");
            for (size_t i = 0; i < e->as.call.nargs; i += 1) {
                if (i > 0) cb(b, ", ");
                if (cf != NULL && i < cf->nparams && cf->params[i].type_ann.tag == TK_TEXPR_NAMED) {
                    tk_path pp = cf->params[i].type_ann.as.named.path;
                    tk_type exp = { .tag = TK_TYPE_NAMED, .as.named.name = pp.segments[pp.len - 1].name };
                    if (!emit_as(b, exp, &e->as.call.args[i], err)) return false;
                } else if (!emit_expr(b, &e->as.call.args[i], err)) return false;
            }
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_FIELD_ACCESS: {
            // W5-idx — `.len`: a `str` lowers to the runtime tk_str's `.len` member
            // (`(recv).len`); a slice is an honest barrier (slice VALUES are the next
            // feature). Otherwise a plain struct-field read `(recv.field)`.
            tk_type rt = e->as.field_access.receiver->type;
            if (rt.tag == TK_TYPE_SLICE && seg_is(e->as.field_access.field, "len")) {
                cb(b, "(");   // `.len` of a tk_slice_<elem> — same {ptr,len} shape as tk_str → u64
                if (!emit_expr(b, e->as.field_access.receiver, err)) return false;
                cb(b, ".len)");
                return true;
            }
            // E2 (native, DEGRADED): an error VALUE is lowered to its message tk_str (no struct
            // yet — Phase-6 follow-on). So `e.message` IS that tk_str; the position/type adornments
            // (file/line/col/expected/actual) have no native carrier and degrade to defaults
            // (line/col -> 0u32, file/expected/actual -> empty str). The VM carries them in full.
            if (rt.tag == TK_TYPE_ERROR) {
                tk_str f = e->as.field_access.field;
                if (seg_is(f, "message")) {        // error IS its message tk_str natively
                    cb(b, "("); if (!emit_expr(b, e->as.field_access.receiver, err)) return false; cb(b, ")");
                } else if (seg_is(f, "line") || seg_is(f, "col")) {
                    cb(b, "((uint32_t)0)");         // degraded position (the Phase-6 error-struct fills it)
                } else {                            // file/expected/actual -> empty str
                    cb(b, "((tk_str){0})");
                }
                return true;
            }
            // auto-box: reading a recursive back-edge field derefs the heap pointer.
            bool fa_boxed = false;
            if (rt.tag == TK_TYPE_NAMED) {
                tk_type_expr fte;
                if (cg_find_struct_field_type(rt.as.named.name, e->as.field_access.field, &fte))
                    fa_boxed = cg_field_boxed(rt.as.named.name, fte);
            }
            cb(b, "(");
            if (fa_boxed) cb(b, "*");
            cb(b, "(");
            if (!emit_expr(b, e->as.field_access.receiver, err)) return false;
            cb(b, ".");
            cb_str(b, e->as.field_access.field);
            cb(b, "))");
            return true;
        }

        case TK_TEXPR_STR: {
            // tk_str compound literal with an EXPLICIT length — the decoded bytes may
            // contain NUL, so we never lean on strlen/sizeof (M.1).
            cb(b, "(tk_str){ (const tk_byte *)\"");
            cb_cstr_escaped(b, e->as.str.text);
            cb(b, "\", ");
            cb_i64(b, (int64_t)e->as.str.text.len);
            cb(b, " }");
            return true;
        }

        case TK_TEXPR_BYTE:
            // byte == uint8_t; emit its decimal value (0..255).
            cb_i64(b, (int64_t)e->as.byte.value);
            return true;

        case TK_TEXPR_BOOL:
            // bool literal (W2) — bool already flows through both backends, so this is
            // FULL support: emit the C `true`/`false` keyword (the C type is `bool`).
            cb(b, e->as.boolean.value ? "true" : "false");
            return true;

        case TK_TEXPR_IF:    return emit_if_value(b, e, err);   // W5a — `if` as a value (GNU stmt-expr)
        case TK_TEXPR_MATCH: return emit_match_value(b, e, err); // W5b — `match` as a value (GNU stmt-expr)

        // null / ?. / ?? — the OPTIONAL value model (REBOOT_PLAN §202/§203; tk_opt_<inner>).
        case TK_TEXPR_NULL:
            // A bare `null` in raw expression position has the SENTINEL optional type (inner
            // NULL): it cannot be emitted alone — its concrete type comes from the destination
            // slot via emit_as. Reaching here means a null with no slot context (a checker/route
            // gap) — honest stop rather than mis-emit.
            return fail_node(err, "codegen: a bare `null` needs a known optional type from context (use it where a `T?` is expected)");
        case TK_TEXPR_SAFE_FIELD_ACCESS: {
            // `recv?.field`: ({ <optTy> _tN = (recv); _tN.present
            //      ? (<resTy>){ .present = true, .value = _tN.value.<field> }
            //      : (<resTy>){ .present = false }; })
            // The node's `.type` is the result optional `(field)?`; the receiver type is `T?`.
            tk_type recvT = e->as.safe_field_access.receiver->type;
            if (recvT.tag != TK_TYPE_OPTIONAL || recvT.as.optional.inner == NULL)
                return fail_node(err, "codegen: safe field access on a non-optional receiver (internal)");
            char tmp[40]; snprintf(tmp, sizeof tmp, "_o%zu", (size_t)b->len);
            cb(b, "({ "); if (!emit_type(b, recvT, err)) return false;
            cb(b, " "); cb(b, tmp); cb(b, " = (");
            if (!emit_expr(b, e->as.safe_field_access.receiver, err)) return false;
            cb(b, "); "); cb(b, tmp); cb(b, ".present ? (");
            if (!emit_type(b, e->type, err)) return false;            // result optional type
            cb(b, "){ .present = true, .value = "); cb(b, tmp); cb(b, ".value.");
            cb_str(b, e->as.safe_field_access.field);
            cb(b, " } : ("); if (!emit_type(b, e->type, err)) return false;
            cb(b, "){ .present = false }; })");
            return true;
        }
        case TK_TEXPR_COALESCE: {
            // `a ?? b`: ({ <optTy> _tN = (a); _tN.present ? <a.value as result> : <b as result>; })
            // Short-circuits b (only evaluated when a is NONE). The result type (the node's
            // `.type`) is `T` (unwrap) or `T?` (b itself optional); both arms are wrapped to it
            // via emit_as — so `T? ?? T` unwraps and `T? ?? T?` stays optional.
            tk_type leftT = e->as.coalesce.left->type;
            if (leftT.tag != TK_TYPE_OPTIONAL || leftT.as.optional.inner == NULL)
                return fail_node(err, "codegen: `??` left operand is not optional (internal)");
            char tmp[40]; snprintf(tmp, sizeof tmp, "_c%zu", (size_t)b->len);
            cb(b, "({ "); if (!emit_type(b, leftT, err)) return false;
            cb(b, " "); cb(b, tmp); cb(b, " = (");
            if (!emit_expr(b, e->as.coalesce.left, err)) return false;
            cb(b, "); "); cb(b, tmp); cb(b, ".present ? ");
            // a's inner value `_tN.value` flows into the result. If the result is `T` (unwrap),
            // emit it plainly; if the result is `T?` (b itself optional), wrap a.value PRESENT
            // into the result optional. (a.value already has the inner's C type.)
            if (e->type.tag == TK_TYPE_OPTIONAL && e->type.as.optional.inner != NULL) {
                cb(b, "("); if (!emit_type(b, e->type, err)) return false;
                cb(b, "){ .present = true, .value = "); cb(b, tmp); cb(b, ".value }");
            } else {
                cb(b, tmp); cb(b, ".value");
            }
            cb(b, " : ");
            if (cg_expr_diverges(e->as.coalesce.right)) {
                // a diverging fallback (panic/exit) yields no value — emit the _Noreturn call, then a
                // zero-of-result so the C `?:` type-checks (the zero is unreachable).
                cb(b, "(");
                if (!emit_expr(b, e->as.coalesce.right, err)) return false;
                cb(b, ", ("); if (!emit_type(b, e->type, err)) return false; cb(b, "){0})");
            } else {
                if (!emit_as(b, e->type, e->as.coalesce.right, err)) return false;   // fallback, wrapped to result
            }
            cb(b, "; })");
            return true;
        }
        case TK_TEXPR_STRUCT_INIT: {
            // W4c — `Name { f = v, … }` -> a C compound literal in DECLARED field order
            //   (tk_t_<MANGLE>){ .<field> = <val>, … }
            // The checker already reordered field_names/field_vals into the struct's
            // declared order (expr.c), so both backends lower identically (differential
            // equivalence with the VM, which stores fields in the same order).
            //
            // The node's `.type` is TK_TYPE_NAMED (a user struct -> mangle its name) or
            // TK_TYPE_ERROR (`error { message = … }`). `error` lowers to its message `tk_str`
            // (REBOOT §202): `error { message = s }` IS the str value `s` (the checker fixed
            // the single `message` field), so the VM's struct value (one "message" str field)
            // and the C str agree observably.
            if (e->type.tag == TK_TYPE_ERROR) {
                if (e->as.struct_init.nfields != 1)
                    return fail_node(err, "codegen: error value must have exactly the `message` field (internal)");
                return emit_expr(b, &e->as.struct_init.field_vals[0], err);
            }
            if (e->type.tag != TK_TYPE_NAMED)
                return fail_node(err, "codegen: struct literal with a non-named type not yet supported");
            cb(b, "(");
            mangle_type_name(b, (tk_str){ NULL, 0 }, e->type.as.named.name);
            cb(b, "){ ");
            for (size_t i = 0; i < e->as.struct_init.nfields; i += 1) {
                if (i > 0) cb(b, ", ");
                cb(b, ".");
                cb_str(b, e->as.struct_init.field_names[i]);
                cb(b, " = ");
                const tk_texpr *fv = &e->as.struct_init.field_vals[i];
                // Look up the declared field type once: it drives both the sentinel/null literal
                // forms AND auto-boxing of a recursive back-edge field.
                tk_type_expr fte; bool have_fte =
                    cg_find_struct_field_type(e->type.as.named.name,
                                              e->as.struct_init.field_names[i], &fte);
                // auto-box: a recursive back-edge field stores a heap pointer — malloc + copy the
                // child value into it (`({ T *_bx = tk_alloc(sizeof *_bx); *_bx = (val); _bx; })`).
                bool boxed = have_fte && cg_field_boxed(e->type.as.named.name, fte);
                size_t bxid = b->len;
                if (boxed) {
                    cb(b, "({ ");
                    if (!emit_type_expr(b, fte, err)) return false;
                    cb(b, " *_bx"); cb_i64(b, (int64_t)bxid);
                    cb(b, " = tk_alloc(sizeof *_bx"); cb_i64(b, (int64_t)bxid); cb(b, "); *_bx"); cb_i64(b, (int64_t)bxid); cb(b, " = ");
                }
                // Sentinel field: emit a concrete literal from the declared field type.
                // Covers: empty-slice fields (element==NULL) and null/absent optional fields.
                bool emitted = false;
                if (fv->type.tag == TK_TYPE_SLICE && fv->type.as.slice.element == NULL) {
                    if (have_fte && fte.tag == TK_TEXPR_SLICE && fte.as.slice.element != NULL) {
                        cb(b, "(");
                        if (!emit_type_expr(b, fte, err)) return false;
                        cb(b, "){ .ptr = 0, .len = 0 }");
                        emitted = true;
                    }
                } else if (fv->tag == TK_TEXPR_NULL) {
                    if (have_fte && fte.tag == TK_TEXPR_OPTIONAL) {
                        cb(b, "(");
                        if (!emit_type_expr(b, fte, err)) return false;
                        cb(b, "){ .present = false }");
                        emitted = true;
                    }
                }
                if (!emitted) {
                    // Build the expected resolved type from the field's type-expr and route through
                    // emit_as so a bare case value WRAPS into a variant field (`.kind = <wrap>`), and
                    // a `[]case` slice REBUILDS into a `[]variant` field (covariant). emit_as passes a
                    // non-variant slot (struct/enum/alias, matching slice) straight through.
                    tk_type exp = { .tag = TK_TYPE_VOID }; tk_type elemT;
                    if (have_fte && fte.tag == TK_TEXPR_NAMED) {
                        tk_str fn = fte.as.named.path.segments[fte.as.named.path.len - 1].name;
                        exp = (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = fn };
                    } else if (have_fte && fte.tag == TK_TEXPR_OPTIONAL && fte.as.optional.inner != NULL
                               && fte.as.optional.inner->tag == TK_TEXPR_NAMED) {
                        // a present value into a `T?` field (T named) → emit_as wraps T → T?
                        // ({ .present = true, .value = … }). (A bare `null` took the branch above.)
                        tk_path ip = fte.as.optional.inner->as.named.path;
                        elemT = (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = ip.segments[ip.len - 1].name };
                        exp = (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional.inner = &elemT };
                    } else if (have_fte && fte.tag == TK_TEXPR_SLICE
                               && fte.as.slice.element->tag == TK_TEXPR_NAMED
                               && !cg_is_prim_name(fte.as.slice.element->as.named.path.segments[fte.as.slice.element->as.named.path.len - 1].name)) {
                        tk_path ep = fte.as.slice.element->as.named.path;
                        elemT = (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = ep.segments[ep.len - 1].name };
                        exp = (tk_type){ .tag = TK_TYPE_SLICE, .as.slice.element = &elemT };
                    }
                    if (exp.tag != TK_TYPE_VOID) { if (!emit_as(b, exp, fv, err)) return false; }
                    else if (!emit_expr(b, fv, err)) return false;
                }
                if (boxed) { cb(b, "; _bx"); cb_i64(b, (int64_t)bxid); cb(b, "; })"); }
            }
            cb(b, " }");
            return true;
        }

        case TK_TEXPR_INTERP: {
            // $"…{expr}…" (self-host parity) — lower to nested tk_str_concat over the piece
            // literals and the holes: `pieces[0] ++ str(holes[0]) ++ pieces[1] ++ …`. A piece
            // literal is the SAME (tk_str){…} emission as TK_TEXPR_STR (decoded bytes, explicit
            // length — may contain NUL). A `str` hole is the str expr directly; an INTEGER hole
            // is wrapped in tk_i64_to_str/tk_u64_to_str (selected by the hole's signedness) — the
            // SAME runtime builders the VM calls, so VM==codegen byte-for-byte. The concats nest
            // left-to-right so the build order matches the VM's accumulator. tk_str_concat is
            // declared in teko_rt.h (the generated TU's runtime header).
            size_t np = e->as.interp.npieces, nh = e->as.interp.nholes;
            size_t nsegs = np + nh;            // total segments (pieces interleaved with holes)
            // nesting: concat(concat(…concat(S0, S1)…, S(n-2)), S(n-1)). Open one
            // tk_str_concat( for each segment after S0 (nsegs - 1 of them).
            for (size_t s = 1; s < nsegs; s += 1) cb(b, "tk_str_concat(");
            for (size_t s = 0; s < nsegs; s += 1) {
                if (s >= 1) cb(b, ", ");       // the second arg of this segment's concat(
                if ((s & 1u) == 0) {           // a PIECE literal (segment 2*i → piece i)
                    tk_str piece = e->as.interp.pieces[s / 2];
                    cb(b, "(tk_str){ (const tk_byte *)\"");
                    cb_cstr_escaped(b, piece);
                    cb(b, "\", ");
                    cb_i64(b, (int64_t)piece.len);
                    cb(b, " }");
                } else {                       // a HOLE (segment 2*i+1 → hole i)
                    const tk_texpr *h = &e->as.interp.holes[s / 2];
                    if (h->type.tag == TK_TYPE_STR) {
                        if (!emit_expr(b, h, err)) return false;       // str passthrough
                    } else if (h->type.tag == TK_TYPE_PRIM && tk_prim_is_int(h->type.as.prim)) {
                        bool sgn = tk_prim_is_signed(h->type.as.prim);
                        cb(b, sgn ? "tk_i64_to_str((int64_t)(" : "tk_u64_to_str((uint64_t)(");
                        if (!emit_expr(b, h, err)) return false;
                        cb(b, "))");
                    } else {
                        return fail_node(err, "codegen: interpolation hole must be a str or an integer");
                    }
                }
                if (s >= 1) cb(b, ")");        // close this segment's tk_str_concat(
            }
            return true;
        }

        case TK_TEXPR_PATH:
            // Enum::Member as a VALUE → the C enum constant `TK_E_<UPPER enum>_<UPPER member>`
            // (the SAME spelling codegen's enum-decl emitter gives each member).
            cb(b, "TK_E_");
            cb_upper(b, e->as.path.enum_name);
            cb(b, "_");
            cb_upper(b, e->as.path.member);
            return true;

        case TK_TEXPR_INDEX: {
            // W5-idx — `recv[index]`. A `str` receiver lowers to a BOUNDS-CHECKED byte access
            // as a GNU statement-expression (single-eval of recv + idx, then the guard):
            //   ({ tk_str _sN = <recv>; uint64_t _iN = <idx>; _iN < _sN.len ? _sN.ptr[_iN]
            //                                              : (tk_panic_oob(), (tk_byte)0); })
            // tk_panic_oob is declared in teko_rt.h (the generated TU's runtime header), so an
            // out-of-bounds index PANICS with the SAME message as the native path (M.1) — the
            // VM's eval_index routes through the identical panic. A slice receiver is an honest
            // barrier (slice VALUES are the next feature).
            tk_type rt = e->as.index.receiver->type;
            if (rt.tag == TK_TYPE_SLICE) {
                // slice subscript: the SAME bounds-checked stmt-expr as str, with the element
                // type substituted for tk_byte and a 0-of-element fallback after the panic.
                if (rt.as.slice.element == NULL)
                    return fail_node(err, "codegen: indexing an untyped empty slice (internal: checker should reject)");
                tk_type elem = *rt.as.slice.element;
                char stmp[40], itmp[40];
                snprintf(stmp, sizeof stmp, "_ls%zu", (size_t)b->len);
                snprintf(itmp, sizeof itmp, "_li%zu", (size_t)b->len + 1);
                cb(b, "({ "); if (!emit_type(b, elem, err)) return false;
                cb(b, " const *"); cb(b, stmp); cb(b, " = (");
                if (!emit_expr(b, e->as.index.receiver, err)) return false; cb(b, ").ptr; uint64_t ");
                cb(b, itmp); cb(b, " = (uint64_t)(");
                if (!emit_expr(b, e->as.index.index, err)) return false; cb(b, "); ");
                cb(b, itmp); cb(b, " < (");
                if (!emit_expr(b, e->as.index.receiver, err)) return false; cb(b, ").len ? ");
                cb(b, stmp); cb(b, "["); cb(b, itmp); cb(b, "]");
                cb(b, " : (tk_panic_oob_at("); cb_u64_dec(b, e->line); cb(b, ", "); cb_u64_dec(b, e->col); cb(b, "), (");
                if (!emit_type(b, elem, err)) return false; cb(b, "){0}); })");
                return true;
            }
            if (rt.tag != TK_TYPE_STR)
                return fail_node(err, "codegen: indexing a non-string value not yet supported");
            // Freeze unique temp names (buffer length is the functional uniquifier — see emit_if_value).
            char stmp[40], itmp[40];
            snprintf(stmp, sizeof stmp, "_s%zu", (size_t)b->len);
            snprintf(itmp, sizeof itmp, "_i%zu", (size_t)b->len + 1);   // +1 so it differs from stmp
            cb(b, "({ tk_str "); cb(b, stmp); cb(b, " = ");
            if (!emit_expr(b, e->as.index.receiver, err)) return false;
            cb(b, "; uint64_t "); cb(b, itmp); cb(b, " = (uint64_t)(");
            if (!emit_expr(b, e->as.index.index, err)) return false;
            cb(b, "); ");
            cb(b, itmp); cb(b, " < "); cb(b, stmp); cb(b, ".len ? ");
            cb(b, stmp); cb(b, ".ptr["); cb(b, itmp); cb(b, "]");
            cb(b, " : (tk_panic_oob_at("); cb_u64_dec(b, e->line); cb(b, ", "); cb_u64_dec(b, e->col); cb(b, "), (tk_byte)0); })");
            return true;
        }

        case TK_TEXPR_IN: {
            // Phase 2 — `<lhs> in [ e0, e1, … ]` -> bool. The lhs is EVALUATED ONCE (single-eval),
            // bound to a temp via a GNU statement-expression, then compared to each element with `==`
            // OR'd together (true iff the lhs equals any element). The node's `.type` is bool.
            //   ({ <CtypeOfLhs> _invN = <lhs>; (_invN == <e0>) || (_invN == <e1>) || …; })
            // The temp `_inv<buflen>` uses the CURRENT buffer length as the functional uniquifier
            // (see emit_if_value / emit_index). The EMPTY set `x in []` still EVALUATES the lhs once
            // (single-eval), then yields false — so a side-effecting lhs runs identically in the VM
            // (which evaluates the lhs before its zero-iteration loop). VM==native even for `[]`.
            if (e->as.in_expr.nelems == 0) {
                cb(b, "({ (void)(");
                if (!emit_expr(b, e->as.in_expr.lhs, err)) return false;
                cb(b, "); 0; })");
                return true;
            }
            char tmp[40];
            snprintf(tmp, sizeof tmp, "_inv%zu", (size_t)b->len);
            cb(b, "({ ");
            if (!emit_type(b, e->as.in_expr.lhs->type, err)) return false;   // the lhs's resolved C type
            cb(b, " "); cb(b, tmp); cb(b, " = ");
            if (!emit_expr(b, e->as.in_expr.lhs, err)) return false;
            cb(b, "; ");
            for (size_t i = 0; i < e->as.in_expr.nelems; i += 1) {
                if (i > 0) cb(b, " || ");
                cb(b, "("); cb(b, tmp); cb(b, " == ");
                if (!emit_expr(b, &e->as.in_expr.elems[i], err)) return false;
                cb(b, ")");
            }
            cb(b, "; })");
            return true;
        }
        case TK_TEXPR_ARRAY: {
            // [ e0, … ] (Increment B+) -> a tk_slice_<elem>. Empty [] -> the empty literal; non-empty
            // allocates N elements (M.1 OOM-abort), stores each via emit_as, yields { .ptr, .len }.
            tk_type elem = (e->type.tag == TK_TYPE_SLICE && e->type.as.slice.element)
                         ? *e->type.as.slice.element : (tk_type){ .tag = TK_TYPE_VOID };
            size_t nelem = e->as.array.nelements;
            if (nelem == 0) {
                cb(b, "("); if (!cg_slice_typename(b, elem, err)) return false;
                cb(b, "){ .ptr = 0, .len = 0 }");
                return true;
            }
            char pN[40]; snprintf(pN, sizeof pN, "_arr%zu", (size_t)b->len);
            char cnt[24]; snprintf(cnt, sizeof cnt, "%zu", nelem);
            cb(b, "({ ");
            if (!emit_type(b, elem, err)) return false;
            cb(b, " *"); cb(b, pN); cb(b, " = malloc("); cb(b, cnt); cb(b, " * sizeof(");
            if (!emit_type(b, elem, err)) return false;
            cb(b, ")); if (!"); cb(b, pN); cb(b, ") abort(); ");
            for (size_t i = 0; i < nelem; i += 1) {
                char idx[24]; snprintf(idx, sizeof idx, "%zu", i);
                cb(b, pN); cb(b, "["); cb(b, idx); cb(b, "] = ");
                if (!emit_as(b, elem, &e->as.array.elements[i], err)) return false;
                cb(b, "; ");
            }
            cb(b, "("); if (!cg_slice_typename(b, elem, err)) return false;
            cb(b, "){ .ptr = "); cb(b, pN); cb(b, ", .len = "); cb(b, cnt); cb(b, " }; })");
            return true;
        }
    }
    return fail_node(err, "codegen: unknown expression not yet supported");
}

// =========================================================================
// W5b — VARIANT-VALUE WRAPPING (the W4c-deferred variant-value layer).
//
// The C value model for a variant (emit_type_decl) is `tag + union as`; a case value is
// the struct of one member. When a case value flows into a variant-typed SLOT (a binding
// whose declared type is a variant, or a return into a variant-returning fn), it must be
// wrapped into that representation:
//   (tk_t_<Variant>){ .tag = TK_TAG_<UPPER Variant>_<UPPER member>, .as.<member> = <value> }
// The tag suffix and the union field name are spelled EXACTLY as emit_type_decl spells
// them (cb_upper for the tag, the member's bare name for the field), so wrap + decl agree.
// The VM needs no wrap (its struct value already carries the case name); this synthesizes
// the C equivalent. When `expected` is not a variant, or `value` is already the variant
// (not a bare member), the value is emitted plainly.
// =========================================================================
// ---- variant-wrap helpers (uniform over an INLINE `tk_u_…` and a NAMED variant decl) ----
// Member COUNT of a variant slot.
static size_t cg_var_nmem(tk_type v) {
    if (v.tag == TK_TYPE_VARIANT) return v.as.variant.len;
    if (v.tag == TK_TYPE_NAMED) {
        const tk_type_decl *d = cg_find_variant_decl(v.as.named.name);
        if (d && d->body.tag == TK_BODY_VARIANT && d->body.as.variant_body.type_expr.tag == TK_TEXPR_UNION)
            return d->body.as.variant_body.type_expr.as.uni.len;
    }
    return 0;
}
// Member i's KEY → out (caller frees out->ptr).
static bool cg_var_mkey(tk_type v, size_t i, cbuf *out, const char **err) {
    if (v.tag == TK_TYPE_VARIANT) return cg_member_key(out, v.as.variant.members[i], err);
    const tk_type_decl *d = cg_find_variant_decl(v.as.named.name);
    return cg_member_key_texpr(out, d->body.as.variant_body.type_expr.as.uni.members[i], err);
}
// Member i AS a tk_type for recursion: an inline member passes through; a NAMED member →
// {NAMED,name}; anything else → VOID (not a recursable variant).
static tk_type cg_var_mtype(tk_type v, size_t i) {
    if (v.tag == TK_TYPE_VARIANT) return v.as.variant.members[i];
    const tk_type_decl *d = cg_find_variant_decl(v.as.named.name);
    tk_type_expr te = d->body.as.variant_body.type_expr.as.uni.members[i];
    if (te.tag == TK_TEXPR_NAMED) {
        tk_str last = te.as.named.path.segments[te.as.named.path.len - 1].name;
        return (tk_type){ .tag = TK_TYPE_NAMED, .as.named.name = last };
    }
    return (tk_type){ .tag = TK_TYPE_VOID };
}
// Is `mt` a (named or inline) variant?
static bool cg_is_variant_type(tk_type mt) {
    return mt.tag == TK_TYPE_VARIANT || (mt.tag == TK_TYPE_NAMED && cg_named_is_variant(mt.as.named.name));
}
// Does variant `v` (TRANSITIVELY) contain a member whose key == vkey?
static bool cg_var_reaches(tk_type v, tk_str vkey, int depth) {
    if (depth <= 0) return false;
    size_t n = cg_var_nmem(v);
    for (size_t i = 0; i < n; i += 1) {
        cbuf k = { NULL, 0, 0 }; const char *e = NULL;
        if (!cg_var_mkey(v, i, &k, &e)) { tk_free0(k.ptr); continue; }
        bool match = k.len == vkey.len && (vkey.len == 0 || memcmp(k.ptr, vkey.ptr, vkey.len) == 0);
        tk_free0(k.ptr);
        if (match) return true;
        tk_type mt = cg_var_mtype(v, i);
        if (cg_is_variant_type(mt) && cg_var_reaches(mt, vkey, depth - 1)) return true;
    }
    return false;
}
// Emit `(<variantCType>){ .tag = <TAG for member key `mkey`>, .as.<mkey> = ` (no closing `}`).
static bool cg_wrap_open(cbuf *b, tk_type v, tk_str mkey, const char **err) {
    if (v.tag == TK_TYPE_VARIANT) {
        cb(b, "("); if (!cg_variant_typename(b, v, err)) return false;
        cb(b, "){ .tag = TK_TAG_U");
        for (size_t j = 0; j < v.as.variant.len; j += 1) {
            cb(b, "_"); cbuf k = { NULL, 0, 0 }; const char *e2 = NULL;
            cg_member_key(&k, v.as.variant.members[j], &e2);
            cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr);
        }
        cb(b, "_"); cb_upper(b, mkey);
    } else {
        cb(b, "("); mangle_type_name(b, (tk_str){ NULL, 0 }, v.as.named.name);
        cb(b, "){ .tag = TK_TAG_"); cb_upper(b, v.as.named.name);
        cb(b, "_"); cb_upper(b, mkey);
    }
    cb(b, ", .as."); cb_str(b, mkey); cb(b, " = ");
    return true;
}
static bool emit_expr(cbuf *b, const tk_texpr *e, const char **err);
// Wrap `value` (a bare case) into the variant `expected`, DIRECTLY or TRANSITIVELY (a value that
// is a case of a NESTED variant member is routed through that member: Named → (Type){…} →
// (Type|error){…}). Returns false if value's key isn't reachable (caller emits plainly).
static bool emit_variant_wrap(cbuf *b, tk_type expected, const tk_texpr *value, const char **err) {
    cbuf vkey = { NULL, 0, 0 }; const char *ke = NULL;
    if (!cg_member_key(&vkey, value->type, &ke)) { tk_free0(vkey.ptr); return false; }
    size_t n = cg_var_nmem(expected);
    // pass 1 — DIRECT: value's key matches a member of `expected`.
    for (size_t i = 0; i < n; i += 1) {
        cbuf mk = { NULL, 0, 0 }; const char *e = NULL;
        if (!cg_var_mkey(expected, i, &mk, &e)) { tk_free0(mk.ptr); continue; }
        bool same = mk.len == vkey.len && (vkey.len == 0 || memcmp(mk.ptr, vkey.ptr, vkey.len) == 0);
        tk_free0(mk.ptr);
        if (!same) continue;
        bool ok = cg_wrap_open(b, expected, (tk_str){ (const tk_byte *)vkey.ptr, vkey.len }, err)
               && emit_expr(b, value, err);
        if (ok) cb(b, " }");
        tk_free0(vkey.ptr);
        return ok;
    }
    // pass 2 — TRANSITIVE: route through a member variant that reaches vkey.
    for (size_t i = 0; i < n; i += 1) {
        tk_type mt = cg_var_mtype(expected, i);
        if (!cg_is_variant_type(mt) || !cg_var_reaches(mt, (tk_str){ (const tk_byte *)vkey.ptr, vkey.len }, 16)) continue;
        cbuf mk = { NULL, 0, 0 }; const char *e = NULL;
        if (!cg_var_mkey(expected, i, &mk, &e)) { tk_free0(mk.ptr); continue; }
        bool ok = cg_wrap_open(b, expected, (tk_str){ (const tk_byte *)mk.ptr, mk.len }, err);
        tk_free0(mk.ptr);
        if (ok) ok = emit_variant_wrap(b, mt, value, err);   // recurse into the member variant
        if (ok) cb(b, " }");
        tk_free0(vkey.ptr);
        return ok;
    }
    tk_free0(vkey.ptr);
    return false;
}

// Like emit_variant_wrap but wraps a C-EXPRESSION STRING `cexpr` of resolved type `vtype` (the
// []U→[]T element-wise slice rebuild wraps each element, a C lvalue, not an AST node).
static bool emit_variant_wrap_str(cbuf *b, tk_type expected, tk_type vtype, const char *cexpr, const char **err) {
    cbuf vkey = { NULL, 0, 0 }; const char *ke = NULL;
    if (!cg_member_key(&vkey, vtype, &ke)) { tk_free0(vkey.ptr); return false; }
    size_t n = cg_var_nmem(expected);
    for (size_t i = 0; i < n; i += 1) {   // DIRECT member match
        cbuf mk = { NULL, 0, 0 }; const char *e = NULL;
        if (!cg_var_mkey(expected, i, &mk, &e)) { tk_free0(mk.ptr); continue; }
        bool same = mk.len == vkey.len && (vkey.len == 0 || memcmp(mk.ptr, vkey.ptr, vkey.len) == 0);
        tk_free0(mk.ptr);
        if (!same) continue;
        bool ok = cg_wrap_open(b, expected, (tk_str){ (const tk_byte *)vkey.ptr, vkey.len }, err);
        if (ok) { cb(b, cexpr); cb(b, " }"); }
        tk_free0(vkey.ptr); return ok;
    }
    for (size_t i = 0; i < n; i += 1) {   // TRANSITIVE
        tk_type mt = cg_var_mtype(expected, i);
        if (!cg_is_variant_type(mt) || !cg_var_reaches(mt, (tk_str){ (const tk_byte *)vkey.ptr, vkey.len }, 16)) continue;
        cbuf mk = { NULL, 0, 0 }; const char *e = NULL;
        if (!cg_var_mkey(expected, i, &mk, &e)) { tk_free0(mk.ptr); continue; }
        bool ok = cg_wrap_open(b, expected, (tk_str){ (const tk_byte *)mk.ptr, mk.len }, err);
        tk_free0(mk.ptr);
        if (ok) ok = emit_variant_wrap_str(b, mt, vtype, cexpr, err);
        if (ok) cb(b, " }");
        tk_free0(vkey.ptr); return ok;
    }
    tk_free0(vkey.ptr); return false;
}

// Lift a host-FFI primitive's fixed-ABI result (teko_rt.h) into the program's result type.
// The runtime can't name the generated sum/optional/slice structs (this header is included
// before them), so codegen builds them here: call the primitive, then construct `e->type`
// from the result's header-knowable fields (`error` is its message str). Single-evaluation —
// the result struct is bound to one temp, then both arms read it.
static bool emit_host_ffi(cbuf *b, int kind, const char *rtfn, const tk_texpr *e, const char **err) {
    char t[40]; snprintf(t, sizeof t, "_h%zu", (size_t)b->len);   // unique temp per call site
    tk_type str_t = { .tag = TK_TYPE_STR };
    tk_type err_t = { .tag = TK_TYPE_ERROR };

    // args() -> []str : ({ uint64_t _hNn; tk_str *_hNp = tk_rt_args(&_hNn); (tk_slice_str){…}; })
    if (kind == CG_FFI_ARGS) {
        cb(b, "({ uint64_t "); cb(b, t); cb(b, "n; tk_str *"); cb(b, t); cb(b, "p = ");
        cb(b, rtfn); cb(b, "(&"); cb(b, t); cb(b, "n); (");
        if (!cg_slice_typename(b, str_t, err)) return false;   // tk_slice_str
        cb(b, "){ .ptr = "); cb(b, t); cb(b, "p, .len = "); cb(b, t); cb(b, "n }; })");
        return true;
    }
    // run([]str) -> i32 : the primitive takes ptr+len (no generated slice type). Single-eval argv.
    if (kind == CG_FFI_RUN) {
        cb(b, "({ "); if (!cg_slice_typename(b, str_t, err)) return false;
        cb(b, " "); cb(b, t); cb(b, " = ");
        if (!emit_expr(b, &e->as.call.args[0], err)) return false;
        cb(b, "; "); cb(b, rtfn); cb(b, "("); cb(b, t); cb(b, ".ptr, "); cb(b, t); cb(b, ".len); })");
        return true;
    }

    // The struct-returning host FFI: bind the fixed-ABI result, then lift per result shape.
    const char *resty = (kind == CG_FFI_SRES)  ? "tk_ffi_sres"
                      : (kind == CG_FFI_URES)  ? "tk_ffi_ures"
                      : (kind == CG_FFI_SLRES) ? "tk_ffi_slres"
                      :                          "tk_ffi_u64res";
    cb(b, "({ "); cb(b, resty); cb(b, " "); cb(b, t); cb(b, " = "); cb(b, rtfn); cb(b, "(");
    for (size_t i = 0; i < e->as.call.nargs; i += 1) {
        if (i > 0) cb(b, ", ");
        if (!emit_expr(b, &e->as.call.args[i], err)) return false;
    }
    cb(b, "); ");

    if (kind == CG_FFI_URES) {
        // error? : ok → present=false (success) ; !ok → present=true, value=<message>.
        cb(b, t); cb(b, ".ok ? ("); if (!emit_type(b, e->type, err)) return false;
        cb(b, "){ .present = false } : ("); if (!emit_type(b, e->type, err)) return false;
        cb(b, "){ .present = true, .value = "); cb(b, t); cb(b, ".err }; })");
        return true;
    }

    // VARIANT results: ok → wrap the value arm ; !ok → wrap the error arm (its message str).
    cb(b, t); cb(b, ".ok ? ");
    char vexpr[64];
    if (kind == CG_FFI_SRES) {
        snprintf(vexpr, sizeof vexpr, "%s.value", t);
        if (!emit_variant_wrap_str(b, e->type, str_t, vexpr, err)) return false;
    } else if (kind == CG_FFI_U64RES) {
        tk_type u64_t = { .tag = TK_TYPE_PRIM, .as.prim = TK_PRIM_U64 };
        snprintf(vexpr, sizeof vexpr, "%s.value", t);
        if (!emit_variant_wrap_str(b, e->type, u64_t, vexpr, err)) return false;
    } else {   // CG_FFI_SLRES — []str
        tk_type selem = { .tag = TK_TYPE_STR };
        tk_type slice_t = { .tag = TK_TYPE_SLICE, .as.slice.element = &selem };
        char sx[96]; snprintf(sx, sizeof sx, "(tk_slice_str){ .ptr = %s.ptr, .len = %s.len }", t, t);
        if (!emit_variant_wrap_str(b, e->type, slice_t, sx, err)) return false;
    }
    cb(b, " : ");
    // u64res carries no message (not-found is the only failure) → an empty error str; the
    // others surface the primitive's `.err` message.
    char eexpr[64];
    if (kind == CG_FFI_U64RES) snprintf(eexpr, sizeof eexpr, "(tk_str){0}");
    else                       snprintf(eexpr, sizeof eexpr, "%s.err", t);
    if (!emit_variant_wrap_str(b, e->type, err_t, eexpr, err)) return false;
    cb(b, "; })");
    return true;
}

// Do two types mangle to the SAME generated C type name (→ same C struct/typedef)?
static bool cg_type_mangle_eq(tk_type a, tk_type c) {
    cbuf ka = { NULL, 0, 0 }, kb = { NULL, 0, 0 }; const char *e = NULL;
    bool oka = cg_opt_mangle(&ka, a, &e), okb = cg_opt_mangle(&kb, c, &e);
    bool same = oka && okb && ka.len == kb.len && (ka.len == 0 || memcmp(ka.ptr, kb.ptr, ka.len) == 0);
    tk_free0(ka.ptr); tk_free0(kb.ptr);
    return same;
}

static bool emit_as(cbuf *b, tk_type expected, const tk_texpr *value, const char **err) {
    // OPTIONAL slot (REBOOT_PLAN §202): wrap the value into the slot's tk_opt_<inner> struct.
    //   * a bare `null`           → (tk_opt_<inner>){ .present = false }
    //   * an already-optional val → emitted plainly (it IS a tk_opt_<inner>)
    //   * a plain `T`             → (tk_opt_<inner>){ .present = true, .value = <v> }  (present-wrap)
    if (expected.tag == TK_TYPE_OPTIONAL && expected.as.optional.inner != NULL) {
        tk_type inner = *expected.as.optional.inner;
        if (value->tag == TK_TEXPR_NULL) {
            cb(b, "("); if (!cg_opt_typename(b, inner, err)) return false; cb(b, "){ .present = false }");
            return true;
        }
        if (value->type.tag == TK_TYPE_OPTIONAL)   // already an optional value → pass through
            return emit_expr(b, value, err);
        cb(b, "("); if (!cg_opt_typename(b, inner, err)) return false;
        cb(b, "){ .present = true, .value = ");
        if (!emit_as(b, inner, value, err)) return false;   // the inner may itself need a wrap (e.g. a variant inner)
        cb(b, " }");
        return true;
    }
    // SLICE slot: the sentinel/untyped empty slice (teko::list::empty(), element unknown)
    // becomes this slot's concrete empty literal; a concrete slice value passes through.
    if (expected.tag == TK_TYPE_SLICE && expected.as.slice.element != NULL) {
        if (value->type.tag == TK_TYPE_SLICE && value->type.as.slice.element == NULL) {
            cb(b, "("); if (!cg_slice_typename(b, *expected.as.slice.element, err)) return false;
            cb(b, "){ .ptr = 0, .len = 0 }");
            return true;
        }
        // COVARIANT `[]U` → `[]T` (e.g. `push(empty(), Prim)` = []Prim flowing into a []Type slot):
        // tk_slice_U and tk_slice_T are DISTINCT C structs, so REBUILD element-wise, wrapping each
        // U into T (the same case→variant wrap emit_variant_wrap does, on each C element lvalue).
        if (value->type.tag == TK_TYPE_SLICE && value->type.as.slice.element != NULL
            && !cg_type_mangle_eq(*expected.as.slice.element, *value->type.as.slice.element)) {
            tk_type T = *expected.as.slice.element, U = *value->type.as.slice.element;
            char sv[40], rp[40], ri[40];
            snprintf(sv, sizeof sv, "_cv%zu", (size_t)b->len);
            snprintf(rp, sizeof rp, "_cp%zu", (size_t)b->len + 1);
            snprintf(ri, sizeof ri, "_ci%zu", (size_t)b->len + 2);
            cb(b, "({ "); if (!cg_slice_typename(b, U, err)) return false;
            cb(b, " "); cb(b, sv); cb(b, " = "); if (!emit_expr(b, value, err)) return false; cb(b, "; ");
            if (!emit_type(b, T, err)) return false; cb(b, " *"); cb(b, rp); cb(b, " = (");
            if (!emit_type(b, T, err)) return false; cb(b, " *)malloc("); cb(b, sv); cb(b, ".len * sizeof(");
            if (!emit_type(b, T, err)) return false; cb(b, ")); if ("); cb(b, rp); cb(b, " == 0 && "); cb(b, sv); cb(b, ".len) abort(); ");
            cb(b, "for (uint64_t "); cb(b, ri); cb(b, " = 0; "); cb(b, ri); cb(b, " < "); cb(b, sv); cb(b, ".len; "); cb(b, ri); cb(b, " += 1) { ");
            cb(b, rp); cb(b, "["); cb(b, ri); cb(b, "] = ");
            char elem[96]; snprintf(elem, sizeof elem, "%s.ptr[%s]", sv, ri);
            if (!emit_variant_wrap_str(b, T, U, elem, err)) { if (*err) return false; cb(b, elem); }
            cb(b, "; } ("); if (!cg_slice_typename(b, T, err)) return false;
            cb(b, "){ .ptr = "); cb(b, rp); cb(b, ", .len = "); cb(b, sv); cb(b, ".len }; })");
            return true;
        }
        return emit_expr(b, value, err);
    }
    // VARIANT slot wrap (B-cg2). Wrap a bare MEMBER value into the variant's tag+union rep:
    //   (<variantCType>){ .tag = TK_TAG_<V|U…>_<UPPER memberKey>, .as.<memberKey> = <value> }
    // The member is identified by RESOLVED-TYPE equality (the value's key == one member's key),
    // so a non-named member (error/prim/slice) wraps too, not just a named case. If the value
    // is ALREADY the whole variant (its key matches no member — e.g. a var of the variant type),
    // no wrap: it passes through.
    // VARIANT slot wrap (B-cg2) — DIRECT or TRANSITIVE (case→variant→variant). When the value's
    // key isn't reachable in `expected` (it IS the whole variant, or `expected` isn't a variant),
    // emit_variant_wrap returns false and the value is emitted plainly.
    if (expected.tag == TK_TYPE_NAMED || expected.tag == TK_TYPE_VARIANT) {
        size_t before = b->len;
        bool wrapped = emit_variant_wrap(b, expected, value, err);
        if (*err != NULL) return false;
        if (wrapped) return true;
        (void)before;
    }
    return emit_expr(b, value, err);
}

// =========================================================================
// W5b — MATCH lowering (differential parity with the VM's eval_match / pat_match).
//
// A `match` selects the FIRST arm whose pattern matches AND whose `when` guard (if any)
// holds (eval_match). The VALUE form lowers to a GNU statement-expression; the TAIL/STMT
// forms lower to a straight-line `if` chain.
//
// SELECTION STRUCTURE (all three forms): a chain of NON-`else` `if`s, one per arm:
//     if (<patTest>) { <binds>  [if (<guard>)] { <commit> } }
// Each arm's bindings live INSIDE that arm's block (so they are scoped to the arm and to
// its guard, exactly like the VM's armenv). A failed guard FALLS THROUGH to the next arm's
// `if` (the VM re-tries later arms after a guard fails) — which is why the tests are NOT
// chained with `else`. `<commit>` is what stops the search after the first winning arm:
//   * VALUE form:  `_r = <body>; break;`  inside a `do { … } while (0)` so `break` leaves
//                  the chain; the result temp `_r` is then the stmt-expr's value.
//   * TAIL form:   `return <body>;` (or `return (int)(<body>)` in main) — the return exits.
//   * STMT form:   `<body>; break;` inside `do {…} while(0)` — body run for effect, search ends.
// This yields the VM's first-match-with-guard order, keeps bindings scoped, and compiles.
//
// PATTERN TESTS against the subject temp `_s`:
//   WILDCARD -> 1 ; LITERAL -> (_s == <lit>) ; RANGE -> (_s >= lo && _s <= hi) ;
//   ALT -> (t0 || t1 || …) ; BIND/FIELD case -> (_s.tag == TK_TAG_<Variant>_<case>).
// PATTERN BINDS (emitted at the top of the arm block, before guard/body):
//   BIND `Foo as x`        -> `auto x = _s.as.<Foo>;`            (the whole case value)
//   FIELD `Type { f; g }`  -> `auto f = _s.as.<Type>.f; …`      (each named field)
// C23 `auto` (the host cc is `-std=c23`) gives each binding the exact C type without a
// separate field-type lookup — `_s.as.<Type>.f` already carries it.
// =========================================================================

// Emit an AST LITERAL pattern bound as a C constant comparable to the subject temp `_s`.
// `_s` already has the subject's C type, so a bare C literal compares correctly (the VM's
// lit_as borrows the subject's width/sign for the same reason). A str-literal pattern has
// no C value-comparison helper in the runtime (no tk_str eq), so it is an honest barrier —
// the VM DOES support str patterns (via name_eq), so this is a true codegen-only gap (M.3).
static bool cg_emit_lit_pattern(cbuf *b, const tk_expr *lit, const char **err) {
    switch (lit->tag) {
        case TK_EXPR_NUMBER:
            if (lit->as.number.is_float) { cb_f64_literal(b, lit->as.number.fval); return true; }
            cb_i128(b, lit->as.number.value);
            return true;
        case TK_EXPR_BYTE:
            cb_i64(b, (int64_t)lit->as.byte.value);
            return true;
        case TK_EXPR_STR:
            return fail_node(err, "codegen: string-literal patterns not yet supported (no tk_str equality helper in the runtime)");
        default:
            return fail_node(err, "codegen: unsupported literal in a pattern (parser emits only number/byte/str)");
    }
}

// B-cg2 — emit a variant tag-constant PREFIX (everything up to and including the second `_`,
// i.e. `TK_TAG_<NAME>_` for a named variant or `TK_TAG_U_<UPPER keys…>_` for an inline union),
// so a case suffix can be appended. `variantT` is the subject's variant type (NAMED decl or
// inline TK_TYPE_VARIANT). Mirrors emit_type_decl / cg_emit_inline_variant_typedef spellings.
static bool cg_emit_tag_prefix(cbuf *b, tk_type variantT, const char **err) {
    cb(b, "TK_TAG_");
    if (variantT.tag == TK_TYPE_VARIANT) {
        cb(b, "U");
        for (size_t j = 0; j < variantT.as.variant.len; j += 1) {
            cb(b, "_"); cbuf k = { NULL, 0, 0 }; const char *e2 = NULL;
            if (!cg_member_key(&k, variantT.as.variant.members[j], &e2)) { tk_free0(k.ptr); return false; }
            cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr);
        }
        cb(b, "_");
        return true;
    }
    cb_upper(b, variantT.as.named.name);   // NAMED variant decl
    cb(b, "_");
    return true;
}

// B-cg2 — emit a BIND/FIELD pattern's CASE KEY (the union field name + the UPPERCASE tag
// suffix source). A non-slice case uses its path-last identifier (== the member key for
// named/error/prim members); a slice case (`[]T as x`) uses `slice_<elem>`.
static bool cg_emit_case_key(cbuf *b, const tk_pattern *pat, const char **err) {
    if (pat->tag == TK_PAT_BIND && pat->as.bind.is_slice) {
        if (pat->as.bind.slice_type == NULL)
            return fail_node(err, "codegen: slice pattern without a slice type (internal)");
        return cg_member_key_texpr(b, *pat->as.bind.slice_type, err);
    }
    tk_path tn = (pat->tag == TK_PAT_FIELD) ? pat->as.field.type_name : pat->as.bind.type_name;
    // keyword-escape (cb_ident) so a `bool` case's `.as.bool_` field / `TK_TAG_..._BOOL_` tag
    // match the definitions (cg_member_key escapes the field+tag; the pattern side must agree).
    cb_ident(b, cg_path_last(tn));
    return true;
}

// Emit the boolean TEST that `_s` (`subj`) matches `pat`. `variantT` is the subject's variant
// type (a NAMED variant decl or an inline TK_TYPE_VARIANT), needed to spell a case's tag
// constant; it is a scalar/void type for a non-variant subject. Mirrors pat_match's match
// decisions (no binding here — binds are separate).
static bool emit_pat_test(cbuf *b, const tk_pattern *pat, const char *subj,
                          tk_type variantT, const char **err) {
    switch (pat->tag) {
        case TK_PAT_WILDCARD:
            cb(b, "1");
            return true;
        case TK_PAT_LITERAL:
            cb(b, "("); cb(b, subj); cb(b, " == ");
            if (!cg_emit_lit_pattern(b, &pat->as.literal.value, err)) return false;
            cb(b, ")");
            return true;
        case TK_PAT_RANGE:
            cb(b, "("); cb(b, subj); cb(b, " >= ");
            if (!cg_emit_lit_pattern(b, &pat->as.range.lo, err)) return false;
            cb(b, " && "); cb(b, subj); cb(b, " <= ");
            if (!cg_emit_lit_pattern(b, &pat->as.range.hi, err)) return false;
            cb(b, ")");
            return true;
        case TK_PAT_ALT:
            cb(b, "(");
            for (size_t i = 0; i < pat->as.alt.n_options; i += 1) {
                if (i > 0) cb(b, " || ");
                if (!emit_pat_test(b, &pat->as.alt.options[i], subj, variantT, err)) return false;
            }
            cb(b, ")");
            return true;
        case TK_PAT_BIND:
            // Binding the WHOLE variant value (`TypeExpr as t` where the subject IS the TypeExpr
            // variant — common over an optional `TypeExpr?`): NOT a case-select. The value already
            // IS that variant, so the test is unconditionally true (any gating .present check is
            // upstream); the bind copies the whole value (emit_pat_binds).
            if (variantT.tag == TK_TYPE_NAMED && !pat->as.bind.is_slice
                && cg_name_eq(cg_path_last(pat->as.bind.type_name), variantT.as.named.name)) {
                cb(b, "1");
                return true;
            }
            // An ENUM subject has no tag+union: a member pattern tests value equality against the
            // enum constant `TK_E_<ENUM>_<MEMBER>` (matches emit_type_decl's enum emission).
            if (variantT.tag == TK_TYPE_NAMED && cg_named_is_enum(variantT.as.named.name)) {
                cb(b, "("); cb(b, subj); cb(b, " == TK_E_");
                cb_upper(b, variantT.as.named.name); cb(b, "_");
                { cbuf k = { NULL, 0, 0 };
                  if (!cg_emit_case_key(&k, pat, err)) { tk_free0(k.ptr); return false; }
                  cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr); }
                cb(b, ")");
                return true;
            }
            cb(b, "("); cb(b, subj); cb(b, ".tag == ");
            if (!cg_emit_tag_prefix(b, variantT, err)) return false;
            { cbuf k = { NULL, 0, 0 };
              if (!cg_emit_case_key(&k, pat, err)) { tk_free0(k.ptr); return false; }
              cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr); }
            cb(b, ")");
            return true;
        case TK_PAT_FIELD:
            cb(b, "("); cb(b, subj); cb(b, ".tag == ");
            if (!cg_emit_tag_prefix(b, variantT, err)) return false;
            { cbuf k = { NULL, 0, 0 };
              if (!cg_emit_case_key(&k, pat, err)) { tk_free0(k.ptr); return false; }
              cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr); }
            cb(b, ")");
            return true;
        case TK_PAT_NULL:
            // A `null` pattern reaches here only over a NON-optional subject (the optional case
            // is handled by cg_emit_pat_test's `.present` test) — the checker rejects that, so
            // be honest rather than mis-emit.
            return fail_node(err, "codegen: `null` pattern over a non-optional subject (internal)");
    }
    return fail_node(err, "codegen: unknown pattern not yet supported");
}

// fwd — emit_pat_binds is defined below; the top-level optional-aware wrapper needs it.
static bool emit_pat_binds(cbuf *b, const tk_pattern *pat, const char *subj,
                           const char *indent, const char **err);

// TOP-LEVEL pattern test (handles an OPTIONAL subject — REBOOT_PLAN §202). `subjT` is the
// subject's resolved type; `subj` is the subject temp name (a tk_opt_<inner> struct when
// optional). For an optional subject: `null` ⇒ `(!_s.present)`; any other pattern ⇒
// `(_s.present && <test over _s.value>)`. The inner test reuses emit_pat_test with the
// value-accessor `_s.value` as the subject and the inner's variant name. For a non-optional
// subject it delegates straight to emit_pat_test.
// AST struct-vs-kind DESCEND. The .tks parser/typed AST models `Expr`/`TExpr`/… as
// `struct { kind: <variant>; … }`, but the corpus matches such a value DIRECTLY against the
// variant's cases (`match e { Number => … }`). If `subjT` is a NAMED struct with a `kind` field
// of a VARIANT type and `pat` selects a CASE of that variant, return true + the variant type and
// whether the kind field is boxed, so the match lowers on `<subj>.kind` (deref if boxed). (The C
// twin's tk_expr is a tagged union, so the C codegen never descends — a model divergence.)
static bool cg_kind_descend(tk_type subjT, const tk_pattern *pat, tk_type *outV, bool *outBoxed, const char **err) {
    if (subjT.tag != TK_TYPE_NAMED) return false;
    if (pat->tag != TK_PAT_BIND && pat->tag != TK_PAT_FIELD) return false;
    const tk_type_decl *d = cg_find_decl(subjT.as.named.name);
    if (d == NULL || d->body.tag != TK_BODY_STRUCT) return false;
    tk_type_expr kfte; bool found = false;
    tk_struct_body sb = d->body.as.struct_body;
    for (size_t i = 0; i < sb.n_fields; i += 1)
        if (seg_is(sb.fields[i].name, "kind")) { kfte = sb.fields[i].type_ann; found = true; break; }
    if (!found || kfte.tag != TK_TEXPR_NAMED) return false;
    tk_str vname = kfte.as.named.path.segments[kfte.as.named.path.len - 1].name;
    if (!cg_named_is_variant(vname)) return false;
    tk_type V = { .tag = TK_TYPE_NAMED, .as.named.name = vname };
    // the pattern must select a CASE of V (not a field-destructure of the struct itself).
    cbuf ck = { NULL, 0, 0 }; const char *e = NULL;
    if (!cg_emit_case_key(&ck, pat, &e)) { tk_free0(ck.ptr); return false; }
    bool ismem = cg_var_reaches(V, (tk_str){ (const tk_byte *)ck.ptr, ck.len }, 2);
    tk_free0(ck.ptr);
    if (!ismem) return false;
    *outV = V;
    *outBoxed = cg_field_boxed(subjT.as.named.name, kfte);
    (void)err;
    return true;
}

static bool cg_emit_pat_test(cbuf *b, const tk_pattern *pat, const char *subj,
                             tk_type subjT, const char **err) {
    // AST descend: `match e { Number => }` over `e: Expr = struct{kind: ExprKind}` lowers on e.kind.
    tk_type kv; bool kboxed;
    if (cg_kind_descend(subjT, pat, &kv, &kboxed, err)) {
        char ds[96];
        snprintf(ds, sizeof ds, "%s(%s).kind%s", kboxed ? "(*" : "", subj, kboxed ? ")" : "");
        return cg_emit_pat_test(b, pat, ds, kv, err);
    }
    if (subjT.tag == TK_TYPE_OPTIONAL && subjT.as.optional.inner != NULL) {
        if (pat->tag == TK_PAT_NULL) { cb(b, "(!"); cb(b, subj); cb(b, ".present)"); return true; }
        tk_type inner = *subjT.as.optional.inner;
        bool inner_is_variant = inner.tag == TK_TYPE_VARIANT
                             || (inner.tag == TK_TYPE_NAMED && cg_named_is_variant(inner.as.named.name));
        // A bare bind/wildcard over a NON-variant present value matches whenever present (no inner
        // tag to test) — `error as e` / `i64 as v` / `_`. Only a variant inner (or a literal/range
        // /field test) adds an inner test against `_s.value`.
        if ((pat->tag == TK_PAT_BIND || pat->tag == TK_PAT_WILDCARD) && !inner_is_variant) {
            cb(b, "("); cb(b, subj); cb(b, ".present)");
            return true;
        }
        char val[56]; snprintf(val, sizeof val, "%s.value", subj);
        cb(b, "("); cb(b, subj); cb(b, ".present && ");
        if (!emit_pat_test(b, pat, val, inner, err)) return false;
        cb(b, ")");
        return true;
    }
    return emit_pat_test(b, pat, subj, subjT, err);
}

// TOP-LEVEL pattern binds (handles an OPTIONAL subject). For an optional present pattern the
// inner value is `_s.value`; for a non-optional subject it delegates straight to emit_pat_binds.
static bool cg_emit_pat_binds(cbuf *b, const tk_pattern *pat, const char *subj,
                              tk_type subjT, const char *indent, const char **err) {
    // AST descend (mirror cg_emit_pat_test): a case pattern over `Expr`/`TExpr` binds off `.kind`.
    tk_type kv; bool kboxed;
    if (cg_kind_descend(subjT, pat, &kv, &kboxed, err)) {
        char ds[96];
        snprintf(ds, sizeof ds, "%s(%s).kind%s", kboxed ? "(*" : "", subj, kboxed ? ")" : "");
        return cg_emit_pat_binds(b, pat, ds, kv, indent, err);
    }
    if (subjT.tag == TK_TYPE_OPTIONAL && subjT.as.optional.inner != NULL) {
        if (pat->tag == TK_PAT_NULL) return true;   // NONE binds nothing
        tk_type inner = *subjT.as.optional.inner;
        bool inner_is_variant = inner.tag == TK_TYPE_VARIANT
                             || (inner.tag == TK_TYPE_NAMED && cg_named_is_variant(inner.as.named.name));
        char val[56]; snprintf(val, sizeof val, "%s.value", subj);
        // A bare `T as x` over an optional binds the INNER value directly (the present value IS
        // the inner — not a variant `.as.<case>`), for any non-variant inner (prim/str/error/
        // struct).
        if (pat->tag == TK_PAT_BIND && !inner_is_variant) {
            if (pat->as.bind.has_binding) {
                cb(b, indent); cb(b, "auto "); cb_str(b, pat->as.bind.binding);
                cb(b, " = "); cb(b, val); cb(b, ";\n");
            }
            return true;
        }
        // A FIELD destructure `Struct { f; g }` over a present STRUCT inner reads fields off the
        // inner value directly (`_s.value.f`), not via a variant union.
        if (pat->tag == TK_PAT_FIELD && !inner_is_variant) {
            for (size_t i = 0; i < pat->as.field.n_fields; i += 1) {
                cb(b, indent); cb(b, "auto "); cb_str(b, pat->as.field.fields[i]);
                cb(b, " = "); cb(b, val); cb(b, "."); cb_str(b, pat->as.field.fields[i]); cb(b, ";\n");
            }
            return true;
        }
        // A bind whose type IS the inner variant itself (`TypeExpr as t` over `TypeExpr?`) binds
        // the WHOLE present value, not a `.as.<case>` member.
        if (pat->tag == TK_PAT_BIND && !pat->as.bind.is_slice && inner.tag == TK_TYPE_NAMED
            && cg_name_eq(cg_path_last(pat->as.bind.type_name), inner.as.named.name)) {
            if (pat->as.bind.has_binding) {
                cb(b, indent); cb(b, "auto "); cb_str(b, pat->as.bind.binding);
                cb(b, " = "); cb(b, val); cb(b, ";\n");
            }
            return true;
        }
        // A variant inner: bind via the variant union over `_s.value`.
        return emit_pat_binds(b, pat, val, indent, err);
    }
    // Non-optional bind-WHOLE-variant (`TypeExpr as t` where the subject IS the TypeExpr variant)
    // binds the whole value, not a `.as.<case>`.
    if (pat->tag == TK_PAT_BIND && !pat->as.bind.is_slice && subjT.tag == TK_TYPE_NAMED
        && cg_name_eq(cg_path_last(pat->as.bind.type_name), subjT.as.named.name)) {
        if (pat->as.bind.has_binding) {
            cb(b, indent); cb(b, "auto "); cb_str(b, pat->as.bind.binding);
            cb(b, " = "); cb(b, subj); cb(b, ";\n");
        }
        return true;
    }
    return emit_pat_binds(b, pat, subj, indent, err);
}

// Emit the BINDINGS a pattern introduces (BIND `as x`, FIELD fields), at the top of the
// arm block (so they are in scope for the guard and the body). Mirrors pat_match's
// env_define calls. Scalar/Alt/Wildcard patterns bind nothing.
static bool emit_pat_binds(cbuf *b, const tk_pattern *pat, const char *subj,
                           const char *indent, const char **err) {
    switch (pat->tag) {
        case TK_PAT_BIND:
            if (pat->as.bind.has_binding) {
                // `Foo as x` binds the WHOLE case value (the union member, named by its KEY:
                // bare name for named/error/prim, `slice_<elem>` for `[]T as x`).
                cb(b, indent); cb(b, "auto ");
                cb_str(b, pat->as.bind.binding);
                cb(b, " = "); cb(b, subj); cb(b, ".as.");
                if (!cg_emit_case_key(b, pat, err)) return false;
                cb(b, ";\n");
            }
            return true;
        case TK_PAT_FIELD:
            for (size_t i = 0; i < pat->as.field.n_fields; i += 1) {
                cb(b, indent); cb(b, "auto ");
                cb_str(b, pat->as.field.fields[i]);
                cb(b, " = "); cb(b, subj); cb(b, ".as.");
                if (!cg_emit_case_key(b, pat, err)) return false;
                cb(b, ".");
                cb_str(b, pat->as.field.fields[i]);
                cb(b, ";\n");
            }
            return true;
        default:
            return true;   // WILDCARD/LITERAL/RANGE/ALT bind nothing
    }
}


// Does a typed block DIVERGE (exit via return/break/continue on every path)? Mirrors the
// checker's tk_tblock_diverges so codegen lowers a diverging arm body as real control flow
// (a `return e;`) instead of a value commit. Kept local (codegen does not include the
// checker-internal header).
static bool cg_block_diverges(const tk_tstatement *stmts, size_t n) {
    if (n == 0) return false;
    const tk_tstatement *last = &stmts[n - 1];
    switch (last->tag) {
        case TK_TSTMT_RETURN:
        case TK_TSTMT_BREAK:
        case TK_TSTMT_CONTINUE:
            return true;
        case TK_TSTMT_EXPR: {
            const tk_texpr *x = &last->as.expr_stmt.expr;
            if (x->tag == TK_TEXPR_IF) {
                if (!x->as.if_expr.has_else) return false;
                return cg_block_diverges(x->as.if_expr.then_blk, x->as.if_expr.nthen)
                    && cg_block_diverges(x->as.if_expr.else_blk, x->as.if_expr.nelse);
            }
            if (x->tag == TK_TEXPR_MATCH) {
                if (x->as.match_expr.narms == 0) return false;
                for (size_t i = 0; i < x->as.match_expr.narms; i += 1)
                    if (!cg_block_diverges(x->as.match_expr.arms[i].body, x->as.match_expr.arms[i].nbody))
                        return false;
                return true;
            }
            return false;
        }
        default: return false;
    }
}

// A block whose trailing statement is a `break`/`continue` (or a trailing if/match that
// diverges with one) — illegal inside a match used as a SUB-EXPRESSION (GNU stmt-expr), the
// same frontier the VM enforces (eval_match honest-stops on such an arm). A `return` divergence
// IS fine (it leaves the whole function). Distinguishes the two divergence kinds.
static bool cg_block_diverges_jump(const tk_tstatement *stmts, size_t n) {
    if (n == 0) return false;
    const tk_tstatement *last = &stmts[n - 1];
    if (last->tag == TK_TSTMT_BREAK || last->tag == TK_TSTMT_CONTINUE) return true;
    if (last->tag == TK_TSTMT_EXPR) {
        const tk_texpr *x = &last->as.expr_stmt.expr;
        if (x->tag == TK_TEXPR_IF && x->as.if_expr.has_else)
            return cg_block_diverges_jump(x->as.if_expr.then_blk, x->as.if_expr.nthen)
                || cg_block_diverges_jump(x->as.if_expr.else_blk, x->as.if_expr.nelse);
        if (x->tag == TK_TEXPR_MATCH)
            for (size_t i = 0; i < x->as.match_expr.narms; i += 1)
                if (cg_block_diverges_jump(x->as.match_expr.arms[i].body, x->as.match_expr.arms[i].nbody)) return true;
    }
    return false;
}

// Emit an arm body BLOCK in VALUE position (the match value form). All-but-last statements
// emit normally; the trailing value flows into `sink`, WRAPPED to the match result type via
// emit_as (so a case body value becomes a variant), then `break` commits. A `return`-DIVERGING
// arm (`=> return e` — THE dominant error idiom) emits real control flow inside the GNU stmt-
// expr (a `return …;` leaves the whole function) and never commits to `sink`. A break/continue
// divergence is an HONEST STOP — it can't escape a stmt-expr, the same frontier the VM enforces
// for a match used as a sub-expression (eval_match).
static bool emit_arm_value(cbuf *b, const tk_texpr *match_e, const tk_tstatement *body, size_t n,
                           const char *sink, const char *commit_indent, const char **err) {
    if (cg_block_diverges_jump(body, n))
        return fail_node(err, "codegen: break/continue inside a `match` used as a sub-expression not yet supported");
    if (cg_block_diverges(body, n)) {
        // `return`-divergence: emit every statement as-is; the trailing `return e;` (or a
        // diverging trailing if/match) leaves the ENCLOSING FUNCTION — so its value wraps into the
        // FUNCTION's return type (g_cg_ret_type), NOT the match result type. No `sink =` / `break`.
        for (size_t i = 0; i + 1 < n; i += 1)
            if (!emit_stmt(b, &body[i], /*in_main=*/false, g_cg_ret_type, commit_indent, err)) return false;
        const tk_tstatement *last = &body[n - 1];
        if (last->tag == TK_TSTMT_EXPR &&
            (last->as.expr_stmt.expr.tag == TK_TEXPR_IF || last->as.expr_stmt.expr.tag == TK_TEXPR_MATCH))
            return emit_exprstmt_tail(b, &last->as.expr_stmt.expr, /*in_main=*/false, g_cg_ret_type, commit_indent, err);
        return emit_stmt(b, last, /*in_main=*/false, g_cg_ret_type, commit_indent, err);
    }
    // Value form: all-but-last normally; the trailing value → `sink` (wrapped), then `break`.
    // ret_type is the FUNCTION's return type (not VOID): a non-last statement may itself be a
    // `return e` (e.g. `_ => { if cond { return error{…} }; v }`), and that `return` must wrap its
    // value into the fn's return slot (g_cg_ret_type) — a VOID slot left an error returned bare.
    for (size_t i = 0; i + 1 < n; i += 1)
        if (!emit_stmt(b, &body[i], /*in_main=*/false, g_cg_ret_type, commit_indent, err)) return false;
    if (n == 0) return fail_node(err, "codegen: empty match arm body in value position");
    const tk_tstatement *last = &body[n - 1];
    if (last->tag != TK_TSTMT_EXPR)
        return fail_node(err, "codegen: non-value trailing statement in a match arm used as a value");
    cb(b, commit_indent); cb(b, sink); cb(b, " = (");
    // A trailing diverging expr (`=> panic(…)` / `exit(…)` — _Noreturn void, not a `return`) can't
    // be assigned to the sink. Emit it as `(panic(…), (T){0})`: the call diverges, the zero-of-T
    // makes the assignment type-check (unreachable). Mirrors the `??` diverging-fallback lowering.
    if (cg_expr_diverges(&last->as.expr_stmt.expr)) {
        if (!emit_expr(b, &last->as.expr_stmt.expr, err)) return false;
        cb(b, ", ("); if (!emit_type(b, match_e->type, err)) return false; cb(b, "){0}");   // sink's `( … )` closes it
    } else if (!emit_as(b, match_e->type, &last->as.expr_stmt.expr, err)) return false;
    cb(b, "); break;\n");
    return true;
}

// VALUE form — a GNU statement-expression yielding the matched arm's body value:
//   ({ <SubjCType> _sN = (<subject>); <ResultCType> _rN; do {
//        if (<test0>) { <binds0> [if (<guard0>)] { <arm body → _rN; break;> } }
//        … } while (0); _rN; })
static bool emit_match_value(cbuf *b, const tk_texpr *e, const char **err) {
    // Freeze unique temp names (buffer length is the functional uniquifier — see emit_if_value).
    char subj[40], res[40];
    snprintf(subj, sizeof subj, "_s%zu", (size_t)b->len);
    snprintf(res,  sizeof res,  "_r%zu", (size_t)b->len + 1);   // +1 so it differs from subj
    tk_type subjT = e->as.match_expr.subject->type;   // for optional / variant pattern lowering
    cb(b, "({ ");
    if (!emit_type(b, e->as.match_expr.subject->type, err)) return false;
    cb(b, " "); cb(b, subj); cb(b, " = (");
    if (!emit_expr(b, e->as.match_expr.subject, err)) return false;
    cb(b, "); ");
    if (!emit_type(b, e->type, err)) return false;
    cb(b, " "); cb(b, res); cb(b, "; do {\n");
    for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
        const tk_tarm *arm = &e->as.match_expr.arms[i];
        cb(b, "    if (");
        if (!cg_emit_pat_test(b, &arm->pattern, subj, subjT, err)) return false;
        cb(b, ") {\n");
        if (!cg_emit_pat_binds(b, &arm->pattern, subj, subjT, "        ", err)) return false;
        const char *commit_indent = "        ";
        if (arm->has_when) {
            cb(b, "        if (");
            if (!emit_expr(b, arm->guard, err)) return false;
            cb(b, ") {\n");
            commit_indent = "            ";
        }
        // The arm body is a BLOCK (B.20): its trailing value flows into _r (wrapped to the
        // match result type), or a diverging arm (`=> return e`) emits real control flow.
        if (!emit_arm_value(b, e, arm->body, arm->nbody, res, commit_indent, err)) return false;
        if (arm->has_when) cb(b, "        }\n");
        cb(b, "    }\n");
    }
    // Exhaustiveness is guaranteed by the checker; the chain always commits before falling
    // through. (No default needed — a fall-through cannot happen for a well-typed program.)
    cb(b, "    } while (0); "); cb(b, res); cb(b, "; })");
    return true;
}

// TAIL form — each winning arm `return`s its body. Lowered as a straight `if` chain inside
// a brace block holding the subject temp. The first matching arm with a holding guard
// returns and exits; a failed guard falls through to the next arm.
static bool emit_match_tail(cbuf *b, const tk_texpr *e, bool in_main,
                            tk_type ret_type, const char *indent, const char **err) {
    char subj[40];
    snprintf(subj, sizeof subj, "_s%zu", (size_t)b->len);
    tk_type subjT = e->as.match_expr.subject->type;   // for optional / variant pattern lowering
    char inner[72]; snprintf(inner, sizeof inner, "%s    ", indent);
    char inner2[80]; snprintf(inner2, sizeof inner2, "%s    ", inner);
    cb(b, indent); cb(b, "{\n");
    cb(b, inner);
    if (!emit_type(b, e->as.match_expr.subject->type, err)) return false;
    cb(b, " "); cb(b, subj); cb(b, " = (");
    if (!emit_expr(b, e->as.match_expr.subject, err)) return false;
    cb(b, ");\n");
    for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
        const tk_tarm *arm = &e->as.match_expr.arms[i];
        cb(b, inner); cb(b, "if (");
        if (!cg_emit_pat_test(b, &arm->pattern, subj, subjT, err)) return false;
        cb(b, ") {\n");
        if (!cg_emit_pat_binds(b, &arm->pattern, subj, subjT, inner2, err)) return false;
        const char *ci = inner2;
        char inner3[88];
        if (arm->has_when) {
            cb(b, inner2); cb(b, "if (");
            if (!emit_expr(b, arm->guard, err)) return false;
            cb(b, ") {\n");
            snprintf(inner3, sizeof inner3, "%s    ", inner2);
            ci = inner3;
        }
        // The arm body is a BLOCK in TAIL position (B.20): emit it as a tail block — its
        // trailing value becomes a `return`, and an explicit return/break/continue inside it
        // emits real C control flow (mirrors the VM running the arm block flow-aware).
        if (!emit_block_tail(b, arm->body, arm->nbody, in_main, ret_type, ci, err)) return false;
        if (arm->has_when) { cb(b, inner2); cb(b, "}\n"); }
        cb(b, inner); cb(b, "}\n");
    }
    cb(b, indent); cb(b, "}\n");
    return true;
}

// STATEMENT form — a match whose result is DISCARDED (run each winning arm body for
// effect). An `if` chain inside a brace block holding the subject temp; the FIRST winning arm
// runs its block and then `goto`s a unique commit label past the chain (so the search stops).
// The commit uses a GOTO (not a `do {…} while(0)` + break) precisely because an arm body is a
// BLOCK that may contain `break`/`continue` targeting an ENCLOSING loop — those must be real C
// break/continue, never swallowed by a wrapper (mirrors the VM propagating the arm's flow).
static bool emit_match_stmt(cbuf *b, const tk_texpr *e, bool in_main, tk_type ret_type, const char *indent, const char **err) {
    char subj[40], done[48];
    snprintf(subj, sizeof subj, "_s%zu", (size_t)b->len);
    snprintf(done, sizeof done, "tk_m%zu_done", (size_t)b->len + 1);   // unique commit label
    tk_type subjT = e->as.match_expr.subject->type;   // for optional / variant pattern lowering
    char inner[72]; snprintf(inner, sizeof inner, "%s    ", indent);
    char inner2[80]; snprintf(inner2, sizeof inner2, "%s    ", inner);
    cb(b, indent); cb(b, "{\n");
    cb(b, inner);
    if (!emit_type(b, e->as.match_expr.subject->type, err)) return false;
    cb(b, " "); cb(b, subj); cb(b, " = (");
    if (!emit_expr(b, e->as.match_expr.subject, err)) return false;
    cb(b, ");\n");
    for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
        const tk_tarm *arm = &e->as.match_expr.arms[i];
        cb(b, inner); cb(b, "if (");
        if (!cg_emit_pat_test(b, &arm->pattern, subj, subjT, err)) return false;
        cb(b, ") {\n");
        if (!cg_emit_pat_binds(b, &arm->pattern, subj, subjT, inner2, err)) return false;
        const char *ci = inner2;
        char inner3[88];
        if (arm->has_when) {
            cb(b, inner2); cb(b, "if (");
            if (!emit_expr(b, arm->guard, err)) return false;
            cb(b, ") {\n");
            snprintf(inner3, sizeof inner3, "%s    ", inner2);
            ci = inner3;
        }
        // Run the arm body BLOCK for effect (value discarded); any break/continue/return
        // inside it propagates via C's own semantics. Thread ret_type so `return null`
        // inside an arm wraps null correctly into the enclosing function's return slot.
        if (!emit_block(b, arm->body, arm->nbody, in_main, ret_type, ci, err)) return false;
        cb(b, ci); cb(b, "goto "); cb(b, done); cb(b, ";\n");   // first match wins → stop the search
        if (arm->has_when) { cb(b, inner2); cb(b, "}\n"); }
        cb(b, inner); cb(b, "}\n");
    }
    cb(b, inner); cb(b, done); cb(b, ": ;\n");
    cb(b, indent); cb(b, "}\n");
    return true;
}

// =========================================================================
// Statements -> C. `in_main` selects the return lowering:
//   * in main : `return n` -> `return (int)(n);`  (early process exit)
//   * in a fn : `return n` -> `return n;`
// (emit_stmt / emit_expr are forward-declared with the W5a value/tail machinery above.)
// =========================================================================
static bool emit_block(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                       tk_type ret_type, const char *indent, const char **err) {
    for (size_t i = 0; i < n; i += 1) {
        if (!emit_stmt(b, &body[i], in_main, ret_type, indent, err)) return false;
    }
    return true;
}

static bool emit_block_tail(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                            tk_type ret_type, const char *indent, const char **err);

// W5a — emit a TRAILING expr-statement (the block's last statement) as an implicit RETURN of
// its value (mirrors the VM: a block's trailing NORMAL value is the function's/main's value —
// B.20). A trailing `if` is lowered as DIRECT control flow (each branch recursively tail-
// emitted), so `return`/`break`/`continue` inside a branch propagate via C's own semantics —
// exactly like the VM's exec_if. This direct form works whether the trailing `if` is typed as
// a value (a function-body tail) or as `void` (a virtual-main tail, which the checker types as
// a statement but the VM still drains for its branch value). A void expr-statement, or any
// non-expr trailing statement, emits normally (it carries no value).
static bool emit_exprstmt_tail(cbuf *b, const tk_texpr *x, bool in_main,
                               tk_type ret_type, const char *indent, const char **err) {
    if (x->tag == TK_TEXPR_IF) {
        // An if-without-else in tail position has no value: emit it as a control-flow statement.
        // The enclosing block's implicit `return 0;` / `return;` handles fall-through.
        if (!x->as.if_expr.has_else)
            return emit_if_stmt(b, x, in_main, ret_type, indent, err);
        cb(b, indent); cb(b, "if (");
        if (!emit_expr(b, x->as.if_expr.cond, err)) return false;
        cb(b, ") {\n");
        char inner[64]; snprintf(inner, sizeof inner, "%s    ", indent);
        if (!emit_block_tail(b, x->as.if_expr.then_blk, x->as.if_expr.nthen, in_main, ret_type, inner, err))
            return false;
        cb(b, indent); cb(b, "} else {\n");
        if (!emit_block_tail(b, x->as.if_expr.else_blk, x->as.if_expr.nelse, in_main, ret_type, inner, err))
            return false;
        cb(b, indent); cb(b, "}\n");
        return true;
    }
    // W5b — a trailing `match` lowers as a control-flow `if` chain whose arms `return`
    // (mirrors the trailing `if`); each arm body is wrapped to the return slot via emit_as.
    if (x->tag == TK_TEXPR_MATCH)
        return emit_match_tail(b, x, in_main, ret_type, indent, err);
    if (x->type.tag == TK_TYPE_VOID) {   // a void expr-statement carries no value → run for effect
        cb(b, indent);
        if (!emit_expr(b, x, err)) return false;
        cb(b, ";\n");
        return true;
    }
    cb(b, indent);
    if (in_main) {
        cb(b, "return (int)(");
        if (!emit_expr(b, x, err)) return false;
        cb(b, ");\n");
    } else {
        cb(b, "return ");
        if (!emit_as(b, ret_type, x, err)) return false;   // W5b — wrap a case value into a variant return slot
        cb(b, ";\n");
    }
    return true;
}

// W5a — IMPLICIT trailing-return. Emit a FUNCTION/MAIN/branch body where the LAST statement,
// if it is an expr-statement carrying a value, `return`s that value (via emit_exprstmt_tail).
// All but the last statement emit normally. An explicit RETURN already yields the value; this
// adds the implicit tail-return that mirrors the VM.
static bool emit_block_tail(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                            tk_type ret_type, const char *indent, const char **err) {
    for (size_t i = 0; i + 1 < n; i += 1) {
        if (!emit_stmt(b, &body[i], in_main, ret_type, indent, err)) return false;
    }
    if (n == 0) return true;
    const tk_tstatement *last = &body[n - 1];
    if (last->tag == TK_TSTMT_EXPR)
        return emit_exprstmt_tail(b, &last->as.expr_stmt.expr, in_main, ret_type, indent, err);
    return emit_stmt(b, last, in_main, ret_type, indent, err);
}

// W5a — emit an `if` in STATEMENT position as plain C control flow:
//   if (<cond>) { <then…> } [else { <else…> }]
// Each branch is emitted via emit_block (NON-tail, value discarded), so `break`/`continue`/
// `return` inside a branch propagate via C's own semantics. This mirrors the VM, which routes
// a statement-position `if` (an expr-statement whose expr is an `if`) to exec_if as CONTROL
// FLOW — never to the value path. (The TAIL `if` takes emit_exprstmt_tail; a SUB-expression
// `if` takes emit_if_value; this is the INTERIOR statement case.)
static bool emit_if_stmt(cbuf *b, const tk_texpr *e, bool in_main,
                         tk_type ret_type, const char *indent, const char **err) {
    cb(b, indent); cb(b, "if (");
    if (!emit_expr(b, e->as.if_expr.cond, err)) return false;
    cb(b, ") {\n");
    char inner[64]; snprintf(inner, sizeof inner, "%s    ", indent);
    if (!emit_block(b, e->as.if_expr.then_blk, e->as.if_expr.nthen, in_main, ret_type, inner, err))
        return false;
    if (e->as.if_expr.has_else) {
        cb(b, indent); cb(b, "} else {\n");
        if (!emit_block(b, e->as.if_expr.else_blk, e->as.if_expr.nelse, in_main, ret_type, inner, err))
            return false;
    }
    cb(b, indent); cb(b, "}\n");
    return true;
}

static bool emit_stmt(cbuf *b, const tk_tstatement *s, bool in_main,
                      tk_type ret_type, const char *indent, const char **err) {
    switch (s->tag) {
        case TK_TSTMT_BINDING: {
            // BindTarget: only SimpleName is lowered for M0.
            tk_bind_target tgt = s->as.binding.target;
            if (tgt.tag != TK_BIND_SIMPLE)
                return fail_node(err, "codegen: destructuring binding not yet supported");
            // `let _ = expr` — DISCARD: evaluate for effect, no C variable (so repeated `let _`
            // never collide). A `return`/divergence inside the value still propagates via C.
            if (tgt.as.simple.name.len == 1 && tgt.as.simple.name.ptr[0] == '_') {
                cb(b, indent); cb(b, "(void)(");
                if (!emit_as(b, s->as.binding.bound, &s->as.binding.value, err)) return false;
                cb(b, ");\n");
                return true;
            }
            cb(b, indent);
            if (s->as.binding.kind == TK_BIND_CONST) cb(b, "const ");
            if (!emit_type(b, s->as.binding.bound, err)) return false;
            cb(b, " ");
            cb_ident(b, tgt.as.simple.name);
            cb(b, " = ");
            // W5b — if the binding's declared type is a variant and the value is a case
            // member, WRAP it into the variant repr (e.g. `let s: Shape = Circle { … }`).
            if (!emit_as(b, s->as.binding.bound, &s->as.binding.value, err)) return false;
            cb(b, ";\n");
            return true;
        }

        case TK_TSTMT_ASSIGN: {
            const char *op = assignop_c(s->as.assign.op);
            if (op == NULL) return fail_node(err, "codegen: assignment operator not yet supported");
            cb(b, indent);
            cb_ident(b, s->as.assign.name);
            cb(b, " "); cb(b, op); cb(b, " ");
            // Wrap the value into the target's declared type (`bound`) — a case → its variant,
            // a `T` → `T?`, a fitting literal — exactly like a binding. (Plain numerics emit as-is;
            // `+=` etc. preserve the op.) Without this, `node = OptionalType { … }` (node: TypeExpr,
            // a variant) emitted the case's fields under the variant's C type → field-designator error.
            if (!emit_as(b, s->as.assign.bound, &s->as.assign.value, err)) return false;
            cb(b, ";\n");
            return true;
        }

        case TK_TSTMT_RETURN: {
            cb(b, indent);
            if (!s->as.ret.has_value) {
                cb(b, in_main ? "return 0;\n" : "return;\n");
                return true;
            }
            if (in_main) {
                cb(b, "return (int)(");
                if (!emit_expr(b, &s->as.ret.value, err)) return false;
                cb(b, ");\n");
            } else {
                cb(b, "return ");
                // W5b — wrap a case value into a variant return slot.
                if (!emit_as(b, ret_type, &s->as.ret.value, err)) return false;
                cb(b, ";\n");
            }
            return true;
        }

        case TK_TSTMT_EXPR:
            // W5a — an `if` in statement position runs as CONTROL FLOW (mirrors the VM's
            // exec_stmt → exec_if), so break/continue/return inside a branch propagate.
            if (s->as.expr_stmt.expr.tag == TK_TEXPR_IF)
                return emit_if_stmt(b, &s->as.expr_stmt.expr, in_main, ret_type, indent, err);
            // W5b — a `match` in statement position runs each winning arm body for effect
            // (mirrors the VM's exec_stmt → eval_match; result discarded).
            if (s->as.expr_stmt.expr.tag == TK_TEXPR_MATCH)
                return emit_match_stmt(b, &s->as.expr_stmt.expr, in_main, ret_type, indent, err);
            cb(b, indent);
            if (!emit_expr(b, &s->as.expr_stmt.expr, err)) return false;
            cb(b, ";\n");
            return true;

        case TK_TSTMT_LOOP: {
            tk_str lbl = s->as.loop_stmt.label;
            bool labeled = lbl.len > 0;
            cb(b, indent); cb(b, "while (1) {\n");
            // nest one level deeper
            char inner[64];
            snprintf(inner, sizeof inner, "%s    ", indent);
            // A labeled loop gets a continue-target at the TOP of its body: `continue L`
            // lowers to `goto tk_lbl_L_cont;`, re-running the body == next iteration of while(1).
            if (labeled) { cb(b, inner); cb(b, "tk_lbl_"); cb_str(b, lbl); cb(b, "_cont: ;\n"); }
            if (!emit_block(b, s->as.loop_stmt.body, s->as.loop_stmt.nbody, in_main, ret_type, inner, err))
                return false;
            cb(b, indent); cb(b, "}\n");
            // …and a break-target AFTER the loop: `break L` lowers to `goto tk_lbl_L_break;`.
            if (labeled) { cb(b, indent); cb(b, "tk_lbl_"); cb_str(b, lbl); cb(b, "_break: ;\n"); }
            return true;
        }

        case TK_TSTMT_BREAK:
            cb(b, indent);
            if (s->as.jump.label.len > 0) { cb(b, "goto tk_lbl_"); cb_str(b, s->as.jump.label); cb(b, "_break;\n"); }
            else                          { cb(b, "break;\n"); }
            return true;
        case TK_TSTMT_CONTINUE:
            cb(b, indent);
            if (s->as.jump.label.len > 0) { cb(b, "goto tk_lbl_"); cb_str(b, s->as.jump.label); cb(b, "_cont;\n"); }
            else                          { cb(b, "continue;\n"); }
            return true;
    }
    return fail_node(err, "codegen: unknown statement not yet supported");
}

// =========================================================================
// A top-level function -> a C function.
// =========================================================================
// Emit a function SIGNATURE (return type, name, parameter list) up to the closing `)`. Shared by
// the prototype pass (`;`) and the definition (`{ … }`), so both agree byte-for-byte.
static bool emit_function_sig(cbuf *b, tk_tfunction f, const char **err) {
    if (!emit_type(b, f.return_type, err)) return false;
    cb(b, " ");
    cb_fn_name(b, f.namespace, f.name);   // (#49) namespace-mangled C name
    cb(b, "(");
    if (f.nparams == 0) {
        cb(b, "void");
    } else {
        for (size_t i = 0; i < f.nparams; i += 1) {
            if (i > 0) cb(b, ", ");
            if (!emit_type_expr(b, f.params[i].type_ann, err)) return false;
            cb(b, " ");
            cb_ident(b, f.params[i].name);
        }
    }
    cb(b, ")");
    return true;
}

// Does this statement, AS LOWERED, guarantee C control does not fall through past it? A `return`
// does; a trailing value-expr lowers to `return <v>;` so it does too — EXCEPT a `match`/`if` value
// tail, which lowers to a condition chain whose exhaustiveness C cannot see (it warns -Wreturn-type
// and, worse, falls off the end as UB if ever reached). break/continue terminate the enclosing
// construct. A loop / binding / assign does NOT terminate the function.
static bool cg_stmt_c_terminates(const tk_tstatement *s) {
    if (s->tag == TK_TSTMT_RETURN || s->tag == TK_TSTMT_BREAK || s->tag == TK_TSTMT_CONTINUE) return true;
    if (s->tag == TK_TSTMT_EXPR)
        return s->as.expr_stmt.expr.tag != TK_TEXPR_MATCH && s->as.expr_stmt.expr.tag != TK_TEXPR_IF;
    return false;
}

static bool emit_function(cbuf *b, tk_tfunction f, const char **err) {
    g_cg_ret_type = f.return_type;   // so a return inside a value-form match/if wraps correctly
    if (!emit_function_sig(b, f, err)) return false;
    cb(b, " {\n");
    // W5a — a fn body's trailing expr-statement carrying a value implicitly returns it.
    // W5b — thread the fn's return type so a tail/return case value is wrapped into a
    // variant return slot (emit_as).
    if (!emit_block_tail(b, f.body, f.nbody, /*in_main=*/false, f.return_type, "    ", err)) return false;
    // A non-void function whose body's tail is a value-form `match`/`if` (or a loop) returns on
    // every path the CHECKER proved exhaustive — but C's flow analysis can't see that, so without a
    // terminator it warns -Wreturn-type and the fall-through is UB. The checker guarantees it is
    // never reached: emit `__builtin_unreachable()`. (Skip when the tail already C-terminates, and
    // for void functions — falling off the end is fine there.)
    bool needs_unreachable = f.return_type.tag != TK_TYPE_VOID
        && !(f.nbody > 0 && cg_stmt_c_terminates(&f.body[f.nbody - 1]));
    if (needs_unreachable) cb(b, "    __builtin_unreachable();\n");
    cb(b, "}\n\n");
    return true;
}

// =========================================================================
// W4c — TYPE DECLARATIONS -> C type declarations.
//   struct { f: T; … }    -> typedef struct tk_t_<M> { <Ctype> <f>; … } tk_t_<M>;
//   variant A | B | …     -> a tag enum tk_tag_<M> (one TK_TAG_<M>_<memberM> per member),
//                            then typedef struct tk_t_<M> { tk_tag_<M> tag;
//                            union { <memberCtype> <memberField>; … } as; } tk_t_<M>;
//   enum { A; B }         -> a C enum (typedef enum tk_t_<M> { TK_E_<M>_A, … } tk_t_<M>;)
// The `tag + union as` idiom is exactly what the bootstrap C itself uses (tk_type/tk_texpr).
// =========================================================================

// Emit the UPPERCASE form of a name's bytes (for enum/tag constants). Non-alnum bytes are
// passed through verbatim (identifiers are alnum/underscore, so this is just case folding).
static void cb_upper(cbuf *b, tk_str s) {
    char one[2] = { 0, '\0' };
    for (size_t i = 0; i < s.len; i += 1) {
        unsigned char c = s.ptr[i];
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        one[0] = (char)c;
        cb(b, one);
    }
}

// A variant member's union FIELD name: the member's bare last path segment (lowercased so
// it is a valid C field; user type names are already identifiers). The TAG constant uses
// the UPPERCASE form. Both derive from the same member name so they agree.
static tk_str variant_member_name(tk_type_expr m) {
    if (m.tag == TK_TEXPR_NAMED) {
        tk_path p = m.as.named.path;
        return p.segments[p.len - 1].name;
    }
    return (tk_str){ NULL, 0 };
}

// B-cg2 — a variant member's KEY (union field name + tag-constant suffix source) for a
// SYNTACTIC member type-expr. Reuses cg_opt_mangle_texpr so a NAMED member is its bare name,
// `error` is "error", a prim is its keyword, a slice is "slice_<elem>", an optional is
// "opt_<inner>". Used by emit_type_decl so EVERY member kind (not just named) lowers.
static bool cg_member_key_texpr(cbuf *b, tk_type_expr m, const char **err) {
    cbuf k = { NULL, 0, 0 };
    if (!cg_opt_mangle_texpr(&k, m, err)) { tk_free0(k.ptr); return false; }
    tk_str ks = { (const tk_byte *)k.ptr, k.len };
    cb_str(b, ks);
    if (cg_is_c_keyword(ks)) cb(b, "_");   // a C-keyword key (`bool`) → `bool_` union field
    tk_free0(k.ptr);
    return true;
}

// Emit the UPPERCASE form of a member key for a tag constant (TK_TAG_<V>_<UPPER key>). The
// key is rendered to a scratch cbuf, then case-folded via cb_upper.
static bool cg_member_key_upper_texpr(cbuf *b, tk_type_expr m, const char **err) {
    cbuf key = { NULL, 0, 0 };
    if (!cg_member_key_texpr(&key, m, err)) { tk_free0(key.ptr); return false; }
    cb_upper(b, (tk_str){ (const tk_byte *)key.ptr, key.len });
    tk_free0(key.ptr);
    return true;
}

// Emit ONE type declaration. `name` is the decl's bare name; namespace is empty (the typed
// item carries no provenance) so decl + ref mangle identically.
static bool emit_type_decl(cbuf *b, tk_type_decl d, const char **err) {
    tk_str ns = { NULL, 0 };
    switch (d.body.tag) {
        case TK_BODY_STRUCT: {
            tk_struct_body sb = d.body.as.struct_body;
            cb(b, "typedef struct ");
            mangle_type_name(b, ns, d.name);
            cb(b, " {\n");
            for (size_t i = 0; i < sb.n_fields; i += 1) {
                cb(b, "    ");
                if (!emit_type_expr(b, sb.fields[i].type_ann, err)) return false;
                cb(b, " ");
                // auto-box: a recursive back-edge field is a heap pointer (finite C struct).
                if (cg_field_boxed(d.name, sb.fields[i].type_ann)) cb(b, "*");
                cb_str(b, sb.fields[i].name);
                cb(b, ";\n");
            }
            cb(b, "} ");
            mangle_type_name(b, ns, d.name);
            cb(b, ";\n\n");
            return true;
        }
        case TK_BODY_VARIANT: {
            // A variant body is a UnionType (A | B | …). Members may be ANY type (B-cg2):
            // named cases, `error`, prims, slices — each mapped to a C-safe member KEY.
            tk_type_expr vt = d.body.as.variant_body.type_expr;
            if (vt.tag != TK_TEXPR_UNION)
                return fail_node(err, "codegen: a variant body must be a union of members");
            size_t nmem = vt.as.uni.len;
            // 1) the tag enum: TK_TAG_<UPPER Name>_<UPPER memberKey> per member.
            cb(b, "typedef enum ");
            cb(b, "tk_tag_"); cb_str(b, d.name);
            cb(b, " {\n");
            for (size_t i = 0; i < nmem; i += 1) {
                cb(b, "    TK_TAG_");
                cb_upper(b, d.name);
                cb(b, "_");
                if (!cg_member_key_upper_texpr(b, vt.as.uni.members[i], err)) return false;
                if (i + 1 < nmem) cb(b, ",");
                cb(b, "\n");
            }
            cb(b, "} tk_tag_"); cb_str(b, d.name); cb(b, ";\n\n");
            // 2) the value: tag + union as. Each union field is named by the member KEY and
            //    typed via emit_type_expr (covers named/error/prim/slice/optional members).
            cb(b, "typedef struct ");
            mangle_type_name(b, ns, d.name);
            cb(b, " {\n    tk_tag_"); cb_str(b, d.name); cb(b, " tag;\n    union {\n");
            for (size_t i = 0; i < nmem; i += 1) {
                tk_type_expr mem = vt.as.uni.members[i];
                cb(b, "        ");
                if (!emit_type_expr(b, mem, err)) return false;
                cb(b, " ");
                if (!cg_member_key_texpr(b, mem, err)) return false;   // union field == the member key
                cb(b, ";\n");
            }
            cb(b, "    } as;\n} ");
            mangle_type_name(b, ns, d.name);
            cb(b, ";\n\n");
            return true;
        }
        case TK_BODY_ENUM: {
            tk_enum_body eb = d.body.as.enum_body;
            cb(b, "typedef enum ");
            mangle_type_name(b, ns, d.name);
            cb(b, " {\n");
            for (size_t i = 0; i < eb.n_members; i += 1) {
                cb(b, "    TK_E_");
                cb_upper(b, d.name);
                cb(b, "_");
                cb_upper(b, eb.members[i]);
                if (i + 1 < eb.n_members) cb(b, ",");
                cb(b, "\n");
            }
            cb(b, "} ");
            mangle_type_name(b, ns, d.name);
            cb(b, ";\n\n");
            return true;
        }
        case TK_BODY_ALIAS:
            // A TRANSPARENT alias emits NO C type — references resolve through to the aliased
            // type at the checker, and codegen emits that resolved type at every use site.
            return true;
    }
    return fail_node(err, "codegen: unknown type body not yet supported");
}

// =========================================================================
// OPTIONAL TYPEDEF COLLECTION (REBOOT_PLAN §202). Every distinct optional inner type used
// anywhere in the typed program needs its `typedef struct { bool present; <inner> value; }
// tk_opt_<inner>;` emitted ONCE in the prelude (before any function). We walk every type that
// flows through emit_type (function sigs, bindings, and every texpr `.type`), collecting the
// distinct inner types (deduped by mangled name). Nested optionals (`T??`) register the inner
// optional first so its typedef precedes the outer's reference.
// =========================================================================
// Collects the per-type generated typedefs: optional inners (tk_opt_<i>) AND slice element
// types (tk_slice_<e>). One walk feeds both — cg_collect_type_opts registers a slice's element
// in the same traversal that registers optionals (so no second walker is needed).
typedef struct { tk_type *inners; size_t len; size_t cap;
                 tk_type *slices; size_t slen; size_t scap;
                 tk_type *variants; size_t vlen; size_t vcap; } cg_opt_set;   // B-cg2 — inline tk_u_<keys>

// Register a slice element type (dedup by mangle), parallel to cg_opt_set_add.
static void cg_slice_add(cg_opt_set *set, tk_type elem) {
    cbuf key = { NULL, 0, 0 }; const char *e = NULL;
    if (!cg_opt_mangle(&key, elem, &e)) { tk_free0(key.ptr); return; }   // unsupported element → skip (emit_type errors honestly)
    for (size_t i = 0; i < set->slen; i += 1) {
        cbuf prev = { NULL, 0, 0 }; const char *pe = NULL;
        if (!cg_opt_mangle(&prev, set->slices[i], &pe)) { tk_free0(prev.ptr); continue; }
        bool same = prev.len == key.len && (key.len == 0 || memcmp(prev.ptr, key.ptr, key.len) == 0);
        tk_free0(prev.ptr);
        if (same) { tk_free0(key.ptr); return; }   // already present
    }
    tk_free0(key.ptr);
    if (set->slen == set->scap) { set->scap = set->scap ? set->scap * 2 : 8;
        set->slices = tk_realloc0(set->slices, set->scap * sizeof *set->slices); }
    set->slices[set->slen++] = elem;
}

static void cg_opt_set_add(cg_opt_set *set, tk_type inner) {
    // Dedup by mangled name (render to a scratch cbuf, compare bytes).
    cbuf key = { NULL, 0, 0 }; const char *e = NULL;
    if (!cg_opt_mangle(&key, inner, &e)) { tk_free0(key.ptr); return; }   // unsupported inner → skip (emit_type will error honestly)
    for (size_t i = 0; i < set->len; i += 1) {
        cbuf prev = { NULL, 0, 0 }; const char *pe = NULL;
        if (!cg_opt_mangle(&prev, set->inners[i], &pe)) { tk_free0(prev.ptr); continue; }
        bool same = prev.len == key.len && (key.len == 0 || memcmp(prev.ptr, key.ptr, key.len) == 0);
        tk_free0(prev.ptr);
        if (same) { tk_free0(key.ptr); return; }   // already present
    }
    tk_free0(key.ptr);
    if (set->len == set->cap) { set->cap = set->cap ? set->cap * 2 : 8;
        set->inners = tk_realloc0(set->inners, set->cap * sizeof *set->inners); if (!set->inners) abort(); }
    set->inners[set->len++] = inner;
}

// B-cg2 — register an INLINE variant type (dedup by its tk_u_<keys> name), parallel to the
// optional/slice adders. A NAMED variant has its own decl typedef and is NOT registered here.
static void cg_variant_add(cg_opt_set *set, tk_type v) {
    cbuf key = { NULL, 0, 0 }; const char *e = NULL;
    if (!cg_variant_typename(&key, v, &e)) { tk_free0(key.ptr); return; }   // unsupported member → skip (emit_type errors honestly)
    for (size_t i = 0; i < set->vlen; i += 1) {
        cbuf prev = { NULL, 0, 0 }; const char *pe = NULL;
        if (!cg_variant_typename(&prev, set->variants[i], &pe)) { tk_free0(prev.ptr); continue; }
        bool same = prev.len == key.len && (key.len == 0 || memcmp(prev.ptr, key.ptr, key.len) == 0);
        tk_free0(prev.ptr);
        if (same) { tk_free0(key.ptr); return; }   // already present
    }
    tk_free0(key.ptr);
    if (set->vlen == set->vcap) { set->vcap = set->vcap ? set->vcap * 2 : 8;
        set->variants = tk_realloc0(set->variants, set->vcap * sizeof *set->variants); if (!set->variants) abort(); }
    set->variants[set->vlen++] = v;
}

// Register every optional in a type (and its nested inners, innermost first).
static void cg_collect_type_opts(cg_opt_set *set, tk_type t) {
    switch (t.tag) {
        case TK_TYPE_OPTIONAL:
            if (t.as.optional.inner != NULL) {
                cg_collect_type_opts(set, *t.as.optional.inner);   // inner first (so `T??` orders right)
                cg_opt_set_add(set, *t.as.optional.inner);
            }
            return;
        case TK_TYPE_SLICE:
            if (t.as.slice.element) {
                cg_collect_type_opts(set, *t.as.slice.element);   // recurse (covers [][]T, []T?) — innermost first
                cg_slice_add(set, *t.as.slice.element);            // register THIS slice's tk_slice_<elem> typedef
            }
            return;
        case TK_TYPE_VARIANT:
            // B-cg2 — an INLINE `A | B | …`. Recurse into each member FIRST (so a member that is
            // a slice/optional gets its tk_slice_/tk_opt_ typedef before this tk_u_ references it),
            // then register THIS variant's tk_u_<keys> typedef.
            for (size_t i = 0; i < t.as.variant.len; i += 1)
                cg_collect_type_opts(set, t.as.variant.members[i]);
            cg_variant_add(set, t);
            return;
        default: return;   // PRIM/STR/BYTE/ERROR/NAMED/FUNC/VOID carry no generated typedef to register here
    }
}

static void cg_collect_expr_opts(cg_opt_set *set, const tk_texpr *e);
static void cg_collect_block_opts(cg_opt_set *set, const tk_tstatement *stmts, size_t n);

static void cg_collect_expr_opts(cg_opt_set *set, const tk_texpr *e) {
    if (e == NULL) return;
    cg_collect_type_opts(set, e->type);   // the node's own resolved type may be `T?`
    switch (e->tag) {
        case TK_TEXPR_BINARY: cg_collect_expr_opts(set, e->as.binary.left); cg_collect_expr_opts(set, e->as.binary.right); return;
        case TK_TEXPR_UNARY:  cg_collect_expr_opts(set, e->as.unary.operand); return;
        case TK_TEXPR_COMPARE:
            cg_collect_expr_opts(set, e->as.compare.first);
            for (size_t i = 0; i < e->as.compare.nrest; i += 1) cg_collect_expr_opts(set, e->as.compare.rest[i].operand);
            return;
        case TK_TEXPR_CALL: for (size_t i = 0; i < e->as.call.nargs; i += 1) cg_collect_expr_opts(set, &e->as.call.args[i]); return;
        case TK_TEXPR_IF:
            cg_collect_expr_opts(set, e->as.if_expr.cond);
            cg_collect_block_opts(set, e->as.if_expr.then_blk, e->as.if_expr.nthen);
            cg_collect_block_opts(set, e->as.if_expr.else_blk, e->as.if_expr.nelse);
            return;
        case TK_TEXPR_MATCH:
            cg_collect_expr_opts(set, e->as.match_expr.subject);
            for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
                if (e->as.match_expr.arms[i].has_when) cg_collect_expr_opts(set, e->as.match_expr.arms[i].guard);
                cg_collect_block_opts(set, e->as.match_expr.arms[i].body, e->as.match_expr.arms[i].nbody);
            }
            return;
        case TK_TEXPR_CAST: cg_collect_expr_opts(set, e->as.cast.expr); return;
        case TK_TEXPR_FIELD_ACCESS:      cg_collect_expr_opts(set, e->as.field_access.receiver); return;
        case TK_TEXPR_SAFE_FIELD_ACCESS: cg_collect_expr_opts(set, e->as.safe_field_access.receiver); return;
        case TK_TEXPR_COALESCE: cg_collect_expr_opts(set, e->as.coalesce.left); cg_collect_expr_opts(set, e->as.coalesce.right); return;
        case TK_TEXPR_STRUCT_INIT: for (size_t i = 0; i < e->as.struct_init.nfields; i += 1) cg_collect_expr_opts(set, &e->as.struct_init.field_vals[i]); return;
        case TK_TEXPR_INDEX: cg_collect_expr_opts(set, e->as.index.receiver); cg_collect_expr_opts(set, e->as.index.index); return;
        case TK_TEXPR_INTERP: for (size_t i = 0; i < e->as.interp.nholes; i += 1) cg_collect_expr_opts(set, &e->as.interp.holes[i]); return;
        case TK_TEXPR_IN:
            cg_collect_expr_opts(set, e->as.in_expr.lhs);
            for (size_t i = 0; i < e->as.in_expr.nelems; i += 1) cg_collect_expr_opts(set, &e->as.in_expr.elems[i]);
            return;
        default: return;   // leaves (NUMBER/VAR/STR/BYTE/BOOL/NULL) — type already registered above
    }
}

static void cg_collect_block_opts(cg_opt_set *set, const tk_tstatement *stmts, size_t n) {
    for (size_t i = 0; i < n; i += 1) {
        const tk_tstatement *s = &stmts[i];
        switch (s->tag) {
            case TK_TSTMT_BINDING: cg_collect_type_opts(set, s->as.binding.bound); cg_collect_expr_opts(set, &s->as.binding.value); break;
            case TK_TSTMT_ASSIGN:  cg_collect_expr_opts(set, &s->as.assign.value); break;
            case TK_TSTMT_RETURN:  if (s->as.ret.has_value) cg_collect_expr_opts(set, &s->as.ret.value); break;
            case TK_TSTMT_LOOP:    cg_collect_block_opts(set, s->as.loop_stmt.body, s->as.loop_stmt.nbody); break;
            case TK_TSTMT_EXPR:    cg_collect_expr_opts(set, &s->as.expr_stmt.expr); break;
            default: break;   // BREAK/CONTINUE carry no expr
        }
    }
}

// B-cg2 — emit ONE inline-variant typedef `tk_u_<keys>` (the SAME tag-enum + tag/union-struct
// shape as a named variant decl, but named/keyed off the resolved member types). Members'
// typedefs (slice/opt) precede it by registration order.
static bool cg_emit_inline_variant_typedef(cbuf *b, tk_type v, const char **err) {
    size_t nmem = v.as.variant.len;
    // tag enum: typedef enum tk_tag_u_<keys> { TK_TAG_U_<keys>_<UPPER key>, … } tk_tag_u_<keys>;
    cb(b, "typedef enum tk_tag");
    for (size_t i = 0; i < nmem; i += 1) { cb(b, "_"); if (!cg_member_key(b, v.as.variant.members[i], err)) return false; }
    cb(b, " {\n");
    for (size_t i = 0; i < nmem; i += 1) {
        cb(b, "    TK_TAG_U");
        for (size_t j = 0; j < nmem; j += 1) {
            cb(b, "_");
            cbuf k = { NULL, 0, 0 };
            if (!cg_member_key(&k, v.as.variant.members[j], err)) { tk_free0(k.ptr); return false; }
            cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr);
        }
        cb(b, "_");
        { cbuf k = { NULL, 0, 0 };
          if (!cg_member_key(&k, v.as.variant.members[i], err)) { tk_free0(k.ptr); return false; }
          cb_upper(b, (tk_str){ (const tk_byte *)k.ptr, k.len }); tk_free0(k.ptr); }
        if (i + 1 < nmem) cb(b, ",");
        cb(b, "\n");
    }
    cb(b, "} tk_tag");
    for (size_t i = 0; i < nmem; i += 1) { cb(b, "_"); if (!cg_member_key(b, v.as.variant.members[i], err)) return false; }
    cb(b, ";\n");
    // value struct: typedef struct tk_u_<keys> { tk_tag_u_<keys> tag; union { … } as; } tk_u_<keys>;
    cb(b, "typedef struct ");
    if (!cg_variant_typename(b, v, err)) return false;
    cb(b, " {\n    tk_tag");
    for (size_t i = 0; i < nmem; i += 1) { cb(b, "_"); if (!cg_member_key(b, v.as.variant.members[i], err)) return false; }
    cb(b, " tag;\n    union {\n");
    for (size_t i = 0; i < nmem; i += 1) {
        cb(b, "        ");
        if (!emit_type(b, v.as.variant.members[i], err)) return false;
        cb(b, " ");
        if (!cg_member_key(b, v.as.variant.members[i], err)) return false;
        cb(b, ";\n");
    }
    cb(b, "    } as;\n} ");
    if (!cg_variant_typename(b, v, err)) return false;
    cb(b, ";\n");
    return true;
}

// Collect generated-typedef dependencies from a TYPE_DECL body (struct fields' resolved-ish
// types via type-exprs are barriers; here we only need the variant-decl member types, which the
// emit already covers — but a struct field that is `[]T` needs the slice typedef). The decl
// member positions carry SYNTACTIC type-exprs; the resolved walk above handles signatures and
// bodies, which is where the corpus's variants live. (Decls are left to emit honestly.)

// (The distinct optional / slice / inline-variant typedefs are emitted by cg_emit_types_ordered
// below, in by-value dependency order alongside the named decls.)

// =========================================================================
// SELF-HOST — TOPOLOGICAL type-declaration emission. A struct/variant body embeds named
// aggregates, optionals (tk_opt_) and inline variants (tk_u_) BY VALUE, so each must be FULLY
// defined BEFORE the body that embeds it. Slices (tk_slice_) are pointer-only (their named
// elements have forward typedefs), so they are emitted up front. The remaining {named, opt,
// inline-variant} typedefs are emitted in DEPENDENCY order by a fixpoint: each pass emits every
// not-yet-emitted node whose by-value dependencies are already emitted, until none remain. The
// value-type graph is a DAG (every Teko recursion passes through a slice = pointer), so the
// fixpoint terminates; a residue means a genuine cyclic value type (an internal error).
// =========================================================================
typedef struct {
    tk_type_decl *named; size_t nnamed; bool *named_done;   // struct/variant/enum decls (alias = no-op)
    cg_opt_set *set; bool *opt_done; bool *uvar_done;       // opt inners + inline variants
} cg_typenodes;

static void cg_key_type(cbuf *out, tk_type t)        { const char *e = NULL; cg_opt_mangle(out, t, &e); }
static void cg_key_texpr(cbuf *out, tk_type_expr t)  { const char *e = NULL; cg_opt_mangle_texpr(out, t, &e); }
static bool cg_key_eq(cbuf a, cbuf c) { return a.len == c.len && (a.len == 0 || memcmp(a.ptr, c.ptr, a.len) == 0); }

// Has the named decl `name` been emitted? (a builtin/alias not in our set has no edge → ready.)
static bool cg_named_emitted(cg_typenodes *N, tk_str name) {
    for (size_t i = 0; i < N->nnamed; i += 1)
        if (cg_name_eq(N->named[i].name, name)) return N->named_done[i];
    return true;
}
// Has the optional whose inner mangles to `key` been emitted? (no matching node → no edge.)
static bool cg_opt_emitted_key(cg_typenodes *N, cbuf key) {
    for (size_t i = 0; i < N->set->len; i += 1) {
        cbuf p = { NULL, 0, 0 }; cg_key_type(&p, N->set->inners[i]);
        bool eq = cg_key_eq(p, key); tk_free0(p.ptr);
        if (eq) return N->opt_done[i];
    }
    return true;
}
// Has the inline variant whose typename is `key` been emitted?
static bool cg_uvar_emitted_key(cg_typenodes *N, cbuf key) {
    for (size_t i = 0; i < N->set->vlen; i += 1) {
        cbuf p = { NULL, 0, 0 }; const char *pe = NULL; cg_variant_typename(&p, N->set->variants[i], &pe);
        bool eq = cg_key_eq(p, key); tk_free0(p.ptr);
        if (eq) return N->uvar_done[i];
    }
    return true;
}
// By-value readiness of a resolved type (an opt inner / a variant member tk_type).
static bool cg_type_ready(cg_typenodes *N, tk_type t) {
    switch (t.tag) {
        case TK_TYPE_NAMED: return cg_named_emitted(N, t.as.named.name);
        case TK_TYPE_OPTIONAL: {
            if (t.as.optional.inner == NULL) return true;
            cbuf k = { NULL, 0, 0 }; cg_key_type(&k, *t.as.optional.inner);
            bool r = cg_opt_emitted_key(N, k); tk_free0(k.ptr); return r;
        }
        case TK_TYPE_VARIANT: {
            cbuf k = { NULL, 0, 0 }; const char *e = NULL; cg_variant_typename(&k, t, &e);
            bool r = cg_uvar_emitted_key(N, k); tk_free0(k.ptr); return r;
        }
        case TK_TYPE_SLICE: return true;   // pointer (slice typedef pre-emitted)
        default: return true;              // prim/byte/str/error/void/func — no aggregate edge
    }
}
// By-value readiness of a field/member SYNTACTIC type-expr (struct field / variant member).
static bool cg_texpr_ready(cg_typenodes *N, tk_type_expr te) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            tk_str last = te.as.named.path.segments[te.as.named.path.len - 1].name;
            static const char *prims[] = { "u8","u16","u32","u64","u128","i8","i16","i32","i64",
                                           "i128","f16","f32","f64","bool","byte","str","error" };
            for (size_t i = 0; i < sizeof prims / sizeof *prims; i += 1)
                if (seg_is(last, prims[i])) return true;   // builtin scalar → ready
            return cg_named_emitted(N, last);
        }
        case TK_TEXPR_OPTIONAL: {
            cbuf k = { NULL, 0, 0 }; cg_key_texpr(&k, *te.as.optional.inner);
            bool r = cg_opt_emitted_key(N, k); tk_free0(k.ptr); return r;
        }
        case TK_TEXPR_SLICE: return true;   // pointer (slice typedef pre-emitted)
        case TK_TEXPR_UNION: {
            cbuf k = { NULL, 0, 0 }; const char *e = NULL; cg_variant_typename_texpr(&k, te, &e);
            bool r = cg_uvar_emitted_key(N, k); tk_free0(k.ptr); return r;
        }
        default: return true;
    }
}
// Is a named decl's body emittable now (all its by-value field/member deps already emitted)?
static bool cg_named_ready(cg_typenodes *N, tk_type_decl d) {
    if (d.body.tag == TK_BODY_STRUCT) {
        tk_struct_body sb = d.body.as.struct_body;
        for (size_t i = 0; i < sb.n_fields; i += 1) {
            // a boxed (recursive back-edge) field is a POINTER — needs only a forward typedef,
            // so it is NOT a by-value dependency (this is what breaks the cycle for the fixpoint).
            if (cg_field_boxed(d.name, sb.fields[i].type_ann)) continue;
            if (!cg_texpr_ready(N, sb.fields[i].type_ann)) return false;
        }
        return true;
    }
    if (d.body.tag == TK_BODY_VARIANT) {
        tk_type_expr vt = d.body.as.variant_body.type_expr;
        if (vt.tag == TK_TEXPR_UNION)
            for (size_t i = 0; i < vt.as.uni.len; i += 1)
                if (!cg_texpr_ready(N, vt.as.uni.members[i])) return false;
        return true;
    }
    return true;   // enum / alias — no by-value dependency
}

// Replaces the old program-order body emission + cg_emit_optional_typedefs: emit slices first
// (pointers), then the named/opt/inline-variant typedefs in by-value dependency order.
static bool cg_emit_types_ordered(cbuf *b, tk_tprogram prog, const char **err) {
    // 1) collect every optional inner / slice element / inline variant used in the program.
    cg_opt_set set = { NULL, 0, 0, NULL, 0, 0, NULL, 0, 0 };
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        switch (it.tag) {
            case TK_TITEM_FUNCTION:
                cg_collect_type_opts(&set, it.as.function.return_type);
                cg_collect_block_opts(&set, it.as.function.body, it.as.function.nbody);
                break;
            case TK_TITEM_STATEMENT: cg_collect_block_opts(&set, &it.as.statement, 1); break;
            default: break;
        }
    }
    // gather the named TYPE_DECL nodes (struct/variant/enum; alias emits nothing but is harmless).
    size_t nn = 0;
    for (size_t i = 0; i < prog.nitems; i += 1)
        if (prog.items[i].tag == TK_TITEM_TYPE_DECL) nn += 1;
    tk_type_decl *named = nn ? tk_alloc(nn * sizeof *named) : NULL;
    bool *named_done = nn ? tk_alloc(nn * sizeof *named_done) : NULL;
    { size_t j = 0;
      for (size_t i = 0; i < prog.nitems; i += 1)
          if (prog.items[i].tag == TK_TITEM_TYPE_DECL) { named[j] = prog.items[i].as.type_decl; named_done[j] = false; j += 1; } }
    bool *opt_done  = set.len  ? tk_alloc(set.len  * sizeof *opt_done)  : NULL;
    bool *uvar_done = set.vlen ? tk_alloc(set.vlen * sizeof *uvar_done) : NULL;
    for (size_t i = 0; i < set.len;  i += 1) opt_done[i]  = false;
    for (size_t i = 0; i < set.vlen; i += 1) uvar_done[i] = false;
    cg_typenodes N = { named, nn, named_done, &set, opt_done, uvar_done };

    #define CG_ORDERED_FREE() do { tk_free0(set.inners); tk_free0(set.slices); tk_free0(set.variants); \
        tk_free0(named); tk_free0(named_done); tk_free0(opt_done); tk_free0(uvar_done); } while (0)

    // 1a) ENUMS FIRST — full typedefs. An enum carries NO by-value type dependency (its members are
    //     plain int constants), so it can lead. Emitting it BEFORE the slice typedefs lets a `[]<enum>`
    //     slice element type (`tk_t_<Enum> *ptr`) resolve — a struct/variant gets a forward `typedef
    //     struct` instead, but a C enum can't be forward-declared as a struct, so we emit it complete
    //     here. Mark each done so the step-3 fixpoint SKIPS it (no duplicate typedef) and any struct
    //     embedding an enum sees it as ready. (Fixes `[]TokenKind` — tk_slice_TokenKind needs
    //     tk_t_TokenKind, which the body pass would otherwise emit only AFTER the slice.)
    size_t enum_count = 0;
    for (size_t i = 0; i < nn; i += 1) {
        if (named[i].body.tag != TK_BODY_ENUM) continue;
        if (!emit_type_decl(b, named[i], err)) { CG_ORDERED_FREE(); return false; }
        named_done[i] = true; enum_count += 1;
    }
    if (enum_count > 0) cb(b, "\n");

    // 1b) FORWARD typedefs for optionals + inline variants (named struct tags), so a boxed
    //     back-edge field (a POINTER to tk_opt_/tk_u_) and slices-of-opt/-uvar resolve before the
    //     bodies are emitted. A pointer needs a named, forward-declarable tag — an anonymous-struct
    //     typedef can't be forward-declared. (Same redef-to-same-type idiom as the named decls.)
    for (size_t i = 0; i < set.len; i += 1) {
        cb(b, "typedef struct ");
        if (!cg_opt_typename(b, set.inners[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, " ");
        if (!cg_opt_typename(b, set.inners[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, ";\n");
    }
    for (size_t i = 0; i < set.vlen; i += 1) {
        cb(b, "typedef struct ");
        if (!cg_variant_typename(b, set.variants[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, " ");
        if (!cg_variant_typename(b, set.variants[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, ";\n");
    }
    if (set.len + set.vlen > 0) cb(b, "\n");

    // 2) SLICE typedefs first (pointer-only; their named elements already have forward typedefs).
    for (size_t i = 0; i < set.slen; i += 1) {
        cb(b, "typedef struct { ");
        if (!emit_type(b, set.slices[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, " *ptr; uint64_t len; } ");
        if (!cg_slice_typename(b, set.slices[i], err)) { CG_ORDERED_FREE(); return false; }
        cb(b, ";\n");
    }
    if (set.slen > 0) cb(b, "\n");

    // 3) fixpoint: emit named bodies + optionals + inline variants in by-value dependency order.
    //    Enums were ALREADY emitted in step 1a (named_done set), so exclude them from `remaining`;
    //    the fixpoint skips them (named_done) and a struct embedding an enum is immediately ready.
    size_t remaining = nn + set.len + set.vlen - enum_count;
    bool progress = true;
    while (remaining > 0 && progress) {
        progress = false;
        for (size_t i = 0; i < nn; i += 1) {
            if (named_done[i] || !cg_named_ready(&N, named[i])) continue;
            if (!emit_type_decl(b, named[i], err)) { CG_ORDERED_FREE(); return false; }
            named_done[i] = true; remaining -= 1; progress = true;
        }
        for (size_t i = 0; i < set.len; i += 1) {
            if (opt_done[i] || !cg_type_ready(&N, set.inners[i])) continue;
            cb(b, "typedef struct ");   // named tag (matches the forward decl) so a pointer to it resolves
            if (!cg_opt_typename(b, set.inners[i], err)) { CG_ORDERED_FREE(); return false; }
            cb(b, " { bool present; ");
            if (!emit_type(b, set.inners[i], err)) { CG_ORDERED_FREE(); return false; }
            cb(b, " value; } ");
            if (!cg_opt_typename(b, set.inners[i], err)) { CG_ORDERED_FREE(); return false; }
            cb(b, ";\n");
            opt_done[i] = true; remaining -= 1; progress = true;
        }
        for (size_t i = 0; i < set.vlen; i += 1) {
            if (uvar_done[i]) continue;
            bool ready = true;   // a tk_u_ embeds its members by value
            for (size_t m = 0; m < set.variants[i].as.variant.len; m += 1)
                if (!cg_type_ready(&N, set.variants[i].as.variant.members[m])) { ready = false; break; }
            if (!ready) continue;
            if (!cg_emit_inline_variant_typedef(b, set.variants[i], err)) { CG_ORDERED_FREE(); return false; }
            uvar_done[i] = true; remaining -= 1; progress = true;
        }
    }
    if (remaining > 0) { CG_ORDERED_FREE(); return fail_node(err, "codegen: cyclic value-type dependency (a recursive type must pass through a slice or a boxed reference)"); }
    CG_ORDERED_FREE();
    #undef CG_ORDERED_FREE
    return true;
}

// =========================================================================
// The program -> a full C translation unit.
// =========================================================================
static tk_cstr_result cg_err(const char *m) {
    return (tk_cstr_result){ .ok = false, .as.error = tk_error_make(m) };
}

// cg_format_c — re-flow the generated C into readable, brace-indented form. The codegen emits
// statements line-by-line but packs each long `({ … })` statement-expression onto ONE line; this
// post-pass breaks after `{`/`;` and before `}`, re-indenting by brace depth. It is structural, not
// semantic: STRING/CHAR literals, `//` line comments and `#` preprocessor lines are copied verbatim
// (never broken); a `;` inside `(…)` (a `for` header) does NOT break; runs of whitespace collapse to
// one space. The result is re-compiled by cc on every build, so any corruption fails loudly.
static char *cg_format_c(const char *src) {
    cbuf out = { NULL, 0, 0 };
    size_t depth = 0;            // open-brace count (= indent level)
    char stk[4096]; size_t sp = 0;   // bracket stack — innermost decides `;` breaking ({ vs ( )
    bool blk[4096];              // per-`{`: is it a function/control BLOCK (preceded by `)`)? — for top-level separation
    bool line_start = true;      // at the start of a fresh output line?
    char one[2] = { 0, 0 };
    size_t i = 0;
    while (src[i] != '\0') {
        char c = src[i];
        // `#` preprocessor directive at line start → copy the whole physical line verbatim.
        if (c == '#' && line_start) {
            while (src[i] != '\0' && src[i] != '\n') { one[0] = src[i]; cb(&out, one); i += 1; }
            cb(&out, "\n"); if (src[i] == '\n') i += 1; line_start = true; continue;
        }
        // `//` line comment → copy to end of line.
        if (c == '/' && src[i + 1] == '/') {
            if (line_start) { for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); line_start = false; }
            while (src[i] != '\0' && src[i] != '\n') { one[0] = src[i]; cb(&out, one); i += 1; }
            cb(&out, "\n"); if (src[i] == '\n') i += 1; line_start = true; continue;
        }
        // string / char literal → copy verbatim (incl. escapes); never break inside.
        if (c == '"' || c == '\'') {
            if (line_start) { for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); line_start = false; }
            char q = c; one[0] = c; cb(&out, one); i += 1;
            while (src[i] != '\0' && src[i] != q) {
                if (src[i] == '\\' && src[i + 1] != '\0') { one[0] = src[i]; cb(&out, one); one[0] = src[i + 1]; cb(&out, one); i += 2; }
                else { one[0] = src[i]; cb(&out, one); i += 1; }
            }
            if (src[i] == q) { one[0] = q; cb(&out, one); i += 1; }
            continue;
        }
        // whitespace → collapse: skip at line start; else a single space (no double spaces).
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i += 1;
            if (!line_start && out.len > 0 && out.ptr[out.len - 1] != ' ') cb(&out, " ");
            continue;
        }
        if (c == '{') {
            if (line_start) { for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); line_start = false; }
            if (out.len > 0 && out.ptr[out.len - 1] == ' ') out.len -= 1;   // trim a space before `{`
            bool is_block = (out.len > 0 && out.ptr[out.len - 1] == ')');   // `) {` = function/control body (vs `struct {`, `= {`, `({`)
            cb(&out, " {\n");
            if (sp < sizeof stk) { stk[sp] = '{'; blk[sp] = is_block; }
            depth += 1; sp += 1;
            line_start = true; i += 1; continue;
        }
        if (c == '}') {
            while (out.len > 0 && (out.ptr[out.len - 1] == ' ' || out.ptr[out.len - 1] == '\n')) out.len -= 1;
            bool was_block = (sp > 0 && sp <= sizeof stk && blk[sp - 1]);
            if (depth > 0) depth -= 1; if (sp > 0) sp -= 1;
            cb(&out, "\n"); for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); cb(&out, "}");
            // a TOP-LEVEL block close (a function body) → blank line before the next declaration.
            if (depth == 0 && was_block) { cb(&out, "\n\n"); line_start = true; }
            else line_start = false;
            i += 1; continue;
        }
        if (c == ';') {
            if (out.len > 0 && out.ptr[out.len - 1] == ' ') out.len -= 1;
            cb(&out, ";");
            // break after `;` only when the innermost open bracket is `{` (NOT a `for(…;…;…)` header).
            if (sp == 0 || (sp <= sizeof stk && stk[sp - 1] == '{')) { cb(&out, "\n"); line_start = true; }
            else if (out.len > 0 && out.ptr[out.len - 1] != ' ') cb(&out, " ");
            i += 1; continue;
        }
        if (c == '(' || c == '[') {
            if (line_start) { for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); line_start = false; }
            if (sp < sizeof stk) { stk[sp] = c; } sp += 1; one[0] = c; cb(&out, one); i += 1; continue;
        }
        if (c == ')' || c == ']') {
            if (sp > 0) sp -= 1; one[0] = c; cb(&out, one); i += 1; continue;
        }
        if (line_start) { for (size_t k = 0; k < depth; k += 1) cb(&out, "    "); line_start = false; }
        one[0] = c; cb(&out, one); i += 1;
    }
    cb(&out, "\n");
    return out.ptr;
}

tk_cstr_result tk_emit_c(tk_tprogram prog) {
    cbuf b = { .ptr = NULL, .len = 0, .cap = 0 };
    const char *err = NULL;

    // W5b — stash the program so expr emission can find variant decls (the wrap scheme +
    // pattern tag/field computation), mirroring the VM's g_prog. Set once, read-only.
    g_cg_prog = prog;

    cb(&b, "// generated by teko (F2 backend) — do not edit\n");
    cb(&b, "#include <stdint.h>\n");
    cb(&b, "#include <stdbool.h>\n");
    cb(&b, "#include <stdlib.h>\n");   // malloc/abort — slice copy-append (fixed+copy)
    cb(&b, "#include <math.h>\n");     // floor/… — float ops the corpus lowers inline (link -lm)
    cb(&b, "#include \"teko_rt.h\"\n");
    cb(&b, "#include \"assert.h\"\n\n");   // teko::assert seed decls (driver adds its -I)

    // W4c — TYPE DECLS come FIRST (before any function that uses them). Two sub-passes:
    //   a) a forward `typedef struct tk_t_<M> tk_t_<M>;` for every STRUCT/VARIANT decl,
    //      so a field/member that references a not-yet-defined aggregate (recursive or
    //      out-of-order types) still compiles.
    //   b) the full type declarations (struct bodies, variant tag+union, enums).
    // (enums need no forward typedef — they carry no aggregate self-reference.)
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl d = it.as.type_decl;
        if (d.body.tag == TK_BODY_STRUCT || d.body.tag == TK_BODY_VARIANT) {
            cb(&b, "typedef struct ");
            mangle_type_name(&b, (tk_str){ NULL, 0 }, d.name);
            cb(&b, " ");
            mangle_type_name(&b, (tk_str){ NULL, 0 }, d.name);
            cb(&b, ";\n");
        }
    }
    cb(&b, "\n");
    // Full type declarations IN DEPENDENCY ORDER (self-host): slice typedefs first (pointers),
    // then named bodies + optionals (REBOOT_PLAN §202) + inline variants topologically, so every
    // by-value embed (`tk_t_Y`, `tk_opt_T`, `tk_u_…`) is fully defined before the body using it.
    // This is also before any function (so `T?`/`[]T`/variant signatures resolve).
    if (!cg_emit_types_ordered(&b, prog, &err)) { tk_free0(b.ptr); return cg_err(err); }

    // Function PROTOTYPES — a forward declaration for every function, so a call to a function
    // defined LATER in the unit (mutual recursion / forward reference across the merged corpus)
    // resolves. C23 makes an undeclared call an error, so without these the whole self-host corpus
    // (which freely forward-references) fails to compile.
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        if (!emit_function_sig(&b, prog.items[i].as.function, &err)) { tk_free0(b.ptr); return cg_err(err); }
        cb(&b, ";\n");
    }
    cb(&b, "\n");

    // First pass: emit every top-level function. Use-decls/type-decls are handled above.
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        switch (it.tag) {
            case TK_TITEM_FUNCTION:
                if (!emit_function(&b, it.as.function, &err)) { tk_free0(b.ptr); return cg_err(err); }
                break;
            case TK_TITEM_USE:
                break;   // imports are a no-op at the C level (M0)
            case TK_TITEM_TYPE_DECL:
                break;   // emitted in the type-decl passes above
            case TK_TITEM_STATEMENT:
                break;   // virtual-main statements handled in the second pass
        }
    }

    // Second pass: the VIRTUAL-MAIN — the loose top-level statements, in order, become
    // the body of C main(); falling off the end -> `return 0;` (default exit 0).
    // W5a — the LAST loose statement, if it is a value-carrying expr-statement, becomes the
    // process exit code via `return (int)(…)` (mirrors tk_vm_run: the virtual-main's trailing
    // value is the exit code — B.20). emit_block_tail threads in_main=true so a tail `if`'s
    // branch returns lower to `return (int)(…)`.
    // Find the LAST loose statement's index (the tail).
    size_t last_stmt = prog.nitems;   // sentinel: no loose statements
    for (size_t i = 0; i < prog.nitems; i += 1)
        if (prog.items[i].tag == TK_TITEM_STATEMENT) last_stmt = i;
    // main captures argv so teko::env::args() (tk_rt_args) can return it; tk_set_args first.
    cb(&b, "int main(int argc, char **argv) {\n");
    cb(&b, "    tk_set_args(argc, argv);\n");
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag != TK_TITEM_STATEMENT) continue;
        // The virtual-main has no variant return slot (its tail value is the int exit
        // code), so ret_type is void — emit_as never wraps for void.
        tk_type main_ret = { .tag = TK_TYPE_VOID };
        bool ok = (i == last_stmt)
            ? emit_block_tail(&b, &it.as.statement, 1, /*in_main=*/true, main_ret, "    ", &err)
            : emit_stmt(&b, &it.as.statement, /*in_main=*/true, main_ret, "    ", &err);
        if (!ok) { tk_free0(b.ptr); return cg_err(err); }
    }
    cb(&b, "    return 0;\n");
    cb(&b, "}\n");

    // Re-flow into readable, brace-indented C (the raw emission packs each `({…})` on one line).
    char *formatted = cg_format_c(b.ptr);
    tk_free0(b.ptr);
    return (tk_cstr_result){ .ok = true, .as.value = formatted };
}

// E3 — the `.tsym` symbol map: one line per emitted function mapping its mangled C symbol to the
// Teko qualified name and source `file:line`. Written beside the binary so a native stack-trace
// (E4 — teko_rt's panic/crash backtrace) can resolve each C frame to its Teko origin.
//   <c-symbol>\t<teko-name>\t<file>:<line>
tk_cstr_result tk_emit_tsym(tk_tprogram prog) {
    cbuf b = { NULL, 0, 0 };
    cb(&b, "# teko symbol map (.tsym v1): <c-symbol>\\t<teko-name>\\t<file>:<line>\n");
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        cb_fn_name(&b, f.namespace, f.name);   // the mangled C symbol (matches the definition)
        cb(&b, "\t");
        if (f.namespace.len != 0) { cb_str(&b, f.namespace); cb(&b, "::"); }
        cb_str(&b, f.name);                    // the Teko qualified name
        cb(&b, "\t");
        cb_str(&b, f.file);
        cb(&b, ":");
        cb_u64_dec(&b, (uint64_t)f.line);
        cb(&b, "\n");
    }
    return (tk_cstr_result){ .ok = true, .as.value = b.ptr };
}
