#include "frontend_interop.h"
#include "lexer.h"
#include "parser.h"
#include "parser_ffi.h"
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

static void eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                           int min_prec, TempAlloc* ta);

// Phase 15 (15.A): `obj.field` READ as an expression primary -> OP_OBJ_GET(handle, idx) -> $w0.
// `obj` must be a class-typed local and `member` one of its fields (resolved at compile time).
// Returns 1 if it consumed a member read, 0 otherwise (a method call `obj.method(` or a non-class
// dotted ident — e.g. duplex.* — is left for the caller). Current token is the dotted IDENTIFIER.
static int lower_member_read(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type != TOKEN_IDENTIFIER) return 0;
    char base[96], member[96];
    if (!dotted_split(p->current_token.lexeme, base, member)) return 0;
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

static void eval_primary(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    if (p->current_token.type == TOKEN_LIT_INT) {
        codegen_li_emit_iconst(b, atoi(p->current_token.lexeme));
        fe_advance(p);
    } else if (p->current_token.type == TOKEN_LPAREN) {
        fe_advance(p);
        eval_expr_prec(b, p, ctx, 1, ta);
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    } else if (lower_member_read(b, p, ctx)) {
        // `obj.field` read consumed (e.g. inside `self.x + self.y`).
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
        if (s >= 0) codegen_li_emit_load_local(b, s);
        else codegen_li_emit_iconst(b, 0); // unknown identifier in this subset → 0
        fe_advance(p);
    } else {
        codegen_li_emit_iconst(b, 0); // empty/unsupported primary → 0
    }
}

static void eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                           int min_prec, TempAlloc* ta) {
    eval_primary(b, p, ctx, ta); // left operand → $w0
    while (p12_tok_prec(p->current_token.type) >= min_prec &&
           p12_tok_prec(p->current_token.type) > 0) {
        int prec = p12_tok_prec(p->current_token.type);
        OpCode op = p12_tok_op(p->current_token.type);
        fe_advance(p); // consume the operator
        int t = ta->next_temp++;
        if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        codegen_li_emit_store_local(b, t);        // temp = left
        eval_expr_prec(b, p, ctx, prec + 1, ta);  // right → $w0 (left-associative)
        codegen_li_emit_store(b);                 // $w1 = right
        codegen_li_emit_load_local(b, t);         // $w0 = left
        codegen_li_emit_binop(b, op);             // $w0 = left <op> right
        ta->next_temp--;                          // free temp
    }
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
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
        if (s >= 0) codegen_li_emit_load_local(b, s);
        else codegen_li_emit_iconst(b, 0);
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
    if (p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) {
        char* sv = strip_quotes(p->current_token.lexeme);
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, sv));
        free(sv); fe_advance(p);
    } else if (is_instantiation_head(p)) {
        // Phase 15 (15.A/15.C): `ClassName(...)` / `T(...)` / `Box<Arg>(...)` instantiation ->
        // OP_OBJ_NEW -> handle in $w0 (sets g_last_inst_class for the let binding).
        lower_instantiation(b, p);
    } else if (lower_trait_dispatch(b, p, env)) {
        // Phase 15 (15.B): `g.method(args)` dynamic dispatch as an RHS -> result in $w0.
    } else if (lower_member_call(b, p, ctx)) {
        // Phase 15 (15.A): `obj.method(args)` as an RHS -> OP_CALL_FUNC -> result in $w0.
    } else if (p->current_token.type == TOKEN_MACRO_IDENT && is_dom_macro(p->current_token.lexeme) &&
               p->peek_token.type == TOKEN_LPAREN) {
        lower_intrinsic_call(b, p, ctx);
    } else if (is_codec_head(p))   { lower_base_codec(b, p, ctx); }
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
    else { env_sync(env); eval_expr_prec(b, p, ctx, 1, ctx->ta); } // integer expression -> $w0
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
    char annot[96]; annot[0] = '\0';
    if (p->current_token.type == TOKEN_COLON) {
        fe_advance(p);
        if (p->current_token.type == TOKEN_IDENTIFIER) {
            strncpy(annot, p->current_token.lexeme, sizeof(annot) - 1); annot[sizeof(annot) - 1] = '\0';
        }
        while (p->current_token.type != TOKEN_ASSIGN && p->current_token.type != TOKEN_QUICK_ASSIGN &&
               p->current_token.type != TOKEN_SEMICOLON && p->current_token.type != TOKEN_EOF)
            fe_advance(p);
    }
    if (p->current_token.type == TOKEN_ASSIGN || p->current_token.type == TOKEN_QUICK_ASSIGN)
        fe_advance(p);
    int s = bind_lookup(*env->locals, *env->nlocals, lname);
    if (s < 0) { s = *env->nlocals; bind_add(env->locals, env->nlocals, env->caplocals, lname, s); }
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
    lower_init_value(b, p, env);
    int rhs_class = g_last_inst_class[0] ? class_find(g_last_inst_class) : rhs_local_class;
    codegen_li_emit_store_local(b, s);
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
    codegen_li_emit_store_local(b, slot);
    if (tl >= 0) {
        codegen_li_emit_iconst(b, rhs_class >= 0 ? class_type_id(rhs_class) : -1);
        codegen_li_emit_store_local(b, g_traitlocal[tl].tid_slot);
    }
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    return 1;
}

// `NAME(arg, …);` extern call inside a body. Args may be string/int literals or named locals
// (loaded from their slot), staged via OP_SETARG with the last left in $w0. Returns 1 if it
// resolved NAME to a registered import, else 0 (caller skips the token).
static int lower_call_stmt(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
    if (p->current_token.type != TOKEN_IDENTIFIER || p->peek_token.type != TOKEN_LPAREN) return 0;
    int idx = bind_lookup(*env->binds, *env->nb, p->current_token.lexeme);
    if (idx < 0) return 0;
    fe_advance(p); // NAME
    fe_advance(p); // '('
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

// Phase 15 (15.A): a class member STATEMENT — `obj.field = expr;`, `obj.method(args);` (result
// discarded). Returns 1 if consumed. Shared by the top-level loop and the block dispatcher.
// Phase 15 (15.B): DYNAMIC dispatch `g.method(args)` where `g` is a FAT trait-typed local. Resolves
// the routine slot at runtime via vtable_get(g.type_id, method_id), then OP_CALL_FUNC with g's handle
// as self. Returns 1 if consumed (the receiver is a trait local + `method` is one of the trait's
// methods), 0 otherwise (so a concrete receiver falls through to 15.A static dispatch). The slot is
// parked in a temp because OP_CALL_FUNC needs $w0=slot while its args occupy $a0.. (and VTABLE_GET
// itself clobbers $a0/$w0). Result in $w0.
static int lower_trait_dispatch(BytecodeBuffer* b, Parser* p, LowerEnv* env) {
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
    env_sync(env);
    // slot = vtable_get(type_id, method_id)  (type_id read at RUNTIME from the fat local)
    codegen_li_emit_load_local(b, tid_slot); codegen_li_emit_setarg(b, 0); // $a0 = type_id
    codegen_li_emit_iconst(b, mid);                                        // $w0 = method_id
    codegen_li_emit_vtable(b, OP_VTABLE_GET);                              // $w0 = routine slot
    int slot_tmp = env->ctx->ta->next_temp++;
    if (env->ctx->ta->next_temp > env->ctx->ta->hw) env->ctx->ta->hw = env->ctx->ta->next_temp;
    codegen_li_emit_store_local(b, slot_tmp);                             // park the slot
    // stage self + explicit args, then call the resolved slot
    codegen_li_emit_load_local(b, handle_slot); codegen_li_emit_setarg(b, 0); // $a0 = self
    int argc = 1;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF && argc < 8) {
        if (p->current_token.type == TOKEN_COMMA) { fe_advance(p); continue; }
        lower_codec_value(b, p, env->ctx);   // arg -> $w0 (int / named local)
        codegen_li_emit_setarg(b, argc); argc++;
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_load_local(b, slot_tmp); // $w0 = slot
    codegen_li_emit_call_func(b, argc);      // $w0 = method result (dynamic dispatch)
    env->ctx->ta->next_temp--;               // free the parked-slot temp
    return 1;
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

        localcls_reset(); // method scope: self (+ any objects it instantiates) are class-typed
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
    localcls_reset();
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
    localcls_reset();
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
    localcls_reset();
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
                if (nargs < 16 && is_codec_head(&parser)) {
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
