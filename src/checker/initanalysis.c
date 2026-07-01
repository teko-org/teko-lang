// src/checker/initanalysis.c — Phase 5: definite-assignment / init analysis.
// C-bootstrap twin of initanalysis.tks. See the header for the law-first rationale (Teko has
// no uninitialized binding form → use-before-init is structural). This pass reports:
//   * unused LOCAL  -> ERROR (returned as a located tk_error)
//   * unused PRIVATE function -> WARNING (printed to stderr — the warnings channel)
// "Used" is over-approximated by a NAME occurrence (a var read, a call's last segment, or a
// COMPOUND-assign target), so shadowing only ever MISSES an unused (a false negative) and never
// flags a live binding — which keeps the self-host gate green.

#include "initanalysis.h"
#include "check_modules.h"   // tk_diag_at — the shared "file:line:col: msg" formatter (W-loc-2)
#include <stdio.h>    // fprintf (the warnings channel → stderr) + snprintf
#include <string.h>   // memcmp

extern void *tk_alloc(size_t);   // bootstrap arena (whole-compile lifetime), like tk_diag_at's

static bool seq(tk_str a, tk_str b) {
    return a.len == b.len && (a.len == 0 || memcmp(a.ptr, b.ptr, a.len) == 0);
}
static bool seq_cstr(tk_str a, const char *b) {
    size_t n = strlen(b);
    return a.len == n && (n == 0 || memcmp(a.ptr, b, n) == 0);
}

// ---- occurrence predicates (mirror initanalysis.tks occurs_*) ----
static bool occurs_block(const tk_tstatement *stmts, size_t n, tk_str name);

static bool occurs_expr(const tk_texpr *e, tk_str name) {
    if (!e) return false;
    switch (e->tag) {
        case TK_TEXPR_VAR:     return seq(e->as.var.name, name);
        case TK_TEXPR_BINARY:  return occurs_expr(e->as.binary.left, name) || occurs_expr(e->as.binary.right, name);
        case TK_TEXPR_UNARY:   return occurs_expr(e->as.unary.operand, name);
        case TK_TEXPR_COMPARE: {
            if (occurs_expr(e->as.compare.first, name)) return true;
            for (size_t i = 0; i < e->as.compare.nrest; i += 1)
                if (occurs_expr(e->as.compare.rest[i].operand, name)) return true;
            return false;
        }
        case TK_TEXPR_CALL: {
            tk_path p = e->as.call.callee;
            if (p.len > 0 && seq(p.segments[p.len - 1].name, name)) return true;
            for (size_t i = 0; i < e->as.call.nargs; i += 1)
                if (occurs_expr(&e->as.call.args[i], name)) return true;
            return false;
        }
        case TK_TEXPR_IF: {
            if (occurs_expr(e->as.if_expr.cond, name)) return true;
            if (occurs_block(e->as.if_expr.then_blk, e->as.if_expr.nthen, name)) return true;
            if (e->as.if_expr.has_else && occurs_block(e->as.if_expr.else_blk, e->as.if_expr.nelse, name)) return true;
            return false;
        }
        case TK_TEXPR_MATCH: {
            if (occurs_expr(e->as.match_expr.subject, name)) return true;
            for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
                tk_tarm a = e->as.match_expr.arms[i];
                if (a.has_when && occurs_expr(a.guard, name)) return true;
                if (occurs_block(a.body, a.nbody, name)) return true;
            }
            return false;
        }
        case TK_TEXPR_CAST:              return occurs_expr(e->as.cast.expr, name);
        case TK_TEXPR_FIELD_ACCESS:      return occurs_expr(e->as.field_access.receiver, name);
        case TK_TEXPR_SAFE_FIELD_ACCESS: return occurs_expr(e->as.safe_field_access.receiver, name);
        case TK_TEXPR_COALESCE:          return occurs_expr(e->as.coalesce.left, name) || occurs_expr(e->as.coalesce.right, name);
        case TK_TEXPR_STRUCT_INIT: {
            for (size_t i = 0; i < e->as.struct_init.nfields; i += 1)
                if (occurs_expr(&e->as.struct_init.field_vals[i], name)) return true;
            return false;
        }
        case TK_TEXPR_INDEX:  return occurs_expr(e->as.index.receiver, name) || occurs_expr(e->as.index.index, name);
        case TK_TEXPR_INTERP: {
            for (size_t i = 0; i < e->as.interp.nholes; i += 1) {
                if (occurs_expr(&e->as.interp.holes[i], name)) return true;
                if (e->as.interp.specs && e->as.interp.specs[i].kind == TK_FSPEC_DYNAMIC)
                    for (size_t k = 0; k < e->as.interp.specs[i].ndyn_args; k++)
                        if (occurs_expr(&e->as.interp.specs[i].dyn_args[k], name)) return true;
            }
            return false;
        }
        case TK_TEXPR_IN: {
            if (occurs_expr(e->as.in_expr.lhs, name)) return true;
            for (size_t i = 0; i < e->as.in_expr.nelems; i += 1)
                if (occurs_expr(&e->as.in_expr.elems[i], name)) return true;
            return false;
        }
        case TK_TEXPR_LAMBDA:   // (W10) a closure READS `name` if it captures it
            for (size_t i = 0; i < e->as.lambda.ncaptures; i += 1)
                if (tk_str_eq(e->as.lambda.captures[i].name, name)) return true;
            return false;
        // (2026-07-01) a fixed pre-existing gap: TK_TEXPR_ARRAY fell through to `default` (false
        // negative) — a name used ONLY as an array-literal element (explicit `[a, b]` or the
        // params call-site desugar's synthetic packed slice — see expr.c pack_variadic_args) was
        // silently treated as unread. Recurse into every element.
        case TK_TEXPR_ARRAY:
            for (size_t i = 0; i < e->as.array.nelements; i += 1)
                if (occurs_expr(&e->as.array.elements[i], name)) return true;
            return false;
        default: return false;   // NUMBER/STR/BYTE/BOOL/NULL/PATH — no reads
    }
}

static bool occurs_stmt(const tk_tstatement *s, tk_str name) {
    if (!s) return false;
    switch (s->tag) {
        case TK_TSTMT_BINDING: return occurs_expr(&s->as.binding.value, name);
        case TK_TSTMT_ASSIGN:
            // a COMPOUND assign (`x += …`) READS the target; a plain `=` does not. A deref-assign
            // (`r.value = …`, MEM-1b-ii) always READS the ref handle `r` (to dereference it).
            if (s->as.assign.op != TK_TOKEN_ASSIGN && seq(s->as.assign.name, name)) return true;
            if (s->as.assign.deref && seq(s->as.assign.name, name)) return true;
            return occurs_expr(&s->as.assign.value, name);
        case TK_TSTMT_RETURN:  return s->as.ret.has_value && occurs_expr(&s->as.ret.value, name);
        case TK_TSTMT_LOOP:    return occurs_block(s->as.loop_stmt.body, s->as.loop_stmt.nbody, name);
        case TK_TSTMT_EXPR:    return occurs_expr(&s->as.expr_stmt.expr, name);
        case TK_TSTMT_DEFER:   return occurs_block(s->as.defer_stmt.body, s->as.defer_stmt.nbody, name);   // (W9.3) a defer body READS its captured locals
        default: return false;   // BREAK/CONTINUE — no reads
    }
}

static bool occurs_block(const tk_tstatement *stmts, size_t n, tk_str name) {
    for (size_t i = 0; i < n; i += 1) if (occurs_stmt(&stmts[i], name)) return true;
    return false;
}

// ---- (C5.1) unused-local ERROR ----
static tk_error check_locals(const tk_tstatement *stmts, size_t n,
                             const tk_tstatement *fbody, size_t nbody, tk_str file);

static tk_error ok(void) { return tk_error_make(NULL); }

static tk_error unused_local_error(tk_str name, uint32_t line, uint32_t col, tk_str file) {
    const char *pre = "unused local `";
    const char *suf = "` — it is declared but never read";
    size_t cap = strlen(pre) + name.len + strlen(suf) + 1;
    char *buf = (char *)tk_alloc(cap); if (!buf) return tk_error_make("unused local");
    snprintf(buf, cap, "%s%.*s%s", pre, (int)name.len, (const char *)name.ptr, suf);
    // bake file:line:col into the MESSAGE (the renderer fallback) AND set the structured fields
    tk_error e = tk_error_make(tk_diag_at(file, line, col, buf));
    return tk_error_at(e, file.ptr ? (const char *)file.ptr : NULL, line, col);
}

static tk_error check_locals_expr(const tk_texpr *e, const tk_tstatement *fbody, size_t nbody, tk_str file) {
    if (!e) return ok();
    if (e->tag == TK_TEXPR_IF) {
        tk_error r = check_locals(e->as.if_expr.then_blk, e->as.if_expr.nthen, fbody, nbody, file);
        if (r.message) return r;
        if (e->as.if_expr.has_else) return check_locals(e->as.if_expr.else_blk, e->as.if_expr.nelse, fbody, nbody, file);
        return ok();
    }
    if (e->tag == TK_TEXPR_MATCH) {
        for (size_t i = 0; i < e->as.match_expr.narms; i += 1) {
            tk_tarm a = e->as.match_expr.arms[i];
            tk_error r = check_locals(a.body, a.nbody, fbody, nbody, file);
            if (r.message) return r;
        }
    }
    return ok();   // locals buried inside an operand are rare → a missed unused (safe)
}

static bool is_underscore(tk_str s) { return s.len == 1 && s.ptr && s.ptr[0] == (tk_byte)'_'; }

static tk_error check_local_stmt(const tk_tstatement *s, const tk_tstatement *fbody, size_t nbody, tk_str file) {
    switch (s->tag) {
        case TK_TSTMT_BINDING: {
            if (s->as.binding.target.tag == TK_BIND_SIMPLE) {
                tk_str nm = s->as.binding.target.as.simple.name;
                // `_` is the explicit DISCARD binding — exempt it.
                if (!is_underscore(nm) && !occurs_block(fbody, nbody, nm))
                    return unused_local_error(nm, s->as.binding.value.line, s->as.binding.value.col, file);
            }
            return check_locals_expr(&s->as.binding.value, fbody, nbody, file);
        }
        case TK_TSTMT_LOOP:   return check_locals(s->as.loop_stmt.body, s->as.loop_stmt.nbody, fbody, nbody, file);
        case TK_TSTMT_EXPR:   return check_locals_expr(&s->as.expr_stmt.expr, fbody, nbody, file);
        case TK_TSTMT_RETURN: return s->as.ret.has_value ? check_locals_expr(&s->as.ret.value, fbody, nbody, file) : ok();
        case TK_TSTMT_ASSIGN: return check_locals_expr(&s->as.assign.value, fbody, nbody, file);
        // (W9.3) A defer BODY is not descended for unused-local errors (a defer's own bindings are
        // cleanup scratch, often intentionally write-only) — the `occurs_stmt` defer case already
        // counts a defer body's READS of OUTER locals as uses, which is what scope-defer requires.
        default: return ok();
    }
}

static tk_error check_locals(const tk_tstatement *stmts, size_t n,
                             const tk_tstatement *fbody, size_t nbody, tk_str file) {
    for (size_t i = 0; i < n; i += 1) {
        tk_error r = check_local_stmt(&stmts[i], fbody, nbody, file);
        if (r.message) return r;
    }
    return ok();
}

// ---- (C5.2) unused-private-function WARNING ----
static bool used_in_program(tk_tprogram prog, tk_str name) {
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag == TK_TITEM_FUNCTION) { if (occurs_block(it.as.function.body, it.as.function.nbody, name)) return true; }
        else if (it.tag == TK_TITEM_STATEMENT) { if (occurs_stmt(&it.as.statement, name)) return true; }
    }
    return false;
}

static void warn_unused_fn(tk_tfunction f) {
    fprintf(stderr, "%.*s:%u: warning: unused private function `%.*s`\n",
            (int)f.file.len, (const char *)f.file.ptr, f.line,
            (int)f.name.len, (const char *)f.name.ptr);
}

tk_error tk_analyze_program(tk_tprogram prog) {
    // collect the loose top-level statements (the virtual-main) into one scope for local checking
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag == TK_TITEM_FUNCTION) {
            tk_tfunction f = it.as.function;
            tk_error r = check_locals(f.body, f.nbody, f.body, f.nbody, f.file);
            if (r.message) return r;
            // `main` and `#test` functions are never source-called by design — exempt both.
            if (f.vis == TK_VIS_PRIVATE && !seq_cstr(f.name, "main") && !f.is_test && !used_in_program(prog, f.name))
                warn_unused_fn(f);
        }
    }
    // the virtual-main loose statements form one shared scope (initanalysis.tks loose_body)
    size_t nloose = 0;
    for (size_t i = 0; i < prog.nitems; i += 1) if (prog.items[i].tag == TK_TITEM_STATEMENT) nloose += 1;
    if (nloose > 0) {
        tk_tstatement *loose = (tk_tstatement *)tk_alloc(nloose * sizeof(tk_tstatement));
        if (!loose) return ok();
        size_t k = 0;
        for (size_t i = 0; i < prog.nitems; i += 1)
            if (prog.items[i].tag == TK_TITEM_STATEMENT) loose[k++] = prog.items[i].as.statement;
        tk_str nofile = { .ptr = NULL, .len = 0 };
        tk_error r = check_locals(loose, nloose, loose, nloose, nofile);
        if (r.message) return r;
    }
    return ok();
}
