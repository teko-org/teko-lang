// src/parser/type.h   (namespace 'teko::parser')
//
// The parser's TYPE-EXPRESSION type declarations, the C23 mirror of parser/type.tks
// (Segment, Path, NamedType, SliceType, UnionType, TypeExpr). Split out of ast.h so
// each Teko file has its same-name C pair. TYPE-ONLY header.
#ifndef TK_PARSER_TYPE_H
#define TK_PARSER_TYPE_H

#include "../text/text.h"     // tk_str

#include <stddef.h>           // size_t

// =========================================================================
// Paths (parser/type.tks: Segment, Path)
// =========================================================================
typedef struct { tk_str name; } tk_segment;          // one `::`-joined identifier
typedef struct {                                     // Path = struct { segments: []Segment }
    tk_segment *segments;                            // at least one
    size_t      len;                                 // segment count (Teko `segments.len`)
} tk_path;

// =========================================================================
// Type expressions (parser/type.tks: NamedType, SliceType, UnionType, TypeExpr)
// =========================================================================
typedef struct tk_type_expr tk_type_expr;            // recursive (Slice/Union/Optional hold TypeExpr)
struct tk_type_expr {
    enum { TK_TEXPR_NAMED, TK_TEXPR_SLICE, TK_TEXPR_UNION, TK_TEXPR_OPTIONAL } tag;
    union {
        struct { tk_path path; tk_type_expr *args; size_t args_len; } named;    // NamedType; args = generic type-args (S4 — args_len 0 for a plain name, e.g. Box<i64>)
        struct { tk_type_expr *element; }              slice;    // SliceType ([]T)
        struct { tk_type_expr *members; size_t len; }  uni;      // UnionType (A | B | …)  ('union' is reserved)
        struct { tk_type_expr *inner; }                optional; // OptionalType (T?) — REBOOT_PLAN §202; checker maps to TK_TYPE_OPTIONAL
    } as;
};

#endif // TK_PARSER_TYPE_H
