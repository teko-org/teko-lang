#include "frontend_interop.h"
#include "lexer.h"
#include "parser.h"
#include "parser_ffi.h"
#include "runtime/teko_decimal.h" // Phase 17.F.3: dec-literal parse -> 256-byte decimal-pool blob
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Local token advance (parser.c's is static): current <- peek <- next.
static void fe_advance(Parser* p) {
    p->current_token = p->peek_token;
    p->peek_token = lexer_next_token(p->lexer);
}

// String-literal lexemes arrive with their surrounding delimiters ("…", `…`, '…').
// Return a malloc'd copy with one matching leading/trailing delimiter removed.
static char* strip_quotes(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len >= 2) {
        char q = s[0];
        if ((q == '"' || q == '`' || q == '\'') && s[len - 1] == q) {
            char* out = (char*)malloc(len - 1);
            memcpy(out, s + 1, len - 2);
            out[len - 2] = '\0';
            return out;
        }
    }
    return strdup(s);
}

// Phase 14 (14.G): normalize an integer literal token carrying an optional unit suffix to its
// canonical integer value. TIME units fold to milliseconds (ms/s/m/h/d) — the canonical timespan
// the runtimes use; other units (data/bandwidth) and unit-less literals pass through unchanged.
// The lexer stores the numeric text in `lexeme` (suffix excluded) and the unit in `literal_unit`,
// so e.g. `2s` → lexeme "2" + LIT_UNIT_S → 2000. Used for channel delay args and `wait`/`await`.
static long literal_canonical_value(const Token* t) {
    long v = (long)atoi(t->lexeme);
    switch (t->literal_unit) {
        case LIT_UNIT_MS: return v;
        case LIT_UNIT_S:  return v * 1000L;
        case LIT_UNIT_M:  return v * 60000L;
        case LIT_UNIT_H:  return v * 3600000L;
        case LIT_UNIT_D:  return v * 86400000L;
        default:          return v; // ms-less / data / bandwidth literals are unchanged
    }
}

// Teko-visible callee name -> import-table index.
typedef struct { char* name; int idx; } ImportBinding;

static void bind_add(ImportBinding** binds, int* n, int* cap, const char* name, int idx) {
    if (!name) return;
    if (*n >= *cap) {
        *cap = (*cap == 0) ? 8 : (*cap * 2);
        *binds = (ImportBinding*)realloc(*binds, sizeof(ImportBinding) * (*cap));
    }
    (*binds)[*n].name = strdup(name);
    (*binds)[*n].idx = idx;
    (*n)++;
}

static int bind_lookup(ImportBinding* binds, int n, const char* name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(binds[i].name, name) == 0) return binds[i].idx;
    }
    return -1;
}

// Register one extern function as an import + a name->index binding.
static void register_extern_fn(BytecodeBuffer* buffer, const char* from_lib,
                               const FFIFunctionNode* fn,
                               ImportBinding** binds, int* nb, int* capb) {
    if (!fn || !fn->fn_name) return;
    // from_lib / alias are string-literal lexemes (quoted); the fn_name is a bare
    // identifier. Strip delimiters off the literals before they become ns/name.
    char* ns = from_lib ? strip_quotes(from_lib) : strdup("env");
    char* name = fn->alias ? strip_quotes(fn->alias) : strdup(fn->fn_name);
    int has_result = (fn->return_type && strcmp(fn->return_type, "void") != 0) ? 1 : 0;
    int idx = codegen_li_add_import(buffer, ns, name, fn->param_count, has_result);
    bind_add(binds, nb, capb, fn->fn_name, idx);
    free(ns);
    free(name);
}

static void register_extern(BytecodeBuffer* buffer, const FFIASTNode* node,
                            ImportBinding** binds, int* nb, int* capb) {
    if (!node) return;
    if (node->type == NODE_FFI_FUNCTION) {
        register_extern_fn(buffer, node->from_lib, &node->data.ffi_function, binds, nb, capb);
    } else if (node->type == NODE_FFI_BLOCK) {
        for (int i = 0; i < node->data.ffi_block.function_count; i++) {
            const FFIASTNode* inner = node->data.ffi_block.functions[i];
            if (inner && inner->type == NODE_FFI_FUNCTION) {
                // Block-level `from "ns"` applies to the inner functions.
                register_extern_fn(buffer, node->from_lib, &inner->data.ffi_function, binds, nb, capb);
            }
        }
    }
}

// A call argument: a string literal, an int literal, or a named local (Phase 12).
typedef struct { int is_string; char* sval; int ival; int is_local; int slot; } CallArg;

// Lower a resolved call: args 0..n-2 staged via OP_SETARG, the last left in $w0,
// then OP_CALL_IMPORT. String args are pooled; int args are immediates; local args
// load from their named slot.
static void lower_call(BytecodeBuffer* buffer, int import_index, CallArg* args, int n) {
    for (int i = 0; i < n; i++) {
        if (args[i].is_local) {
            codegen_li_emit_load_local(buffer, args[i].slot);
        } else if (args[i].is_string) {
            int s = codegen_li_add_string_constant(buffer, args[i].sval);
            codegen_li_emit_sconst(buffer, s);
        } else {
            codegen_li_emit_iconst(buffer, args[i].ival);
        }
        if (i != n - 1) codegen_li_emit_setarg(buffer, i);
    }
    codegen_li_emit_call_import(buffer, import_index);
}

// --- @dom/@js intrinsics (FE-E) -------------------------------------------------
// A `@dom.method(args)` / `@js.method(args)` call auto-registers a host import in the
// `dom`/`js` namespace and lowers like an extern call, with two refinements:
//   • a string argument expands to TWO wasm params (ptr, len) — the (ptr,len) ABI the
//     dom.* glue marshals — so n_params is computed from the args, and
//   • the FIRST argument may be a nested `@dom.…()` call (its result handle feeds the
//     outer call), which covers e.g. setText(getElementById("out"), "…").
// Only the leading arg may be nested (one level of staging-slot reuse is safe); other
// args must be string/int literals. The call result (a handle, when any) lands in $w0.

// Which dom/js intrinsics return a value (an i32 handle).
static int intrinsic_has_result(const char* method) {
    return (strcmp(method, "getElementById") == 0 ||
            strcmp(method, "createElement") == 0) ? 1 : 0;
}

// Temp-local allocator (P12-E/F): named-local slots ($v) double as expression and
// nested-arg spill temps. next_temp = next free slot; hw = high-water for the count
// the backend must declare.
typedef struct { int next_temp; int hw; } TempAlloc;

// Lowering context threaded through @dom calls: the current handler param name (an
// identifier arg matching it loads the event arg from $w1), the handler name->table
// slot map (an identifier arg matching one is a function reference -> its table index),
// the named-local table, and the temp allocator (for nested-arg spills, P12-F).
typedef struct {
    const char* param_name; // current handler param, or NULL at top level
    ImportBinding* fns;      // handler name -> table slot (idx = slot)
    int nfns;
    ImportBinding* locals;   // Phase 12: named local -> slot (idx = $v slot)
    int nlocals;
    TempAlloc* ta;           // P12-F: temp-local allocator for nested-arg spills
} LowerCtx;

// A flat "producer" (one wasm param). kind: 0=ICONST payload, 1=SCONST pool idx,
// 2=LOAD the handler param ($w0 <- $w1), 3=LOAD_LOCAL payload (a named local).
typedef struct { int kind; int payload; } Prod;

// ---- Phase 15 (15.A): class registry + member access -----------------------------
// ZERO RUNTIME REFLECTION: a class is a COMPILE-TIME layout (ordered field list) + a method
// table (each method -> a routine table slot, shared with `fn` handlers). Instances are
// teko_object handles; `obj.field` resolves the field's index at compile time (OP_OBJ_GET/SET)
// and `obj.method(args)` resolves the method's slot at compile time (static dispatch via
// OP_CALL_FUNC, passing `self` as arg0). File-static state — one single-threaded compile per
// teko_compile_interop call (reset at entry); avoids threading the registry through every helper.
#define TEKO_MAX_CLASSES        64
#define TEKO_CLASS_MAX_FIELDS   64
#define TEKO_CLASS_MAX_METHODS  64
#define TEKO_CLASS_MAX_TRAITS   8
typedef struct {
    char name[96];
    char fields[TEKO_CLASS_MAX_FIELDS][96]; int nfields;
    char methods[TEKO_CLASS_MAX_METHODS][96];
    int  method_slot[TEKO_CLASS_MAX_METHODS];    // routine table slot (global, dense); -1 = abstract (bodyless)
    int  method_nparams[TEKO_CLASS_MAX_METHODS]; // includes the implicit leading `self`
    int  nmethods;
    // Phase 15 (15.B): the traits/abstract bases this class composes (`class C : T1, T2`), and
    // whether the class itself is `abstract` (a contract that cannot be instantiated directly).
    char traits[TEKO_CLASS_MAX_TRAITS][96]; int ntraits;
    int  is_abstract;
    // The concrete class's dense type_id (= its index in g_class) is used as the vtable row id.
    // Phase 15 (15.C): MONOMORPHIZATION. A generic template `class Box<T>` is NOT registered
    // directly; instead one CONCRETE instance per used type-arg is registered as "Box$Arg" with the
    // type-param substituted. For such an instance: typeparam = "T", mono_arg = "Arg" (the concrete
    // type), tmpl_name = "Box" (the source template to re-lex its body from). Empty for a plain class.
    char typeparam[96];
    char mono_arg[96];
    char tmpl_name[96];
} ClassInfo;
static ClassInfo g_class[TEKO_MAX_CLASSES];
static int g_nclass;

// ---- Phase 15 (15.B): trait registry + global method-id table -----------------------------
// A `trait NAME { fn m(self): T; … }` is a pure CONTRACT — ordered bodyless method names, no
// fields, no slots. A concrete class supplies the bodies. Dynamic dispatch through a trait-typed
// reference resolves (concrete type_id, method_id) -> routine slot via the static vtable runtime.
#define TEKO_MAX_TRAITS        64
#define TEKO_MAX_METHOD_NAMES  256
typedef struct {
    char name[96];
    char methods[TEKO_CLASS_MAX_METHODS][96]; int nmethods;
} TraitInfo;
static TraitInfo g_trait[TEKO_MAX_TRAITS];
static int g_ntrait;

// Global method-NAME -> dense method_id, shared across all classes/traits so a dynamic `g.m()`
// resolves the SAME vtable column regardless of the concrete type behind `g`.
static char g_methodname[TEKO_MAX_METHOD_NAMES][96];
static int  g_nmethodname;

// Set when the OOP surface has a hard compile error (e.g. an ambiguous trait-method collision);
// teko_compile_interop returns non-zero so the CLI/driver reports failure instead of emitting.
static int g_oop_error;

static int trait_find(const char* n) {
    for (int i = 0; i < g_ntrait; i++) if (strcmp(g_trait[i].name, n) == 0) return i;
    return -1;
}
static int trait_method_idx(int ti, const char* m) {
    if (ti < 0 || ti >= g_ntrait) return -1;
    for (int i = 0; i < g_trait[ti].nmethods; i++) if (strcmp(g_trait[ti].methods[i], m) == 0) return i;
    return -1;
}
static int methodid_of(const char* name) {
    for (int i = 0; i < g_nmethodname; i++) if (strcmp(g_methodname[i], name) == 0) return i;
    if (g_nmethodname < TEKO_MAX_METHOD_NAMES) {
        strncpy(g_methodname[g_nmethodname], name, 95); g_methodname[g_nmethodname][95] = '\0';
        return g_nmethodname++;
    }
    return -1;
}
// A concrete class's dense type_id is its index in g_class (the static vtable row).
static int class_type_id(int ci) { return ci; }

// ---- Phase 15 (15.C): generic monomorphization ---------------------------------------------
// Generic templates and the concrete (template, type-arg) instantiations discovered in the source.
// Each distinct pair yields a specialized concrete class "Template$Arg".
#define TEKO_MAX_GENERICS  64
#define TEKO_MAX_GENINST   128
static char g_generic[TEKO_MAX_GENERICS][96]; static int g_ngeneric; // generic class template names
typedef struct { char tmpl[96]; char arg[96]; } GenInst;
static GenInst g_geninst[TEKO_MAX_GENINST];   static int g_ngeninst;
static int is_generic_template(const char* n) {
    for (int i = 0; i < g_ngeneric; i++) if (strcmp(g_generic[i], n) == 0) return 1;
    return 0;
}
static void geninst_add(const char* tmpl, const char* arg) {
    for (int i = 0; i < g_ngeninst; i++)
        if (strcmp(g_geninst[i].tmpl, tmpl) == 0 && strcmp(g_geninst[i].arg, arg) == 0) return; // dedup
    if (g_ngeninst < TEKO_MAX_GENINST) {
        strncpy(g_geninst[g_ngeninst].tmpl, tmpl, 95); g_geninst[g_ngeninst].tmpl[95] = '\0';
        strncpy(g_geninst[g_ngeninst].arg, arg, 95);   g_geninst[g_ngeninst].arg[95]  = '\0';
        g_ngeninst++;
    }
}
// The ACTIVE type-param substitution while a monomorphized method body is lowered/emitted: any
// identifier equal to g_subst_param resolves to g_subst_arg (so `T()` -> `Arg()`, a `T`-typed local
// is `Arg`-typed). Empty when not inside a monomorphized body.
static char g_subst_param[96]; static char g_subst_arg[96];
static const char* resolve_type_name(const char* n) {
    if (g_subst_param[0] && n && strcmp(n, g_subst_param) == 0) return g_subst_arg;
    return n;
}
// The concrete class name produced by the most recent lower_instantiation (so lower_let_stmt can
// bind the local's class even for `Box<Arg>()`, whose type-arg is past the parser's 2-token
// lookahead). Empty when the last RHS was not an instantiation.
static char g_last_inst_class[200];
static int class_find(const char* n); // defined just below (class registry)
// Is the parser at a class instantiation head? `Class()` / `T()` (T substituted) or `Box<…>()`
// (a generic template). The full `Box$Arg` name is resolved by lower_instantiation (it can read the
// type-arg as it consumes it); here we only need to route to it.
static int is_instantiation_head(const Parser* p) {
    if (p->current_token.type != TOKEN_IDENTIFIER) return 0;
    if (p->peek_token.type == TOKEN_LPAREN && class_find(resolve_type_name(p->current_token.lexeme)) >= 0) return 1;
    if (p->peek_token.type == TOKEN_LT && is_generic_template(p->current_token.lexeme)) return 1;
    return 0;
}

// ---- Phase 15 (15.B): FAT trait-typed locals (dynamic dispatch) ---------------------------
// A `let g: Trait = concrete;` reference is FAT — two $v slots: the instance handle, and the
// concrete `type_id` (a COMPILE-TIME CONSTANT, since the RHS's static class is known at each
// assignment). This keeps the object layout unchanged (15.A stays byte-identical). `g.method()`
// loads the runtime type_id from `tid_slot`, does vtable_get(type_id, method_id) -> slot, and
// dispatches via OP_CALL_FUNC with `g`'s handle as self. Per-function scope (reset like localcls).
#define TEKO_MAX_TRAITLOCALS 128
typedef struct { char name[96]; int trait; int handle_slot; int tid_slot; } TraitLocal;
static TraitLocal g_traitlocal[TEKO_MAX_TRAITLOCALS];
static int g_ntraitlocal;
static void traitlocal_reset(void) { g_ntraitlocal = 0; }
static int traitlocal_find(const char* n) {
    for (int i = 0; i < g_ntraitlocal; i++) if (strcmp(g_traitlocal[i].name, n) == 0) return i;
    return -1;
}
static void traitlocal_add(const char* n, int trait, int handle_slot, int tid_slot) {
    int e = traitlocal_find(n);
    if (e >= 0) { g_traitlocal[e].trait = trait; g_traitlocal[e].handle_slot = handle_slot;
                  g_traitlocal[e].tid_slot = tid_slot; return; }
    if (g_ntraitlocal < TEKO_MAX_TRAITLOCALS) {
        strncpy(g_traitlocal[g_ntraitlocal].name, n, 95); g_traitlocal[g_ntraitlocal].name[95] = '\0';
        g_traitlocal[g_ntraitlocal].trait = trait;
        g_traitlocal[g_ntraitlocal].handle_slot = handle_slot;
        g_traitlocal[g_ntraitlocal].tid_slot = tid_slot;
        g_ntraitlocal++;
    }
}

// local var name -> class index (-1 = not a class instance). Per-function scope (reset for $main
// and for each method body), so a method's `self`/locals don't alias $main's instance names.
#define TEKO_MAX_LOCALCLS 256
typedef struct { char name[96]; int cls; } LocalClass;
static LocalClass g_localcls[TEKO_MAX_LOCALCLS];
static int g_nlocalcls;

static int class_find(const char* n) {
    for (int i = 0; i < g_nclass; i++) if (strcmp(g_class[i].name, n) == 0) return i;
    return -1;
}
static int class_field_idx(int ci, const char* f) {
    if (ci < 0 || ci >= g_nclass) return -1;
    ClassInfo* c = &g_class[ci];
    for (int i = 0; i < c->nfields; i++) if (strcmp(c->fields[i], f) == 0) return i;
    return -1;
}
static int class_method_idx(int ci, const char* m) {
    if (ci < 0 || ci >= g_nclass) return -1;
    ClassInfo* c = &g_class[ci];
    for (int i = 0; i < c->nmethods; i++) if (strcmp(c->methods[i], m) == 0) return i;
    return -1;
}
static void localcls_reset(void) { g_nlocalcls = 0; }
static void localcls_set(const char* n, int ci) {
    for (int i = 0; i < g_nlocalcls; i++)
        if (strcmp(g_localcls[i].name, n) == 0) { g_localcls[i].cls = ci; return; }
    if (g_nlocalcls < TEKO_MAX_LOCALCLS) {
        strncpy(g_localcls[g_nlocalcls].name, n, 95); g_localcls[g_nlocalcls].name[95] = '\0';
        g_localcls[g_nlocalcls].cls = ci; g_nlocalcls++;
    }
}
static int localcls_get(const char* n) {
    for (int i = 0; i < g_nlocalcls; i++) if (strcmp(g_localcls[i].name, n) == 0) return g_localcls[i].cls;
    return -1;
}

// Phase 16 (16.B): the static value-type of a lowered expression result in $w0. VT_INT is a
// register-width integer (object/class handles ride here too); VT_STR a NUL-terminated string
// pointer. This drives auto-`to_string` on `+`: when either operand of `+` is a string, the op
// becomes culture-invariant string CONCATENATION (OP_CALL_RUNTIME id 52), with any int operand
// first converted via id 49 (`teko_rt_int_to_string`). The conversion resolves at compile time —
// zero runtime reflection — exactly the hook Phase 15 left for the auto-call machinery.
#define TEKO_VT_INT 0
#define TEKO_VT_STR 1
// Phase 16 (16.D): an object-typed expression result — `TEKO_VT_OBJ_BASE + class_index`. In a
// concat/interpolation it coerces to a string by dispatching the class's (own or inherited)
// `to_string` method (Phase-15 hook, resolved by name → OP_CALL_FUNC), or, when the class defines
// none, by synthesizing the culture-invariant default `ClassName(field0, field1, …)`. Zero runtime
// reflection (the slot / field layout are compile-time constants).
#define TEKO_VT_OBJ_BASE 2

// Phase 17 (17.A): a float-typed expression result — the value lives in the PARALLEL float
// accumulator $f0 (NOT $w0). Encoded as a HIGH SENTINEL (not 2/3) so it never collides with
// TEKO_VT_OBJ_BASE + class_index (an object-typed result of an arbitrary class). This keeps every
// existing `>= TEKO_VT_OBJ_BASE` / `vt - TEKO_VT_OBJ_BASE` site (eval_primary's object case,
// coerce_to_string_in_w0) UNCHANGED — float is checked by exact equality before those range tests.
// (Float→string formatting does not exist until 17.D; a VT_FLOAT reaching a concat is a no-op for
// now, see coerce_to_string_in_w0.)
#define TEKO_VT_FLOAT (1 << 20)
// Phase 17.F (RESERVED — owner-APPROVED, implemented after 17.A–17.E): the value-type sentinel for
// the EXACT base-10 `decimal` type — a FIXED-WIDTH 256-BYTE value (~8B metadata: sign + decimal
// scale/exponent; ~248B base-10 coefficient → ~590 digits, ~38 fractional places; banker's rounding;
// distinct from the binary f64 above; see codegen_li.h's reserved 0x83–0x96 opcode range). 256B does
// NOT fit a register → it flows via memory-slot $d0/$d1 + teko_rt_decimal_* calls. Its OWN high
// sentinel so 17.F never has to renumber VT_FLOAT/VT_OBJ_BASE. NOT used yet (no live token, no
// emission) — claimed only to keep the encoding stable until 17.F goes live.
#define TEKO_VT_DECIMAL (1 << 21)

// Phase 16: the value-type a runtime primitive (OP_CALL_RUNTIME id) leaves in $w0. Almost all
// codec/convert/hash/format ids return a string POINTER (VT_STR); the CHECKED parsers (16.F) return
// an integer (VT_INT) — so `"n=" + convert.parse_int(s)` concatenates correctly (the int is then
// auto-`to_string`'d), not treated as a pointer.
static int runtime_result_vt(int id) {
    // Phase 17.E — parse_float (id 54) leaves a `double` in the float accumulator $f0 (VT_FLOAT), so
    // `convert.parse_float(s) + 1.0` is FLOAT arithmetic and `"x=" + convert.parse_float(s)`
    // auto-`to_string`s through id 50 — exactly the inverse of id 50's f64-arg surface.
    if (id == 54) return TEKO_VT_FLOAT;
    // Phase 17.F.4 — decimal.parse (id 60) leaves the 256-byte decimal value in the $d0 slot
    // (VT_DECIMAL), so `decimal.parse(s) + 1.00dec` is decimal arithmetic and `"x=" + decimal.parse(s)`
    // auto-`to_string`s through id 59. decimal.to_string (id 59) returns a string (VT_STR, the default).
    if (id == 60) return TEKO_VT_DECIMAL;
    return (id == 53 || id == 55) ? TEKO_VT_INT : TEKO_VT_STR; // parse_int / parse_bool
}
static int codec_id_for(const char* lex); // fwd (defined with the codec surface below)

// Set by lower_init_value to the value-type of the initializer it just lowered, so lower_let_stmt /
// lower_reassign can remember a string-typed local (read like g_last_inst_class).
static int g_last_init_vt = TEKO_VT_INT;

// Names of string-typed named locals (so `s` reads as VT_STR in an expression). Per-function scope,
// reset alongside g_localcls. Mirrors the localcls registry (name set membership).
static char g_localstr[TEKO_MAX_LOCALCLS][96];
static int  g_nlocalstr;
static void localstr_reset(void) { g_nlocalstr = 0; }
static void localstr_set(const char* n, int is_str) {
    for (int i = 0; i < g_nlocalstr; i++)
        if (strcmp(g_localstr[i], n) == 0) {            // already known string-typed
            if (!is_str) { // demote: a reassignment to a non-string value
                g_localstr[i][0] = g_localstr[i][1] = '\0'; // tombstone (empty name never matches)
            }
            return;
        }
    if (is_str && g_nlocalstr < TEKO_MAX_LOCALCLS) {
        strncpy(g_localstr[g_nlocalstr], n, 95); g_localstr[g_nlocalstr][95] = '\0';
        g_nlocalstr++;
    }
}
static int localstr_get(const char* n) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_nlocalstr; i++) if (strcmp(g_localstr[i], n) == 0) return 1;
    return 0;
}
// Phase 17 (17.A): names of FLOAT-typed named locals (so `f` reads as VT_FLOAT and routes through
// the float accumulator). Per-function scope, reset alongside g_localstr/g_localcls. Mirrors the
// localstr registry exactly (set membership, demote-on-reassign via tombstone).
static char g_localflt[TEKO_MAX_LOCALCLS][96];
static int  g_nlocalflt;
static void localflt_reset(void) { g_nlocalflt = 0; }
static void localflt_set(const char* n, int is_flt) {
    for (int i = 0; i < g_nlocalflt; i++)
        if (strcmp(g_localflt[i], n) == 0) {
            if (!is_flt) { g_localflt[i][0] = g_localflt[i][1] = '\0'; } // demote (tombstone)
            return;
        }
    if (is_flt && g_nlocalflt < TEKO_MAX_LOCALCLS) {
        strncpy(g_localflt[g_nlocalflt], n, 95); g_localflt[g_nlocalflt][95] = '\0';
        g_nlocalflt++;
    }
}
static int localflt_get(const char* n) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_nlocalflt; i++) if (strcmp(g_localflt[i], n) == 0) return 1;
    return 0;
}
// Phase 17.F.3: names of DECIMAL-typed named locals (so `d` reads as VT_DECIMAL and routes through
// the 256-byte decimal memory-slot accumulator $d0). Per-function scope, reset alongside
// g_localstr/g_localflt/g_localcls. Mirrors the localflt registry exactly (set membership,
// demote-on-reassign via tombstone).
static char g_localdec[TEKO_MAX_LOCALCLS][96];
static int  g_nlocaldec;
static void localdec_reset(void) { g_nlocaldec = 0; }
static void localdec_set(const char* n, int is_dec) {
    for (int i = 0; i < g_nlocaldec; i++)
        if (strcmp(g_localdec[i], n) == 0) {
            if (!is_dec) { g_localdec[i][0] = g_localdec[i][1] = '\0'; } // demote (tombstone)
            return;
        }
    if (is_dec && g_nlocaldec < TEKO_MAX_LOCALCLS) {
        strncpy(g_localdec[g_nlocaldec], n, 95); g_localdec[g_nlocaldec][95] = '\0';
        g_nlocaldec++;
    }
}
static int localdec_get(const char* n) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_nlocaldec; i++) if (strcmp(g_localdec[i], n) == 0) return 1;
    return 0;
}
// Phase 18 (18.A): OPTIONAL named locals — a `let x: ?T = …` reference is FAT (like the 15.B
// trait local): the payload rides in the local's own $v slot (an int/string/handle in $w0), and a
// hidden COMPANION slot `x#opt` holds the 1-word PRESENT flag (1 = a value, 0 = null). The model is
// compacted + zero-overhead — no boxing/heap, just one extra integer slot. The Elvis operator
// `x ?? d` reads the present flag and branches via OP_IF (→ native `je`/arm64 `cbz`/WASM `(if)`),
// exactly the hardware-conditional the memorandum asks for. Per-function scope, reset alongside the
// other local registries. `base_vt` is the payload's value-type (VT_INT for the MVP; VT_STR works
// incidentally — both flow through $w0; float/decimal optional payloads are future work).
typedef struct { char name[96]; int present_slot; int base_vt; } OptLocal;
static OptLocal g_localopt[TEKO_MAX_LOCALCLS];
static int g_nlocalopt;
static void localopt_reset(void) { g_nlocalopt = 0; }
static void localopt_set(const char* n, int present_slot, int base_vt) {
    for (int i = 0; i < g_nlocalopt; i++)
        if (strcmp(g_localopt[i].name, n) == 0) {     // already optional — refresh
            g_localopt[i].present_slot = present_slot; g_localopt[i].base_vt = base_vt; return;
        }
    if (g_nlocalopt < TEKO_MAX_LOCALCLS) {
        strncpy(g_localopt[g_nlocalopt].name, n, 95); g_localopt[g_nlocalopt].name[95] = '\0';
        g_localopt[g_nlocalopt].present_slot = present_slot;
        g_localopt[g_nlocalopt].base_vt = base_vt; g_nlocalopt++;
    }
}
// Return the present-companion slot of optional local `n`, or -1 if `n` is not optional.
static int localopt_present_slot(const char* n) {
    if (!n || !n[0]) return -1;
    for (int i = 0; i < g_nlocalopt; i++) if (strcmp(g_localopt[i].name, n) == 0) return g_localopt[i].present_slot;
    return -1;
}
static int localopt_base_vt(const char* n) {
    if (!n || !n[0]) return TEKO_VT_INT;
    for (int i = 0; i < g_nlocalopt; i++) if (strcmp(g_localopt[i].name, n) == 0) return g_localopt[i].base_vt;
    return TEKO_VT_INT;
}
// Phase 18 (18.E.1): names of ARRAY-typed named locals. An array local's $w0 value IS its handle
// (so it reads as a plain VT_INT integer — passable to functions, storable), but it is tracked here
// so `a[i]` index read/write and `a.len` resolve to OP_ARR_* against the handle. Per-function scope,
// reset alongside the other local registries. Mirrors the localstr/localflt registries (set
// membership, demote-on-reassign via tombstone).
static char g_localarr[TEKO_MAX_LOCALCLS][96];
static int  g_nlocalarr;
static void localarr_reset(void) { g_nlocalarr = 0; }
static void localarr_set(const char* n, int is_arr) {
    for (int i = 0; i < g_nlocalarr; i++)
        if (strcmp(g_localarr[i], n) == 0) {
            if (!is_arr) { g_localarr[i][0] = g_localarr[i][1] = '\0'; } // demote (tombstone)
            return;
        }
    if (is_arr && g_nlocalarr < TEKO_MAX_LOCALCLS) {
        strncpy(g_localarr[g_nlocalarr], n, 95); g_localarr[g_nlocalarr][95] = '\0';
        g_nlocalarr++;
    }
}
static int localarr_get(const char* n) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_nlocalarr; i++) if (strcmp(g_localarr[i], n) == 0) return 1;
    return 0;
}
// Phase 18 (18.E.2): names of TYPED `i32[]` packed-array locals — a SEPARATE registry from g_localarr.
// A typed-array local's $w0 value is its handle (reads as VT_INT) just like a plain array; tracked
// here so `a[i]` read/write, `a.len` and `for x in a` resolve to the OP_IARR_* family (vs OP_ARR_* for
// g_localarr). A name is in AT MOST ONE registry. Same set semantics (demote-on-reassign tombstone).
static char g_localiarr[TEKO_MAX_LOCALCLS][96];
static int  g_nlocaliarr;
static void localiarr_reset(void) { g_nlocaliarr = 0; }
static void localiarr_set(const char* n, int is_arr) {
    for (int i = 0; i < g_nlocaliarr; i++)
        if (strcmp(g_localiarr[i], n) == 0) {
            if (!is_arr) { g_localiarr[i][0] = g_localiarr[i][1] = '\0'; } // demote (tombstone)
            return;
        }
    if (is_arr && g_nlocaliarr < TEKO_MAX_LOCALCLS) {
        strncpy(g_localiarr[g_nlocaliarr], n, 95); g_localiarr[g_nlocaliarr][95] = '\0';
        g_nlocaliarr++;
    }
}
static int localiarr_get(const char* n) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_nlocaliarr; i++) if (strcmp(g_localiarr[i], n) == 0) return 1;
    return 0;
}
// Phase 18 (18.E.2): pick the array op family for a base name — the TYPED OP_IARR_* family if the base
// is a typed `i32[]` local, else the plain OP_ARR_* family. The two registries are disjoint, so a base
// in neither defaults to OP_ARR_* (callers gate on membership first).
static OpCode arr_op_for(const char* base, OpCode op_arr) {
    if (localiarr_get(base)) {
        switch (op_arr) {
            case OP_ARR_NEW: return OP_IARR_NEW;
            case OP_ARR_GET: return OP_IARR_GET;
            case OP_ARR_SET: return OP_IARR_SET;
            case OP_ARR_LEN: return OP_IARR_LEN;
            default: break;
        }
    }
    return op_arr;
}
// Emit one array op (OP_ARR_* OR the OP_IARR_* sibling) via the correct emit helper so the right
// uses_array/uses_iarray gate is set. `op` is already resolved by arr_op_for.
static void emit_arr_op(BytecodeBuffer* b, OpCode op) {
    if (op == OP_IARR_NEW || op == OP_IARR_GET || op == OP_IARR_SET || op == OP_IARR_LEN)
        codegen_li_emit_iarray(b, op);
    else
        codegen_li_emit_array(b, op);
}
// Phase 18 (18.E.2): true if `n` is ANY array local (plain i64 or typed i32) — used by `for x in a`.
static int localanyarr_get(const char* n) { return localarr_get(n) || localiarr_get(n); }

// Phase 18 (18.E.3): SoA (structure-of-arrays) locals. `let s = soa Point[N];` allocates k CONTIGUOUS
// typed-i32 arrays (one per class field of Point), each handle parked in a HIDDEN named local
// `s#f<idx>` (the fat-local pattern, like 15.B's `g#tid` / 18.A's `x#opt`). This registry records the
// SoA local's name, its element class `ci`, and N, so `s[i].field` read/write resolves
// `field`→its index, loads the iarray handle `s#f<idx>`, and dispatches OP_IARR_GET/SET; `s.len` is N;
// and the whole-run accessor `s.field` yields the contiguous i32[] handle (the 18.E.4 SIMD hook). The
// per-field handle SLOT is recovered by name (bind_lookup `s#f<idx>`), so this registry needs no slot
// vector. Per-function scope; reset alongside the other local registries (the 4 sites).
typedef struct { char name[96]; int ci; int n; } LocalSoa;
static LocalSoa g_localsoa[TEKO_MAX_LOCALCLS];
static int g_nlocalsoa;
static void localsoa_reset(void) { g_nlocalsoa = 0; }
static void localsoa_set(const char* n, int ci, int len) {
    for (int i = 0; i < g_nlocalsoa; i++)
        if (strcmp(g_localsoa[i].name, n) == 0) { g_localsoa[i].ci = ci; g_localsoa[i].n = len; return; }
    if (g_nlocalsoa < TEKO_MAX_LOCALCLS) {
        strncpy(g_localsoa[g_nlocalsoa].name, n, 95); g_localsoa[g_nlocalsoa].name[95] = '\0';
        g_localsoa[g_nlocalsoa].ci = ci; g_localsoa[g_nlocalsoa].n = len; g_nlocalsoa++;
    }
}
// Return the SoA registry index for local `n`, or -1. (The caller reads .ci / .n off g_localsoa[idx].)
static int localsoa_find(const char* n) {
    if (!n || !n[0]) return -1;
    for (int i = 0; i < g_nlocalsoa; i++) if (strcmp(g_localsoa[i].name, n) == 0) return i;
    return -1;
}

// Phase 18 (18.E.3): AoS (array-of-objects) element-class tracking. `let a = [Point(), Point(), …];`
// builds an i64 `array` (g_localarr) of OBJECT HANDLES; this companion registry records the ELEMENT
// CLASS so `a[i].field` (index-then-member) resolves the field index at compile time. A single-class
// element array (the element class is inferred from the literal's `ClassName()` elements). The handle
// itself stays a plain array local (byte-identical literal lowering); only the field RESOLUTION uses
// this. Reset with the other registries.
typedef struct { char name[96]; int ci; } LocalAos;
static LocalAos g_localaos[TEKO_MAX_LOCALCLS];
static int g_nlocalaos;
static void localaos_reset(void) { g_nlocalaos = 0; }
static void localaos_set(const char* n, int ci) {
    for (int i = 0; i < g_nlocalaos; i++)
        if (strcmp(g_localaos[i].name, n) == 0) { g_localaos[i].ci = ci; return; }
    if (g_nlocalaos < TEKO_MAX_LOCALCLS) {
        strncpy(g_localaos[g_nlocalaos].name, n, 95); g_localaos[g_nlocalaos].name[95] = '\0';
        g_localaos[g_nlocalaos].ci = ci; g_nlocalaos++;
    }
}
// Return the AoS element class for array local `n`, or -1 (not an AoS-of-class array).
static int localaos_get(const char* n) {
    if (!n || !n[0]) return -1;
    for (int i = 0; i < g_nlocalaos; i++) if (strcmp(g_localaos[i].name, n) == 0) return g_localaos[i].ci;
    return -1;
}
// Phase 18 (18.A): set by eval_primary to describe the OPTIONALITY of the primary it just lowered,
// so eval_expr_prec's Elvis (`??`) can recover the left operand's present flag:
//   -1 = not optional (a plain value → treated as always-present);
//   -2 = the `null` literal (present = 0, a compile-time constant);
//   >=0 = the present-companion slot of an optional local.
static int g_prim_present_slot = -1;
// Phase 18 (18.A): set by eval_primary's `null` case (cleared by lower_init_value before each
// initializer) so lower_let_stmt / lower_reassign know a `let x: ?T = null;` is the null state and
// emit present = 0 (vs present = 1 for any other initializer).
static int g_last_init_is_null = 0;
// Phase 18 (18.E.1): set by lower_array_literal (via lower_init_value's expr path) so lower_let_stmt /
// lower_reassign remember an ARRAY-typed local (so a later `a[i]`/`a.len` resolves to OP_ARR_*).
// Cleared at the top of lower_init_value before each initializer.
static int g_last_init_is_array = 0;
// Phase 18 (18.E.2): set by lower_let_stmt when a `: i32[]` annotation forces the `[…]` RHS to lower
// as a TYPED i32 packed array (OP_IARR_*) — so the local is recorded in g_localiarr (not g_localarr).
// A plain `[…]` without the annotation stays the i64 array (g_last_init_is_array path, byte-identical).
static int g_force_iarray_literal = 0;
// Phase 18 (18.E.2): set by lower_array_literal when it lowered a TYPED i32 packed array, so
// lower_let_stmt records the local in g_localiarr (vs g_localarr). Cleared in lower_init_value.
static int g_last_init_is_iarray = 0;
// Phase 18 (18.E.3): set by lower_member_read when it lowered the SoA WHOLE-RUN accessor `s.field`
// (the field-run i32[] handle in $w0 — the SIMD hook). lower_let_stmt reads it so `let col = s.field;`
// records `col` as an iarray local (g_localiarr) referencing the SAME contiguous packed-i32 handle, so
// `col[i]` / `col.len` (and later `simd.*`) consume it exactly like any typed `i32[]` local. Cleared
// in lower_init_value before each initializer.
static int g_last_member_read_is_iarray = 0;
// Phase 18 (18.E.3): set by lower_array_literal to the element class index when an i64 array literal's
// elements are all `ClassName()` instantiations of one class (an AoS array-of-objects), else -1. The
// let binding records it in g_localaos so a later `a[i].field` resolves the field index at compile
// time. Cleared in lower_init_value before each initializer. A single-class element array (the MVP).
static int g_last_init_aos_class = -1;

// Phase 18 (18.C): `defer <stmt>;` — registration of a scope-closing statement, run in LIFO order at
// scope exit. MVP scope = the `$main` body: each deferred statement's SOURCE is captured (rebuilt
// from its tokens — the lexeme of a string literal keeps its quotes, a dotted ident is one token —
// re-lexable, exactly like a 16.C interpolation hole) and pushed here; at `$main` close (before
// OP_HALT) the stack is DRAINED IN REVERSE through the normal statement dispatcher (lower_one_stmt).
// No new opcode/runtime — the deferred statements lower to ordinary IL, just relocated to scope end.
#define TEKO_MAX_DEFERS 256
static char g_defer[TEKO_MAX_DEFERS][1024];
static int  g_ndefer;
static void defer_reset(void) { g_ndefer = 0; }

// Phase 18 (18.D): `comptime` — compile-time evaluation. A `comptime let NAME = <const-expr>;`
// computes the value AT COMPILE TIME (a constant evaluator over int literals, other comptime
// constants, and +/-/* / % with precedence + parens) and binds NAME as a COMPTIME CONSTANT. No
// IL arithmetic is emitted for the expression — a read of NAME lowers to a single iconst(value), so
// the runtime carries the folded constant, never the computation. This is the metaprogramming
// foundation (compile-time-known values); comptime-free programs are byte-identical.
typedef struct { char name[96]; long value; } ComptimeConst;
static ComptimeConst g_comptime[TEKO_MAX_LOCALCLS];
static int g_ncomptime;
static void comptime_reset(void) { g_ncomptime = 0; }
static int comptime_find(const char* n, long* out) {
    if (!n || !n[0]) return 0;
    for (int i = 0; i < g_ncomptime; i++)
        if (strcmp(g_comptime[i].name, n) == 0) { if (out) *out = g_comptime[i].value; return 1; }
    return 0;
}
static void comptime_set(const char* n, long v) {
    for (int i = 0; i < g_ncomptime; i++)
        if (strcmp(g_comptime[i].name, n) == 0) { g_comptime[i].value = v; return; }
    if (g_ncomptime < TEKO_MAX_LOCALCLS) {
        strncpy(g_comptime[g_ncomptime].name, n, 95); g_comptime[g_ncomptime].name[95] = '\0';
        g_comptime[g_ncomptime].value = v; g_ncomptime++;
    }
}
static int p12_tok_prec(TokenType t);   // fwd (defined with the expression evaluator below)
static int is_compare_tok(TokenType t); // fwd
// Compile-time constant expression evaluator (precedence-climbing, mirroring p12_tok_prec): int
// literals, comptime-constant identifiers, `( … )`, and + - * / %. Returns the folded value and
// consumes the tokens. Unknown identifiers / unsupported forms evaluate to 0 (a degenerate
// compile-time constant, not a runtime fault).
static long comptime_eval(Parser* p, int min_prec) {
    long left;
    if (p->current_token.type == TOKEN_LPAREN) {
        fe_advance(p); left = comptime_eval(p, 1);
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    } else if (p->current_token.type == TOKEN_MINUS) { // unary minus
        fe_advance(p); left = -comptime_eval(p, 4);
    } else if (p->current_token.type == TOKEN_LIT_INT) {
        left = (long)literal_canonical_value(&p->current_token); fe_advance(p);
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        long v = 0; comptime_find(p->current_token.lexeme, &v); left = v; fe_advance(p);
    } else { left = 0; fe_advance(p); }
    while (p12_tok_prec(p->current_token.type) >= min_prec && p12_tok_prec(p->current_token.type) > 0) {
        TokenType op = p->current_token.type; int prec = p12_tok_prec(op);
        if (is_compare_tok(op)) break; // comptime MVP folds arithmetic only (no compares)
        fe_advance(p);
        long right = comptime_eval(p, prec + 1);
        switch (op) {
            case TOKEN_PLUS:  left = left + right; break;
            case TOKEN_MINUS: left = left - right; break;
            case TOKEN_MUL:   left = left * right; break;
            case TOKEN_DIV:   left = right != 0 ? left / right : 0; break;
            case TOKEN_MOD:   left = right != 0 ? left % right : 0; break;
            default: break;
        }
    }
    return left;
}
// Capture the deferred statement at the parser (current token is just past `defer`) as a re-lexable
// source string, advancing past the terminating `;`. Pushes onto the LIFO stack.
static void defer_capture(Parser* p) {
    char buf[1024]; int n = 0; int depth = 0;
    while (p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_SEMICOLON && depth == 0) { fe_advance(p); break; }
        if (p->current_token.type == TOKEN_LPAREN || p->current_token.type == TOKEN_LBRACE ||
            p->current_token.type == TOKEN_LBRACKET) depth++;
        if (p->current_token.type == TOKEN_RPAREN || p->current_token.type == TOKEN_RBRACE ||
            p->current_token.type == TOKEN_RBRACKET) { if (depth > 0) depth--; }
        const char* lx = p->current_token.lexeme ? p->current_token.lexeme : "";
        int ln = (int)strlen(lx);
        if (n + ln + 2 < (int)sizeof(buf)) {
            memcpy(buf + n, lx, ln); n += ln; buf[n++] = ' '; // space-join keeps tokens separable
        }
        fe_advance(p);
    }
    buf[n] = '\0';
    if (g_ndefer < TEKO_MAX_DEFERS) {
        strncpy(g_defer[g_ndefer], buf, sizeof(g_defer[0]) - 1);
        g_defer[g_ndefer][sizeof(g_defer[0]) - 1] = '\0';
        g_ndefer++;
    }
}
// Split a dotted lexeme "base.member" into its two parts. Returns 1 on success (a dot present).
static int dotted_split(const char* lex, char* base, char* member) {
    const char* dot = lex ? strchr(lex, '.') : NULL;
    if (!dot) return 0;
    size_t bl = (size_t)(dot - lex); if (bl >= 96) bl = 95;
    memcpy(base, lex, bl); base[bl] = '\0';
    strncpy(member, dot + 1, 95); member[95] = '\0';
    return 1;
}

// Phase 15 (15.A): skip a generic type-parameter clause `<T, U, …>` (balanced angle brackets,
// nesting tolerated) between a method/fn name and its `(` param list. Methods mirror functions:
// they may be GENERIC. The value model is uniform i32, so a generic method binds its params and
// lowers exactly like a non-generic one in this MVP; per-type monomorphization (substituting T)
// is Phase 15.C. Leaves the parser on the token after the matching `>` (typically `(`).
static void skip_generic_clause(Parser* p) {
    if (p->current_token.type != TOKEN_LT) return;
    int d = 0;
    while (p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_LT) { d++; fe_advance(p); continue; }
        if (p->current_token.type == TOKEN_GT) { d--; fe_advance(p); if (d <= 0) break; continue; }
        fe_advance(p);
    }
}

static int is_dom_macro(const char* lexeme); // defined below
static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p, const LowerCtx* ctx); // recursive

static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p, const LowerCtx* ctx) {
    // current token is the MACRO_IDENT, e.g. "@dom.setText". Split into ns + method.
    char full[128];
    strncpy(full, p->current_token.lexeme, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    char* dot = strchr(full, '.');
    if (full[0] != '@' || !dot) { fe_advance(p); return; }
    char ns[32];
    size_t nslen = (size_t)(dot - (full + 1));
    if (nslen >= sizeof(ns)) nslen = sizeof(ns) - 1;
    memcpy(ns, full + 1, nslen);
    ns[nslen] = '\0';
    char method[96];
    strncpy(method, dot + 1, sizeof(method) - 1);
    method[sizeof(method) - 1] = '\0';

    fe_advance(p); // consume the macro ident
    if (p->current_token.type != TOKEN_LPAREN) return;
    fe_advance(p); // consume '('

    // Collect args as flat producers (string -> ptr+len). A nested `@dom.…()` arg in
    // ANY position (P12-F) is lowered eagerly and its result spilled to a fresh temp
    // local; the producer then LOAD_LOCALs it during staging. So multiple nested handle
    // args work (e.g. appendChild(getElementById(…), createElement(…))).
    Prod prods[32];
    int np = 0;
    int temps_used = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_MACRO_IDENT && is_dom_macro(p->current_token.lexeme) &&
            np + 1 <= 32) {
            lower_intrinsic_call(buffer, p, ctx);              // inner result -> $w0
            int t = (ctx && ctx->ta) ? ctx->ta->next_temp++ : 0;
            if (ctx && ctx->ta && ctx->ta->next_temp > ctx->ta->hw) ctx->ta->hw = ctx->ta->next_temp;
            codegen_li_emit_store_local(buffer, t);            // park the handle in a temp
            prods[np].kind = 3; prods[np].payload = t; np++;   // producer: LOAD_LOCAL temp
            temps_used++;
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        } else if ((p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) && np + 2 <= 32) {
            char* s = strip_quotes(p->current_token.lexeme);
            int idx = codegen_li_add_string_constant(buffer, s);
            int len = (int)strlen(s);
            free(s);
            prods[np].kind = 1; prods[np].payload = idx; np++;   // ptr (SCONST)
            prods[np].kind = 0; prods[np].payload = len; np++;   // len (ICONST)
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_LIT_INT && np + 1 <= 32) {
            prods[np].kind = 0; prods[np].payload = atoi(p->current_token.lexeme); np++;
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_IDENTIFIER && np + 1 <= 32) {
            const char* id = p->current_token.lexeme;
            int lslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, id) : -1;
            if (ctx && ctx->param_name && strcmp(id, ctx->param_name) == 0) {
                prods[np].kind = 2; prods[np].payload = 0; np++;            // LOAD handler param
            } else if (lslot >= 0) {
                prods[np].kind = 3; prods[np].payload = lslot; np++;        // LOAD_LOCAL (named local)
            } else {
                int slot = ctx ? bind_lookup(ctx->fns, ctx->nfns, id) : -1;
                if (slot >= 0) { prods[np].kind = 0; prods[np].payload = slot; np++; } // fn ref -> ICONST slot
                // else: unknown identifier in this subset — produces nothing
            }
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_COMMA) {
            fe_advance(p);
        } else {
            fe_advance(p); // skip unsupported tokens in this subset
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);

    // Emit the producers at slots [0..np-1]; the last param stays in $w0.
    for (int k = 0; k < np; k++) {
        switch (prods[k].kind) {
            case 1: codegen_li_emit_sconst(buffer, prods[k].payload); break;
            case 2: codegen_li_emit_load(buffer); break;
            case 3: codegen_li_emit_load_local(buffer, prods[k].payload); break;
            default: codegen_li_emit_iconst(buffer, prods[k].payload); break;
        }
        if (k < np - 1) codegen_li_emit_setarg(buffer, k);
    }

    // Free the nested-arg temps reserved for this call.
    if (ctx && ctx->ta) ctx->ta->next_temp -= temps_used;

    int import_index = codegen_li_add_import(buffer, ns, method, np, intrinsic_has_result(method));
    codegen_li_emit_call_import(buffer, import_index);
}

static int is_dom_macro(const char* lexeme) {
    return lexeme && (strncmp(lexeme, "@dom.", 5) == 0 || strncmp(lexeme, "@js.", 4) == 0);
}

// --- integer expression parser (FE P12-E) ---------------------------------------
// A precedence-climbing (Pratt) parser for integer arithmetic + comparisons. Each
// binary node spills its left operand to a fresh temporary named-local so arbitrary
// nesting works in the accumulator model: result lands in $w0. Operand/temp slots
// live in the same $v file as named locals (P12-D); `hw` tracks the high-water so the
// backend declares enough locals. Scope: int literals, named locals, parentheses, and
// `+ - * / % == != < <= > >=` (left-assoc). Float / `&&`/`||` are future work.
static int p12_tok_prec(TokenType t) {
    switch (t) {
        case TOKEN_MUL: case TOKEN_DIV: case TOKEN_MOD: return 3;
        case TOKEN_PLUS: case TOKEN_MINUS: return 2;
        case TOKEN_EQ: case TOKEN_NE: case TOKEN_LT:
        case TOKEN_LE: case TOKEN_GT: case TOKEN_GE: return 1;
        default: return 0;
    }
}

static OpCode p12_tok_op(TokenType t) {
    switch (t) {
        case TOKEN_PLUS:  return OP_ADD; case TOKEN_MINUS: return OP_SUB;
        case TOKEN_MUL:   return OP_MUL; case TOKEN_DIV:   return OP_DIV;
        case TOKEN_MOD:   return OP_MOD;
        case TOKEN_EQ:    return OP_EQ;  case TOKEN_NE:    return OP_NE;
        case TOKEN_LT:    return OP_LT;  case TOKEN_LE:    return OP_LE;
        case TOKEN_GT:    return OP_GT;  case TOKEN_GE:    return OP_GE;
        default:          return OP_ADD;
    }
}

// Phase 17 (17.A): map a binary operator token to its FLOAT opcode (parallel to p12_tok_op). Arith
// (+ - * /) → OP_F*; compares (== != < <= > >=) → OP_F* compare (result i32 → VT_INT).
static OpCode p12_tok_fop(TokenType t) {
    switch (t) {
        case TOKEN_PLUS:  return OP_FADD; case TOKEN_MINUS: return OP_FSUB;
        case TOKEN_MUL:   return OP_FMUL; case TOKEN_DIV:   return OP_FDIV;
        case TOKEN_MOD:   return OP_FMOD; // Phase 17 (17.B): float `%` (fmod, remainder toward zero)
        case TOKEN_EQ:    return OP_FEQ;  case TOKEN_NE:    return OP_FNE;
        case TOKEN_LT:    return OP_FLT;  case TOKEN_LE:    return OP_FLE;
        case TOKEN_GT:    return OP_FGT;  case TOKEN_GE:    return OP_FGE;
        default:          return OP_FADD; // (unreachable for the routed binary float operators)
    }
}
// Phase 17 (17.A): a comparison operator yields an i32 0/1 (VT_INT) regardless of operand types.
static int is_compare_tok(TokenType t) {
    return t == TOKEN_EQ || t == TOKEN_NE || t == TOKEN_LT ||
           t == TOKEN_LE || t == TOKEN_GT || t == TOKEN_GE;
}
// Phase 17.F.3: map a binary operator token to its DECIMAL opcode (parallel to p12_tok_fop). Arith
// (+ - * / %) → OP_D*; compares (== != < <= > >=) → OP_D* compare (result i32 → VT_INT).
static OpCode p12_tok_dop(TokenType t) {
    switch (t) {
        case TOKEN_PLUS:  return OP_DADD; case TOKEN_MINUS: return OP_DSUB;
        case TOKEN_MUL:   return OP_DMUL; case TOKEN_DIV:   return OP_DDIV;
        case TOKEN_MOD:   return OP_DMOD; // Python Decimal.__mod__
        case TOKEN_EQ:    return OP_DEQ;  case TOKEN_NE:    return OP_DNE;
        case TOKEN_LT:    return OP_DLT;  case TOKEN_LE:    return OP_DLE;
        case TOKEN_GT:    return OP_DGT;  case TOKEN_GE:    return OP_DGE;
        default:          return OP_DADD; // (unreachable for the routed binary decimal operators)
    }
}

// Phase 16 (16.B): the expression evaluators now RETURN the result's value-type (TEKO_VT_*), so a
// binary `+` with a string operand lowers to culture-invariant concatenation (auto-`to_string`).
static int eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                          int min_prec, TempAlloc* ta);
static void lower_instantiation(BytecodeBuffer* b, Parser* p); // fwd (18.E.3: AoS object literals)
static int is_codec_head(const Parser* p);   // fwd (defined with the codec surface below)
static void lower_base_codec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
static int lower_interp_string(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd (16.C)
// Phase 17 (17.B): `convert.to_int` / `convert.to_float` — direct-opcode checked int↔float casts
// whose argument is a full EXPRESSION (not a codec literal/local), so they take a dedicated path
// (NOT codec_id_for / lower_base_codec). is_floatcast_head claims the dotted ident BEFORE the codec
// check; lower_floatcast evaluates the inner expr and emits OP_F2I (to_int) / OP_I2F (to_float),
// returning the result value-type (VT_INT / VT_FLOAT).
static int is_floatcast_head(const Parser* p); // fwd (defined below)
static int lower_floatcast(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// Phase 17.F.4: the decimal.to_string / decimal.parse surface (ids 59/60).
static int is_decimalsurf_head(const Parser* p); // fwd
static int lower_decimalsurf(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// Phase 18 (18.E.4): the `simd.sum(<expr>)` head — a dotted-identifier surface (the lexer folds
// `simd.sum` into one IDENTIFIER) lowering to OP_SIMD_SUM (the REAL per-ISA vector reduction).
static int is_simd_head(const Parser* p);        // fwd
static int lower_simd(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// Phase 18 (18.B): safe-navigation `obj?.member` / `obj?.method(args)` — a null-propagating member
// access over an optional object. Defined after the member-access helpers; forward-declared so
// eval_primary can claim the `IDENTIFIER ?. …` form. Returns the member-result value-type and exposes
// the chain's present flag (g_prim_present_slot) for a following Elvis `??`.
static int lower_safe_nav(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// Phase 15 (15.A): `obj.method(args)` STATIC method call as a VALUE — defined far below with the
// class surface, but forward-declared here so the shared expression evaluators (eval_primary, the
// codec-arg + extern-arg argument paths) can claim a method-call sub-expression and lower it to
// OP_CALL_FUNC -> result in $w0. Without this, a method call in any expression position (an extern
// argument `emit(p.m())`, a sub-expression `p.m() + 1`, an interpolation hole `"{p.m()}"`) fell
// through to the bare-identifier branch, which emitted iconst 0 and orphaned the `()`.
static int lower_member_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// True iff the current token is a class-typed `obj.method(` call head (a dotted identifier whose
// base is a known class-typed local and whose member is one of that class's methods, peek `(`). A
// pure predicate (no token consumption) used to route such a head to lower_member_call.
static int is_member_call_head(const Parser* p) {
    if (!p || p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    int ci = localcls_get(base);
    if (ci < 0) return 0;
    return class_method_idx(ci, member) >= 0;
}
// Phase 15 (15.B): `g.method(args)` DYNAMIC trait dispatch as a VALUE — the ctx-only core of
// lower_trait_dispatch (forward-declared; defined far below with the class surface). The arg paths
// already position ctx->locals before evaluating, so no env_sync is needed here — the LowerEnv*
// wrapper lower_trait_dispatch adds only that sync. Forward-declared so the shared evaluators
// (eval_primary, the codec-arg + extern-arg paths) can claim a trait-dispatch sub-expression and
// lower it to vtable_get + OP_CALL_FUNC -> result in $w0, exactly like the static-method fix.
static int lower_trait_dispatch_ctx(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // fwd
// True iff the current token is a fat trait-typed `g.method(` dispatch head (a dotted identifier
// whose base is a known trait-typed local and whose member is one of that trait's methods, peek
// `(`). A pure predicate (no consumption); mutually exclusive with is_member_call_head (a base is
// either a class-typed local or a trait-typed local, never both).
static int is_trait_dispatch_head(const Parser* p) {
    if (!p || p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    int tl = traitlocal_find(base);
    if (tl < 0) return 0;
    return trait_method_idx(g_traitlocal[tl].trait, member) >= 0;
}
// Phase 16 (16.D): coerce the operand VALUE in $w0 (per its value-type `vt`) to a string pointer in
// $w0 — int via to_string (id 49), string unchanged, object via its to_string / synthesized default.
static void coerce_to_string_in_w0(BytecodeBuffer* b, const LowerCtx* ctx, int vt); // fwd (16.D)

// Phase 16 (16.C): a STRING-INTERPOLATION literal — a `"…"` whose (unescaped) content contains a
// `{expr}` hole (e.g. `"x = {n}"`, `"{p}"`). `{{`/`}}` are literal braces. The interop frontend
// treats a brace-bearing double-quoted literal as interpolated (matching the owner's `"{p}"`
// surface); a literal `{` is written `{{`. (The full-AST backtick interpolation subsystem in
// parser_string.c is separate and unchanged.)
static int strlit_is_interp(const char* lexeme) {
    if (!lexeme) return 0;
    // Any `{` (a `{expr}` hole, or the `{{` escape) routes through interpolation lowering, which
    // also unescapes `{{`/`}}`. The `}}` escape is honored even with no `{` present. A literal brace
    // in a double-quoted string is written `{{`/`}}` (a lone `{` opens an interpolation hole).
    for (const char* s = lexeme; *s; s++) {
        if (s[0] == '{') return 1;
        if (s[0] == '}' && s[1] == '}') return 1;
    }
    return 0;
}

// Phase 15 (15.A): `obj.field` READ as an expression primary -> OP_OBJ_GET(handle, idx) -> $w0.
// `obj` must be a class-typed local and `member` one of its fields (resolved at compile time).
// Returns 1 if it consumed a member read, 0 otherwise (a method call `obj.method(` or a non-class
// dotted ident — e.g. duplex.* — is left for the caller). Current token is the dotted IDENTIFIER.
static int lower_member_read(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type != TOKEN_IDENTIFIER) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    // Phase 18 (18.E.1): `a.len` on an array local -> OP_ARR_LEN (O(1) metadata) -> $w0. Claimed
    // BEFORE the class-field path (an array local is never a class local, so no ambiguity).
    if (localanyarr_get(base) && strcmp(member, "len") == 0) {
        int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, base) : -1;
        if (hslot < 0) return 0;
        codegen_li_emit_load_local(b, hslot);  // $w0 = handle
        emit_arr_op(b, arr_op_for(base, OP_ARR_LEN));  // $w0 = length (i64 OR typed-i32 family)
        fe_advance(p);
        return 1;
    }
    // Phase 18 (18.E.3): a SoA member read with NO index — `s.len` (the element count N) or the
    // WHOLE-RUN accessor `s.field` (the contiguous packed-i32 field-run handle — the 18.E.4 SIMD hook).
    {
        int si = localsoa_find(base);
        if (si >= 0) {
            if (strcmp(member, "len") == 0) {
                codegen_li_emit_iconst(b, g_localsoa[si].n);   // $w0 = N (compile-time constant)
                fe_advance(p);
                return 1;
            }
            int fidx = class_field_idx(g_localsoa[si].ci, member);
            if (fidx >= 0) {
                // `s.field` -> the field-run handle (a usable i32[] — `let col = s.field` makes `col`
                // an iarray local; `col[i]`/`col.len`/`simd.*` then operate on this contiguous run).
                char fnm[120]; snprintf(fnm, sizeof(fnm), "%s#f%d", base, fidx);
                int fslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, fnm) : -1;
                if (fslot >= 0) codegen_li_emit_load_local(b, fslot); else codegen_li_emit_iconst(b, 0);
                g_last_member_read_is_iarray = 1; // signal the let binding to track `col` as i32[]
                fe_advance(p);
                return 1;
            }
        }
    }
    int ci = localcls_get(base);
    if (ci < 0) return 0;
    int fidx = class_field_idx(ci, member);
    if (fidx < 0) return 0; // not a field (could be a method — handled by the call path)
    int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, base) : -1;
    if (hslot < 0) return 0;
    codegen_li_emit_load_local(b, hslot);  // $w0 = handle
    codegen_li_emit_setarg(b, 0);          // $a0 = handle
    codegen_li_emit_iconst(b, fidx);       // $w0 = field index (compile-time constant)
    codegen_li_emit_object(b, OP_OBJ_GET); // $w0 = field value
    fe_advance(p);
    return 1;
}

// Phase 18 (18.E.1): an ARRAY LITERAL `[e0, e1, …]` as a primary / initializer. Mirrors the
// object-new arg convention: ARR_NEW(n) -> handle in $w0, spill the handle to a temp, then for each
// element i stage (handle, i) into $a0/$a1, evaluate the element into $w0, OP_ARR_SET; finally
// reload the handle into $w0 so the whole literal evaluates to the array handle (a VT_INT value).
// Current token is the `[`. Consumes through the matching `]`. Returns the handle in $w0.
// (eval_expr_prec is forward-declared above; it is the element/index evaluator.)
static void lower_array_literal(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    // Phase 18 (18.E.2): a `: i32[]` annotation (g_force_iarray_literal, set by lower_let_stmt) makes
    // this literal a TYPED i32 PACKED array (OP_IARR_*); otherwise it is the plain i64 array (OP_ARR_*,
    // byte-identical to 18.E.1). The flag is consumed (one-shot) so nested literals don't inherit it.
    int typed = g_force_iarray_literal; g_force_iarray_literal = 0;
    OpCode op_new = typed ? OP_IARR_NEW : OP_ARR_NEW;
    OpCode op_set = typed ? OP_IARR_SET : OP_ARR_SET;
    if (typed) g_last_init_is_iarray = 1; else g_last_init_is_array = 1; // mark for the let binding
    fe_advance(p); // consume '['
    // Phase 18 (18.E.3): an AoS array-of-objects — the FIRST element is `ClassName()`. Record the
    // element class so the let binding tracks it (g_localaos) and `a[i].field` resolves the field
    // index at compile time. Only the plain i64 array carries object handles (a typed i32[] cannot).
    if (!typed && p->current_token.type == TOKEN_IDENTIFIER && p->peek_token.type == TOKEN_LPAREN) {
        int ec = class_find(p->current_token.lexeme);
        if (ec >= 0) g_last_init_aos_class = ec;
    }
    // Pre-scan to count the elements (ARR_NEW needs the exact length FIRST). The real parser shares a
    // heap Lexer* with everything else, so we snapshot the lexer STATE (source/cursor/line are plain
    // values) into a LOCAL lexer + a local parser pointing at it, and scan that — leaving the real
    // parser/lexer untouched. Count top-level commas + 1 (unless the list is empty); brackets/parens
    // may nest in an element, so track depth. (Lexemes lex'd during the scan leak, consistent with
    // fe_advance's existing token-overwrite — this is a compile-once tool.)
    Lexer scan_lx = *p->lexer;            // value snapshot of the lexer position
    Parser scan = *p;                     // copy all fields (is_stdlib_compilation etc.)…
    scan.lexer = &scan_lx;                // …then redirect to the LOCAL lexer snapshot
    int n = 0, depth = 0, seen = 0;
    while (scan.current_token.type != TOKEN_EOF) {
        TokenType t = scan.current_token.type;
        if (depth == 0 && t == TOKEN_RBRACKET) break;
        if (t == TOKEN_LBRACKET || t == TOKEN_LPAREN) depth++;
        else if (t == TOKEN_RBRACKET || t == TOKEN_RPAREN) { if (depth > 0) depth--; }
        else if (depth == 0 && t == TOKEN_COMMA) { n++; }
        else if (depth == 0) seen = 1;
        fe_advance(&scan);
    }
    if (seen) n++; // element_count = top-level commas + 1 when non-empty
    // ARR_NEW(n) -> handle; park it in a temp (it is reloaded between every element SET).
    codegen_li_emit_iconst(b, n);
    emit_arr_op(b, op_new); // $w0 = handle (i64 OR typed-i32 family)
    int ht = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    codegen_li_emit_store_local(b, ht);   // temp = handle
    int i = 0;
    while (p->current_token.type != TOKEN_RBRACKET && p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_COMMA) { fe_advance(p); continue; }
        // value FIRST into a scratch temp (an element expr may itself use $a0 via a member/index read)
        int vt = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        eval_expr_prec(b, p, ctx, 1, ta);  // element -> $w0
        codegen_li_emit_store_local(b, vt);
        codegen_li_emit_load_local(b, ht); codegen_li_emit_setarg(b, 0); // $a0 = handle
        codegen_li_emit_iconst(b, i);      codegen_li_emit_setarg(b, 1); // $a1 = index
        codegen_li_emit_load_local(b, vt); // $w0 = element value
        emit_arr_op(b, op_set);
        ta->next_temp--;                   // free the element scratch
        i++;
    }
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    codegen_li_emit_load_local(b, ht);     // $w0 = the array handle (the literal's value)
    ta->next_temp--;                       // free the handle temp
}

// Phase 18 (18.E.1): an INDEX READ `a[i]` as a primary -> OP_ARR_GET(handle, idx) -> $w0. `a` is an
// array local (current token), peek is `[`. Stage handle=$a0, evaluate the index -> $w0, OP_ARR_GET.
// Consumes `a [ <idx> ]`. Returns 1 if consumed. Out-of-range traps fail-loud in the runtime.
static int lower_array_index_read(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    if (!localanyarr_get(p->current_token.lexeme)) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, base) : -1;
    if (hslot < 0) return 0;
    fe_advance(p); // consume the array name
    fe_advance(p); // consume '['
    codegen_li_emit_load_local(b, hslot);  // $w0 = handle
    codegen_li_emit_setarg(b, 0);          // $a0 = handle
    eval_expr_prec(b, p, ctx, 1, ta);      // index -> $w0
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    emit_arr_op(b, arr_op_for(base, OP_ARR_GET));  // $w0 = element value (i64 OR typed-i32 family)
    return 1;
}

// Phase 18 (18.E.3): a SoA INDEX-then-FIELD READ `s[i].field` -> OP_IARR_GET(field-run handle, i).
// `s` is a SoA local (current token), peek is `[`. The k field runs are contiguous packed-i32 arrays
// (one per class field); `field` resolves to its compile-time index, whose run handle lives in the
// hidden local `s#f<idx>`. Stage handle=$a0, evaluate the index into $w0, OP_IARR_GET. Consumes
// `s [ <idx> ] . field`. Returns 1 if consumed (the base is a SoA local + the member is a field of its
// element class), 0 otherwise. Out-of-range traps fail-loud (the packed-array runtime).
static int lower_soa_index_read(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    int si = localsoa_find(p->current_token.lexeme);
    if (si < 0) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int ci = g_localsoa[si].ci;
    fe_advance(p); // consume the SoA name
    fe_advance(p); // consume '['
    // Evaluate the index into a temp FIRST (it may itself stage $a0 via a member/index read), then
    // stage the field-run handle as $a0 and reload the index. (After we resolve the field name below.)
    int it = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    eval_expr_prec(b, p, ctx, 1, ta);          // index -> $w0
    codegen_li_emit_store_local(b, it);        // it = index
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    if (p->current_token.type == TOKEN_DOT) fe_advance(p); // '.'
    int fidx = -1;
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        fidx = class_field_idx(ci, p->current_token.lexeme);
        fe_advance(p);                         // consume the field name
    }
    if (fidx < 0) fidx = 0;
    char fnm[120]; snprintf(fnm, sizeof(fnm), "%s#f%d", base, fidx);
    int fslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, fnm) : -1;
    if (fslot >= 0) codegen_li_emit_load_local(b, fslot); else codegen_li_emit_iconst(b, 0);
    codegen_li_emit_setarg(b, 0);              // $a0 = field-run handle
    codegen_li_emit_load_local(b, it);         // $w0 = index
    codegen_li_emit_iarray(b, OP_IARR_GET);    // $w0 = field cell at i
    ta->next_temp--;                           // free the index temp
    return 1;
}

// Phase 18 (18.E.3): an AoS INDEX-then-FIELD READ `a[i].field` -> OP_ARR_GET(a, i)=handle then
// OP_OBJ_GET(handle, field_idx). `a` is an i64 `array` of object handles whose ELEMENT CLASS is known
// (g_localaos, recorded when the literal was all `ClassName()` instances). The fields are NOT
// contiguous (interleaved per object) — the AoS contrast to SoA. Consumes `a [ <idx> ] . field`.
// Returns 1 if consumed. Out-of-range on the array traps fail-loud (the array runtime).
static int lower_aos_index_read(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    int ci = localaos_get(p->current_token.lexeme);
    if (ci < 0) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, base) : -1;
    if (hslot < 0) return 0;
    fe_advance(p); // consume the array name
    fe_advance(p); // consume '['
    // index into a temp first (may stage $a0), then ARR_GET(handle, i) -> object handle.
    int it = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    eval_expr_prec(b, p, ctx, 1, ta);          // index -> $w0
    codegen_li_emit_store_local(b, it);        // it = index
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    codegen_li_emit_load_local(b, hslot); codegen_li_emit_setarg(b, 0); // $a0 = array handle
    codegen_li_emit_load_local(b, it);                                  // $w0 = index
    codegen_li_emit_array(b, OP_ARR_GET);      // $w0 = element (object handle)
    codegen_li_emit_setarg(b, 0);              // $a0 = object handle
    int fidx = -1;
    if (p->current_token.type == TOKEN_DOT) fe_advance(p); // '.'
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        fidx = class_field_idx(ci, p->current_token.lexeme);
        fe_advance(p);                         // consume the field name
    }
    codegen_li_emit_iconst(b, fidx >= 0 ? fidx : 0); // $w0 = field index
    codegen_li_emit_object(b, OP_OBJ_GET);     // $w0 = field value (interleaved per object — AoS)
    ta->next_temp--;
    return 1;
}

static int eval_primary(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    g_prim_present_slot = -1; // Phase 18 (18.A): default — this primary is not an optional reference
    if (p->current_token.type == TOKEN_LBRACKET) {
        // Phase 18 (18.E.1): an ARRAY LITERAL `[e0, e1, …]` -> ARR_NEW + per-element SET -> handle.
        lower_array_literal(b, p, ctx, ta);
        return TEKO_VT_INT; // an array handle reads as a plain integer value
    } else if (is_instantiation_head(p)) {
        // Phase 18 (18.E.3): a `ClassName()` instantiation as a sub-expression (e.g. an AoS array-of-
        // objects element `[Point(), …]`) -> OP_OBJ_NEW -> the object handle in $w0 (a VT_INT value).
        lower_instantiation(b, p);
        return TEKO_VT_INT;
    } else if (lower_soa_index_read(b, p, ctx, ta)) {
        // Phase 18 (18.E.3): a SoA INDEX-then-FIELD READ `s[i].field` -> OP_IARR_GET on the field run.
        return TEKO_VT_INT;
    } else if (lower_aos_index_read(b, p, ctx, ta)) {
        // Phase 18 (18.E.3): an AoS INDEX-then-FIELD READ `a[i].field` -> ARR_GET then OBJ_GET. Claimed
        // BEFORE the plain index read (which would consume `a[i]` alone, orphaning `.field`).
        return TEKO_VT_INT;
    } else if (lower_array_index_read(b, p, ctx, ta)) {
        // Phase 18 (18.E.1): an INDEX READ `a[i]` on an array local -> OP_ARR_GET -> $w0.
        return TEKO_VT_INT;
    } else if (p->current_token.type == TOKEN_NULL) {
        // Phase 18 (18.A): the `null` literal — the empty optional. Payload = 0 in $w0; the present
        // descriptor is the literal-null sentinel (-2) so an Elvis `null ?? d` resolves to `d`, and
        // a `let x: ?T = null;` binding records present = 0 (via g_last_init_is_null).
        codegen_li_emit_iconst(b, 0);
        g_prim_present_slot = -2;
        g_last_init_is_null = 1;
        fe_advance(p);
        return TEKO_VT_INT;
    } else if (p->current_token.type == TOKEN_LIT_INT) {
        codegen_li_emit_iconst(b, atoi(p->current_token.lexeme));
        fe_advance(p);
        return TEKO_VT_INT;
    } else if (p->current_token.type == TOKEN_LIT_FLOAT) {
        // Phase 17 (17.A): a float literal (`3.14`, `2.0`, `0.5`). The lexer emits TOKEN_LIT_FLOAT
        // ONLY for a `<digits>.<digits>` form (a `.`-fraction; `2.` and `1e9`/`f`-suffix are NOT
        // lexed as floats — see lex_number). strtod the lexeme (digit-separator `_` stripped first,
        // since the lexeme may keep them) → OP_FCONST → $f0. Value-type is VT_FLOAT.
        char buf[64]; int j = 0;
        const char* lx = p->current_token.lexeme;
        for (int k = 0; lx[k] && j < (int)sizeof(buf) - 1; k++) if (lx[k] != '_') buf[j++] = lx[k];
        buf[j] = '\0';
        codegen_li_emit_fconst(b, strtod(buf, NULL));
        fe_advance(p);
        return TEKO_VT_FLOAT;
    } else if (p->current_token.type == TOKEN_LIT_DECIMAL) {
        // Phase 17.F.3: a `dec` literal (`9.99dec`, `1000dec`). The lexer already stripped the `dec`
        // suffix, so the lexeme is the bare number; parse it through the EXACT 17.F.2 decimal core
        // (teko_decimal_parse — culture-invariant, banker's-rounded) into a 256-byte value, pool it,
        // and emit OP_DCONST. Digit separators `_` are stripped first (the lexeme may keep them).
        // A parse failure (only on >1984-bit coefficient — the grammar already vetted the digits)
        // canonicalizes to zero; that is a degenerate compile-time literal, not a runtime fail.
        char buf[128]; int j = 0;
        const char* lx = p->current_token.lexeme;
        for (int k = 0; lx[k] && j < (int)sizeof(buf) - 1; k++) if (lx[k] != '_') buf[j++] = lx[k];
        buf[j] = '\0';
        teko_decimal dv;
        if (!teko_decimal_parse(buf, &dv)) teko_decimal_zero(&dv);
        codegen_li_emit_dconst(b, (const unsigned char*)&dv);
        fe_advance(p);
        return TEKO_VT_DECIMAL;
    } else if (p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) {
        if (strlit_is_interp(p->current_token.lexeme)) {
            // Phase 16 (16.C): an interpolated literal `"…{expr}…"` -> concat of chunks + holes.
            return lower_interp_string(b, p, ctx);
        }
        // Phase 16 (16.B): a string-literal primary -> SCONST pointer in $w0 (VT_STR), so `"a" + x`
        // (or a bare string sub-expression) is recognized as concatenation, not arithmetic.
        char* sv = strip_quotes(p->current_token.lexeme);
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, sv));
        free(sv); fe_advance(p);
        return TEKO_VT_STR;
    } else if (is_floatcast_head(p)) {
        // Phase 17 (17.B): `convert.to_int(<expr>)` / `convert.to_float(<expr>)` — checked int↔float
        // casts. Claimed BEFORE the codec check so the dotted ident is not mistaken for a codec call.
        return lower_floatcast(b, p, ctx);
    } else if (is_decimalsurf_head(p)) {
        // Phase 17.F.4: `decimal.to_string(<decimal>)` (id 59, VT_STR) / `decimal.parse(<string>)`
        // (id 60, VT_DECIMAL). Same pre-codec claim as the floatcast head.
        return lower_decimalsurf(b, p, ctx);
    } else if (is_simd_head(p)) {
        // Phase 18 (18.E.4): `simd.sum(<i32[] expr>)` -> OP_SIMD_SUM -> the scalar sum (VT_INT) in $w0.
        // Same pre-codec dotted-head claim; works as a let-initializer and a call argument via the
        // expression paths (try_lower_call_arg_expr / lower_init_value route through eval_primary).
        return lower_simd(b, p, ctx);
    } else if (is_codec_head(p)) {
        // Phase 16 (16.B/16.F): a codec / convert / hash call primary — most return a string pointer
        // (VT_STR); the checked parsers return an int (VT_INT). So `"n=" + convert.parse_int(s)`
        // concatenates correctly (the int is auto-`to_string`'d), not mistaken for a pointer.
        int id = codec_id_for(p->current_token.lexeme);
        lower_base_codec(b, p, ctx);
        return runtime_result_vt(id);
    } else if (p->current_token.type == TOKEN_LPAREN) {
        fe_advance(p);
        int vt = eval_expr_prec(b, p, ctx, 1, ta);
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
        return vt;
    } else if (is_member_call_head(p) && lower_member_call(b, p, ctx)) {
        // Phase 15 (15.A): a `obj.method(args)` static method call as a sub-expression -> OP_CALL_FUNC.
        // Claimed BEFORE the field read (peek `(` disambiguates a call from a `obj.field` read) so a
        // method call works in any expression position (extern arg, arithmetic operand, interp hole).
        // The result is a register-width integer cell in $w0 (object results are ints in this model).
        return TEKO_VT_INT;
    } else if (is_trait_dispatch_head(p) && lower_trait_dispatch_ctx(b, p, ctx)) {
        // Phase 15 (15.B): a `g.method(args)` DYNAMIC trait dispatch as a sub-expression -> vtable_get
        // + OP_CALL_FUNC. Same argument/sub-expression reach as the static case; the receiver is a fat
        // trait-typed local (mutually exclusive with the class-local member-call head above).
        return TEKO_VT_INT;
    } else if (lower_member_read(b, p, ctx)) {
        // `obj.field` read consumed (e.g. inside `self.x + self.y`). Field cells are integers here.
        return TEKO_VT_INT;
    } else if (p->current_token.type == TOKEN_IDENTIFIER && p->peek_token.type == TOKEN_SAFE_DOT) {
        // Phase 18 (18.B): safe navigation `obj?.member` — null-propagating member access. Must be
        // claimed BEFORE the optional-local payload read below (which would consume `obj` alone).
        return lower_safe_nav(b, p, ctx);
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        long cv;
        if (comptime_find(p->current_token.lexeme, &cv)) {
            // Phase 18 (18.D): a comptime constant reads as its folded value — a single iconst, the
            // computation never reaches the runtime.
            codegen_li_emit_iconst(b, (int)cv);
            fe_advance(p);
            return TEKO_VT_INT;
        }
        int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
        int isstr = localstr_get(p->current_token.lexeme);
        int isflt = localflt_get(p->current_token.lexeme); // Phase 17: a float-typed local?
        int isdec = localdec_get(p->current_token.lexeme); // Phase 17.F.3: a decimal-typed local?
        int ci = localcls_get(p->current_token.lexeme); // Phase 16.D: a class-instance local?
        int opt_present = localopt_present_slot(p->current_token.lexeme); // Phase 18: optional?
        if (s >= 0 && opt_present >= 0) {
            // Phase 18 (18.A): an OPTIONAL local read — load the payload into $w0 (a bare read
            // assumes present; `??`/`?.` are the safe accessors) and expose its present-companion slot
            // so a following Elvis can branch on it. The payload value-type is the optional's base.
            codegen_li_emit_load_local(b, s);
            g_prim_present_slot = opt_present;
            int bvt = localopt_base_vt(p->current_token.lexeme);
            fe_advance(p);
            return bvt;
        }
        if (s >= 0 && isflt) {
            // Phase 17 (17.A): a float local reads through the float accumulator ($f0), not $w0.
            codegen_li_emit_fload_local(b, s);
            fe_advance(p);
            return TEKO_VT_FLOAT;
        }
        if (s >= 0 && isdec) {
            // Phase 17.F.3: a decimal local reads through the 256-byte decimal slot ($d0), not $w0.
            codegen_li_emit_dload_local(b, s);
            fe_advance(p);
            return TEKO_VT_DECIMAL;
        }
        if (s >= 0) codegen_li_emit_load_local(b, s);   // $w0 = value (int / string ptr / handle)
        else codegen_li_emit_iconst(b, 0); // unknown identifier in this subset → 0
        fe_advance(p);
        if (s >= 0 && ci >= 0) return TEKO_VT_OBJ_BASE + ci; // object handle → dispatch to_string
        return isstr ? TEKO_VT_STR : TEKO_VT_INT;
    } else {
        codegen_li_emit_iconst(b, 0); // empty/unsupported primary → 0
        return TEKO_VT_INT;
    }
}

static int eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                          int min_prec, TempAlloc* ta) {
    int vt_l = eval_primary(b, p, ctx, ta); // left operand → $w0
    int left_present_slot = g_prim_present_slot; // Phase 18 (18.A): the left operand's optionality
    while (p12_tok_prec(p->current_token.type) >= min_prec &&
           p12_tok_prec(p->current_token.type) > 0) {
        // Phase 18 (18.E.3): once an operator is applied, a `s.field` left operand is no longer the
        // raw field-run handle (it has been combined arithmetically) — so the WHOLE-RUN i32[] signal
        // is void. `let col = s.field;` (no operator) is the only form that records `col` as i32[].
        g_last_member_read_is_iarray = 0;
        int prec = p12_tok_prec(p->current_token.type);
        TokenType optok = p->current_token.type;
        OpCode op = p12_tok_op(optok);
        fe_advance(p); // consume the operator
        int t = ta->next_temp++;
        if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        // Phase 17 (17.A): spill the LEFT operand with the type-appropriate store — a float operand
        // lives in $f0 (FSTORE_LOCAL to a float slot), an int/ptr operand in $w0 (STORE_LOCAL). The
        // temp slot `t` is one frame slot; on native it is reused as either type (a slot has one
        // type per use), on WASM $fvN / $vN are parallel local files.
        // Phase 17.F.3: a decimal operand lives in the 256-byte $d0 slot — spill it to a decimal temp
        // slot (DSTORE_LOCAL); a float to $fvN; an int/ptr to $vN.
        if (vt_l == TEKO_VT_DECIMAL)    codegen_li_emit_dstore_local(b, t); // temp = left (decimal)
        else if (vt_l == TEKO_VT_FLOAT) codegen_li_emit_fstore_local(b, t); // temp = left (float)
        else                            codegen_li_emit_store_local(b, t);  // temp = left (raw value)
        int vt_r = eval_expr_prec(b, p, ctx, prec + 1, ta);  // right → $w0 / $f0 / $d0 (left-assoc)
        if (optok == TOKEN_PLUS && (vt_l == TEKO_VT_STR || vt_r == TEKO_VT_STR)) {
            // Phase 16 (16.B): a `+` with a string operand is culture-invariant CONCATENATION with
            // auto-`to_string`. Right is in $w0 — convert it to a string if it is an int (id 49),
            // stash it, then build the left string and call str_concat (id 52, arg0=left, $w0=right).
            // Phase 17.D: a FLOAT operand auto-converts via id 50 (which reads $f0). For the RIGHT
            // operand this works as-is — it was just evaluated into $f0 (coerce(FLOAT) reads it).
            coerce_to_string_in_w0(b, ctx, vt_r);      // $w0 = to_string(right) (int/float/object/string)
            int t2 = ta->next_temp++;
            if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
            codegen_li_emit_store_local(b, t2);        // temp2 = right (string ptr)
            // Phase 17.D: the LEFT operand was spilled to temp `t` with the type-appropriate store
            // (FSTORE_LOCAL for a float, STORE_LOCAL otherwise). Reload it with the matching load so a
            // float value reaches $f0 (where coerce(FLOAT)→id 50 reads it), not the integer $w0.
            // Phase 17.F.4: a LEFT decimal operand was spilled to temp `t` with DSTORE_LOCAL — reload
            // it into $d0 (DLOAD_LOCAL) so coerce(DECIMAL)→id 59 reads it, exactly as the float case
            // reloads $f0. A float left reloads $f0; everything else reloads the raw $w0.
            if (vt_l == TEKO_VT_DECIMAL)    codegen_li_emit_dload_local(b, t); // $d0 = left (decimal)
            else if (vt_l == TEKO_VT_FLOAT) codegen_li_emit_fload_local(b, t); // $f0 = left (float)
            else                            codegen_li_emit_load_local(b, t);  // $w0 = left (raw)
            coerce_to_string_in_w0(b, ctx, vt_l);      // $w0 = to_string(left)
            codegen_li_emit_setarg(b, 0);              // $a0 = left string
            codegen_li_emit_load_local(b, t2);         // $w0 = right string
            codegen_li_emit_call_runtime(b, 52);       // $w0 = str_concat(left, right)
            ta->next_temp--;                           // free temp2
            vt_l = TEKO_VT_STR;
        } else if (vt_l == TEKO_VT_DECIMAL || vt_r == TEKO_VT_DECIMAL) {
            // Phase 17.F.3/17.F.4: decimal arithmetic / comparison. With BOTH operands decimal this is
            // the exact-decimal path; 17.F.4 adds MIXED int/float + decimal PROMOTION (the non-decimal
            // operand is widened to decimal via I2D/F2D — exactly like the float branch promotes ints).
            // Step 1 — RIGHT operand → $d1. The right was just evaluated into $d0 (decimal) / $f0
            // (float) / $w0 (int); promote a non-decimal right to $d0 (I2D/F2D), then DSTORE → $d1.
            if (vt_r == TEKO_VT_FLOAT)        codegen_li_emit_dcast(b, OP_F2D); // $d0 = (decimal)$f0
            else if (vt_r != TEKO_VT_DECIMAL) codegen_li_emit_dcast(b, OP_I2D); // $d0 = (decimal)$w0
            codegen_li_emit_dunop(b, OP_DSTORE);   // $d1 = $d0 (right, now decimal)
            // Step 2 — LEFT operand → $d0. Reload from temp `t` with the matching load (a decimal left
            // was DSTORE_LOCAL'd; a float was FSTORE_LOCAL'd; an int was STORE_LOCAL'd), then promote
            // a non-decimal left to $d0 (I2D reads $w0, F2D reads $f0).
            if (vt_l == TEKO_VT_DECIMAL) {
                codegen_li_emit_dload_local(b, t);                   // $d0 = left (decimal)
            } else if (vt_l == TEKO_VT_FLOAT) {
                codegen_li_emit_fload_local(b, t);                   // $f0 = left (float)
                codegen_li_emit_dcast(b, OP_F2D);                    // $d0 = (decimal)$f0
            } else {
                codegen_li_emit_load_local(b, t);                    // $w0 = left (int)
                codegen_li_emit_dcast(b, OP_I2D);                    // $d0 = (decimal)$w0
            }
            codegen_li_emit_dunop(b, p12_tok_dop(optok)); // $d0 = left <op> right  (or $w0 for compares)
            vt_l = is_compare_tok(optok) ? TEKO_VT_INT : TEKO_VT_DECIMAL;
        } else if (vt_l == TEKO_VT_FLOAT || vt_r == TEKO_VT_FLOAT) {
            // Phase 17 (17.A): float arithmetic / comparison. Promote any int operand to f64 (I2F),
            // bring left into $f0 and right into $f1, then emit the float op. Arith ops (+ - * /)
            // produce VT_FLOAT; compares (== != < <= > >=) produce an i32 0/1 in $w0 (VT_INT).
            // Right is currently in $f0 (if it was float) or $w0 (if it was int) — move it to $f1.
            if (vt_r == TEKO_VT_FLOAT) {
                codegen_li_emit_funop(b, OP_FSTORE);   // $f1 = $f0 (right)
            } else {
                codegen_li_emit_funop(b, OP_I2F);      // $f0 = (double)$w0 (right)
                codegen_li_emit_funop(b, OP_FSTORE);   // $f1 = $f0 (right)
            }
            // Left was spilled in temp `t` — reload to $f0, promoting if it was an int.
            if (vt_l == TEKO_VT_FLOAT) {
                codegen_li_emit_fload_local(b, t);     // $f0 = left (float)
            } else {
                codegen_li_emit_load_local(b, t);      // $w0 = left (int)
                codegen_li_emit_funop(b, OP_I2F);      // $f0 = (double)$w0
            }
            codegen_li_emit_funop(b, p12_tok_fop(optok)); // $f0 = left <op> right  (or $w0 for compares)
            vt_l = is_compare_tok(optok) ? TEKO_VT_INT : TEKO_VT_FLOAT;
        } else {
            codegen_li_emit_store(b);                  // $w1 = right
            codegen_li_emit_load_local(b, t);          // $w0 = left
            codegen_li_emit_binop(b, op);              // $w0 = left <op> right
            vt_l = TEKO_VT_INT;
        }
        ta->next_temp--;                               // free temp
    }
    // Phase 18 (18.A): the ELVIS operator `lhs ?? rhs` — LOWEST precedence (handled here, not in the
    // prec table, so it never collides with arithmetic/compare folding). `min_prec <= 1` keeps it out
    // of a higher-precedence right-operand sub-expression, so `a ?? b ?? c` is right-associative.
    // Lowering (zero-overhead, reuses existing opcodes — no new IL): the LHS payload is already in
    // $w0; spill it + its present flag, evaluate the RHS as the default, then conditionally overwrite
    // with the payload when present. The OP_IF_BEGIN test on the present flag lowers to a hardware
    // conditional branch (`je`/`cbz`/WASM `(if)`).
    if (p->current_token.type == TOKEN_ELVIS && min_prec <= 1) {
        int payload_t = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        codegen_li_emit_store_local(b, payload_t);            // payload_t = lhs payload (from $w0)
        int present_t = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        if (left_present_slot == -2)     codegen_li_emit_iconst(b, 0);                  // literal null
        else if (left_present_slot >= 0) codegen_li_emit_load_local(b, left_present_slot); // companion
        else                             codegen_li_emit_iconst(b, 1);                  // non-optional
        codegen_li_emit_store_local(b, present_t);            // present_t = lhs present flag
        int result_t = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        fe_advance(p);                                        // consume `??`
        int vt_r = eval_expr_prec(b, p, ctx, 1, ta);          // rhs (right-assoc) → $w0 (the default)
        codegen_li_emit_store_local(b, result_t);             // result_t = default (rhs)
        codegen_li_emit_load_local(b, present_t);             // $w0 = present flag
        codegen_li_emit_cf(b, OP_IF_BEGIN);                   // if present != 0 …
        codegen_li_emit_load_local(b, payload_t);             //   $w0 = payload
        codegen_li_emit_store_local(b, result_t);             //   result_t = payload
        codegen_li_emit_cf(b, OP_IF_END);
        codegen_li_emit_load_local(b, result_t);              // $w0 = result (present ? payload : default)
        ta->next_temp -= 3;                                   // free payload_t, present_t, result_t
        g_prim_present_slot = -1;                             // the Elvis result is a plain value
        vt_l = vt_r;                                          // result type = the default/payload type
    }
    return vt_l;
}

// Phase 16 (16.C): lower an interpolated string literal `"…{expr}…"` to a single string pointer in
// $w0 (VT_STR), built by concatenating literal chunks and per-hole `to_string(expr)` — the same
// culture-invariant auto-`to_string` as `+`. Each hole's expression is re-lexed through a sub-parser
// that shares THIS lowering ctx (so locals/temps resolve), then reuses str_concat (id 52). `{{`/`}}`
// are literal braces. Current token is the string literal; it is consumed before returning.
static int lower_interp_string(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    char* content = strip_quotes(p->current_token.lexeme);   // inner text, no surrounding quotes
    TempAlloc* ta = ctx->ta;
    int acc = ta->next_temp++;                                // accumulator temp (running result)
    if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    int have_acc = 0;

    // Append the string pointer currently in $w0 to the accumulator (str_concat, left-assoc).
    #define INTERP_APPEND_W0() do {                                                       \
        if (!have_acc) { codegen_li_emit_store_local(b, acc); have_acc = 1; }             \
        else {                                                                            \
            int tp = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp; \
            codegen_li_emit_store_local(b, tp);          /* tp = piece */                 \
            codegen_li_emit_load_local(b, acc); codegen_li_emit_setarg(b, 0); /* a0=acc */\
            codegen_li_emit_load_local(b, tp);           /* $w0 = piece */                \
            codegen_li_emit_call_runtime(b, 52);         /* $w0 = concat(acc, piece) */   \
            codegen_li_emit_store_local(b, acc);                                          \
            ta->next_temp--;                                                              \
        }                                                                                 \
    } while (0)

    char lit[4096]; int ln = 0;
    const char* s = content ? content : "";
    while (*s) {
        if (s[0] == '{' && s[1] == '{') { if (ln < (int)sizeof(lit)-1) lit[ln++]='{'; s += 2; continue; }
        if (s[0] == '}' && s[1] == '}') { if (ln < (int)sizeof(lit)-1) lit[ln++]='}'; s += 2; continue; }
        if (s[0] == '{') {
            // Flush any pending literal chunk as a SCONST piece.
            if (ln > 0) {
                lit[ln] = '\0';
                codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, lit));
                INTERP_APPEND_W0();
                ln = 0;
            }
            // Extract the hole expression up to the matching '}' (brace-depth aware).
            s++; // past '{'
            char expr[1024]; int en = 0, depth = 1;
            while (*s && depth > 0) {
                if (*s == '{') depth++;
                else if (*s == '}') { depth--; if (depth == 0) break; }
                if (en < (int)sizeof(expr)-1) expr[en++] = *s;
                s++;
            }
            expr[en] = '\0';
            if (*s == '}') s++; // past closing '}'
            // Re-lex + lower the hole expression with the SHARED ctx (locals/temps), then to_string.
            Lexer sublx; lexer_init(&sublx, expr);
            Parser subp; parser_init(&subp, &sublx);
            int vt = eval_expr_prec(b, &subp, ctx, 1, ta);   // hole value → $w0
            coerce_to_string_in_w0(b, ctx, vt);              // auto-to_string (int/object/string)
            INTERP_APPEND_W0();
            continue;
        }
        if (ln < (int)sizeof(lit)-1) lit[ln++] = *s;
        s++;
    }
    if (ln > 0) { // trailing literal chunk
        lit[ln] = '\0';
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, lit));
        INTERP_APPEND_W0();
    }
    if (!have_acc) { // empty / all-escaped → the empty string
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, ""));
        codegen_li_emit_store_local(b, acc); have_acc = 1;
    }
    codegen_li_emit_load_local(b, acc); // result → $w0
    ta->next_temp--;                    // free acc
    #undef INTERP_APPEND_W0
    if (content) free(content);
    fe_advance(p); // consume the interpolated literal
    return TEKO_VT_STR;
}

// Phase 16 (16.D): coerce an OBJECT handle in $w0 (instance of class `ci`) to a string pointer in
// $w0. If the class defines (or inherits) a `to_string` method, dispatch it statically through the
// Phase-15 hook (self=arg0, routine slot via OP_CALL_FUNC) — exactly like `p.to_string()`. When the
// class defines none, synthesize the culture-invariant default `ClassName(f0, f1, …)` over the
// fields (each rendered via the integer to_string), the compile-time generated-stringifier spirit.
static void emit_object_to_string(BytecodeBuffer* b, const LowerCtx* ctx, int ci) {
    TempAlloc* ta = ctx->ta;
    int midx = (ci >= 0) ? class_method_idx(ci, "to_string") : -1;
    if (ci >= 0 && midx >= 0 && g_class[ci].method_slot[midx] >= 0) {
        codegen_li_emit_setarg(b, 0);                              // $a0 = self (the handle)
        codegen_li_emit_iconst(b, g_class[ci].method_slot[midx]); // $w0 = method slot (static)
        codegen_li_emit_call_func(b, 1);                          // $w0 = self.to_string()
        return;
    }
    // --- synthesized default: "ClassName(" + field0 + ", " + field1 + … + ")" -----------------
    int th = ta->next_temp++;  if (ta->next_temp > ta->hw) ta->hw = ta->next_temp; // handle
    codegen_li_emit_store_local(b, th);
    int acc = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp; // running result
    char head[120]; snprintf(head, sizeof(head), "%s(", (ci >= 0) ? g_class[ci].name : "?");
    codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, head));
    codegen_li_emit_store_local(b, acc);
    int nf = (ci >= 0) ? g_class[ci].nfields : 0;
    for (int i = 0; i < nf; i++) {
        if (i > 0) {                                              // acc += ", "
            int tp = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
            codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, ", "));
            codegen_li_emit_store_local(b, tp);
            codegen_li_emit_load_local(b, acc); codegen_li_emit_setarg(b, 0);
            codegen_li_emit_load_local(b, tp);  codegen_li_emit_call_runtime(b, 52);
            codegen_li_emit_store_local(b, acc); ta->next_temp--;
        }
        codegen_li_emit_load_local(b, th); codegen_li_emit_setarg(b, 0);     // $a0 = handle
        codegen_li_emit_iconst(b, i); codegen_li_emit_object(b, OP_OBJ_GET); // $w0 = field i
        codegen_li_emit_call_runtime(b, 49);                                 // -> decimal string
        int tp = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        codegen_li_emit_store_local(b, tp);                                  // acc += fieldstr
        codegen_li_emit_load_local(b, acc); codegen_li_emit_setarg(b, 0);
        codegen_li_emit_load_local(b, tp);  codegen_li_emit_call_runtime(b, 52);
        codegen_li_emit_store_local(b, acc); ta->next_temp--;
    }
    {                                                             // acc += ")"
        int tp = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, ")"));
        codegen_li_emit_store_local(b, tp);
        codegen_li_emit_load_local(b, acc); codegen_li_emit_setarg(b, 0);
        codegen_li_emit_load_local(b, tp);  codegen_li_emit_call_runtime(b, 52);
        codegen_li_emit_store_local(b, acc); ta->next_temp--;
    }
    codegen_li_emit_load_local(b, acc);  // $w0 = the assembled default
    ta->next_temp -= 2;                  // free acc + th
}

static void coerce_to_string_in_w0(BytecodeBuffer* b, const LowerCtx* ctx, int vt) {
    if (vt == TEKO_VT_STR) return;                            // already a string pointer
    if (vt == TEKO_VT_FLOAT) {
        // Phase 17 (17.D): float→string. UNLIKE int/string/object (whose value is in $w0), a VT_FLOAT
        // operand's value lives in the PARALLEL float accumulator $f0 — the caller guarantees it is
        // there at this point (a freshly-evaluated float expr leaves $f0; a left-float concat operand
        // is reloaded via FLOAD_LOCAL before coercing). id 50 (teko_rt_float_to_string, the 17.C Ryu
        // shortest-round-trip formatter) reads $f0 and leaves the char* result in $w0 (VT_STR).
        codegen_li_emit_call_runtime(b, 50);
        return;
    }
    if (vt == TEKO_VT_DECIMAL) {
        // Phase 17.F.4: decimal→string. Like a VT_FLOAT, the value does NOT live in $w0 — it is in the
        // 256-byte $d0 slot, which the caller guarantees holds the operand at this point (a freshly-
        // evaluated decimal expr leaves $d0; a left-decimal concat operand is reloaded via
        // DLOAD_LOCAL before coercing — see eval_expr_prec). id 59 (decimal.to_string) reads &$d0 and
        // leaves the char* in $w0 (VT_STR). This is what makes `"x = " + d` and `"{d}"` work.
        codegen_li_emit_call_runtime(b, 59);
        return;
    }
    if (vt >= TEKO_VT_OBJ_BASE) { emit_object_to_string(b, ctx, vt - TEKO_VT_OBJ_BASE); return; }
    codegen_li_emit_call_runtime(b, 49);                      // VT_INT → culture-invariant decimal
}

// --- base-encoding codecs (P12-G) -----------------------------------------------
// `base64.encode(x)` / `.decode`, `hex.encode/.decode`. Lexes as
// <TOKEN_BASE64|TOKEN_HEX> TOKEN_DOT <TOKEN_ENCODE|TOKEN_DECODE> '(' arg ')'. The arg is
// a string literal, a named local, or a nested codec call; it is lowered to a pointer
// in $w0, then OP_CALL_RUNTIME invokes the native codec (result pointer -> $w0).
// The lexer folds `base64.encode` into a single dotted identifier (the same rule that
// makes `@marshall.to_ptr` one macro token), so a codec call is an IDENTIFIER whose
// lexeme is "<base64|hex>.<encode|decode>" followed by '('. Returns the codec id
// (0=b64e,1=b64d,2=hexe,3=hexd) or -1.
static int codec_id_for(const char* lex) {
    if (!lex) return -1;
    if (strcmp(lex, "base64.encode") == 0) return 0;
    if (strcmp(lex, "base64.decode") == 0) return 1;
    if (strcmp(lex, "hex.encode") == 0) return 2;
    if (strcmp(lex, "hex.decode") == 0) return 3;
    // Phase 13 (13.1): native hash primitives. Same dotted-identifier lowering as the
    // base codecs — `hash.sha256(x)` takes a string/local/nested value, lowers it to a
    // NUL-terminated pointer in $w0, then OP_CALL_RUNTIME invokes the in-module SHA-256
    // runtime which returns a pointer to the lowercase hex digest. (hash.sha512 = id 5,
    // wired in the next increment alongside its i64 WAT runtime.)
    if (strcmp(lex, "hash.sha256") == 0) return 4;
    // Phase 13 native runner — rest of the fixed-size hash family (native surface; the
    // WASM lowering of these is deferred to Sub-phase C, where the WASM emitter traps).
    if (strcmp(lex, "hash.sha512") == 0) return 5;
    if (strcmp(lex, "hash.sha384") == 0) return 10;
    if (strcmp(lex, "hash.sha3_256") == 0) return 11;
    if (strcmp(lex, "hash.sha3_512") == 0) return 12;
    if (strcmp(lex, "hash.blake3") == 0) return 15;
    if (strcmp(lex, "hash.blake2b") == 0) return 16;
    // HMAC (two args: hex key, message) — Phase 13 native surface, ids 17-19.
    if (strcmp(lex, "hmac.sha256") == 0) return 17;
    if (strcmp(lex, "hmac.sha384") == 0) return 18;
    if (strcmp(lex, "hmac.sha512") == 0) return 19;
    // AEAD (four args: hex key, nonce, aad, plaintext|cipher‖tag) — ids 20-23.
    // seal -> (ct‖tag) hex; open -> plaintext hex, or "REJECT" on auth failure.
    if (strcmp(lex, "crypto.aes_gcm_seal") == 0) return 20;
    if (strcmp(lex, "crypto.aes_gcm_open") == 0) return 21;
    if (strcmp(lex, "crypto.chacha20poly1305_seal") == 0) return 22;
    if (strcmp(lex, "crypto.chacha20poly1305_open") == 0) return 23;
    // Signatures — Ed25519 (ids 24/25). sign(seedHex, msgHex) -> sigHex;
    // verify(pubHex, msgHex, sigHex) -> "1" (valid) | "0" (invalid).
    if (strcmp(lex, "crypto.ed25519_sign") == 0) return 24;
    if (strcmp(lex, "crypto.ed25519_verify") == 0) return 25;
    // Key exchange — X25519 (RFC 7748). x25519(scalarHex, uHex) -> sharedHex. id 26.
    if (strcmp(lex, "crypto.x25519") == 0) return 26;
    // KDF — HKDF / PBKDF2 over SHA-256. Hex inputs + integer length/iteration args. ids 27/28.
    // hkdf_sha256(ikmHex, saltHex, infoHex, len); pbkdf2_sha256(passHex, saltHex, iters, len).
    if (strcmp(lex, "kdf.hkdf_sha256") == 0) return 27;
    if (strcmp(lex, "kdf.pbkdf2_sha256") == 0) return 28;
    // ECDSA over the NIST P-curves (RFC 6979 deterministic). sign(privHex, hashHex) -> r‖s hex;
    // verify(pubHex, hashHex, sigHex) -> "1"/"0". The hash is the message digest. ids 29-32.
    if (strcmp(lex, "crypto.ecdsa_p256_sign") == 0) return 29;
    if (strcmp(lex, "crypto.ecdsa_p256_verify") == 0) return 30;
    if (strcmp(lex, "crypto.ecdsa_p384_sign") == 0) return 31;
    if (strcmp(lex, "crypto.ecdsa_p384_verify") == 0) return 32;
    // SHAKE128/256 XOF (msg + output length). ids 33/34.
    if (strcmp(lex, "hash.shake128") == 0) return 33;
    if (strcmp(lex, "hash.shake256") == 0) return 34;
    // RSA (RFC 8017) — keys are big-endian hex (modulus n, exponent e/d), SHA-256 + MGF1.
    // PSS sign/verify (salt_len=hLen, random salt); OAEP encrypt/decrypt (random seed, empty
    // label). ids 37-40. sign->sigHex; verify->"1"/"0"; encrypt->ctHex; decrypt->msgHex|REJECT.
    if (strcmp(lex, "crypto.rsa_pss_sign") == 0) return 37;
    if (strcmp(lex, "crypto.rsa_pss_verify") == 0) return 38;
    if (strcmp(lex, "crypto.rsa_oaep_encrypt") == 0) return 39;
    if (strcmp(lex, "crypto.rsa_oaep_decrypt") == 0) return 40;
    // CSPRNG — random.bytes(n) -> n random bytes as hex. id 41.
    if (strcmp(lex, "random.bytes") == 0) return 41;
    // UUID v4 (random) / v7 (time-ordered + random) -> canonical UUID string. ids 42/43.
    // No surface args (entropy/time come from the runtime/host); the lowered $w0 is ignored.
    if (strcmp(lex, "uuid.v4") == 0) return 42;
    if (strcmp(lex, "uuid.v7") == 0) return 43;
    // Phase 14 (wall-clock / timezone surface) — OS-sourced civil time, string-returning like the
    // crypto ids. now_* ignore their arg (time/zone from the OS); format_* take a decimal epoch
    // string. ids 44-48 (reactor-backed on WASM, where now/offset come from host imports).
    if (strcmp(lex, "time.now_unix")     == 0) return 44;
    if (strcmp(lex, "time.now_local")    == 0) return 45;
    if (strcmp(lex, "time.now_utc")      == 0) return 46;
    if (strcmp(lex, "time.format_local") == 0) return 47;
    if (strcmp(lex, "time.format_utc")   == 0) return 48;
    // Legacy hashes (insecure — interop only): in-module WAT runtimes, ids 6/7.
    if (strcmp(lex, "hash.md5") == 0) return 6;
    if (strcmp(lex, "hash.sha1") == 0) return 7;
    // UUID name-based generators (deterministic; DNS namespace) — ids 8/9.
    if (strcmp(lex, "uuid.v3") == 0) return 8;
    if (strcmp(lex, "uuid.v5") == 0) return 9;
    // Phase 16 (Casting / Conversions & Parsing) — culture-invariant conversion surface, string-
    // returning like the crypto/time ids. `convert.int_to_str(n)` lowers n (an int immediate/local)
    // in $w0; `convert.str_concat(a, b)` stages a + leaves b in $w0 (arity 2). The lexer folds
    // `convert.int_to_str` into ONE dotted identifier (same rule as `time.now_unix`). Parse +
    // explicit-format + the auto-call on concat/interpolation are later 16.x sub-blocks.
    if (strcmp(lex, "convert.int_to_str") == 0)  return 49;
    if (strcmp(lex, "convert.bool_to_str") == 0) return 51;
    if (strcmp(lex, "convert.str_concat") == 0)  return 52;
    // Phase 16.E — explicit integer formats (developer-supplied spec; deviates from the default).
    if (strcmp(lex, "convert.to_radix") == 0)    return 56; // (v, base 2..36)
    if (strcmp(lex, "convert.pad") == 0)         return 57; // (v, width)
    if (strcmp(lex, "convert.group") == 0)       return 58; // (v) -> thousands-grouped
    // Phase 16.F — CHECKED parse (string -> primitive; fail-loud on malformed input).
    if (strcmp(lex, "convert.parse_int") == 0)   return 53; // (str) -> i32
    if (strcmp(lex, "convert.parse_bool") == 0)  return 55; // (str) -> 0/1
    if (strcmp(lex, "convert.parse_float") == 0) return 54; // (str) -> f64 (checked, fail-loud)
    return -1;
}

static int is_codec_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           codec_id_for(p->current_token.lexeme) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

// Arg count of a runtime primitive (OP_CALL_RUNTIME id). Most are single-arg (codecs,
// hashes); HMAC takes (hexKey, msg). Multi-arg calls stage args 0..n-2 via OP_SETARG and
// leave the last in $w0 — the same convention OP_CALL_IMPORT uses.
static int runtime_arity(int id) {
    switch (id) {
        case 17: case 18: case 19: return 2; // hmac.sha256/384/512
        case 20: case 21: case 22: case 23: return 4; // AEAD: key, nonce, aad, msg/ct‖tag
        case 24: return 2; // ed25519_sign(seed, msg)
        case 25: return 3; // ed25519_verify(pub, msg, sig)
        case 26: return 2; // x25519(scalar, u)
        case 27: return 4; // hkdf_sha256(ikm, salt, info, len)
        case 28: return 4; // pbkdf2_sha256(pass, salt, iters, len)
        case 29: case 31: return 2; // ecdsa_p256/p384_sign(priv, hash)
        case 30: case 32: return 3; // ecdsa_p256/p384_verify(pub, hash, sig)
        case 33: case 34: return 2; // shake128/256(msg, out_len)
        case 37: return 3; // rsa_pss_sign(n, d, mhash)
        case 38: return 4; // rsa_pss_verify(n, e, mhash, sig)
        case 39: return 3; // rsa_oaep_encrypt(n, e, msg)
        case 40: return 3; // rsa_oaep_decrypt(n, d, ct)
        case 52: return 2; // convert.str_concat(a, b)
        case 56: return 2; // convert.to_radix(v, base)
        case 57: return 2; // convert.pad(v, width)
        default: return 1;
    }
}

static void lower_base_codec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // recursive

// Lower a codec argument (string literal / int literal / named local / nested codec) to
// $w0. Strings/hex-strings become pointers; integer literals become immediates (e.g. the
// KDF length / iteration count). The hosted emitter marshals $w0 into the ABI arg register
// regardless of whether it is a pointer or an integer.
static void lower_codec_value(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) {
        char* s = strip_quotes(p->current_token.lexeme);
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, s));
        free(s);
        fe_advance(p);
    } else if (p->current_token.type == TOKEN_LIT_INT) {
        // Phase 14 (14.G): a timespan literal arg (e.g. `delayed.send(d, v, 2s)`) normalizes to
        // canonical milliseconds at compile time, so the runtimes stay integer-only and unchanged.
        codegen_li_emit_iconst(b, (int)literal_canonical_value(&p->current_token));
        fe_advance(p);
    } else if (is_codec_head(p)) {
        lower_base_codec(b, p, ctx); // nested: base64.decode(base64.encode(x))
    } else if (lower_soa_index_read(b, p, ctx, ctx ? ctx->ta : NULL)) {
        // Phase 18 (18.E.3): a SoA `s[i].field` read as a codec/call argument -> OP_IARR_GET.
    } else if (lower_aos_index_read(b, p, ctx, ctx ? ctx->ta : NULL)) {
        // Phase 18 (18.E.3): an AoS `a[i].field` read as a codec/call argument -> ARR_GET then OBJ_GET.
    } else if (lower_array_index_read(b, p, ctx, ctx ? ctx->ta : NULL)) {
        // Phase 18 (18.E.1): an array index READ `a[i]` as a codec/call argument -> OP_ARR_GET.
    } else if (is_member_call_head(p) && lower_member_call(b, p, ctx)) {
        // Phase 15 (15.A): a `obj.method(args)` static method call as a codec / nested-call argument
        // -> OP_CALL_FUNC -> result in $w0 (claimed before the field read; peek `(` disambiguates).
    } else if (is_trait_dispatch_head(p) && lower_trait_dispatch_ctx(b, p, ctx)) {
        // Phase 15 (15.B): a `g.method(args)` DYNAMIC trait dispatch as a codec / nested-call argument
        // -> vtable_get + OP_CALL_FUNC -> result in $w0 (receiver is a fat trait-typed local).
    } else if (lower_member_read(b, p, ctx)) {
        // Phase 18 (18.E.1): an array `.len` (or a class field) as a codec/call argument.
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        long cv;
        if (comptime_find(p->current_token.lexeme, &cv)) {
            codegen_li_emit_iconst(b, (int)cv);   // Phase 18 (18.D): comptime constant → folded value
        } else {
            int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
            if (s >= 0) codegen_li_emit_load_local(b, s);
            else codegen_li_emit_iconst(b, 0);
        }
        fe_advance(p);
    } else {
        codegen_li_emit_iconst(b, 0); // unsupported arg in this subset
    }
}

static void lower_base_codec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int id = codec_id_for(p->current_token.lexeme);      // current = "base64.encode" etc.
    if (id < 0) id = 0;
    int arity = runtime_arity(id);
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    // Lower each arg to $w0; stage args 0..n-2 into $a<i> via OP_SETARG, leave the last
    // in $w0. emit_native_hosted marshals staging slots + $w0 into the ABI arg registers.
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_call_runtime(b, id);                 // $w0 = rt(args); sets uses_codec/uses_hash by id
}

// --- duplex channels (Phase 14, 14.B) -------------------------------------------
// `duplex.open/send/recv/poll/close(args)`. The lexer folds `duplex.open` into ONE dotted
// IDENTIFIER (bare `duplex` stays the keyword), so this reuses the dotted-identifier call
// path — but lowers to the dedicated OP_DUPLEX_* opcodes (not OP_CALL_RUNTIME). Args are
// int literals (capacity / endpoint / value) or named locals (the channel handle); they are
// lowered with lower_codec_value, staged via OP_SETARG (0..n-2) with the last left in $w0.
// The result (handle / value / status) lands in $w0. Returns the opcode, or -1.
static int duplex_op_for(const char* lex, int* arity) {
    int a = 1; int op = -1;
    if (!lex) return -1;
    if      (strcmp(lex, "duplex.open")  == 0) { op = OP_DUPLEX_OPEN;  a = 1; }
    else if (strcmp(lex, "duplex.send")  == 0) { op = OP_DUPLEX_SEND;  a = 3; }
    else if (strcmp(lex, "duplex.recv")  == 0) { op = OP_DUPLEX_RECV;  a = 2; }
    else if (strcmp(lex, "duplex.poll")  == 0) { op = OP_DUPLEX_POLL;  a = 2; }
    else if (strcmp(lex, "duplex.close") == 0) { op = OP_DUPLEX_CLOSE; a = 1; }
    if (op >= 0 && arity) *arity = a;
    return op;
}

static int is_duplex_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           duplex_op_for(p->current_token.lexeme, NULL) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

static void lower_duplex_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int arity = 1;
    int op = duplex_op_for(p->current_token.lexeme, &arity);
    if (op < 0) { op = OP_DUPLEX_OPEN; arity = 1; }
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0 (int / named local)
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_duplex(b, (OpCode)op);               // $w0 = duplex op result
}

// --- Phase 17 (17.B): checked int↔float casts (convert.to_int / convert.to_float) ---------------
// These are NOT codec calls: their argument is a full EXPRESSION (a float or int sub-expression),
// so they bypass codec_id_for / lower_base_codec (which emit OP_CALL_RUNTIME over a literal/local
// arg). Instead the inner expression is eval'd via eval_expr_prec (leaving its result in $w0 for an
// int or $f0 for a float), then the conversion opcode is emitted:
//   convert.to_int(<expr>)   -> if the expr is VT_FLOAT, OP_F2I (CHECKED, fail-loud) -> i32 in $w0;
//                               an int expr is already in $w0 (no-op). Result VT_INT.
//   convert.to_float(<expr>) -> if the expr is NOT VT_FLOAT (an int in $w0), OP_I2F -> $f0; a float
//                               expr is already in $f0 (no-op). Result VT_FLOAT.
// Phase 17.D adds `convert.float_to_str(<expr>)` to this SAME dotted-head machinery (so every
// is_floatcast_head call site — eval_primary, lower_init_value, the call-arg path — picks it up):
//   convert.float_to_str(<expr>) -> eval the inner expr; promote an int operand with OP_I2F (-> $f0);
//                               then emit id 50 (reads $f0) -> char* in $w0. Result VT_STR.
static int is_floatcast_head(const Parser* p) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    const char* lx = p->current_token.lexeme;
    return strcmp(lx, "convert.to_int") == 0 || strcmp(lx, "convert.to_float") == 0 ||
           strcmp(lx, "convert.float_to_str") == 0 ||
           // Phase 17.F.4 — `convert.to_decimal(<int|float>)` lowers to OP_I2D/OP_F2D; `to_int`/
           // `to_float` of a decimal arg add the OP_D2I/OP_D2F branch (handled inside lower_floatcast).
           strcmp(lx, "convert.to_decimal") == 0;
}
static int lower_floatcast(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int to_int = (strcmp(p->current_token.lexeme, "convert.to_int") == 0);
    int to_str = (strcmp(p->current_token.lexeme, "convert.float_to_str") == 0);
    int to_dec = (strcmp(p->current_token.lexeme, "convert.to_decimal") == 0);
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    // The inner expression -> $w0 (int) / $f0 (float) / $d0 (decimal). ctx->ta is the temp allocator.
    int vt = eval_expr_prec(b, p, ctx, 1, ctx->ta);
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (to_dec) {
        // Phase 17.F.4 — to_decimal: int -> OP_I2D ($w0 -> $d0); float -> OP_F2D ($f0 -> $d0); an
        // already-decimal expr is a no-op (stays in $d0). Result VT_DECIMAL.
        if (vt == TEKO_VT_FLOAT)        codegen_li_emit_dcast(b, OP_F2D);
        else if (vt != TEKO_VT_DECIMAL) codegen_li_emit_dcast(b, OP_I2D);
        return TEKO_VT_DECIMAL;
    }
    if (to_str) {
        // Phase 17.D: float->string surface. Promote an int operand to f64 first ($f0); a float
        // expr is already in $f0. id 50 (the 17.C Ryu formatter) reads $f0 -> char* in $w0 (VT_STR).
        // A decimal arg here would have no f64 home; route it to its own to_string (id 59) instead.
        if (vt == TEKO_VT_DECIMAL) { codegen_li_emit_call_runtime(b, 59); return TEKO_VT_STR; }
        if (vt != TEKO_VT_FLOAT) codegen_li_emit_funop(b, OP_I2F); // $f0 = (double)$w0
        codegen_li_emit_call_runtime(b, 50);
        return TEKO_VT_STR;
    } else if (to_int) {
        // Phase 17.F.4 — a decimal arg lowers to OP_D2I (checked, truncate toward zero).
        if (vt == TEKO_VT_DECIMAL) codegen_li_emit_dcast(b, OP_D2I);  // $w0 = (i32)$d0 (checked)
        else if (vt == TEKO_VT_FLOAT) codegen_li_emit_f2i(b);         // $w0 = (i32)trunc($f0) (checked)
        // else: an int expression is already in $w0 — to_int is a no-op.
        return TEKO_VT_INT;
    } else {
        // convert.to_float: a decimal arg lowers to OP_D2F (decimal -> f64 via shortest string).
        if (vt == TEKO_VT_DECIMAL) codegen_li_emit_dcast(b, OP_D2F); // $f0 = (f64)$d0
        else if (vt != TEKO_VT_FLOAT) codegen_li_emit_funop(b, OP_I2F); // $f0 = (double)$w0
        // else: a float expression is already in $f0 — to_float is a no-op.
        return TEKO_VT_FLOAT;
    }
}

// --- Phase 17.F.4: the `decimal.to_string` / `decimal.parse` language surface ---------------------
// `decimal.to_string(<decimal expr>)` -> id 59 (reads the decimal value from $d0, returns a culture-
// invariant `.`-decimal string in $w0, VT_STR). `decimal.parse(<string expr>)` -> id 60 (reads a
// string ptr from $w0, writes the parsed 256-byte decimal into $d0, VT_DECIMAL; CHECKED/fail-loud).
// Claimed via the dotted-identifier head, same machinery as the floatcast (eval_primary / init).
static int is_decimalsurf_head(const Parser* p) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    const char* lx = p->current_token.lexeme;
    return strcmp(lx, "decimal.to_string") == 0 || strcmp(lx, "decimal.parse") == 0;
}
static int lower_decimalsurf(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int to_str = (strcmp(p->current_token.lexeme, "decimal.to_string") == 0);
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    int vt = eval_expr_prec(b, p, ctx, 1, ctx->ta);      // inner expr -> $w0 / $f0 / $d0
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (to_str) {
        // decimal.to_string: promote a non-decimal arg to decimal first (so `decimal.to_string(42)`
        // and `decimal.to_string(3.14)` work), then emit id 59 (reads $d0).
        if (vt == TEKO_VT_FLOAT)        codegen_li_emit_dcast(b, OP_F2D);
        else if (vt != TEKO_VT_DECIMAL) codegen_li_emit_dcast(b, OP_I2D);
        codegen_li_emit_call_runtime(b, 59);             // $w0 = decimal.to_string($d0)
        return TEKO_VT_STR;
    } else {
        // decimal.parse: the arg must be a string in $w0 (id 60 writes the result into $d0).
        codegen_li_emit_call_runtime(b, 60);             // $d0 = decimal.parse($w0) (checked)
        return TEKO_VT_DECIMAL;
    }
}

// --- Phase 18 (18.E.4): the `simd.sum` SIMD-reduction language surface ----------------------------
// `simd.sum(<expr>)` reduces a contiguous typed i32[] run to its scalar sum (VT_INT). The inner
// expression evaluates to an i32[] HANDLE in $w0 — typically a typed-array local (`a`) or the SoA
// whole-run accessor `s.field` (the 18.E.3 hook), both of which leave the handle in $w0. OP_SIMD_SUM
// then lowers (per backend) to: fetch the run's data ptr + length, call the REAL per-ISA vector kernel
// (SSE2/NEON/simd128; scalar fallback on the 16 freestanding emitters + riscv). Claimed via the
// dotted-identifier head, same machinery as the floatcast/decimalsurf heads.
static int is_simd_head(const Parser* p) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    return strcmp(p->current_token.lexeme, "simd.sum") == 0;
}
static int lower_simd(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    fe_advance(p);                                       // consume `simd.sum`
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    // The inner expression -> an i32[] handle in $w0 (a typed-array local or a SoA `s.field` run).
    eval_expr_prec(b, p, ctx, 1, ctx->ta);
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_simd(b, OP_SIMD_SUM);                // $w0 = scalar sum of the run (real vector kernel)
    return TEKO_VT_INT;
}

// --- delayed (timed) channels (Phase 14, 14.C) ----------------------------------
// `delayed.open/send/advance/recv/poll/close(args)` — same dotted-identifier surface as duplex
// (the lexer folds `delayed.open` into one IDENTIFIER), lowering to the dedicated OP_DELAYED_*.
static int delayed_op_for(const char* lex, int* arity) {
    int a = 1; int op = -1;
    if (!lex) return -1;
    if      (strcmp(lex, "delayed.open")    == 0) { op = OP_DELAYED_OPEN;    a = 1; }
    else if (strcmp(lex, "delayed.send")    == 0) { op = OP_DELAYED_SEND;    a = 3; }
    else if (strcmp(lex, "delayed.recv")    == 0) { op = OP_DELAYED_RECV;    a = 1; }
    else if (strcmp(lex, "delayed.poll")    == 0) { op = OP_DELAYED_POLL;    a = 1; }
    else if (strcmp(lex, "delayed.close")   == 0) { op = OP_DELAYED_CLOSE;   a = 1; }
    if (op >= 0 && arity) *arity = a;
    return op;
}

static int is_delayed_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           delayed_op_for(p->current_token.lexeme, NULL) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

static void lower_delayed_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int arity = 1;
    int op = delayed_op_for(p->current_token.lexeme, &arity);
    if (op < 0) { op = OP_DELAYED_OPEN; arity = 1; }
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0 (int / named local)
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_delayed(b, (OpCode)op);              // $w0 = delayed op result
}

// --- broadcast (non-destructive 1:N pub-sub) channels (Phase 14, 14.D) -----------
// `broadcast.open/subscribe/publish/recv/poll/close(args)`. `broadcast` is NOT a keyword, so
// the lexer folds `broadcast.open` into one IDENTIFIER — same dotted-identifier path as
// duplex/delayed, lowering to the dedicated OP_BCAST_*.
static int bcast_op_for(const char* lex, int* arity) {
    int a = 1; int op = -1;
    if (!lex) return -1;
    if      (strcmp(lex, "broadcast.open")      == 0) { op = OP_BCAST_OPEN;      a = 1; }
    else if (strcmp(lex, "broadcast.subscribe") == 0) { op = OP_BCAST_SUBSCRIBE; a = 1; }
    else if (strcmp(lex, "broadcast.publish")   == 0) { op = OP_BCAST_PUBLISH;   a = 2; }
    else if (strcmp(lex, "broadcast.recv")      == 0) { op = OP_BCAST_RECV;      a = 2; }
    else if (strcmp(lex, "broadcast.poll")      == 0) { op = OP_BCAST_POLL;      a = 2; }
    else if (strcmp(lex, "broadcast.close")     == 0) { op = OP_BCAST_CLOSE;     a = 1; }
    if (op >= 0 && arity) *arity = a;
    return op;
}

static int is_bcast_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           bcast_op_for(p->current_token.lexeme, NULL) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

static void lower_bcast_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int arity = 1;
    int op = bcast_op_for(p->current_token.lexeme, &arity);
    if (op < 0) { op = OP_BCAST_OPEN; arity = 1; }
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0 (int / named local)
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_bcast(b, (OpCode)op);                // $w0 = broadcast op result
}

// --- shared memory: `atomic.*` ops + the `shared { }` block (Phase 14, 14.E) ----
// `atomic.cell/add/load/store(args)` — `atomic` is a keyword, but the lexer folds `atomic.cell`
// into one IDENTIFIER (bare `atomic` stays the keyword), so this reuses the dotted-identifier
// path, lowering to OP_ATOMIC_*.
static int atomic_op_for(const char* lex, int* arity) {
    int a = 1; int op = -1;
    if (!lex) return -1;
    if      (strcmp(lex, "atomic.cell")  == 0) { op = OP_ATOMIC_CELL;  a = 1; }
    else if (strcmp(lex, "atomic.add")   == 0) { op = OP_ATOMIC_ADD;   a = 2; }
    else if (strcmp(lex, "atomic.load")  == 0) { op = OP_ATOMIC_LOAD;  a = 1; }
    else if (strcmp(lex, "atomic.store") == 0) { op = OP_ATOMIC_STORE; a = 2; }
    if (op >= 0 && arity) *arity = a;
    return op;
}

static int is_atomic_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           atomic_op_for(p->current_token.lexeme, NULL) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

static void lower_atomic_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int arity = 1;
    int op = atomic_op_for(p->current_token.lexeme, &arity);
    if (op < 0) { op = OP_ATOMIC_LOAD; arity = 1; }
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0 (int / named local handle)
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_shared(b, (OpCode)op);               // $w0 = atomic op result
}

// --- timespan waiters: `wait <ts>;` / `await <ts>;` (Phase 14, 14.G) -------------
// `wait` is a SYNCHRONOUS sleep; `await` is a cooperative timed yield. The timespan operand is
// lowered to canonical milliseconds in $w0, then OP_WAIT / OP_AWAIT_FOR fires. A literal operand
// (e.g. `2s`) is unit-normalized at compile time; a named local or parenthesized expression is
// assumed to already be in ms. Current token is TOKEN_WAIT / TOKEN_AWAIT.
static void lower_wait_await(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, int is_await) {
    fe_advance(p); // consume 'wait' / 'await'
    if (p->current_token.type == TOKEN_LIT_INT) {
        codegen_li_emit_iconst(b, (int)literal_canonical_value(&p->current_token)); // ms in $w0
        fe_advance(p);
    } else if (ctx && ctx->ta) {
        eval_expr_prec(b, p, ctx, 1, ctx->ta); // named local / int expression (already ms) -> $w0
    } else {
        codegen_li_emit_iconst(b, 0);
    }
    if (is_await) codegen_li_emit_await(b);
    else          codegen_li_emit_wait(b);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
}

// --- control-flow foundation: structured loops + branches (Phase 14) -------------
// `while (cond) { … }`, `loop { … }`, `if (cond) { … }`, `break;`, `continue;`, lowered to the
// structured OP_LOOP_*/OP_IF_*/OP_BREAK* opcodes (native asm labels / WASM block+loop). A block
// body reuses the same single-statement dispatcher (lower_one_stmt) as the top level, so loops
// compose with let/reassignment/calls/channels/waiters and with each other (nesting).
//
// LowerEnv threads the MUTABLE named-local table (grown by `let` inside a body) + the read-only
// fn/extern bindings + the expression-lowering context, so a `let`/reassignment inside a loop
// allocates real $v slots and the ctx's local view stays current after a realloc.
typedef struct {
    ImportBinding** locals; int* nlocals; int* caplocals; // mutable named-local table
    ImportBinding** binds;  int* nb;                       // extern import bindings (read)
    LowerCtx* ctx;                                         // expression context (locals view + ta)
} LowerEnv;

static void lower_block(BytecodeBuffer* b, Parser* p, LowerEnv* env);   // fwd (mutual recursion)
static void lower_one_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env); // fwd
static void lower_retry_block(BytecodeBuffer* b, Parser* p, LowerEnv* env);   // fwd (14.F)
static void lower_circuit_block(BytecodeBuffer* b, Parser* p, LowerEnv* env); // fwd (14.F)

// Allocate a fresh hidden named local ($v slot) and return its slot index. Used by the resilience
// lowering for its synthetic state (policy/attempt/elapsed/flags). Refreshes the ctx local view.
static int env_alloc_local(LowerEnv* env, const char* name) {
    int s = *env->nlocals;
    bind_add(env->locals, env->nlocals, env->caplocals, name, s);
    env->ctx->locals = *env->locals; env->ctx->nlocals = *env->nlocals;
    if (*env->nlocals > env->ctx->ta->hw) env->ctx->ta->hw = *env->nlocals;
    return s;
}

// local = constant.
static void emit_set_local_const(BytecodeBuffer* b, int slot, int v) {
    codegen_li_emit_iconst(b, v);
    codegen_li_emit_store_local(b, slot);
}
// local = local + ($w0)  ($w0 holds the delta on entry).
static void emit_local_add_w0(BytecodeBuffer* b, int slot) {
    codegen_li_emit_store(b);            // $w1 = delta
    codegen_li_emit_load_local(b, slot); // $w0 = local
    codegen_li_emit_binop(b, OP_ADD);    // $w0 = local + delta
    codegen_li_emit_store_local(b, slot);
}
// $w0 = (local == 0).
static void emit_local_is_zero(BytecodeBuffer* b, int slot) {
    codegen_li_emit_iconst(b, 0);        // $w0 = 0
    codegen_li_emit_store(b);            // $w1 = 0
    codegen_li_emit_load_local(b, slot); // $w0 = local
    codegen_li_emit_binop(b, OP_EQ);     // $w0 = (local == 0)
}

// Refresh the expression context's local view + position the temp allocator above the named
// locals (so spills never clobber a binding). Call before lowering any expression in a body.
static void env_sync(LowerEnv* env) {
    env->ctx->locals = *env->locals;
    env->ctx->nlocals = *env->nlocals;
    env->ctx->ta->next_temp = *env->nlocals;
    if (*env->nlocals > env->ctx->ta->hw) env->ctx->ta->hw = *env->nlocals;
}

// Lower an initializer / RHS value into $w0: string literal, @dom/@js intrinsic, codec/hash/crypto,
// duplex/delayed/broadcast/atomic op, or an integer expression (literals/locals/parens/arith/cmp).
// Phase 15 (15.A): forward decls for the class-surface value forms (defined after lower_call_stmt,
// since they reuse lower_codec_value / eval_expr_prec).
static void lower_instantiation(BytecodeBuffer* b, Parser* p);                      // ClassName(...)
static int  lower_member_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx);   // obj.method(...)
static int  lower_trait_dispatch(BytecodeBuffer* b, Parser* p, LowerEnv* env);      // g.method(...) dynamic

static void lower_init_value(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    LowerCtx* ctx = env->ctx;
    g_last_init_vt = TEKO_VT_INT; // Phase 16 (16.B): default; the string-yielding cases set VT_STR
    g_last_init_is_null = 0;      // Phase 18 (18.A): set iff the initializer is the `null` literal
    g_last_init_is_array = 0;     // Phase 18 (18.E.1): set iff the initializer is an array literal
    g_last_init_is_iarray = 0;    // Phase 18 (18.E.2): set iff it is a TYPED i32 packed-array literal
    g_last_member_read_is_iarray = 0; // Phase 18 (18.E.3): set iff the initializer is a `s.field` run accessor
    g_last_init_aos_class = -1;       // Phase 18 (18.E.3): set iff the initializer is an AoS object-array literal
    if ((p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) &&
        p12_tok_prec(p->peek_token.type) == 0 && !strlit_is_interp(p->current_token.lexeme)) {
        // A LONE, NON-interpolated string literal. `"a" + x` and `"{x}"` fall through to
        // eval_expr_prec (→ eval_primary), lowered as concatenation / interpolation (16.B/16.C).
        char* sv = strip_quotes(p->current_token.lexeme);
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, sv));
        free(sv); fe_advance(p);
        g_last_init_vt = TEKO_VT_STR;
    } else if (is_instantiation_head(p)) {
        // Phase 15 (15.A/15.C): `ClassName(...)` / `T(...)` / `Box<Arg>(...)` instantiation ->
        // OP_OBJ_NEW -> handle in $w0 (sets g_last_inst_class for the let binding).
        lower_instantiation(b, p);
    } else if (p->current_token.type == TOKEN_MACRO_IDENT && is_dom_macro(p->current_token.lexeme) &&
               p->peek_token.type == TOKEN_LPAREN) {
        lower_intrinsic_call(b, p, ctx);
    } else if (is_floatcast_head(p) || is_decimalsurf_head(p) || is_simd_head(p)) {
        // Phase 17 (17.B): `let f = convert.to_float(3);` / `let n = convert.to_int(7.9);` —
        // Phase 17.F.4: `let d = convert.to_decimal(42);`, `let s = decimal.to_string(d);`,
        // `let d = decimal.parse(s);`. Phase 18 (18.E.4): `let vec = simd.sum(a);`. Route through
        // eval_expr_prec (NOT the bare lower_*) so a TRAILING binary operator is also consumed — e.g.
        // `let g = convert.to_decimal(n) + total;` (mixed promotion) or `let t = simd.sum(a) + 1;`.
        // eval_primary already claims all three heads; the returned VT (after any `+`) is recorded in
        // g_last_init_vt so the binding reads back as the right type.
        env_sync(env);
        g_last_init_vt = eval_expr_prec(b, p, ctx, 1, ctx->ta);
    } else if (is_codec_head(p))   { int id = codec_id_for(p->current_token.lexeme);
                                     lower_base_codec(b, p, ctx); g_last_init_vt = runtime_result_vt(id); }
    else if (is_duplex_head(p))    { lower_duplex_call(b, p, ctx); }
    else if (is_delayed_head(p))   { lower_delayed_call(b, p, ctx); }
    else if (is_bcast_head(p))     { lower_bcast_call(b, p, ctx); }
    else if (is_atomic_head(p))    { lower_atomic_call(b, p, ctx); }
    else if (p->current_token.type == TOKEN_CIRCUIT && p->peek_token.type == TOKEN_LPAREN) {
        // `circuit(threshold K, cooldown C)` breaker constructor as a let-initializer (14.F):
        // parse the header, emit OP_CIRCUIT_NEW -> handle in $w0.
        fe_advance(p); // 'circuit'
        int threshold = 1, cooldown = 0;
        if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
        while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
            if (p->current_token.type == TOKEN_IDENTIFIER &&
                strcmp(p->current_token.lexeme, "threshold") == 0) {
                fe_advance(p);
                if (p->current_token.type == TOKEN_LIT_INT) {
                    threshold = (int)literal_canonical_value(&p->current_token); fe_advance(p);
                }
            } else if (p->current_token.type == TOKEN_IDENTIFIER &&
                       strcmp(p->current_token.lexeme, "cooldown") == 0) {
                fe_advance(p);
                if (p->current_token.type == TOKEN_LIT_INT) {
                    cooldown = (int)literal_canonical_value(&p->current_token); fe_advance(p);
                }
            } else {
                fe_advance(p);
            }
        }
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
        codegen_li_emit_iconst(b, threshold); codegen_li_emit_setarg(b, 0);
        codegen_li_emit_iconst(b, cooldown);  // last arg in $w0
        codegen_li_emit_retry(b, OP_CIRCUIT_NEW); // $w0 = breaker handle
    }
    else { env_sync(env); g_last_init_vt = eval_expr_prec(b, p, ctx, 1, ctx->ta); } // expr -> $w0 (VT tracked)
}

// Phase 18 (18.E.3): true iff the upcoming initializer is a `soa Class[N]` construction (current
// token is TOKEN_SOA, peek a known class name). A pure predicate; no consumption.
static int is_soa_init_head(const Parser* p) {
    if (!p || p->current_token.type != TOKEN_SOA) return 0;
    if (p->peek_token.type != TOKEN_IDENTIFIER) return 0;
    return class_find(p->peek_token.lexeme) >= 0;
}

// Phase 18 (18.E.3): lower a `soa Class[N]` construction. For a class with k fields, allocate k
// CONTIGUOUS typed-i32 arrays of length N (OP_IARR_NEW), each handle stored into a HIDDEN named local
// `<lname>#f<idx>` (so the per-field run is reachable by name). The SoA local `<lname>` itself carries
// no $w0 value — it is a pure compile-time grouping (registered in g_localsoa). Consumes
// `soa Class [ N ]`. Returns 1 on success. (env_alloc_local refreshes the ctx local view each time.)
static int lower_soa_init(BytecodeBuffer* b, Parser* p, LowerEnv* env, const char* lname) {
    if (p->current_token.type != TOKEN_SOA) return 0;
    fe_advance(p); // consume 'soa'
    if (p->current_token.type != TOKEN_IDENTIFIER) return 0;
    int ci = class_find(p->current_token.lexeme);
    if (ci < 0) return 0;
    fe_advance(p); // consume the class name
    int n = 0;
    if (p->current_token.type == TOKEN_LBRACKET) {
        fe_advance(p); // '['
        if (p->current_token.type == TOKEN_LIT_INT) {
            n = (int)literal_canonical_value(&p->current_token);
            fe_advance(p);
        }
        if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    }
    int k = g_class[ci].nfields;
    // Allocate one packed-i32 array of length N per field; park each handle in `<lname>#f<idx>`.
    for (int f = 0; f < k; f++) {
        char fnm[120]; snprintf(fnm, sizeof(fnm), "%s#f%d", lname, f);
        int fslot = env_alloc_local(env, fnm);
        codegen_li_emit_iconst(b, n);
        codegen_li_emit_iarray(b, OP_IARR_NEW);   // $w0 = field-run handle (packed int32, length N)
        codegen_li_emit_store_local(b, fslot);    // <lname>#f<idx> = handle
    }
    localsoa_set(lname, ci, n);
    return 1;
}

// `let`/`mut NAME [: type] = <init>;` inside a body — allocate (or reuse) a $v slot, lower the
// initializer, store it. Refreshes the ctx local view (bind_add may realloc the table).
static void lower_let_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    fe_advance(p); // let/mut
    char lname[96];
    strncpy(lname, p->current_token.lexeme, sizeof(lname) - 1); lname[sizeof(lname) - 1] = '\0';
    fe_advance(p); // NAME
    // Phase 15.B: capture the FIRST identifier of a `: Type` annotation — a trait name makes this a
    // dynamically-dispatched (fat) reference; anything else is a plain local.
    // Phase 18 (18.A): a leading `?` in the annotation (`let x: ?int`) marks the local OPTIONAL — it
    // carries a hidden present companion (emitted below). The `?` is consumed before the type name.
    char annot[96]; annot[0] = '\0';
    int is_optional = 0;
    // Phase 18 (18.E.2): a `: i32[]` annotation forces the `[…]` RHS to lower as a TYPED i32 PACKED
    // array (OP_IARR_*). The annot capture grabs the type name (`i32`); we then detect the trailing
    // `[` `]` during the skip-to-`=` walk. (A plain `[…]` with no `i32[]` annotation stays the i64
    // array — byte-identical to 18.E.1.)
    int is_iarray_annot = 0;
    if (p->current_token.type == TOKEN_COLON) {
        fe_advance(p);
        if (p->current_token.type == TOKEN_QUESTION) { is_optional = 1; fe_advance(p); } // `?T`
        if (p->current_token.type == TOKEN_IDENTIFIER) {
            strncpy(annot, p->current_token.lexeme, sizeof(annot) - 1); annot[sizeof(annot) - 1] = '\0';
        }
        while (p->current_token.type != TOKEN_ASSIGN && p->current_token.type != TOKEN_QUICK_ASSIGN &&
               p->current_token.type != TOKEN_SEMICOLON && p->current_token.type != TOKEN_EOF) {
            // `i32` immediately followed by `[` `]` is the typed-array annotation `i32[]`.
            if (p->current_token.type == TOKEN_LBRACKET && p->peek_token.type == TOKEN_RBRACKET &&
                strcmp(annot, "i32") == 0)
                is_iarray_annot = 1;
            fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_ASSIGN || p->current_token.type == TOKEN_QUICK_ASSIGN)
        fe_advance(p);
    int s = bind_lookup(*env->locals, *env->nlocals, lname);
    if (s < 0) { s = *env->nlocals; bind_add(env->locals, env->nlocals, env->caplocals, lname, s); }
    env_sync(env);
    // Phase 18 (18.E.3): `let s = soa Class[N];` — a structure-of-arrays construction. Allocate k
    // contiguous typed-i32 field runs (handles in hidden `s#f<idx>` locals) and register `s` in
    // g_localsoa. The SoA local `s` itself carries no $w0 value (it is a compile-time grouping); the
    // base slot `s` stays allocated but unused. Claimed BEFORE the generic initializer path.
    if (is_soa_init_head(p)) {
        codegen_li_emit_iconst(b, 0);                // s = 0 (placeholder; the value is never read)
        codegen_li_emit_store_local(b, s);
        lower_soa_init(b, p, env, lname);
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
        return;
    }
    // Phase 15 (15.A/15.C): remember this local's class so later `lname.field`/`lname.method(...)`
    // resolve their compile-time index/slot. A concrete-instance RHS (`let g = c`) is read here; an
    // instantiation RHS (`ClassName()`/`T()`/`Box<Arg>()`) is read from g_last_inst_class AFTER
    // lowering (its type-arg can be past the parser's lookahead).
    int rhs_local_class = (p->current_token.type == TOKEN_IDENTIFIER &&
                           p->peek_token.type != TOKEN_LPAREN && p->peek_token.type != TOKEN_LT)
                          ? localcls_get(p->current_token.lexeme) : -1;
    int annot_trait = (annot[0] && trait_find(annot) >= 0) ? trait_find(annot) : -1;
    g_last_inst_class[0] = '\0';
    env_sync(env);
    // Phase 18 (18.E.2): a `: i32[]` annotation makes the `[…]` literal lower as a TYPED i32 packed
    // array (lower_array_literal consumes this one-shot flag).
    g_force_iarray_literal = is_iarray_annot;
    lower_init_value(b, p, env);
    g_force_iarray_literal = 0;
    int rhs_class = g_last_inst_class[0] ? class_find(g_last_inst_class) : rhs_local_class;
    // Phase 17 (17.A / 17.F.3): a float initializer lives in $f0 (FSTORE_LOCAL), a decimal in the
    // 256-byte $d0 slot (DSTORE_LOCAL); both record the local so `lname` reads back with the right
    // value-type. An int/ptr stays in $w0 (STORE_LOCAL).
    if (g_last_init_vt == TEKO_VT_DECIMAL)    codegen_li_emit_dstore_local(b, s);
    else if (g_last_init_vt == TEKO_VT_FLOAT) codegen_li_emit_fstore_local(b, s);
    else                                      codegen_li_emit_store_local(b, s);
    if (annot_trait >= 0) {
        // Phase 15.B: a FAT trait-typed reference — its concrete type_id (from the RHS) rides in a
        // hidden tid slot as a compile-time constant; `lname.method()` dispatches dynamically.
        char tname[120]; snprintf(tname, sizeof(tname), "%s#tid", lname);
        int tid_slot = env_alloc_local(env, tname);
        codegen_li_emit_iconst(b, rhs_class >= 0 ? class_type_id(rhs_class) : -1);
        codegen_li_emit_store_local(b, tid_slot);
        traitlocal_add(lname, annot_trait, s, tid_slot);
    } else if (rhs_class >= 0) {
        localcls_set(lname, rhs_class);
    } else if (is_optional && annot[0] && class_find(annot) >= 0) {
        // Phase 18 (18.B): an OPTIONAL object (`let q: ?Box = null;`) — record the class from the
        // `?Class` ANNOTATION (the null RHS carries no class) so a later `q?.member` resolves the
        // field index / method slot at compile time. The handle is null at runtime, but a `?.`
        // access is guarded by the present flag, so the null handle is never dereferenced.
        localcls_set(lname, class_find(annot));
    }
    // Phase 16 (16.B): remember a string-typed local so `s` reads as VT_STR in a later concat.
    localstr_set(lname, g_last_init_vt == TEKO_VT_STR);
    // Phase 17 (17.A): remember a float-typed local so `lname` routes through the float accumulator.
    localflt_set(lname, g_last_init_vt == TEKO_VT_FLOAT);
    // Phase 17.F.3: remember a decimal-typed local so `lname` routes through the 256-byte $d0 slot.
    localdec_set(lname, g_last_init_vt == TEKO_VT_DECIMAL);
    // Phase 18 (18.E.1): remember an array-typed local (handle in $w0, VT_INT) so a later
    // `lname[i]` / `lname.len` resolves to OP_ARR_*. The handle was STORE_LOCAL'd above (VT_INT).
    localarr_set(lname, g_last_init_is_array);
    // Phase 18 (18.E.3): an AoS array-of-objects (`let a = [Point(), …]`) — record its element class so
    // `a[i].field` (index-then-member) resolves the field index at compile time. `a` stays a plain
    // array local (the literal lowering is byte-identical); only the field RESOLUTION uses this.
    if (g_last_init_is_array && g_last_init_aos_class >= 0) localaos_set(lname, g_last_init_aos_class);
    // `for x in lname` resolves to OP_IARR_* (disjoint from g_localarr). The two are mutually
    // exclusive (lower_array_literal sets exactly one of g_last_init_is_array/_is_iarray).
    // Phase 18 (18.E.3): the SoA whole-run accessor `let col = s.field;` ALSO yields an i32[] handle —
    // record `col` as an iarray local referencing the SAME contiguous packed-i32 run (the SIMD hook),
    // so `col[i]`/`col.len`/`simd.*` operate on it exactly like any typed `i32[]` local.
    localiarr_set(lname, g_last_init_is_iarray || g_last_member_read_is_iarray);
    // Phase 18 (18.A): an OPTIONAL local (`let x: ?T = …`) carries a hidden present companion slot
    // `x#opt` set to 0 when the initializer is `null`, else 1 (compile-time constant — runtime-null
    // propagation arrives with the `?.` safe-navigation in 18.B). The payload was stored above; the
    // base value-type is the initializer's (VT_INT for the MVP). Register so `x` reads optional + a
    // following `??` recovers the present flag.
    if (is_optional) {
        char oname[120]; snprintf(oname, sizeof(oname), "%s#opt", lname);
        int present_slot = env_alloc_local(env, oname);
        codegen_li_emit_iconst(b, g_last_init_is_null ? 0 : 1);
        codegen_li_emit_store_local(b, present_slot);
        localopt_set(lname, present_slot, g_last_init_vt);
    }
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
}

// `NAME = <expr>;` reassignment of an existing named local. Returns 1 if it consumed the
// statement, 0 if NAME is not a known local (so the caller tries other forms).
static int lower_reassign(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_ASSIGN) return 0;
    char nm[96]; strncpy(nm, p->current_token.lexeme, 95); nm[95] = '\0';
    int slot = bind_lookup(*env->locals, *env->nlocals, nm);
    if (slot < 0) return 0;
    fe_advance(p); // NAME
    fe_advance(p); // '='
    // Phase 15.B: reassigning a FAT trait local updates its tid slot too — the new concrete type_id
    // (known from the RHS's static class) is a compile-time constant, so dynamic dispatch on the
    // reassigned reference picks the new implementation.
    int tl = traitlocal_find(nm);
    int rhs_class = (tl >= 0 && p->current_token.type == TOKEN_IDENTIFIER)
        ? (p->peek_token.type == TOKEN_LPAREN ? class_find(p->current_token.lexeme)
                                              : localcls_get(p->current_token.lexeme))
        : -1;
    env_sync(env);
    lower_init_value(b, p, env);
    // Phase 17 (17.A / 17.F.3): keep the store type-correct on reassignment (decimal → DSTORE_LOCAL,
    // float → FSTORE_LOCAL, else STORE_LOCAL).
    if (g_last_init_vt == TEKO_VT_DECIMAL)    codegen_li_emit_dstore_local(b, slot);
    else if (g_last_init_vt == TEKO_VT_FLOAT) codegen_li_emit_fstore_local(b, slot);
    else                                      codegen_li_emit_store_local(b, slot);
    if (tl >= 0) {
        codegen_li_emit_iconst(b, rhs_class >= 0 ? class_type_id(rhs_class) : -1);
        codegen_li_emit_store_local(b, g_traitlocal[tl].tid_slot);
    }
    // Phase 16 (16.B): keep the local's string-typed-ness in sync with its new value.
    localstr_set(nm, g_last_init_vt == TEKO_VT_STR);
    // Phase 17 (17.A): keep the local's float-typed-ness in sync with its new value.
    localflt_set(nm, g_last_init_vt == TEKO_VT_FLOAT);
    // Phase 17.F.3: keep the local's decimal-typed-ness in sync with its new value.
    localdec_set(nm, g_last_init_vt == TEKO_VT_DECIMAL);
    // Phase 18 (18.E.1): keep the local's array-typed-ness in sync (a reassign to a new `[...]`).
    localarr_set(nm, g_last_init_is_array);
    // Phase 18 (18.A): if `nm` is an optional local, refresh its present companion from the new
    // initializer (`x = null;` → present 0, else 1), so a later `x ?? d` branches correctly.
    int oslot = localopt_present_slot(nm);
    if (oslot >= 0) {
        codegen_li_emit_iconst(b, g_last_init_is_null ? 0 : 1);
        codegen_li_emit_store_local(b, oslot);
    }
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// Phase 16 (16.B): a call argument that is an EXPRESSION rather than a bare literal/local — namely
// a parenthesized expression, or a primary immediately followed by a binary operator (e.g.
// `"x = " + n`). Such an argument is evaluated into $w0 (auto-`to_string`-on-`+` happens inside
// eval_expr_prec) and spilled to a temp local, then passed as a local-slot CallArg — reusing the
// existing marshalling. *temps_used is bumped; the caller restores ctx->ta->next_temp after the
// call. Returns 1 if it consumed an expression argument, 0 if `*p` is a simple literal/local form
// the caller should handle directly. `locals`/`nlocals` is the current named-local table.
static int try_lower_call_arg_expr(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                                   ImportBinding* locals, int nlocals,
                                   CallArg* arg, int* temps_used) {
    int is_paren = (p->current_token.type == TOKEN_LPAREN);
    int is_str   = (p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT);
    int is_prim  = (is_str || p->current_token.type == TOKEN_LIT_INT ||
                    (p->current_token.type == TOKEN_IDENTIFIER &&
                     bind_lookup(locals, nlocals, p->current_token.lexeme) >= 0));
    // Phase 16.C: a bare interpolated literal `"{n}"` (no trailing operator) is also an expression.
    int is_interp = is_str && strlit_is_interp(p->current_token.lexeme);
    // Phase 15 (15.A/15.B): a `obj.method(args)` static method call or a `g.method(args)` dynamic
    // trait dispatch is an expression argument too (both were previously skipped, so `emit(p.m())` /
    // `emit(g.m())` lowered the call to nothing and passed 0). eval_expr_prec -> eval_primary now
    // claims either; route through here so it is spilled to a temp like any expression arg.
    int is_member_call = is_member_call_head(p) || is_trait_dispatch_head(p);
    if (!is_paren && !is_interp && !is_member_call &&
        !(is_prim && p12_tok_prec(p->peek_token.type) > 0)) return 0;
    TempAlloc* ta = ctx->ta;
    eval_expr_prec(b, p, ctx, 1, ta);                 // expression → $w0 (string ptr or int)
    int t = ta->next_temp++;
    if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    codegen_li_emit_store_local(b, t);                // spill so subsequent args don't clobber it
    arg->is_string = 0; arg->sval = NULL; arg->ival = 0; arg->is_local = 1; arg->slot = t;
    (*temps_used)++;
    return 1;
}

// `NAME(arg, …);` extern call inside a body. Args may be EXPRESSIONS (Phase 16.B — incl. string
// concatenation `"x = " + n`), string/int literals, or named locals (loaded from their slot),
// staged via OP_SETARG with the last left in $w0. Returns 1 if it resolved NAME to a registered
// import, else 0 (caller skips the token).
static int lower_call_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    int idx = bind_lookup(*env->binds, *env->nb, p->current_token.lexeme);
    if (idx < 0) return 0;
    fe_advance(p); // NAME
    fe_advance(p); // '('
    env_sync(env); // position ctx->locals + temp allocator before evaluating any expression arg
    CallArg args[16];
    int nargs = 0, temps_used = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if (nargs < 16 && try_lower_call_arg_expr(b, p, env->ctx, *env->locals, *env->nlocals,
                                                  &args[nargs], &temps_used)) {
            nargs++;
        } else if (nargs < 16 && is_simd_head(p)) {
            // Phase 18 (18.E.4): a bare `simd.sum(<expr>)` call argument leaves the scalar sum (i32)
            // in $w0, spilled to a temp like the codec case (so `emit(simd.sum(a))` passes the value).
            lower_simd(b, p, env->ctx);                       // $w0 = scalar sum
            int t = env->ctx->ta->next_temp++;
            if (env->ctx->ta->next_temp > env->ctx->ta->hw) env->ctx->ta->hw = env->ctx->ta->next_temp;
            codegen_li_emit_store_local(b, t);
            args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
            args[nargs].is_local = 1; args[nargs].slot = t;
            nargs++; temps_used++;
        } else if (nargs < 16 && (p->current_token.type == TOKEN_LIT_STR ||
                           p->current_token.type == TOKEN_STRING_LIT)) {
            args[nargs].is_string = 1; args[nargs].sval = strip_quotes(p->current_token.lexeme);
            args[nargs].ival = 0; args[nargs].is_local = 0; args[nargs].slot = 0;
            nargs++; fe_advance(p);
        } else if (nargs < 16 && p->current_token.type == TOKEN_LIT_INT) {
            args[nargs].is_string = 0; args[nargs].sval = NULL;
            args[nargs].ival = (int)literal_canonical_value(&p->current_token);
            args[nargs].is_local = 0; args[nargs].slot = 0;
            nargs++; fe_advance(p);
        } else if (nargs < 16 && p->current_token.type == TOKEN_IDENTIFIER &&
                   bind_lookup(*env->locals, *env->nlocals, p->current_token.lexeme) >= 0) {
            args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
            args[nargs].is_local = 1;
            args[nargs].slot = bind_lookup(*env->locals, *env->nlocals, p->current_token.lexeme);
            nargs++; fe_advance(p);
        } else if (p->current_token.type == TOKEN_COMMA) {
            fe_advance(p);
        } else {
            fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    lower_call(b, idx, args, nargs);
    env->ctx->ta->next_temp -= temps_used; // free expression-arg spill temps
    for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
    return 1;
}

// Phase 15 (15.A): `ClassName(...)` instantiation -> OP_OBJ_NEW(nfields) -> handle in $w0. Current
// token is the class-name IDENTIFIER (peek '('). Constructor arguments are skipped in the MVP
// (fields are assigned explicitly after construction); the field COUNT is the class's compile-time
// layout size, so the instance has exactly the right number of zero-initialized cells.
static void lower_instantiation(BytecodeBuffer* b, Parser* p) {
    // Resolve the concrete class name. Three forms: `Class()`, `T()` (the active type-param, 15.C
    // substitution), and `Box<Arg>()` (a generic template -> the monomorphized class "Box$Arg").
    char cname[200];
    if (p->peek_token.type == TOKEN_LT && is_generic_template(p->current_token.lexeme)) {
        char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
        fe_advance(p); // template name
        fe_advance(p); // '<'
        char arg[96]; arg[0] = '\0';
        if (p->current_token.type == TOKEN_IDENTIFIER) {
            strncpy(arg, resolve_type_name(p->current_token.lexeme), 95); arg[95] = '\0';
        }
        while (p->current_token.type != TOKEN_GT && p->current_token.type != TOKEN_LPAREN &&
               p->current_token.type != TOKEN_EOF) fe_advance(p);
        if (p->current_token.type == TOKEN_GT) fe_advance(p); // '>'
        snprintf(cname, sizeof(cname), "%s$%s", base, arg);
    } else {
        strncpy(cname, resolve_type_name(p->current_token.lexeme), sizeof(cname) - 1);
        cname[sizeof(cname) - 1] = '\0';
        fe_advance(p); // ClassName / T
    }
    int ci = class_find(cname);
    strncpy(g_last_inst_class, ci >= 0 ? cname : "", sizeof(g_last_inst_class) - 1);
    g_last_inst_class[sizeof(g_last_inst_class) - 1] = '\0';
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    int depth = 1; // skip the (possibly empty) constructor-arg list
    while (p->current_token.type != TOKEN_EOF && depth > 0) {
        if (p->current_token.type == TOKEN_LPAREN) depth++;
        else if (p->current_token.type == TOKEN_RPAREN) depth--;
        fe_advance(p); // consume incl. the closing ')'
    }
    int nf = (ci >= 0) ? g_class[ci].nfields : 0;
    codegen_li_emit_iconst(b, nf);
    codegen_li_emit_object(b, OP_OBJ_NEW); // $w0 = handle
}

// Phase 15 (15.A): `obj.method(args)` STATIC dispatch -> OP_CALL_FUNC. `obj` must be a class-typed
// local; `member` one of its methods (slot resolved at compile time). `self` (the instance handle)
// is passed as arg0, then each explicit arg (int / named local). The result lands in $w0. Returns
// 1 if consumed, 0 if not a class method call (e.g. duplex.*/a non-class dotted ident).
static int lower_member_call(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    int ci = localcls_get(base);
    if (ci < 0) return 0;
    int midx = class_method_idx(ci, member);
    if (midx < 0) return 0;
    int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, base) : -1;
    if (hslot < 0) return 0;
    int slot = g_class[ci].method_slot[midx];
    fe_advance(p);                                       // consume "obj.method"
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    codegen_li_emit_load_local(b, hslot);               // $w0 = self handle
    codegen_li_emit_setarg(b, 0);                       // $a0 = self
    int argc = 1;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF && argc < 8) {
        if (p->current_token.type == TOKEN_COMMA) { fe_advance(p); continue; }
        lower_codec_value(b, p, ctx);                   // arg -> $w0 (int / named local)
        codegen_li_emit_setarg(b, argc);                // $a{argc} = arg
        argc++;
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_iconst(b, slot);                    // $w0 = method slot (static dispatch)
    codegen_li_emit_call_func(b, argc);                 // $w0 = method result
    return 1;
}

// Phase 18 (18.B): SAFE NAVIGATION `obj?.member` / `obj?.method(args)` — a null-propagating member
// access over an OPTIONAL object. `obj` is an optional local (its class is known from the `?Class`
// annotation even when its value is null); `member` is one of the class's fields or methods. The
// result is itself an OPTIONAL: present iff `obj` is present, payload = the member access — so a
// following Elvis (`obj?.m() ?? d`) supplies the default on null, and the access is SKIPPED entirely
// when `obj` is null (no OP_OBJ_GET/CALL_FUNC on a null handle). Lowering reuses OP_IF (the present
// guard → native je/cbz / WASM `(if)`) + the existing OBJ_GET / CALL_FUNC member emission — no new
// IL/runtime. Current token is the receiver IDENTIFIER, peek is TOKEN_SAFE_DOT. The chain present
// flag is parked in a temp and exposed via g_prim_present_slot for the caller's `??`.
static int lower_safe_nav(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    char objname[96];
    strncpy(objname, p->current_token.lexeme, sizeof(objname) - 1); objname[sizeof(objname) - 1] = '\0';
    int hslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, objname) : -1;
    int present_slot = localopt_present_slot(objname);   // -1 if not declared optional
    int ci = localcls_get(objname);                       // the receiver's class (from `?Class`)
    fe_advance(p); // consume the receiver identifier
    fe_advance(p); // consume `?.`
    char member[96];
    strncpy(member, p->current_token.lexeme, sizeof(member) - 1); member[sizeof(member) - 1] = '\0';
    int is_call = (p->peek_token.type == TOKEN_LPAREN);
    TempAlloc* ta = ctx->ta;
    // present_t — the chain's present flag (companion value, or 1 for a non-optional receiver). Kept
    // allocated past this call so the caller's Elvis can read it (g_prim_present_slot).
    int present_t = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    if (present_slot >= 0) codegen_li_emit_load_local(b, present_slot);
    else                   codegen_li_emit_iconst(b, 1);
    codegen_li_emit_store_local(b, present_t);
    int payload_t = ta->next_temp++; if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
    codegen_li_emit_iconst(b, 0);                          // default payload = 0 (absent)
    codegen_li_emit_store_local(b, payload_t);
    codegen_li_emit_load_local(b, present_t);
    codegen_li_emit_cf(b, OP_IF_BEGIN);                    // if obj present … (skipped when null)
    if (is_call) {
        int midx = (ci >= 0) ? class_method_idx(ci, member) : -1;
        int slot = (midx >= 0) ? g_class[ci].method_slot[midx] : 0;
        fe_advance(p);                                     // consume member name
        if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
        if (hslot >= 0) codegen_li_emit_load_local(b, hslot); else codegen_li_emit_iconst(b, 0);
        codegen_li_emit_setarg(b, 0);                      // $a0 = self
        int argc = 1;
        while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF && argc < 8) {
            if (p->current_token.type == TOKEN_COMMA) { fe_advance(p); continue; }
            lower_codec_value(b, p, ctx);                  // arg -> $w0
            codegen_li_emit_setarg(b, argc);
            argc++;
        }
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
        codegen_li_emit_iconst(b, slot);                   // $w0 = method slot (static dispatch)
        codegen_li_emit_call_func(b, argc);                // $w0 = method result
    } else {
        int fidx = (ci >= 0) ? class_field_idx(ci, member) : -1;
        fe_advance(p);                                     // consume member name
        if (hslot >= 0) codegen_li_emit_load_local(b, hslot); else codegen_li_emit_iconst(b, 0);
        codegen_li_emit_setarg(b, 0);                      // $a0 = handle
        codegen_li_emit_iconst(b, fidx >= 0 ? fidx : 0);   // $w0 = field index
        codegen_li_emit_object(b, OP_OBJ_GET);             // $w0 = field value
    }
    codegen_li_emit_store_local(b, payload_t);             // payload_t = member result (when present)
    codegen_li_emit_cf(b, OP_IF_END);
    codegen_li_emit_load_local(b, payload_t);              // $w0 = result (or 0 when absent)
    ta->next_temp--;                                       // free payload_t; KEEP present_t for Elvis
    g_prim_present_slot = present_t;                       // expose the chain present flag to `??`
    return TEKO_VT_INT;                                    // MVP: int member results (object/string later)
}

// Phase 15 (15.A): `obj.field = <expr>;` field WRITE -> OP_OBJ_SET(handle, idx, value). Returns 1
// if consumed. The value is evaluated FIRST into a temp (it may itself read `obj.field` via
// OP_OBJ_GET, which stages $a0 — so we must not have the SET's handle/idx staged yet).
static int lower_member_write(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_ASSIGN) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    int ci = localcls_get(base);
    if (ci < 0) return 0;
    int fidx = class_field_idx(ci, member);
    if (fidx < 0) return 0;
    int hslot = bind_lookup(*env->locals, *env->nlocals, base);
    if (hslot < 0) return 0;
    fe_advance(p); // "obj.field"
    fe_advance(p); // '='
    env_sync(env);
    lower_init_value(b, p, env);                        // value -> $w0 (may use $a0 via member reads)
    int t = env->ctx->ta->next_temp++;
    if (env->ctx->ta->next_temp > env->ctx->ta->hw) env->ctx->ta->hw = env->ctx->ta->next_temp;
    codegen_li_emit_store_local(b, t);                  // temp = value
    codegen_li_emit_load_local(b, hslot); codegen_li_emit_setarg(b, 0); // $a0 = handle
    codegen_li_emit_iconst(b, fidx);      codegen_li_emit_setarg(b, 1); // $a1 = field index
    codegen_li_emit_load_local(b, t);                  // $w0 = value
    codegen_li_emit_object(b, OP_OBJ_SET);
    env->ctx->ta->next_temp--;                          // free the temp
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// Phase 18 (18.E.1): `a[i] = <expr>;` INDEX WRITE -> OP_ARR_SET(handle, idx, value). `a` is an array
// local (current token), peek is `[`. Mirrors lower_member_write's ordering: the index AND value are
// evaluated into temps FIRST (each may itself stage $a0 via an index/member read), then handle=$a0 /
// index=$a1 are staged and the value (in $w0) is set. Out-of-range traps fail-loud in the runtime.
// Returns 1 if consumed, 0 if `a` is not an array local (so the caller tries other statement forms).
static int lower_array_index_write(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    if (!localanyarr_get(p->current_token.lexeme)) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int hslot = bind_lookup(*env->locals, *env->nlocals, base);
    if (hslot < 0) return 0;
    fe_advance(p); // consume the array name
    fe_advance(p); // consume '['
    // Park the index + value in HIDDEN NAMED LOCALS (permanent $v slots), not temps: lower_init_value
    // re-syncs the temp allocator internally (resetting next_temp), which would clobber a temp
    // reservation made before it. Named locals sit below the temp region and survive that re-sync.
    static int g_arrset_seq = 0;
    char inm[120], vnm[120];
    snprintf(inm, sizeof(inm), "__arrset_i_%d", g_arrset_seq);
    snprintf(vnm, sizeof(vnm), "__arrset_v_%d", g_arrset_seq);
    g_arrset_seq++;
    int it = env_alloc_local(env, inm);
    int vt = env_alloc_local(env, vnm);
    env_sync(env);
    eval_expr_prec(b, p, env->ctx, 1, env->ctx->ta);    // index -> $w0
    codegen_li_emit_store_local(b, it);                 // it = index
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    if (p->current_token.type == TOKEN_ASSIGN) fe_advance(p);
    lower_init_value(b, p, env);                        // value -> $w0 (may use $a0 via reads)
    codegen_li_emit_store_local(b, vt);                 // vt = value
    codegen_li_emit_load_local(b, hslot); codegen_li_emit_setarg(b, 0); // $a0 = handle
    codegen_li_emit_load_local(b, it);    codegen_li_emit_setarg(b, 1); // $a1 = index
    codegen_li_emit_load_local(b, vt);                  // $w0 = value
    emit_arr_op(b, arr_op_for(base, OP_ARR_SET));       // i64 OR typed-i32 family
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// Phase 18 (18.E.3): `s[i].field = <expr>;` SoA INDEX-then-FIELD WRITE -> OP_IARR_SET(field-run, i, v).
// `s` is a SoA local (current token), peek `[`. Resolves the field run handle (`s#f<idx>`), evaluates
// the index + value into hidden named locals first (each may stage $a0 via reads), then stages
// handle=$a0 / index=$a1 / value=$w0 and writes. Returns 1 if consumed, 0 if `s` is not a SoA local.
static int lower_soa_index_write(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    int si = localsoa_find(p->current_token.lexeme);
    if (si < 0) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int ci = g_localsoa[si].ci;
    fe_advance(p); // consume the SoA name
    fe_advance(p); // consume '['
    static int g_soaset_seq = 0;
    char inm[120], vnm[120];
    snprintf(inm, sizeof(inm), "__soaset_i_%d", g_soaset_seq);
    snprintf(vnm, sizeof(vnm), "__soaset_v_%d", g_soaset_seq);
    g_soaset_seq++;
    int it = env_alloc_local(env, inm);
    int vt = env_alloc_local(env, vnm);
    env_sync(env);
    eval_expr_prec(b, p, env->ctx, 1, env->ctx->ta);    // index -> $w0
    codegen_li_emit_store_local(b, it);                 // it = index
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    if (p->current_token.type == TOKEN_DOT) fe_advance(p); // '.'
    int fidx = -1;
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        fidx = class_field_idx(ci, p->current_token.lexeme);
        fe_advance(p);                                  // field name
    }
    if (fidx < 0) fidx = 0;
    if (p->current_token.type == TOKEN_ASSIGN) fe_advance(p);
    lower_init_value(b, p, env);                        // value -> $w0
    codegen_li_emit_store_local(b, vt);                 // vt = value
    char fnm[120]; snprintf(fnm, sizeof(fnm), "%s#f%d", base, fidx);
    int fslot = bind_lookup(*env->locals, *env->nlocals, fnm);
    if (fslot >= 0) codegen_li_emit_load_local(b, fslot); else codegen_li_emit_iconst(b, 0);
    codegen_li_emit_setarg(b, 0);                       // $a0 = field-run handle
    codegen_li_emit_load_local(b, it); codegen_li_emit_setarg(b, 1); // $a1 = index
    codegen_li_emit_load_local(b, vt);                  // $w0 = value
    codegen_li_emit_iarray(b, OP_IARR_SET);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// Phase 18 (18.E.3): `a[i].field = <expr>;` AoS INDEX-then-FIELD WRITE -> ARR_GET(a,i)=handle then
// OP_OBJ_SET(handle, field_idx, v). `a` is an i64 array of object handles with a known element class
// (g_localaos). Returns 1 if consumed, 0 if `a` is not an AoS-of-class array.
static int lower_aos_index_write(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LBRACKET) return 0;
    int ci = localaos_get(p->current_token.lexeme);
    if (ci < 0) return 0;
    char base[96]; strncpy(base, p->current_token.lexeme, 95); base[95] = '\0';
    int hslot = bind_lookup(*env->locals, *env->nlocals, base);
    if (hslot < 0) return 0;
    fe_advance(p); // consume the array name
    fe_advance(p); // consume '['
    static int g_aosset_seq = 0;
    char inm[120], vnm[120], onm[120];
    snprintf(inm, sizeof(inm), "__aosset_i_%d", g_aosset_seq);
    snprintf(vnm, sizeof(vnm), "__aosset_v_%d", g_aosset_seq);
    snprintf(onm, sizeof(onm), "__aosset_o_%d", g_aosset_seq);
    g_aosset_seq++;
    int it = env_alloc_local(env, inm);
    int vt = env_alloc_local(env, vnm);
    int ot = env_alloc_local(env, onm);
    env_sync(env);
    eval_expr_prec(b, p, env->ctx, 1, env->ctx->ta);    // index -> $w0
    codegen_li_emit_store_local(b, it);                 // it = index
    if (p->current_token.type == TOKEN_RBRACKET) fe_advance(p);
    if (p->current_token.type == TOKEN_DOT) fe_advance(p); // '.'
    int fidx = -1;
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        fidx = class_field_idx(ci, p->current_token.lexeme);
        fe_advance(p);                                  // field name
    }
    if (fidx < 0) fidx = 0;
    if (p->current_token.type == TOKEN_ASSIGN) fe_advance(p);
    lower_init_value(b, p, env);                        // value -> $w0
    codegen_li_emit_store_local(b, vt);                 // vt = value
    // object handle = ARR_GET(a, i)
    codegen_li_emit_load_local(b, hslot); codegen_li_emit_setarg(b, 0); // $a0 = array handle
    codegen_li_emit_load_local(b, it);                                  // $w0 = index
    codegen_li_emit_array(b, OP_ARR_GET);               // $w0 = object handle
    codegen_li_emit_store_local(b, ot);                 // ot = object handle
    codegen_li_emit_load_local(b, ot); codegen_li_emit_setarg(b, 0);   // $a0 = object handle
    codegen_li_emit_iconst(b, fidx);   codegen_li_emit_setarg(b, 1);   // $a1 = field index
    codegen_li_emit_load_local(b, vt);                  // $w0 = value
    codegen_li_emit_object(b, OP_OBJ_SET);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// Phase 15 (15.A): a class member STATEMENT — `obj.field = expr;`, `obj.method(args);` (result
// discarded). Returns 1 if consumed. Shared by the top-level loop and the block dispatcher.
// Phase 15 (15.B): DYNAMIC dispatch `g.method(args)` where `g` is a FAT trait-typed local. Resolves
// the routine slot at runtime via vtable_get(g.type_id, method_id), then OP_CALL_FUNC with g's handle
// as self. Returns 1 if consumed (the receiver is a trait local + `method` is one of the trait's
// methods), 0 otherwise (so a concrete receiver falls through to 15.A static dispatch). The slot is
// parked in a temp because OP_CALL_FUNC needs $w0=slot while its args occupy $a0.. (and VTABLE_GET
// itself clobbers $a0/$w0). Result in $w0.
// The ctx-only core: assumes ctx->locals is already positioned (the LowerEnv* wrapper below syncs
// first; the shared evaluators sync before they reach a primary). Reachable from eval_primary /
// lower_codec_value / try_lower_call_arg_expr so a trait dispatch works in argument / sub-expression
// position too — not only as a let-RHS or a discarded statement.
static int lower_trait_dispatch_ctx(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
    int tl = traitlocal_find(base);
    if (tl < 0) return 0;
    int trait = g_traitlocal[tl].trait;
    if (trait_method_idx(trait, member) < 0) return 0; // not a method of `g`'s trait
    int mid = methodid_of(member);
    int handle_slot = g_traitlocal[tl].handle_slot;
    int tid_slot = g_traitlocal[tl].tid_slot;
    fe_advance(p); // consume "g.method"
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    // slot = vtable_get(type_id, method_id)  (type_id read at RUNTIME from the fat local)
    codegen_li_emit_load_local(b, tid_slot); codegen_li_emit_setarg(b, 0); // $a0 = type_id
    codegen_li_emit_iconst(b, mid);                                        // $w0 = method_id
    codegen_li_emit_vtable(b, OP_VTABLE_GET);                              // $w0 = routine slot
    int slot_tmp = ctx->ta->next_temp++;
    if (ctx->ta->next_temp > ctx->ta->hw) ctx->ta->hw = ctx->ta->next_temp;
    codegen_li_emit_store_local(b, slot_tmp);                             // park the slot
    // stage self + explicit args, then call the resolved slot
    codegen_li_emit_load_local(b, handle_slot); codegen_li_emit_setarg(b, 0); // $a0 = self
    int argc = 1;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF && argc < 8) {
        if (p->current_token.type == TOKEN_COMMA) { fe_advance(p); continue; }
        lower_codec_value(b, p, ctx);        // arg -> $w0 (int / named local)
        codegen_li_emit_setarg(b, argc); argc++;
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_load_local(b, slot_tmp); // $w0 = slot
    codegen_li_emit_call_func(b, argc);      // $w0 = method result (dynamic dispatch)
    ctx->ta->next_temp--;                    // free the parked-slot temp
    return 1;
}
static int lower_trait_dispatch(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    // The LowerEnv* entry point (let-RHS / statement): position ctx->locals, then delegate to the
    // ctx-only core. Cheaply pre-checked so a non-trait head skips the env_sync and leaves `*p` intact.
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    env_sync(env);
    return lower_trait_dispatch_ctx(b, p, env->ctx);
}

static int lower_member_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (lower_member_write(b, p, env)) return 1;
    if (lower_trait_dispatch(b, p, env)) { // Phase 15.B: `g.method(args);` dynamic call (discarded)
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
        return 1;
    }
    if (p->current_token.type == TOKEN_IDENTIFIER && p->peek_token.type == TOKEN_LPAREN) {
        char base[96], member[96];
        if (dotted_split(p->current_token.lexeme, base, member) &&
            localcls_get(base) >= 0 && class_method_idx(localcls_get(base), member) >= 0) {
            env_sync(env);
            lower_member_call(b, p, env->ctx);          // result discarded
            if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
            return 1;
        }
    }
    return 0;
}

// `while (cond) { body }` — LOOP_BEGIN; <cond→$w0>; BREAK_IF_FALSE; body; LOOP_END.
static void lower_while(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    fe_advance(p); // 'while'
    codegen_li_emit_cf(b, OP_LOOP_BEGIN);
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    env_sync(env);
    eval_expr_prec(b, p, env->ctx, 1, env->ctx->ta); // condition -> $w0
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_cf(b, OP_BREAK_IF_FALSE);
    lower_block(b, p, env);
    codegen_li_emit_cf(b, OP_LOOP_END);
}

// Phase 18 (18.E.2): `for NAME in ARR { body }` — index iteration over ANY array (plain i64 OR typed
// i32). Reuses the control-flow foundation: a hidden index local walks `[0, ARR.len)`, binding NAME to
// `ARR[i]` each pass, then the body lowers like any block.
//   __for_i = 0
//   LOOP_BEGIN
//     $w0 = (__for_i < ARR.len)   ; BREAK_IF_FALSE
//     NAME = ARR[__for_i]         ; (the right GET op family for ARR)
//     <body>
//     __for_i = __for_i + 1
//   LOOP_END
// `in` is a CONTEXTUAL keyword (there is no TOKEN_IN): an IDENTIFIER whose lexeme is "in".
static void lower_for(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    fe_advance(p); // 'for'
    char vname[96]; vname[0] = '\0';
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        strncpy(vname, p->current_token.lexeme, sizeof(vname) - 1); vname[sizeof(vname) - 1] = '\0';
        fe_advance(p);
    }
    // Contextual `in`.
    if (p->current_token.type == TOKEN_IDENTIFIER && strcmp(p->current_token.lexeme, "in") == 0)
        fe_advance(p);
    // Array base name — resolve its handle slot + op family (i64 vs typed-i32).
    char aname[96]; aname[0] = '\0';
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        strncpy(aname, p->current_token.lexeme, sizeof(aname) - 1); aname[sizeof(aname) - 1] = '\0';
        fe_advance(p);
    }
    int hslot = bind_lookup(*env->locals, *env->nlocals, aname);
    int is_arr = localanyarr_get(aname) && hslot >= 0;
    OpCode op_get = arr_op_for(aname, OP_ARR_GET);
    OpCode op_len = arr_op_for(aname, OP_ARR_LEN);
    // Hidden index local + the loop var local (a plain int local the body reads). Each iteration's
    // NAME = ARR[__for_i] is stored into the loop var's slot.
    static int g_for_seq = 0;
    char inm[120]; snprintf(inm, sizeof(inm), "__for_i_%d", g_for_seq); g_for_seq++;
    int islot = env_alloc_local(env, inm);
    int vslot = bind_lookup(*env->locals, *env->nlocals, vname);
    if (vslot < 0) vslot = env_alloc_local(env, vname);
    // __for_i = 0
    codegen_li_emit_iconst(b, 0);
    codegen_li_emit_store_local(b, islot);
    codegen_li_emit_cf(b, OP_LOOP_BEGIN);
    if (is_arr) {
        // $w0 = (__for_i < ARR.len): len -> $w1, __for_i -> $w0, OP_LT ($w0 = $w0 < $w1).
        codegen_li_emit_load_local(b, hslot);   // $w0 = handle
        emit_arr_op(b, op_len);                 // $w0 = len
        codegen_li_emit_store(b);               // $w1 = len
        codegen_li_emit_load_local(b, islot);   // $w0 = __for_i
        codegen_li_emit_binop(b, OP_LT);        // $w0 = (__for_i < len)
    } else {
        // Not an array (a well-typed program never hits this) — never enter the loop.
        codegen_li_emit_iconst(b, 0);
    }
    codegen_li_emit_cf(b, OP_BREAK_IF_FALSE);
    if (is_arr) {
        // NAME = ARR[__for_i]
        codegen_li_emit_load_local(b, hslot);   // $w0 = handle
        codegen_li_emit_setarg(b, 0);           // $a0 = handle
        codegen_li_emit_load_local(b, islot);   // $w0 = __for_i (index)
        emit_arr_op(b, op_get);                 // $w0 = element
        codegen_li_emit_store_local(b, vslot);  // NAME = element
    }
    lower_block(b, p, env);                      // body
    // __for_i = __for_i + 1  (__for_i -> $w1, 1 -> $w0, OP_ADD)
    codegen_li_emit_load_local(b, islot);
    codegen_li_emit_store(b);                    // $w1 = __for_i
    codegen_li_emit_iconst(b, 1);                // $w0 = 1
    codegen_li_emit_binop(b, OP_ADD);            // $w0 = __for_i + 1
    codegen_li_emit_store_local(b, islot);
    codegen_li_emit_cf(b, OP_LOOP_END);
}

// `loop { body }` — an infinite loop exited only by `break` (LOOP_BEGIN; body; LOOP_END).
static void lower_loop(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    fe_advance(p); // 'loop'
    codegen_li_emit_cf(b, OP_LOOP_BEGIN);
    lower_block(b, p, env);
    codegen_li_emit_cf(b, OP_LOOP_END);
}

// `if (cond) { body }` — enter the body iff cond is non-zero (<cond→$w0>; IF_BEGIN; body; IF_END).
static void lower_if(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    fe_advance(p); // 'if'
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    env_sync(env);
    eval_expr_prec(b, p, env->ctx, 1, env->ctx->ta); // condition -> $w0
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_cf(b, OP_IF_BEGIN);
    lower_block(b, p, env);
    codegen_li_emit_cf(b, OP_IF_END);
}

// Dispatch ONE statement inside a body. Always makes progress (advances at least one token).
static void lower_one_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    LowerCtx* ctx = env->ctx;
    if ((p->current_token.type == TOKEN_LET || p->current_token.type == TOKEN_MUT) &&
        p->peek_token.type == TOKEN_IDENTIFIER) {
        lower_let_stmt(b, p, env);
    } else if (p->current_token.type == TOKEN_WHILE) {
        lower_while(b, p, env);
    } else if (p->current_token.type == TOKEN_FOR) {
        lower_for(b, p, env);                     // Phase 18 (18.E.2): for NAME in ARR { }
    } else if (p->current_token.type == TOKEN_LOOP) {
        lower_loop(b, p, env);
    } else if (p->current_token.type == TOKEN_IF) {
        lower_if(b, p, env);
    } else if (p->current_token.type == TOKEN_BREAK) {
        fe_advance(p); codegen_li_emit_cf(b, OP_BREAK);
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_CONTINUE) {
        fe_advance(p); codegen_li_emit_cf(b, OP_CONTINUE);
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_WAIT || p->current_token.type == TOKEN_AWAIT) {
        env_sync(env);
        lower_wait_await(b, p, ctx, p->current_token.type == TOKEN_AWAIT);
    } else if (p->current_token.type == TOKEN_RETURN) {
        // Phase 15 (15.A): `return <expr>;` — evaluate the expression into $w0 (the routine's
        // result; a native routine leaves $w0 in rax across `ret`, WASM spills it at FUNC_END).
        // MVP: no early-return control flow — `return` is expected as a method's last statement.
        fe_advance(p);
        if (p->current_token.type != TOKEN_SEMICOLON && p->current_token.type != TOKEN_RBRACE &&
            p->current_token.type != TOKEN_EOF) {
            env_sync(env);
            lower_init_value(b, p, env); // value -> $w0 (member reads/calls, arithmetic, ints, locals)
        }
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_MACRO_IDENT && is_dom_macro(p->current_token.lexeme) &&
               p->peek_token.type == TOKEN_LPAREN) {
        lower_intrinsic_call(b, p, ctx);
    } else if (is_duplex_head(p)) {
        lower_duplex_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (is_delayed_head(p)) {
        lower_delayed_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (is_bcast_head(p)) {
        lower_bcast_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (is_atomic_head(p)) {
        lower_atomic_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_RETRY) {
        lower_retry_block(b, p, env);     // 14.F: retry { } fallback { }
    } else if (p->current_token.type == TOKEN_CIRCUIT &&
               p->peek_token.type == TOKEN_IDENTIFIER) {
        lower_circuit_block(b, p, env);   // 14.F: circuit cb { } fallback { }
    } else if (lower_soa_index_write(b, p, env)) {
        /* Phase 18 (18.E.3): s[i].field = expr; consumed (before the plain index write) */
    } else if (lower_aos_index_write(b, p, env)) {
        /* Phase 18 (18.E.3): a[i].field = expr; consumed (before the plain index write) */
    } else if (lower_array_index_write(b, p, env)) {
        /* Phase 18 (18.E.1): a[i] = expr; consumed */
    } else if (lower_member_stmt(b, p, env)) {
        /* Phase 15: obj.field = expr; / obj.method(args); consumed */
    } else if (lower_reassign(b, p, env)) {
        /* NAME = expr; consumed */
    } else if (lower_call_stmt(b, p, env)) {
        /* NAME(args); consumed */
    } else if (p->current_token.type == TOKEN_LIT_INT || p->current_token.type == TOKEN_LPAREN ||
               p->current_token.type == TOKEN_IDENTIFIER) {
        // Bare expression statement (e.g. a retry/circuit body's trailing ok/fail expression):
        // evaluate it into $w0 so the enclosing block's result is the last expression.
        env_sync(env);
        eval_expr_prec(b, p, ctx, 1, ctx->ta);
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else {
        fe_advance(p); // skip anything else in this subset (always progress)
    }
}

// Lower a `{ … }` block body: statements until the matching `}`.
static void lower_block(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type == TOKEN_LBRACE) fe_advance(p);
    while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
        lower_one_stmt(b, p, env);
    }
    if (p->current_token.type == TOKEN_RBRACE) fe_advance(p);
}

// Skip a `{ … }` block without lowering it (consume the matched braces). Used when a fallback
// block is absent or when a body must be parsed but not emitted on a given path.
static void skip_brace_block(Parser* p) {
    if (p->current_token.type != TOKEN_LBRACE) return;
    int depth = 0;
    while (p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_LBRACE) { depth++; fe_advance(p); }
        else if (p->current_token.type == TOKEN_RBRACE) { depth--; fe_advance(p); if (depth == 0) break; }
        else fe_advance(p);
    }
}

// `retry (attempts N, [timeout T,] exponential|logarithmic, base B) { body } [fallback { fb }]`
// (Phase 14, 14.F). Drives the teko_retry C policy through the control-flow foundation: a loop
// gated by teko_retry_should_continue runs the body each attempt; the body's trailing expression
// is its ok/fail result (non-zero = success). On success it breaks; when the policy gives up it
// runs `fallback`. Backoff (teko_retry_next_delay) accumulates `elapsed` for the timeout rule.
static void lower_retry_block(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    static int uid = 0; int id = uid++;
    fe_advance(p); // 'retry'
    int attempts = 3, timeout = 0, mode = 0, base = 1;
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF &&
           p->current_token.type != TOKEN_LBRACE) {
        if (p->current_token.type == TOKEN_ATTEMPTS) {
            fe_advance(p);
            if (p->current_token.type == TOKEN_LIT_INT) { attempts = (int)literal_canonical_value(&p->current_token); fe_advance(p); }
        } else if (p->current_token.type == TOKEN_TIMEOUT) {
            fe_advance(p);
            if (p->current_token.type == TOKEN_LIT_INT) { timeout = (int)literal_canonical_value(&p->current_token); fe_advance(p); }
        } else if (p->current_token.type == TOKEN_EXPONENTIAL) { mode = 0; fe_advance(p); }
        else if (p->current_token.type == TOKEN_LOGARITHMIC) { mode = 1; fe_advance(p); }
        else if (p->current_token.type == TOKEN_IDENTIFIER && strcmp(p->current_token.lexeme, "base") == 0) {
            fe_advance(p);
            if (p->current_token.type == TOKEN_LIT_INT) { base = (int)literal_canonical_value(&p->current_token); fe_advance(p); }
        } else { fe_advance(p); }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);

    char nm[64];
    snprintf(nm, sizeof(nm), "__retry_pol_%d", id);  int s_pol  = env_alloc_local(env, nm);
    snprintf(nm, sizeof(nm), "__retry_att_%d", id);  int s_att  = env_alloc_local(env, nm);
    snprintf(nm, sizeof(nm), "__retry_ok_%d",  id);  int s_ok   = env_alloc_local(env, nm);
    snprintf(nm, sizeof(nm), "__retry_suc_%d", id);  int s_suc  = env_alloc_local(env, nm);

    // policy = retry_new(attempts, timeout, mode, base) — records its real start instant.
    codegen_li_emit_iconst(b, attempts); codegen_li_emit_setarg(b, 0);
    codegen_li_emit_iconst(b, timeout);  codegen_li_emit_setarg(b, 1);
    codegen_li_emit_iconst(b, mode);     codegen_li_emit_setarg(b, 2);
    codegen_li_emit_iconst(b, base);     // last arg in $w0
    codegen_li_emit_retry(b, OP_RETRY_NEW);
    codegen_li_emit_store_local(b, s_pol);
    emit_set_local_const(b, s_att, 0); // attempt = 0
    emit_set_local_const(b, s_suc, 0); // succeeded = 0

    codegen_li_emit_cf(b, OP_LOOP_BEGIN);
    //   if (should_continue(policy, attempt) == 0) break — the policy reads REAL elapsed internally
    codegen_li_emit_load_local(b, s_pol); codegen_li_emit_setarg(b, 0);
    codegen_li_emit_load_local(b, s_att); // last in $w0
    codegen_li_emit_retry(b, OP_RETRY_SHOULD_CONTINUE); // $w0 = 0/1
    codegen_li_emit_cf(b, OP_BREAK_IF_FALSE);
    //   body -> $w0 (ok/fail); ok = $w0
    lower_block(b, p, env);
    codegen_li_emit_store_local(b, s_ok);
    //   if (ok) { succeeded = 1; break }
    codegen_li_emit_load_local(b, s_ok);
    codegen_li_emit_cf(b, OP_IF_BEGIN);
    emit_set_local_const(b, s_suc, 1);
    codegen_li_emit_cf(b, OP_BREAK);
    codegen_li_emit_cf(b, OP_IF_END);
    //   back off for next_delay(policy, attempt) ms on the REAL clock (accumulates real elapsed,
    //   which drives the timeout budget); then attempt += 1.
    codegen_li_emit_load_local(b, s_pol); codegen_li_emit_setarg(b, 0);
    codegen_li_emit_load_local(b, s_att); // last in $w0
    codegen_li_emit_retry(b, OP_RETRY_NEXT_DELAY); // $w0 = delay ms
    codegen_li_emit_wait(b);                        // real-time backoff wait
    codegen_li_emit_iconst(b, 1);
    emit_local_add_w0(b, s_att);
    codegen_li_emit_cf(b, OP_LOOP_END);

    // fallback (optional): if (!succeeded) { fb }
    if (p->current_token.type == TOKEN_FALLBACK) {
        fe_advance(p); // 'fallback'
        emit_local_is_zero(b, s_suc);     // $w0 = (succeeded == 0)
        codegen_li_emit_cf(b, OP_IF_BEGIN);
        lower_block(b, p, env);
        codegen_li_emit_cf(b, OP_IF_END);
    }
}

// `circuit <breaker> { body } [fallback { fb }]` (Phase 14, 14.F). `breaker` is a named local
// created by `let cb = circuit(threshold K, cooldown C);`. Guards the body with
// teko_circuit_allow; records the outcome (teko_circuit_record); runs `fallback` when the call
// was short-circuited (breaker OPEN) OR the body failed. `failed` starts 1 (assume fallback) and
// is cleared only on an allowed, successful call — so the single-parse fallback emits once.
static void lower_circuit_block(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    static int uid = 0; int id = uid++;
    fe_advance(p); // 'circuit'
    int cb = -1;
    if (p->current_token.type == TOKEN_IDENTIFIER) {
        cb = bind_lookup(*env->locals, *env->nlocals, p->current_token.lexeme);
        fe_advance(p); // breaker name
    }
    char nm[64];
    snprintf(nm, sizeof(nm), "__circ_allowed_%d", id); int s_allowed = env_alloc_local(env, nm);
    snprintf(nm, sizeof(nm), "__circ_failed_%d",  id); int s_failed  = env_alloc_local(env, nm);
    snprintf(nm, sizeof(nm), "__circ_ok_%d",      id); int s_ok      = env_alloc_local(env, nm);

    if (cb < 0) { // unknown breaker: parse-and-skip the blocks so we still consume the tokens
        skip_brace_block(p);
        if (p->current_token.type == TOKEN_FALLBACK) { fe_advance(p); skip_brace_block(p); }
        return;
    }

    emit_set_local_const(b, s_failed, 1); // assume failed (fallback) until proven otherwise
    // allowed = circuit_allow(cb) — the breaker consults the REAL clock for its cooldown internally
    codegen_li_emit_load_local(b, cb); // handle in $w0
    codegen_li_emit_retry(b, OP_CIRCUIT_ALLOW); // $w0 = 0/1
    codegen_li_emit_store_local(b, s_allowed);
    //   if (allowed) { body -> ok; record(cb, ok); if (ok) failed = 0 }
    codegen_li_emit_load_local(b, s_allowed);
    codegen_li_emit_cf(b, OP_IF_BEGIN);
    lower_block(b, p, env);                 // body -> $w0
    codegen_li_emit_store_local(b, s_ok);
    codegen_li_emit_load_local(b, cb);  codegen_li_emit_setarg(b, 0);
    codegen_li_emit_load_local(b, s_ok); // ok in $w0
    codegen_li_emit_retry(b, OP_CIRCUIT_RECORD);
    codegen_li_emit_load_local(b, s_ok);
    codegen_li_emit_cf(b, OP_IF_BEGIN);
    emit_set_local_const(b, s_failed, 0); // success -> no fallback
    codegen_li_emit_cf(b, OP_IF_END);
    codegen_li_emit_cf(b, OP_IF_END);
    // fallback (optional): if (failed) { fb }  (covers OPEN fail-fast + body failure)
    if (p->current_token.type == TOKEN_FALLBACK) {
        fe_advance(p);
        codegen_li_emit_load_local(b, s_failed);
        codegen_li_emit_cf(b, OP_IF_BEGIN);
        lower_block(b, p, env);
        codegen_li_emit_cf(b, OP_IF_END);
    }
}

static int lower_routine_extern_call(BytecodeBuffer* buffer, Parser* p,
                                     ImportBinding* binds, int nb); // defined below

// `shared { … }` — the compiler injects a coarse lock around the block: emit OP_SHARED_ENTER,
// lower the enclosed statements (atomic/channel ops + extern calls in this subset), emit
// OP_SHARED_LEAVE at the matching `}`. Named-local declarations inside the block are future work.
static void lower_shared_block(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                               ImportBinding* binds, int nb) {
    fe_advance(p);                                       // consume 'shared'
    if (p->current_token.type == TOKEN_LBRACE) fe_advance(p);
    codegen_li_emit_shared(b, OP_SHARED_ENTER);
    int depth = 1;
    while (p->current_token.type != TOKEN_EOF && depth > 0) {
        if (p->current_token.type == TOKEN_LBRACE) { depth++; fe_advance(p); }
        else if (p->current_token.type == TOKEN_RBRACE) { depth--; fe_advance(p); }
        else if (is_atomic_head(p)) { lower_atomic_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p); }
        else if (is_duplex_head(p)) { lower_duplex_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p); }
        else if (is_delayed_head(p)){ lower_delayed_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p); }
        else if (is_bcast_head(p))  { lower_bcast_call(b, p, ctx); if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p); }
        else if (lower_routine_extern_call(b, p, binds, nb)) { /* extern call (e.g. emit_int) */ }
        else fe_advance(p);
    }
    codegen_li_emit_shared(b, OP_SHARED_LEAVE);
}

// Skip a whole `extern …;` / `extern { … }` declaration. Needed by the fn scanners
// below so the `fn` token INSIDE `extern fn …` is not mistaken for a handler.
static void skip_extern_decl(Parser* p) {
    fe_advance(p); // consume 'extern'
    while (p->current_token.type != TOKEN_LBRACE &&
           p->current_token.type != TOKEN_SEMICOLON &&
           p->current_token.type != TOKEN_EOF) {
        fe_advance(p);
    }
    if (p->current_token.type == TOKEN_LBRACE) {
        int depth = 0;
        while (p->current_token.type != TOKEN_EOF) {
            if (p->current_token.type == TOKEN_LBRACE) { depth++; fe_advance(p); }
            else if (p->current_token.type == TOKEN_RBRACE) {
                depth--; fe_advance(p);
                if (depth == 0) break;
            } else {
                fe_advance(p);
            }
        }
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_SEMICOLON) {
        fe_advance(p);
    }
}

// Phase 15 (15.A): skip a whole `class NAME [..] { … }` declaration (brace-matched). Current
// token is TOKEN_CLASS. Used so the fn/handler passes don't treat a class's methods as top-level
// routines, and so the main lowering pass doesn't emit a class declaration as a statement.
static void skip_class_decl(Parser* p) {
    fe_advance(p); // 'class'
    while (p->current_token.type != TOKEN_LBRACE && p->current_token.type != TOKEN_EOF) fe_advance(p);
    if (p->current_token.type == TOKEN_LBRACE) {
        int d = 1; fe_advance(p);
        while (p->current_token.type != TOKEN_EOF && d > 0) {
            if (p->current_token.type == TOKEN_LBRACE) d++;
            else if (p->current_token.type == TOKEN_RBRACE) d--;
            fe_advance(p);
        }
    }
}

// Phase 15 (15.B): pre-pass that builds the trait registry (g_trait) — each `trait NAME { fn m(self):
// T; … }` records its ordered (bodyless) contract method names and reserves a global method_id per
// name. A concrete class supplies the bodies; dynamic dispatch resolves (type_id, method_id) via the
// static vtable. Trait method names also seed the method-id space so a class method overriding a
// trait method shares the same method_id (same vtable column).
static void collect_traits(const char* source) {
    g_ntrait = 0;
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type != TOKEN_TRAIT || p.peek_token.type != TOKEN_IDENTIFIER) {
            fe_advance(&p); continue;
        }
        if (g_ntrait >= TEKO_MAX_TRAITS) { skip_class_decl(&p); continue; } // skip_class_decl is keyword-generic
        TraitInfo* t = &g_trait[g_ntrait];
        memset(t, 0, sizeof(*t));
        fe_advance(&p); // 'trait'
        strncpy(t->name, p.current_token.lexeme, 95); t->name[95] = '\0';
        fe_advance(&p); // trait name
        while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
        if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p);
        int depth = 1;
        while (p.current_token.type != TOKEN_EOF && depth > 0) {
            if (p.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&p); continue; }
            if (p.current_token.type == TOKEN_RBRACE) { depth--; fe_advance(&p); continue; }
            if (depth == 1 && p.current_token.type == TOKEN_ASYNC && p.peek_token.type == TOKEN_FN) fe_advance(&p);
            if (depth == 1 && p.current_token.type == TOKEN_FN && p.peek_token.type == TOKEN_IDENTIFIER) {
                fe_advance(&p); // 'fn'
                if (t->nmethods < TEKO_CLASS_MAX_METHODS) {
                    strncpy(t->methods[t->nmethods], p.current_token.lexeme, 95);
                    t->methods[t->nmethods][95] = '\0'; t->nmethods++;
                    methodid_of(p.current_token.lexeme); // reserve the global method_id (shared vtable column)
                }
                fe_advance(&p); // method name
                skip_generic_clause(&p);
                // A contract method ends at ';'; tolerate an (unused-in-MVP) default body `{ … }`.
                while (p.current_token.type != TOKEN_SEMICOLON && p.current_token.type != TOKEN_LBRACE &&
                       p.current_token.type != TOKEN_RBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
                if (p.current_token.type == TOKEN_SEMICOLON) fe_advance(&p);
                else if (p.current_token.type == TOKEN_LBRACE) {
                    int d2 = 1; fe_advance(&p);
                    while (p.current_token.type != TOKEN_EOF && d2 > 0) {
                        if (p.current_token.type == TOKEN_LBRACE) d2++;
                        else if (p.current_token.type == TOKEN_RBRACE) d2--;
                        fe_advance(&p);
                    }
                }
                continue;
            }
            fe_advance(&p);
        }
        g_ntrait++;
    }
}

// Phase 15 (15.B): trait-composition collision check. A class composing two traits that BOTH
// declare a method of the same name, WITHOUT the class overriding it, is ambiguous — a hard
// compile error (owner rule). Sets g_oop_error so teko_compile_interop fails instead of emitting.
static void check_oop_collisions(void) {
    for (int ci = 0; ci < g_nclass; ci++) {
        ClassInfo* c = &g_class[ci];
        for (int a = 0; a < c->ntraits; a++) {
            int ta = trait_find(c->traits[a]);
            if (ta < 0) continue;
            for (int b = a + 1; b < c->ntraits; b++) {
                int tb = trait_find(c->traits[b]);
                if (tb < 0) continue;
                for (int mi = 0; mi < g_trait[ta].nmethods; mi++) {
                    const char* mn = g_trait[ta].methods[mi];
                    if (trait_method_idx(tb, mn) >= 0 && class_method_idx(ci, mn) < 0) {
                        fprintf(stderr,
                          "[Teko OOP] error: class '%s' inherits method '%s' from both traits '%s' and '%s' "
                          "without overriding it — ambiguous trait composition.\n",
                          c->name, mn, c->traits[a], c->traits[b]);
                        g_oop_error = 1;
                    }
                }
            }
        }
    }
}

// ---- Phase 15 (15.D): event subsystem ------------------------------------------------------
// `event E;` declares an event; `subscribe E with H [fanout|fire_and_forget];` registers a handler
// (a top-level `fn`) + a delivery mode AT SUBSCRIPTION TIME; `raise E(args);` fan-outs to every
// subscriber. Subscriptions are STATIC (compile-time), so the subscriber set per event is known at
// compile time — `raise` lowers to a spawn of each handler over the Phase-14 cooperative scheduler
// (drained at program exit). `fanout` = parallel green threads (real parallelism on Layer B / wasm
// -threads); `fire_and_forget` = enqueue, no result tracked. Both spawn in the cooperative MVP.
#define TEKO_MAX_EVENTS      64
#define TEKO_EVENT_MAX_SUBS  16
typedef struct {
    char name[96];
    int  handler_slot[TEKO_EVENT_MAX_SUBS]; // routine table slot of each subscribed handler
    int  mode[TEKO_EVENT_MAX_SUBS];         // 0 = fanout, 1 = fire_and_forget
    int  nsubs;
} EventInfo;
static EventInfo g_event[TEKO_MAX_EVENTS];
static int g_nevent;
static int event_find(const char* n) {
    for (int i = 0; i < g_nevent; i++) if (strcmp(g_event[i].name, n) == 0) return i;
    return -1;
}
static int event_find_or_add(const char* n) {
    int e = event_find(n);
    if (e >= 0) return e;
    if (g_nevent < TEKO_MAX_EVENTS) {
        memset(&g_event[g_nevent], 0, sizeof(g_event[g_nevent]));
        strncpy(g_event[g_nevent].name, n, 95); g_event[g_nevent].name[95] = '\0';
        return g_nevent++;
    }
    return -1;
}
static void event_add_sub(int ei, int slot, int mode) {
    if (ei < 0 || ei >= g_nevent || slot < 0) return;
    EventInfo* e = &g_event[ei];
    if (e->nsubs < TEKO_EVENT_MAX_SUBS) { e->handler_slot[e->nsubs] = slot; e->mode[e->nsubs] = mode; e->nsubs++; }
}

// Phase 15 (15.D): pre-pass that builds the event registry — `event E;` declarations and
// `subscribe E with H [fanout|fire_and_forget];` registrations (H resolves to a top-level fn slot).
// Parsing is permissive: after `subscribe`, the first identifier is the event, an identifier that
// resolves to a known fn is the handler, and FANOUT/FIRE_AND_FORGET sets the mode (any connector
// word like `with` is simply skipped).
static void collect_events(const char* source, ImportBinding* fns, int nfns) {
    g_nevent = 0;
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_EVENT && p.peek_token.type == TOKEN_IDENTIFIER) {
            fe_advance(&p); // 'event'
            event_find_or_add(p.current_token.lexeme);
            fe_advance(&p); // event name
            continue;
        }
        if (p.current_token.type == TOKEN_SUBSCRIBE) {
            fe_advance(&p); // 'subscribe'
            char ename[96] = ""; int slot = -1, mode = 0;
            if (p.current_token.type == TOKEN_IDENTIFIER) {
                strncpy(ename, p.current_token.lexeme, 95); ename[95] = '\0'; fe_advance(&p);
            }
            while (p.current_token.type != TOKEN_SEMICOLON && p.current_token.type != TOKEN_EOF) {
                if (p.current_token.type == TOKEN_IDENTIFIER) {
                    int s = bind_lookup(fns, nfns, p.current_token.lexeme);
                    if (s >= 0) slot = s; // an identifier resolving to a fn is the handler
                } else if (p.current_token.type == TOKEN_FANOUT) mode = 0;
                else if (p.current_token.type == TOKEN_FIRE_AND_FORGET) mode = 1;
                fe_advance(&p);
            }
            if (p.current_token.type == TOKEN_SEMICOLON) fe_advance(&p);
            if (ename[0] && slot >= 0) event_add_sub(event_find_or_add(ename), slot, mode);
            continue;
        }
        fe_advance(&p);
    }
}

// Phase 15 (15.C): discover generic class templates + their concrete instantiations.
// Pass 1: every `class NAME <` is a generic template (record NAME). Pass 2: every use
// `Tmpl < Arg >` (where Tmpl is a template and the `<` is NOT the declaration's own clause —
// guarded by prev != class) records a (Tmpl, Arg) instantiation to monomorphize.
static void collect_generics(const char* source) {
    g_ngeneric = 0; g_ngeninst = 0;
    { Lexer lx; lexer_init(&lx, source); Parser p; parser_init(&p, &lx);
      while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_CLASS && p.peek_token.type == TOKEN_IDENTIFIER) {
            char nm[96]; strncpy(nm, p.peek_token.lexeme, 95); nm[95] = '\0';
            fe_advance(&p); fe_advance(&p); // 'class', NAME
            if (p.current_token.type == TOKEN_LT && g_ngeneric < TEKO_MAX_GENERICS) {
                strncpy(g_generic[g_ngeneric], nm, 95); g_generic[g_ngeneric][95] = '\0'; g_ngeneric++;
            }
        } else fe_advance(&p);
      }
    }
    { Lexer lx; lexer_init(&lx, source); Parser p; parser_init(&p, &lx);
      TokenType prev = TOKEN_EOF;
      while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_IDENTIFIER && p.peek_token.type == TOKEN_LT &&
            prev != TOKEN_CLASS && is_generic_template(p.current_token.lexeme)) {
            char tmpl[96]; strncpy(tmpl, p.current_token.lexeme, 95); tmpl[95] = '\0';
            fe_advance(&p); // NAME -> '<'
            fe_advance(&p); // '<' -> Arg
            if (p.current_token.type == TOKEN_IDENTIFIER) geninst_add(tmpl, p.current_token.lexeme);
            prev = TOKEN_IDENTIFIER; // the Arg (current); continue from here
            continue;
        }
        prev = p.current_token.type;
        fe_advance(&p);
      }
    }
}

// Phase 15 (15.A): pre-pass that builds the class registry (g_class) — each class's ordered field
// list (compile-time layout) and method table (each method -> a routine slot, continuing the
// global counter after the top-level `fn`s at base_slot). Method bodies are emitted later by
// emit_method_routines in the SAME class/declaration order, so slots stay dense and consistent
// with the backend's routine function table. Phase 15.B: also captures the `: Trait1, Trait2`
// implements clause + the `abstract` modifier, and records bodyless (abstract) methods with no slot.
// Phase 15.C: a generic `class Box<T>` template is monomorphized into one concrete `Box$Arg` per
// discovered type-arg (collect_generics) instead of being registered directly.
static void collect_classes(const char* source, int base_slot) {
    g_nclass = 0;
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    int slot = base_slot;
    while (p.current_token.type != TOKEN_EOF) {
        int is_abstract = 0;
        if (p.current_token.type == TOKEN_ABSTRACT && p.peek_token.type == TOKEN_CLASS) {
            is_abstract = 1; fe_advance(&p); // consume 'abstract'; now at 'class'
        }
        if (p.current_token.type != TOKEN_CLASS || p.peek_token.type != TOKEN_IDENTIFIER) {
            fe_advance(&p); continue;
        }
        if (g_nclass >= TEKO_MAX_CLASSES) { skip_class_decl(&p); continue; }
        ClassInfo* c = &g_class[g_nclass];
        memset(c, 0, sizeof(*c));
        c->is_abstract = is_abstract;
        fe_advance(&p); // 'class'
        strncpy(c->name, p.current_token.lexeme, 95); c->name[95] = '\0';
        fe_advance(&p); // class name
        // Phase 15.C: optional `<T>` generic clause — a TEMPLATE (monomorphized per type-arg below).
        char typeparam[96]; typeparam[0] = '\0'; int generic = 0;
        if (p.current_token.type == TOKEN_LT) {
            generic = 1; fe_advance(&p); // '<'
            if (p.current_token.type == TOKEN_IDENTIFIER) {
                strncpy(typeparam, p.current_token.lexeme, 95); typeparam[95] = '\0';
            }
            while (p.current_token.type != TOKEN_GT && p.current_token.type != TOKEN_LBRACE &&
                   p.current_token.type != TOKEN_EOF) fe_advance(&p);
            if (p.current_token.type == TOKEN_GT) fe_advance(&p); // '>'
        }
        // Phase 15.B: optional `: Trait1, Trait2` implements/extends clause (capture the names).
        if (p.current_token.type == TOKEN_COLON) {
            fe_advance(&p);
            while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) {
                if (p.current_token.type == TOKEN_IDENTIFIER && c->ntraits < TEKO_CLASS_MAX_TRAITS) {
                    strncpy(c->traits[c->ntraits], p.current_token.lexeme, 95);
                    c->traits[c->ntraits][95] = '\0'; c->ntraits++;
                }
                fe_advance(&p);
            }
        } else {
            while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
        }
        if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p); // '{'
        int depth = 1;
        while (p.current_token.type != TOKEN_EOF && depth > 0) {
            if (p.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&p); continue; }
            if (p.current_token.type == TOKEN_RBRACE) { depth--; fe_advance(&p); continue; }
            if (depth == 1 && (p.current_token.type == TOKEN_LET || p.current_token.type == TOKEN_MUT) &&
                p.peek_token.type == TOKEN_IDENTIFIER) {
                fe_advance(&p); // let/mut
                if (c->nfields < TEKO_CLASS_MAX_FIELDS) {
                    strncpy(c->fields[c->nfields], p.current_token.lexeme, 95);
                    c->fields[c->nfields][95] = '\0'; c->nfields++;
                }
                fe_advance(&p); // field name
                while (p.current_token.type != TOKEN_SEMICOLON && p.current_token.type != TOKEN_RBRACE &&
                       p.current_token.type != TOKEN_EOF) fe_advance(&p); // skip ': type'
                if (p.current_token.type == TOKEN_SEMICOLON) fe_advance(&p);
                continue;
            }
            // Methods mirror functions: an optional leading `async` (the method returns intent<>),
            // an optional generic clause `<T>` after the name, and a `: ReturnType` before the body.
            if (depth == 1 && p.current_token.type == TOKEN_ASYNC && p.peek_token.type == TOKEN_FN) {
                fe_advance(&p); // consume 'async' (async method — intent<> return; MVP body is synchronous)
            }
            if (depth == 1 && p.current_token.type == TOKEN_FN && p.peek_token.type == TOKEN_IDENTIFIER) {
                fe_advance(&p); // 'fn'
                char mname[96]; strncpy(mname, p.current_token.lexeme, 95); mname[95] = '\0';
                fe_advance(&p); // method name
                skip_generic_clause(&p); // optional `<T>` generic params (uniform i32 model; 15.C)
                int nparams = 0;
                if (p.current_token.type == TOKEN_LPAREN) {
                    fe_advance(&p);
                    int expect = 1; // a param name follows '(' or ','
                    while (p.current_token.type != TOKEN_RPAREN && p.current_token.type != TOKEN_EOF) {
                        if (expect && p.current_token.type == TOKEN_IDENTIFIER) { nparams++; expect = 0; }
                        else if (p.current_token.type == TOKEN_COMMA) expect = 1;
                        fe_advance(&p);
                    }
                    if (p.current_token.type == TOKEN_RPAREN) fe_advance(&p);
                }
                while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_SEMICOLON &&
                       p.current_token.type != TOKEN_RBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p); // skip `: ReturnType`
                // Phase 15.B: a bodied method gets a routine slot; a bodyless one (`fn m(self): T;`)
                // is an abstract CONTRACT (slot -1, no routine) — a concrete subclass supplies it.
                int bodied = (p.current_token.type == TOKEN_LBRACE);
                int mi = c->nmethods;
                if (mi < TEKO_CLASS_MAX_METHODS) {
                    strncpy(c->methods[mi], mname, 95); c->methods[mi][95] = '\0';
                    // -2 = bodied, slot assigned AFTER the header is known (a generic template's
                    // bodied methods get slots only on its monomorphized instances). -1 = abstract.
                    c->method_slot[mi] = bodied ? -2 : -1;
                    c->method_nparams[mi] = nparams;
                    c->nmethods++;
                }
                methodid_of(mname); // reserve/share the global method_id (vtable column)
                if (bodied) {
                    int d2 = 1; fe_advance(&p);
                    while (p.current_token.type != TOKEN_EOF && d2 > 0) {
                        if (p.current_token.type == TOKEN_LBRACE) d2++;
                        else if (p.current_token.type == TOKEN_RBRACE) d2--;
                        fe_advance(&p);
                    }
                } else if (p.current_token.type == TOKEN_SEMICOLON) {
                    fe_advance(&p); // bodyless contract method
                }
                continue;
            }
            fe_advance(&p);
        }
        // Phase 15.C: finalize this class. A non-generic class gets its bodied-method slots assigned
        // inline now (source order). A GENERIC template is NOT registered directly — instead one
        // concrete instance "Name$Arg" is cloned per discovered (template, type-arg); their slots are
        // assigned after the whole walk (so they sit after every non-generic class's slots).
        if (!generic) {
            for (int mi = 0; mi < c->nmethods; mi++)
                if (c->method_slot[mi] == -2) c->method_slot[mi] = slot++;
            g_nclass++;
        } else {
            ClassInfo tmpl = *c; // the parsed template (fields + methods, slots still -2)
            for (int gi = 0; gi < g_ngeninst && g_nclass < TEKO_MAX_CLASSES; gi++) {
                if (strcmp(g_geninst[gi].tmpl, tmpl.name) != 0) continue;
                ClassInfo* inst = &g_class[g_nclass];
                *inst = tmpl;
                snprintf(inst->name, sizeof(inst->name), "%s$%s", tmpl.name, g_geninst[gi].arg);
                strncpy(inst->typeparam, typeparam, 95);          inst->typeparam[95] = '\0';
                strncpy(inst->mono_arg,  g_geninst[gi].arg, 95);  inst->mono_arg[95]  = '\0';
                strncpy(inst->tmpl_name, tmpl.name, 95);          inst->tmpl_name[95] = '\0';
                g_nclass++; // slots stay -2 — assigned in the finalize pass below
            }
        }
    }
    // Phase 15.C: assign routine slots to monomorphized instance methods — AFTER every non-generic
    // class (so the global slot order is: fns, non-generic class methods, then mono-instance methods,
    // matching the emission order emit_method_routines + emit_mono_routines).
    for (int ci = 0; ci < g_nclass; ci++) {
        if (g_class[ci].mono_arg[0] == '\0') continue;
        for (int mi = 0; mi < g_class[ci].nmethods; mi++)
            if (g_class[ci].method_slot[mi] == -2) g_class[ci].method_slot[mi] = slot++;
    }
}

// Pre-pass: assign each top-level `fn NAME` a table slot (declaration order), so a
// main-level @dom.on(…, NAME) can resolve the handler reference before its body. Class bodies
// are skipped (their methods get slots in collect_classes, continuing the same counter).
static void collect_functions(const char* source, ImportBinding** fns, int* nfns, int* capfns) {
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    int slot = 0;
    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_EXTERN) {
            skip_extern_decl(&p); // do not treat `extern fn` as a handler
        } else if (p.current_token.type == TOKEN_CLASS || p.current_token.type == TOKEN_ABSTRACT ||
                   p.current_token.type == TOKEN_TRAIT) {
            skip_class_decl(&p);  // class/abstract/trait bodies are not top-level routines (keyword-generic skip)
        } else if (p.current_token.type == TOKEN_FN && p.peek_token.type == TOKEN_IDENTIFIER) {
            bind_add(fns, nfns, capfns, p.peek_token.lexeme, slot++);
            fe_advance(&p); // consume 'fn'
            fe_advance(&p); // consume name
        } else {
            fe_advance(&p);
        }
    }
}

// Phase 14 (14.A): lower a plain `NAME(arg, …)` extern call inside a routine/handler
// body. Current token is the callee IDENTIFIER (peek == '('); the callee must resolve to
// a registered import (e.g. `emit`). Args are string/int literals in this subset (enough
// to drive `emit("…")` from a background routine); other arg forms are skipped. Returns 1
// if it consumed a call, 0 if the current token was not a resolvable extern call.
static int lower_routine_extern_call(BytecodeBuffer* buffer, Parser* p,
                                     ImportBinding* binds, int nb) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN)
        return 0;
    int idx = bind_lookup(binds, nb, p->current_token.lexeme);
    if (idx < 0) return 0;
    fe_advance(p); // consume NAME
    fe_advance(p); // consume '('
    CallArg args[16];
    int nargs = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if (nargs < 16 && (p->current_token.type == TOKEN_LIT_STR ||
                           p->current_token.type == TOKEN_STRING_LIT)) {
            args[nargs].is_string = 1; args[nargs].sval = strip_quotes(p->current_token.lexeme);
            args[nargs].ival = 0; args[nargs].is_local = 0; args[nargs].slot = 0;
            nargs++; fe_advance(p);
        } else if (nargs < 16 && p->current_token.type == TOKEN_LIT_INT) {
            args[nargs].is_string = 0; args[nargs].sval = NULL;
            args[nargs].ival = atoi(p->current_token.lexeme);
            args[nargs].is_local = 0; args[nargs].slot = 0;
            nargs++; fe_advance(p);
        } else if (p->current_token.type == TOKEN_COMMA) {
            fe_advance(p);
        } else {
            fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    lower_call(buffer, idx, args, nargs);
    for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
    return 1;
}

// Routine pass: emit each `fn NAME(param) { body }` as a table routine. The handler is
// invoked via teko_invoke(slot, event_arg): on entry $w0 = the arg, which we stash to
// $w1 so `param` references (LOAD) survive across the body's @dom calls. Phase 14: a body
// may also contain plain extern calls (e.g. `emit("…")`) so a `routines`-fired background
// task can do real work; `binds` resolves those callees.
static void emit_handler_routines(const char* source, BytecodeBuffer* buffer,
                                  ImportBinding* fns, int nfns,
                                  ImportBinding* binds, int nb, TempAlloc* ta) {
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);

    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_EXTERN) { skip_extern_decl(&p); continue; }
        if (p.current_token.type == TOKEN_CLASS || p.current_token.type == TOKEN_ABSTRACT ||
            p.current_token.type == TOKEN_TRAIT) { skip_class_decl(&p); continue; } // methods: emit_method_routines
        if (p.current_token.type != TOKEN_FN) { fe_advance(&p); continue; }
        fe_advance(&p); // consume 'fn'
        if (p.current_token.type != TOKEN_IDENTIFIER) continue;

        char fn_name[96];
        strncpy(fn_name, p.current_token.lexeme, sizeof(fn_name) - 1);
        fn_name[sizeof(fn_name) - 1] = '\0';
        int slot = bind_lookup(fns, nfns, fn_name);
        fe_advance(&p); // consume name

        // Parameter list: capture ALL param names (Phase 14.I), skipping `: type` annotations.
        char params[8][96]; int nparams = 0;
        if (p.current_token.type == TOKEN_LPAREN) {
            fe_advance(&p);
            int expect_name = 1; // a param name follows '(' or ','
            while (p.current_token.type != TOKEN_RPAREN && p.current_token.type != TOKEN_EOF) {
                if (expect_name && p.current_token.type == TOKEN_IDENTIFIER && nparams < 8) {
                    strncpy(params[nparams], p.current_token.lexeme, 95);
                    params[nparams][95] = '\0';
                    nparams++; expect_name = 0;
                } else if (p.current_token.type == TOKEN_COMMA) {
                    expect_name = 1;
                }
                fe_advance(&p); // skip type tokens / colons / the captured name
            }
            if (p.current_token.type == TOKEN_RPAREN) fe_advance(&p);
        }
        // Body open brace.
        while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
        if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p);

        codegen_li_emit_func_begin(buffer, slot >= 0 ? slot : 0);
        // On entry $w0 = the spawn-args pointer (native) / arg0 reloaded from the frame (WASM).
        if (nparams > 0) codegen_li_emit_store(buffer); // $w1 = arg0 (WASM @dom event-param access)

        LowerCtx ctx;
        ctx.param_name = nparams > 0 ? params[0] : NULL;
        ctx.fns = fns;
        ctx.nfns = nfns;
        ctx.locals = NULL; // refreshed by the routine's own local table below
        ctx.nlocals = 0;
        ctx.ta = ta;       // nested-arg spill temps + expression temps
        ta->next_temp = 0; // a routine's $v file starts fresh

        // A routine body is lowered with the SAME shared statement dispatcher as $main and the
        // control-flow blocks, so loops/branches/let/reassignment/channels/waiters compose inside
        // a background task (14.H worker loops). The routine gets its OWN named-local table ($v
        // slots overlap $main's — separate function scopes; local_count = the max via ta->hw).
        ImportBinding* rlocals = NULL; int rnlocals = 0, rcaplocals = 0;
        LowerEnv renv;
        renv.locals = &rlocals; renv.nlocals = &rnlocals; renv.caplocals = &rcaplocals;
        renv.binds = &binds; renv.nb = &nb; renv.ctx = &ctx;

        // Phase 14 (14.I): bind each param to a named local read from the spawn arguments
        // (OP_LOAD_SPAWN_ARG i), so the body uses them as values — e.g. channel handles shared with
        // the producer/consumer. (@dom event handlers also keep the $w1/param_name path above,
        // which takes precedence in lower_intrinsic_call, so they are unaffected.)
        for (int pi = 0; pi < nparams; pi++) {
            int ps = env_alloc_local(&renv, params[pi]);
            codegen_li_emit_load_spawn_arg(buffer, pi); // $w0 = args[pi]
            codegen_li_emit_store_local(buffer, ps);    // slot = args[pi]
        }

        while (p.current_token.type != TOKEN_RBRACE && p.current_token.type != TOKEN_EOF) {
            lower_one_stmt(buffer, &p, &renv);
        }
        if (p.current_token.type == TOKEN_RBRACE) fe_advance(&p);

        for (int i = 0; i < rnlocals; i++) free(rlocals[i].name);
        free(rlocals);
        codegen_li_emit_func_end(buffer);
    }
}

// Phase 15 (15.A): emit each class method body as a table routine (slots assigned in
// collect_classes, continuing after the top-level fns). A method takes its instance as the
// leading `self` param (bound from spawn arg 0) plus any explicit params; `self`'s class is
// recorded so `self.field` / `self.method(...)` resolve at compile time. Body lowering reuses
// the SAME statement dispatcher as $main and `fn` handlers, so methods compose loops/branches/
// field access/method calls. Walks classes/methods in the SAME order collect_classes assigned
// slots, so the routine table stays dense and correct.
// Emit every bodied method of class `ci` as a table routine. `p` MUST be positioned just after the
// class's opening `{`; consumes through the matching `}`. Shared by the non-generic pass
// (emit_method_routines) and the monomorphization pass (emit_mono_routines, with g_subst active).
static void emit_class_body(Parser* p, BytecodeBuffer* buffer, int ci,
                            ImportBinding* fns, int nfns, ImportBinding* binds, int nb, TempAlloc* ta) {
    int depth = 1;
    while (p->current_token.type != TOKEN_EOF && depth > 0) {
        if (p->current_token.type == TOKEN_LBRACE) { depth++; fe_advance(p); continue; }
        if (p->current_token.type == TOKEN_RBRACE) { depth--; fe_advance(p); continue; }
        if (!(depth == 1 && p->current_token.type == TOKEN_FN &&
              p->peek_token.type == TOKEN_IDENTIFIER)) { fe_advance(p); continue; }

        fe_advance(p); // 'fn'  (an `async fn` reaches here with its `async` already skipped)
        char mname[96]; strncpy(mname, p->current_token.lexeme, 95); mname[95] = '\0';
        int midx = class_method_idx(ci, mname);
        int slot = (ci >= 0 && midx >= 0) ? g_class[ci].method_slot[midx] : 0;
        fe_advance(p); // method name
        skip_generic_clause(p); // optional `<T>` generic params

        char params[8][96]; int nparams = 0;
        if (p->current_token.type == TOKEN_LPAREN) {
            fe_advance(p);
            int expect = 1;
            while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
                if (expect && p->current_token.type == TOKEN_IDENTIFIER && nparams < 8) {
                    strncpy(params[nparams], p->current_token.lexeme, 95); params[nparams][95] = '\0';
                    nparams++; expect = 0;
                } else if (p->current_token.type == TOKEN_COMMA) expect = 1;
                fe_advance(p);
            }
            if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
        }
        while (p->current_token.type != TOKEN_LBRACE && p->current_token.type != TOKEN_SEMICOLON &&
               p->current_token.type != TOKEN_EOF) fe_advance(p); // skip `: ReturnType`
        // Phase 15.B: a bodyless (abstract) contract method has no routine — skip it (slot == -1).
        if (p->current_token.type == TOKEN_SEMICOLON || slot < 0) {
            if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
            continue;
        }
        if (p->current_token.type == TOKEN_LBRACE) fe_advance(p);

        codegen_li_emit_func_begin(buffer, slot);
        if (nparams > 0) codegen_li_emit_store(buffer); // $w1 = arg0 (parity with the handler ABI)

        LowerCtx ctx;
        ctx.param_name = nparams > 0 ? params[0] : NULL;
        ctx.fns = fns; ctx.nfns = nfns;
        ctx.locals = NULL; ctx.nlocals = 0; ctx.ta = ta;
        ta->next_temp = 0;
        ImportBinding* rlocals = NULL; int rnlocals = 0, rcaplocals = 0;
        LowerEnv renv;
        renv.locals = &rlocals; renv.nlocals = &rnlocals; renv.caplocals = &rcaplocals;
        renv.binds = &binds; renv.nb = &nb; renv.ctx = &ctx;

        localcls_reset(); localstr_reset(); localflt_reset(); localdec_reset(); localopt_reset(); localarr_reset(); localiarr_reset(); localsoa_reset(); localaos_reset(); // method scope: self (+ any objects it instantiates) are class-typed
        traitlocal_reset(); // method scope: trait-typed locals don't leak across method bodies
        for (int pi = 0; pi < nparams; pi++) {
            int ps = env_alloc_local(&renv, params[pi]);
            codegen_li_emit_load_spawn_arg(buffer, pi); // $w0 = args[pi]
            codegen_li_emit_store_local(buffer, ps);
        }
        // The leading param IS the instance: bind it to this method's class so member access inside
        // the body resolves (`self.x` -> OP_OBJ_GET, `self.m()` -> OP_CALL_FUNC).
        if (nparams > 0 && ci >= 0) localcls_set(params[0], ci);

        while (p->current_token.type != TOKEN_RBRACE && p->current_token.type != TOKEN_EOF) {
            lower_one_stmt(buffer, p, &renv);
        }
        if (p->current_token.type == TOKEN_RBRACE) fe_advance(p);

        for (int i = 0; i < rnlocals; i++) free(rlocals[i].name);
        free(rlocals);
        codegen_li_emit_func_end(buffer);
    }
}

static void emit_method_routines(const char* source, BytecodeBuffer* buffer,
                                 ImportBinding* fns, int nfns,
                                 ImportBinding* binds, int nb, TempAlloc* ta) {
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    while (p.current_token.type != TOKEN_EOF) {
        // Phase 15.B: traits carry only bodyless contracts — never emit bodies for them.
        if (p.current_token.type == TOKEN_TRAIT) { skip_class_decl(&p); continue; }
        // `abstract class` reaches the class branch with `abstract` consumed by the skip below.
        if (p.current_token.type != TOKEN_CLASS || p.peek_token.type != TOKEN_IDENTIFIER) {
            fe_advance(&p); continue;
        }
        fe_advance(&p); // 'class'
        int ci = class_find(p.current_token.lexeme);
        fe_advance(&p); // class name
        // Phase 15.C: a GENERIC template (`class NAME<T>`) is NOT emitted here — its monomorphized
        // instances are emitted by emit_mono_routines. class_find returned -1 for it (never registered).
        if (p.current_token.type == TOKEN_LT) {
            while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
            if (p.current_token.type == TOKEN_LBRACE) {
                int d = 1; fe_advance(&p);
                while (p.current_token.type != TOKEN_EOF && d > 0) {
                    if (p.current_token.type == TOKEN_LBRACE) d++;
                    else if (p.current_token.type == TOKEN_RBRACE) d--;
                    fe_advance(&p);
                }
            }
            continue;
        }
        while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
        if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p);
        emit_class_body(&p, buffer, ci, fns, nfns, binds, nb, ta);
    }
    localcls_reset(); localstr_reset(); localflt_reset(); localdec_reset(); localopt_reset(); localarr_reset(); localiarr_reset(); localsoa_reset(); localaos_reset();
}

// Phase 15.C: emit each MONOMORPHIZED instance's methods. For each `Tmpl$Arg` concrete instance, set
// the type-param substitution (T -> Arg), re-lex source to the generic template `class Tmpl<T>`, and
// emit its body under the instance's slots — so `T()` instantiates Arg and a `T`-typed local is
// Arg-typed (real per-type specialization, zero runtime cost). Runs AFTER emit_method_routines, so
// the mono instances' (higher) slots are emitted in slot order → the routine table stays dense.
static void emit_mono_routines(const char* source, BytecodeBuffer* buffer,
                               ImportBinding* fns, int nfns,
                               ImportBinding* binds, int nb, TempAlloc* ta) {
    for (int ci = 0; ci < g_nclass; ci++) {
        if (g_class[ci].mono_arg[0] == '\0') continue; // only monomorphized instances
        strncpy(g_subst_param, g_class[ci].typeparam, 95); g_subst_param[95] = '\0';
        strncpy(g_subst_arg,   g_class[ci].mono_arg, 95);  g_subst_arg[95]   = '\0';
        Lexer lx; lexer_init(&lx, source);
        Parser p; parser_init(&p, &lx);
        int found = 0;
        while (p.current_token.type != TOKEN_EOF) {
            if (p.current_token.type == TOKEN_CLASS && p.peek_token.type == TOKEN_IDENTIFIER &&
                strcmp(p.peek_token.lexeme, g_class[ci].tmpl_name) == 0) { found = 1; break; }
            fe_advance(&p);
        }
        if (found) {
            fe_advance(&p); // 'class'
            fe_advance(&p); // template name
            while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
            if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p);
            emit_class_body(&p, buffer, ci, fns, nfns, binds, nb, ta);
        }
        g_subst_param[0] = '\0'; g_subst_arg[0] = '\0';
    }
    localcls_reset(); localstr_reset(); localflt_reset(); localdec_reset(); localopt_reset(); localarr_reset(); localiarr_reset(); localsoa_reset(); localaos_reset();
}

int teko_compile_interop(const char* source, BytecodeBuffer* buffer) {
    if (!source || !buffer) return 1;

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);

    ImportBinding* binds = NULL;
    int nb = 0, capb = 0;

    // Pre-pass: map every handler `fn NAME` to a table slot so main-level
    // @dom.on(…, NAME) can reference it even if declared later.
    ImportBinding* fns = NULL;
    int nfns = 0, capfns = 0;
    collect_functions(source, &fns, &nfns, &capfns);

    // Phase 15 (15.B): pre-pass the trait contracts (g_trait) + the global method-id space, then
    // (15.A) the class layouts + method table. Method routine slots continue the global counter
    // after the top-level fns (nfns), so the routine function table stays dense across handlers +
    // methods. localcls is the per-scope instance->class map; reset for $main.
    g_oop_error = 0;
    g_nmethodname = 0;
    g_subst_param[0] = '\0'; g_subst_arg[0] = '\0'; // no active monomorphization substitution
    collect_traits(source);
    collect_generics(source); // Phase 15.C: discover generic templates + their concrete instantiations
    collect_events(source, fns, nfns); // Phase 15.D: events + their static subscriptions
    collect_classes(source, nfns);
    check_oop_collisions(); // ambiguous trait composition -> g_oop_error (compile failure below)
    localcls_reset(); localstr_reset(); localflt_reset(); localdec_reset(); localopt_reset(); localarr_reset(); localiarr_reset(); localsoa_reset(); localaos_reset();
    if (g_oop_error) return 1; // do not emit a module with an unresolved OOP compile error

    // Phase 12: named local variables ($v0..) declared with `let`/`mut` at top level.
    ImportBinding* locals = NULL;
    int nlocals = 0, caplocals = 0;
    TempAlloc ta; ta.next_temp = 0; ta.hw = 0; // expression-temp allocator (P12-E)
    LowerCtx top_ctx;
    top_ctx.param_name = NULL;
    top_ctx.fns = fns; top_ctx.nfns = nfns;
    top_ctx.locals = NULL; top_ctx.nlocals = 0; // refreshed before each use below
    top_ctx.ta = &ta;

    // Control-flow foundation: a mutable lowering environment shared with the block-body
    // statement dispatcher (lower_block / lower_one_stmt) for `while`/`loop`/`if` bodies.
    LowerEnv top_env;
    top_env.locals = &locals; top_env.nlocals = &nlocals; top_env.caplocals = &caplocals;
    top_env.binds = &binds; top_env.nb = &nb; top_env.ctx = &top_ctx;

    // Phase 15.B: populate the STATIC vtable at $main start — for every concrete class implementing
    // a trait, vtable_set(type_id, method_id, slot) for each of its bodied methods (its abstract
    // contract methods have slot -1 and are skipped; a subclass override re-points the same column).
    // The mapping is fixed at compile time; teko_vtable_set self-resets on its first call. Programs
    // with no trait-implementing class emit nothing here → byte-identical to 15.A.
    traitlocal_reset();
    defer_reset();    // Phase 18 (18.C): no defers carried over from a prior compile
    comptime_reset(); // Phase 18 (18.D): no comptime constants carried over
    {
        int any_dispatch = 0;
        for (int ci = 0; ci < g_nclass; ci++) if (g_class[ci].ntraits > 0) { any_dispatch = 1; break; }
        if (any_dispatch) {
            for (int ci = 0; ci < g_nclass; ci++) {
                if (g_class[ci].ntraits == 0) continue;
                for (int mi = 0; mi < g_class[ci].nmethods; mi++) {
                    if (g_class[ci].method_slot[mi] < 0) continue; // abstract contract — no routine
                    codegen_li_emit_iconst(buffer, class_type_id(ci));                 codegen_li_emit_setarg(buffer, 0);
                    codegen_li_emit_iconst(buffer, methodid_of(g_class[ci].methods[mi])); codegen_li_emit_setarg(buffer, 1);
                    codegen_li_emit_iconst(buffer, g_class[ci].method_slot[mi]);
                    codegen_li_emit_vtable(buffer, OP_VTABLE_SET);
                }
            }
        }
    }

    while (parser.current_token.type != TOKEN_EOF) {
        // Keep the top-level lowering context's local view current, and start temp
        // slots above the named locals so spills never clobber a `let` binding.
        top_ctx.locals = locals; top_ctx.nlocals = nlocals;
        ta.next_temp = nlocals;
        if (nlocals > ta.hw) ta.hw = nlocals;

        if ((parser.current_token.type == TOKEN_LET || parser.current_token.type == TOKEN_MUT) &&
            parser.peek_token.type == TOKEN_IDENTIFIER) {
            // `let`/`mut NAME [: type] = <initializer>` — a named local binding. Shared with block
            // bodies (lower_let_stmt → lower_init_value), so `let cb = circuit(...)` works here too.
            lower_let_stmt(buffer, &parser, &top_env);
        } else if (parser.current_token.type == TOKEN_DEFER) {
            // Phase 18 (18.C): `defer <stmt>;` — register a scope-closing statement (LIFO at `$main`
            // close). Capture its source now; it is re-lexed + lowered after the loop, before OP_HALT.
            fe_advance(&parser); // consume `defer`
            defer_capture(&parser);
        } else if (parser.current_token.type == TOKEN_COMPTIME) {
            // Phase 18 (18.D): `comptime let NAME = <const-expr>;` — fold the expression AT COMPILE
            // TIME and bind NAME as a comptime constant (no IL arithmetic emitted; a read of NAME is a
            // single iconst). MVP form: a `let`/`mut` binding of a constant integer expression.
            fe_advance(&parser); // consume `comptime`
            if (parser.current_token.type == TOKEN_LET || parser.current_token.type == TOKEN_MUT)
                fe_advance(&parser);
            char cname[96]; cname[0] = '\0';
            if (parser.current_token.type == TOKEN_IDENTIFIER) {
                strncpy(cname, parser.current_token.lexeme, sizeof(cname) - 1); cname[sizeof(cname) - 1] = '\0';
                fe_advance(&parser);
            }
            if (parser.current_token.type == TOKEN_ASSIGN || parser.current_token.type == TOKEN_QUICK_ASSIGN)
                fe_advance(&parser);
            long cval = comptime_eval(&parser, 1);          // compile-time fold
            if (cname[0]) comptime_set(cname, cval);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_EVENT) {
            // Phase 15.D: `event E;` — a compile-time declaration (registered in collect_events);
            // emits no code. Skip to the statement end.
            while (parser.current_token.type != TOKEN_SEMICOLON && parser.current_token.type != TOKEN_EOF)
                fe_advance(&parser);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_SUBSCRIBE) {
            // Phase 15.D: `subscribe E with H [fanout|fire_and_forget];` — registered statically in
            // collect_events; emits no code at the subscription site.
            while (parser.current_token.type != TOKEN_SEMICOLON && parser.current_token.type != TOKEN_EOF)
                fe_advance(&parser);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_RAISE &&
                   parser.peek_token.type == TOKEN_IDENTIFIER) {
            // Phase 15.D: `raise E(args);` — fan-out to every subscriber of E. The subscriber set is
            // compile-time static; each handler is spawned over the Phase-14 cooperative scheduler
            // (drained at program exit, so handlers run AFTER the raise site — deferred fan-out).
            // Args are parsed once (int literals / named locals) and RE-STAGED per subscriber (a
            // spawn consumes the staged args). fanout + fire_and_forget both spawn in the MVP.
            fe_advance(&parser); // 'raise'
            int ei = event_find(parser.current_token.lexeme);
            fe_advance(&parser); // event name
            // Parse the args once.
            int ev_is_local[8]; int ev_val[8]; int ev_argc = 0;
            if (parser.current_token.type == TOKEN_LPAREN) {
                fe_advance(&parser);
                while (parser.current_token.type != TOKEN_RPAREN &&
                       parser.current_token.type != TOKEN_EOF && ev_argc < 8) {
                    if (parser.current_token.type == TOKEN_LIT_INT) {
                        ev_is_local[ev_argc] = 0; ev_val[ev_argc] = (int)literal_canonical_value(&parser.current_token);
                        ev_argc++; fe_advance(&parser);
                    } else if (parser.current_token.type == TOKEN_IDENTIFIER) {
                        int s = bind_lookup(locals, nlocals, parser.current_token.lexeme);
                        ev_is_local[ev_argc] = (s >= 0) ? 1 : 0; ev_val[ev_argc] = (s >= 0) ? s : 0;
                        ev_argc++; fe_advance(&parser);
                    } else if (parser.current_token.type == TOKEN_COMMA) {
                        fe_advance(&parser);
                    } else fe_advance(&parser);
                }
                if (parser.current_token.type == TOKEN_RPAREN) fe_advance(&parser);
            }
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
            if (ei >= 0) {
                for (int si = 0; si < g_event[ei].nsubs; si++) {
                    for (int ai = 0; ai < ev_argc; ai++) {
                        if (ev_is_local[ai]) codegen_li_emit_load_local(buffer, ev_val[ai]);
                        else                 codegen_li_emit_iconst(buffer, ev_val[ai]);
                        codegen_li_emit_setarg(buffer, ai);
                    }
                    codegen_li_emit_iconst(buffer, g_event[ei].handler_slot[si]); // $w0 = handler slot
                    if (ev_argc > 0) codegen_li_emit_spawn_async_args(buffer, ev_argc);
                    else             codegen_li_emit_spawn_async(buffer);
                }
            }
        } else if (parser.current_token.type == TOKEN_ROUTINES) {
            // Phase 14 (14.A): `routines { foo(); bar(); }` — fire each enclosed call as a
            // background task. Each `NAME(…)` resolves to a top-level `fn NAME`'s table slot
            // (collect_functions assigned it); we lower `ICONST slot; OP_SPAWN_ASYNC`, which
            // enqueues the routine on the cooperative scheduler. The runtime drains the queue
            // before the program exits (WASM `$teko_sched_run` at $main close / native
            // `teko_rt_run` at HALT). Args are ignored in this MVP (fire pure tasks).
            fe_advance(&parser); // consume 'routines'
            if (parser.current_token.type == TOKEN_LBRACE) {
                fe_advance(&parser); // consume '{'
                int depth = 1;
                while (parser.current_token.type != TOKEN_EOF && depth > 0) {
                    if (parser.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&parser); }
                    else if (parser.current_token.type == TOKEN_RBRACE) { depth--; fe_advance(&parser); }
                    else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                             parser.peek_token.type == TOKEN_LPAREN) {
                        int slot = bind_lookup(fns, nfns, parser.current_token.lexeme);
                        fe_advance(&parser); // NAME
                        fe_advance(&parser); // '('
                        // Phase 14 (14.I): collect ALL arguments (Go-style) — named-local handles
                        // or int literals — staging each into $a<i>; then OP_SPAWN_ASYNC_ARGS argc
                        // passes the whole vector to the task. No args -> legacy OP_SPAWN_ASYNC
                        // (byte-identical for arg-less routines programs).
                        int argc = 0;
                        while (parser.current_token.type != TOKEN_RPAREN &&
                               parser.current_token.type != TOKEN_EOF && argc < 8) {
                            if (parser.current_token.type == TOKEN_IDENTIFIER) {
                                int s = bind_lookup(locals, nlocals, parser.current_token.lexeme);
                                if (s >= 0) { codegen_li_emit_load_local(buffer, s); }
                                else        { codegen_li_emit_iconst(buffer, 0); }
                                codegen_li_emit_setarg(buffer, argc++);
                                fe_advance(&parser);
                            } else if (parser.current_token.type == TOKEN_LIT_INT) {
                                codegen_li_emit_iconst(buffer, (int)literal_canonical_value(&parser.current_token));
                                codegen_li_emit_setarg(buffer, argc++);
                                fe_advance(&parser);
                            } else if (parser.current_token.type == TOKEN_COMMA) {
                                fe_advance(&parser);
                            } else {
                                fe_advance(&parser);
                            }
                        }
                        if (parser.current_token.type == TOKEN_RPAREN) fe_advance(&parser);
                        if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
                        if (slot >= 0) {
                            codegen_li_emit_iconst(buffer, slot);  // $w0 = routine table slot
                            if (argc > 0) codegen_li_emit_spawn_async_args(buffer, argc);
                            else          codegen_li_emit_spawn_async(buffer);
                        }
                    } else {
                        fe_advance(&parser);
                    }
                }
            }
        } else if (parser.current_token.type == TOKEN_EXTERN) {
            FFIASTNode* node = parse_extern_declaration(&parser);
            if (node) {
                register_extern(buffer, node, &binds, &nb, &capb);
                free_ffi_ast_node(node);
            }
        } else if (parser.current_token.type == TOKEN_CLASS || parser.current_token.type == TOKEN_ABSTRACT ||
                   parser.current_token.type == TOKEN_TRAIT) {
            // Phase 15 (15.A/15.B): class/abstract/trait declarations emit no top-level code; their
            // layout/contracts were pre-collected, and method bodies are emitted after $main
            // (emit_method_routines). Keyword-generic brace skip.
            skip_class_decl(&parser);
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LBRACKET &&
                   localsoa_find(parser.current_token.lexeme) >= 0 &&
                   lower_soa_index_write(buffer, &parser, &top_env)) {
            // Phase 18 (18.E.3): top-level `s[i].field = expr;` SoA write (before the plain index write).
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LBRACKET &&
                   localaos_get(parser.current_token.lexeme) >= 0 &&
                   lower_aos_index_write(buffer, &parser, &top_env)) {
            // Phase 18 (18.E.3): top-level `a[i].field = expr;` AoS write (before the plain index write).
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LBRACKET &&
                   localanyarr_get(parser.current_token.lexeme) &&
                   lower_array_index_write(buffer, &parser, &top_env)) {
            // Phase 18 (18.E.1/18.E.2): top-level `a[i] = expr;` index write on an i64 OR typed-i32 array.
        } else if (lower_member_stmt(buffer, &parser, &top_env)) {
            // Phase 15 (15.A): top-level `obj.field = expr;` / `obj.method(args);`.
        } else if (parser.current_token.type == TOKEN_FN) {
            // Handler declaration: skip its body in the main pass; it is emitted as a
            // table routine after main's HALT (emit_handler_routines).
            while (parser.current_token.type != TOKEN_LBRACE &&
                   parser.current_token.type != TOKEN_EOF) fe_advance(&parser);
            int depth = 0;
            while (parser.current_token.type != TOKEN_EOF) {
                if (parser.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&parser); }
                else if (parser.current_token.type == TOKEN_RBRACE) {
                    depth--; fe_advance(&parser);
                    if (depth == 0) break;
                } else {
                    fe_advance(&parser);
                }
            }
        } else if (parser.current_token.type == TOKEN_MACRO_IDENT &&
                   is_dom_macro(parser.current_token.lexeme) &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level @dom/@js intrinsic call statement.
            lower_intrinsic_call(buffer, &parser, &top_ctx);
        } else if (is_duplex_head(&parser)) {
            // Top-level duplex statement: duplex.send/close/… ( args )  (result discarded).
            lower_duplex_call(buffer, &parser, &top_ctx);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (is_delayed_head(&parser)) {
            // Top-level delayed statement: delayed.send/advance/close/… ( args ).
            lower_delayed_call(buffer, &parser, &top_ctx);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (is_bcast_head(&parser)) {
            // Top-level broadcast statement: broadcast.publish/close/… ( args ).
            lower_bcast_call(buffer, &parser, &top_ctx);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (is_atomic_head(&parser)) {
            // Top-level atomic statement: atomic.add/store/… ( args ).
            lower_atomic_call(buffer, &parser, &top_ctx);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_WAIT ||
                   parser.current_token.type == TOKEN_AWAIT) {
            // Phase 14 (14.G): top-level `wait <ts>;` / `await <ts>;` timespan waiter.
            lower_wait_await(buffer, &parser, &top_ctx,
                             parser.current_token.type == TOKEN_AWAIT);
        } else if (parser.current_token.type == TOKEN_WHILE) {
            lower_while(buffer, &parser, &top_env);   // control-flow foundation
        } else if (parser.current_token.type == TOKEN_FOR) {
            lower_for(buffer, &parser, &top_env);     // Phase 18 (18.E.2): for NAME in ARR { }
        } else if (parser.current_token.type == TOKEN_LOOP) {
            lower_loop(buffer, &parser, &top_env);
        } else if (parser.current_token.type == TOKEN_IF) {
            lower_if(buffer, &parser, &top_env);
        } else if (parser.current_token.type == TOKEN_BREAK) {
            fe_advance(&parser); codegen_li_emit_cf(buffer, OP_BREAK);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_CONTINUE) {
            fe_advance(&parser); codegen_li_emit_cf(buffer, OP_CONTINUE);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_RETRY) {
            lower_retry_block(buffer, &parser, &top_env);     // 14.F retry { } fallback { }
        } else if (parser.current_token.type == TOKEN_CIRCUIT &&
                   parser.peek_token.type == TOKEN_IDENTIFIER) {
            lower_circuit_block(buffer, &parser, &top_env);   // 14.F circuit cb { } fallback { }
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_ASSIGN &&
                   bind_lookup(locals, nlocals, parser.current_token.lexeme) >= 0) {
            // Top-level reassignment of an existing named local: `NAME = expr;`.
            lower_reassign(buffer, &parser, &top_env);
        } else if (parser.current_token.type == TOKEN_SHARED &&
                   parser.peek_token.type == TOKEN_LBRACE) {
            // Phase 14 (14.E): `shared { … }` — coarse-locked critical section.
            lower_shared_block(buffer, &parser, &top_ctx, binds, nb);
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level call statement: NAME ( arg, … )
            char* callee = strdup(parser.current_token.lexeme);
            fe_advance(&parser); // consume NAME
            fe_advance(&parser); // consume '('

            CallArg args[16];
            int nargs = 0;
            int temps_used = 0; // codec-arg results spilled to temp locals
            while (parser.current_token.type != TOKEN_RPAREN &&
                   parser.current_token.type != TOKEN_EOF) {
                if (nargs < 16 && try_lower_call_arg_expr(buffer, &parser, &top_ctx, locals, nlocals,
                                                          &args[nargs], &temps_used)) {
                    // Phase 16.B: an expression argument (e.g. `"x = " + n`) — evaluated + spilled.
                    nargs++;
                } else if (nargs < 16 && is_floatcast_head(&parser) &&
                           (strcmp(parser.current_token.lexeme, "convert.to_int") == 0 ||
                            strcmp(parser.current_token.lexeme, "convert.float_to_str") == 0)) {
                    // Phase 17 (17.B/17.D): a bare `convert.to_int(<expr>)` (checked i32 in $w0) OR
                    // `convert.float_to_str(<expr>)` (char* in $w0) call argument — both leave their
                    // result in $w0, spilled to a temp like the codec case. (to_float yields $f0, not
                    // passable as a value arg in this subset — reach it via an expression arg.)
                    lower_floatcast(buffer, &parser, &top_ctx); // $w0 = result (i32 / char*)
                    int t = ta.next_temp++;
                    if (ta.next_temp > ta.hw) ta.hw = ta.next_temp;
                    codegen_li_emit_store_local(buffer, t);
                    args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
                    args[nargs].is_local = 1; args[nargs].slot = t;
                    nargs++; temps_used++;
                } else if (nargs < 16 && is_decimalsurf_head(&parser) &&
                           strcmp(parser.current_token.lexeme, "decimal.to_string") == 0) {
                    // Phase 17.F.4: a bare `decimal.to_string(<expr>)` call arg leaves a char* in $w0
                    // (passable), spilled to a temp like the codec case. (decimal.parse yields a value
                    // in $d0, not passable as a value arg in this subset — reach it via an expr arg.)
                    lower_decimalsurf(buffer, &parser, &top_ctx); // $w0 = char* (decimal string)
                    int t = ta.next_temp++;
                    if (ta.next_temp > ta.hw) ta.hw = ta.next_temp;
                    codegen_li_emit_store_local(buffer, t);
                    args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
                    args[nargs].is_local = 1; args[nargs].slot = t;
                    nargs++; temps_used++;
                } else if (nargs < 16 && is_simd_head(&parser)) {
                    // Phase 18 (18.E.4): a bare `simd.sum(<expr>)` top-level call arg leaves the
                    // scalar sum (i32) in $w0, spilled to a temp like the codec case.
                    lower_simd(buffer, &parser, &top_ctx); // $w0 = scalar sum
                    int t = ta.next_temp++;
                    if (ta.next_temp > ta.hw) ta.hw = ta.next_temp;
                    codegen_li_emit_store_local(buffer, t);
                    args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
                    args[nargs].is_local = 1; args[nargs].slot = t;
                    nargs++; temps_used++;
                } else if (nargs < 16 && is_codec_head(&parser)) {
                    // base64/hex codec call as an argument (P12-G): lower eagerly and
                    // spill the result pointer to a temp local, then pass the local.
                    lower_base_codec(buffer, &parser, &top_ctx); // $w0 = result ptr
                    int t = ta.next_temp++;
                    if (ta.next_temp > ta.hw) ta.hw = ta.next_temp;
                    codegen_li_emit_store_local(buffer, t);
                    args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
                    args[nargs].is_local = 1; args[nargs].slot = t;
                    nargs++; temps_used++;
                } else if (nargs < 16 &&
                    (parser.current_token.type == TOKEN_LIT_STR ||
                     parser.current_token.type == TOKEN_STRING_LIT)) {
                    args[nargs].is_string = 1;
                    args[nargs].sval = strip_quotes(parser.current_token.lexeme);
                    args[nargs].ival = 0;
                    args[nargs].is_local = 0; args[nargs].slot = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_LIT_INT) {
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = atoi(parser.current_token.lexeme);
                    args[nargs].is_local = 0; args[nargs].slot = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_IDENTIFIER &&
                           bind_lookup(locals, nlocals, parser.current_token.lexeme) >= 0) {
                    // A named local passed as a call argument (Phase 12).
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = 0;
                    args[nargs].is_local = 1;
                    args[nargs].slot = bind_lookup(locals, nlocals, parser.current_token.lexeme);
                    nargs++;
                    fe_advance(&parser);
                } else if (parser.current_token.type == TOKEN_COMMA) {
                    fe_advance(&parser);
                } else {
                    fe_advance(&parser); // skip anything else in the interop subset
                }
            }
            if (parser.current_token.type == TOKEN_RPAREN) fe_advance(&parser);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);

            int idx = bind_lookup(binds, nb, callee);
            if (idx >= 0) lower_call(buffer, idx, args, nargs);
            ta.next_temp -= temps_used; // free codec-arg temps

            for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
            free(callee);
        } else {
            fe_advance(&parser);
        }
    }

    // Phase 18 (18.C): drain the `defer` stack at `$main` close — re-lex + lower each captured
    // statement IN REVERSE (LIFO) through the normal statement dispatcher, just before OP_HALT. The
    // top-level locals are still live here, so a deferred `emit(x)` / `obj.method()` resolves. Defer-
    // free programs push nothing, so their byte stream is unchanged.
    for (int di = g_ndefer - 1; di >= 0; di--) {
        top_ctx.locals = locals; top_ctx.nlocals = nlocals;
        ta.next_temp = nlocals; if (nlocals > ta.hw) ta.hw = nlocals;
        Lexer dlx; lexer_init(&dlx, g_defer[di]);
        Parser dp;  parser_init(&dp, &dlx);
        lower_one_stmt(buffer, &dp, &top_env);
    }

    codegen_li_emit_halt(buffer); // close main

    // Emit handler bodies as table routines (after main), so @dom.on references resolve.
    // (May raise the temp high-water for nested args inside handler bodies.)
    emit_handler_routines(source, buffer, fns, nfns, binds, nb, &ta);

    // Phase 15 (15.A): emit class method bodies as table routines (after the handlers; their
    // slots continue past nfns, matching collect_classes). Methods may use named locals + temps,
    // so this also feeds the shared $v high-water below.
    emit_method_routines(source, buffer, fns, nfns, binds, nb, &ta);

    // Phase 15.C: emit the MONOMORPHIZED instance method bodies (one specialized copy per generic
    // type-arg, with the type-param substituted) — AFTER the non-generic methods so their (higher)
    // slots are emitted in slot order, keeping the routine table dense.
    emit_mono_routines(source, buffer, fns, nfns, binds, nb, &ta);

    // Phase 12: how many $v locals to declare per function — named locals plus the
    // expression/nested-arg temp high-water (across $main and the handler routines).
    buffer->local_count = (ta.hw > nlocals) ? ta.hw : nlocals;

    for (int i = 0; i < nb; i++) free(binds[i].name);
    free(binds);
    for (int i = 0; i < nfns; i++) free(fns[i].name);
    free(fns);
    for (int i = 0; i < nlocals; i++) free(locals[i].name);
    free(locals);
    return 0;
}
