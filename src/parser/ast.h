// src/parser/ast.h — the parser AST TYPES, the C23 mirror of the Teko AST source
// (parser/ast.tks, parser/type.tks, parser/pattern.tks, plus parse_file's File model).
//
// TYPE-ONLY header (B0c part 1). It defines every struct + tag enum the AST has, so the
// checker/emit `.c` files that read the AST can compile. The parser FUNCTION BODIES
// (parse_*) live in parser/parser.c (B0c part 2 — next lote), NOT here.
//
// Field/shape names are mirrored from the canonical Teko source AND from the canonical
// C consumers (src/checker/*.c, src/emit/*.c — `src` is canonical for code). Where the
// two disagree, the consumers decide (they are the code that must compile).
//
// Recursive nodes use pointers (the compiler-managed indirection of the Teko side).
#ifndef TK_PARSER_AST_H
#define TK_PARSER_AST_H

#include "../lexer/token.h"   // tk_token, tk_token_kind (op-typed fields)
#include "../text/text.h"     // tk_str, tk_byte
#include "../core.h"          // TK_RESULT, TK_LIST, tk_error
#include "type.h"             // tk_segment, tk_path, tk_type_expr (parser/type.tks)

#include <stddef.h>           // size_t
#include <stdint.h>           // int64_t

// (Paths + type expressions are now in type.h — parser/type.tks's same-name C pair.)
// (Patterns + Arm are in pattern.h, included BELOW once tk_expr is defined.)

// =========================================================================
// Expressions (parser/ast.tks: Number/Var/StrLit/ByteLit/Binary/Unary/CmpTerm/
//   Compare/Call/IfExpr/MatchExpr/FieldAccess/MethodCall/Cast/PathExpr + Expr)
// =========================================================================
typedef struct tk_expr      tk_expr;        // recursive (children are tk_expr*)
typedef struct tk_statement tk_statement;   // IfExpr blocks are []Statement
typedef struct tk_arm       tk_arm;         // MatchExpr arms

typedef struct { tk_token_kind op; tk_expr *operand; } tk_cmp_term;   // one cmp continuation

// Each non-leaf Expr node is a named typedef (the checker passes them by value to
// helpers — e.g. type_binary(tk_binary), type_if(tk_if_expr)). The leaf nodes
// (Number/Var/StrLit/ByteLit/PathExpr) stay inline in the union.
// A literal number leaf (N1/N2 — TEKO_CORRECTION_PLAN §5). The node carries the RAW
// value; its resolved `.type` (set by the typer) decides width/signedness/float-kind.
//  - integer literal (any width, incl u128/i128): is_float=false, `value` holds it.
//  - float literal (f16/f32/f64): is_float=true, `fval` holds it (default f64).
// (C bootstrap uses __int128 for the integer carrier; the canonical Teko spelling uses
//  the appropriate native int.)
typedef struct { bool is_float; __int128 value; double fval; } tk_number;        // a literal number leaf
typedef struct { tk_str name; }                              tk_var;           // a variable reference
typedef struct { tk_str text; }                              tk_str_lit;       // "…"
typedef struct { tk_byte value; }                            tk_byte_lit;      // b'x'
typedef struct { tk_str bytes; }                             tk_char_lit;      // c'x' — one UTF-8 codepoint (raw bytes)
typedef struct { bool value; }                               tk_bool_lit;      // true / false (LEGISLATION §75)
typedef struct { char _unused; }                             tk_null_lit;      // null (REBOOT_PLAN §202) — no payload; typed `T?` by the checker
typedef struct { tk_token_kind op; tk_expr *left, *right; }  tk_binary;        // any binary op
typedef struct { tk_token_kind op; tk_expr *operand; }       tk_unary;         // - ~ ! (prefix)
typedef struct { tk_expr *first; tk_cmp_term *rest; size_t nrest; } tk_compare; // a<b<c chain (M.3)
typedef struct { tk_path callee; tk_expr *args; size_t nargs; tk_str *arg_names; }     tk_call;    // f(x), lexer::foo(x); arg_names[i] PARALLEL to args (DEFARGS 2026-07-01) — {0} (empty) = positional, else the NAMED param this arg targets
typedef struct { tk_expr *cond; tk_statement *then_blk; size_t nthen;          // if/else IS an expression
                 bool has_else; tk_statement *else_blk; size_t nelse; } tk_if_expr;
typedef struct { tk_expr *subject; tk_arm *arms; size_t narms; }    tk_match_expr; // match IS an expression
typedef struct { tk_expr *receiver; tk_str field; }          tk_field_access;  // x.field
typedef struct { tk_expr *receiver; tk_str field; }          tk_safe_field_access; // x?.field — null-propagating (REBOOT_PLAN §203)
typedef struct { tk_expr *left, *right; }                    tk_coalesce;      // x ?? y — Elvis/null-coalescing (REBOOT_PLAN §203)
typedef struct { tk_expr *receiver; tk_str method;                            // recv.method(a, …)
                 tk_expr *args; size_t nargs; }              tk_method_call;
typedef struct { tk_expr *expr; tk_type_expr target; }       tk_cast;          // x to T
typedef struct { tk_path path; }                             tk_path_expr;     // Enum::Member as a VALUE
// Name { field = value, … } — a struct VALUE constructor (W4a). Parallel arrays (field_vals is a
// flat tk_expr array like tk_call.args — by-value tk_expr is incomplete here, so pointer fields).
// (W9.4) `type_args` carries explicit generic type-arguments at the construction site —
// `Foo<i64>{ … }` → type_args=[i64], so the literal targets the stamped instance `Foo__g__i64`
// with NO `let x: Foo<i64> = …` annotation. NULL/0 for the bare `Foo { … }` form (unchanged).
typedef struct { tk_path type_path; tk_type_expr *type_args; size_t nargs; tk_str *field_names; tk_expr *field_vals; size_t nfields; } tk_struct_lit;
typedef struct { tk_expr *receiver; tk_expr *index; }       tk_index;         // recv[index] — str→byte, []T→T (W5-idx)
// Format spec for a single $"…{expr:spec}…" hole. Three cases:
//   TK_FSPEC_NONE    — no colon in hole (existing behavior)
//   TK_FSPEC_STATIC  — `:literal` → static_spec holds the literal string (e.g. "F2")
//   TK_FSPEC_DYNAMIC — `:[arg1,arg2,…]` → dyn_args[0..ndyn_args) are expr arguments
typedef enum { TK_FSPEC_NONE, TK_FSPEC_STATIC, TK_FSPEC_DYNAMIC } tk_fspec_kind;
typedef struct {
    tk_fspec_kind kind;
    tk_str        static_spec;   // TK_FSPEC_STATIC: the literal spec string
    tk_expr      *dyn_args;      // TK_FSPEC_DYNAMIC: array of expressions
    size_t        ndyn_args;     // length of dyn_args
} tk_format_spec;
// `$"pre {a} mid {b} post"` — string interpolation (self-host parity). The string is
// pieces[0] ++ str(holes[0]) ++ pieces[1] ++ … ++ pieces[nholes] — so npieces == nholes + 1.
// Pieces are DECODED literal str spans (escapes resolved, like StrLit); each hole is a full
// Teko expression (parsed from the hole's source span). A `str` hole passes through; an
// integer hole converts to its decimal text; any other hole type is a clean checker error.
// specs[i] carries the optional format spec for holes[i] (nspecs == nholes).
typedef struct { tk_str *pieces; size_t npieces; tk_expr *holes; size_t nholes;
                 tk_format_spec *specs; /* nholes elements, parallel to holes */ } tk_interp;
// `<expr> in [ e0, e1, … ]` → bool (Phase 2). True iff the LHS equals any element; the LHS is
// EVALUATED ONCE. The `[ … ]` is a SPECIAL membership-set syntax valid ONLY here (no general
// array literal). Empty set `x in []` is allowed → always false. `elems` is a flat tk_expr
// array (like tk_call.args — by-value tk_expr is incomplete here, so a pointer field).
typedef struct { tk_expr *lhs; tk_expr *elems; size_t nelems; } tk_in;
typedef struct { bool is_spread; tk_expr *expr; } tk_array_elem;        // one element in an array literal: plain (is_spread=false) or spread (is_spread=true, ..expr)
typedef struct { tk_array_elem *elements; size_t nelements; } tk_array_lit;   // [ e0, e1, … ] — slice/array literal (Increment B+)
// (W10) a closure LITERAL `(params) => expr` / `(params) => { … }` — an anonymous function value
// (NO `fn`). A param's type is OPTIONAL (`has_type` false ⇒ inferred from the target). The body is
// a STATEMENT BLOCK (`=> expr` is stored as one trailing ExprStmt). Return type inferred. The
// checker types it to a Func and lifts it to a top-level function + capture env.
typedef struct { tk_str name; bool has_type; tk_type_expr type_ann; } tk_lambda_param;
typedef struct { tk_lambda_param *params; size_t nparams; tk_statement *body; size_t nbody; } tk_lambda;

typedef enum {
    TK_EXPR_NUMBER, TK_EXPR_VAR, TK_EXPR_STR, TK_EXPR_BYTE,
    TK_EXPR_BOOL, TK_EXPR_NULL,
    TK_EXPR_BINARY, TK_EXPR_UNARY, TK_EXPR_COMPARE, TK_EXPR_CALL,
    TK_EXPR_IF, TK_EXPR_MATCH, TK_EXPR_FIELD_ACCESS, TK_EXPR_METHOD_CALL,
    TK_EXPR_SAFE_FIELD_ACCESS, TK_EXPR_COALESCE,
    TK_EXPR_CAST, TK_EXPR_PATH, TK_EXPR_STRUCT_LIT, TK_EXPR_INDEX,
    TK_EXPR_INTERP,   // $"…{expr}…" — string interpolation (self-host parity)
    TK_EXPR_IN,       // <expr> in [ … ] — membership test (Phase 2)
    TK_EXPR_ARRAY,    // [ e0, e1, … ] — slice/array literal (Increment B+)
    TK_EXPR_LAMBDA,   // (W10) (params) => body — anonymous closure literal
    TK_EXPR_CHAR,     // c'x' — one UTF-8 codepoint literal
} tk_expr_kind;

struct tk_expr {
    tk_expr_kind tag;
    uint32_t line, col;   // (C1-POS/E1) node source position — the expr's first token; stamped by the parser; 0 = unknown
    union {
        tk_number       number;        // TK_EXPR_NUMBER
        tk_var          var;           // TK_EXPR_VAR
        tk_str_lit      str;           // TK_EXPR_STR
        tk_byte_lit     byte;          // TK_EXPR_BYTE
        tk_char_lit     char_lit;      // TK_EXPR_CHAR
        tk_bool_lit     boolean;       // TK_EXPR_BOOL
        tk_null_lit     null_lit;      // TK_EXPR_NULL
        tk_binary       binary;        // TK_EXPR_BINARY
        tk_unary        unary;         // TK_EXPR_UNARY
        tk_compare      compare;       // TK_EXPR_COMPARE
        tk_call         call;          // TK_EXPR_CALL
        tk_if_expr      if_expr;       // TK_EXPR_IF
        tk_match_expr   match_expr;    // TK_EXPR_MATCH
        tk_field_access field_access;  // TK_EXPR_FIELD_ACCESS
        tk_safe_field_access safe_field_access; // TK_EXPR_SAFE_FIELD_ACCESS
        tk_coalesce     coalesce;      // TK_EXPR_COALESCE
        tk_method_call  method_call;   // TK_EXPR_METHOD_CALL
        tk_cast         cast;          // TK_EXPR_CAST
        tk_path_expr    path;          // TK_EXPR_PATH
        tk_struct_lit   struct_lit;    // TK_EXPR_STRUCT_LIT
        tk_index        index;         // TK_EXPR_INDEX
        tk_interp       interp;        // TK_EXPR_INTERP
        tk_in           in_expr;       // TK_EXPR_IN
        tk_array_lit    array;         // TK_EXPR_ARRAY
        tk_lambda       lambda;        // TK_EXPR_LAMBDA (W10)
    } as;
};

// =========================================================================
// Patterns + Arm (parser/pattern.tks) — now in pattern.h, their same-name C pair.
// tk_expr is fully defined above, so pattern.h's literal/range/guard/body fields
// (which hold tk_expr by value) and struct tk_arm resolve cleanly here.
// =========================================================================
#include "pattern.h"

// =========================================================================
// Statements (parser/ast.tks: BindKind/SimpleName/DestructurePattern/BindTarget/
//   Binding/Assign/Return/LoopStmt/BreakStmt/ContinueStmt/ExprStmt + Statement)
// =========================================================================
typedef enum { TK_BIND_LET, TK_BIND_MUT, TK_BIND_CONST } tk_bind_kind;   // let/mut/const

typedef struct { tk_str name; }                  tk_simple_name;          // let x = …
typedef struct { tk_str *names; size_t nnames; } tk_destructure_target;   // let { x; y } = …
typedef struct {                                                          // BindTarget = SimpleName | DestructurePattern
    enum { TK_BIND_SIMPLE, TK_BIND_DESTRUCTURE } tag;
    union { tk_simple_name simple; tk_destructure_target destructure; } as;
} tk_bind_target;

typedef struct {                                                          // let/mut/const TARGET [: T] = value
    tk_bind_kind  kind;
    tk_bind_target target;
    bool          has_type;
    tk_type_expr  type_ann;                                               // the parsed annotation (valid iff has_type)
    tk_expr       value;
} tk_binding;
typedef struct { tk_str name; tk_token_kind op; tk_expr value; bool deref; } tk_assign;   // x = / += / … (B.4); deref ⇒ `name.value op= …` writes THROUGH a Ref<T> (MEM-1b-ii)
typedef struct { bool has_value; tk_expr value; }               tk_return;    // return [expr] (value gated by has_value)
typedef struct { tk_str label; tk_statement *body; size_t nbody; } tk_loop_stmt; // loop [NAME] { … } (M.5); label empty (len 0) = unlabeled
typedef struct { tk_str label; }                                tk_jump;      // break [NAME] / continue [NAME]; label empty = innermost loop
typedef struct { tk_expr expr; }                                tk_expr_stmt; // a bare expression on its own line
// `defer { stmts }` (C7.18) — scoped cleanup block; executes LIFO at ANY scope exit
// (normal return, early return inside a loop, break out of a function).
// Panic does NOT run deferred blocks (simplifies implementation).
// Nested `defer` is rejected by the checker.
typedef struct { tk_statement *body; size_t nbody; } tk_defer_stmt;

struct tk_statement {
    enum { TK_STMT_BINDING, TK_STMT_ASSIGN, TK_STMT_RETURN,
           TK_STMT_LOOP, TK_STMT_BREAK, TK_STMT_CONTINUE, TK_STMT_EXPR,
           TK_STMT_DEFER } tag;
    union {
        tk_binding    binding;
        tk_assign     assign;
        tk_return     ret;
        tk_loop_stmt  loop_stmt;
        tk_expr_stmt  expr_stmt;
        tk_jump       jump;          // BREAK, CONTINUE — optional loop label
        tk_defer_stmt defer_stmt;    // DEFER — the cleanup block
    } as;
};

// =========================================================================
// Top-level items (parser/ast.tks: Param/Function/Field/StructBody/EnumBody/
//   VariantBody/TypeBody/TypeDecl/UseDecl/Decl + File model from parse_file.tks)
// =========================================================================
typedef struct { tk_str name; tk_type_expr type_ann; bool is_params; bool has_default; tk_expr default_expr; } tk_param;   // immutable (B.21); is_params = C#-style variadic modifier (2026-07-01), trailing-only, type_ann must be a Slice; has_default/default_expr = DEFARGS (2026-07-01) — TRAILING-ONLY, default_expr valid iff has_default

// tk_visibility — a declaration's REACH (LEGISLATION "Visibility — pub vs exp"; B.9).
// PRIVATE (default, no keyword) = own namespace only; PUB = visible across the project's
// namespaces (but NOT in the binary header); EXP = exported in the `.tkh` (the library's
// public ABI) — public by definition. Ordered by increasing reach: PRIVATE < PUB < EXP,
// so `vis >= TK_VIS_PUB` = "reachable cross-namespace" and `vis == TK_VIS_EXP` = "in the
// header". Governing Law: M.2 (the boundary is explicit) + M.3 (the mark is honest).
typedef enum { TK_VIS_PRIVATE = 0, TK_VIS_PUB, TK_VIS_EXP } tk_visibility;

typedef struct {                                                   // Function (parser/ast.tks)
    tk_str       name;
    tk_str      *type_params; size_t n_type_params;                 // generic type-parameter names (S4 — n_type_params 0 for a non-generic fn)
    tk_param    *params; size_t nparams;
    bool         has_return;                                       // `-> ret` present? (absent = Unit)
    tk_type_expr return_type;                                      // valid iff has_return
    tk_statement *body;  size_t nbody;
    tk_visibility vis;                                             // private (default) / pub / exp
    bool         has_doc;                                          // a `/** … */` doc precedes it?
    tk_str       doc;                                              // the doc span (valid iff has_doc)
    uint32_t     line, col;                                        // the name's 1-based source position (W-loc-2 diagnostics)
    bool         is_test;                                          // a `#test` attribute precedes it? (D2 — run by `teko test` / the build gate)
    bool         is_extern;                                        // an `extern` foreign-function declaration? (C7.1a — no body)
    tk_str       c_symbol;                                         // the C symbol it binds (valid iff is_extern)
    tk_str       from_lib;                                         // the providing library ("" = implicit libc; valid iff is_extern)
    tk_str       os_guard;                                         // a `#os("…")` conditional-compile guard ("" = all OSes; C7.1f)
} tk_function;

typedef struct { tk_str name; tk_type_expr type_ann; } tk_field;
typedef struct { tk_field *fields; size_t n_fields; }  tk_struct_body;
typedef struct { tk_str  *members; size_t n_members; } tk_enum_body;    // member names, in order
typedef struct {
    tk_str            *members;  // member names, in order
    size_t             n_members;
    unsigned __int128 *values;   // (C8.6) power-of-2 value per member (member i → 1<<i); set by checker (C8.3); NULL before check
} tk_flags_body;                 // member names + auto-assigned power-of-2 values (C8.3 assigns; C8.6 serializes)
typedef struct { tk_type_expr type_expr; }             tk_variant_body; // a union (A.8)
typedef struct { tk_type_expr alias; }                 tk_alias_body;   // `type Name = <type-expr>` — a TRANSPARENT alias (self-host parity)
typedef struct { int _unused; }                        tk_extern_body;  // `extern type Name` — an OPAQUE foreign handle (C7.1a; lowers to `void *`)
typedef struct {                                                        // TypeBody = StructBody | EnumBody | FlagsBody | VariantBody | AliasBody | ExternBody
    enum { TK_BODY_STRUCT, TK_BODY_ENUM, TK_BODY_FLAGS, TK_BODY_VARIANT, TK_BODY_ALIAS, TK_BODY_EXTERN } tag;
    union { tk_struct_body struct_body; tk_enum_body enum_body; tk_flags_body flags_body; tk_variant_body variant_body; tk_alias_body alias_body; tk_extern_body extern_body; } as;
} tk_type_body;
typedef struct {                                                        // TypeDecl (nominal — B.13)
    tk_str        name;
    tk_str       *type_params; size_t n_type_params;                     // generic type-parameter names (S4 — n_type_params 0 for a non-generic type)
    tk_type_body  body;
    tk_visibility vis;                                                  // private (default) / pub / exp
    bool          has_doc;
    tk_str        doc;                                                  // valid iff has_doc
    uint32_t      line, col;                                            // the name's 1-based source position (W-loc-2 diagnostics)
} tk_type_decl;

typedef struct {                                                        // UseDecl = struct { path; has_alias; alias }
    tk_path path;
    bool    has_alias;
    tk_str  alias;
} tk_use_decl;

// --- R-main file model (parser/ast.tks: Decl/MainFile/Module/File) ---
typedef struct {                                                        // Decl = Function | TypeDecl
    enum { TK_DECL_FUNCTION, TK_DECL_TYPE } tag;
    union { tk_function function; tk_type_decl type_decl; } as;
} tk_decl;
typedef struct { tk_use_decl *uses; size_t n_uses; tk_statement *body;  size_t n_body;  } tk_main_file;  // use header + virtual main body
typedef struct { tk_use_decl *uses; size_t n_uses; tk_decl      *decls; size_t n_decls; } tk_module;     // use header + declarations
typedef struct {                                                        // File = MainFile | Module
    enum { TK_FILE_MAIN, TK_FILE_MODULE } tag;
    union { tk_main_file main_file; tk_module module; } as;
} tk_file;

// --- the FLATTENED Item/Program model — the checker's input view. The parser produces
//     File/Module/Decl; the driver flattens them into this Program of Items, which the
//     checker (typer.c, collect.c) consumes. Mirrored in parser/ast.tks (ItemKind/Item/
//     Program). The tk_item tag+union = Teko's ItemKind variant; `namespace` = the A3
//     provenance field (resolution/typing ignore it). ---
typedef struct {                                                        // a top-level item (function/type/use/loose stmt)
    enum { TK_ITEM_FUNCTION, TK_ITEM_TYPE_DECL, TK_ITEM_USE, TK_ITEM_STATEMENT } tag;
    union {
        tk_function  function;
        tk_type_decl type_decl;
        tk_use_decl  use_decl;
        tk_statement statement;
    } as;
    // A3 — the namespace this item came from, for multi-file project assembly
    // (codegen name-mangling later). The project assemble pass tags every merged item
    // with its SourceFile namespace; the bare root main.tks items carry the project's
    // canonical root name. Resolution/typing IGNORE this — it carries provenance only.
    tk_str namespace;
    // W-loc-2 — the source FILE this item came from (assemble tags it from the SourceFile
    // path), so a flattened-program checker error resolves to file:line:col. Empty for the
    // single-file (compile/run) path, where the driver already knows the path.
    tk_str file;
} tk_item;
typedef struct { tk_item *items; size_t len; } tk_program;              // a flat item list

// =========================================================================
// List-append + box helper PROTOTYPES (bodies: parser/parser.c, next lote).
// These grow the variable-length AST arrays the parser builds; box helpers give the
// compiler-managed indirection for recursive nodes (tk_expr*, tk_type_expr*).
// =========================================================================
void tk_exprs_push       (tk_expr **xs,       size_t *n, tk_expr       item);
void tk_array_elems_push (tk_array_elem **xs, size_t *n, tk_array_elem item);
void tk_stmts_push (tk_statement **xs, size_t *n, tk_statement item);
void tk_pats_push  (tk_pattern **xs,   size_t *n, tk_pattern   item);
void tk_arms_push  (tk_arm **xs,       size_t *n, tk_arm       item);
void tk_params_push(tk_param **xs,     size_t *n, tk_param     item);
void tk_lambda_params_push(tk_lambda_param **xs, size_t *n, tk_lambda_param item);   // (W10)
void tk_fields_push(tk_field **xs,     size_t *n, tk_field     item);
void tk_segs_push  (tk_segment **xs,   size_t *n, tk_segment   item);
void tk_strvec_push(tk_str **xs,       size_t *n, tk_str       item);   // (renamed from tk_strs_push to avoid the TK_LIST(tk_str, tk_strs) clash in TUs that also include build/manifest.h — A3)
void tk_types_push (tk_type_expr **xs, size_t *n, tk_type_expr item);
void tk_terms_push (tk_cmp_term **xs,  size_t *n, tk_cmp_term  item);
void tk_decls_push (tk_decl **xs,      size_t *n, tk_decl      item);
void tk_uses_push  (tk_use_decl **xs,  size_t *n, tk_use_decl  item);

tk_expr      *tk_box_expr(tk_expr e);        // heap-box a tk_expr  (recursive children)
tk_type_expr *tk_box_type(tk_type_expr t);   // heap-box a tk_type_expr ([]T element)

#endif // TK_PARSER_AST_H
