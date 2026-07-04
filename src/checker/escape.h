// src/checker/escape.h — THE ESCAPE CHECK (MEM Step 1; canonical twin = escape.tks).
//
// A conservative, ONE-DEPTH, SECURITY-FIRST analysis over the typed AST: for one function it
// computes the set of LOCAL variable names that ESCAPE the frame. The codegen reads this to
// decide whether a heap allocation a function makes may live in a per-FUNCTION frame region
// (bulk-freed at every exit edge) or must outlive the frame (root → leaked, which is SAFE).
//
// SOUNDNESS IS THE ABSOLUTE RULE (M.1/M.5): never classify an escaping allocation as
// frame-local. When not provably frame-local an allocation is treated as ESCAPING (a leak is
// safe; a use-after-free is a vulnerability). The analysis is therefore intentionally
// INCOMPLETE — it admits only a provable frame-local subset.
#ifndef TK_CHECK_ESCAPE_H
#define TK_CHECK_ESCAPE_H

#include "tast.h"

// The per-function escape set: a small owned vector of local-variable names that outlive the
// frame. `names` is arena-allocated (tk_alloc); the caller never frees it (M.5 leak-tolerant).
typedef struct { tk_str *names; size_t n; } tk_escape_set;

// tk_fn_escaping_vars — compute the escape set for one typed function. PARAMETERS are immutable
// inputs (B.21) and are never frame-allocated, so they get no entry.
tk_escape_set tk_fn_escaping_vars(tk_tfunction f);

// tk_escape_set_has — membership (the codegen asks "does this bound name escape?").
bool tk_escape_set_has(tk_escape_set set, tk_str name);

// tk_binding_is_frame_local — does this binding allocate into the frame region? True iff it is a
// `let`/`mut` to a SIMPLE name that is NOT in the escape set AND whose value is a struct-init
// (the only codegen allocation site that can be frame-routed — the auto-boxed recursive
// back-edge). A `const`, destructure, discard (`_`), escaping name, or non-struct-init value →
// NOT frame-local (allocates in root, the safe default).
bool tk_binding_is_frame_local(tk_escape_set set, tk_tstatement binding);

// (S2 Level-1) tk_binding_is_frame_local_slice — a non-escaping SLICE binding (its buffer never
// leaves the frame). Unlike the struct-init predicate the value need not be an allocation site — a
// slice accumulator is built by later `xs = push(xs, …)` self-appends, each frame-routed.
bool tk_binding_is_frame_local_slice(tk_escape_set set, tk_tstatement binding);

// (S2 Level-1) tk_assign_routes_to_frame — THE routing predicate: is this a self-append
// `xs = push(xs, …)` to a NON-escaping slice (its grown buffer should live in the frame region)?
bool tk_assign_routes_to_frame(tk_escape_set set, tk_tstatement s);

// (#148 S2 Level-2) tk_assign_frees_old — may this self-append's copy-grow PARK the old buffer for
// reuse? True iff the whole-fn LINEAR-CHAIN proof holds for the assigned name (born once from
// list::empty(), writes are self-append-only, and every non-final-statement read is a self-append
// base — so no alias can observe a parked intermediate buffer).
bool tk_assign_frees_old(const tk_tstatement *fn_body, size_t fn_n, tk_tstatement s);

// tk_binding_is_block_local — S2: may a binding declared at the TOP LEVEL of block B (whose
// enclosing function body is fn_body, and which yields a value iff is_value) be freed at B's exit
// edges? True iff tk_binding_is_frame_local holds AND every textual read of the name in the whole
// function is a SAFE non-tail/non-assign read strictly inside B. CONSERVATIVE — any read outside B,
// in B's tail value, or as an assignment RHS ⇒ false ⇒ route to the enclosing region (W8 default).
bool tk_binding_is_block_local(tk_escape_set set, const tk_tstatement *fn_body, size_t fn_n,
                               const tk_tstatement *block_body, size_t block_n, bool is_value,
                               tk_tstatement binding);

#endif // TK_CHECK_ESCAPE_H
