// src/checker/type.c — nominal type equality (B.13).
#include "type.h"
// tk_str_eq is declared in text.h (included via type.h) and implemented in teko_rt.c.

static bool tk_types_eq(const tk_type *xs, size_t nx, const tk_type *ys, size_t ny) {
    if (nx != ny) return false;
    for (size_t i = 0; i < nx; i += 1) {
        if (!tk_type_eq(&xs[i], &ys[i])) return false;
    }
    return true;
}

bool tk_type_eq(const tk_type *a, const tk_type *b) {
    if (a->tag != b->tag) return false;          // different shapes → not equal
    switch (a->tag) {
        case TK_TYPE_PRIM:  return a->as.prim == b->as.prim;
        case TK_TYPE_BYTE:  return true;
        case TK_TYPE_STR:   return true;
        case TK_TYPE_ERROR: return true;   // the native `error` (lowercase in Teko)
        case TK_TYPE_VOID:  return true;   // return-only marker (M.3); legal only as Func.ret
        // []T == []U iff their elements are equal (built-in unary former). A `teko::list::empty()`
        // carries the SENTINEL slice (element == NULL): it unifies with ANY concrete slice, so a
        // bare `empty()` flows into a typed slot / a `push` whose item fixes the element type. Two
        // sentinels are equal; sentinel vs concrete is equal (the concrete wins at the destination).
        case TK_TYPE_SLICE:
            if (a->as.slice.element == NULL || b->as.slice.element == NULL) return true;
            return tk_type_eq(a->as.slice.element, b->as.slice.element);
        // T? == U? iff their inners are equal (built-in unary former, like Slice). A bare
        // `null` literal carries the SENTINEL optional (inner == NULL — REBOOT_PLAN §202): it
        // unifies with ANY concrete optional, so `if`/`match` arms and binding/return slots
        // accept it. Two sentinels are equal; sentinel vs concrete is equal (the concrete wins
        // the resolved type at the destination via assignable_to / emit_as).
        case TK_TYPE_OPTIONAL:
            if (a->as.optional.inner == NULL || b->as.optional.inner == NULL) return true;
            return tk_type_eq(a->as.optional.inner, b->as.optional.inner);
        case TK_TYPE_NAMED: return tk_str_eq(a->as.named.name, b->as.named.name);   // nominal
        case TK_TYPE_VARIANT:
            return tk_types_eq(a->as.variant.members, a->as.variant.len,
                               b->as.variant.members, b->as.variant.len);
        case TK_TYPE_FUNC:
            return tk_type_eq(a->as.func.ret, b->as.func.ret) &&
                   tk_types_eq(a->as.func.params, a->as.func.nparams,
                               b->as.func.params, b->as.func.nparams);
        case TK_TYPE_PTR:   // ptr<T> equal by pointed type; NULL inner = opaque ptr (≡ ptr<void>)
            if (a->as.ptr.inner == NULL || b->as.ptr.inner == NULL) return a->as.ptr.inner == b->as.ptr.inner;
            return tk_type_eq(a->as.ptr.inner, b->as.ptr.inner);
        case TK_TYPE_UPTR:  return true;   // (C7.1a) opaque, same-kind only
    }
    return false;
}
