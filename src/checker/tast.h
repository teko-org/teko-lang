// src/checker/tast.h — the TYPED AST (the checker's output; codegen reads it, .tkb
// serializes it). C1: complete — every expression node, statement and item.
#ifndef TK_CHECK_TAST_H
#define TK_CHECK_TAST_H

#include "type.h"
#include "scope.h"           // tk_env (the typed pass threads it like check_*)
#include "../core.h"         // TK_RESULT, TK_LIST
#include "../parser/ast.h"   // tk_path, tk_token_kind, tk_bind_kind, tk_bind_target,
                             // tk_param, tk_pattern, tk_type_decl, tk_use_decl

typedef struct tk_texpr tk_texpr;            // recursive (children are tk_texpr*)
typedef struct tk_tstatement tk_tstatement;  // recursive (blocks are tk_tstatement*)

// --- typed expressions ---
typedef enum {
    TK_TEXPR_NUMBER, TK_TEXPR_VAR, TK_TEXPR_STR, TK_TEXPR_BYTE,
    TK_TEXPR_BOOL, TK_TEXPR_NULL,                          // true/false (LEGISLATION §75); null (REBOOT §202)
    TK_TEXPR_BINARY, TK_TEXPR_UNARY, TK_TEXPR_COMPARE, TK_TEXPR_CALL,
    TK_TEXPR_IF, TK_TEXPR_MATCH, TK_TEXPR_CAST, TK_TEXPR_FIELD_ACCESS,
    TK_TEXPR_SAFE_FIELD_ACCESS, TK_TEXPR_COALESCE,         // recv?.field / a ?? b (REBOOT §203)
    TK_TEXPR_STRUCT_INIT,                                  // Name { f = v, … } — struct value constructor (W4a)
    TK_TEXPR_INDEX,                                        // recv[index] — str→byte / []T→T (W5-idx)
    TK_TEXPR_INTERP,                                       // $"…{expr}…" — string interpolation (self-host parity)
    TK_TEXPR_IN,                                           // <expr> in [ … ] — membership test (Phase 2); `.type` is bool
    TK_TEXPR_PATH,                                         // Enum::Member as a VALUE — `.type` is the NAMED enum
    TK_TEXPR_ARRAY,                                        // [ e0, e1, … ] — slice/array literal (Increment B+); `.type` is []T
    TK_TEXPR_LAMBDA,                                       // (W10) (params) => body — typed closure literal; `.type` is the Func type
    TK_TEXPR_CHAR,                                         // c'x' — one UTF-8 codepoint literal; `.type` is `char`
} tk_texpr_tag;

// (W10) a typed closure literal. params/body/captures/ret as in tast.tks::TLambda; `lift_id` names
// the lifted C function `__tkclo_<lift_id>` + its env struct. A capture is BY COPY unless `by_ref`.
typedef struct { tk_str name; tk_type type; }                 tk_tlambda_param;
typedef struct { tk_str name; tk_type type; bool by_ref; }    tk_tcapture;
typedef struct {
    tk_tlambda_param *params; size_t nparams;
    tk_tstatement   *body;    size_t nbody;
    tk_tcapture     *captures; size_t ncaptures;
    tk_type          ret;
    uint64_t         lift_id;
} tk_tlambda;

typedef struct { tk_token_kind op; tk_texpr *operand; } tk_tcmp_term;
// Format spec entry in a typed interp node (ROUND 0). Parallel to holes[].
typedef struct {
    tk_fspec_kind kind;
    tk_str        static_spec;   // TK_FSPEC_STATIC: the literal spec string (e.g. "F2")
    tk_texpr     *dyn_args;      // TK_FSPEC_DYNAMIC: typed arg exprs (must be str)
    size_t        ndyn_args;
} tk_tinterp_spec;

typedef struct {
    tk_pattern pattern;       // syntactic (binds; its own typing is C7a)
    bool       has_when;
    tk_texpr  *guard;         // valid iff has_when
    // The typed arm body is a STATEMENT BLOCK, exactly like a typed `if` then/else branch
    // (B.20): the block's trailing-value type is the arm's value (tk_tblock_type), OR the
    // block DIVERGES (return/break/continue) and contributes no value.
    tk_tstatement *body; size_t nbody;
} tk_tarm;

struct tk_texpr {
    tk_texpr_tag tag;
    tk_type      type;        // this node's resolved type
    uint32_t     line, col;   // (C1-POS/E1) source position, copied from the untyped tk_expr; 0 = unknown
    union {
        struct { bool is_float; __int128 value; double fval; }       number;  // raw literal value; `.type` decides width/float-kind (N1/N2). C bootstrap uses __int128.
        struct { tk_str name; bool is_func; tk_str func_ns; }        var;   // (W10a) is_func = a bare top-level-fn reference used as a VALUE → tk_closure literal; func_ns = the fn's declaring namespace (C-symbol mangle)
        struct { tk_str text; }                                      str;
        struct { tk_byte value; }                                    byte;
        struct { tk_str bytes; }                                     char_lit; // TK_TEXPR_CHAR — `.type` is `char`; the codepoint's raw UTF-8 bytes
        struct { bool value; }                                       boolean; // TK_TEXPR_BOOL — `.type` is bool prim (LEGISLATION §75)
        struct { char _unused; }                                     null_lit; // TK_TEXPR_NULL — `.type` is the inferred TK_TYPE_OPTIONAL (REBOOT §202)
        struct { tk_token_kind op; tk_texpr *left, *right; }         binary;
        struct { tk_token_kind op; tk_texpr *operand; }             unary;
        struct { tk_texpr *first; tk_tcmp_term *rest; size_t nrest; } compare;
        struct { tk_path callee; tk_texpr *args; size_t nargs; tk_str call_ns; bool is_closure_call; tk_type callee_type; bool is_iface_dispatch; uint32_t iface_slot; } call;  // call_ns: resolved target's namespace ("" = builtin/local → no mangle) (#41/#49). (W10a) is_closure_call = callee is a LOCAL of function type (a tk_closure VALUE) → call through `((R(*)(A,B))f.fn)(args)` using callee_type (the Func) for the cast. (W10b.D3) is_iface_dispatch = a DYNAMIC contract-method call: callee = [<Iface>, <method>], args[0] = the receiver (an interface value or a class instance the emit upcasts), callee_type = the resolved interface-method Func (receiver param typed Named{Iface}), iface_slot = the method's index in the interface's effective-method list (== its vtable slot)
        struct { tk_texpr *cond; tk_tstatement *then_blk; size_t nthen;
                 bool has_else; tk_tstatement *else_blk; size_t nelse; } if_expr;
        struct { tk_texpr *subject; tk_tarm *arms; size_t narms; }    match_expr;
        struct { tk_texpr *expr; }                                    cast;   // `x to T` — target rides the node's `type`
        struct { tk_texpr *receiver; tk_str field; }                  field_access; // `x.field` (C3) — `.type` is the field's type
        struct { tk_texpr *receiver; tk_str field; }                  safe_field_access; // `recv?.field` (REBOOT §203) — `.type` is `(field)?`
        struct { tk_texpr *left, *right; }                            coalesce; // `a ?? b` (REBOOT §203) — `.type` is the unwrapped/result type
        // Name { f = v, … } (W4a) — `.type` is the named struct type; field_names/field_vals are
        // in the struct's DECLARED order (the checker reorders), so both backends lower identically.
        struct { tk_str *field_names; tk_texpr *field_vals; size_t nfields; } struct_init;
        // recv[index] (W5-idx) — `.type` is the element type: byte for a `str` receiver, the
        // slice's element type for a `[]T` receiver. The index is an integer-typed texpr.
        struct { tk_texpr *receiver; tk_texpr *index; }              index;
        // $"…{expr}…" (self-host parity) — `.type` is `str`. The string is pieces[0] ++
        // str(holes[0]) ++ pieces[1] ++ … ++ pieces[nholes] (npieces == nholes + 1). Each
        // hole carries its OWN resolved type (str passthrough vs integer→decimal text), so
        // both backends lower it identically (differential equivalence).
        // specs[i] carries the optional format spec for holes[i] (nspecs == nholes).
        // (ROUND 0) A TK_TFSPEC_STATIC spec on a primitive hole enables format helpers;
        // a TK_TFSPEC_DYNAMIC spec carries the typed arg expressions.
        struct {
            tk_str   *pieces; size_t npieces;
            tk_texpr *holes;  size_t nholes;
            tk_tinterp_spec *specs;   /* nholes elements, parallel to holes; NULL = all NONE */
        } interp;
        // <expr> in [ … ] (Phase 2) — `.type` is bool. The lhs is EVALUATED ONCE; the result
        // is true iff lhs equals any element. Both lhs and the elements are typed TExprs, so
        // both backends lower it identically (differential equivalence).
        struct { tk_texpr *lhs; tk_texpr *elems; size_t nelems; }                  in_expr;
        // Enum::Member as a VALUE (E#/value-level enum paths). `.type` is the NAMED enum; the
        // checker resolves the enum decl + member ORDINAL so both backends lower without re-lookup:
        // codegen → the C constant `TK_E_<UPPER enum_name>_<UPPER member>`; VM → the ordinal int.
        // (#50) `value` is the checker-RESOLVED member value — the ordinal for an enum member,
        // `1 << ordinal` (power-of-2, u128 cap) for a flags member — so the VM reads the value
        // instead of recomputing it (native codegen's pre-emitted constants already encode it).
        struct { tk_str enum_name; tk_str member; uint64_t ordinal; unsigned __int128 value; }  path;
        // [ e0, e1, … ] (Increment B+) — `.type` is []T; each element is a typed texpr already
        // adopted into the element type T, so both backends build the same `[]T` value.
        // A spread element (`..xs`) carries is_spread=true and was checked against []T; the spread
        // slice itself is stored as the typed expr (codegen flattens it at runtime).
        struct { tk_texpr *elements; size_t nelements; bool *is_spread; }  array;
        tk_tlambda  lambda;   // TK_TEXPR_LAMBDA (W10)
    } as;
};

// --- typed statements ---
typedef enum {
    TK_TSTMT_BINDING, TK_TSTMT_ASSIGN, TK_TSTMT_RETURN, TK_TSTMT_LOOP,
    TK_TSTMT_BREAK, TK_TSTMT_CONTINUE, TK_TSTMT_EXPR, TK_TSTMT_DEFER,
} tk_tstatement_tag;

struct tk_tstatement {
    tk_tstatement_tag tag;
    union {
        struct { tk_bind_kind kind; tk_bind_target target; tk_type bound; tk_texpr value; } binding;
        struct { tk_str name; tk_token_kind op; tk_type bound; tk_texpr value; bool deref; }  assign;   // bound = the target's declared type (codegen wraps the value into it — emit_as); deref ⇒ `name.value op= …` writes THROUGH a Ref<T> (MEM-1b-ii)
        struct { bool has_value; tk_texpr value; }                                           ret;   // value gated by has_value
        struct { tk_str label; tk_tstatement *body; size_t nbody; }                          loop_stmt;   // label empty = unlabeled
        struct { tk_str label; }                                                             jump;        // BREAK/CONTINUE — label empty = innermost
        struct { tk_texpr expr; }                                                            expr_stmt;
        struct { tk_tstatement *body; size_t nbody; }                                       defer_stmt;  // DEFER (C7.18) — the cleanup block
    } as;
};

// --- typed items + program (mirror the checker's Item/Program — E5c) ---
typedef struct {
    tk_str         name;
    tk_str        *type_params; size_t n_type_params;   // (S4) generic type-parameter names (0 for a non-generic fn); monomorphized before codegen
    tk_constraint_expr *type_constraints;    // (W11/S6) PARALLEL to type_params — checked by the monomorph pass when a param is bound to a concrete type
    tk_param      *params; size_t nparams;   // immutable (B.21), carried unchanged
    tk_type        return_type;              // void when there is no `-> ret` (M.3)
    tk_tstatement *body;   size_t nbody;
    tk_visibility  vis;                      // private / pub / exp (carried from the parsed decl)
    bool           has_doc;                  // a `/** … */` doc precedes it? (carried for the `.tkh`)
    tk_str         doc;                      // the doc span (valid iff has_doc)
    tk_str         namespace;                // (#41/#49) the declaring namespace — drives the mangled C name
    tk_str         file;                     // (E3) the source file the fn was declared in (for the .tsym symbol map)
    uint32_t       line, col;                // (E3) the fn name's 1-based source position (for the .tsym symbol map)
    bool           is_test;                  // (D2) a `#test` attribute precedes it? (run by `teko test` / the build gate)
    bool           is_extern;                // (C7.1a) a foreign `extern fn` (no body — codegen emits a C prototype + direct call)
    tk_str         c_symbol;                 // (C7.1a) the C symbol it binds (valid iff is_extern)
    tk_str         from_lib;                 // (C7.1a) the providing library ("" = implicit libc; valid iff is_extern)
} tk_tfunction;

typedef enum { TK_TITEM_FUNCTION, TK_TITEM_TYPE_DECL, TK_TITEM_USE, TK_TITEM_STATEMENT } tk_titem_tag;
typedef struct {
    tk_titem_tag tag;
    union {
        tk_tfunction  function;
        tk_type_decl  type_decl;   // pass-through (no expr to type)
        tk_use_decl   use_decl;    // pass-through
        tk_tstatement statement;   // a loose top-level statement, typed
    } as;
} tk_titem;

typedef struct { tk_titem *items; size_t nitems; } tk_tprogram;

// env-threading carriers (the typed pass advances the env, like check_*).
typedef struct { tk_tstatement node;  tk_env env; }            tk_typed_stmt;
typedef struct { tk_tstatement *stmts; size_t n; tk_env env; } tk_typed_block;

// result stamps (T | error). tk_texpr_result is the canonical home (tkb_read.c reuses it).
TK_RESULT(tk_texpr,      tk_texpr_result);        // TExpr      | error
TK_RESULT(tk_tarm,       tk_tarm_result);         // TArm       | error
TK_RESULT(tk_typed_stmt, tk_typed_stmt_result);   // TypedStmt  | error
TK_RESULT(tk_typed_block,tk_typed_block_result);  // TypedBlock | error
TK_RESULT(tk_tfunction,  tk_tfunction_result);    // TFunction  | error
TK_RESULT(tk_titem,      tk_titem_result);        // TItem      | error
TK_RESULT(tk_tprogram,   tk_tprogram_result);     // TProgram   | error

#endif // TK_CHECK_TAST_H
