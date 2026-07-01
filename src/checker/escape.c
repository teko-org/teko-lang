// src/checker/escape.c — THE ESCAPE CHECK (MEM Step 1). C-bootstrap twin of escape.tks.
// SOUNDNESS IS THE ABSOLUTE RULE: never under-classify an escaping allocation as frame-local.
#include "escape.h"
#include "../core.h"     // tk_alloc, tk_str
#include <string.h>      // memcmp

// --- a tiny owned string set (arena-allocated; deduped; leak-tolerant M.5) ---
static bool esc_name_eq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static bool esc_set_has(tk_escape_set s, tk_str name) {
    for (size_t i = 0; i < s.n; i += 1) if (esc_name_eq(s.names[i], name)) return true;
    return false;
}
// add `name` if absent — grow-by-copy into a fresh arena block (the set is tiny: a fn's locals).
static tk_escape_set esc_set_add(tk_escape_set s, tk_str name) {
    if (esc_set_has(s, name)) return s;
    tk_str *grown = (tk_str *)tk_alloc((s.n + 1) * sizeof *grown);
    for (size_t i = 0; i < s.n; i += 1) grown[i] = s.names[i];
    grown[s.n] = name;
    s.names = grown; s.n = s.n + 1;
    return s;
}

// type_is_scalar — a Prim / byte. Reading a field of this type yields a pure value COPY that
// embeds NO interior heap pointer, so the receiver does not escape through it. (str is a VIEW
// {ptr,len} that may alias — deliberately NOT scalar here, to stay conservative.)
static bool type_is_scalar(tk_type t) {
    return t.tag == TK_TYPE_PRIM || t.tag == TK_TYPE_BYTE;
}

// expr_is_bare_var — is `e` a DIRECT variable read (a `TVar`), with NO field-access / index /
// call / cast / etc. wrapping it? The scalar-read non-escaping refinement is sound ONLY for a
// scalar field of a BARE local: such a field is a STACK member of the named-struct value (boxed /
// recursive back-edge fields are NEVER scalar), so it survives the frame-region drop and may be
// read post-drop. For ANY non-bare receiver (a nested `a.b`, an index `a[i]`, a call result, …)
// the receiver lives in the SAME (escaping) context — reading a scalar OFF it still derefs an
// interior heap hop (e.g. `a.b.y`: `.y` is scalar but `a.b` is a boxed frame pointer), so the
// refinement must NOT clear escaping there (MEM Step 1 UAF fix #2 — security).
static bool expr_is_bare_var(tk_texpr e) {
    return e.tag == TK_TEXPR_VAR;
}

static void mark_block(const tk_tstatement *body, size_t n, bool tail, tk_escape_set *acc);
static void mark_stmt(tk_tstatement s, bool tail, tk_escape_set *acc);

// mark_expr — walk `e`, adding every variable read in an ESCAPING position to `*acc` IN PLACE.
// `acc` is the `Ref<[]str>` twin: a `tk_escape_set *` whose whole value is replaced on each add
// (`*acc = esc_set_add(*acc, name)` — grow-by-copy), so the grown set is visible to every caller
// through the one shared cell — no thread-and-return. `escaping` is the context: true ⇒ a read
// here lets the variable outlive the frame. The ref is FORWARDED straight through every recursion.
static void mark_expr(tk_texpr e, bool escaping, tk_escape_set *acc) {
    switch (e.tag) {
        case TK_TEXPR_VAR:
            if (escaping) *acc = esc_set_add(*acc, e.as.var.name);
            return;

        case TK_TEXPR_NUMBER:
        case TK_TEXPR_STR:
        case TK_TEXPR_BYTE:
        case TK_TEXPR_CHAR:
        case TK_TEXPR_BOOL:
        case TK_TEXPR_NULL:
        case TK_TEXPR_PATH:
            return;

        case TK_TEXPR_BINARY:
            mark_expr(*e.as.binary.left, escaping, acc);
            mark_expr(*e.as.binary.right, escaping, acc);
            return;
        case TK_TEXPR_UNARY:
            mark_expr(*e.as.unary.operand, escaping, acc);
            return;
        case TK_TEXPR_COMPARE: {
            // a comparison RESULT is a bool (a pure copy), but the operands are EVALUATED in the
            // outer context: when this comparison is the tail value (escaping=true) the operands run
            // AFTER the frame-region drop, so a `a.b`-style operand dereferences a frame cell
            // post-drop → propagate `escaping` (UAF audit). The bare-TVar scalar refinement still
            // applies one level down (e.g. `a.val < b.val` keeps `a`/`b` frame-local).
            mark_expr(*e.as.compare.first, escaping, acc);
            for (size_t i = 0; i < e.as.compare.nrest; i += 1)
                mark_expr(*e.as.compare.rest[i].operand, escaping, acc);
            return;
        }
        case TK_TEXPR_CALL: {
            // a call: every argument may be RETAINED by the callee → escaping (one-depth).
            for (size_t i = 0; i < e.as.call.nargs; i += 1)
                mark_expr(e.as.call.args[i], true, acc);
            return;
        }
        case TK_TEXPR_IF: {
            // The branch bodies are tail blocks whose trailing value IS this if's value: when this
            // if is itself in an escaping position (escaping=true) — e.g. it is the function-body
            // tail that codegen drop-then-returns — each branch's trailing value is returned and
            // therefore escaping (MEM Step 1 UAF fix #1). The COND is a bool test, but when the if
            // is a SUB-expression of a tail value it is EVALUATED post-drop too (the whole tail
            // value is emitted after the region drop), so a `a.b`-style cond would deref a frame
            // cell → propagate `escaping` (UAF audit; safe over-marking).
            mark_expr(*e.as.if_expr.cond, escaping, acc);
            mark_block(e.as.if_expr.then_blk, e.as.if_expr.nthen, escaping, acc);
            if (e.as.if_expr.has_else)
                mark_block(e.as.if_expr.else_blk, e.as.if_expr.nelse, escaping, acc);
            return;
        }
        case TK_TEXPR_MATCH: {
            mark_expr(*e.as.match_expr.subject, escaping, acc);
            for (size_t i = 0; i < e.as.match_expr.narms; i += 1) {
                // the guard is likewise post-drop in a tail position → propagate escaping (UAF audit).
                if (e.as.match_expr.arms[i].has_when)
                    mark_expr(*e.as.match_expr.arms[i].guard, escaping, acc);
                // arm tail value is this match's value → escaping iff the match escapes (tail = escaping).
                mark_block(e.as.match_expr.arms[i].body, e.as.match_expr.arms[i].nbody, escaping, acc);
            }
            return;
        }
        case TK_TEXPR_CAST:
            mark_expr(*e.as.cast.expr, escaping, acc);
            return;

        case TK_TEXPR_FIELD_ACCESS: {
            // the soundness-critical refinement: a SCALAR field read off a BARE local (`x.f`, x a
            // TVar) is a pure copy of a STACK member (recursive/boxed fields are never scalar), so
            // the receiver does NOT escape through it. For ANY non-bare receiver (`a.b.y`, `a[i].y`,
            // a call result, …) the receiver is in the SAME (escaping) context — a scalar read OFF
            // it still derefs an interior heap hop (`a.b` is a boxed frame pointer) → propagate
            // escaping (UAF fix #2). Otherwise (non-scalar, or non-bare receiver) inherit context.
            bool refine = type_is_scalar(e.type) && expr_is_bare_var(*e.as.field_access.receiver);
            bool recv_ctx = refine ? false : escaping;
            mark_expr(*e.as.field_access.receiver, recv_ctx, acc);
            return;
        }
        case TK_TEXPR_SAFE_FIELD_ACCESS: {
            bool refine = type_is_scalar(e.type) && expr_is_bare_var(*e.as.safe_field_access.receiver);
            bool recv_ctx = refine ? false : escaping;
            mark_expr(*e.as.safe_field_access.receiver, recv_ctx, acc);
            return;
        }
        case TK_TEXPR_COALESCE:
            mark_expr(*e.as.coalesce.left, escaping, acc);
            mark_expr(*e.as.coalesce.right, escaping, acc);
            return;

        case TK_TEXPR_INDEX: {
            // a slice/array is ALWAYS heap-backed, so an index read DEREFERENCES a heap pointer.
            // There is NO bare-local stack-member analogue (elements live behind the slice's `.ptr`),
            // so in an escaping context the receiver is ESCAPING — no scalar refinement for an index
            // (UAF fix #2). The index sub-expression is also EVALUATED in the outer context (post-drop
            // when escaping=true), so a `a.b`-style index derefs a frame cell → it too propagates
            // `escaping`; the bare-TVar scalar refinement still keeps `a[x.i]` ⇒ `x` frame-local.
            mark_expr(*e.as.index.receiver, escaping, acc);
            mark_expr(*e.as.index.index, escaping, acc);
            return;
        }

        case TK_TEXPR_STRUCT_INIT: {
            // every component is STORED into a value that takes the outer context.
            for (size_t i = 0; i < e.as.struct_init.nfields; i += 1)
                mark_expr(e.as.struct_init.field_vals[i], escaping, acc);
            return;
        }
        case TK_TEXPR_ARRAY: {
            for (size_t i = 0; i < e.as.array.nelements; i += 1)
                mark_expr(e.as.array.elements[i], escaping, acc);
            return;
        }
        case TK_TEXPR_INTERP: {
            for (size_t i = 0; i < e.as.interp.nholes; i += 1) {
                mark_expr(e.as.interp.holes[i], escaping, acc);
                if (e.as.interp.specs && e.as.interp.specs[i].kind == TK_FSPEC_DYNAMIC)
                    for (size_t k = 0; k < e.as.interp.specs[i].ndyn_args; k++)
                        mark_expr(e.as.interp.specs[i].dyn_args[k], escaping, acc);
            }
            return;
        }
        case TK_TEXPR_IN: {
            // `x in [ … ]` yields a bool, but lhs/elems are EVALUATED (and compared) in the outer
            // context: when the `in` is the tail value (escaping=true) they run AFTER the frame-region
            // drop, so a `a.b`-style operand derefs a frame cell post-drop → propagate `escaping`
            // (UAF audit). The bare-TVar scalar refinement still applies one level down.
            mark_expr(*e.as.in_expr.lhs, escaping, acc);
            for (size_t i = 0; i < e.as.in_expr.nelems; i += 1)
                mark_expr(e.as.in_expr.elems[i], escaping, acc);
            return;
        }
        case TK_TEXPR_LAMBDA:   // (W10) a closure's captures escape with it (sound over-approximation)
            if (escaping) for (size_t i = 0; i < e.as.lambda.ncaptures; i += 1) *acc = esc_set_add(*acc, e.as.lambda.captures[i].name);
            return;
        default: return;
    }
    return;   // unreachable (all tags handled) — fail-soft to the safe (no-mark) result
}

// mark_block — walk a statement block, mutating the escaping-var accumulator IN PLACE. `tail` is
// TRUE when this block's TRAILING value is RETURNED (the function/branch tail) — codegen drops the
// frame region BEFORE that value and `return`s it (emit_block_tail/emit_exprstmt_tail), so the
// last statement, when it is a value-bearing expr-statement, must be analysed in an ESCAPING
// context (MEM Step 1 UAF fix). All NON-last statements are interior (tail=false).
static void mark_block(const tk_tstatement *body, size_t n, bool tail, tk_escape_set *acc) {
    for (size_t i = 0; i < n; i += 1) {
        bool is_last = (i + 1 == n);
        mark_stmt(body[i], tail && is_last, acc);
    }
}

// mark_stmt — walk one statement, establishing the escaping context per position. `tail` is TRUE
// only for the block's LAST statement when that block's trailing value is returned: an
// expr-statement there is an IMPLICIT RETURN (B.20/W5a) whose value codegen produces AFTER
// dropping the frame region, so it must be ESCAPING (not discarded).
static void mark_stmt(tk_tstatement s, bool tail, tk_escape_set *acc) {
    switch (s.tag) {
        case TK_TSTMT_BINDING:
            mark_expr(s.as.binding.value, true, acc);   // the bound value lives on
            return;
        case TK_TSTMT_ASSIGN:
            mark_expr(s.as.assign.value, true, acc);    // may be written to an outer location
            return;
        case TK_TSTMT_RETURN:
            if (s.as.ret.has_value) mark_expr(s.as.ret.value, true, acc);
            return;
        case TK_TSTMT_EXPR:
            // DISCARDED (NON-escaping) UNLESS in TAIL position, where it is an implicit RETURN whose
            // value escapes (codegen drop-then-returns it). Nested calls/returns still escape via
            // their own positions.
            mark_expr(s.as.expr_stmt.expr, tail, acc);
            return;
        case TK_TSTMT_LOOP:
            // a loop body's trailing expr is NOT a return (a loop exits via break) → NON-tail.
            mark_block(s.as.loop_stmt.body, s.as.loop_stmt.nbody, false, acc);
            return;
        case TK_TSTMT_BREAK:
        case TK_TSTMT_CONTINUE:
            return;
        case TK_TSTMT_DEFER:
            // a defer body's trailing expr is run for effect, NOT a return → NON-tail.
            mark_block(s.as.defer_stmt.body, s.as.defer_stmt.nbody, false, acc);
            return;
    }
}

tk_escape_set tk_fn_escaping_vars(tk_tfunction f) {
    tk_escape_set acc = { .names = NULL, .n = 0 };
    // The body is analysed in TAIL position: its trailing value-bearing expr-statement is an
    // IMPLICIT RETURN (escaping). `acc` is mutated in place through the shared `&acc` ref.
    mark_block(f.body, f.nbody, true, &acc);
    return acc;
}

bool tk_escape_set_has(tk_escape_set set, tk_str name) {
    return esc_set_has(set, name);
}

bool tk_binding_is_frame_local(tk_escape_set set, tk_tstatement binding) {
    if (binding.tag != TK_TSTMT_BINDING) return false;
    if (binding.as.binding.target.tag != TK_BIND_SIMPLE) return false;   // destructure → not frame-local
    tk_str name = binding.as.binding.target.as.simple.name;
    if (name.len == 1 && name.ptr[0] == '_') return false;               // discard → no C var
    if (binding.as.binding.kind == TK_BIND_CONST) return false;
    if (esc_set_has(set, name)) return false;
    return binding.as.binding.value.tag == TK_TEXPR_STRUCT_INIT;
}

// ── S2 — PER-BLOCK escape (block-local freeing). C twin of escape.tks. ──────────
// A binding `x` declared at the top level of block B may be freed at B's every exit edge ONLY IF
// every textual READ of `x` in the whole function is a SAFE non-tail/non-assign read strictly
// inside B. Counting proves it: total reads in the fn == safe reads inside B. Any read outside B,
// in B's tail value, or as an assignment RHS ⇒ block-ESCAPING ⇒ route to the enclosing region
// (the W8/leak-safe default). SOUNDNESS IS ABSOLUTE — a leak is safe, a use-after-free is not.
static bool name_eq_str(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}

static size_t count_reads_block(const tk_tstatement *body, size_t n, tk_str name);

// count_reads_expr — total bare-variable reads of `name` anywhere in `e` (every position).
static size_t count_reads_expr(tk_texpr e, tk_str name) {
    switch (e.tag) {
        case TK_TEXPR_VAR:
            return name_eq_str(e.as.var.name, name) ? 1 : 0;
        case TK_TEXPR_NUMBER:
        case TK_TEXPR_STR:
        case TK_TEXPR_BYTE:
        case TK_TEXPR_CHAR:
        case TK_TEXPR_BOOL:
        case TK_TEXPR_NULL:
        case TK_TEXPR_PATH:
            return 0;
        case TK_TEXPR_BINARY:
            return count_reads_expr(*e.as.binary.left, name) + count_reads_expr(*e.as.binary.right, name);
        case TK_TEXPR_UNARY:
            return count_reads_expr(*e.as.unary.operand, name);
        case TK_TEXPR_COMPARE: {
            size_t n = count_reads_expr(*e.as.compare.first, name);
            for (size_t i = 0; i < e.as.compare.nrest; i += 1)
                n += count_reads_expr(*e.as.compare.rest[i].operand, name);
            return n;
        }
        case TK_TEXPR_CALL: {
            size_t n = 0;
            for (size_t i = 0; i < e.as.call.nargs; i += 1)
                n += count_reads_expr(e.as.call.args[i], name);
            return n;
        }
        case TK_TEXPR_IF: {
            size_t n = count_reads_expr(*e.as.if_expr.cond, name);
            n += count_reads_block(e.as.if_expr.then_blk, e.as.if_expr.nthen, name);
            if (e.as.if_expr.has_else)
                n += count_reads_block(e.as.if_expr.else_blk, e.as.if_expr.nelse, name);
            return n;
        }
        case TK_TEXPR_MATCH: {
            size_t n = count_reads_expr(*e.as.match_expr.subject, name);
            for (size_t i = 0; i < e.as.match_expr.narms; i += 1) {
                if (e.as.match_expr.arms[i].has_when)
                    n += count_reads_expr(*e.as.match_expr.arms[i].guard, name);
                n += count_reads_block(e.as.match_expr.arms[i].body, e.as.match_expr.arms[i].nbody, name);
            }
            return n;
        }
        case TK_TEXPR_CAST:
            return count_reads_expr(*e.as.cast.expr, name);
        case TK_TEXPR_FIELD_ACCESS:
            return count_reads_expr(*e.as.field_access.receiver, name);
        case TK_TEXPR_SAFE_FIELD_ACCESS:
            return count_reads_expr(*e.as.safe_field_access.receiver, name);
        case TK_TEXPR_COALESCE:
            return count_reads_expr(*e.as.coalesce.left, name) + count_reads_expr(*e.as.coalesce.right, name);
        case TK_TEXPR_INDEX:
            return count_reads_expr(*e.as.index.receiver, name) + count_reads_expr(*e.as.index.index, name);
        case TK_TEXPR_STRUCT_INIT: {
            size_t n = 0;
            for (size_t i = 0; i < e.as.struct_init.nfields; i += 1)
                n += count_reads_expr(e.as.struct_init.field_vals[i], name);
            return n;
        }
        case TK_TEXPR_ARRAY: {
            size_t n = 0;
            for (size_t i = 0; i < e.as.array.nelements; i += 1)
                n += count_reads_expr(e.as.array.elements[i], name);
            return n;
        }
        case TK_TEXPR_INTERP: {
            size_t n = 0;
            for (size_t i = 0; i < e.as.interp.nholes; i += 1) {
                n += count_reads_expr(e.as.interp.holes[i], name);
                if (e.as.interp.specs && e.as.interp.specs[i].kind == TK_FSPEC_DYNAMIC)
                    for (size_t k = 0; k < e.as.interp.specs[i].ndyn_args; k++)
                        n += count_reads_expr(e.as.interp.specs[i].dyn_args[k], name);
            }
            return n;
        }
        case TK_TEXPR_IN: {
            size_t n = count_reads_expr(*e.as.in_expr.lhs, name);
            for (size_t i = 0; i < e.as.in_expr.nelems; i += 1)
                n += count_reads_expr(e.as.in_expr.elems[i], name);
            return n;
        }
        case TK_TEXPR_LAMBDA:   // (W10) a closure reads `name` once if it captures it (body → lifted fn)
            for (size_t i = 0; i < e.as.lambda.ncaptures; i += 1)
                if (tk_str_eq(e.as.lambda.captures[i].name, name)) return 1;
            return 0;
        default: return 0;
    }
    return 0;
}

// count_reads_stmt — total reads of `name` in one statement (all sub-positions).
static size_t count_reads_stmt(tk_tstatement s, tk_str name) {
    switch (s.tag) {
        case TK_TSTMT_BINDING:  return count_reads_expr(s.as.binding.value, name);
        case TK_TSTMT_ASSIGN:   return count_reads_expr(s.as.assign.value, name);
        case TK_TSTMT_RETURN:   return s.as.ret.has_value ? count_reads_expr(s.as.ret.value, name) : 0;
        case TK_TSTMT_EXPR:     return count_reads_expr(s.as.expr_stmt.expr, name);
        case TK_TSTMT_LOOP:     return count_reads_block(s.as.loop_stmt.body, s.as.loop_stmt.nbody, name);
        case TK_TSTMT_BREAK:
        case TK_TSTMT_CONTINUE: return 0;
        case TK_TSTMT_DEFER:    return count_reads_block(s.as.defer_stmt.body, s.as.defer_stmt.nbody, name);
    }
    return 0;
}

static size_t count_reads_block(const tk_tstatement *body, size_t n, tk_str name) {
    size_t total = 0;
    for (size_t i = 0; i < n; i += 1) total += count_reads_stmt(body[i], name);
    return total;
}

// count_block_local_reads — reads of `name` strictly inside B that are SAFE to free past: every
// read EXCEPT (a) B's TAIL value when B yields one (an arm/if value used outside B), and (b) any
// ASSIGN RHS (could write an enclosing var). A binding's own initializer never reads its own name.
static size_t count_block_local_reads(const tk_tstatement *body, size_t n, tk_str name, bool is_value) {
    size_t total = 0;
    for (size_t i = 0; i < n; i += 1) {
        bool is_last = (i + 1 == n);
        if (body[i].tag == TK_TSTMT_ASSIGN) {
            // escaping write target → do not count as a safe inside read.
        } else if (body[i].tag == TK_TSTMT_EXPR) {
            if (is_value && is_last) { /* tail value escapes B */ }
            else total += count_reads_expr(body[i].as.expr_stmt.expr, name);
        } else {
            total += count_reads_stmt(body[i], name);
        }
    }
    return total;
}

bool tk_binding_is_block_local(tk_escape_set set, const tk_tstatement *fn_body, size_t fn_n,
                               const tk_tstatement *block_body, size_t block_n, bool is_value,
                               tk_tstatement binding) {
    if (!tk_binding_is_frame_local(set, binding)) return false;
    if (binding.as.binding.target.tag != TK_BIND_SIMPLE) return false;
    tk_str name = binding.as.binding.target.as.simple.name;
    size_t total  = count_reads_block(fn_body, fn_n, name);
    size_t inside = count_block_local_reads(block_body, block_n, name, is_value);
    return total == inside;
}
