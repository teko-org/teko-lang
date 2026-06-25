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
} tk_texpr_tag;

typedef struct { tk_token_kind op; tk_texpr *operand; } tk_tcmp_term;

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
    union {
        struct { bool is_float; __int128 value; double fval; }       number;  // raw literal value; `.type` decides width/float-kind (N1/N2). C bootstrap uses __int128.
        struct { tk_str name; }                                      var;
        struct { tk_str text; }                                      str;
        struct { tk_byte value; }                                    byte;
        struct { bool value; }                                       boolean; // TK_TEXPR_BOOL — `.type` is bool prim (LEGISLATION §75)
        struct { char _unused; }                                     null_lit; // TK_TEXPR_NULL — `.type` is the inferred TK_TYPE_OPTIONAL (REBOOT §202)
        struct { tk_token_kind op; tk_texpr *left, *right; }         binary;
        struct { tk_token_kind op; tk_texpr *operand; }             unary;
        struct { tk_texpr *first; tk_tcmp_term *rest; size_t nrest; } compare;
        struct { tk_path callee; tk_texpr *args; size_t nargs; }      call;
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
        struct { tk_str *pieces; size_t npieces; tk_texpr *holes; size_t nholes; } interp;
    } as;
};

// --- typed statements ---
typedef enum {
    TK_TSTMT_BINDING, TK_TSTMT_ASSIGN, TK_TSTMT_RETURN, TK_TSTMT_LOOP,
    TK_TSTMT_BREAK, TK_TSTMT_CONTINUE, TK_TSTMT_EXPR,
} tk_tstatement_tag;

struct tk_tstatement {
    tk_tstatement_tag tag;
    union {
        struct { tk_bind_kind kind; tk_bind_target target; tk_type bound; tk_texpr value; } binding;
        struct { tk_str name; tk_token_kind op; tk_texpr value; }                            assign;
        struct { bool has_value; tk_texpr value; }                                           ret;   // value gated by has_value
        struct { tk_str label; tk_tstatement *body; size_t nbody; }                          loop_stmt;   // label empty = unlabeled
        struct { tk_str label; }                                                             jump;        // BREAK/CONTINUE — label empty = innermost
        struct { tk_texpr expr; }                                                            expr_stmt;
    } as;
};

// --- typed items + program (mirror the checker's Item/Program — E5c) ---
typedef struct {
    tk_str         name;
    tk_param      *params; size_t nparams;   // immutable (B.21), carried unchanged
    tk_type        return_type;              // void when there is no `-> ret` (M.3)
    tk_tstatement *body;   size_t nbody;
    tk_visibility  vis;                      // private / pub / exp (carried from the parsed decl)
    bool           has_doc;                  // a `/** … */` doc precedes it? (carried for the `.tkh`)
    tk_str         doc;                      // the doc span (valid iff has_doc)
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
