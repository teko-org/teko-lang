// src/vm/vm.c   (namespace 'teko::vm')
//
// D1 — the typed-tree VM / interpreter. A tree-walking evaluator over the CHECKED
// typed tree (tk_tprogram). MIRRORS src/codegen/codegen_c.c semantics node-for-node
// (the differential-correctness anchor): same coverage frontier, same ÷0 / cast guards
// (F3), same print/println/assert builtin recognition, same VIRTUAL-MAIN return rules.
//
// LAWS: M.0 (tagged-union value model — metal, no boxing); M.1 (fail loud — every panic
// routes through the runtime's tk_panic_* so the message MATCHES the native path); M.3
// (honest — any node not yet interpreted ABORTS with a clear "vm: … not yet supported",
// never a wrong-silent value; this frontier matches codegen's).
#include "vm.h"

#include "../lexer/token.h"   // tk_token_kind operator kinds
#include "../parser/ast.h"    // tk_bind_kind, tk_bind_target, tk_path
#include "../text/text.h"     // tk_str, tk_byte

// NOTE: we do NOT #include "teko_rt.h" — it re-typedefs tk_str / tk_byte (it is a
// SELF-CONTAINED header for GENERATED programs, distinct from the compiler's text.h),
// which collides with text.h's identical-shape definitions already pulled in via tast.h.
// Instead we forward-declare exactly the runtime symbols the VM calls, against the
// compiler's tk_str. The runtime's tk_str and text.h's tk_str are bit-identical
// ({const tk_byte*, size_t}) — same ABI — so the link is sound (M.0).
//
// The F3 numeric guards (tk_div_*/tk_mod_*/tk_to_*) are STATIC INLINE in teko_rt.h, so
// they cannot be linked from a prototype; the VM re-derives those checks inline below
// (calling only the shared tk_panic_div0 / tk_panic_cast so the MESSAGES match — M.1).
void tk_print(tk_str s);
void tk_println(tk_str s);
_Noreturn void tk_panic_div0(void);
_Noreturn void tk_panic_cast(void);
_Noreturn void tk_panic_oob(void);    // "index out of bounds" (the subscript guard — W5-idx, M.1)
// string-interpolation builders — the VM concatenates pieces+holes via the SAME runtime
// symbols codegen emits, so VM==codegen byte-for-byte (incl int→decimal text). EXTERN
// (linked from teko_rt.c), not the static-inline numeric guards.
tk_str tk_str_concat(tk_str a, tk_str b);
tk_str tk_i64_to_str(int64_t v);
tk_str tk_u64_to_str(uint64_t v);
void teko__assert__is_true(bool c);
void teko__assert__is_false(bool c);
void teko__assert__str_contains(tk_str hay, tk_str needle);

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>           // malloc/realloc/free, abort
#include <stdio.h>            // fputs, stderr
#include <string.h>           // memcmp

// =========================================================================
// M.3 honest barrier. A node the VM does not yet interpret is NOT silently
// wrong — it aborts loud with a clear message. Distinct prefix ("vm:") from
// codegen's ("codegen:"), but the SAME frontier (see each call site).
// =========================================================================
_Noreturn static void vm_unsupported(const char *msg) {
    fputs("vm: ", stderr);
    fputs(msg, stderr);
    fputs("\n", stderr);
    abort();
}

// =========================================================================
// The VALUE model (M.0 — a tagged union; integers all held in a 128-bit carrier,
// signedness/width tracked alongside so the F3 guards can reproduce codegen's
// panic checks exactly; floats held in a `double` carrier + width). Bool, str (a
// {ptr,len} view) and list complete it.
//
// Tier-1 widening (TEKO_CORRECTION_PLAN §5 [N1/N2]): the integer carrier is now
// `unsigned __int128`, so width ∈ {8,16,32,64,128}; a FLOAT value kind carries a
// `double` with width ∈ {16,32,64} (the VM-debug engine approximates f16/f32 via
// `double`+width; the native path uses real _Float16/float/double — the rounding
// divergence is noted in the plan).
//
// Doctrinal correction (TEKO_CORRECTION_PLAN §4 [Z-design]): there is NO `Unit`
// value. `void` is "produces no value" — a `-> void` call is a STATEMENT,
// evaluated for effect, and the checker guarantees a void-typed expression only
// ever appears in statement position (its result is discarded). So no synthetic
// Unit tag is needed: the tagged union mirrors the legal `.tks` Value variant
// `u8 | … | i128 | f16 | f32 | f64 | bool | byte | str | []Value` (ints
// distinguished by width/sign; floats by width).
// =========================================================================
typedef struct tk_value tk_value;

typedef enum { TK_VAL_INT, TK_VAL_FLOAT, TK_VAL_BOOL, TK_VAL_STR, TK_VAL_LIST, TK_VAL_STRUCT } tk_value_tag;

// the value list — TK_LIST over tk_value (core.h convention). Declared after tk_value.
typedef struct { tk_value *ptr; size_t len; size_t cap; } tk_value_list;
// a STRUCT value's fields (W4b): parallel name/value arrays in DECLARED order (the order
// the checker fixed on the TStructInit node), so field access + later layout are stable.
// `error` is also represented as a struct value (type_name "error", one field "message").
typedef struct { tk_str *names; tk_value *vals; size_t len; } tk_value_fields;

struct tk_value {
    tk_value_tag tag;
    union {
        // INT: one 128-bit carrier + how to read it. `is_signed` picks __int128 vs
        // unsigned __int128 reading; `width` (8/16/32/64/128) is the prim's bit-width.
        // Both come from the node's resolved prim — exactly what codegen uses to select
        // tk_div_*/tk_to_*.
        struct { unsigned __int128 bits; bool is_signed; int width; } i;
        // FLOAT: a `double` carrier + the prim's width (16/32/64). f16/f32 are
        // approximated in `double` (the VM-debug engine); the native path uses the real
        // IEEE widths (rounding divergence noted in the plan).
        struct { double f; int width; } fl;
        bool          b;
        tk_str        s;
        tk_value_list list;
        // STRUCT (W4b): the nominal type name (the named type, or "error") + its fields in
        // declared order. A variant value is just a member-struct value (the case is the
        // value's type_name) — no separate wrapper; match-over-variant (W5) reads type_name.
        struct { tk_str type_name; tk_value_fields fields; } st;
    } as;
};

// v_void — the result slot of a `-> void` call (print/println/assert, or a void user
// fn). A void call PRODUCES NO VALUE: it is a statement, run for effect, and the checker
// guarantees its result is never read (void exprs appear only in statement position). This
// is NOT a Unit value — it is a never-consumed placeholder so the C `tk_value`-returning
// evaluator stays well-typed. Tagged INT/0 purely so the struct is fully initialized; the
// tag is meaningless and must not be inspected by any caller of a void-typed expression.
static tk_value v_void(void)        { return (tk_value){ .tag = TK_VAL_INT, .as.i = { 0 } }; }
static tk_value v_bool(bool x)      { return (tk_value){ .tag = TK_VAL_BOOL, .as.b = x }; }
static tk_value v_str(tk_str s)     { return (tk_value){ .tag = TK_VAL_STR, .as.s = s }; }
static tk_value v_int(unsigned __int128 bits, bool is_signed, int width) {
    return (tk_value){ .tag = TK_VAL_INT, .as.i = { .bits = bits, .is_signed = is_signed, .width = width } };
}
// v_float — a FLOAT value at the given IEEE width (16/32/64). The VM-debug engine holds
// f16/f32 in `double`; the native path uses the real width (rounding divergence noted).
static tk_value v_float(double f, int width) {
    return (tk_value){ .tag = TK_VAL_FLOAT, .as.fl = { .f = f, .width = width } };
}
// v_struct (W4b) — a struct value: nominal type name + fields (declared order).
static tk_value v_struct(tk_str type_name, tk_value_fields fields) {
    return (tk_value){ .tag = TK_VAL_STRUCT, .as.st = { .type_name = type_name, .fields = fields } };
}

// Read an INT value's signed view (sign-extended from its width, held in the 128-bit
// carrier). Used by signed ops and by signed-source cast carriers (mirrors codegen's
// "_s" carrier choice).
static __int128 v_as_i128(tk_value v) {
    return (__int128)v.as.i.bits;   // bits already hold the value in two's complement
}
static unsigned __int128 v_as_u128(tk_value v) { return v.as.i.bits; }

// =========================================================================
// prim helpers — the VM's copy of codegen_c.c's prim_is_signed / prim_width /
// cast_may_lose (verbatim semantics; this is the mirror). Integer prims only.
// =========================================================================
static bool prim_is_signed(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8: case TK_PRIM_I16: case TK_PRIM_I32:
        case TK_PRIM_I64: case TK_PRIM_I128: return true;
        default: return false;
    }
}
static int prim_width(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:   case TK_PRIM_I8:   return 8;
        case TK_PRIM_U16:  case TK_PRIM_I16:  case TK_PRIM_F16: return 16;
        case TK_PRIM_U32:  case TK_PRIM_I32:  case TK_PRIM_F32: return 32;
        case TK_PRIM_U64:  case TK_PRIM_I64:  case TK_PRIM_F64: return 64;
        case TK_PRIM_U128: case TK_PRIM_I128: return 128;
        case TK_PRIM_BOOL: return 1;
    }
    return 0;
}
static bool prim_is_float(tk_prim_kind k) {
    return k == TK_PRIM_F16 || k == TK_PRIM_F32 || k == TK_PRIM_F64;
}
// integer prims only — NOT bool, NOT float (mirrors type.h's tk_prim_is_int).
static bool prim_is_int(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:  case TK_PRIM_U16: case TK_PRIM_U32:
        case TK_PRIM_U64: case TK_PRIM_U128:
        case TK_PRIM_I8:  case TK_PRIM_I16: case TK_PRIM_I32:
        case TK_PRIM_I64: case TK_PRIM_I128:
            return true;
        default:
            return false;
    }
}

// cast src->dst may lose data (needs a runtime guard) — VERBATIM from codegen_c.c
// for the INTEGER case. (Float-involving casts are guarded separately in eval_cast.)
static bool cast_may_lose(tk_prim_kind src, tk_prim_kind dst) {
    bool ss = prim_is_signed(src), ds = prim_is_signed(dst);
    int sw = prim_width(src), dw = prim_width(dst);
    if (ss == ds)  return sw > dw;
    if (!ss && ds) return sw >= dw;
    /* ss && !ds */ return true;
}

// F3 checked div/mod — re-derives teko_rt.h's tk_div_*/tk_mod_* (which are static inline
// and thus not linkable). Same single-check, same PANIC path (tk_panic_div0 ->
// "teko: panic: division by zero"), so the message MATCHES the native path (M.1). Width
// truncation is applied by norm_int at the call site, mirroring each helper's cast.
static unsigned __int128 checked_div_u(unsigned __int128 a, unsigned __int128 b) { if (b == 0) tk_panic_div0(); return a / b; }
static unsigned __int128 checked_mod_u(unsigned __int128 a, unsigned __int128 b) { if (b == 0) tk_panic_div0(); return a % b; }
static __int128 checked_div_i(__int128 a, __int128 b) { if (b == 0) tk_panic_div0(); return a / b; }
static __int128 checked_mod_i(__int128 a, __int128 b) { if (b == 0) tk_panic_div0(); return a % b; }

// Mask an unsigned result back into its width (codegen relies on C's fixed-width
// wraparound for u8/u16/u32/u64; the VM reproduces it on the 128-bit carrier).
static unsigned __int128 mask_to_width(unsigned __int128 v, int width) {
    if (width >= 128) return v;
    return v & ((((unsigned __int128)1) << width) - 1);
}
// Sign-extend a width-bit two's-complement value held in a 128-bit carrier, then
// re-truncate to the carrier (so v_as_i128 reads it correctly).
static unsigned __int128 sext_to_width(unsigned __int128 v, int width) {
    if (width >= 128) return v;
    unsigned __int128 m = mask_to_width(v, width);
    unsigned __int128 sign = ((unsigned __int128)1) << (width - 1);
    if (m & sign) m |= ~((((unsigned __int128)1) << width) - 1);   // set high bits
    return m;
}

// Normalize a raw 128-bit arithmetic result into a width/signedness-correct INT value
// (matches C's fixed-width integer result for the node's prim).
static tk_value norm_int(unsigned __int128 raw, bool is_signed, int width) {
    unsigned __int128 bits = is_signed ? sext_to_width(raw, width) : mask_to_width(raw, width);
    return v_int(bits, is_signed, width);
}

// Pull the integer prim out of a node's resolved type (the node IS int-typed by the
// checker at every use site below). A non-prim/non-int there is an internal invariant
// break, reported honestly.
static tk_prim_kind expr_int_prim(const tk_texpr *e, const char *ctx) {
    if (e->type.tag != TK_TYPE_PRIM || !prim_is_int(e->type.as.prim)) vm_unsupported(ctx);
    return e->type.as.prim;
}

// Pull the NUMERIC prim (int OR float) out of a node's resolved type — for arithmetic
// nodes that may be either. A non-prim/non-numeric there is an internal invariant break.
static tk_prim_kind expr_num_prim(const tk_texpr *e, const char *ctx) {
    if (e->type.tag != TK_TYPE_PRIM
        || !(prim_is_int(e->type.as.prim) || prim_is_float(e->type.as.prim)))
        vm_unsupported(ctx);
    return e->type.as.prim;
}

// =========================================================================
// ENV — a simple chained frame mapping var name -> value. Mirrors the checker's
// lexical scoping: a binding adds to the current frame; assign mutates the nearest
// existing slot; function calls run in a FRESH root frame (no closure capture — M0
// has no captures, matching codegen's flat C functions).
// =========================================================================
typedef struct tk_slot { tk_str name; tk_value val; struct tk_slot *next; } tk_slot;
typedef struct { tk_slot *head; } tk_venv;

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static tk_slot *env_find(tk_venv *env, tk_str name) {
    for (tk_slot *s = env->head; s != NULL; s = s->next)
        if (name_eq(s->name, name)) return s;
    return NULL;
}
static void env_define(tk_venv *env, tk_str name, tk_value val) {
    tk_slot *s = malloc(sizeof *s);
    if (s == NULL) abort();
    s->name = name; s->val = val; s->next = env->head;
    env->head = s;
}
static void env_free(tk_venv *env) {
    tk_slot *s = env->head;
    while (s != NULL) { tk_slot *n = s->next; free(s); s = n; }
    env->head = NULL;
}

// =========================================================================
// CONTROL FLOW — exec results carry the non-local exits a block can produce.
// Mirrors codegen's C control structure: return / break / continue. A `return`
// value rides `ret`. (No exceptions; panics exit the process via tk_panic_*.)
// =========================================================================
typedef enum { TK_FLOW_NORMAL, TK_FLOW_RETURN, TK_FLOW_BREAK, TK_FLOW_CONTINUE } tk_flow_kind;
// A flow carries an optional VALUE (W5): for RETURN it's the return value; for NORMAL it's
// the statement's value (an expr-statement's result) — so a block's TRAILING expression
// becomes the block's value (B.20 — implicit return / if-as-value). BREAK/CONTINUE carry an
// optional loop LABEL (empty = innermost): a labeled break/continue bubbles through inner
// loops until the loop whose label matches catches it.
typedef struct { tk_flow_kind kind; bool has_value; tk_value value; tk_str label; } tk_flow;

// str equality (labels) — len-compared bytes, empty matches empty.
static bool vm_str_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

// The whole program (so a call expr can find a top-level function by name).
static tk_tprogram g_prog;

// forward decls
static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env);
static tk_flow  tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env);
static tk_flow  exec_if(const tk_texpr *e, tk_venv *env);     // W5 — `if` as control flow (+ value)
static tk_flow  exec_match(const tk_texpr *e, tk_venv *env);  // arm bodies are blocks (B.20) — run flow-aware

// =========================================================================
// BUILTIN call recognition — VERBATIM mirror of codegen_c.c's CALL lowering:
// `print`/`println` and `teko::assert::*` are non-shadowable builtins, recognized
// by the path's LAST segment when the path is single-segment OR rooted at `teko`.
// =========================================================================
static bool seg_is(tk_str s, const char *lit) {
    size_t n = 0; while (lit[n]) n += 1;
    return s.len == n && (n == 0 || memcmp(s.ptr, lit, n) == 0);
}

// Try the builtins; returns true and sets *out if `p` named one (and runs it).
static bool try_builtin_call(tk_path p, const tk_texpr *args, size_t nargs,
                             tk_venv *env, tk_value *out) {
    if (p.len < 1) return false;
    tk_str last = p.segments[p.len - 1].name;
    bool addressable = (p.len == 1) || seg_is(p.segments[0].name, "teko");
    if (!addressable) return false;

    // print / println — write a str arg to stdout via the runtime (SAME bytes/newline
    // as the native path: tk_print writes exactly len bytes; tk_println adds one '\n').
    if (seg_is(last, "print") || seg_is(last, "println")) {
        if (nargs != 1) vm_unsupported("print/println expects exactly one argument");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("print/println on a non-str value not yet supported");
        if (seg_is(last, "print")) tk_print(a.as.s); else tk_println(a.as.s);
        *out = v_void();   // `-> void`: run for effect, the result is never read (statement)
        return true;
    }

    // teko::assert::* — the injected testing assertions. Recognized as a `teko`-rooted
    // path whose tail is `assert::<name>` (mirrors codegen emitting teko__assert__<name>;
    // the runtime symbols PANIC with the canonical "assertion failed: <name>" — M.1).
    bool assert_ns = (p.len >= 2) && seg_is(p.segments[0].name, "teko")
                                  && seg_is(p.segments[p.len - 2].name, "assert");
    if (assert_ns) {
        if (seg_is(last, "is_true") || seg_is(last, "is_false")) {
            if (nargs != 1) vm_unsupported("teko::assert::is_true/is_false expects one argument");
            tk_value a = tk_vm_eval_expr(&args[0], env);
            if (a.tag != TK_VAL_BOOL) vm_unsupported("teko::assert::is_true/is_false on a non-bool not yet supported");
            if (seg_is(last, "is_true")) teko__assert__is_true(a.as.b);
            else                          teko__assert__is_false(a.as.b);
            *out = v_void();   // `-> void`: run for effect, the result is never read
            return true;
        }
        if (seg_is(last, "str_contains")) {
            if (nargs != 2) vm_unsupported("teko::assert::str_contains expects two arguments");
            tk_value hay = tk_vm_eval_expr(&args[0], env);
            tk_value ndl = tk_vm_eval_expr(&args[1], env);
            if (hay.tag != TK_VAL_STR || ndl.tag != TK_VAL_STR)
                vm_unsupported("teko::assert::str_contains on non-str args not yet supported");
            teko__assert__str_contains(hay.as.s, ndl.as.s);
            *out = v_void();   // `-> void`: run for effect, the result is never read
            return true;
        }
        // other assert::* (equals/is_ok/...) need generics — DEFERRED (matches the seed).
        vm_unsupported("vm: this teko::assert builtin not yet supported (needs generics)");
    }
    return false;
}

// Find a top-level user function by (single-segment) name. M0 calls are single-segment
// identifiers joined by "__" in codegen; here we match the joined path against a function
// name. Single-segment is the common case; multi-segment user calls are honest-deferred.
static const tk_tfunction *find_function(tk_path p) {
    if (p.len == 0) return NULL;
    // Match by the LAST segment — a cross-namespace call qualifies (`ns::fn`), and the seed
    // resolves names by last segment (like resolve_named for types). W5 unblocks cross-ns calls.
    tk_str name = p.segments[p.len - 1].name;
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        if (name_eq(g_prog.items[i].as.function.name, name))
            return &g_prog.items[i].as.function;
    }
    return NULL;
}

// =========================================================================
// EXPRESSIONS -> tk_value. The node coverage MIRRORS codegen_c.c's emit_expr:
//   Number, Var, Str, Byte, Binary, Unary, Compare, Cast, Call, FieldAccess.
//   IfExpr / MatchExpr as a VALUE = honest-unsupported (same frontier as codegen).
// =========================================================================
static tk_value eval_binary(const tk_texpr *e, tk_venv *env) {
    tk_token_kind op = e->as.binary.op;

    // Logical && / || — bool-typed, short-circuit (matches C's && / ||).
    if (op == TK_TOKEN_ANDAND || op == TK_TOKEN_OROR) {
        tk_value l = tk_vm_eval_expr(e->as.binary.left, env);
        if (l.tag != TK_VAL_BOOL) vm_unsupported("logical operator on a non-bool not yet supported");
        if (op == TK_TOKEN_ANDAND && !l.as.b) return v_bool(false);
        if (op == TK_TOKEN_OROR  &&  l.as.b) return v_bool(true);
        tk_value r = tk_vm_eval_expr(e->as.binary.right, env);
        if (r.tag != TK_VAL_BOOL) vm_unsupported("logical operator on a non-bool not yet supported");
        return v_bool(r.as.b);
    }

    // Numeric binary ops. The result prim (width/signedness/float-kind) is the node's
    // type — EXACTLY what codegen's prim_div_tag / fixed-width C arithmetic use.
    tk_prim_kind rp = expr_num_prim(e, "binary arithmetic on a non-numeric type not yet supported");

    tk_value lv = tk_vm_eval_expr(e->as.binary.left, env);
    tk_value rv = tk_vm_eval_expr(e->as.binary.right, env);

    // FLOAT arithmetic (f16/f32/f64): + - * / honestly in `double`; float ÷0 -> PANIC
    // (same as int, M.1). `%` / bitwise / shift on floats are honest-unsupported — the
    // checker rejects them and codegen never lowers them, so reaching here is an internal
    // invariant break, reported loud (M.3).
    if (prim_is_float(rp)) {
        int fwidth = prim_width(rp);
        if (lv.tag != TK_VAL_FLOAT || rv.tag != TK_VAL_FLOAT)
            vm_unsupported("binary arithmetic on a non-float value not yet supported");
        double a = lv.as.fl.f, b = rv.as.fl.f, res;
        switch (op) {
            case TK_TOKEN_PLUS:  res = a + b; break;
            case TK_TOKEN_MINUS: res = a - b; break;
            case TK_TOKEN_STAR:  res = a * b; break;
            case TK_TOKEN_SLASH:
                if (b == 0.0) tk_panic_div0();   // float ÷0 -> PANIC (mirrors codegen, M.1)
                res = a / b; break;
            case TK_TOKEN_PERCENT:
                vm_unsupported("'%' on a float value not supported (checker should reject)");
            case TK_TOKEN_AMP: case TK_TOKEN_PIPE: case TK_TOKEN_CARET:
            case TK_TOKEN_SHL: case TK_TOKEN_SHR:
                vm_unsupported("bitwise/shift on a float value not supported (checker should reject)");
            default: vm_unsupported("binary operator not yet supported");
        }
        return v_float(res, fwidth);
    }

    // INTEGER arithmetic. The result prim selects width/signedness.
    bool is_signed = prim_is_signed(rp);
    int  width     = prim_width(rp);
    if (lv.tag != TK_VAL_INT || rv.tag != TK_VAL_INT)
        vm_unsupported("binary arithmetic on a non-integer value not yet supported");

    // F3 (M.1): `/` and `%` route through the SAME runtime guards codegen emits, so a
    // zero divisor PANICS with the identical "division by zero" message. Width/signedness
    // select the helper, mirroring prim_div_tag.
    if (op == TK_TOKEN_SLASH || op == TK_TOKEN_PERCENT) {
        bool isdiv = (op == TK_TOKEN_SLASH);
        // Operands are already in-range for their width; the quotient/remainder of
        // in-range values is identical at any carrier width — so a 128-bit checked op +
        // norm_int width-truncation reproduces tk_div_<width>/tk_mod_<width> exactly.
        unsigned __int128 res;
        if (is_signed) {
            __int128 a = v_as_i128(lv), b = v_as_i128(rv);
            res = (unsigned __int128)(isdiv ? checked_div_i(a, b) : checked_mod_i(a, b));
        } else {
            unsigned __int128 a = v_as_u128(lv), b = v_as_u128(rv);
            res = isdiv ? checked_div_u(a, b) : checked_mod_u(a, b);
        }
        return norm_int(res, is_signed, width);
    }

    // +,-,*,&,|,^,<<,>> — plain fixed-width arithmetic (overflow guarding DEFERRED to
    // build profiles, exactly as codegen leaves +,-,* as plain C — out of scope here).
    unsigned __int128 a = v_as_u128(lv), b = v_as_u128(rv), raw;
    switch (op) {
        case TK_TOKEN_PLUS:  raw = a + b; break;
        case TK_TOKEN_MINUS: raw = a - b; break;
        case TK_TOKEN_STAR:  raw = a * b; break;
        case TK_TOKEN_AMP:   raw = a & b; break;
        case TK_TOKEN_PIPE:  raw = a | b; break;
        case TK_TOKEN_CARET: raw = a ^ b; break;
        case TK_TOKEN_SHL:   raw = a << (b & 127); break;
        case TK_TOKEN_SHR:
            if (is_signed) raw = (unsigned __int128)(v_as_i128(lv) >> (b & 127));
            else           raw = a >> (b & 127);
            break;
        default: vm_unsupported("binary operator not yet supported");
    }
    return norm_int(raw, is_signed, width);
}

static tk_value eval_unary(const tk_texpr *e, tk_venv *env) {
    tk_token_kind op = e->as.unary.op;
    tk_value x = tk_vm_eval_expr(e->as.unary.operand, env);
    switch (op) {
        case TK_TOKEN_BANG:
            if (x.tag != TK_VAL_BOOL) vm_unsupported("logical not on a non-bool not yet supported");
            return v_bool(!x.as.b);
        case TK_TOKEN_MINUS: {
            tk_prim_kind rp = expr_num_prim(e, "unary minus on a non-numeric type not yet supported");
            if (prim_is_float(rp)) {
                if (x.tag != TK_VAL_FLOAT) vm_unsupported("unary minus on a non-float value not yet supported");
                return v_float(-x.as.fl.f, prim_width(rp));
            }
            if (x.tag != TK_VAL_INT) vm_unsupported("unary minus on a non-integer value not yet supported");
            return norm_int((unsigned __int128)(- (__int128)x.as.i.bits), prim_is_signed(rp), prim_width(rp));
        }
        case TK_TOKEN_TILDE: {
            tk_prim_kind rp = expr_int_prim(e, "bitwise not on a non-integer type not yet supported");
            if (x.tag != TK_VAL_INT) vm_unsupported("bitwise not on a non-integer value not yet supported");
            return norm_int((unsigned __int128)~x.as.i.bits, prim_is_signed(rp), prim_width(rp));
        }
        default: vm_unsupported("unary operator not yet supported");
    }
}

// One adjacent comparison a <op> b — codegen lowers chains to && of these.
static bool cmp_pair(tk_value l, tk_token_kind op, tk_value r) {
    if (l.tag == TK_VAL_INT && r.tag == TK_VAL_INT) {
        bool sgn = l.as.i.is_signed;   // operands share signedness (checker-typed)
        if (sgn) {
            __int128 a = v_as_i128(l), b = v_as_i128(r);
            switch (op) {
                case TK_TOKEN_EQEQ: return a == b; case TK_TOKEN_NE: return a != b;
                case TK_TOKEN_LT:   return a <  b; case TK_TOKEN_LE: return a <= b;
                case TK_TOKEN_GT:   return a >  b; case TK_TOKEN_GE: return a >= b;
                default: vm_unsupported("comparison operator not yet supported");
            }
        } else {
            unsigned __int128 a = v_as_u128(l), b = v_as_u128(r);
            switch (op) {
                case TK_TOKEN_EQEQ: return a == b; case TK_TOKEN_NE: return a != b;
                case TK_TOKEN_LT:   return a <  b; case TK_TOKEN_LE: return a <= b;
                case TK_TOKEN_GT:   return a >  b; case TK_TOKEN_GE: return a >= b;
                default: vm_unsupported("comparison operator not yet supported");
            }
        }
    }
    if (l.tag == TK_VAL_FLOAT && r.tag == TK_VAL_FLOAT) {
        double a = l.as.fl.f, b = r.as.fl.f;   // IEEE compare (matches codegen's double ops)
        switch (op) {
            case TK_TOKEN_EQEQ: return a == b; case TK_TOKEN_NE: return a != b;
            case TK_TOKEN_LT:   return a <  b; case TK_TOKEN_LE: return a <= b;
            case TK_TOKEN_GT:   return a >  b; case TK_TOKEN_GE: return a >= b;
            default: vm_unsupported("comparison operator not yet supported");
        }
    }
    if (l.tag == TK_VAL_BOOL && r.tag == TK_VAL_BOOL) {
        switch (op) {
            case TK_TOKEN_EQEQ: return l.as.b == r.as.b;
            case TK_TOKEN_NE:   return l.as.b != r.as.b;
            default: vm_unsupported("ordered comparison on bool not yet supported");
        }
    }
    vm_unsupported("comparison on these value kinds not yet supported");
}

static tk_value eval_compare(const tk_texpr *e, tk_venv *env) {
    size_t nrest = e->as.compare.nrest;
    if (nrest == 0) return tk_vm_eval_expr(e->as.compare.first, env);   // degenerate (matches codegen)
    // chained a<b<c -> AND over adjacent pairs. Mirror codegen, but only re-evaluate
    // each operand once per its appearances; codegen emits each twice — pure exprs, so
    // value-equivalent. (Operands here are checker-pure leaf/var/number in M0.)
    const tk_texpr *prev = e->as.compare.first;
    tk_value prevv = tk_vm_eval_expr(prev, env);
    bool acc = true;
    for (size_t i = 0; i < nrest; i += 1) {
        tk_tcmp_term term = e->as.compare.rest[i];
        tk_value rv = tk_vm_eval_expr(term.operand, env);
        acc = acc && cmp_pair(prevv, term.op, rv);
        prevv = rv;
    }
    return v_bool(acc);
}

// Closed signed-range bounds [lo, hi] of a SIGNED int prim at `width` bits, on the
// 128-bit signed axis (i8..i128).
static __int128 i_min_of(int width) {
    if (width >= 128) return (__int128)1 << 127;   // INT128_MIN
    return -(((__int128)1) << (width - 1));
}
static __int128 i_max_of(int width) {
    if (width >= 128) return ~((__int128)1 << 127);   // INT128_MAX
    return (((__int128)1) << (width - 1)) - 1;
}
// Max value of an UNSIGNED int prim at `width` bits, on the 128-bit unsigned axis.
static unsigned __int128 u_max_of(int width) {
    if (width >= 128) return ~(unsigned __int128)0;   // UINT128_MAX
    return (((unsigned __int128)1) << width) - 1;
}

// The EFFECTIVE numeric prim of a cast endpoint: a PRIM is itself; a `byte` is u8
// (B.36 — byte is a u8 newtype, the same effective kind the checker's cast_kind uses).
// Returns false for any other type (str/named/…), which is the honest cast frontier.
static bool cast_prim_of(tk_type t, tk_prim_kind *out) {
    if (t.tag == TK_TYPE_PRIM) { *out = t.as.prim; return true; }
    if (t.tag == TK_TYPE_BYTE) { *out = TK_PRIM_U8; return true; }
    return false;
}

static tk_value eval_cast(const tk_texpr *e, tk_venv *env) {
    const tk_texpr *inner = e->as.cast.expr;
    tk_value iv = tk_vm_eval_expr(inner, env);

    // Both sides must be numeric (int/float, with `byte` counting as u8 — B.36). Bool<->num
    // and str casts are not in M0 — the honest frontier. Mirrors codegen's prim_both path,
    // which also lets a byte source ride the plain C cast (a tk_byte is a uint8_t).
    tk_prim_kind dst, src;
    bool both_num = cast_prim_of(e->type, &dst) && cast_prim_of(inner->type, &src)
                 && (prim_is_int(dst) || prim_is_float(dst))
                 && (prim_is_int(src) || prim_is_float(src));
    if (!both_num) {
        vm_unsupported("cast to/from a non-numeric type not yet supported");
    }
    bool sflt = prim_is_float(src), dflt = prim_is_float(dst);

    // ---- FLOAT -> FLOAT: a plain double reinterpret at the dest width (rounding noted). ----
    if (sflt && dflt) {
        if (iv.tag != TK_VAL_FLOAT) vm_unsupported("cast of a non-float value not yet supported");
        return v_float(iv.as.fl.f, prim_width(dst));
    }

    // ---- INT -> FLOAT: every in-range int is representable as a double in this VM-debug
    //      engine (the native path rounds at the real width). No guard needed. ----
    if (!sflt && dflt) {
        if (iv.tag != TK_VAL_INT) vm_unsupported("cast of a non-integer value not yet supported");
        double f = prim_is_signed(src) ? (double)v_as_i128(iv) : (double)v_as_u128(iv);
        return v_float(f, prim_width(dst));
    }

    // ---- FLOAT -> INT: TRUNCATE toward zero; NaN / ±∞ / out-of-range -> tk_panic_cast
    //      ("impossible conversion"), paritied with the integer cast guard (plan §5, M.1). ----
    if (sflt && !dflt) {
        if (iv.tag != TK_VAL_FLOAT) vm_unsupported("cast of a non-float value not yet supported");
        double f = iv.as.fl.f;
        if (f != f) tk_panic_cast();                        // NaN never converts
        double t = f < 0 ? -__builtin_floor(-f) : __builtin_floor(f);   // trunc toward zero
        bool dsigned = prim_is_signed(dst);
        int  dwidth  = prim_width(dst);
        if (dsigned) {
            // representable closed interval on the double axis
            double lo = (double)i_min_of(dwidth), hi = (double)i_max_of(dwidth);
            if (t < lo || t > hi) tk_panic_cast();          // also catches ±∞
            return norm_int((unsigned __int128)(__int128)t, true, dwidth);
        } else {
            double hi = (double)u_max_of(dwidth);
            if (t < 0 || t > hi) tk_panic_cast();           // negatives never fit; also ±∞
            return norm_int((unsigned __int128)t, false, dwidth);
        }
    }

    // ---- INT -> INT: the existing F3 guard, now on the 128-bit carrier. ----
    if (iv.tag != TK_VAL_INT) vm_unsupported("cast of a non-integer value not yet supported");

    bool dsigned = prim_is_signed(dst);
    int  dwidth  = prim_width(dst);

    if (cast_may_lose(src, dst)) {
        // Re-derive teko_rt.h's tk_to_*_s/_u range checks (static inline -> not linkable).
        // The carrier matches codegen's: signed source rides a __int128 carrier, unsigned an
        // unsigned __int128 carrier. Out of the DESTINATION range -> tk_panic_cast
        // ("impossible conversion"), identical to the native path (M.1).
        if (prim_is_signed(src)) {
            __int128 v = v_as_i128(iv);
            if (dsigned) {
                if (v < i_min_of(dwidth) || v > i_max_of(dwidth)) tk_panic_cast();
                return norm_int((unsigned __int128)v, true, dwidth);
            }
            // signed source -> unsigned dst: negatives never fit; upper bound is 2^w-1.
            if (v < 0) tk_panic_cast();
            if ((unsigned __int128)v > u_max_of(dwidth)) tk_panic_cast();
            return norm_int((unsigned __int128)v, false, dwidth);
        } else {
            unsigned __int128 v = v_as_u128(iv);
            if (dsigned) {
                // unsigned source -> signed dst: upper bound is the signed max.
                if (v > (unsigned __int128)i_max_of(dwidth)) tk_panic_cast();
                return norm_int(v, true, dwidth);
            }
            // unsigned source -> unsigned dst: upper bound 2^w-1 (u128 never narrows further).
            if (v > u_max_of(dwidth)) tk_panic_cast();
            return norm_int(v, false, dwidth);
        }
    }
    // Widening / same-type / lossless: a plain reinterpret to the target width
    // (codegen emits the bare C cast). Re-normalize into the destination prim.
    return norm_int(iv.as.i.bits, dsigned, dwidth);
}

static tk_value eval_call(const tk_texpr *e, tk_venv *env) {
    tk_path p = e->as.call.callee;
    const tk_texpr *args = e->as.call.args;
    size_t nargs = e->as.call.nargs;

    tk_value out;
    if (try_builtin_call(p, args, nargs, env, &out)) return out;

    const tk_tfunction *fn = find_function(p);
    if (fn == NULL) vm_unsupported("call to an unknown function not yet supported");
    // M0/codegen: functions take NO params (codegen fails on params). Honest frontier.
    if (fn->nparams != 0 || nargs != 0)
        vm_unsupported("function parameters not yet supported");

    // A fresh root frame — no closure capture (M0 functions are flat, like codegen's C).
    tk_venv fenv = { .head = NULL };
    tk_flow fl = tk_vm_exec_block(fn->body, fn->nbody, &fenv);
    env_free(&fenv);
    if (fl.kind == TK_FLOW_RETURN && fl.has_value) return fl.value;   // explicit `return e`
    if (fl.kind == TK_FLOW_NORMAL && fl.has_value) return fl.value;   // W5 — implicit trailing value (B.20)
    // A `-> void` fn (or one that falls off the end without a value) PRODUCES NO VALUE:
    // the call is a statement, its result discarded. Not a Unit value (TEKO_CORRECTION
    // §4 [Z-design]) — a never-consumed placeholder.
    return v_void();
}

// =========================================================================
// W5b — match execution. The typed arm keeps the SYNTACTIC pattern (literal/range bounds
// are AST literal exprs; BIND/FIELD name a case by its path's LAST segment — a variant
// value is a struct value whose type_name IS the case). Exhaustiveness is guaranteed by
// the checker; a fall-through is an honest stop (M.3). Mirrors codegen's match lowering.
// =========================================================================

// last path segment name (cases/types match by their final identifier, like find_function).
static tk_str path_last(tk_path p) { return p.len ? p.segments[p.len - 1].name : (tk_str){ NULL, 0 }; }

// value equality (literal patterns) — same-tag scalar compare.
static bool value_eq(tk_value a, tk_value b) {
    if (a.tag != b.tag) return false;
    switch (a.tag) {
        case TK_VAL_INT:   return a.as.i.bits == b.as.i.bits;   // both normalized to one width/sign
        case TK_VAL_BOOL:  return a.as.b == b.as.b;
        case TK_VAL_STR:   return name_eq(a.as.s, b.as.s);
        case TK_VAL_FLOAT: return a.as.fl.f == b.as.fl.f;
        default:           return false;                        // struct/list aren't literal-comparable
    }
}

// Mint an AST literal pattern bound as a value comparable to the subject. The checker
// guarantees the literal's type == the subject's, so the int repr borrows the subject's
// width/sign (the AST literal node carries no resolved prim).
static tk_value lit_as(const tk_expr *lit, tk_value subj) {
    switch (lit->tag) {
        case TK_EXPR_NUMBER:
            if (lit->as.number.is_float)
                return v_float(lit->as.number.fval, subj.tag == TK_VAL_FLOAT ? subj.as.fl.width : 64);
            return norm_int((unsigned __int128)lit->as.number.value,
                            subj.tag == TK_VAL_INT ? subj.as.i.is_signed : false,
                            subj.tag == TK_VAL_INT ? subj.as.i.width : 64);
        case TK_EXPR_BYTE: return v_int((uint64_t)lit->as.byte.value, false, 8);
        case TK_EXPR_STR:  return v_str(lit->as.str.text);
        default: vm_unsupported("unsupported literal in a pattern (parser emits only number/byte/str)");
    }
}

// pop env slots added since `stop` (discard a pattern's bindings between arms).
static void env_pop_to(tk_venv *env, tk_slot *stop) {
    tk_slot *s = env->head;
    while (s != stop) { tk_slot *n = s->next; free(s); s = n; }
    env->head = stop;
}

// Does `subj` match `pat`? On match, defines the pattern's bindings into `env`.
static bool pat_match(const tk_pattern *pat, tk_value subj, tk_venv *env) {
    switch (pat->tag) {
        case TK_PAT_WILDCARD: return true;
        case TK_PAT_LITERAL:  return value_eq(subj, lit_as(&pat->as.literal.value, subj));
        case TK_PAT_RANGE: {
            if (subj.tag != TK_VAL_INT) return false;
            tk_value lo = lit_as(&pat->as.range.lo, subj), hi = lit_as(&pat->as.range.hi, subj);
            if (subj.as.i.is_signed) { __int128 v = v_as_i128(subj); return v_as_i128(lo) <= v && v <= v_as_i128(hi); }
            unsigned __int128 v = v_as_u128(subj); return v_as_u128(lo) <= v && v <= v_as_u128(hi);
        }
        case TK_PAT_ALT:
            for (size_t i = 0; i < pat->as.alt.n_options; i += 1)
                if (pat_match(&pat->as.alt.options[i], subj, env)) return true;   // options bind nothing (checker)
            return false;
        case TK_PAT_BIND: {
            if (subj.tag != TK_VAL_STRUCT) return false;                          // a case value is a struct
            if (!name_eq(subj.as.st.type_name, path_last(pat->as.bind.type_name))) return false;
            if (pat->as.bind.has_binding) env_define(env, pat->as.bind.binding, subj);   // `Foo as x` binds the whole value
            return true;
        }
        case TK_PAT_FIELD: {
            if (subj.tag != TK_VAL_STRUCT) return false;
            if (!name_eq(subj.as.st.type_name, path_last(pat->as.field.type_name))) return false;
            for (size_t i = 0; i < pat->as.field.n_fields; i += 1) {              // bind each named field
                bool found = false;
                for (size_t j = 0; j < subj.as.st.fields.len; j += 1)
                    if (name_eq(subj.as.st.fields.names[j], pat->as.field.fields[i])) {
                        env_define(env, pat->as.field.fields[i], subj.as.st.fields.vals[j]); found = true; break;
                    }
                if (!found) return false;                                         // the checker guarantees the field
            }
            return true;
        }
    }
    return false;
}

// match as a VALUE (a match used as a SUB-expression): the first arm whose pattern matches AND
// whose `when` holds; run its body BLOCK; a NORMAL trailing value is the match's value. A
// `return` inside the arm CANNOT be expressed as a value here — honest stop, IDENTICAL to
// eval_expr's TK_TEXPR_IF sub-expr rule (flow-aware positions take exec_match). Mirrors
// codegen's emit_arm_value frontier.
static tk_value eval_match(const tk_texpr *e, tk_venv *env) {
    tk_value subj = tk_vm_eval_expr(e->as.match_expr.subject, env);
    for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
        const tk_tarm *arm = &e->as.match_expr.arms[i];
        tk_venv armenv = *env;   // shares the parent chain; bindings prepend to armenv.head only
        if (pat_match(&arm->pattern, subj, &armenv)) {
            bool guard_ok = !arm->has_when || tk_vm_eval_expr(arm->guard, &armenv).as.b;
            if (guard_ok) {
                tk_flow fl = tk_vm_exec_block(arm->body, arm->nbody, &armenv);
                env_pop_to(&armenv, env->head);
                if (fl.kind == TK_FLOW_NORMAL && fl.has_value) return fl.value;
                vm_unsupported("control flow inside a `match` used as a sub-expression not yet supported");
            }
        }
        env_pop_to(&armenv, env->head);   // discard this arm's bindings; try the next
    }
    vm_unsupported("non-exhaustive match at runtime (internal: the checker guarantees exhaustiveness)");
}

// W5-idx — subscript `recv[index]`. A `str` receiver yields the byte at `index`,
// BOUNDS-CHECKED: an index >= len routes through tk_panic_oob (the SAME panic the native
// path uses — M.1), else a u8 byte value (mirrors codegen's stmt-expr guard). A list value
// (slice) is an honest stop — slice VALUES can't be constructed yet (the next feature).
static tk_value eval_index(const tk_texpr *e, tk_venv *env) {
    tk_value recv = tk_vm_eval_expr(e->as.index.receiver, env);
    tk_value idx  = tk_vm_eval_expr(e->as.index.index, env);
    if (idx.tag != TK_VAL_INT) vm_unsupported("subscript index is not an integer (internal: checker should reject)");
    unsigned __int128 i = idx.as.i.bits;
    if (recv.tag == TK_VAL_STR) {
        if (i >= recv.as.s.len) tk_panic_oob();
        return v_int((uint64_t)recv.as.s.ptr[(size_t)i], false, 8);   // byte == u8
    }
    if (recv.tag == TK_VAL_LIST) vm_unsupported("slice value layer not yet implemented");
    vm_unsupported("subscript on a non-indexable value (internal: checker should reject)");
}

// $"…{expr}…" (self-host parity) — build the result str by concatenating each piece and each
// hole's value: a STR value's bytes pass through; an INT value → its decimal text via the SAME
// runtime builders codegen emits (tk_i64_to_str / tk_u64_to_str), so VM==codegen byte-for-byte
// (incl the signed/unsigned choice from the value's own carrier). The result is `pieces[0] ++
// str(holes[0]) ++ pieces[1] ++ … ++ pieces[nholes]` (npieces == nholes + 1). The checker
// already restricted hole types to str/integer, so a non-str/non-int here is an invariant break.
static tk_value eval_interp(const tk_texpr *e, tk_venv *env) {
    tk_str acc = { NULL, 0 };
    size_t np = e->as.interp.npieces, nh = e->as.interp.nholes;
    for (size_t i = 0; i < np; i += 1) {
        acc = tk_str_concat(acc, e->as.interp.pieces[i]);
        if (i < nh) {
            tk_value h = tk_vm_eval_expr(&e->as.interp.holes[i], env);
            tk_str hs;
            if (h.tag == TK_VAL_STR) {
                hs = h.as.s;
            } else if (h.tag == TK_VAL_INT) {
                hs = h.as.i.is_signed ? tk_i64_to_str((int64_t)v_as_i128(h))
                                      : tk_u64_to_str((uint64_t)v_as_u128(h));
            } else {
                vm_unsupported("interpolation hole is not a str or an integer (internal: checker should reject)");
            }
            acc = tk_str_concat(acc, hs);
        }
    }
    return v_str(acc);
}

static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env) {
    // New type tags (TEKO_CORRECTION_PLAN [Z1]). A value-position expression can never be
    // typed `void` (void is return-only, never a value — the checker rejects it elsewhere),
    // and `T?` optionals are a later wave (the parser does not emit TK_TYPE_OPTIONAL yet).
    // If either is ever reached here it is an honest stop (M.3), never a wrong-silent value.
    if (e->type.tag == TK_TYPE_OPTIONAL)
        vm_unsupported("optional value (T?) not yet supported");
    // A void CALL is the one void expression that legitimately RUNS (print/println/assert
    // or a `-> void` fn), executed for effect in statement position; eval_call returns the
    // never-consumed v_void() placeholder. Any OTHER void expr in a value position is a
    // checker failure (the checker rejects void as a value/binding/operand) — honest stop.
    if (e->type.tag == TK_TYPE_VOID && e->tag != TK_TEXPR_CALL)
        vm_unsupported("void expression has no value (internal: checker should reject in value position)");
    switch (e->tag) {
        case TK_TEXPR_NUMBER: {
            // The literal's prim comes from the node's type; bool literals don't reach here.
            if (e->type.tag != TK_TYPE_PRIM)
                vm_unsupported("number literal with a non-primitive type not yet supported");
            tk_prim_kind k = e->type.as.prim;
            // A FLOAT literal (node.is_float) carries its value in `fval`; emit a FLOAT
            // value at the prim's width (f16/f32/f64). (plan §5 [N2]: default f64.)
            if (e->as.number.is_float) {
                if (!prim_is_float(k))
                    vm_unsupported("float literal with a non-float type (internal: checker should reject)");
                return v_float(e->as.number.fval, prim_width(k));
            }
            if (k == TK_PRIM_BOOL) return v_bool(e->as.number.value != 0);
            // Integer literal: the raw __int128 value, normalized to the prim's width/sign
            // (incl 128) — exactly what codegen lowers.
            return norm_int((unsigned __int128)e->as.number.value, prim_is_signed(k), prim_width(k));
        }
        case TK_TEXPR_VAR: {
            tk_slot *s = env_find(env, e->as.var.name);
            if (s == NULL) vm_unsupported("reference to an unbound variable (internal: checker should reject)");
            return s->val;
        }
        case TK_TEXPR_STR:  return v_str(e->as.str.text);
        case TK_TEXPR_BYTE: return v_int((uint64_t)e->as.byte.value, false, 8);   // byte == u8 rep
        // bool literal (W2) — FULL support (bool already flows through the value model).
        case TK_TEXPR_BOOL: return v_bool(e->as.boolean.value);
        case TK_TEXPR_BINARY:       return eval_binary(e, env);
        case TK_TEXPR_UNARY:        return eval_unary(e, env);
        case TK_TEXPR_COMPARE:      return eval_compare(e, env);
        case TK_TEXPR_CAST:         return eval_cast(e, env);
        case TK_TEXPR_CALL:         return eval_call(e, env);
        case TK_TEXPR_FIELD_ACCESS: {   // W4b — `x.field`: eval the struct receiver, read the field by name.
            tk_value recv = tk_vm_eval_expr(e->as.field_access.receiver, env);
            // W5-idx — `.len`: a `str` value yields its byte length as a u64. A list value
            // (slice) is an honest stop (slice VALUES are the next feature). The checker
            // already typed `.len` as u64 for str/slice receivers.
            if (recv.tag == TK_VAL_STR && name_eq(e->as.field_access.field, (tk_str){ (const tk_byte *)"len", 3 }))
                return v_int((uint64_t)recv.as.s.len, false, 64);
            if (recv.tag == TK_VAL_LIST && name_eq(e->as.field_access.field, (tk_str){ (const tk_byte *)"len", 3 }))
                vm_unsupported("slice value layer not yet implemented");
            if (recv.tag != TK_VAL_STRUCT)
                vm_unsupported("field access on a non-struct value (internal: checker should reject)");
            for (size_t i = 0; i < recv.as.st.fields.len; i += 1)
                if (name_eq(recv.as.st.fields.names[i], e->as.field_access.field))
                    return recv.as.st.fields.vals[i];
            vm_unsupported("field not found in struct value (internal: the checker guarantees the field)");
        }
        case TK_TEXPR_IF: {   // W5 — `if` as a value: run it; yield the taken branch's trailing value.
            tk_flow fl = exec_if(e, env);
            if (fl.kind == TK_FLOW_NORMAL && fl.has_value) return fl.value;
            // A branch that diverges (return/break/continue) inside an `if` used as a SUB-expression
            // can't be expressed as a value here — honest stop (statement/tail position handles it).
            vm_unsupported("control flow inside an `if` used as a sub-expression not yet supported");
        }
        case TK_TEXPR_MATCH: return eval_match(e, env);   // W5b — pattern matching (literal/range/Alt/variant-case/destructure + `when`)
        case TK_TEXPR_INDEX: return eval_index(e, env);   // W5-idx — subscript recv[index] (str→byte; slice = honest stop)
        case TK_TEXPR_INTERP: return eval_interp(e, env); // $"…{expr}…" — string interpolation (pieces ++ str(holes))
        // null literal + safe-field-access (x?.field) + coalesce (x ?? y) (W2 surface) all
        // need the OPTIONAL value representation (presence+value) — a later wave (W6). Honest
        // stop now (M.3), the SAME frontier as codegen's fail_node for these nodes.
        case TK_TEXPR_NULL:
            vm_unsupported("null literal not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_SAFE_FIELD_ACCESS:
            vm_unsupported("safe field access (x?.field) not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_COALESCE:
            vm_unsupported("coalesce (x ?? y) not yet supported (optional value repr is a later wave)");
        case TK_TEXPR_STRUCT_INIT: {   // W4b — `Name { f = v, … }`: build a struct value (declared field order).
            size_t nf = e->as.struct_init.nfields;
            tk_str   *names = malloc((nf ? nf : 1) * sizeof *names); if (!names) abort();
            tk_value *vals  = malloc((nf ? nf : 1) * sizeof *vals);  if (!vals)  abort();
            for (size_t i = 0; i < nf; i += 1) {
                names[i] = e->as.struct_init.field_names[i];
                vals[i]  = tk_vm_eval_expr(&e->as.struct_init.field_vals[i], env);
            }
            tk_str tn = e->type.tag == TK_TYPE_NAMED ? e->type.as.named.name
                      : e->type.tag == TK_TYPE_ERROR ? (tk_str){ (const tk_byte *)"error", 5 }
                      : (tk_str){ NULL, 0 };
            return v_struct(tn, (tk_value_fields){ names, vals, nf });
        }
    }
    vm_unsupported("unknown expression not yet supported");
}

// =========================================================================
// STATEMENTS / BLOCKS. Coverage MIRRORS codegen_c.c's emit_stmt:
//   Binding, Assign, Return, ExprStmt, Loop, Break, Continue.
// =========================================================================
static tk_flow flow_normal(void) { return (tk_flow){ .kind = TK_FLOW_NORMAL }; }

// Evaluate an RHS that may be an `if`/`match` FLOW-AWARE: route it through exec_if/exec_match
// so a `return`/`break`/`continue` inside a branch/arm propagates out (the dominant idiom
// `let v = match r { i64 as x => x ; error as e => return e }`). For any other expr, evaluate
// plainly and wrap the value in NORMAL flow. `*out` receives the NORMAL value (valid only when
// the returned flow is NORMAL); a non-NORMAL flow must be propagated by the caller.
static tk_flow eval_rhs_flow(const tk_texpr *x, tk_venv *env, tk_value *out) {
    if (x->tag == TK_TEXPR_IF)    return exec_if(x, env);
    if (x->tag == TK_TEXPR_MATCH) return exec_match(x, env);
    *out = tk_vm_eval_expr(x, env);
    return (tk_flow){ .kind = TK_FLOW_NORMAL, .has_value = true, .value = *out };
}

static tk_flow exec_stmt(const tk_tstatement *s, tk_venv *env) {
    switch (s->tag) {
        case TK_TSTMT_BINDING: {
            tk_bind_target tgt = s->as.binding.target;
            if (tgt.tag != TK_BIND_SIMPLE)
                vm_unsupported("destructuring binding not yet supported");   // matches codegen
            // The RHS may be an `if`/`match` whose arm/branch DIVERGES (`error as e => return e`):
            // run it flow-aware so a return/break/continue propagates instead of binding.
            tk_value v;
            tk_flow fl = eval_rhs_flow(&s->as.binding.value, env, &v);
            if (fl.kind != TK_FLOW_NORMAL) return fl;            // diverged → propagate, no bind
            v = fl.has_value ? fl.value : v;                     // the NORMAL trailing value
            env_define(env, tgt.as.simple.name, v);
            return flow_normal();
        }
        case TK_TSTMT_ASSIGN: {
            tk_slot *slot = env_find(env, s->as.assign.name);
            if (slot == NULL) vm_unsupported("assignment to an unbound variable (internal: checker should reject)");
            tk_token_kind op = s->as.assign.op;
            tk_value rhs = tk_vm_eval_expr(&s->as.assign.value, env);
            if (op == TK_TOKEN_ASSIGN) { slot->val = rhs; return flow_normal(); }
            // compound assign x op= v: only meaningful on ints in M0. Reproduce the op on
            // the slot's int value at its own width/signedness.
            if (slot->val.tag != TK_VAL_INT || rhs.tag != TK_VAL_INT)
                vm_unsupported("compound assignment on a non-integer value not yet supported");
            bool sgn = slot->val.as.i.is_signed; int w = slot->val.as.i.width;
            uint64_t a = slot->val.as.i.bits, b = rhs.as.i.bits, raw;
            switch (op) {
                case TK_TOKEN_PLUSEQ:  raw = a + b; break;
                case TK_TOKEN_MINUSEQ: raw = a - b; break;
                case TK_TOKEN_STAREQ:  raw = a * b; break;
                case TK_TOKEN_AMPEQ:   raw = a & b; break;
                case TK_TOKEN_PIPEEQ:  raw = a | b; break;
                case TK_TOKEN_CARETEQ: raw = a ^ b; break;
                case TK_TOKEN_SHLEQ:   raw = a << (b & 63); break;
                case TK_TOKEN_SHREQ:   raw = sgn ? (uint64_t)((int64_t)a >> (b & 63)) : (a >> (b & 63)); break;
                case TK_TOKEN_SLASHEQ:
                case TK_TOKEN_PERCENTEQ: {
                    // checked: route through the guard so ÷0 PANICS like the native path.
                    bool isdiv = (op == TK_TOKEN_SLASHEQ);
                    if (sgn) raw = (uint64_t)(isdiv ? checked_div_i((int64_t)a,(int64_t)b)
                                                    : checked_mod_i((int64_t)a,(int64_t)b));
                    else     raw = isdiv ? checked_div_u(a,b) : checked_mod_u(a,b);
                    break;
                }
                default: vm_unsupported("assignment operator not yet supported");
            }
            slot->val = norm_int(raw, sgn, w);
            return flow_normal();
        }
        case TK_TSTMT_RETURN: {
            if (!s->as.ret.has_value) return (tk_flow){ .kind = TK_FLOW_RETURN, .has_value = false };
            // The returned value may itself be an `if`/`match` whose arm DIVERGES — run it
            // flow-aware so e.g. `return match r { … error as e => return e }` propagates.
            tk_value v;
            tk_flow fl = eval_rhs_flow(&s->as.ret.value, env, &v);
            if (fl.kind != TK_FLOW_NORMAL) return fl;            // inner return/break/continue
            v = fl.has_value ? fl.value : v;
            return (tk_flow){ .kind = TK_FLOW_RETURN, .has_value = true, .value = v };
        }
        case TK_TSTMT_EXPR: {
            // W5 — an `if`/`match` in statement/tail position runs as CONTROL FLOW, so
            // `return`/`break`/`continue` inside a branch/arm propagate AND its trailing value
            // becomes this statement's value (a block's last expression is its value — B.20).
            // Other exprs carry their value.
            const tk_texpr *x = &s->as.expr_stmt.expr;
            if (x->tag == TK_TEXPR_IF)    return exec_if(x, env);
            if (x->tag == TK_TEXPR_MATCH) return exec_match(x, env);
            tk_value v = tk_vm_eval_expr(x, env);
            return (tk_flow){ .kind = TK_FLOW_NORMAL, .has_value = true, .value = v };
        }
        case TK_TSTMT_LOOP: {
            // while(1){…}; break/continue steer it (matches codegen's loop lowering). A bare
            // break/continue (empty label) targets THIS loop; a labeled one targets THIS loop
            // iff the labels match, otherwise it bubbles up to an enclosing loop.
            tk_str mylabel = s->as.loop_stmt.label;
            for (;;) {
                tk_flow fl = tk_vm_exec_block(s->as.loop_stmt.body, s->as.loop_stmt.nbody, env);
                if (fl.kind == TK_FLOW_RETURN) return fl;
                if (fl.kind == TK_FLOW_BREAK) {
                    if (fl.label.len == 0 || vm_str_eq(fl.label, mylabel)) break;   // this loop
                    return fl;                                                      // propagate to outer
                }
                if (fl.kind == TK_FLOW_CONTINUE) {
                    if (fl.label.len == 0 || vm_str_eq(fl.label, mylabel)) continue; // next iter of this loop
                    return fl;                                                       // propagate to outer
                }
                // NORMAL: loop again.
            }
            return flow_normal();
        }
        case TK_TSTMT_BREAK:    return (tk_flow){ .kind = TK_FLOW_BREAK,    .label = s->as.jump.label };
        case TK_TSTMT_CONTINUE: return (tk_flow){ .kind = TK_FLOW_CONTINUE, .label = s->as.jump.label };
    }
    vm_unsupported("unknown statement not yet supported");
}

static tk_flow tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env) {
    tk_flow last = flow_normal();
    for (size_t i = 0; i < n; i += 1) {
        tk_flow fl = exec_stmt(&body[i], env);
        if (fl.kind != TK_FLOW_NORMAL) return fl;   // non-local exit (return/break/continue) short-circuits
        last = fl;   // W5 — keep the NORMAL value so the block's TRAILING expression is its value (B.20)
    }
    return last;
}

// W5 — run an `if` as control flow: eval the cond, run the taken branch's block, and return
// its flow. The branch's trailing value rides the NORMAL flow (the `if`'s value); a `return`/
// `break`/`continue` inside the branch propagates out. An `if`-without-`else` taken false → no value.
static tk_flow exec_if(const tk_texpr *e, tk_venv *env) {
    tk_value c = tk_vm_eval_expr(e->as.if_expr.cond, env);
    if (c.tag != TK_VAL_BOOL) vm_unsupported("if condition is not a bool (internal: checker should reject)");
    if (c.as.b) return tk_vm_exec_block(e->as.if_expr.then_blk, e->as.if_expr.nthen, env);
    if (e->as.if_expr.has_else) return tk_vm_exec_block(e->as.if_expr.else_blk, e->as.if_expr.nelse, env);
    return flow_normal();
}

// run a `match` as control flow (the FLOW form, mirroring exec_if): eval the subject, find the
// FIRST arm whose pattern matches AND whose `when` holds, run its body BLOCK and RETURN that
// flow. A `return`/`break`/`continue` inside the arm propagates out; a NORMAL trailing value
// rides the flow (the match's value). Routed from a binding/return/loose-statement RHS, exactly
// as `if` RHS routes through exec_if. (Arm bindings prepend to a private armenv, popped after.)
static tk_flow exec_match(const tk_texpr *e, tk_venv *env) {
    tk_value subj = tk_vm_eval_expr(e->as.match_expr.subject, env);
    for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
        const tk_tarm *arm = &e->as.match_expr.arms[i];
        tk_venv armenv = *env;
        if (pat_match(&arm->pattern, subj, &armenv)) {
            bool guard_ok = !arm->has_when || tk_vm_eval_expr(arm->guard, &armenv).as.b;
            if (guard_ok) {
                tk_flow fl = tk_vm_exec_block(arm->body, arm->nbody, &armenv);
                env_pop_to(&armenv, env->head);
                return fl;
            }
        }
        env_pop_to(&armenv, env->head);   // discard this arm's bindings; try the next
    }
    vm_unsupported("non-exhaustive match at runtime (internal: the checker guarantees exhaustiveness)");
}

// =========================================================================
// PUBLIC ENTRY — run the VIRTUAL-MAIN. MIRRORS codegen's main(): the loose
// top-level statements run in order; a `return n` is an early process exit with
// `(int)n`; falling off the end -> 0. Top-level functions are callable.
// =========================================================================
int tk_vm_run(tk_tprogram prog) {
    g_prog = prog;
    tk_venv env = { .head = NULL };
    int code = 0;
    tk_flow last = flow_normal();
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_STATEMENT) continue;   // functions are callable, not run
        last = exec_stmt(&prog.items[i].as.statement, &env);
        if (last.kind == TK_FLOW_RETURN) break;   // early exit, exactly like main's `return (int)(n);`
        // BREAK/CONTINUE at top level can't escape a loop — checker rejects; treat as no-op.
    }
    // Exit code from an explicit `return n` OR the virtual-main's TRAILING value (B.20 — W5).
    if (last.has_value) {
        if (last.value.tag == TK_VAL_INT)       code = (int)(int64_t)last.value.as.i.bits;
        else if (last.value.tag == TK_VAL_BOOL) code = last.value.as.b ? 1 : 0;
    }
    env_free(&env);
    return code;
}
