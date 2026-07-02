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
#include "../checker/collect.h"   // tk_type_table_of, tk_find_class_body (W10b.CLASS residual — VM reference semantics)

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
void tk_eprint(tk_str s);
void tk_eprintln(tk_str s);
_Noreturn void tk_panic_div0(void);
_Noreturn void tk_panic_cast(void);
_Noreturn void tk_panic_oob(void);    // "index out of bounds" (the subscript guard — W5-idx, M.1)
// issue #72 — the global diverging builtins `panic(str)` / `exit(<int>)` (legislator's ruling,
// no `never` type — see typer.tks:595). Both terminate the WHOLE PROCESS, matching native
// (tk_panic_str prints "teko: panic: <msg>" + backtrace then abort()s; tk_exit frees arena
// regions then calls the libc exit(code)). Declared here (not teko_rt.h — see the note above)
// so try_builtin_call can route to them exactly like print/println.
_Noreturn void tk_panic_str(tk_str msg);
_Noreturn void tk_exit(int32_t code);
// string-interpolation builders — the VM concatenates pieces+holes via the SAME runtime
// symbols codegen emits, so VM==codegen byte-for-byte (incl int→decimal text). EXTERN
// (linked from teko_rt.c), not the static-inline numeric guards.
tk_str tk_str_concat(tk_str a, tk_str b);
tk_str tk_i64_to_str(int64_t v);
tk_str tk_u64_to_str(uint64_t v);
// str/byte STDLIB builtins (Phase 3) — the SAME runtime symbols codegen emits, so the
// named-builtin calls run byte-for-byte the same in the VM as natively. EXTERN (linked
// from teko_rt.c). str_of_bytes COPIES the []byte's bytes into a fresh str; one_byte
// makes a fresh 1-byte str; ftoa is %.17g float text. (str_concat3 REMOVED 2026-07-01.)
tk_str tk_str_of_bytes(tk_str bytes);
tk_str tk_one_byte(tk_byte c);
tk_str tk_ftoa(double x);
// fmt_* — ROUND 0 format-spec helpers (teko_rt.c). Declared here so vm.c doesn't pull in teko_rt.h.
tk_str tk_fmt_f(double val, int prec);
tk_str tk_fmt_d(int64_t val, int width);
tk_str tk_fmt_x_upper(uint64_t val);
tk_str tk_fmt_x_lower(uint64_t val);
tk_str tk_fmt_e(double val, int prec);
tk_str tk_fmt_n_f(double val, int prec);
tk_str tk_fmt_n_i(int64_t val);
tk_str tk_fmt_g(double val, int prec);
tk_str tk_fmt_b(uint64_t val);
tk_str tk_fmt_p(double val, int prec);
tk_str tk_fmt_dyn_f64(double val, tk_str spec);
tk_str tk_fmt_dyn_i64(int64_t val, tk_str spec);
tk_str tk_fmt_dyn_u64(uint64_t val, tk_str spec);
double tk_float_parse(tk_str s);  // teko::float::parse(str) -> f64
uint64_t tk_f64_bits(double x);   // f64 → u64 IEEE-754 bit reinterpret (teko::f64_bits)
double   tk_f64_from_bits(uint64_t bits);  // u64 → f64 IEEE-754 bit reinterpret (teko::f64_from_bits)
// tk_str_slice is a static inline in text.h (included above); the rest link from teko_rt.c.
tk_str tk_str_slice_to(tk_str s, uint64_t end);
tk_str tk_str_slice_from(tk_str s, uint64_t start);
uint64_t tk_str_len(tk_str s);
bool tk_str_ends_with(tk_str s, tk_str suffix);
bool tk_str_contains(tk_str s, tk_str needle);
// chars/len_chars builtins (ROUND 0 UTF-8). tk_slice_char must be declared before tk_str_chars.
typedef struct { uint8_t *ptr; uint64_t len; } tk_char;  // mirrors teko_rt.h
typedef struct { tk_char *ptr; uint64_t len; } tk_slice_char;
tk_slice_char tk_str_chars(tk_str s);
uint64_t      tk_str_len_chars(tk_str s);
// ROUND 0 UTF-8 codepoint operations (linked from teko_rt.c).
tk_char  tk_char_at(tk_str s, int64_t i);
tk_str   tk_str_slice_chars(tk_str s, int64_t from, int64_t to);
bool     tk_is_alpha(tk_char c);
bool     tk_is_digit(tk_char c);
bool     tk_is_space(tk_char c);
tk_char  tk_to_lower(tk_char c);
tk_char  tk_to_upper(tk_char c);
void teko__assert__is_true(bool c);
void teko__assert__is_false(bool c);
void teko__assert__str_contains(tk_str hay, tk_str needle);

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>           // malloc/realloc/free, abort
#include <stdio.h>            // fputs, stderr
#include <string.h>           // memcmp

// D3 — test-coverage sink (linked from teko_rt.c; see teko_rt.h). Declared here, after <stdint.h>,
// because vm.c deliberately does not include teko_rt.h (it would re-typedef tk_str/tk_byte).
void     tk_cov_reset(void);
void     tk_cov_mark(uint64_t id);
uint64_t tk_cov_distinct(void);
bool     tk_cov_is_marked(uint64_t id);
void     tk_cov_branches_on(bool on);     // D3-branch — branch coverage (off by default)
void     tk_cov_branch_reset(void);
void     tk_cov_enter(uint64_t fn);
void     tk_cov_leave(void);
void     tk_cov_branch(uint32_t line, uint32_t col, uint64_t outcome);
bool     tk_cov_branch_hit(uint64_t fn, uint32_t line, uint32_t col, uint64_t outcome);
void     tk_cov_lines_on(bool on);        // D3-line — line coverage (off by default)
void     tk_cov_line_reset(void);
void     tk_cov_line(uint32_t line);
bool     tk_cov_line_hit(uint64_t fn, uint32_t line);
// teko::fs::list_dir / teko::io::read_file — host builtins. Forward-declared manually (we skip
// teko_rt.h to avoid re-typedef clashes); the ABI matches the runtime struct bit-for-bit.
typedef struct { bool ok; tk_str *ptr; uint64_t len; tk_str err; } tk_ffi_slres;
typedef struct { bool ok; tk_str value; tk_str err; } tk_ffi_sres;
typedef struct { bool ok; tk_str err; } tk_ffi_ures;
tk_ffi_slres tk_rt_list_dir(tk_str path);
tk_ffi_sres  tk_rt_read_file(tk_str path);
tk_ffi_ures  tk_rt_write_file(tk_str path, tk_str content);   // D3-branch — write the cobertura report
// C7.12: write_file_bytes(path, ptr, len) — write a raw byte slice to a file ([]byte VM path).
tk_ffi_ures  tk_rt_write_file_bytes(tk_str path, const uint8_t *ptr, uint64_t len);
tk_ffi_ures  tk_rt_mkdir(tk_str path);
// ROUND 0: str_from_utf8(ptr, len) — validated bytes -> str (or error "invalid UTF-8").
tk_ffi_sres  tk_rt_str_from_utf8(const uint8_t *ptr, uint64_t len);

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
// C1.6 — RUNTIME PANICS CARRY file:line:col. A runtime panic (÷0, bad cast,
// index-out-of-bounds) is raised AS the VM evaluates a typed node, so the node's
// {line,col} (tast.h, copied from the untyped expr at C1-POS) locates the offence.
// We print "<line>:<col>: " to stderr FIRST, then tail-call the runtime panic
// helper, which prints the CANONICAL "teko: panic: <msg>\n" (M.1 — the panic
// message stays byte-for-byte identical to the native path; we only PREFIX a
// locator the native path does not yet have). A 0 line means "unknown" (the node
// carried no position) — we then skip the locator and the panic reads as before.
//
// FILE is NOT included: the typed tree (tk_tprogram / tk_titem / tk_texpr in
// tast.h) carries no `.file`, and tk_vm_run(tk_tprogram) receives no filename, so
// the file is not reachable at a panic site WITHOUT threading a new field through
// tast.h + the checker + the public VM entry (out of this crumb's edit scope —
// vm.c/vm.tks only). File-threading is DEFERRED; we emit the honest "line:col:".
static void vm_panic_pos(uint32_t line, uint32_t col) {
    if (line == 0) return;   // no position recorded — fall through to the bare runtime panic
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%u:%u: ", (unsigned)line, (unsigned)col);
    if (n > 0) fputs(buf, stderr);
}
_Noreturn static void vm_panic_div0_at(const tk_texpr *e) { vm_panic_pos(e->line, e->col); tk_panic_div0(); }
_Noreturn static void vm_panic_cast_at(const tk_texpr *e) { vm_panic_pos(e->line, e->col); tk_panic_cast(); }
_Noreturn static void vm_panic_oob_at (const tk_texpr *e) { vm_panic_pos(e->line, e->col); tk_panic_oob();  }

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

typedef enum { TK_VAL_INT, TK_VAL_FLOAT, TK_VAL_BOOL, TK_VAL_STR, TK_VAL_LIST, TK_VAL_STRUCT, TK_VAL_OPT, TK_VAL_REF, TK_VAL_FUNC, TK_VAL_CLASS_REF } tk_value_tag;   // (W10a) TK_VAL_FUNC = a function/closure value; (W10b.CLASS residual) TK_VAL_CLASS_REF = a class instance

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
        // OPT (REBOOT_PLAN §202) — an optional `T?` value: NONE (present=false) or PRESENT
        // (present=true, `inner` heap-boxed). NONE carries inner=NULL. Distinguishable from any
        // payload value, so `null`/`?.`/`??`/match-over-`T?` all read it unambiguously.
        struct { bool present; tk_value *inner; } opt;
        // REF (MEM Step 2/3) — a `Ref<T>` value: the index into the global CELL STORE (g_cells)
        // where the aliased scalar lives. `Ref<T>` is escape-gated to a PARAM-ONLY, SCALAR-only
        // borrow (MEM Step 0), so this only ever aliases a `mut` scalar local of an ancestor frame:
        // an auto-ref PROMOTES the origin var to a cell, this carries the cell index, and `.value`
        // read/write goes through cell_get/cell_set (the .tks twin is the RefVal variant member).
        struct { uint64_t cell; } ref;
        // CLASS_REF (W10b.CLASS residual — VM reference semantics) — a class instance value: the
        // index into the SAME global cell store (g_cells) where its struct payload lives.
        // STRUCTURALLY identical to `ref` (both are just a cell index) but semantically DISTINCT:
        // TK_VAL_REF is MEM-1b's escape-gated, param-only, scalar-only borrow (may NOT be
        // returned — R3), whereas a class is a REFERENCE type (increment 3's native pointer
        // semantics) — meant to be constructed, returned, stored, and shared freely. A separate
        // tag keeps that contract distinction explicit rather than smuggling an unrestricted use
        // through TK_VAL_REF's "cannot escape" type.
        struct { uint64_t cell; } class_ref;
        // FUNC (W10) — a function/closure value. A NAMED fn carries (ns, name) and is_lambda=false. A
        // closure LITERAL carries is_lambda=true + its typed params/body + a SNAPSHOT of its captured
        // variables (cap_names/cap_vals — a RefVal snapshot shares the cell → by-ref mutation). The
        // .tks twin is FuncVal.
        struct {
            bool is_lambda; tk_str ns; tk_str name;
            const tk_tlambda_param *params; size_t nparams;
            const tk_tstatement *body; size_t nbody;
            tk_str *cap_names; tk_value *cap_vals; size_t ncaps;
        } func;
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

// =========================================================================
// E2 — the ERROR value carries compiler-diagnostic adornments. An error value is a
// STRUCT value (type_name "error"); its fields mirror tk_error (core.h): message:str,
// file:str, line:u32, col:u32, expected:str, actual:str. A message-only literal
// `error { message = … }` carries ONLY the "message" field; the other adornments default
// (empty str / 0) — set by the err_loc / err_typed builtins, read by error field access.
// We re-use the generic STRUCT carrier (parallel name/value arrays) — no new value tag.
// =========================================================================
static bool name_eq(tk_str a, tk_str b);   // fwd — defined in the ENV section (byte-compare, empty==empty)
static tk_str ERR_LIT(const char *s) {   // a static-ASCII literal as a tk_str view
    size_t n = 0; while (s[n]) n += 1;
    return (tk_str){ (const tk_byte *)s, n };
}
// Build a FRESH error value from `base`, with the named field set to `val` (overwriting if
// `base` already holds it, appending otherwise). FUNCTIONAL (copy-on-set, mirroring
// v_list_push / tk_env_define): `base` is never mutated, so err_loc(e,…) / err_typed(e,…)
// leave the source error intact (M.5 — process-lifetime buffers, leak-tolerant). This is
// the VM twin of core.h's tk_error_loc / tk_error_types (which return a modified copy).
static tk_value v_error_set(tk_value base, tk_str field, tk_value val) {
    size_t n = (base.tag == TK_VAL_STRUCT) ? base.as.st.fields.len : 0;
    // does `base` already hold `field`? then overwrite in the copy; else append one slot.
    bool present = false;
    for (size_t i = 0; i < n; i += 1)
        if (name_eq(base.as.st.fields.names[i], field)) { present = true; break; }
    size_t m = present ? n : n + 1;
    tk_str   *names = tk_alloc((m ? m : 1) * sizeof *names);
    tk_value *vals  = tk_alloc((m ? m : 1) * sizeof *vals);
    for (size_t i = 0; i < n; i += 1) {           // copy the existing fields (overwriting the target)
        names[i] = base.as.st.fields.names[i];
        vals[i]  = name_eq(base.as.st.fields.names[i], field) ? val : base.as.st.fields.vals[i];
    }
    if (!present) { names[n] = field; vals[n] = val; }   // append the new field
    return v_struct(ERR_LIT("error"), (tk_value_fields){ names, vals, m });
}
// Read an error value's field by name; if absent (the field was never set), return the
// adornment DEFAULT: empty str for the str fields, 0:u32 for line/col (mirrors tk_error's
// zero/NULL defaults — C1.3). Called by error field access so `e.line` on a message-only
// error reads 0 (never an honest stop). A field that IS present rides as stored.
static bool err_field_is_str(tk_str f) {
    return name_eq(f, ERR_LIT("message")) || name_eq(f, ERR_LIT("file"))
        || name_eq(f, ERR_LIT("expected")) || name_eq(f, ERR_LIT("actual"));
}
static tk_value v_error_field(tk_value e, tk_str field) {
    for (size_t i = 0; i < e.as.st.fields.len; i += 1)
        if (name_eq(e.as.st.fields.names[i], field)) return e.as.st.fields.vals[i];
    // unset adornment → the default: line/col are u32 0; the str fields are the empty str.
    if (name_eq(field, ERR_LIT("line")) || name_eq(field, ERR_LIT("col")))
        return v_int(0, false, 32);
    if (err_field_is_str(field)) return v_str(ERR_LIT(""));
    return v_str(ERR_LIT(""));   // any other field on an error → empty str (honest, never a crash)
}
// v_list_empty / v_list_push — the SLICE value model (teko::list::empty / push). The list is a
// {ptr,len,cap} of tk_value (TK_VAL_LIST). push is amortized O(1): when there is spare capacity
// (len < cap) we extend in-place; otherwise we allocate a fresh buffer with 2× capacity and copy.
// Teko's list usage is always linear (xs = list::push(xs, item) — the result replaces xs), so
// sharing the backing buffer with the old value is safe. Leak-tolerant (M.5 — process-lifetime).
static tk_value v_list_empty(void) {
    return (tk_value){ .tag = TK_VAL_LIST, .as.list = { .ptr = NULL, .len = 0, .cap = 0 } };
}
static tk_value v_list_push(tk_value_list base, tk_value item) {
    size_t n = base.len, cap = base.cap;
    tk_value *ptr;
    if (n < cap) {
        ptr = base.ptr;   // spare capacity — extend the existing buffer in place
    } else {
        size_t ncap = (cap < 8) ? 8 : (cap * 2);
        ptr = tk_alloc(ncap * sizeof *ptr); if (!ptr) abort();
        for (size_t i = 0; i < n; i += 1) ptr[i] = base.ptr[i];
        cap = ncap;
    }
    ptr[n] = item;
    return (tk_value){ .tag = TK_VAL_LIST, .as.list = { .ptr = ptr, .len = n + 1, .cap = cap } };
}
// v_none / v_some (REBOOT_PLAN §202) — the optional `T?` value model. NONE is the `null`
// literal; PRESENT(inner) heap-boxes the wrapped value. coerce_opt wraps a bare value into
// PRESENT when a `T?` slot expects it (the binding/return/struct-field present-wrap).
static tk_value v_none(void) { return (tk_value){ .tag = TK_VAL_OPT, .as.opt = { .present = false, .inner = NULL } }; }
static tk_value v_some(tk_value inner) {
    tk_value *p = tk_alloc(sizeof *p); if (!p) abort(); *p = inner;
    return (tk_value){ .tag = TK_VAL_OPT, .as.opt = { .present = true, .inner = p } };
}
// coerce a value into the declared slot type `t`: if `t` is `T?` and the value is NOT already
// an optional, wrap it PRESENT (the present-wrap — mirrors codegen's emit_as). A value that is
// already optional (NONE from `null`, or a PRESENT) passes through. Non-optional slots are a
// no-op. This is the VM twin of codegen's emit_as optional wrapping.
static tk_value coerce_to(tk_value v, tk_type t) {
    if (t.tag == TK_TYPE_OPTIONAL && v.tag != TK_VAL_OPT) return v_some(v);
    // SLICE slot covariance (the VM twin of codegen's emit_as slice rebuild): a `[]U` value flowing
    // into a `[]T` slot REBUILDS element-wise, coerce_to-ing each element into T. Recursion composes
    // (an OPTIONAL element → present-wrap each element; same-type element → no-op copy). A bare-null
    // element type (NULL) leaves elements untouched (no per-element type to coerce into).
    if (t.tag == TK_TYPE_SLICE && t.as.slice.element != NULL && v.tag == TK_VAL_LIST) {
        tk_value out = v_list_empty();
        for (size_t i = 0; i < v.as.list.len; i += 1)
            out = v_list_push(out.as.list, coerce_to(v.as.list.ptr[i], *t.as.slice.element));
        return out;
    }
    return v;
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

// (#50) fwd — defined after g_prog (it scans the program's type decls). The VM's NUMERIC-path
// rule for a NAMED type: a `flags`-declared name computes at its unsigned CARRIER prim; any
// other Named is the enum-ordinal u64 (E7). Mirrors vm.tks's num_prim_of.
static bool flags_carrier_prim(tk_str name, tk_prim_kind *out);
static tk_prim_kind named_num_prim(tk_str name) {
    tk_prim_kind fp;
    if (flags_carrier_prim(name, &fp)) return fp;   // (C8.3/#50) flags → its carrier prim
    return TK_PRIM_U64;                             // E7: enum ordinal — checker validated; stored as u64
}

// Pull the integer prim out of a node's resolved type (the node IS int-typed by the
// checker at every use site below). A non-prim/non-int there is an internal invariant
// break, reported honestly. (#50) A NAMED type computes via named_num_prim (a flags value
// at its carrier prim, an enum at the ordinal u64) — same rule as vm.tks's num_prim_of.
static tk_prim_kind expr_int_prim(const tk_texpr *e, const char *ctx) {
    if (e->type.tag == TK_TYPE_NAMED) return named_num_prim(e->type.as.named.name);
    if (e->type.tag != TK_TYPE_PRIM || !prim_is_int(e->type.as.prim)) vm_unsupported(ctx);
    return e->type.as.prim;
}

// Pull the NUMERIC prim (int OR float) out of a node's resolved type — for arithmetic
// nodes that may be either. A non-prim/non-numeric there is an internal invariant break.
// (#50) A NAMED type computes via named_num_prim (flags carrier / enum-ordinal u64).
static tk_prim_kind expr_num_prim(const tk_texpr *e, const char *ctx) {
    if (e->type.tag == TK_TYPE_NAMED) return named_num_prim(e->type.as.named.name);
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
// (MEM Step 2/3) `has_cell`/`cell_id` — when a slot is a `Ref<T>` aliasing TARGET (its `mut` value
// was PROMOTED to the cell store by an auto-ref at a call), the BINDING value lives at
// g_cells[cell_id], not in `val`; a cell-backed slot reads/writes through the cell so the origin var
// and every RefVal aliasing it observe the SAME storage. (The .tks twin is slot.cell_id: u64?.)
typedef struct tk_slot { tk_str name; tk_value val; bool has_cell; uint64_t cell_id; struct tk_slot *next; } tk_slot;
typedef struct { tk_slot *head; } tk_venv;

// (MEM Step 2/3) THE CELL STORE — the program-wide store of aliased scalars. A `Ref<T>` value is an
// index here. A global growable array (in-place set, O(1)), shared by pointer across frames so a
// callee's write through a ref is automatically visible at the caller (no env threading needed — the
// value-functional .tks twin threads `cells` through the env to reproduce this shared-pointer effect).
// The store only GROWS (cell_alloc appends) and MUTATES (cell_set), never shrinks/reorders.
static tk_value *g_cells = NULL;
static size_t g_cells_len = 0;
static size_t g_cells_cap = 0;
static uint64_t cell_alloc(tk_value v) {
    if (g_cells_len == g_cells_cap) {
        size_t ncap = g_cells_cap ? g_cells_cap * 2 : 8;
        tk_value *nb = tk_alloc(ncap * sizeof *nb);
        if (nb == NULL) abort();
        for (size_t i = 0; i < g_cells_len; i += 1) nb[i] = g_cells[i];
        g_cells = nb; g_cells_cap = ncap;
    }
    uint64_t id = (uint64_t)g_cells_len;
    g_cells[g_cells_len] = v;
    g_cells_len += 1;
    return id;
}
static tk_value cell_get(uint64_t id) {
    if (id >= g_cells_len) vm_unsupported("cell index out of range (internal: Ref<T> aliasing invariant break)");
    return g_cells[id];
}
static void cell_set(uint64_t id, tk_value v) {
    if (id >= g_cells_len) vm_unsupported("cell index out of range (internal: Ref<T> aliasing invariant break)");
    g_cells[id] = v;
}
static tk_value v_ref(uint64_t cell) { return (tk_value){ .tag = TK_VAL_REF, .as.ref = { .cell = cell } }; }

static bool name_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static tk_slot *env_find(tk_venv *env, tk_str name) {
    for (tk_slot *s = env->head; s != NULL; s = s->next)
        if (name_eq(s->name, name)) return s;
    return NULL;
}
static void env_define(tk_venv *env, tk_str name, tk_value val) {
    tk_slot *s = tk_alloc(sizeof *s);
    if (s == NULL) abort();
    s->name = name; s->val = val; s->has_cell = false; s->cell_id = 0; s->next = env->head;
    env->head = s;
}
static void env_free(tk_venv *env) {
    tk_slot *s = env->head;
    while (s != NULL) { tk_slot *n = s->next; tk_free0(s); s = n; }
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

// (#50) flags carrier prim — a NAMED type whose decl is a `flags` computes at the unsigned
// width native codegen chose by MEMBER COUNT (1–8 → u8, 9–16 → u16, 17–32 → u32, 33–64 → u64,
// 65–128 → u128; codegen's TK_BODY_FLAGS uint_type). True + *out iff `name` IS a flags decl.
// Mirrors vm.tks's flags_carrier_prim.
static bool flags_carrier_prim(tk_str name, tk_prim_kind *out) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl td = g_prog.items[i].as.type_decl;
        if (!name_eq(td.name, name)) continue;
        if (td.body.tag != TK_BODY_FLAGS) return false;
        size_t n = td.body.as.flags_body.n_members;
        *out = n <=  8 ? TK_PRIM_U8
             : n <= 16 ? TK_PRIM_U16
             : n <= 32 ? TK_PRIM_U32
             : n <= 64 ? TK_PRIM_U64
             :           TK_PRIM_U128;
        return true;
    }
    return false;
}

// (C7.18) Per-call-frame defer stack: a simple linked list of deferred blocks.
// Pushed as exec_stmt encounters TK_TSTMT_DEFER; drained LIFO at function exit.
// `g_vm_defer_top` points at the current call frame's stack; eval_call saves/restores it.
typedef struct tk_vm_defer_node {
    const tk_tstatement *stmt;      // the TK_TSTMT_DEFER node (carries its body)
    struct tk_vm_defer_node *next;  // older entry (toward the bottom of the stack)
} tk_vm_defer_node;
static tk_vm_defer_node *g_vm_defer_top = NULL;   // current call frame's defer stack top
// (W9.3 part 2) Count of nodes currently on g_vm_defer_top — the defer-scope mark is a DEPTH, not a
// raw pointer. A scope records the depth at its entry and drains down to that depth at exit; "already
// drained below my mark" (current depth ≤ my mark) is a safe no-op — no dangling-pointer compare.
// This lets a `return <expr>` pre-drain ALL frame defers (to depth 0) BEFORE evaluating the value
// (mirroring codegen's emit_defers(base 0) before `return <expr>`), with the unwinding exec_blocks
// then draining nothing.
static size_t g_vm_defer_depth = 0;

// forward decls
static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env);
static tk_flow  tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env);
static tk_flow  tk_vm_exec_block_ex(const tk_tstatement *body, size_t n, tk_venv *env, bool is_fn_body);
static void     vm_drain_defers_to_depth(size_t depth, tk_venv *env);  // (W9.3 part 2)
static tk_flow  exec_if(const tk_texpr *e, tk_venv *env);     // W5 — `if` as control flow (+ value)
static tk_flow  exec_match(const tk_texpr *e, tk_venv *env);  // arm bodies are blocks (B.20) — run flow-aware
static bool     call_value(const tk_texpr *e, tk_venv *env, tk_value *out);   // (MEM Step 2/3) statement-position call (Ref<T>-aware)

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

    // print / println / write — stdout output builtins.
    // ewrite / eprint / eprintln — stderr output builtins (teko::io::e*).
    // All have signature (str) -> void; the VM routes them to the same runtime symbols
    // the native backend uses, so VM==native byte-for-byte.
    if (seg_is(last, "print") || seg_is(last, "println") || seg_is(last, "write")) {
        if (nargs != 1) vm_unsupported("print/println/write expects exactly one argument");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("print/println/write on a non-str value not yet supported");
        if (seg_is(last, "println")) tk_println(a.as.s); else tk_print(a.as.s);
        *out = v_void();
        return true;
    }
    if (seg_is(last, "ewrite") || seg_is(last, "eprint") || seg_is(last, "eprintln")) {
        if (nargs != 1) vm_unsupported("ewrite/eprint/eprintln expects exactly one argument");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("ewrite/eprint/eprintln on a non-str value not yet supported");
        if (seg_is(last, "eprintln")) tk_eprintln(a.as.s); else tk_eprint(a.as.s);
        *out = v_void();
        return true;
    }

    // issue #72 — `exit(<int>)` / `panic(str)`: the injected GLOBAL diverging builtins
    // (typer.tks:595 — no `never` type; the checker recognizes them unqualified or under the
    // reserved `teko::` root, whichever namespace lookup finds nothing). Both terminate the
    // WHOLE PROCESS, exactly like native (tk_exit / tk_panic_str — teko_rt.c), so `teko run`
    // and a compiled binary agree on exit code (M.1/M.3): a success-path `exit(5)` now ends
    // the VM with status 5 instead of aborting into the "host function" honest-stop. Neither
    // call returns, so *out is never read after — set for consistency with the other builtins.
    if (seg_is(last, "exit")) {
        if (nargs != 1) vm_unsupported("exit expects exactly one argument (an integer status code)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_INT) vm_unsupported("exit's argument must be an integer status code (internal: checker should reject)");
        tk_exit((int32_t)v_as_i128(a));   // _Noreturn — frees arena regions, then libc exit(code)
        *out = v_void();
        return true;
    }
    if (seg_is(last, "panic")) {
        // The checker accepts `panic(error | str)` (typer.tks:595), but native codegen only
        // wires the `str` arm today (codegen.c:1865 emits a bare `tk_panic_str(<arg>)`, which
        // does not compile when the arg is a `tk_error` struct — a PRE-EXISTING, SEPARATE
        // codegen gap, not this issue's VM≠native divergence; see the #72 report). Mirror that
        // exact frontier here: `str` runs for real, `error` falls through to the honest stop
        // (so a program using `panic(error)` fails the SAME way on both engines — neither runs).
        if (nargs != 1) vm_unsupported("panic expects exactly one argument (an `error` or a `str`)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag == TK_VAL_STR) {
            tk_panic_str(a.as.s);   // _Noreturn — "teko: panic: <msg>" + backtrace, frees regions, abort()
        }
        vm_unsupported("panic(error) not yet supported (native codegen cannot compile it either — a separate, pre-existing gap; use panic(str))");
        *out = v_void();
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

    // teko::list::empty / push — the SLICE (collection) builtins, FIXED+COPY semantics
    // (no dynamic push; copy-append only — TEKO_CORRECTION_PLAN §16). `empty()` is the
    // sentinel/untyped empty slice; `push(xs, item)` returns a FRESH list (copy-on-push,
    // v_list_push) so `xs = teko::list::push(xs, item)` never mutates a captured `xs`.
    bool list_ns = (p.len >= 2) && seg_is(p.segments[0].name, "teko")
                                && seg_is(p.segments[p.len - 2].name, "list");
    if (list_ns) {
        if (seg_is(last, "empty")) {
            if (nargs != 0) vm_unsupported("teko::list::empty expects no arguments");
            *out = v_list_empty();
            return true;
        }
        if (seg_is(last, "push")) {
            if (nargs != 2) vm_unsupported("teko::list::push expects two arguments (the list, the item)");
            tk_value base = tk_vm_eval_expr(&args[0], env);
            tk_value item = tk_vm_eval_expr(&args[1], env);
            if (base.tag != TK_VAL_LIST) vm_unsupported("teko::list::push on a non-list value (internal: checker should reject)");
            *out = v_list_push(base.as.list, item);
            return true;
        }
        // other teko::list::* (len/get/…) are deferred — only empty/push in the fixed+copy seed.
        vm_unsupported("vm: this teko::list builtin not yet supported (only empty/push — fixed+copy)");
    }

    // E2 — err_loc / err_typed: the error-adornment builtins. Recognized by the LAST path
    // segment (single-segment OR teko-rooted — the `addressable` gate above), exactly like
    // print/println. Both PRODUCE A VALUE (a fresh error with the diagnostic fields set);
    // the source error is preserved (v_error_set is copy-on-set). These mirror C's
    // tk_error_loc(e,line,col) / tk_error_types(e,expected,actual).
    if (seg_is(last, "err_loc")) {
        if (nargs != 3) vm_unsupported("err_loc expects three arguments (the error, line, col)");
        tk_value e   = tk_vm_eval_expr(&args[0], env);
        tk_value ln  = tk_vm_eval_expr(&args[1], env);
        tk_value cl  = tk_vm_eval_expr(&args[2], env);
        if (e.tag != TK_VAL_STRUCT) vm_unsupported("err_loc on a non-error value (internal: checker should reject)");
        if (ln.tag != TK_VAL_INT || cl.tag != TK_VAL_INT) vm_unsupported("err_loc line/col must be integers (internal: checker should reject)");
        // line/col are u32 (the contract's field types) — re-normalize the int carriers to width 32.
        tk_value e1 = v_error_set(e,  ERR_LIT("line"), norm_int(ln.as.i.bits, false, 32));
        tk_value e2 = v_error_set(e1, ERR_LIT("col"),  norm_int(cl.as.i.bits, false, 32));
        *out = e2;
        return true;
    }
    if (seg_is(last, "err_typed")) {
        if (nargs != 3) vm_unsupported("err_typed expects three arguments (the error, expected, actual)");
        tk_value e   = tk_vm_eval_expr(&args[0], env);
        tk_value exp = tk_vm_eval_expr(&args[1], env);
        tk_value act = tk_vm_eval_expr(&args[2], env);
        if (e.tag != TK_VAL_STRUCT) vm_unsupported("err_typed on a non-error value (internal: checker should reject)");
        if (exp.tag != TK_VAL_STR || act.tag != TK_VAL_STR) vm_unsupported("err_typed expected/actual must be strings (internal: checker should reject)");
        tk_value e1 = v_error_set(e,  ERR_LIT("expected"), exp);
        tk_value e2 = v_error_set(e1, ERR_LIT("actual"),   act);
        *out = e2;
        return true;
    }

    // str/byte STDLIB builtins (Phase 3) — the unqualified str-returning helpers the corpus
    // calls. Recognized by the LAST path segment (single-segment OR teko-rooted — the same
    // `addressable` gate as print). Each evaluates its args and calls the SAME tk_* runtime
    // symbol codegen emits, so VM==native byte-for-byte; the result is a fresh str VALUE
    // (v_str over a runtime-owned buffer — exactly like the interp builders).
    //
    // str / str_of_bytes — ([]byte) -> str. A []byte is a TK_VAL_LIST of u8 INT byte
    // values; the runtime's tk_str_of_bytes takes a tk_str (same {ptr,len} shape as a
    // []byte, the form codegen passes directly). The VM list is NOT a contiguous byte
    // buffer (its elements are tk_value), so we first MATERIALIZE the bytes into a tk_str
    // view (alloc via the seam, M.1), then call tk_str_of_bytes (which COPIES into the
    // fresh owned result). The materialized view is a throwaway (the VM-debug engine).
    if (seg_is(last, "str") || seg_is(last, "str_of_bytes")) {
        if (nargs != 1) vm_unsupported("str/str_of_bytes expects exactly one argument (a []byte)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_LIST) vm_unsupported("str/str_of_bytes on a non-[]byte value (internal: checker should reject)");
        size_t n = a.as.list.len;
        tk_byte *buf = tk_alloc((n ? n : 1) * sizeof *buf);   // OOM-panics in the seam (M.1)
        for (size_t i = 0; i < n; i += 1) {
            tk_value el = a.as.list.ptr[i];
            if (el.tag != TK_VAL_INT) vm_unsupported("str/str_of_bytes element is not a byte (internal: checker should reject)");
            buf[i] = (tk_byte)v_as_u128(el);   // a byte == u8; take the low 8 bits of the carrier
        }
        tk_str view = { buf, n };
        *out = v_str(tk_str_of_bytes(view));   // COPIES the bytes into a fresh owned str
        return true;
    }
    // bytes_of_str — (str) -> []byte. Build a TK_VAL_LIST of u8 INT values, mirroring the
    // inverse of the str_of_bytes path above (there: list of INT → str; here: str → list of INT).
    if (seg_is(last, "bytes_of_str")) {
        if (nargs != 1) vm_unsupported("bytes_of_str expects exactly one argument (a str)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("bytes_of_str on a non-str value (internal: checker should reject)");
        tk_str s = a.as.s;
        tk_value list = v_list_empty();
        for (size_t i = 0; i < s.len; i++) {
            tk_value bv = v_int((uint64_t)s.ptr[i], false, 8);   // byte == u8 rep (width=8, unsigned)
            list = v_list_push(list.as.list, bv);
        }
        *out = list;
        return true;
    }
    // one_byte — (byte) -> str. A fresh 1-byte str holding the byte value.
    if (seg_is(last, "one_byte")) {
        if (nargs != 1) vm_unsupported("one_byte expects exactly one argument (a byte)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_INT) vm_unsupported("one_byte on a non-byte value (internal: checker should reject)");
        *out = v_str(tk_one_byte((tk_byte)v_as_u128(a)));
        return true;
    }
    // str_concat3 REMOVED (2026-07-01) — superseded by the variadic "concat" below.
    // float::parse — (str) -> f64. Parse a decimal float string to a double.
    if (seg_is(last, "parse")) {
        if (nargs != 1) vm_unsupported("float::parse expects exactly one argument (a str)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_STR) vm_unsupported("float::parse on a non-str value (internal: checker should reject)");
        *out = v_float(tk_float_parse(a.as.s), 64);
        return true;
    }
    // ftoa — (f64) -> str. The float rendered as %.17g text in a fresh owned str.
    if (seg_is(last, "ftoa")) {
        if (nargs != 1) vm_unsupported("ftoa expects exactly one argument (an f64)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_FLOAT) vm_unsupported("ftoa on a non-float value (internal: checker should reject)");
        *out = v_str(tk_ftoa(a.as.fl.f));
        return true;
    }
    // fmt_* — format helpers (ROUND 0 format spec). Each tk_fmt_* is in teko_rt.c.
    if (seg_is(last, "fmt_f")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), p = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_f(v.as.fl.f, (int)v_as_i128(p))); return true;
    }
    if (seg_is(last, "fmt_e")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), p = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_e(v.as.fl.f, (int)v_as_i128(p))); return true;
    }
    if (seg_is(last, "fmt_g")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), p = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_g(v.as.fl.f, (int)v_as_i128(p))); return true;
    }
    if (seg_is(last, "fmt_n_f")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), p = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_n_f(v.as.fl.f, (int)v_as_i128(p))); return true;
    }
    if (seg_is(last, "fmt_p")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), p = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_p(v.as.fl.f, (int)v_as_i128(p))); return true;
    }
    if (seg_is(last, "fmt_d")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), w = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_d((int64_t)v_as_i128(v), (int)v_as_i128(w))); return true;
    }
    if (seg_is(last, "fmt_x_upper")) {
        tk_value v = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_fmt_x_upper((uint64_t)v_as_u128(v))); return true;
    }
    if (seg_is(last, "fmt_x_lower")) {
        tk_value v = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_fmt_x_lower((uint64_t)v_as_u128(v))); return true;
    }
    if (seg_is(last, "fmt_b")) {
        tk_value v = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_fmt_b((uint64_t)v_as_u128(v))); return true;
    }
    if (seg_is(last, "fmt_n_i")) {
        tk_value v = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_fmt_n_i((int64_t)v_as_i128(v))); return true;
    }
    if (seg_is(last, "fmt_dyn_f64")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), s = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_dyn_f64(v.as.fl.f, s.as.s)); return true;
    }
    if (seg_is(last, "fmt_dyn_i64")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), s = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_dyn_i64((int64_t)v_as_i128(v), s.as.s)); return true;
    }
    if (seg_is(last, "fmt_dyn_u64")) {
        tk_value v = tk_vm_eval_expr(&args[0], env), s = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_fmt_dyn_u64((uint64_t)v_as_u128(v), s.as.s)); return true;
    }
    // str_concat — the internal 2-arg primitive, unchanged.
    if (seg_is(last, "str_concat")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_str_concat(a.as.s, b.as.s)); return true;
    }
    // "concat" (== the LEGISLATED string::concat, TEKO_LEGISLATION.md "`+` never concatenates")
    // is the ONE public variadic form (2026-07-01, `concat3` removed). After the checker's params
    // call-site desugar, the call always arrives here with EXACTLY ONE arg — a []str (TK_VAL_LIST
    // of TK_VAL_STR), whether packed from N pieces or passed through. Fold it via tk_str_concat.
    if (seg_is(last, "concat")) {
        tk_value pieces = tk_vm_eval_expr(&args[0], env);
        if (pieces.tag != TK_VAL_LIST) vm_unsupported("concat on a non-[]str value (internal: checker should reject)");
        tk_str acc = (tk_str){0};
        for (size_t i = 0; i < pieces.as.list.len; i += 1) {
            tk_value p = pieces.as.list.ptr[i];
            if (p.tag != TK_VAL_STR) vm_unsupported("concat element is not a str (internal: checker should reject)");
            acc = tk_str_concat(acc, p.as.s);
        }
        *out = v_str(acc); return true;
    }
    if (seg_is(last, "slice")) {
        tk_value s = tk_vm_eval_expr(&args[0], env), a = tk_vm_eval_expr(&args[1], env), b = tk_vm_eval_expr(&args[2], env);
        *out = v_str(tk_str_slice(s.as.s, (uint64_t)v_as_u128(a), (uint64_t)v_as_u128(b))); return true;
    }
    if (seg_is(last, "slice_to")) {
        tk_value s = tk_vm_eval_expr(&args[0], env), n = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_str_slice_to(s.as.s, (uint64_t)v_as_u128(n))); return true;
    }
    if (seg_is(last, "slice_from")) {
        tk_value s = tk_vm_eval_expr(&args[0], env), n = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_str_slice_from(s.as.s, (uint64_t)v_as_u128(n))); return true;
    }
    if (seg_is(last, "len")) {
        tk_value s = tk_vm_eval_expr(&args[0], env);
        *out = v_int(tk_str_len(s.as.s), false, 64); return true;
    }
    // chars — (str) -> []char. Split a str into a list of char values (each a tk_str slice into
    // the source). In the VM, a char value is a TK_VAL_STR (mirrors TK_TEXPR_CHAR → v_str).
    // Uses the same codepoint-boundary logic as tk_str_chars in the runtime.
    if (seg_is(last, "chars")) {
        if (nargs != 1) vm_unsupported("chars expects exactly one argument (a str)");
        tk_value sv = tk_vm_eval_expr(&args[0], env);
        if (sv.tag != TK_VAL_STR) vm_unsupported("chars on a non-str value (internal: checker should reject)");
        tk_str s = sv.as.s;
        tk_slice_char slc = tk_str_chars(s);
        tk_value list = v_list_empty();
        for (uint64_t i = 0; i < slc.len; i += 1) {
            tk_char ch = slc.ptr[i];
            // copy the codepoint bytes into a fresh owned str (borrowing into `s` is unsafe
            // if `s` is a transient VM buffer; a copy keeps VM memory sound).
            tk_byte *buf = tk_alloc(ch.len ? ch.len : 1);
            if (ch.len) memcpy(buf, ch.ptr, ch.len);
            list = v_list_push(list.as.list, v_str((tk_str){ buf, ch.len }));
        }
        free(slc.ptr);
        *out = list; return true;
    }
    // len_chars — (str) -> i64. Count UTF-8 codepoints without allocation.
    if (seg_is(last, "len_chars")) {
        if (nargs != 1) vm_unsupported("len_chars expects exactly one argument (a str)");
        tk_value sv = tk_vm_eval_expr(&args[0], env);
        if (sv.tag != TK_VAL_STR) vm_unsupported("len_chars on a non-str value (internal: checker should reject)");
        uint64_t n = tk_str_len_chars(sv.as.s);
        *out = v_int((unsigned __int128)n, true, 64); return true;
    }
    // --- ROUND 0: UTF-8 codepoint operations ---
    // char_at — (str, i64) -> char. In the VM, char values are TK_VAL_STR.
    if (seg_is(last, "char_at")) {
        if (nargs != 2) vm_unsupported("char_at expects exactly two arguments (str, i64)");
        tk_value sv = tk_vm_eval_expr(&args[0], env);
        tk_value iv = tk_vm_eval_expr(&args[1], env);
        if (sv.tag != TK_VAL_STR) vm_unsupported("char_at: first argument must be str");
        int64_t idx = (int64_t)(unsigned __int128)v_as_u128(iv);
        tk_char ch = tk_char_at(sv.as.s, idx);
        // copy codepoint bytes into a fresh owned str so the VM char value outlives `sv`
        tk_byte *buf = tk_alloc(ch.len ? ch.len : 1);
        if (ch.len) memcpy(buf, ch.ptr, ch.len);
        *out = v_str((tk_str){ buf, ch.len }); return true;
    }
    // str_slice_chars — (str, i64, i64) -> str. Returns a fresh owned str slice by codepoint.
    if (seg_is(last, "str_slice_chars")) {
        if (nargs != 3) vm_unsupported("str_slice_chars expects exactly three arguments (str, i64, i64)");
        tk_value sv   = tk_vm_eval_expr(&args[0], env);
        tk_value fromv = tk_vm_eval_expr(&args[1], env);
        tk_value tov   = tk_vm_eval_expr(&args[2], env);
        if (sv.tag != TK_VAL_STR) vm_unsupported("str_slice_chars: first argument must be str");
        int64_t from = (int64_t)(unsigned __int128)v_as_u128(fromv);
        int64_t to   = (int64_t)(unsigned __int128)v_as_u128(tov);
        *out = v_str(tk_str_slice_chars(sv.as.s, from, to)); return true;
    }
    // is_alpha — (char) -> bool. In the VM, char is a TK_VAL_STR; extract first byte.
    // GUARD: if the first argument is NOT a str (e.g., a byte/int from lexer's own is_alpha),
    // fall through so the user-defined function is found instead (avoids shadowing lexer.tks).
    if (seg_is(last, "is_alpha")) {
        if (nargs != 1) return false;  // wrong arity → fall through to user fn
        tk_value cv = tk_vm_eval_expr(&args[0], env);
        if (cv.tag != TK_VAL_STR) return false;  // not a char value → fall through to user fn
        tk_str cs = cv.as.s;
        tk_char ch = { (uint8_t *)cs.ptr, cs.len };
        *out = v_bool(tk_is_alpha(ch)); return true;
    }
    // is_digit — (char) -> bool. Same guard as is_alpha.
    if (seg_is(last, "is_digit")) {
        if (nargs != 1) return false;
        tk_value cv = tk_vm_eval_expr(&args[0], env);
        if (cv.tag != TK_VAL_STR) return false;  // byte/int → user fn's is_digit
        tk_str cs = cv.as.s;
        tk_char ch = { (uint8_t *)cs.ptr, cs.len };
        *out = v_bool(tk_is_digit(ch)); return true;
    }
    // is_space — (char) -> bool. Same guard.
    if (seg_is(last, "is_space")) {
        if (nargs != 1) return false;
        tk_value cv = tk_vm_eval_expr(&args[0], env);
        if (cv.tag != TK_VAL_STR) return false;
        tk_str cs = cv.as.s;
        tk_char ch = { (uint8_t *)cs.ptr, cs.len };
        *out = v_bool(tk_is_space(ch)); return true;
    }
    // to_lower — (char) -> char. Returns a fresh str so VM char value is stable. Same guard.
    if (seg_is(last, "to_lower")) {
        if (nargs != 1) return false;
        tk_value cv = tk_vm_eval_expr(&args[0], env);
        if (cv.tag != TK_VAL_STR) return false;
        tk_str cs = cv.as.s;
        tk_char ch = { (uint8_t *)cs.ptr, cs.len };
        tk_char res = tk_to_lower(ch);
        tk_byte *buf = tk_alloc(res.len ? res.len : 1);
        if (res.len) memcpy(buf, res.ptr, res.len);
        *out = v_str((tk_str){ buf, res.len }); return true;
    }
    // to_upper — (char) -> char. Same guard.
    if (seg_is(last, "to_upper")) {
        if (nargs != 1) return false;
        tk_value cv = tk_vm_eval_expr(&args[0], env);
        if (cv.tag != TK_VAL_STR) return false;
        tk_str cs = cv.as.s;
        tk_char ch = { (uint8_t *)cs.ptr, cs.len };
        tk_char res = tk_to_upper(ch);
        tk_byte *buf = tk_alloc(res.len ? res.len : 1);
        if (res.len) memcpy(buf, res.ptr, res.len);
        *out = v_str((tk_str){ buf, res.len }); return true;
    }
    if (seg_is(last, "ends_with")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_bool(tk_str_ends_with(a.as.s, b.as.s)); return true;
    }
    if (seg_is(last, "contains")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_bool(tk_str_contains(a.as.s, b.as.s)); return true;
    }
    // str_* prefixed aliases — the teko::runtime::str_* forms used by teko_rt.tks (last segment
    // is "str_eq", "str_len", etc.). These must be builtins; the Teko interpreter path returns
    // a non-bool on Windows for loop-based bool functions (str_eq, str_ends_with, str_contains).
    if (seg_is(last, "str_eq")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_bool(tk_str_eq(a.as.s, b.as.s)); return true;
    }
    if (seg_is(last, "str_len")) {
        tk_value s = tk_vm_eval_expr(&args[0], env);
        *out = v_int(tk_str_len(s.as.s), false, 64); return true;
    }
    if (seg_is(last, "str_slice")) {
        tk_value s = tk_vm_eval_expr(&args[0], env),
                 a = tk_vm_eval_expr(&args[1], env),
                 b = tk_vm_eval_expr(&args[2], env);
        *out = v_str(tk_str_slice(s.as.s, (size_t)v_as_u128(a), (size_t)v_as_u128(b))); return true;
    }
    if (seg_is(last, "str_slice_to")) {
        tk_value s = tk_vm_eval_expr(&args[0], env), n = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_str_slice_to(s.as.s, (uint64_t)v_as_u128(n))); return true;
    }
    if (seg_is(last, "str_slice_from")) {
        tk_value s = tk_vm_eval_expr(&args[0], env), n = tk_vm_eval_expr(&args[1], env);
        *out = v_str(tk_str_slice_from(s.as.s, (uint64_t)v_as_u128(n))); return true;
    }
    if (seg_is(last, "str_ends_with")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_bool(tk_str_ends_with(a.as.s, b.as.s)); return true;
    }
    if (seg_is(last, "str_contains")) {
        tk_value a = tk_vm_eval_expr(&args[0], env), b = tk_vm_eval_expr(&args[1], env);
        *out = v_bool(tk_str_contains(a.as.s, b.as.s)); return true;
    }
    if (seg_is(last, "i64_to_str")) {
        tk_value n = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_i64_to_str((int64_t)v_as_i128(n))); return true;
    }
    if (seg_is(last, "u64_to_str")) {
        tk_value n = tk_vm_eval_expr(&args[0], env);
        *out = v_str(tk_u64_to_str((uint64_t)v_as_u128(n))); return true;
    }
    // f64_bits — (f64) -> u64: reinterpret a float's IEEE-754 bits as a u64 integer.
    if (seg_is(last, "f64_bits")) {
        if (nargs != 1) vm_unsupported("f64_bits expects exactly one argument (an f64)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_FLOAT) vm_unsupported("f64_bits on a non-float value (internal: checker should reject)");
        *out = v_int((unsigned __int128)tk_f64_bits(a.as.fl.f), false, 64);
        return true;
    }
    // f64_from_bits — (u64) -> f64: reinterpret u64 bits as an IEEE-754 double.
    if (seg_is(last, "f64_from_bits")) {
        if (nargs != 1) vm_unsupported("f64_from_bits expects exactly one argument (a u64)");
        tk_value a = tk_vm_eval_expr(&args[0], env);
        if (a.tag != TK_VAL_INT) vm_unsupported("f64_from_bits on a non-integer value (internal: checker should reject)");
        *out = v_float(tk_f64_from_bits((uint64_t)v_as_u128(a)), 64);
        return true;
    }
    // teko::io::read_file — (str) -> str | error: slurp a whole file as UTF-8 (C2* host surface).
    // The last-segment "read_file" also matches driver.tks's local wrapper fn of the same name;
    // handling it here as a builtin prevents the infinite recursion that would otherwise occur
    // (wrapper calls teko::io::read_file → find_function finds the wrapper again → recurse).
    if (seg_is(last, "read_file")) {
        if (nargs != 1) vm_unsupported("read_file expects exactly one argument (a str path)");
        tk_value path_val = tk_vm_eval_expr(&args[0], env);
        if (path_val.tag != TK_VAL_STR) vm_unsupported("read_file on a non-str path (internal: checker should reject)");
        tk_ffi_sres res = tk_rt_read_file(path_val.as.s);
        if (!res.ok) {
            tk_value base = v_struct(ERR_LIT("error"), (tk_value_fields){ NULL, NULL, 0 });
            *out = v_error_set(base, ERR_LIT("message"), v_str(res.err));
            return true;
        }
        *out = v_str(res.value);
        return true;
    }
    // teko::fs::list_dir — (str) -> []str | error: directory entries (C2* host surface in VM).
    // Returns a list of entry-name strings on success, or an error struct on failure.
    if (seg_is(last, "list_dir")) {
        if (nargs != 1) vm_unsupported("list_dir expects exactly one argument (a str path)");
        tk_value path_val = tk_vm_eval_expr(&args[0], env);
        if (path_val.tag != TK_VAL_STR) vm_unsupported("list_dir on a non-str path (internal: checker should reject)");
        tk_ffi_slres res = tk_rt_list_dir(path_val.as.s);
        if (!res.ok) {
            tk_value base = v_struct(ERR_LIT("error"), (tk_value_fields){ NULL, NULL, 0 });
            *out = v_error_set(base, ERR_LIT("message"), v_str(res.err));
            return true;
        }
        tk_value list = v_list_empty();
        for (uint64_t i = 0; i < res.len; i += 1)
            list = v_list_push(list.as.list, v_str(res.ptr[i]));
        *out = list;
        return true;
    }
    // C7.12: write_file_bytes(path: str, data: []byte) -> error? — write raw bytes to a file.
    // The VM represents []byte as a TK_VAL_LIST where each element is TK_VAL_INT (u8 bits).
    // We build a contiguous byte buffer, call the runtime directly, and return null (= success)
    // or an error struct. The error? result is TK_VAL_OPT: present=false (null) or present=true.
    if (seg_is(last, "write_file_bytes")) {
        if (nargs != 2) vm_unsupported("write_file_bytes expects exactly two arguments (str path, []byte data)");
        tk_value path_val = tk_vm_eval_expr(&args[0], env);
        tk_value data_val = tk_vm_eval_expr(&args[1], env);
        if (path_val.tag != TK_VAL_STR) vm_unsupported("write_file_bytes: path is not a str (internal: checker should reject)");
        // An empty []byte may arrive as an empty TK_VAL_LIST; normalise either case.
        if (data_val.tag != TK_VAL_LIST) {
            if (data_val.tag == TK_VAL_OPT && !data_val.as.opt.present) { data_val = v_list_empty(); }
            else vm_unsupported("write_file_bytes: data is not a []byte list (internal: checker should reject)");
        }
        // Build a heap buffer from the VM's integer-element byte list.
        tk_value_list lst = data_val.as.list;
        uint8_t *buf = NULL;
        if (lst.len > 0) {
            buf = (uint8_t *)tk_alloc(lst.len); if (!buf) abort();
            for (uint64_t i = 0; i < lst.len; i += 1) {
                tk_value elem = lst.ptr[i];
                if (elem.tag != TK_VAL_INT) vm_unsupported("write_file_bytes: []byte element is not an integer (internal: checker should reject)");
                buf[i] = (uint8_t)(elem.as.i.bits & 0xFF);
            }
        }
        tk_ffi_ures res = tk_rt_write_file_bytes(path_val.as.s, buf, (uint64_t)lst.len);
        if (buf) tk_free0(buf);
        if (!res.ok) {
            tk_value base = v_struct(ERR_LIT("error"), (tk_value_fields){ NULL, NULL, 0 });
            *out = v_error_set(base, ERR_LIT("message"), v_str(res.err));
            return true;
        }
        *out = v_none();   // error? success = null (TK_VAL_OPT present=false)
        return true;
    }
    // ROUND 0: str_from_utf8(bytes: []byte) -> str | error — the validated bytes -> str door
    // (B.36). The VM represents []byte as a TK_VAL_LIST of TK_VAL_INT (u8 bits); build a
    // contiguous byte buffer (same approach as write_file_bytes) and call the runtime validator
    // directly, mirroring the codegen sres_byteslice lift.
    if (seg_is(last, "str_from_utf8")) {
        if (nargs != 1) vm_unsupported("str_from_utf8 expects exactly one argument (a []byte)");
        tk_value data_val = tk_vm_eval_expr(&args[0], env);
        if (data_val.tag != TK_VAL_LIST) {
            if (data_val.tag == TK_VAL_OPT && !data_val.as.opt.present) { data_val = v_list_empty(); }
            else vm_unsupported("str_from_utf8: argument is not a []byte list (internal: checker should reject)");
        }
        tk_value_list lst = data_val.as.list;
        uint8_t *buf = NULL;
        if (lst.len > 0) {
            buf = (uint8_t *)tk_alloc(lst.len); if (!buf) abort();
            for (uint64_t i = 0; i < lst.len; i += 1) {
                tk_value elem = lst.ptr[i];
                if (elem.tag != TK_VAL_INT) vm_unsupported("str_from_utf8: []byte element is not an integer (internal: checker should reject)");
                buf[i] = (uint8_t)(elem.as.i.bits & 0xFF);
            }
        }
        tk_ffi_sres res = tk_rt_str_from_utf8(buf, (uint64_t)lst.len);
        if (buf) tk_free0(buf);
        if (!res.ok) {
            tk_value base = v_struct(ERR_LIT("error"), (tk_value_fields){ NULL, NULL, 0 });
            *out = v_error_set(base, ERR_LIT("message"), v_str(res.err));
            return true;
        }
        *out = v_str(res.value);
        return true;
    }
    return false;
}

// Find a top-level user function by (single-segment) name. Returns the function pointer and
// sets *out_idx to the g_prog.items index (a globally unique coverage ID). M0 calls are
// single-segment identifiers joined by "__" in codegen; here we match the joined path against
// a function name. Single-segment is the common case; multi-segment user calls are honest-deferred.
//
// BUGFIX (VM name-resolution / cross-namespace collision): a bare last-segment match, with NO
// namespace disambiguation, silently resolves to the WRONG function whenever two DIFFERENT
// namespaces declare a same-named fn (e.g. two modules each define their own `parse_atom`,
// with different arities/signatures) — the FIRST same-named fn anywhere in program-item order
// wins, corrupting the call regardless of which module the caller actually meant. This bled
// into completely unrelated tests once a source file introducing such a collision was added to
// the corpus. The checker ALREADY resolves the correct target namespace per call-site (TCall's
// call_ns, tast.h/tast.tks) — this now HONORS it: when `call_ns` is non-empty, prefer an exact
// (namespace, name) match; only fall back to the old last-segment-anywhere scan when call_ns is
// empty (a local/unqualified/builtin call has no namespace to disambiguate) or no such
// namespaced function exists (defensive — never turns a previously-resolvable call into a
// failure). Mirrors vm.tks::find_function (SUPREME RULE — both engines fixed together).
static const tk_tfunction *find_function_ns(tk_path p, tk_str call_ns, size_t *out_idx) {
    if (p.len == 0) return NULL;
    // Match by the LAST segment — a cross-namespace call qualifies (`ns::fn`), and the seed
    // resolves names by last segment (like resolve_named for types). W5 unblocks cross-ns calls.
    tk_str name = p.segments[p.len - 1].name;
    if (call_ns.len != 0) {
        for (size_t i = 0; i < g_prog.nitems; i += 1) {
            if (g_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
            const tk_tfunction *f = &g_prog.items[i].as.function;
            if (name_eq(f->namespace, call_ns) && name_eq(f->name, name)) {
                if (out_idx) *out_idx = i;
                return f;
            }
        }
    }
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        if (name_eq(g_prog.items[i].as.function.name, name)) {
            if (out_idx) *out_idx = i;
            return &g_prog.items[i].as.function;
        }
    }
    return NULL;
}
// find_function — the no-namespace-hint convenience form (call_ns unknown/inapplicable): behaves
// exactly like the OLD last-segment-anywhere scan. Kept as a thin wrapper so call sites that
// genuinely have no resolved namespace (none today, but a safe default) stay honest.
static const tk_tfunction *find_function(tk_path p, size_t *out_idx) {
    return find_function_ns(p, (tk_str){0}, out_idx);
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
                if (b == 0.0) vm_panic_div0_at(e);   // float ÷0 -> PANIC, positioned at this node (C1.6; mirrors codegen, M.1)
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
        // C1.6 — POSITION the ÷0 panic at THIS node before the runtime helper fires (the
        // checked_* helpers still call tk_panic_div0 as the canonical message — M.1).
        unsigned __int128 res;
        if (is_signed) {
            __int128 a = v_as_i128(lv), b = v_as_i128(rv);
            if (b == 0) vm_panic_div0_at(e);
            res = (unsigned __int128)(isdiv ? checked_div_i(a, b) : checked_mod_i(a, b));
        } else {
            unsigned __int128 a = v_as_u128(lv), b = v_as_u128(rv);
            if (b == 0) vm_panic_div0_at(e);
            res = isdiv ? checked_div_u(a, b) : checked_mod_u(a, b);
        }
        return norm_int(res, is_signed, width);
    }

    // +,-,*,&,|,^,<<,>> — plain fixed-width arithmetic (overflow guarding DEFERRED to
    // build profiles, exactly as codegen leaves +,-,* as plain C — out of scope here).
    // #49 — shift counts are masked by the LEFT OPERAND's bit-width minus 1 (C#/Java
    // semantics), NOT a fixed 127: `(1 as i32) << 40` masks to `<< 8` (width=32 -> &31),
    // matching codegen's tk_shl_*/tk_shr_* runtime helpers exactly. In-range counts are
    // unaffected (mask is a no-op when count < width).
    unsigned __int128 a = v_as_u128(lv), b = v_as_u128(rv), raw;
    unsigned shift_mask = (unsigned)(width - 1);
    switch (op) {
        case TK_TOKEN_PLUS:  raw = a + b; break;
        case TK_TOKEN_MINUS: raw = a - b; break;
        case TK_TOKEN_STAR:  raw = a * b; break;
        case TK_TOKEN_AMP:   raw = a & b; break;
        case TK_TOKEN_PIPE:  raw = a | b; break;
        case TK_TOKEN_CARET: raw = a ^ b; break;
        case TK_TOKEN_SHL:   raw = a << (b & shift_mask); break;
        case TK_TOKEN_SHR:
            if (is_signed) raw = (unsigned __int128)(v_as_i128(lv) >> (b & shift_mask));
            else           raw = a >> (b & shift_mask);
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
    if (l.tag == TK_VAL_STR && r.tag == TK_VAL_STR) {   // byte-equality (the lexer/parser tests compare text)
        bool eq = name_eq(l.as.s, r.as.s);
        switch (op) {
            case TK_TOKEN_EQEQ: return eq;
            case TK_TOKEN_NE:   return !eq;
            default: vm_unsupported("ordered comparison on str not supported");
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
    if (width >= 128) return (__int128)INT64_MIN * ((__int128)1 << 64);   // -2^127
    return -(((__int128)1) << (width - 1));
}
static __int128 i_max_of(int width) {
    if (width >= 128) return ~((__int128)INT64_MIN * ((__int128)1 << 64));   // 2^127-1
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
    // E7: enum→int/byte — checker already validated; enum ordinals are stored as u64 in the VM.
    // (#50) a flags-typed Named computes at its carrier prim (same rule as expr_num_prim).
    if (t.tag == TK_TYPE_NAMED) { *out = named_num_prim(t.as.named.name); return true; }
    return false;
}

static tk_value eval_cast(const tk_texpr *e, tk_venv *env) {
    const tk_texpr *inner = e->as.cast.expr;
    tk_value iv = tk_vm_eval_expr(inner, env);

    // (W10b.CLASS residual — VM reference semantics) the base-binding's synthetic `self to Base`
    // cast (typer.c::type_method, the `class Base(binding) { … }` feature) is a same-storage
    // REINTERPRET, not a numeric conversion — mirrors the native engine's plain C pointer cast
    // (`(tk_t_Base *)(self)`), which is a no-op at the value level (field-flattening already lays
    // the base's fields out as a PREFIX of the derived struct, so field-by-NAME lookup on the SAME
    // cell already resolves correctly under either type). Any Named-to-Named cast is this identity
    // pass-through — the checker only ever builds one for this exact feature.
    if (inner->type.tag == TK_TYPE_NAMED && e->type.tag == TK_TYPE_NAMED) return iv;

    // (UTF-8 increment 1) `char to u32`/u64/i64 — DECODE the codepoint (its UTF-8 bytes, carried as
    // the str value) to its scalar value, normalized to the target int. Mirrors tk_char_to_u32 (the
    // native runtime helper) and codegen's char-cast branch (VM==native). The checker restricted the
    // target to u32/u64/i64, so the decoded codepoint always fits.
    if (inner->type.tag == TK_TYPE_CHAR) {
        if (iv.tag != TK_VAL_STR) vm_unsupported("cast of a non-char value not yet supported");
        tk_str s = iv.as.s;
        uint32_t cp = 0;
        if (s.len != 0) {
            uint8_t b0 = s.ptr[0];
            if (b0 <= 0x7F)        cp = b0;
            else if (s.len == 2)   cp = ((uint32_t)(b0 & 0x1F) << 6)  |  (uint32_t)(s.ptr[1] & 0x3F);
            else if (s.len == 3)   cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(s.ptr[1] & 0x3F) << 6)
                                                                      |  (uint32_t)(s.ptr[2] & 0x3F);
            else                   cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(s.ptr[1] & 0x3F) << 12)
                                                                      | ((uint32_t)(s.ptr[2] & 0x3F) << 6)
                                                                      |  (uint32_t)(s.ptr[3] & 0x3F);
        }
        tk_prim_kind ctgt;
        if (!cast_prim_of(e->type, &ctgt)) vm_unsupported("char cast to a non-numeric target (internal)");
        return norm_int((unsigned __int128)cp, prim_is_signed(ctgt), prim_width(ctgt));
    }

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
        if (f != f) vm_panic_cast_at(e);                    // NaN never converts (C1.6 — positioned)
        double t = f < 0 ? -__builtin_floor(-f) : __builtin_floor(f);   // trunc toward zero
        bool dsigned = prim_is_signed(dst);
        int  dwidth  = prim_width(dst);
        if (dsigned) {
            // representable closed interval on the double axis
            double lo = (double)i_min_of(dwidth), hi = (double)i_max_of(dwidth);
            if (t < lo || t > hi) vm_panic_cast_at(e);      // also catches ±∞ (C1.6 — positioned)
            return norm_int((unsigned __int128)(__int128)t, true, dwidth);
        } else {
            double hi = (double)u_max_of(dwidth);
            if (t < 0 || t > hi) vm_panic_cast_at(e);       // negatives never fit; also ±∞ (C1.6 — positioned)
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
                if (v < i_min_of(dwidth) || v > i_max_of(dwidth)) vm_panic_cast_at(e);   // C1.6 — positioned
                return norm_int((unsigned __int128)v, true, dwidth);
            }
            // signed source -> unsigned dst: negatives never fit; upper bound is 2^w-1.
            if (v < 0) vm_panic_cast_at(e);                                  // C1.6 — positioned
            if ((unsigned __int128)v > u_max_of(dwidth)) vm_panic_cast_at(e);   // C1.6 — positioned
            return norm_int((unsigned __int128)v, false, dwidth);
        } else {
            unsigned __int128 v = v_as_u128(iv);
            if (dsigned) {
                // unsigned source -> signed dst: upper bound is the signed max.
                if (v > (unsigned __int128)i_max_of(dwidth)) vm_panic_cast_at(e);   // C1.6 — positioned
                return norm_int(v, true, dwidth);
            }
            // unsigned source -> unsigned dst: upper bound 2^w-1 (u128 never narrows further).
            if (v > u_max_of(dwidth)) vm_panic_cast_at(e);   // C1.6 — positioned
            return norm_int(v, false, dwidth);
        }
    }
    // Widening / same-type / lossless: a plain reinterpret to the target width
    // (codegen emits the bare C cast). Re-normalize into the destination prim.
    return norm_int(iv.as.i.bits, dsigned, dwidth);
}

// (MEM Step 2/3) param_is_ref — does parameter `i` of `fn` have a `Ref<T>` annotation (last path
// segment "Ref")? The same surface-syntax detection codegen and the escape-gate use. fn_has_ref_param
// folds it over all params. (Mirrors vm.tks param_is_ref / fn_has_ref_param.)
static bool param_is_ref(const tk_tfunction *fn, size_t i) {
    tk_type_expr ta = fn->params[i].type_ann;
    return ta.tag == TK_TEXPR_NAMED && ta.as.named.path.len > 0
        && seg_is(ta.as.named.path.segments[ta.as.named.path.len - 1].name, "Ref");
}
static bool fn_has_ref_param(const tk_tfunction *fn) {
    for (size_t i = 0; i < fn->nparams; i += 1) if (param_is_ref(fn, i)) return true;
    return false;
}

// param_coerce_type — the coerce-slot type for a call PARAMETER, built from its declared TypeExpr (the
// param twin of field_coerce_type, but RECURSIVE so a `[]T?` param rebuilds its elements). An
// OptionalType → TK_TYPE_OPTIONAL (coerce_to PRESENT-wraps a bare `T`); a SliceType → TK_TYPE_SLICE
// whose element is the recursively-built coerce type (coerce_to rebuilds element-wise so a `[]T?`
// present-wraps each element); every other shape → TK_TYPE_VOID (coerce_to's no-op). The inner/element
// is heap-allocated (leak-tolerant — M.5 process-lifetime). Mirrors vm.tks param_coerce_type.
static tk_type param_coerce_type(tk_type_expr ann) {
    if (ann.tag == TK_TEXPR_OPTIONAL)
        return (tk_type){ .tag = TK_TYPE_OPTIONAL, .as.optional = { NULL } };
    if (ann.tag == TK_TEXPR_SLICE && ann.as.slice.element != NULL) {
        tk_type elt = param_coerce_type(*ann.as.slice.element);
        if (elt.tag == TK_TYPE_VOID) return (tk_type){ .tag = TK_TYPE_VOID };   // []T with no element wrap → no-op
        tk_type *el = tk_alloc(sizeof *el); if (!el) abort();
        *el = elt;
        return (tk_type){ .tag = TK_TYPE_SLICE, .as.slice = { el } };
    }
    return (tk_type){ .tag = TK_TYPE_VOID };
}

// (MEM Step 2/3) bind_call_args — bind `fn`'s parameters to the call's arguments in `fenv`. For a
// `Ref<T>` parameter the checker guarantees the argument is either a `mut` scalar lvalue (a TVar —
// AUTO-REF: promote it to a cell, pass v_ref(cell)) or a value already of reference type (FORWARD:
// a RefVal from a ref param read — pass it through). Promotion sets the CALLER slot's has_cell so its
// future reads/writes route through the cell too (the cell store is global, shared by pointer — so a
// callee write through the ref is automatically visible at the caller). Mirrors vm.tks bind_call_args.
static void bind_call_args(const tk_tfunction *fn, const tk_texpr *args, size_t nargs, tk_venv *env, tk_venv *fenv) {
    for (size_t i = 0; i < fn->nparams && i < nargs; i += 1) {
        if (param_is_ref(fn, i)) {
            if (args[i].tag != TK_TEXPR_VAR)
                vm_unsupported("a `Ref<T>` argument must be a mutable variable (internal: checker should reject)");
            // A Reference-typed arg is a forwarded ref param (pass its RefVal through); any other
            // type is an auto-ref of a `mut` lvalue (promote the origin var to a cell).
            if (args[i].type.tag == TK_TYPE_REF) {
                tk_value fwd = tk_vm_eval_expr(&args[i], env);   // reads the existing RefVal (cell-backed or RefVal slot)
                env_define(fenv, fn->params[i].name, fwd);
            } else {
                tk_slot *s = env_find(env, args[i].as.var.name);
                if (s == NULL) vm_unsupported("auto-ref of an unbound variable (internal: checker should reject)");
                if (!s->has_cell) { s->cell_id = cell_alloc(s->val); s->has_cell = true; }  // promote: origin reads now go through the cell
                env_define(fenv, fn->params[i].name, v_ref(s->cell_id));
            }
        } else {
            tk_value av = tk_vm_eval_expr(&args[i], env);
            // Present-wrap a bare `T` arg into a `T?` param, and rebuild a `[]U` arg into a `[]T` param
            // element-wise (uniform with field/return — the VM twin of codegen's call-arg emit_as).
            // coerce_to no-ops on an already-optional value (no double-wrap), on a same-type element,
            // and on every non-wrapping param. (Mirrors param_coerce_type built from the type_ann.)
            tk_type pcoerce = param_coerce_type(fn->params[i].type_ann);
            env_define(fenv, fn->params[i].name, coerce_to(av, pcoerce));
        }
    }
}

// run_user_fn — the shared callee-frame run (cov, defer save/restore, body exec, defer drain, free).
// Returns the callee's resulting flow (the caller coerces it). The cell store is global, so a write
// through a `Ref<T>` arg is already visible at the caller (no merge needed) — the value-functional
// .tks twin threads `cells` through the env to reproduce this. Used by both eval_call and call_value.
static tk_flow run_user_fn(const tk_tfunction *fn, size_t fn_idx, tk_venv *fenv) {
    // (W9.3) Save/clear/restore the defer stack across the call frame so a callee's defers never mix
    // with the caller's. The fn-body exec_block is itself a defer scope (W9.3) and drains the body's
    // own defers at its exit — so NO separate drain is needed here. Panic still does NOT drain (a
    // panic aborts; defers registered before it don't run).
    tk_vm_defer_node *saved_defer_top = g_vm_defer_top;
    size_t saved_defer_depth = g_vm_defer_depth;   // (W9.3 part 2) the count mark, restored with the top
    g_vm_defer_top = NULL;
    g_vm_defer_depth = 0;
    tk_cov_enter(fn_idx);   // D3-branch — attribute body branches to this fn (no-op unless coverage on)
    // (W9.3 part 2) is_fn_body=true: the body's trailing value-expr fires the body's defers BEFORE it is
    // evaluated, mirroring native (emit_block_tail → emit_exprstmt_tail drains base-0 defers first).
    tk_flow fl = tk_vm_exec_block_ex(fn->body, fn->nbody, fenv, /*is_fn_body=*/true);
    tk_cov_leave();
    g_vm_defer_top = saved_defer_top;   // restore caller's defer stack
    g_vm_defer_depth = saved_defer_depth;
    return fl;
}

// (W10a) resolve a CLOSURE call's target function: the callee is a LOCAL bound to a TK_VAL_FUNC
// value (a named fn used as a value). Read that local, then resolve the function by its name (no
// captured env in W10a). Shared by call_value (statement) and eval_call (value). (Mirrors vm.tks.)
// (W10) if the closure-call callee is a LAMBDA value, run it: bind params to the evaluated args and
// the captured snapshot in a fresh frame, exec the body, and return true with the result in *out. A
// captured RefVal shares its cell (global store) → by-ref mutation. Returns false for a named-fn
// value (the caller falls through to closure_target/find_function — the W10a path).
static bool try_lambda_call(const tk_texpr *e, tk_venv *env, tk_value *out) {
    tk_path p = e->as.call.callee;
    tk_slot *cs = env_find(env, p.segments[p.len - 1].name);
    tk_value cv = (cs && cs->has_cell) ? cell_get(cs->cell_id) : (cs ? cs->val : (tk_value){0});
    if (cs == NULL || cv.tag != TK_VAL_FUNC || !cv.as.func.is_lambda) return false;
    tk_venv fenv = { .head = NULL };
    for (size_t i = 0; i < cv.as.func.nparams; i += 1)
        env_define(&fenv, cv.as.func.params[i].name, tk_vm_eval_expr(&e->as.call.args[i], env));
    for (size_t i = 0; i < cv.as.func.ncaps; i += 1)
        env_define(&fenv, cv.as.func.cap_names[i], cv.as.func.cap_vals[i]);
    tk_flow fl = tk_vm_exec_block_ex(cv.as.func.body, cv.as.func.nbody, &fenv, true);
    env_free(&fenv);
    *out = ((fl.kind == TK_FLOW_RETURN || fl.kind == TK_FLOW_NORMAL) && fl.has_value) ? fl.value : v_void();
    return true;
}

static const tk_tfunction *closure_target(const tk_texpr *e, tk_venv *env, size_t *out_idx) {
    tk_path p = e->as.call.callee;
    tk_slot *cs = env_find(env, p.segments[p.len - 1].name);
    tk_value cv = (cs && cs->has_cell) ? cell_get(cs->cell_id) : (cs ? cs->val : (tk_value){0});
    if (cs == NULL || cv.tag != TK_VAL_FUNC) vm_unsupported("closure call on a non-function value (internal: checker should reject)");
    tk_segment fseg = { cv.as.func.name };
    tk_path fp = { &fseg, 1 };
    // A named-fn VALUE carries its OWN declaring namespace (cv.as.func.ns, set when the TVar
    // reference was evaluated — is_func's func_ns) — use it to disambiguate a same-named fn in
    // another namespace, same fix as the direct-call sites below.
    return find_function_ns(fp, cv.as.func.ns, out_idx);
}

// (W10b.D3) resolve a DYNAMIC contract-method call's target: the concrete class's stamped
// function named `method` whose namespace IS `class_name` or ends with "::" + `class_name` —
// the SAME rule codegen's vtable build uses (cg_find_method_ns), so both engines dispatch to
// the SAME method (virtual overrides included: every effective method is stamped per class).
// Mirror of vm.tks::find_class_method.
static const tk_tfunction *find_class_method(tk_str class_name, tk_str method, size_t *out_idx) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        const tk_tfunction *tf = &g_prog.items[i].as.function;
        if (!name_eq(tf->name, method)) continue;
        if (name_eq(tf->namespace, class_name)) { *out_idx = i; return tf; }
        size_t suffix_len = 2 + class_name.len;
        if (tf->namespace.len >= suffix_len) {
            const tk_byte *tail = tf->namespace.ptr + (tf->namespace.len - suffix_len);
            if (tail[0] == ':' && tail[1] == ':') {
                tk_str name_part = { tail + 2, class_name.len };
                if (name_eq(name_part, class_name)) { *out_idx = i; return tf; }
            }
        }
    }
    return NULL;
}

// (W10b.D3) DYNAMIC contract-method dispatch. The receiver (args[0]) evaluates ONCE; a class
// instance IS the interface value in the VM (a ClassRef cell — no fat pointer needed, the cell's
// struct payload carries the CONCRETE class name to dispatch on). The ORIGINAL ClassRef binds as
// the receiver param (reference semantics survive the dispatch). Non-receiver args bind exactly
// like bind_call_args (Ref auto-promotion included). Shared by BOTH eval_call and call_value —
// the twin-asymmetry trap's two entry points. Mirror of vm.tks::eval_iface_call.
static tk_value eval_iface_call(const tk_texpr *e, tk_venv *env) {
    tk_value recv = tk_vm_eval_expr(&e->as.call.args[0], env);
    if (recv.tag != TK_VAL_CLASS_REF)
        vm_unsupported("interface dispatch on a non-class value (internal: only a class instance upcasts)");
    tk_value payload = cell_get(recv.as.class_ref.cell);
    if (payload.tag != TK_VAL_STRUCT)
        vm_unsupported("interface dispatch: the class cell holds no struct payload (internal)");
    tk_str method = e->as.call.callee.segments[e->as.call.callee.len - 1].name;
    size_t fn_idx = 0;
    const tk_tfunction *fn = find_class_method(payload.as.st.type_name, method, &fn_idx);
    if (fn == NULL)
        vm_unsupported("interface dispatch: no stamped method on the concrete class (internal: conformance was checked)");
    if (fn->nparams == 0 || fn->nparams != e->as.call.nargs)
        vm_unsupported("interface dispatch: arity mismatch (internal: the checker fixed the arity)");
    if (!fn->is_test) tk_cov_mark(fn_idx);
    tk_venv fenv = { .head = NULL };
    env_define(&fenv, fn->params[0].name, recv);   // the receiver — the ORIGINAL ClassRef
    for (size_t i = 1; i < e->as.call.nargs; i += 1) {
        if (param_is_ref(fn, i)) {   // same auto-ref/forward rule as bind_call_args
            if (e->as.call.args[i].tag != TK_TEXPR_VAR)
                vm_unsupported("a `Ref<T>` argument must be a mutable variable (internal: checker should reject)");
            if (e->as.call.args[i].type.tag == TK_TYPE_REF) {
                tk_value fwd = tk_vm_eval_expr(&e->as.call.args[i], env);
                env_define(&fenv, fn->params[i].name, fwd);
            } else {
                tk_slot *s = env_find(env, e->as.call.args[i].as.var.name);
                if (s == NULL) vm_unsupported("auto-ref of an unbound variable (internal: checker should reject)");
                if (!s->has_cell) { s->cell_id = cell_alloc(s->val); s->has_cell = true; }
                env_define(&fenv, fn->params[i].name, v_ref(s->cell_id));
            }
        } else {
            tk_value av = tk_vm_eval_expr(&e->as.call.args[i], env);
            env_define(&fenv, fn->params[i].name, coerce_to(av, param_coerce_type(fn->params[i].type_ann)));
        }
    }
    tk_flow fl = run_user_fn(fn, fn_idx, &fenv);
    env_free(&fenv);
    if ((fl.kind == TK_FLOW_RETURN || fl.kind == TK_FLOW_NORMAL) && fl.has_value)
        return coerce_to(fl.value, fn->return_type);
    return v_void();
}

// call_value — a call in STATEMENT position (the .tks twin's call_value). (MEM Step 2/3) This is the
// path that SUPPORTS `Ref<T>` aliasing: auto-ref promotes the caller's `mut` lvalue to a (global) cell
// and the callee writes through it, so `bump(x)` mutates `x`. The C cell store is global/shared, so
// there is no env to thread back — the write is already visible at the caller. The .tks twin threads
// the env out (StmtVal.env). eval_call (value position) REJECTS ref params (no caller-env channel in
// the value-functional twin); they are only reachable in statement position. *out gets the call value.
static bool call_value(const tk_texpr *e, tk_venv *env, tk_value *out) {
    tk_path p = e->as.call.callee;
    const tk_texpr *args = e->as.call.args;
    size_t nargs = e->as.call.nargs;

    // (W10b.D3) dynamic contract dispatch — FIRST, before builtin name-sniffing could collide
    // with a method name. Same guard in eval_call (the VM twin-asymmetry rule).
    if (e->as.call.is_iface_dispatch) { *out = eval_iface_call(e, env); return true; }
    if (try_builtin_call(p, args, nargs, env, out)) return true;   // `-> void` builtin (or value builtin) — no ref args
    if (e->as.call.is_closure_call && try_lambda_call(e, env, out)) return true;   // (W10) a lambda value runs its own body

    size_t fn_idx;
    const tk_tfunction *fn = e->as.call.is_closure_call ? closure_target(e, env, &fn_idx) : find_function_ns(p, e->as.call.call_ns, &fn_idx);   // (W10a) closure call → resolve through the local FuncVal; direct call → namespace-disambiguated (BUGFIX above)
    if (fn == NULL) {
        static char buf[256];
        tk_str last = p.segments[p.len - 1].name;
        snprintf(buf, sizeof buf, "`%.*s` is a host function the VM cannot run (use `teko build` to compile natively)",
                 (int)last.len, (const char *)last.ptr);
        vm_unsupported(buf);
    }
    if (fn->is_extern) vm_unsupported("an `extern` function cannot run in the VM (foreign C call) — use `teko build` to compile it natively (C7.1a)");
    if (!fn->is_test) tk_cov_mark(fn_idx);

    tk_venv fenv = { .head = NULL };
    bind_call_args(fn, args, nargs, env, &fenv);   // auto-ref `Ref<T>` args (promote caller lvalues to cells)
    tk_flow fl = run_user_fn(fn, fn_idx, &fenv);
    env_free(&fenv);
    if (fl.kind == TK_FLOW_RETURN && fl.has_value) { *out = fl.value; return true; }   // explicit `return e`
    if (fl.kind == TK_FLOW_NORMAL && fl.has_value) { *out = fl.value; return true; }   // W5 — implicit trailing value (B.20)
    *out = v_void();   // `-> void` fn / falls off the end — no value
    return true;
}

static tk_value eval_call(const tk_texpr *e, tk_venv *env) {
    tk_path p = e->as.call.callee;
    const tk_texpr *args = e->as.call.args;
    size_t nargs = e->as.call.nargs;

    // (W10b.D3) dynamic contract dispatch — FIRST, before builtin name-sniffing could collide
    // with a method name. Same guard in call_value (the VM twin-asymmetry rule).
    if (e->as.call.is_iface_dispatch) return eval_iface_call(e, env);
    tk_value out;
    if (try_builtin_call(p, args, nargs, env, &out)) return out;
    if (e->as.call.is_closure_call && try_lambda_call(e, env, &out)) return out;   // (W10) a lambda value runs its own body

    size_t fn_idx;
    const tk_tfunction *fn = e->as.call.is_closure_call ? closure_target(e, env, &fn_idx) : find_function_ns(p, e->as.call.call_ns, &fn_idx);   // (W10a) closure call → resolve through the local FuncVal; direct call → namespace-disambiguated (BUGFIX above)
    if (fn == NULL) {
        // HOST-FFI FRONTIER (not a checker bug): the checker accepts host-FFI/builtin calls
        // because the native backend lowers them; the VM has no host surface (Phase 7). Stop
        // honestly, naming the callee and pointing at `teko build`. (Mirrors vm.tks find_function.)
        static char buf[256];
        tk_str last = p.segments[p.len - 1].name;
        snprintf(buf, sizeof buf, "`%.*s` is a host function the VM cannot run (use `teko build` to compile natively)",
                 (int)last.len, (const char *)last.ptr);
        vm_unsupported(buf);
    }
    // C7.1a — an `extern` is a foreign C function with NO Teko body; the tree-walking VM has no
    // FFI surface (the same deferred host edge as the io/fs bottoms). Stop HONESTLY (M.3), never
    // synthesize a value, and point at the native path. (Mirrors vm.tks eval_call.)
    if (fn->is_extern) vm_unsupported("an `extern` function cannot run in the VM (foreign C call) — use `teko build` to compile it natively (C7.1a)");
    // (MEM Step 2/3) A `Ref<T>` call in VALUE position (e.g. `let y = bump(x)`) mutates an aliased caller
    // cell; that mutation must be visible at the caller. The C cell store is global/shared by pointer, so
    // bind_call_args' auto-ref promotion of the caller's `mut` lvalue and the callee's writes through that
    // cell are ALREADY visible at the caller — no env-return channel is needed (the value-functional .tks
    // twin threads the env out of eval_expr to reproduce this same effect). So value-position ref calls
    // run here exactly like statement-position ones (call_value), matching native. (NO honest-stop.)
    if (!fn->is_test) tk_cov_mark(fn_idx);   // D3 — globally unique index (not line) avoids cross-file collisions

    // A fresh root frame — no closure capture (flat functions, like codegen's C). Bind each parameter to
    // its evaluated argument; a `Ref<T>` param auto-refs the caller's `mut` lvalue to a global cell. (B-vm.)
    tk_venv fenv = { .head = NULL };
    bind_call_args(fn, args, nargs, env, &fenv);
    tk_flow fl = run_user_fn(fn, fn_idx, &fenv);
    env_free(&fenv);
    // Coerce the returned value into the declared return type: a `T` returned from a `-> T?`
    // fn present-wraps (REBOOT §202), mirroring codegen's emit_as on the return slot. A NONE
    // (`null`) / already-optional value passes through.
    if (fl.kind == TK_FLOW_RETURN && fl.has_value) return coerce_to(fl.value, fn->return_type);   // explicit `return e`
    if (fl.kind == TK_FLOW_NORMAL && fl.has_value) return coerce_to(fl.value, fn->return_type);   // W5 — implicit trailing value (B.20)
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

// (W9.4) a SYNTACTIC type-expr → its mangle fragment, mirroring tk_type_mangle (prim/str/byte/named
// → the segment name, slice_/opt_ prefixes, a nested generic use → its own instance name). Mirror of
// vm.tks::vm_texpr_mangle / vm_texpr_inst_name.
static tk_str vm_texpr_mangle(tk_type_expr te) {
    switch (te.tag) {
        case TK_TEXPR_NAMED: {
            tk_str last = te.as.named.path.segments[te.as.named.path.len - 1].name;
            if (te.as.named.args_len == 0) return last;
            tk_str out = tk_str_concat(last, (tk_str){ (const tk_byte *)"__g__", 5 });
            for (size_t i = 0; i < te.as.named.args_len; i += 1) {
                if (i > 0) out = tk_str_concat(out, (tk_str){ (const tk_byte *)"__", 2 });
                out = tk_str_concat(out, vm_texpr_mangle(te.as.named.args[i]));
            }
            return out;
        }
        case TK_TEXPR_SLICE:    return tk_str_concat((tk_str){ (const tk_byte *)"slice_", 6 }, vm_texpr_mangle(*te.as.slice.element));
        case TK_TEXPR_OPTIONAL: return tk_str_concat((tk_str){ (const tk_byte *)"opt_", 4 }, vm_texpr_mangle(*te.as.optional.inner));
        case TK_TEXPR_UNION:    return (tk_str){ (const tk_byte *)"variant", 7 };
        case TK_TEXPR_FUNC:     return (tk_str){ (const tk_byte *)"func", 4 };   // (W10a) matches checker type_mangle's Func fragment
    }
    return (tk_str){ NULL, 0 };
}

// (W9.4) a bind pattern's CASE NAME: the STAMPED instance `Gen__g__i64` when it carries explicit
// type-args (`Gen<i64> as x`), else the plain path-last identifier. Mirror of vm.tks::bind_case_name.
static tk_str bind_case_name(tk_bind_pattern bp) {
    if (bp.nargs == 0) return path_last(bp.type_name);
    tk_type_expr gte = { .tag = TK_TEXPR_NAMED, .as.named = { .path = bp.type_name, .args = bp.type_args, .args_len = bp.nargs } };
    return vm_texpr_mangle(gte);
}

// (W10b.D3 codec fix) are `a` and `b` BOTH members of one declared variant? If so, a struct value
// tagged `a` is ALREADY a fully-discriminated case wherever `b` could appear — so descending into
// its fields to reinterpret it as the sibling case `b` is the conflation bug the tag-24 roundtrip
// caught (`Func { ret = Prim }` matched a `Prim` arm through `ret`, since Func and Prim are both
// `Type` members). Combined with the field-count guard at the descent site: descent is SKIPPED
// only for a MULTI-FIELD sibling — a SINGLE-FIELD sibling wrapper (`Optional { inner = Prim }`)
// still descends (doctrine — teko-vm-wrapper-descent-bug), and a NON-sibling multi-field node
// (`Expr { kind = Compare; line; col }` → Compare, an ExprKind member Expr is not a peer of) still
// descends (the corpus AST-node-into-kind idiom). Mirrors vm.tks::variant_siblings.
static bool variant_siblings(tk_str a, tk_str b) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl td = g_prog.items[i].as.type_decl;
        if (td.body.tag != TK_BODY_VARIANT) continue;
        tk_type_expr te = td.body.as.variant_body.type_expr;
        if (te.tag != TK_TEXPR_UNION) continue;
        bool has_a = false, has_b = false;
        for (size_t j = 0; j < te.as.uni.len; j += 1) {
            tk_type_expr m = te.as.uni.members[j];
            if (m.tag != TK_TEXPR_NAMED || m.as.named.path.len == 0) continue;
            tk_str nm = m.as.named.path.segments[m.as.named.path.len - 1].name;
            if (name_eq(nm, a)) has_a = true;
            if (name_eq(nm, b)) has_b = true;
        }
        if (has_a && has_b) return true;
    }
    return false;
}

// case_in_variant — is `cname` a MEMBER of the variant type `vname`? A match arm pattern may name
// a VARIANT (e.g. `Type`) and the value's case is one of its members (e.g. `Prim`): native codegen
// discriminates the tagged union; the VM has no wrapper (the value IS the member struct), so it
// must resolve the variant decl in the program and check membership. (Mirrors vm.tks.)
static bool case_in_variant(tk_str vname, tk_str cname) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl td = g_prog.items[i].as.type_decl;
        if (!name_eq(td.name, vname)) continue;
        if (td.body.tag != TK_BODY_VARIANT) return false;
        tk_type_expr te = td.body.as.variant_body.type_expr;
        if (te.tag != TK_TEXPR_UNION) return false;
        for (size_t j = 0; j < te.as.uni.len; j += 1) {
            tk_type_expr m = te.as.uni.members[j];
            if (m.tag == TK_TEXPR_NAMED && m.as.named.path.len > 0
                && name_eq(m.as.named.path.segments[m.as.named.path.len - 1].name, cname))
                return true;
        }
        return false;
    }
    return false;
}

// field_coerce_type — the resolved Type to COERCE a field value into at struct construction, derived
// from the field's declared TypeExpr in the program's type decls (the SAME table native's
// cg_find_struct_field_type reads — the generic-stamped decl, e.g. `Box__g__opt_i64`, is rewritten
// into g_prog by monomorph, so it resolves here too). An OptionalType field yields TK_TYPE_OPTIONAL
// so coerce_to PRESENT-wraps a bare value (native's emit_as does the same for a `T?` field). Every
// other shape (incl. an absent struct/field) yields TK_TYPE_VOID, coerce_to's no-op: a bare CASE
// into a variant field needs no VM wrapper (the case value IS its variant value, discriminated at
// match), and a `[]T?` field is an array literal whose elements eval_expr already coerces. The
// returned OPTIONAL carries a NULL inner; coerce_to reads only the tag, never the inner. (Mirrors
// vm.tks's vm_field_coerce_type.)
static tk_type field_coerce_type(tk_str sname, tk_str fname) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl td = g_prog.items[i].as.type_decl;
        if (!name_eq(td.name, sname)) continue;
        if (td.body.tag != TK_BODY_STRUCT) return (tk_type){ .tag = TK_TYPE_VOID };
        tk_struct_body sb = td.body.as.struct_body;
        for (size_t j = 0; j < sb.n_fields; j += 1) {
            if (!name_eq(sb.fields[j].name, fname)) continue;
            // OptionalType field → present-wrap; SliceType field → element-wise rebuild (a `[]T?` field
            // present-wraps each element); every other shape → no-op. Shared with param_coerce_type so
            // field and call-arg slots wrap uniformly. (Native parity with emit_struct_init's emit_as.)
            return param_coerce_type(sb.fields[j].type_ann);
        }
        return (tk_type){ .tag = TK_TYPE_VOID };
    }
    return (tk_type){ .tag = TK_TYPE_VOID };
}

// match_as_enum_int — does `name` match an integer `ordinal` when the subject is an enum value?
// Two cases:
//   (A) `name` is an ENUM TYPE NAME (e.g., `PrimKind as k`): any integer is a valid enum value
//       of that type — the checker guarantees the subject came from a `PrimKind | error` union.
//   (B) `name` is an ENUM MEMBER NAME (e.g., `U8 =>`, `I64 =>`): matches only when the ordinal
//       matches that member's position in the enum.
// Scans all enum TypeDecls; both cases may coexist (a member name in one enum, type name in another).
static bool match_as_enum_int(tk_str name, unsigned __int128 ordinal) {
    for (size_t i = 0; i < g_prog.nitems; i += 1) {
        if (g_prog.items[i].tag != TK_TITEM_TYPE_DECL) continue;
        tk_type_decl td = g_prog.items[i].as.type_decl;
        if (td.body.tag != TK_BODY_ENUM) continue;
        // Case A: pattern names the whole enum type — any valid ordinal matches.
        if (name_eq(td.name, name)) return true;
        // Case B: pattern names a specific member — match only at the right ordinal.
        tk_enum_body eb = td.body.as.enum_body;
        for (size_t j = 0; j < eb.n_members; j += 1)
            if (name_eq(eb.members[j], name) && (unsigned __int128)j == ordinal)
                return true;
    }
    return false;
}

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
    while (s != stop) { tk_slot *n = s->next; tk_free0(s); s = n; }
    env->head = stop;
}

// val_type_matches — does a value's TYPE match a bare type-pattern name (a union member named by
// its type)? A prim member matches by tag + width/sign; bool/str by tag; a named case / `error` by
// the struct's type_name. Lets a `match` arm name a member by TYPE with NO destructure and NO
// alias — `match x { i64 => …; error => … }` — mirroring the native tagged-union discrimination.
// (Slices are handled by the is_slice branch; `null`/optionals above.) (Mirrors vm.tks.)
static bool val_type_matches(tk_value subj, tk_str name) {
    switch (subj.tag) {
        case TK_VAL_INT: {
            int w = subj.as.i.width; bool s = subj.as.i.is_signed;
            const char *nm = s ? (w==8?"i8":w==16?"i16":w==32?"i32":w==128?"i128":"i64")
                               : (w==8?"u8":w==16?"u16":w==32?"u32":w==128?"u128":"u64");
            if (seg_is(name, nm)) return true;
            if (!s && w == 8 && seg_is(name, "byte")) return true;   // byte ≈ unsigned 8-bit in the C model
            return false;
        }
        case TK_VAL_FLOAT: { int w = subj.as.fl.width; return seg_is(name, w==16?"f16":w==32?"f32":"f64"); }
        case TK_VAL_BOOL:  return seg_is(name, "bool");
        case TK_VAL_STR:   return seg_is(name, "str");
        case TK_VAL_STRUCT:return name_eq(subj.as.st.type_name, name);   // a named case / `error`
        default:           return false;   // LIST → is_slice branch; OPT → handled above; CLASS_REF → cell deref in pat_match (C1)
    }
}

// Does `subj` match `pat`? On match, defines the pattern's bindings into `env`.
static bool pat_match(const tk_pattern *pat, tk_value subj, tk_venv *env) {
    // OPTIONAL subject (REBOOT_PLAN §202): `null` matches NONE; any other pattern matches the
    // PRESENT case and is re-tested against the inner value. Mirrors the checker's tk_check_pattern.
    if (subj.tag == TK_VAL_OPT) {
        if (pat->tag == TK_PAT_NULL) return !subj.as.opt.present;          // `null` ⇔ NONE
        if (!subj.as.opt.present) return false;                            // non-null pattern over NONE: no match
        tk_value inner = *subj.as.opt.inner;
        // A bare bind/wildcard over a present NON-struct inner (i64/str/error/…) matches and
        // binds the inner directly — `i64 as v` / `error as e` / `_`. (A struct/variant inner
        // recurses so the variant tag check / field destructure still applies.)
        if (inner.tag != TK_VAL_STRUCT) {
            if (pat->tag == TK_PAT_WILDCARD) return true;
            if (pat->tag == TK_PAT_BIND) {
                if (pat->as.bind.has_binding) env_define(env, pat->as.bind.binding, inner);
                return true;
            }
        }
        return pat_match(pat, inner, env);                                 // PRESENT → match the inner
    }
    switch (pat->tag) {
        case TK_PAT_NULL: return false;   // a `null` pattern over a non-optional value never matches
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
            if (pat->as.bind.is_slice) {                                          // `[]T as x` — the slice case of a `[]T | error`
                if (subj.tag != TK_VAL_LIST) return false;                        // a slice value is a list
                if (pat->as.bind.has_binding) env_define(env, pat->as.bind.binding, subj);
                return true;
            }
            // a member matched by TYPE: a named case is a struct whose type_name IS the case; a
            // PRIM/bool/str member matches by the value's kind (val_type_matches). Supports `Type =>`
            // with NO destructure and NO alias, for every member kind. ALSO a pattern that names a
            // VARIANT (e.g. `Type`) matches a value whose case is one of its members (case_in_variant).
            {
                tk_str bname = bind_case_name(pat->as.bind);   // (W9.4) `Gen<i64> as x` → `Gen__g__i64`
                bool direct = val_type_matches(subj, bname)
                    || (subj.tag == TK_VAL_STRUCT && case_in_variant(bname, subj.as.st.type_name))
                    || (subj.tag == TK_VAL_INT && match_as_enum_int(bname, v_as_u128(subj)));
                // (C1 fallible factories) a CLASS instance in a union (`C | error`) is matched by
                // its class NAME: deref the ClassRef cell to its struct payload and compare its
                // type_name. On a hit the ORIGINAL ClassRef is bound (reference semantics — the
                // arm's binding keeps aliasing the shared object, mirroring the native pointer).
                if (!direct && subj.tag == TK_VAL_CLASS_REF) {
                    tk_value payload = cell_get(subj.as.class_ref.cell);
                    direct = payload.tag == TK_VAL_STRUCT && name_eq(payload.as.st.type_name, bname);
                }
                if (direct) {
                    if (pat->as.bind.has_binding) env_define(env, pat->as.bind.binding, subj);
                    return true;
                }
                // Transparent field descent: when the subject is a struct whose fields hold a
                // value matching the pattern name (e.g. `Compare as c` on `Expr { kind = Compare;
                // line; col }`), drill into the first matching STRUCT field and bind. SKIPPED only
                // for a MULTI-FIELD SIBLING of the target — a subject that is itself a properly-
                // discriminated case of the SAME union the target belongs to must not be re-read as
                // the sibling case through an inner field (`Func { params; ret; … }` → a `Prim` arm
                // via `ret`, both `Type` members — the tag-24 codec bug). A SINGLE-FIELD sibling
                // wrapper (`Optional { inner = Prim }` → Prim) still descends (doctrine); a NON-
                // sibling multi-field node still descends (the AST-node-into-kind corpus idiom).
                // Only struct fields are eligible — int fields are excluded because match_as_enum_int
                // could match enum member names that coincidentally share an ordinal (e.g.
                // TyShape::Variant=2 vs PrimKind::U32=2 causing false descent).
                bool descend_ok = subj.tag == TK_VAL_STRUCT
                    && !(subj.as.st.fields.len > 1 && variant_siblings(subj.as.st.type_name, bname));
                if (descend_ok) {
                    for (size_t fi = 0; fi < subj.as.st.fields.len; fi += 1) {
                        tk_value fv = subj.as.st.fields.vals[fi];
                        if (fv.tag != TK_VAL_STRUCT) continue;
                        bool fhit = val_type_matches(fv, bname)
                            || case_in_variant(bname, fv.as.st.type_name);
                        if (fhit) {
                            if (pat->as.bind.has_binding) env_define(env, pat->as.bind.binding, fv);
                            return true;
                        }
                    }
                }
                return false;
            }
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
                tk_cov_branch(e->line, e->col, i);   // D3-branch: value-position arm `i` taken
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
        if (i >= recv.as.s.len) vm_panic_oob_at(e);   // C1.6 — positioned at the subscript node
        return v_int((uint64_t)recv.as.s.ptr[(size_t)i], false, 8);   // byte == u8
    }
    if (recv.tag == TK_VAL_LIST) {   // slice subscript: bounds-checked, same panic as native (M.1)
        if (i >= recv.as.list.len) vm_panic_oob_at(e);   // C1.6 — positioned at the subscript node
        return recv.as.list.ptr[(size_t)i];
    }
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
            tk_tinterp_spec *sp = (e->as.interp.specs) ? &e->as.interp.specs[i] : NULL;
            tk_fspec_kind fk = sp ? sp->kind : TK_FSPEC_NONE;
            tk_str hs;
            if (fk == TK_FSPEC_STATIC) {
                tk_str spec = sp->static_spec;
                char fc = (spec.len > 0) ? (char)(spec.ptr[0] | 0x20) : 'f';
                int prec = 0; bool has_prec = false;
                for (size_t si = 1; si < spec.len; si++) {
                    if (spec.ptr[si] >= '0' && spec.ptr[si] <= '9') {
                        prec = prec * 10 + (spec.ptr[si] - '0'); has_prec = true;
                    }
                }
                if (!has_prec) prec = (fc == 'd') ? 1 : 6;
                bool is_float_val = (h.tag == TK_VAL_FLOAT);
                bool is_sint = (h.tag == TK_VAL_INT && h.as.i.is_signed);
                double dval = is_float_val ? h.as.fl.f : (is_sint ? (double)(int64_t)v_as_i128(h) : (double)(uint64_t)v_as_u128(h));
                int64_t ival = is_sint ? (int64_t)v_as_i128(h) : 0;
                uint64_t uval = (!is_sint && !is_float_val) ? (uint64_t)v_as_u128(h) : 0;
                if      (fc == 'f') hs = tk_fmt_f(dval, prec);
                else if (fc == 'e') hs = tk_fmt_e(dval, prec);
                else if (fc == 'g') hs = tk_fmt_g(dval, prec);
                else if (fc == 'n' && is_float_val) hs = tk_fmt_n_f(dval, prec);
                else if (fc == 'p') hs = tk_fmt_p(dval, prec);
                else if (fc == 'd') hs = tk_fmt_d(ival, prec);
                else if (spec.len > 0 && spec.ptr[0] == 'X') hs = tk_fmt_x_upper(uval);
                else if (fc == 'x') hs = tk_fmt_x_lower(uval);
                else if (fc == 'b') hs = tk_fmt_b(uval);
                else if (fc == 'n') hs = tk_fmt_n_i(ival);
                else vm_unsupported("interpolation: unrecognized format spec");
            } else if (fk == TK_FSPEC_DYNAMIC) {
                tk_value spec_val = tk_vm_eval_expr(&sp->dyn_args[0], env);
                tk_str spec_str = (spec_val.tag == TK_VAL_STR) ? spec_val.as.s : (tk_str){ NULL, 0 };
                if (h.tag == TK_VAL_FLOAT)       hs = tk_fmt_dyn_f64(h.as.fl.f, spec_str);
                else if (h.as.i.is_signed)        hs = tk_fmt_dyn_i64((int64_t)v_as_i128(h), spec_str);
                else                              hs = tk_fmt_dyn_u64((uint64_t)v_as_u128(h), spec_str);
            } else if (h.tag == TK_VAL_STR) {
                hs = h.as.s;
            } else if (h.tag == TK_VAL_INT) {
                hs = h.as.i.is_signed ? tk_i64_to_str((int64_t)v_as_i128(h))
                                      : tk_u64_to_str((uint64_t)v_as_u128(h));
            } else if (h.tag == TK_VAL_FLOAT) {
                hs = tk_ftoa(h.as.fl.f);
            } else if (h.tag == TK_VAL_BOOL) {
                hs = h.as.b ? (tk_str){ (const tk_byte *)"true", 4 }
                            : (tk_str){ (const tk_byte *)"false", 5 };
            } else if (h.tag == TK_VAL_STRUCT) {
                hs = v_error_field(h, ERR_LIT("message")).as.s;  // error stored as struct
            } else {
                vm_unsupported("interpolation hole type not supported");
            }
            acc = tk_str_concat(acc, hs);
        }
    }
    return v_str(acc);
}

// Phase 2 — `<lhs> in [ e0, e1, … ]` -> bool. The LHS is EVALUATED ONCE; then each element is
// evaluated and compared to that SAME value with value_eq (the SAME value-equality the VM uses for
// scalar `==` / literal patterns — INT/BOOL/STR/FLOAT). Returns true iff any element equals the lhs;
// the empty set `x in []` -> false (the loop runs zero times). value_eq returns false for
// struct/list operands, but the checker restricts `in` to scalar-comparable element types, so the
// honest frontier matches `==`. Mirrors the codegen lowering (a fold of value_eq, single-eval lhs).
static tk_value eval_in(const tk_texpr *e, tk_venv *env) {
    tk_value lhs = tk_vm_eval_expr(e->as.in_expr.lhs, env);   // single-eval: the LHS once
    for (size_t i = 0; i < e->as.in_expr.nelems; i += 1) {
        tk_value el = tk_vm_eval_expr(&e->as.in_expr.elems[i], env);
        if (value_eq(lhs, el)) return v_bool(true);
    }
    return v_bool(false);   // empty set, or no element matched
}

static tk_value tk_vm_eval_expr(const tk_texpr *e, tk_venv *env) {
    tk_cov_line(e->line);   // D3-line — mark this source line executed (no-op unless line recording is on)
    // New type tags (TEKO_CORRECTION_PLAN [Z1]). A value-position expression can never be
    // typed `void` (void is return-only, never a value — the checker rejects it elsewhere).
    // Optional `T?` values ARE supported now (TK_VAL_OPT — REBOOT_PLAN §202): null / ?. / ??
    // produce them, the present-wrap (coerce_to) constructs them, and pattern matching reads
    // them; they flow through the value model like any other tag.
    // A void CALL is the one void expression that legitimately RUNS (print/println/assert
    // or a `-> void` fn), executed for effect in statement position; eval_call returns the
    // never-consumed v_void() placeholder. Any OTHER void expr in a value position is a
    // checker failure (the checker rejects void as a value/binding/operand) — honest stop.
    if (e->type.tag == TK_TYPE_VOID && e->tag != TK_TEXPR_CALL)
        vm_unsupported("void expression has no value (internal: checker should reject in value position)");
    switch (e->tag) {
        case TK_TEXPR_NUMBER: {
            // The literal's prim comes from the node's type; bool literals don't reach here.
            // TK_TYPE_BYTE (`byte` type) is like u8 — an unsigned 8-bit integer literal.
            if (e->type.tag == TK_TYPE_BYTE)
                return v_int((unsigned __int128)(uint8_t)e->as.number.value, false, 8);
            // (#50) a FLAGS-typed number literal — the checker's fabricated zero in the any/none
            // lowering (type_flags_method) — computes at the flags carrier prim (unsigned).
            if (e->type.tag == TK_TYPE_NAMED) {
                tk_prim_kind fp;
                if (flags_carrier_prim(e->type.as.named.name, &fp))
                    return v_int((unsigned __int128)e->as.number.value, false, prim_width(fp));
            }
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
            // (W10a) a bare top-level-fn reference used as a VALUE → a function value (no captured env).
            if (e->as.var.is_func)
                return (tk_value){ .tag = TK_VAL_FUNC, .as.func = { .is_lambda = false, .ns = e->as.var.func_ns, .name = e->as.var.name } };
            tk_slot *s = env_find(env, e->as.var.name);
            if (s == NULL) vm_unsupported("reference to an unbound variable (internal: checker should reject)");
            // (MEM Step 2/3) a CELL-BACKED slot (a `Ref<T>` aliasing target whose `mut` value was
            // promoted to the cell store) reads its CURRENT value from the cell, so the origin var
            // observes writes made through any RefVal aliasing it. A plain slot yields slot->val.
            return s->has_cell ? cell_get(s->cell_id) : s->val;
        }
        case TK_TEXPR_STR:  return v_str(e->as.str.text);
        case TK_TEXPR_BYTE: return v_int((uint64_t)e->as.byte.value, false, 8);   // byte == u8 rep
        case TK_TEXPR_CHAR: return v_str(e->as.char_lit.bytes);   // char — the codepoint's UTF-8 bytes (same str carrier; distinct by the checker tag, VM==native)
        case TK_TEXPR_PATH: {
            // (#50) Type::Member → the checker-RESOLVED member value (enum: the ordinal, u64 —
            // codegen's C enum auto-numbers identically; flags: 1 << ordinal at the carrier width —
            // codegen's pre-emitted power-of-2 constants). No recompute — read the stored value.
            tk_prim_kind fp;
            if (e->type.tag == TK_TYPE_NAMED && flags_carrier_prim(e->type.as.named.name, &fp))
                return v_int(e->as.path.value, false, prim_width(fp));
            return v_int(e->as.path.value, false, 64);
        }

        // bool literal (W2) — FULL support (bool already flows through the value model).
        case TK_TEXPR_BOOL: return v_bool(e->as.boolean.value);
        case TK_TEXPR_BINARY:       return eval_binary(e, env);
        case TK_TEXPR_UNARY:        return eval_unary(e, env);
        case TK_TEXPR_COMPARE:      return eval_compare(e, env);
        case TK_TEXPR_CAST:         return eval_cast(e, env);
        case TK_TEXPR_CALL:         return eval_call(e, env);
        case TK_TEXPR_FIELD_ACCESS: {   // W4b — `x.field`: eval the struct receiver, read the field by name.
            tk_value recv = tk_vm_eval_expr(e->as.field_access.receiver, env);
            // (MEM Step 2/3) `r.value` on a `Ref<T>` — the deref READ: the receiver is a RefVal (a
            // cell index); `.value` reads the aliased storage (cell_get). The checker restricts a
            // Reference receiver to ONLY `.value`, so any other field is impossible here.
            if (recv.tag == TK_VAL_REF) {
                if (name_eq(e->as.field_access.field, (tk_str){ (const tk_byte *)"value", 5 }))
                    return cell_get(recv.as.ref.cell);
                vm_unsupported("a reference (`Ref<T>`) exposes only `.value` (internal: checker should reject)");
            }
            // (W10b.CLASS residual — VM reference semantics) a class receiver is a
            // TK_VAL_CLASS_REF (a cell index) — deref through the cell to its struct payload,
            // then fall through to the SAME field lookup a struct value uses below.
            if (recv.tag == TK_VAL_CLASS_REF) recv = cell_get(recv.as.class_ref.cell);
            // W5-idx — `.len`: a `str` value yields its byte length as a u64. A list value
            // (slice) is an honest stop (slice VALUES are the next feature). The checker
            // already typed `.len` as u64 for str/slice receivers.
            if (recv.tag == TK_VAL_STR && name_eq(e->as.field_access.field, (tk_str){ (const tk_byte *)"len", 3 }))
                return v_int((uint64_t)recv.as.s.len, false, 64);
            if (recv.tag == TK_VAL_LIST && name_eq(e->as.field_access.field, (tk_str){ (const tk_byte *)"len", 3 }))
                return v_int((uint64_t)recv.as.list.len, false, 64);
            if (recv.tag != TK_VAL_STRUCT)
                vm_unsupported("field access on a non-struct value (internal: checker should reject)");
            // E2 — an ERROR value's adornment field (message/file/line/col/expected/actual):
            // a present field rides as stored; an UNSET adornment reads its default (empty str /
            // 0:u32) rather than an honest stop, so `e.line` on a message-only error is 0 (C1.3).
            if (name_eq(recv.as.st.type_name, (tk_str){ (const tk_byte *)"error", 5 }))
                return v_error_field(recv, e->as.field_access.field);
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
        case TK_TEXPR_IN: return eval_in(e, env);         // Phase 2 — `<lhs> in [ … ]`: true iff lhs (single-eval) value_eq's any element
        case TK_TEXPR_ARRAY: {                            // [ e0, … ] (Increment B+) — build a list value
            tk_type elem_t = (e->type.tag == TK_TYPE_SLICE && e->type.as.slice.element)
                           ? *e->type.as.slice.element : (tk_type){ .tag = TK_TYPE_VOID };
            tk_value arr = v_list_empty();
            for (size_t i = 0; i < e->as.array.nelements; i += 1) {
                bool is_spread = e->as.array.is_spread && e->as.array.is_spread[i];
                if (is_spread) {
                    // spread element: flatten sub-slice items individually into arr
                    tk_value sub = tk_vm_eval_expr(&e->as.array.elements[i], env);
                    if (sub.tag == TK_VAL_LIST) {
                        for (size_t j = 0; j < sub.as.list.len; j += 1)
                            arr = v_list_push(arr.as.list, coerce_to(sub.as.list.ptr[j], elem_t));
                    }
                } else {
                    tk_value el = coerce_to(tk_vm_eval_expr(&e->as.array.elements[i], env), elem_t);
                    arr = v_list_push(arr.as.list, el);
                }
            }
            return arr;
        }
        // (W10) a closure LITERAL → a function value with a SNAPSHOT of its captures (a RefVal snapshot
        // shares the cell → by-ref mutation). params/body point at the typed AST (lifetime = program).
        case TK_TEXPR_LAMBDA: {
            const tk_tlambda *lam = &e->as.lambda;
            tk_str *cn = lam->ncaptures ? tk_alloc(lam->ncaptures * sizeof *cn) : NULL;
            tk_value *cv = lam->ncaptures ? tk_alloc(lam->ncaptures * sizeof *cv) : NULL;
            for (size_t i = 0; i < lam->ncaptures; i += 1) {
                cn[i] = lam->captures[i].name;
                tk_slot *s = env_find(env, lam->captures[i].name);
                cv[i] = s ? (s->has_cell ? cell_get(s->cell_id) : s->val) : (tk_value){0};
            }
            return (tk_value){ .tag = TK_VAL_FUNC, .as.func = { .is_lambda = true, .ns = (tk_str){0}, .name = (tk_str){0},
                .params = lam->params, .nparams = lam->nparams, .body = lam->body, .nbody = lam->nbody,
                .cap_names = cn, .cap_vals = cv, .ncaps = lam->ncaptures } };
        }
        // null / ?. / ?? (REBOOT_PLAN §202/§203) — the OPTIONAL value model (TK_VAL_OPT).
        case TK_TEXPR_NULL:
            return v_none();   // the `null` literal is NONE; the destination's wrap (coerce_to) makes it a concrete `T?`
        case TK_TEXPR_SAFE_FIELD_ACCESS: {
            // `recv?.field`: NONE → NONE; PRESENT → the inner's field, re-wrapped PRESENT
            // (an already-optional field stays as-is). Result is always an optional value.
            tk_value recv = tk_vm_eval_expr(e->as.safe_field_access.receiver, env);
            if (recv.tag != TK_VAL_OPT)
                vm_unsupported("safe field access on a non-optional value (internal: checker should reject)");
            if (!recv.as.opt.present) return v_none();              // NONE propagates
            tk_value inner = *recv.as.opt.inner;
            // (W10b.CLASS residual — VM reference semantics) a class-typed optional's inner is a
            // TK_VAL_CLASS_REF (not a struct value directly) — deref through the cell first.
            if (inner.tag == TK_VAL_CLASS_REF) inner = cell_get(inner.as.class_ref.cell);
            if (inner.tag != TK_VAL_STRUCT)
                vm_unsupported("safe field access on a non-struct optional (internal: checker should reject)");
            for (size_t i = 0; i < inner.as.st.fields.len; i += 1)
                if (name_eq(inner.as.st.fields.names[i], e->as.safe_field_access.field)) {
                    tk_value f = inner.as.st.fields.vals[i];
                    return f.tag == TK_VAL_OPT ? f : v_some(f);     // result is `(field)?`
                }
            vm_unsupported("field not found in safe field access (internal: the checker guarantees the field)");
        }
        case TK_TEXPR_COALESCE: {
            // `a ?? b`: a's inner if PRESENT, else b (short-circuit — b only evaluated on NONE).
            // The result type (the node's `.type`) is `T` (unwrap) or `T?` (b itself optional);
            // coerce both arms to that type so a `T? ?? T?` stays optional and a `T? ?? T`
            // unwraps. Mirrors the checker's type_coalesce + codegen's emit lowering.
            tk_value a = tk_vm_eval_expr(e->as.coalesce.left, env);
            if (a.tag != TK_VAL_OPT)
                vm_unsupported("coalesce left operand is not optional (internal: checker should reject)");
            if (a.as.opt.present) return coerce_to(*a.as.opt.inner, e->type);
            tk_value b = tk_vm_eval_expr(e->as.coalesce.right, env);
            return coerce_to(b, e->type);
        }
        case TK_TEXPR_STRUCT_INIT: {   // W4b — `Name { f = v, … }`: build a struct value (declared field order).
            size_t nf = e->as.struct_init.nfields;
            tk_str   *names = tk_alloc((nf ? nf : 1) * sizeof *names); if (!names) abort();
            tk_value *vals  = tk_alloc((nf ? nf : 1) * sizeof *vals);  if (!vals)  abort();
            tk_str tn = e->type.tag == TK_TYPE_NAMED ? e->type.as.named.name
                      : e->type.tag == TK_TYPE_ERROR ? (tk_str){ (const tk_byte *)"error", 5 }
                      : (tk_str){ NULL, 0 };
            // COERCE each field value into its declared field type (field_coerce_type → coerce_to), so
            // a bare `T` supplied to a `T?` field PRESENT-wraps — native parity with emit_struct_init's
            // per-field emit_as. Without this a later `?? / ?.` over that field found a non-optional and
            // stopped (a VM-only divergence). The error path (tn == "error") has no struct decl, so
            // field_coerce_type returns VOID and the adornments ride as stored (E2). (Mirrors vm.tks.)
            for (size_t i = 0; i < nf; i += 1) {
                names[i] = e->as.struct_init.field_names[i];
                tk_value raw = tk_vm_eval_expr(&e->as.struct_init.field_vals[i], env);
                vals[i]  = coerce_to(raw, field_coerce_type(tn, e->as.struct_init.field_names[i]));
            }
            tk_value sv = v_struct(tn, (tk_value_fields){ names, vals, nf });
            // (W10b.CLASS residual — VM reference semantics) a class instance is a REFERENCE
            // type (increment 3, native side): construction allocates a cell in the SAME
            // global cell store MEM-1b already uses (g_cells) and yields a TK_VAL_CLASS_REF, so
            // passing/binding/returning it is a cheap cell-index COPY (shared identity),
            // matching the native engine's arena-per-object pointer semantics — NOT a struct's
            // by-value copy.
            {
                tk_type_table table = tk_type_table_of(g_prog);
                tk_classbody_result cb = tk_find_class_body(tn, table);
                if (cb.ok) {
                    uint64_t id = cell_alloc(sv);
                    return (tk_value){ .tag = TK_VAL_CLASS_REF, .as.class_ref = { .cell = id } };
                }
            }
            return sv;
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

// (MEM Step 2/3) compound_apply — apply an assignment operator to `old` with `rhs`: a plain `=`
// stores rhs; a compound op applies the int op at `old`'s own width/signedness (M0 — ints only;
// div/mod route through the checked guard so ÷0 PANICS like the native path, positioned at
// `value_node`). Factored out so a plain-slot, a cell-backed, and a `.value op= …` deref assignment
// all share the SAME arithmetic. (Mirrors vm.tks compound_apply.)
static tk_value compound_apply(tk_value old, tk_token_kind op, tk_value rhs, const tk_texpr *value_node) {
    if (op == TK_TOKEN_ASSIGN) return rhs;
    if (old.tag != TK_VAL_INT || rhs.tag != TK_VAL_INT)
        vm_unsupported("compound assignment on a non-integer value not yet supported");
    bool sgn = old.as.i.is_signed; int w = old.as.i.width;
    uint64_t a = old.as.i.bits, b = rhs.as.i.bits, raw;
    // #49 — mask the shift count by the LEFT (target slot's) width - 1, not a fixed 63;
    // mirrors the eval_binary SHL/SHR fix above (same C#/Java-style semantics).
    unsigned shift_mask = (unsigned)(w - 1);
    switch (op) {
        case TK_TOKEN_PLUSEQ:  raw = a + b; break;
        case TK_TOKEN_MINUSEQ: raw = a - b; break;
        case TK_TOKEN_STAREQ:  raw = a * b; break;
        case TK_TOKEN_AMPEQ:   raw = a & b; break;
        case TK_TOKEN_PIPEEQ:  raw = a | b; break;
        case TK_TOKEN_CARETEQ: raw = a ^ b; break;
        case TK_TOKEN_SHLEQ:   raw = a << (b & shift_mask); break;
        case TK_TOKEN_SHREQ:   raw = sgn ? (uint64_t)((int64_t)a >> (b & shift_mask)) : (a >> (b & shift_mask)); break;
        case TK_TOKEN_SLASHEQ:
        case TK_TOKEN_PERCENTEQ: {
            bool isdiv = (op == TK_TOKEN_SLASHEQ);
            if (b == 0) vm_panic_div0_at(value_node);
            if (sgn) raw = (uint64_t)(isdiv ? checked_div_i((int64_t)a,(int64_t)b)
                                            : checked_mod_i((int64_t)a,(int64_t)b));
            else     raw = isdiv ? checked_div_u(a,b) : checked_mod_u(a,b);
            break;
        }
        default: vm_unsupported("assignment operator not yet supported");
    }
    return norm_int(raw, sgn, w);
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
            v = coerce_to(v, s->as.binding.bound);               // present-wrap into a `T?` slot (REBOOT §202)
            env_define(env, tgt.as.simple.name, v);
            return flow_normal();
        }
        case TK_TSTMT_ASSIGN: {
            tk_token_kind op = s->as.assign.op;
            tk_value rhs = tk_vm_eval_expr(&s->as.assign.value, env);
            // (MEM Step 2/3) `r.value op= x` writes THROUGH a reference: `s->as.assign.name` names the
            // `Ref<T>` binding (a RefVal cell index); a deref-assign reads the cell, applies the op, and
            // writes BACK to the cell (cell_set) — so the caller's aliased var observes the write.
            if (s->as.assign.deref) {
                tk_slot *rslot = env_find(env, s->as.assign.name);
                if (rslot == NULL) vm_unsupported("deref-assign to an unbound reference (internal: checker should reject)");
                tk_value rv = rslot->has_cell ? cell_get(rslot->cell_id) : rslot->val;
                if (rv.tag != TK_VAL_REF) vm_unsupported("deref-assign target is not a reference (internal: checker should reject)");
                tk_value cur = cell_get(rv.as.ref.cell);
                // present-wrap the FINAL value into the ref's inner type (s->as.assign.bound) before the
                // cell write — same coercion the binding path uses (REBOOT §202). For a `Ref<T?>` this
                // makes a deref-WRITE store a PRESENT optional, so a later `?? / ?.` read does not see a
                // bare value (the VM==native divergence: native's deref-assign emit_as already wraps).
                cell_set(rv.as.ref.cell, coerce_to(compound_apply(cur, op, rhs, &s->as.assign.value), s->as.assign.bound));
                return flow_normal();
            }
            tk_slot *slot = env_find(env, s->as.assign.name);
            if (slot == NULL) vm_unsupported("assignment to an unbound variable (internal: checker should reject)");
            // (MEM Step 2/3) a CELL-BACKED slot's value lives in the cell store — read OLD from the
            // cell so a compound op operates on the aliased storage, and write the result BACK to the
            // cell (the slot stays cell-backed). A plain slot reads/writes slot->val as before.
            tk_value old = slot->has_cell ? cell_get(slot->cell_id) : slot->val;
            tk_value nv = compound_apply(old, op, rhs, &s->as.assign.value);
            if (slot->has_cell) cell_set(slot->cell_id, nv); else slot->val = nv;
            return flow_normal();
        }
        case TK_TSTMT_RETURN: {
            // (W9.3 part 2) A `return` leaves EVERY enclosing scope: fire ALL in-scope frame defers
            // (down to depth 0) LIFO — innermost-block-first — BEFORE evaluating the return value, so
            // a defer that mutates the variable being returned is observed. Mirrors codegen's
            // emit_defers(base 0) emitted BEFORE the `return <expr>` text. The unwinding exec_blocks
            // then drain to their (≥0) marks with depth already 0 → safe no-op (no re-fire, no dangle).
            vm_drain_defers_to_depth(0, env);
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
            // (MEM Step 2/3) a statement-position CALL goes through call_value (the Ref<T>-aware path:
            // auto-ref promotes a `mut` lvalue arg to a cell so the callee can mutate it). Other exprs
            // evaluate plainly. (The .tks twin routes TCall → call_value via eval_stmt_value.)
            tk_value v;
            if (x->tag == TK_TEXPR_CALL) { call_value(x, env, &v); }
            else                         { v = tk_vm_eval_expr(x, env); }
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
        case TK_TSTMT_DEFER: {
            // (C7.18) Push this defer block onto the current frame's defer stack.
            tk_vm_defer_node *node = tk_alloc(sizeof *node);
            if (node == NULL) abort();
            node->stmt = s;
            node->next = g_vm_defer_top;
            g_vm_defer_top = node;
            g_vm_defer_depth += 1;
            return flow_normal();
        }
    }
    vm_unsupported("unknown statement not yet supported");
}

// (W9.3) EVERY exec_block is a SCOPE-DEFER frame: defers registered inside this block run at the
// block's exit (LIFO), BEFORE control leaves it — for a loop body that is per-iteration, for an
// if/match arm that is per-arm, for a fn body that is per-call. `saved` is this scope's defer base:
// at exit we drain the nodes pushed on top of `saved` (the most-recent / innermost first), execute
// each via exec_block (itself a no-defer scope — a defer body may not contain defers, so it pushes
// nothing and cannot re-enter), free them, and restore `g_vm_defer_top = saved`. A non-NORMAL flow
// (return/break/continue) crossing this block still drains THIS block's defers first, then the flow
// propagates outward where each crossed exec_block drains its own — innermost-block-first unwind.
// (W9.3 part 2) Drain every defer node above `depth` (i.e. those pushed after the scope whose mark is
// `depth`) in LIFO order, executing each via exec_block, then leave g_vm_defer_depth == depth. If the
// current depth is already ≤ `depth` (e.g. a `return` already pre-drained the whole frame), this is a
// no-op — the safe "already drained below my mark" case.
static void vm_drain_defers_to_depth(size_t depth, tk_venv *env) {
    while (g_vm_defer_depth > depth && g_vm_defer_top != NULL) {
        tk_vm_defer_node *d = g_vm_defer_top;
        g_vm_defer_top = d->next;   // pop BEFORE running so the defer body's scope sees the right base
        g_vm_defer_depth -= 1;
        tk_vm_exec_block(d->stmt->as.defer_stmt.body, d->stmt->as.defer_stmt.nbody, env);
        tk_free0(d);
    }
}

static tk_flow tk_vm_exec_block(const tk_tstatement *body, size_t n, tk_venv *env) {
    return tk_vm_exec_block_ex(body, n, env, /*is_fn_body=*/false);
}

// (W9.3 part 2) `is_fn_body` marks the FUNCTION-BODY scope: its trailing value-carrying expr-statement
// is treated like `return <expr>` for ordering — the body's defers fire BEFORE the trailing expr is
// evaluated (mirroring codegen's emit_block_tail → emit_exprstmt_tail: emit_defers(base 0) BEFORE the
// implicit trailing `return`). Inner scopes (loop body, statement-position arm) discard their trailing
// value, so they keep the original "run all statements, THEN drain" order — unchanged, already matches.
static tk_flow tk_vm_exec_block_ex(const tk_tstatement *body, size_t n, tk_venv *env, bool is_fn_body) {
    size_t base = g_vm_defer_depth;   // (W9.3 part 2) this scope's defer base (a DEPTH mark)
    tk_flow last = flow_normal();
    for (size_t i = 0; i < n; i += 1) {
        // (W9.3 part 2) Function-body TRAILING value: if the last statement is a value-carrying
        // expr-statement (not an `if`/`match` taken as control flow — those self-route), fire this
        // body's defers FIRST, then evaluate it as the return value. This matches native ordering.
        bool is_last = (i + 1 == n);
        if (is_fn_body && is_last && body[i].tag == TK_TSTMT_EXPR
            && body[i].as.expr_stmt.expr.tag != TK_TEXPR_IF
            && body[i].as.expr_stmt.expr.tag != TK_TEXPR_MATCH) {
            vm_drain_defers_to_depth(base, env);   // fire the body's defers BEFORE the trailing expr
            tk_flow tl = exec_stmt(&body[i], env);
            return tl;   // its value rides the NORMAL flow; defers already drained to `base`
        }
        tk_flow fl = exec_stmt(&body[i], env);
        if (fl.kind != TK_FLOW_NORMAL) {            // non-local exit (return/break/continue)
            vm_drain_defers_to_depth(base, env);    // (W9.3) fire THIS block's defers, then propagate
            return fl;
        }
        last = fl;   // W5 — keep the NORMAL value so the block's TRAILING expression is its value (B.20)
    }
    vm_drain_defers_to_depth(base, env);   // (W9.3) normal fall-through: fire THIS block's defers LIFO
    return last;
}

// W5 — run an `if` as control flow: eval the cond, run the taken branch's block, and return
// its flow. The branch's trailing value rides the NORMAL flow (the `if`'s value); a `return`/
// `break`/`continue` inside the branch propagates out. An `if`-without-`else` taken false → no value.
static tk_flow exec_if(const tk_texpr *e, tk_venv *env) {
    tk_value c = tk_vm_eval_expr(e->as.if_expr.cond, env);
    if (c.tag != TK_VAL_BOOL) vm_unsupported("if condition is not a bool (internal: checker should reject)");
    // D3-branch: outcome 0 = then taken, 1 = else/skip taken (no-op unless coverage on).
    if (c.as.b) { tk_cov_branch(e->line, e->col, 0); return tk_vm_exec_block(e->as.if_expr.then_blk, e->as.if_expr.nthen, env); }
    tk_cov_branch(e->line, e->col, 1);
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
                tk_cov_branch(e->line, e->col, i);   // D3-branch: arm `i` taken
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

// tk_vm_run_tests — the D2 TEST RUNNER (`teko test`). Run every `#test` function (zero-arg,
// void) in the merged program, fail-fast: a failed assertion panics (aborts) from inside the
// VM after the running test's name was printed. All pass → print the count, return 0. An empty
// suite is not a failure. (Mirrors vm.tks run_tests.)
// count_prod_fns — coverage DENOMINATOR: production (non-`#test`) functions, excluding vm.tks
// (namespace "teko::vm"). The vm.tks functions implement the NATIVE VM (only exercised by
// `bin/teko test .`); they are never called via find_function during `build/teko test .`.
// Including them would permanently suppress coverage below 80% with no way to test them here.
static uint64_t count_prod_fns(tk_tprogram prog) {
    uint64_t n = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        if (f.is_test) continue;
        // exclude vm.tks (namespace "teko::vm") — only testable by the native binary
        static const char vm_ns[] = "teko::vm";
        if (f.namespace.len == sizeof(vm_ns) - 1 &&
            memcmp(f.namespace.ptr, vm_ns, sizeof(vm_ns) - 1) == 0) continue;
        n += 1;
    }
    return n;
}

// tk_vm_coverage_pct — function-level coverage % from the last run (the cov sink still holds its
// marks). 100 when there are no production functions. (Mirrors vm.tks coverage_pct.)
uint64_t tk_vm_coverage_pct(tk_tprogram prog) {
    uint64_t total = count_prod_fns(prog);
    if (total == 0) return 100;
    return tk_cov_distinct() * 100 / total;
}

// ---------------------------------------------------------------------------
// D3-branch — Cobertura branch-coverage report (mirrors vm.tks cov_cobertura + cov_branches_*). A
// branch SITE is one `if` (2 outcomes) / `match` (narms outcomes); we statically enumerate every
// site per production fn and query tk_cov_branch_hit per outcome. Single-quoted XML attributes.
// ---------------------------------------------------------------------------
typedef struct { char *p; size_t len, cap; } cov_sb;
static void cov_sb_bytes(cov_sb *b, const char *s, size_t n) {
    // grow when full OR when the buffer is still unallocated (b->p == NULL ⇒ first use). The explicit
    // NULL check makes the post-grow `b->p` provably non-NULL for the trailing memcpy (closes a
    // clang-analyzer core.NullDereference path; behaviour is unchanged — a NULL p only ever coincides
    // with cap == 0, which already forces the grow).
    if (b->p == NULL || b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 8192;
        while (nc < b->len + n + 1) nc *= 2;
        char *g = (char *)tk_alloc(nc); if (!g) abort();
        if (b->len) memcpy(g, b->p, b->len);
        b->p = g; b->cap = nc;
    }
    memcpy(b->p + b->len, s, n); b->len += n; b->p[b->len] = 0;
}
static void cov_sb_puts(cov_sb *b, const char *s) { cov_sb_bytes(b, s, strlen(s)); }
static void cov_sb_str(cov_sb *b, tk_str s) { cov_sb_bytes(b, (const char *)s.ptr, s.len); }
static void cov_sb_putu(cov_sb *b, uint64_t v) { char t[24]; int n = snprintf(t, sizeof t, "%llu", (unsigned long long)v); cov_sb_bytes(b, t, (size_t)n); }
static void cov_sb_rate(cov_sb *b, uint64_t cov, uint64_t val) {
    if (val == 0) { cov_sb_puts(b, "1.0"); return; }
    uint64_t m = cov * 1000 / val;
    if (m >= 1000) { cov_sb_puts(b, "1.0"); return; }
    char t[16]; int n = snprintf(t, sizeof t, "0.%03llu", (unsigned long long)m); cov_sb_bytes(b, t, (size_t)n);
}

typedef struct { uint32_t line, col; uint64_t n; } cov_site;
typedef struct { cov_site *p; size_t len, cap; } cov_sites;
typedef struct { uint32_t *p; size_t len, cap; } cov_lines_t;
typedef struct { cov_sites sites; cov_lines_t lines; } cov_walk_t;   // branch sites + DISTINCT executable lines
static void cov_site_add(cov_sites *s, uint32_t line, uint32_t col, uint64_t n) {
    if (s->len == s->cap) {
        size_t nc = s->cap ? s->cap * 2 : 16;
        cov_site *g = (cov_site *)tk_alloc(nc * sizeof *g); if (!g) abort();
        if (s->len) memcpy(g, s->p, s->len * sizeof *g);
        s->p = g; s->cap = nc;
    }
    s->p[s->len].line = line; s->p[s->len].col = col; s->p[s->len].n = n; s->len += 1;
}
static void cov_line_add(cov_lines_t *l, uint32_t line) {   // distinct, skip 0 (unpositioned)
    if (line == 0) return;
    for (size_t i = 0; i < l->len; i += 1) if (l->p[i] == line) return;
    if (l->len == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 16;
        uint32_t *g = (uint32_t *)tk_alloc(nc * sizeof *g); if (!g) abort();
        if (l->len) memcpy(g, l->p, l->len * sizeof *g);
        l->p = g; l->cap = nc;
    }
    l->p[l->len++] = line;
}
static void cov_walk_block(const tk_tstatement *b, size_t n, cov_walk_t *w);
static void cov_walk_expr(const tk_texpr *e, cov_walk_t *w) {
    if (!e) return;
    cov_line_add(&w->lines, e->line);   // line coverage — every expr's source line
    switch (e->tag) {
        case TK_TEXPR_IF:
            cov_site_add(&w->sites, e->line, e->col, 2);
            cov_walk_expr(e->as.if_expr.cond, w);
            cov_walk_block(e->as.if_expr.then_blk, e->as.if_expr.nthen, w);
            if (e->as.if_expr.has_else) cov_walk_block(e->as.if_expr.else_blk, e->as.if_expr.nelse, w);
            break;
        case TK_TEXPR_MATCH:
            cov_site_add(&w->sites, e->line, e->col, e->as.match_expr.narms);
            cov_walk_expr(e->as.match_expr.subject, w);
            for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
                const tk_tarm *a = &e->as.match_expr.arms[i];
                if (a->has_when) cov_walk_expr(a->guard, w);
                cov_walk_block(a->body, a->nbody, w);
            }
            break;
        case TK_TEXPR_BINARY: cov_walk_expr(e->as.binary.left, w); cov_walk_expr(e->as.binary.right, w); break;
        case TK_TEXPR_UNARY:  cov_walk_expr(e->as.unary.operand, w); break;
        case TK_TEXPR_COMPARE:
            cov_walk_expr(e->as.compare.first, w);
            for (size_t i = 0; i < e->as.compare.nrest; i += 1) cov_walk_expr(e->as.compare.rest[i].operand, w);
            break;
        case TK_TEXPR_CALL:
            for (size_t i = 0; i < e->as.call.nargs; i += 1) cov_walk_expr(&e->as.call.args[i], w);
            break;
        case TK_TEXPR_CAST: cov_walk_expr(e->as.cast.expr, w); break;
        case TK_TEXPR_FIELD_ACCESS: cov_walk_expr(e->as.field_access.receiver, w); break;
        case TK_TEXPR_SAFE_FIELD_ACCESS: cov_walk_expr(e->as.safe_field_access.receiver, w); break;
        case TK_TEXPR_COALESCE: cov_walk_expr(e->as.coalesce.left, w); cov_walk_expr(e->as.coalesce.right, w); break;
        case TK_TEXPR_INDEX: cov_walk_expr(e->as.index.receiver, w); cov_walk_expr(e->as.index.index, w); break;
        case TK_TEXPR_STRUCT_INIT:
            for (size_t i = 0; i < e->as.struct_init.nfields; i += 1) cov_walk_expr(&e->as.struct_init.field_vals[i], w);
            break;
        case TK_TEXPR_INTERP:
            for (size_t i = 0; i < e->as.interp.nholes; i += 1) {
                cov_walk_expr(&e->as.interp.holes[i], w);
                if (e->as.interp.specs && e->as.interp.specs[i].kind == TK_FSPEC_DYNAMIC)
                    for (size_t k = 0; k < e->as.interp.specs[i].ndyn_args; k++)
                        cov_walk_expr(&e->as.interp.specs[i].dyn_args[k], w);
            }
            break;
        case TK_TEXPR_IN:
            cov_walk_expr(e->as.in_expr.lhs, w);
            for (size_t i = 0; i < e->as.in_expr.nelems; i += 1) cov_walk_expr(&e->as.in_expr.elems[i], w);
            break;
        case TK_TEXPR_ARRAY:
            for (size_t i = 0; i < e->as.array.nelements; i += 1) cov_walk_expr(&e->as.array.elements[i], w);
            break;
        default: break;   // leaves (number/var/str/byte/bool/null/path) — line already added
    }
}
static void cov_walk_stmt(const tk_tstatement *st, cov_walk_t *w) {
    switch (st->tag) {
        case TK_TSTMT_BINDING: cov_walk_expr(&st->as.binding.value, w); break;
        case TK_TSTMT_ASSIGN:  cov_walk_expr(&st->as.assign.value, w); break;
        case TK_TSTMT_RETURN:  if (st->as.ret.has_value) cov_walk_expr(&st->as.ret.value, w); break;
        case TK_TSTMT_LOOP:    cov_walk_block(st->as.loop_stmt.body, st->as.loop_stmt.nbody, w); break;
        case TK_TSTMT_EXPR:    cov_walk_expr(&st->as.expr_stmt.expr, w); break;
        case TK_TSTMT_DEFER:   cov_walk_block(st->as.defer_stmt.body, st->as.defer_stmt.nbody, w); break;
        default: break;   // break/continue
    }
}
static void cov_walk_block(const tk_tstatement *b, size_t n, cov_walk_t *w) {
    for (size_t i = 0; i < n; i += 1) cov_walk_stmt(&b[i], w);
}
// branch taken/total over the sites on source line `ln` (a line may host >1 if/match).
static void cov_line_branch(const cov_sites *s, uint64_t fn, uint32_t ln, uint64_t *taken, uint64_t *total) {
    *taken = 0; *total = 0;
    for (size_t k = 0; k < s->len; k += 1) {
        if (s->p[k].line != ln) continue;
        for (uint64_t o = 0; o < s->p[k].n; o += 1)
            if (tk_cov_branch_hit(fn, s->p[k].line, s->p[k].col, o)) *taken += 1;
        *total += s->p[k].n;
    }
}

// is this a production (non-test, non-vm.tks) function? (the coverage universe)
static bool cov_is_prod(tk_tfunction f) {
    static const char vm_ns[] = "teko::vm";
    if (f.is_test) return false;
    if (f.namespace.len == sizeof(vm_ns) - 1 && memcmp(f.namespace.ptr, vm_ns, sizeof(vm_ns) - 1) == 0) return false;
    return true;
}

static const char *cov_cobertura(tk_tprogram prog) {
    cov_sb body = { 0 };
    uint64_t lines_cov = 0, lines_val = 0, br_cov = 0, br_val = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        if (!cov_is_prod(f)) continue;
        uint64_t fnhit = tk_cov_is_marked(i) ? 1 : 0;
        cov_walk_t w = { 0 };
        cov_walk_block(f.body, f.nbody, &w);
        // LINE coverage — one <line> per distinct executable body line (branch lines also annotated).
        cov_sb llines = { 0 };
        uint64_t flc = 0, flv = 0;
        for (size_t k = 0; k < w.lines.len; k += 1) {
            uint32_t ln = w.lines.p[k];
            uint64_t lhit = tk_cov_line_hit(i, ln) ? 1 : 0;
            flv += 1; flc += lhit;
            uint64_t bt, bn; cov_line_branch(&w.sites, i, ln, &bt, &bn);
            cov_sb_puts(&llines, "          <line number='"); cov_sb_putu(&llines, ln);
            cov_sb_puts(&llines, "' hits='"); cov_sb_putu(&llines, lhit);
            if (bn > 0) {
                cov_sb_puts(&llines, "' branch='true' condition-coverage='"); cov_sb_putu(&llines, bt * 100 / bn);
                cov_sb_puts(&llines, "% ("); cov_sb_putu(&llines, bt); cov_sb_puts(&llines, "/"); cov_sb_putu(&llines, bn);
                cov_sb_puts(&llines, ")'/>\n");
            } else {
                cov_sb_puts(&llines, "'/>\n");
            }
        }
        lines_cov += flc; lines_val += flv;
        // BRANCH coverage — taken/total over every site.
        uint64_t fbc = 0, fbv = 0;
        for (size_t k = 0; k < w.sites.len; k += 1) {
            for (uint64_t o = 0; o < w.sites.p[k].n; o += 1)
                if (tk_cov_branch_hit(i, w.sites.p[k].line, w.sites.p[k].col, o)) fbc += 1;
            fbv += w.sites.p[k].n;
        }
        br_cov += fbc; br_val += fbv;
        cov_sb_puts(&body, "      <class name='");
        if (f.namespace.len) { cov_sb_str(&body, f.namespace); cov_sb_puts(&body, "::"); }
        cov_sb_str(&body, f.name);
        cov_sb_puts(&body, "' filename='"); cov_sb_str(&body, f.file);
        cov_sb_puts(&body, "' line-rate='"); cov_sb_rate(&body, flc, flv);
        cov_sb_puts(&body, "' branch-rate='"); cov_sb_rate(&body, fbc, fbv);
        cov_sb_puts(&body, "' complexity='0'>\n        <methods><method name='"); cov_sb_str(&body, f.name);
        cov_sb_puts(&body, "' signature='()V' line-rate='"); cov_sb_rate(&body, flc, flv);
        cov_sb_puts(&body, "' branch-rate='"); cov_sb_rate(&body, fbc, fbv);
        cov_sb_puts(&body, "'><lines><line number='"); cov_sb_putu(&body, f.line);
        cov_sb_puts(&body, "' hits='"); cov_sb_putu(&body, fnhit);
        cov_sb_puts(&body, "'/></lines></method></methods>\n        <lines>\n");
        if (llines.len) cov_sb_bytes(&body, llines.p, llines.len);
        cov_sb_puts(&body, "        </lines>\n      </class>\n");
    }
    cov_sb out = { 0 };
    cov_sb_puts(&out, "<?xml version='1.0' ?>\n<coverage line-rate='"); cov_sb_rate(&out, lines_cov, lines_val);
    cov_sb_puts(&out, "' branch-rate='"); cov_sb_rate(&out, br_cov, br_val);
    cov_sb_puts(&out, "' lines-covered='"); cov_sb_putu(&out, lines_cov);
    cov_sb_puts(&out, "' lines-valid='"); cov_sb_putu(&out, lines_val);
    cov_sb_puts(&out, "' branches-covered='"); cov_sb_putu(&out, br_cov);
    cov_sb_puts(&out, "' branches-valid='"); cov_sb_putu(&out, br_val);
    cov_sb_puts(&out, "' complexity='0' version='teko' timestamp='0'>\n  <sources><source>.</source></sources>\n  <packages>\n    <package name='teko' line-rate='");
    cov_sb_rate(&out, lines_cov, lines_val); cov_sb_puts(&out, "' branch-rate='"); cov_sb_rate(&out, br_cov, br_val);
    cov_sb_puts(&out, "' complexity='0'>\n      <classes>\n");
    if (body.len) cov_sb_bytes(&out, body.p, body.len);
    cov_sb_puts(&out, "      </classes>\n    </package>\n  </packages>\n</coverage>\n");
    return out.p;
}

// (cov, total) counts for the LINE and BRANCH metrics over all production fns. (Mirror vm.tks.)
static void cov_line_counts(tk_tprogram prog, uint64_t *cov, uint64_t *total) {
    *cov = 0; *total = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        if (!cov_is_prod(f)) continue;
        cov_walk_t w = { 0 };
        cov_walk_block(f.body, f.nbody, &w);
        for (size_t k = 0; k < w.lines.len; k += 1) { *total += 1; if (tk_cov_line_hit(i, w.lines.p[k])) *cov += 1; }
    }
}
static void cov_branch_counts(tk_tprogram prog, uint64_t *cov, uint64_t *total) {
    *cov = 0; *total = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        if (!cov_is_prod(f)) continue;
        cov_walk_t w = { 0 };
        cov_walk_block(f.body, f.nbody, &w);
        for (size_t k = 0; k < w.sites.len; k += 1) {
            for (uint64_t o = 0; o < w.sites.p[k].n; o += 1)
                if (tk_cov_branch_hit(i, w.sites.p[k].line, w.sites.p[k].col, o)) *cov += 1;
            *total += w.sites.p[k].n;
        }
    }
}
uint64_t tk_vm_line_coverage_pct(tk_tprogram prog) {
    uint64_t c, t; cov_line_counts(prog, &c, &t); return t == 0 ? 100 : c * 100 / t;
}
// aggregate BRANCH coverage % from the last recorded run (the D4 BRANCH floor reads this).
uint64_t tk_vm_branch_coverage_pct(tk_tprogram prog) {
    uint64_t c, t; cov_branch_counts(prog, &c, &t); return t == 0 ? 100 : c * 100 / t;
}

// run the test suite. `record_branches` enables branch recording (the GATE needs it for the floor,
// even without --coverage); `write_xml` then writes the Cobertura report to `cov_path`.
int tk_vm_run_tests_cov(tk_tprogram prog, bool record_branches, bool write_xml, const char *cov_path) {
    g_prog = prog;
    tk_cov_reset();          // D3 — start a fresh coverage run
    // the GATE records lines+branches even without --coverage (for the floors).
    if (record_branches) { tk_cov_branch_reset(); tk_cov_branches_on(true); tk_cov_line_reset(); tk_cov_lines_on(true); }
    size_t passed = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) {
        if (prog.items[i].tag != TK_TITEM_FUNCTION) continue;
        tk_tfunction f = prog.items[i].as.function;
        if (!f.is_test) continue;
        if (f.namespace.len)
            printf("test %.*s::%.*s ... ", (int)f.namespace.len, (const char *)f.namespace.ptr,
                   (int)f.name.len, (const char *)f.name.ptr);
        else
            printf("test %.*s ... ", (int)f.name.len, (const char *)f.name.ptr);
        fflush(stdout);
        tk_venv fenv = { .head = NULL };
        tk_cov_enter(i);   // attribute the test body's own lines/branches to the test (NOT prod fn 0)
        tk_vm_exec_block(f.body, f.nbody, &fenv);   // a failed assert panics here (fail-fast)
        tk_cov_leave();
        env_free(&fenv);
        printf("ok\n");
        passed += 1;
    }
    if (record_branches) { tk_cov_branches_on(false); tk_cov_lines_on(false); }
    if (passed == 0) {
        printf("teko: no tests (no `#test` functions)\n");
    } else {
        printf("teko: %zu test(s) passed\n", passed);
        printf("teko: coverage %llu%% (%llu/%llu functions)\n",
               (unsigned long long)tk_vm_coverage_pct(prog),
               (unsigned long long)tk_cov_distinct(), (unsigned long long)count_prod_fns(prog));
        if (record_branches) {
            uint64_t lc, lt, bc, bt;
            cov_line_counts(prog, &lc, &lt);
            cov_branch_counts(prog, &bc, &bt);
            printf("teko: coverage %llu%% (%llu/%llu lines)\n",
                   (unsigned long long)(lt == 0 ? 100 : lc * 100 / lt), (unsigned long long)lc, (unsigned long long)lt);
            printf("teko: coverage %llu%% (%llu/%llu branches)\n",
                   (unsigned long long)(bt == 0 ? 100 : bc * 100 / bt), (unsigned long long)bc, (unsigned long long)bt);
        }
    }
    if (write_xml) {
        const char *xml = cov_cobertura(prog);
        tk_str path = { (const tk_byte *)cov_path, strlen(cov_path) };
        tk_str content = { (const tk_byte *)xml, strlen(xml) };
        tk_rt_write_file(path, content);
        printf("teko: wrote coverage report %s\n", cov_path);
    }
    return 0;
}

int tk_vm_run_tests(tk_tprogram prog) { return tk_vm_run_tests_cov(prog, false, false, ""); }
