// src/parser/pattern.h   (namespace 'teko::parser')
//
// The parser's PATTERN type declarations, the C23 mirror of parser/pattern.tks
// (LiteralPattern, RangePattern, AltPattern, BindPattern, FieldPattern,
// WildcardPattern, Pattern + Arm). Split out of ast.h so each Teko file has its
// same-name C pair. TYPE-ONLY header.
//
// Patterns reference expressions by value (a range/literal bound IS an Expr), so the
// full tk_expr definition must already be visible. ast.h includes this header AFTER
// it defines tk_expr; do not include pattern.h before tk_expr exists.
#ifndef TK_PARSER_PATTERN_H
#define TK_PARSER_PATTERN_H

#include "type.h"             // tk_path
// NOTE: tk_expr (the literal/range bound type) must be defined before this header is
// included — ast.h arranges that ordering.

#include <stdbool.h>
#include <stddef.h>           // size_t

// =========================================================================
// Patterns (parser/pattern.tks: Literal/Range/Alt/Bind/Field/Wildcard + Pattern)
// =========================================================================
typedef struct tk_pattern tk_pattern;        // recursive (AltPattern holds []Pattern)

typedef struct { tk_expr value; }                                   tk_literal_pattern;  // a scalar literal
typedef struct { tk_expr lo; tk_expr hi; }                          tk_range_pattern;    // lo ..= hi (inclusive)
typedef struct { tk_pattern *options; size_t n_options; }           tk_alt_pattern;      // a | b | … (value axis)
typedef struct { tk_path type_name; bool has_binding; tk_str binding;
                 bool is_slice; tk_type_expr *slice_type;
                 tk_type_expr *type_args; size_t nargs; }              tk_bind_pattern; // `Foo` / `Foo as x` / `[]T as x` (is_slice: type_name unused, slice_type holds the `[]T`); (W9.4) type_args = explicit `Foo<i64> as x` generic args (NULL/0 otherwise, never with is_slice)
typedef struct { tk_path type_name; tk_str *fields; size_t n_fields; }  tk_field_pattern; // Type { f; g }
typedef struct { int _unused; }                                     tk_wildcard_pattern; // _ (no payload)
typedef struct { int _unused; }                                     tk_null_pattern;     // null — matches the NONE of an optional `T?` (REBOOT_PLAN §202); no payload

struct tk_pattern {
    enum { TK_PAT_WILDCARD, TK_PAT_LITERAL, TK_PAT_BIND, TK_PAT_FIELD, TK_PAT_RANGE, TK_PAT_ALT, TK_PAT_NULL } tag;
    union {
        tk_literal_pattern literal;
        tk_bind_pattern    bind;
        tk_field_pattern   field;
        tk_range_pattern   range;
        tk_alt_pattern     alt;
        // WILDCARD carries no payload
    } as;
};

struct tk_arm {                              // Arm = struct { pattern; has_when; guard; body }
    tk_pattern    pattern;
    bool          has_when;                  // a `when` guard present? (NOT counted for exhaustiveness)
    tk_expr       guard;                     // the guard condition (valid iff has_when)
    // The arm body is a STATEMENT BLOCK, exactly like an `if` then/else branch (B.20): a
    // `{ … }` block, OR one bracketless statement (`=> expr`, `=> return x`, `=> break`,
    // `=> continue`). A bracketless body lowers to a 1-element block, so checker / VM /
    // codegen treat every arm body like a braced block.
    tk_statement *body; size_t nbody;
};

#endif // TK_PARSER_PATTERN_H
