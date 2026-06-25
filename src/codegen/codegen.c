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
    char *np = realloc(b->ptr, ncap);
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

static bool emit_type(cbuf *b, tk_type t, const char **err) {
    switch (t.tag) {
        case TK_TYPE_PRIM: return emit_prim(b, t.as.prim, err);
        case TK_TYPE_BYTE: cb(b, "uint8_t"); return true;
        // VOID is the return-only marker (M.3): a `-> void` function lowers to C `void`.
        // The checker (Z2a) forbids void as a value/binding/variant member, so reaching
        // here is always a Func.ret position.
        case TK_TYPE_VOID:    cb(b, "void");   return true;
        case TK_TYPE_STR:     cb(b, "tk_str"); return true;
        // OPTIONAL (T?) — the parser doesn't emit it yet; full lowering is Z3-codegen.
        // Be honest now rather than mis-emit (M.3).
        case TK_TYPE_OPTIONAL: return fail_node(err, "codegen: optional type not yet supported");
        case TK_TYPE_SLICE:   return fail_node(err, "codegen: slice type not yet supported");
        // W4c — a named aggregate references its mangled typedef name (decl + ref agree
        // via mangle_type_name). The semantic NAMED carries only the bare name.
        case TK_TYPE_NAMED:   mangle_type_name(b, (tk_str){ NULL, 0 }, t.as.named.name); return true;
        case TK_TYPE_VARIANT: return fail_node(err, "codegen: variant type not yet supported");
        case TK_TYPE_FUNC:    return fail_node(err, "codegen: function type not yet supported");
        case TK_TYPE_ERROR:   return fail_node(err, "codegen: error type not yet supported");
    }
    return fail_node(err, "codegen: unknown type not yet supported");
}

// =========================================================================
// W4c — a SYNTACTIC type-expr (parser tk_type_expr) -> C type text. Needed for type-decl
// member positions (struct field annotations, variant member types), where the typed item
// carries the SYNTACTIC body, not the resolved `tk_type`. A NAMED type-expr's last path
// segment is either a built-in name (u8..f64/bool/byte/str -> its C type) or a user type
// (-> its mangled typedef). Slice/union/optional members are honest barriers (later waves).
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
            else if (seg_is(last, "error")) return fail_node(err, "codegen: error type not yet supported");
            // a user-defined named aggregate -> its mangled typedef name (matches emit_type).
            mangle_type_name(b, (tk_str){ NULL, 0 }, last);
            return true;
        }
        case TK_TEXPR_SLICE:    return fail_node(err, "codegen: slice type not yet supported");
        case TK_TEXPR_UNION:    return fail_node(err, "codegen: inline union type not yet supported");
        case TK_TEXPR_OPTIONAL: return fail_node(err, "codegen: optional type not yet supported");
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
static bool emit_expr(cbuf *b, const tk_texpr *e, const char **err);
static bool emit_stmt(cbuf *b, const tk_tstatement *s, bool in_main,
                      tk_type ret_type, const char *indent, const char **err);
// W5b — emit `value` into a slot whose EXPECTED type is `expected`. When `expected` is a
// (named) variant and `value`'s type is one of its case members, WRAP the value into the
// variant's `tag + union` representation; otherwise emit the value plainly. (The VM needs
// no wrap because its struct value already carries the case name — this synthesizes the
// C wrap, completing the W4c-deferred variant-value layer.)
static bool emit_as(cbuf *b, tk_type expected, const tk_texpr *value, const char **err);
// W5b — a `match` lowered to a GNU statement-expression (the VALUE form).
static bool emit_match_value(cbuf *b, const tk_texpr *e, const char **err);
// W5b — a `match` in TAIL position (each arm body `return`s) and in STATEMENT position
// (each arm body runs for effect). `ret_type` threads the enclosing fn return type so a
// tail arm body that yields a case value into a variant return slot is wrapped (emit_as).
static bool emit_match_tail(cbuf *b, const tk_texpr *e, bool in_main,
                            tk_type ret_type, const char *indent, const char **err);
static bool emit_match_stmt(cbuf *b, const tk_texpr *e, const char *indent, const char **err);
// Arm bodies are STATEMENT BLOCKS (B.20); these tail/block emitters lower them. Forward-
// declared here so the match value form (emit_arm_value) can route a diverging arm.
static bool emit_block(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                       tk_type ret_type, const char *indent, const char **err);
static bool emit_block_tail(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                            tk_type ret_type, const char *indent, const char **err);
static bool emit_exprstmt_tail(cbuf *b, const tk_texpr *x, bool in_main,
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
static bool emit_stmt_value(cbuf *b, const tk_tstatement *s, const char *sink,
                            const char *indent, const char **err) {
    if (s->tag == TK_TSTMT_RETURN || s->tag == TK_TSTMT_BREAK || s->tag == TK_TSTMT_CONTINUE)
        return fail_node(err, "codegen: control flow inside an `if` used as a sub-expression not yet supported");
    if (s->tag == TK_TSTMT_EXPR && s->as.expr_stmt.expr.type.tag != TK_TYPE_VOID) {
        cb(b, indent);
        cb(b, sink); cb(b, " = ");
        if (!emit_expr(b, &s->as.expr_stmt.expr, err)) return false;
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
static bool emit_branch_value(cbuf *b, const tk_tstatement *body, size_t n,
                              const char *sink, const char *indent, const char **err) {
    for (size_t i = 0; i + 1 < n; i += 1)
        if (!emit_stmt(b, &body[i], /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, indent, err)) return false;
    if (n > 0)
        if (!emit_stmt_value(b, &body[n - 1], sink, indent, err)) return false;
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
    if (!emit_branch_value(b, e->as.if_expr.then_blk, e->as.if_expr.nthen, tmp, "    ", err))
        return false;
    cb(b, "} else {\n");
    if (!emit_branch_value(b, e->as.if_expr.else_blk, e->as.if_expr.nelse, tmp, "    ", err))
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
            cb_str(b, e->as.var.name);
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
                cb(b, "(");
                if (!emit_expr(b, prev, err)) return false;
                cb(b, " "); cb(b, op); cb(b, " ");
                if (!emit_expr(b, term.operand, err)) return false;
                cb(b, ")");
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
            // Non-shadowable built-ins: `print`/`println`, either bare or under `teko`.
            const char *builtin = NULL;
            if (p.len >= 1) {
                tk_str last = p.segments[p.len - 1].name;
                bool addressable = (p.len == 1) || seg_is(p.segments[0].name, "teko");
                if (addressable) {
                    if      (seg_is(last, "print"))   builtin = "tk_print";
                    else if (seg_is(last, "println")) builtin = "tk_println";
                }
            }
            if (builtin != NULL) {
                cb(b, builtin);
            } else {
                // A user call lowers to the callee's BARE name (its LAST path segment) — the
                // SAME key the VM's find_function matches on, and the SAME name emit_function
                // gives the decl (tf.name is the bare function name). So a cross-namespace
                // call `ns::fn()` and the decl `fn` agree, and both backends call identically.
                cb_str(b, p.segments[p.len - 1].name);
            }
            cb(b, "(");
            for (size_t i = 0; i < e->as.call.nargs; i += 1) {
                if (i > 0) cb(b, ", ");
                if (!emit_expr(b, &e->as.call.args[i], err)) return false;
            }
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_FIELD_ACCESS: {
            // W5-idx — `.len`: a `str` lowers to the runtime tk_str's `.len` member
            // (`(recv).len`); a slice is an honest barrier (slice VALUES are the next
            // feature). Otherwise a plain struct-field read `(recv.field)`.
            tk_type rt = e->as.field_access.receiver->type;
            if (rt.tag == TK_TYPE_SLICE && seg_is(e->as.field_access.field, "len"))
                return fail_node(err, "codegen: slice .len not yet supported (slice value layer pending)");
            cb(b, "(");
            if (!emit_expr(b, e->as.field_access.receiver, err)) return false;
            cb(b, ".");
            cb_str(b, e->as.field_access.field);
            cb(b, ")");
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

        // null literal + safe-field-access (`x?.field`) + coalesce (`x ?? y`) (W2 surface)
        // all need the OPTIONAL value representation (presence+value), which is a later
        // wave (W6). Be honest now rather than mis-emit (M.3), exactly like the if/match
        // frontier above — never crash, never emit broken C.
        case TK_TEXPR_NULL:
            return fail_node(err, "codegen: null literal not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_SAFE_FIELD_ACCESS:
            return fail_node(err, "codegen: safe field access (x?.field) not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_COALESCE:
            return fail_node(err, "codegen: coalesce (x ?? y) not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_STRUCT_INIT: {
            // W4c — `Name { f = v, … }` -> a C compound literal in DECLARED field order
            //   (tk_t_<MANGLE>){ .<field> = <val>, … }
            // The checker already reordered field_names/field_vals into the struct's
            // declared order (expr.c), so both backends lower identically (differential
            // equivalence with the VM, which stores fields in the same order).
            //
            // The node's `.type` is TK_TYPE_NAMED (a user struct -> mangle its name) or
            // TK_TYPE_ERROR (`error { message = … }`). `error` has NO C value
            // representation in the generated TU yet (teko_rt.h defines no tk_error value
            // type), so that case is an honest barrier — named-struct construction works.
            if (e->type.tag == TK_TYPE_ERROR)
                return fail_node(err, "codegen: error value construction not yet supported (error has no C value repr yet)");
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
                if (!emit_expr(b, &e->as.struct_init.field_vals[i], err)) return false;
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
            if (rt.tag == TK_TYPE_SLICE)
                return fail_node(err, "codegen: slice indexing not yet supported (slice value layer pending)");
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
            cb(b, " : (tk_panic_oob(), (tk_byte)0); })");
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
static bool emit_as(cbuf *b, tk_type expected, const tk_texpr *value, const char **err) {
    // Wrap only when: expected is a NAMED variant, the value's type is a NAMED case, and
    // that case is one of the variant's members. (If value's type IS the variant already
    // — e.g. a var of the variant type — it is not a bare member, so no wrap.)
    if (expected.tag == TK_TYPE_NAMED && value->type.tag == TK_TYPE_NAMED) {
        const tk_type_decl *vd = cg_find_variant_decl(expected.as.named.name);
        if (vd != NULL && cg_variant_has_member(vd, value->type.as.named.name)) {
            cb(b, "(");
            mangle_type_name(b, (tk_str){ NULL, 0 }, expected.as.named.name);
            cb(b, "){ .tag = TK_TAG_");
            cb_upper(b, expected.as.named.name);
            cb(b, "_");
            cb_upper(b, value->type.as.named.name);
            cb(b, ", .as.");
            cb_str(b, value->type.as.named.name);   // union field == the member's bare name
            cb(b, " = ");
            if (!emit_expr(b, value, err)) return false;
            cb(b, " }");
            return true;
        }
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

// Emit the boolean TEST that `_s` (`subj`) matches `pat`. `variant_name` is the subject's
// variant name (its NAMED type), needed to spell a case's tag constant; it is empty for a
// scalar subject. Mirrors pat_match's match decisions (no binding here — binds are separate).
static bool emit_pat_test(cbuf *b, const tk_pattern *pat, const char *subj,
                          tk_str variant_name, const char **err) {
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
                if (!emit_pat_test(b, &pat->as.alt.options[i], subj, variant_name, err)) return false;
            }
            cb(b, ")");
            return true;
        case TK_PAT_BIND:
            cb(b, "("); cb(b, subj); cb(b, ".tag == TK_TAG_");
            cb_upper(b, variant_name);
            cb(b, "_");
            cb_upper(b, cg_path_last(pat->as.bind.type_name));
            cb(b, ")");
            return true;
        case TK_PAT_FIELD:
            cb(b, "("); cb(b, subj); cb(b, ".tag == TK_TAG_");
            cb_upper(b, variant_name);
            cb(b, "_");
            cb_upper(b, cg_path_last(pat->as.field.type_name));
            cb(b, ")");
            return true;
    }
    return fail_node(err, "codegen: unknown pattern not yet supported");
}

// Emit the BINDINGS a pattern introduces (BIND `as x`, FIELD fields), at the top of the
// arm block (so they are in scope for the guard and the body). Mirrors pat_match's
// env_define calls. Scalar/Alt/Wildcard patterns bind nothing.
static bool emit_pat_binds(cbuf *b, const tk_pattern *pat, const char *subj,
                           const char *indent, const char **err) {
    switch (pat->tag) {
        case TK_PAT_BIND:
            if (pat->as.bind.has_binding) {
                // `Foo as x` binds the WHOLE case value (the union member struct).
                cb(b, indent); cb(b, "auto ");
                cb_str(b, pat->as.bind.binding);
                cb(b, " = "); cb(b, subj); cb(b, ".as.");
                cb_str(b, cg_path_last(pat->as.bind.type_name));
                cb(b, ";\n");
            }
            return true;
        case TK_PAT_FIELD:
            for (size_t i = 0; i < pat->as.field.n_fields; i += 1) {
                cb(b, indent); cb(b, "auto ");
                cb_str(b, pat->as.field.fields[i]);
                cb(b, " = "); cb(b, subj); cb(b, ".as.");
                cb_str(b, cg_path_last(pat->as.field.type_name));
                cb(b, ".");
                cb_str(b, pat->as.field.fields[i]);
                cb(b, ";\n");
            }
            return true;
        default:
            return true;   // WILDCARD/LITERAL/RANGE/ALT bind nothing
    }
}

// The subject's variant name (its NAMED type), or empty for a scalar subject. The tag
// constant needs the VARIANT name; the case name comes from the pattern.
static tk_str cg_match_variant_name(const tk_texpr *e) {
    if (e->as.match_expr.subject->type.tag == TK_TYPE_NAMED)
        return e->as.match_expr.subject->type.as.named.name;
    return (tk_str){ NULL, 0 };
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
        // diverging trailing if/match) leaves the enclosing function. No `sink =` / `break`.
        for (size_t i = 0; i + 1 < n; i += 1)
            if (!emit_stmt(b, &body[i], /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, commit_indent, err)) return false;
        const tk_tstatement *last = &body[n - 1];
        if (last->tag == TK_TSTMT_EXPR &&
            (last->as.expr_stmt.expr.tag == TK_TEXPR_IF || last->as.expr_stmt.expr.tag == TK_TEXPR_MATCH))
            return emit_exprstmt_tail(b, &last->as.expr_stmt.expr, /*in_main=*/false, match_e->type, commit_indent, err);
        return emit_stmt(b, last, /*in_main=*/false, match_e->type, commit_indent, err);
    }
    // Value form: all-but-last normally; the trailing value → `sink` (wrapped), then `break`.
    for (size_t i = 0; i + 1 < n; i += 1)
        if (!emit_stmt(b, &body[i], /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, commit_indent, err)) return false;
    if (n == 0) return fail_node(err, "codegen: empty match arm body in value position");
    const tk_tstatement *last = &body[n - 1];
    if (last->tag != TK_TSTMT_EXPR)
        return fail_node(err, "codegen: non-value trailing statement in a match arm used as a value");
    cb(b, commit_indent); cb(b, sink); cb(b, " = (");
    if (!emit_as(b, match_e->type, &last->as.expr_stmt.expr, err)) return false;
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
    tk_str vn = cg_match_variant_name(e);
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
        if (!emit_pat_test(b, &arm->pattern, subj, vn, err)) return false;
        cb(b, ") {\n");
        if (!emit_pat_binds(b, &arm->pattern, subj, "        ", err)) return false;
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
    tk_str vn = cg_match_variant_name(e);
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
        if (!emit_pat_test(b, &arm->pattern, subj, vn, err)) return false;
        cb(b, ") {\n");
        if (!emit_pat_binds(b, &arm->pattern, subj, inner2, err)) return false;
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
static bool emit_match_stmt(cbuf *b, const tk_texpr *e, const char *indent, const char **err) {
    char subj[40], done[48];
    snprintf(subj, sizeof subj, "_s%zu", (size_t)b->len);
    snprintf(done, sizeof done, "tk_m%zu_done", (size_t)b->len + 1);   // unique commit label
    tk_str vn = cg_match_variant_name(e);
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
        if (!emit_pat_test(b, &arm->pattern, subj, vn, err)) return false;
        cb(b, ") {\n");
        if (!emit_pat_binds(b, &arm->pattern, subj, inner2, err)) return false;
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
        // inside it propagates via C's own semantics (emit_block emits each statement as-is).
        if (!emit_block(b, arm->body, arm->nbody, /*in_main=*/false, (tk_type){ .tag = TK_TYPE_VOID }, ci, err)) return false;
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
        if (!x->as.if_expr.has_else)
            return fail_node(err, "codegen: if-without-else in tail position not yet supported");
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
    for (size_t i = 0; i + 1 < n; i += 1)
        if (!emit_stmt(b, &body[i], in_main, ret_type, indent, err)) return false;
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
            cb(b, indent);
            if (s->as.binding.kind == TK_BIND_CONST) cb(b, "const ");
            if (!emit_type(b, s->as.binding.bound, err)) return false;
            cb(b, " ");
            cb_str(b, tgt.as.simple.name);
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
            cb_str(b, s->as.assign.name);
            cb(b, " "); cb(b, op); cb(b, " ");
            if (!emit_expr(b, &s->as.assign.value, err)) return false;
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
                return emit_match_stmt(b, &s->as.expr_stmt.expr, indent, err);
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
static bool emit_function(cbuf *b, tk_tfunction f, const char **err) {
    if (!emit_type(b, f.return_type, err)) return false;
    cb(b, " ");
    cb_str(b, f.name);
    cb(b, "(");
    if (f.nparams == 0) {
        cb(b, "void");
    } else {
        for (size_t i = 0; i < f.nparams; i += 1) {
            if (i > 0) cb(b, ", ");
            // Param type annotations are syntactic (tk_type_expr); M0 has none, so
            // params are not yet supported (would require resolving the annotation).
            (void)f.params;
            return fail_node(err, "codegen: function parameters not yet supported");
        }
    }
    cb(b, ") {\n");
    // W5a — a fn body's trailing expr-statement carrying a value implicitly returns it.
    // W5b — thread the fn's return type so a tail/return case value is wrapped into a
    // variant return slot (emit_as).
    if (!emit_block_tail(b, f.body, f.nbody, /*in_main=*/false, f.return_type, "    ", err)) return false;
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
                cb_str(b, sb.fields[i].name);
                cb(b, ";\n");
            }
            cb(b, "} ");
            mangle_type_name(b, ns, d.name);
            cb(b, ";\n\n");
            return true;
        }
        case TK_BODY_VARIANT: {
            // A variant body is a UnionType (A | B | …). Its members are NAMED type-exprs.
            tk_type_expr vt = d.body.as.variant_body.type_expr;
            if (vt.tag != TK_TEXPR_UNION)
                return fail_node(err, "codegen: a variant body must be a union of named members");
            size_t nmem = vt.as.uni.len;
            // 1) the tag enum.
            cb(b, "typedef enum ");
            cb(b, "tk_tag_"); cb_str(b, d.name);
            cb(b, " {\n");
            for (size_t i = 0; i < nmem; i += 1) {
                tk_str mn = variant_member_name(vt.as.uni.members[i]);
                if (mn.ptr == NULL)
                    return fail_node(err, "codegen: a variant member must be a named type");
                cb(b, "    TK_TAG_");
                cb_upper(b, d.name);
                cb(b, "_");
                cb_upper(b, mn);
                if (i + 1 < nmem) cb(b, ",");
                cb(b, "\n");
            }
            cb(b, "} tk_tag_"); cb_str(b, d.name); cb(b, ";\n\n");
            // 2) the value: tag + union as.
            cb(b, "typedef struct ");
            mangle_type_name(b, ns, d.name);
            cb(b, " {\n    tk_tag_"); cb_str(b, d.name); cb(b, " tag;\n    union {\n");
            for (size_t i = 0; i < nmem; i += 1) {
                tk_type_expr mem = vt.as.uni.members[i];
                tk_str mn = variant_member_name(mem);
                cb(b, "        ");
                if (!emit_type_expr(b, mem, err)) return false;
                cb(b, " ");
                cb_str(b, mn);   // the member's bare name is the union field name
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
    }
    return fail_node(err, "codegen: unknown type body not yet supported");
}

// =========================================================================
// The program -> a full C translation unit.
// =========================================================================
static tk_cstr_result cg_err(const char *m) {
    return (tk_cstr_result){ .ok = false, .as.error = tk_error_make(m) };
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
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag != TK_TITEM_TYPE_DECL) continue;
        if (!emit_type_decl(&b, it.as.type_decl, &err)) { free(b.ptr); return cg_err(err); }
    }

    // First pass: emit every top-level function. Use-decls/type-decls are handled above.
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        switch (it.tag) {
            case TK_TITEM_FUNCTION:
                if (!emit_function(&b, it.as.function, &err)) { free(b.ptr); return cg_err(err); }
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
    cb(&b, "int main(void) {\n");
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag != TK_TITEM_STATEMENT) continue;
        // The virtual-main has no variant return slot (its tail value is the int exit
        // code), so ret_type is void — emit_as never wraps for void.
        tk_type main_ret = { .tag = TK_TYPE_VOID };
        bool ok = (i == last_stmt)
            ? emit_block_tail(&b, &it.as.statement, 1, /*in_main=*/true, main_ret, "    ", &err)
            : emit_stmt(&b, &it.as.statement, /*in_main=*/true, main_ret, "    ", &err);
        if (!ok) { free(b.ptr); return cg_err(err); }
    }
    cb(&b, "    return 0;\n");
    cb(&b, "}\n");

    return (tk_cstr_result){ .ok = true, .as.value = b.ptr };
}
