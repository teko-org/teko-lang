// src/checker/type.h — the checker's semantic Type model. Mirrors type.tks.
#ifndef TK_CHECK_TYPE_H
#define TK_CHECK_TYPE_H

#include "../core.h"
#include "../text/text.h"   // tk_str
#include <stdbool.h>
#include <stddef.h>

// Native numeric set, Tier 1 (B.38 / TEKO_CORRECTION_PLAN §5): unsigned/signed
// integers now include the 128-bit widths, and the three IEEE floats (F16/F32/F64)
// are native (no longer deferred). `dec`/`bigint` stay deferred (runtime-backed).
// Placement: new prims are APPENDED within their family (U128 after U64, I128 after
// I64, floats as a contiguous block) and BOOL stays LAST — perturbing no existing
// ordinal. The .tkb prim serialization (emit) is updated in the cascade, not here.
typedef enum {
    TK_PRIM_U8, TK_PRIM_U16, TK_PRIM_U32, TK_PRIM_U64, TK_PRIM_U128,
    TK_PRIM_I8, TK_PRIM_I16, TK_PRIM_I32, TK_PRIM_I64, TK_PRIM_I128,
    TK_PRIM_F16, TK_PRIM_F32, TK_PRIM_F64,
    TK_PRIM_BOOL,
} tk_prim_kind;

// --- PrimKind predicates / helpers (B.38) — mirror type.tks -----------------
// The cascade (checker / codegen / VM) depends on these.

// true for the IEEE float prims (F16/F32/F64).
static inline bool tk_prim_is_float(tk_prim_kind k) {
    return k == TK_PRIM_F16 || k == TK_PRIM_F32 || k == TK_PRIM_F64;
}

// true for the integer prims (U8..U128, I8..I128). NOT bool, NOT float.
static inline bool tk_prim_is_int(tk_prim_kind k) {
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

// true for the SIGNED integer prims (I8..I128). Floats are signed by nature, but
// this predicate is about INTEGER signedness — call it only for ints (it returns
// false for floats and bool, which carry no integer-signedness concept).
static inline bool tk_prim_is_signed(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8:  case TK_PRIM_I16: case TK_PRIM_I32:
        case TK_PRIM_I64: case TK_PRIM_I128:
            return true;
        default:
            return false;
    }
}

// bit width of a prim: ints 8/16/32/64/128, floats 16/32/64, bool 1.
static inline unsigned tk_prim_width(tk_prim_kind k) {
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

// Doctrinal correction (TEKO_CORRECTION_PLAN §3 [Z1]; Laws M.0/M.1/M.3):
//  - Unit EXCISED — "returns no value" is `void`, not a value-less type.
//  - VOID added as a return-only marker (legal ONLY as Func.ret; never a value,
//    a binding type, or a variant member — enforced in the checker, Z2a).
//  - ERROR is the native `error` (lowercase in Teko source); the C enum constant
//    name TK_TYPE_ERROR is mere scaffolding. Supersedes the capitalized `Error`.
//  - OPTIONAL added: the built-in unary type-former `T?` (nullability complete in
//    the seed, Z3=a). A variant member may NOT be optional — enforced in Z2a.
typedef enum {
    TK_TYPE_PRIM, TK_TYPE_BYTE, TK_TYPE_STR, TK_TYPE_SLICE,
    TK_TYPE_NAMED, TK_TYPE_VARIANT, TK_TYPE_FUNC, TK_TYPE_ERROR,
    TK_TYPE_OPTIONAL,  // T? — built-in nullable former (like Slice []T)
    TK_TYPE_VOID,      // return-only marker; legal ONLY as Func.ret
    TK_TYPE_PTR,       // (C7.1a) opaque pointer — transport-only at the FFI boundary, never dereferenced
    TK_TYPE_UPTR,      // (C7.1a) opaque word-size unsigned — transport-only at the FFI boundary
} tk_type_tag;

// recursive (the Slice/Variant/Func cases hold tk_type) — the indirection the Teko
// side keeps compiler-managed shows here as a forward declaration + pointers.
typedef struct tk_type tk_type;

struct tk_type {
    tk_type_tag tag;
    union {
        tk_prim_kind prim;                                   // TK_TYPE_PRIM
        struct { tk_type *element; }            slice;       // TK_TYPE_SLICE
        struct { tk_str name; }                 named;       // TK_TYPE_NAMED (nominal)
        struct { tk_type *members; size_t len; } variant;    // TK_TYPE_VARIANT
        struct { tk_type *params; size_t nparams; tk_type *ret; } func;  // TK_TYPE_FUNC
        struct { tk_type *inner; }              optional;    // TK_TYPE_OPTIONAL — T?
        struct { tk_type *inner; }              ptr;         // TK_TYPE_PTR — ptr<T> (inner NULL = opaque ptr ≡ ptr<void> ≡ *void)
        // BYTE, STR, ERROR, VOID carry no payload
    } as;
};

bool tk_type_eq(const tk_type *a, const tk_type *b);

// `void` is the return-only marker (M.3): a `Func.ret` may be void, but void may
// never be a value, a binding type, or a variant member (enforced in the checker).
// This predicate is what the checker (Z2a) calls to recognize/reject void.
static inline bool tk_type_is_void(const tk_type *t) {
    return t != NULL && t->tag == TK_TYPE_VOID;
}

#endif // TK_CHECK_TYPE_H
