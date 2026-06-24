// src/codegen/codegen_c.c — F2 BACKEND implementation: tk_tprogram -> C source text.
//
// Lowering design (M0 = integer arithmetic + let + return -> running native binary):
//   * types  : tk_type -> C type text (U8..U64 -> uintN_t, I8..I64 -> intN_t,
//              Bool -> bool, byte -> uint8_t, Unit -> void). Everything else =
//              "not yet supported".
//   * exprs  : Number -> literal; Var -> identifier; Binary -> (L op R);
//              Unary -> (op X); Compare -> chained && of adjacent C comparisons;
//              Cast -> ((Ctype)(inner)); Call -> name(args…); FieldAccess ->
//              (recv.field). if/match AS A VALUE = not yet supported.
//   * stmts  : Binding -> Ctype x = v; Assign -> x op= v; Return -> early exit in
//              main / `return v;` in a fn; ExprStmt -> expr; Loop -> while(1){…};
//              Break/Continue -> break;/continue;. match-stmt = not yet supported.
//   * program: each tk_tfunction -> a C function; the loose top-level statements (the
//              VIRTUAL-MAIN) -> int main(void){ … return 0; } with early-return.
//
// COVERAGE RULE (M.3): unsupported nodes fail with a clear message; never emit broken
// C, never crash.
#include "codegen_c.h"

#include "../lexer/token.h"   // tk_token_kind operator kinds
#include "../parser/ast.h"    // tk_bind_kind, tk_bind_target, tk_path

#include <stdlib.h>           // malloc/realloc/free
#include <string.h>           // memcpy, strlen
#include <stdio.h>            // snprintf
#include <inttypes.h>         // PRId64

// =========================================================================
// A growable C-source buffer. Allocation failure PANICS (M.5).
// =========================================================================
typedef struct { char *ptr; size_t len; size_t cap; } cbuf;

static void cbuf_reserve(cbuf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;   // +1 for the NUL
    size_t ncap = b->cap == 0 ? 256 : b->cap;
    while (b->len + extra + 1 > ncap) ncap *= 2;
    char *np = realloc(b->ptr, ncap);
    if (np == NULL) abort();
    b->ptr = np;
    b->cap = ncap;
}

static void cb(cbuf *b, const char *s) {        // append a C string
    size_t n = strlen(s);
    cbuf_reserve(b, n);
    memcpy(b->ptr + b->len, s, n);
    b->len += n;
    b->ptr[b->len] = '\0';
}

static void cb_str(cbuf *b, tk_str s) {         // append a tk_str span (raw bytes)
    cbuf_reserve(b, s.len);
    memcpy(b->ptr + b->len, s.ptr, s.len);
    b->len += s.len;
    b->ptr[b->len] = '\0';
}

static void cb_i64(cbuf *b, int64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof tmp, "%" PRId64, v);
    cb(b, tmp);
}

// =========================================================================
// Error plumbing — the FIRST error wins and short-circuits the whole walk.
// Helpers return bool (true == ok); the failed message lands in `*err`.
// =========================================================================
static bool fail_node(const char **err, const char *msg) { *err = msg; return false; }

// =========================================================================
// Types -> C type text.
// =========================================================================
static bool emit_prim(cbuf *b, tk_prim_kind k, const char **err) {
    switch (k) {
        case TK_PRIM_U8:  cb(b, "uint8_t");  return true;
        case TK_PRIM_U16: cb(b, "uint16_t"); return true;
        case TK_PRIM_U32: cb(b, "uint32_t"); return true;
        case TK_PRIM_U64: cb(b, "uint64_t"); return true;
        case TK_PRIM_I8:  cb(b, "int8_t");   return true;
        case TK_PRIM_I16: cb(b, "int16_t");  return true;
        case TK_PRIM_I32: cb(b, "int32_t");  return true;
        case TK_PRIM_I64: cb(b, "int64_t");  return true;
        case TK_PRIM_BOOL: cb(b, "bool");    return true;
    }
    return fail_node(err, "codegen: unknown primitive type not yet supported");
}

static bool emit_type(cbuf *b, tk_type t, const char **err) {
    switch (t.tag) {
        case TK_TYPE_PRIM: return emit_prim(b, t.as.prim, err);
        case TK_TYPE_BYTE: cb(b, "uint8_t"); return true;
        case TK_TYPE_UNIT: cb(b, "void");    return true;
        case TK_TYPE_STR:     return fail_node(err, "codegen: str type not yet supported");
        case TK_TYPE_SLICE:   return fail_node(err, "codegen: slice type not yet supported");
        case TK_TYPE_NAMED:   return fail_node(err, "codegen: named type not yet supported");
        case TK_TYPE_VARIANT: return fail_node(err, "codegen: variant type not yet supported");
        case TK_TYPE_FUNC:    return fail_node(err, "codegen: function type not yet supported");
        case TK_TYPE_ERROR:   return fail_node(err, "codegen: error type not yet supported");
    }
    return fail_node(err, "codegen: unknown type not yet supported");
}

// =========================================================================
// Operator token kind -> C operator text.
// =========================================================================
static const char *binop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_PLUS:    return "+";
        case TK_TOKEN_MINUS:   return "-";
        case TK_TOKEN_STAR:    return "*";
        case TK_TOKEN_SLASH:   return "/";
        case TK_TOKEN_PERCENT: return "%";
        case TK_TOKEN_AMP:     return "&";
        case TK_TOKEN_PIPE:    return "|";
        case TK_TOKEN_CARET:   return "^";
        case TK_TOKEN_SHL:     return "<<";
        case TK_TOKEN_SHR:     return ">>";
        case TK_TOKEN_ANDAND:  return "&&";
        case TK_TOKEN_OROR:    return "||";
        default:               return NULL;   // unsupported binary op
    }
}

static const char *unop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_MINUS: return "-";
        case TK_TOKEN_TILDE: return "~";
        case TK_TOKEN_BANG:  return "!";
        default:             return NULL;
    }
}

static const char *cmpop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_EQEQ: return "==";
        case TK_TOKEN_NE:   return "!=";
        case TK_TOKEN_LT:   return "<";
        case TK_TOKEN_LE:   return "<=";
        case TK_TOKEN_GT:   return ">";
        case TK_TOKEN_GE:   return ">=";
        default:            return NULL;
    }
}

static const char *assignop_c(tk_token_kind op) {
    switch (op) {
        case TK_TOKEN_ASSIGN:    return "=";
        case TK_TOKEN_PLUSEQ:    return "+=";
        case TK_TOKEN_MINUSEQ:   return "-=";
        case TK_TOKEN_STAREQ:    return "*=";
        case TK_TOKEN_SLASHEQ:   return "/=";
        case TK_TOKEN_PERCENTEQ: return "%=";
        case TK_TOKEN_AMPEQ:     return "&=";
        case TK_TOKEN_PIPEEQ:    return "|=";
        case TK_TOKEN_CARETEQ:   return "^=";
        case TK_TOKEN_SHLEQ:     return "<<=";
        case TK_TOKEN_SHREQ:     return ">>=";
        default:                 return NULL;
    }
}

// =========================================================================
// Expressions -> C expression text.
// =========================================================================
static bool emit_expr(cbuf *b, const tk_texpr *e, const char **err) {
    switch (e->tag) {
        case TK_TEXPR_NUMBER:
            cb_i64(b, e->as.number.value);
            return true;

        case TK_TEXPR_VAR:
            cb_str(b, e->as.var.name);
            return true;

        case TK_TEXPR_BINARY: {
            const char *op = binop_c(e->as.binary.op);
            if (op == NULL) return fail_node(err, "codegen: binary operator not yet supported");
            cb(b, "(");
            if (!emit_expr(b, e->as.binary.left, err)) return false;
            cb(b, " "); cb(b, op); cb(b, " ");
            if (!emit_expr(b, e->as.binary.right, err)) return false;
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_UNARY: {
            const char *op = unop_c(e->as.unary.op);
            if (op == NULL) return fail_node(err, "codegen: unary operator not yet supported");
            cb(b, "("); cb(b, op);
            if (!emit_expr(b, e->as.unary.operand, err)) return false;
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_COMPARE: {
            // chained a<b<c -> (a<b) && (b<c) over ADJACENT operands.
            size_t nrest = e->as.compare.nrest;
            if (nrest == 0) {   // a lone subject is degenerate; emit it bare
                return emit_expr(b, e->as.compare.first, err);
            }
            cb(b, "(");
            const tk_texpr *prev = e->as.compare.first;
            for (size_t i = 0; i < nrest; i += 1) {
                tk_tcmp_term term = e->as.compare.rest[i];
                const char *op = cmpop_c(term.op);
                if (op == NULL) return fail_node(err, "codegen: comparison operator not yet supported");
                if (i > 0) cb(b, " && ");
                cb(b, "(");
                if (!emit_expr(b, prev, err)) return false;
                cb(b, " "); cb(b, op); cb(b, " ");
                if (!emit_expr(b, term.operand, err)) return false;
                cb(b, ")");
                prev = term.operand;
            }
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_CAST: {
            cb(b, "((");
            if (!emit_type(b, e->type, err)) return false;   // target rides the node's .type
            cb(b, ")(");
            if (!emit_expr(b, e->as.cast.expr, err)) return false;
            cb(b, "))");
            return true;
        }

        case TK_TEXPR_CALL: {
            // callee path -> C identifier joined by "__" (single-segment in M0).
            tk_path p = e->as.call.callee;
            for (size_t i = 0; i < p.len; i += 1) {
                if (i > 0) cb(b, "__");
                cb_str(b, p.segments[i].name);
            }
            cb(b, "(");
            for (size_t i = 0; i < e->as.call.nargs; i += 1) {
                if (i > 0) cb(b, ", ");
                if (!emit_expr(b, &e->as.call.args[i], err)) return false;
            }
            cb(b, ")");
            return true;
        }

        case TK_TEXPR_FIELD_ACCESS:
            cb(b, "(");
            if (!emit_expr(b, e->as.field_access.receiver, err)) return false;
            cb(b, ".");
            cb_str(b, e->as.field_access.field);
            cb(b, ")");
            return true;

        case TK_TEXPR_STR:   return fail_node(err, "codegen: string literal not yet supported");
        case TK_TEXPR_BYTE:  return fail_node(err, "codegen: byte literal not yet supported");
        case TK_TEXPR_IF:    return fail_node(err, "codegen: if-expression not yet supported");
        case TK_TEXPR_MATCH: return fail_node(err, "codegen: match-expression not yet supported");
    }
    return fail_node(err, "codegen: unknown expression not yet supported");
}

// =========================================================================
// Statements -> C. `in_main` selects the return lowering:
//   * in main : `return n` -> `return (int)(n);`  (early process exit)
//   * in a fn : `return n` -> `return n;`
// =========================================================================
static bool emit_stmt(cbuf *b, const tk_tstatement *s, bool in_main,
                      const char *indent, const char **err);

static bool emit_block(cbuf *b, const tk_tstatement *body, size_t n, bool in_main,
                       const char *indent, const char **err) {
    for (size_t i = 0; i < n; i += 1) {
        if (!emit_stmt(b, &body[i], in_main, indent, err)) return false;
    }
    return true;
}

static bool emit_stmt(cbuf *b, const tk_tstatement *s, bool in_main,
                      const char *indent, const char **err) {
    switch (s->tag) {
        case TK_TSTMT_BINDING: {
            // BindTarget: only SimpleName is lowered for M0.
            tk_bind_target tgt = s->as.binding.target;
            if (tgt.tag != TK_BIND_SIMPLE)
                return fail_node(err, "codegen: destructuring binding not yet supported");
            cb(b, indent);
            if (s->as.binding.kind == TK_BIND_CONST) cb(b, "const ");
            if (!emit_type(b, s->as.binding.bound, err)) return false;
            cb(b, " ");
            cb_str(b, tgt.as.simple.name);
            cb(b, " = ");
            if (!emit_expr(b, &s->as.binding.value, err)) return false;
            cb(b, ";\n");
            return true;
        }

        case TK_TSTMT_ASSIGN: {
            const char *op = assignop_c(s->as.assign.op);
            if (op == NULL) return fail_node(err, "codegen: assignment operator not yet supported");
            cb(b, indent);
            cb_str(b, s->as.assign.name);
            cb(b, " "); cb(b, op); cb(b, " ");
            if (!emit_expr(b, &s->as.assign.value, err)) return false;
            cb(b, ";\n");
            return true;
        }

        case TK_TSTMT_RETURN: {
            cb(b, indent);
            if (!s->as.ret.has_value) {
                cb(b, in_main ? "return 0;\n" : "return;\n");
                return true;
            }
            if (in_main) {
                cb(b, "return (int)(");
                if (!emit_expr(b, &s->as.ret.value, err)) return false;
                cb(b, ");\n");
            } else {
                cb(b, "return ");
                if (!emit_expr(b, &s->as.ret.value, err)) return false;
                cb(b, ";\n");
            }
            return true;
        }

        case TK_TSTMT_EXPR:
            cb(b, indent);
            if (!emit_expr(b, &s->as.expr_stmt.expr, err)) return false;
            cb(b, ";\n");
            return true;

        case TK_TSTMT_LOOP: {
            cb(b, indent); cb(b, "while (1) {\n");
            // nest one level deeper
            char inner[64];
            snprintf(inner, sizeof inner, "%s    ", indent);
            if (!emit_block(b, s->as.loop_stmt.body, s->as.loop_stmt.nbody, in_main, inner, err))
                return false;
            cb(b, indent); cb(b, "}\n");
            return true;
        }

        case TK_TSTMT_BREAK:    cb(b, indent); cb(b, "break;\n");    return true;
        case TK_TSTMT_CONTINUE: cb(b, indent); cb(b, "continue;\n"); return true;
    }
    return fail_node(err, "codegen: unknown statement not yet supported");
}

// =========================================================================
// A top-level function -> a C function.
// =========================================================================
static bool emit_function(cbuf *b, tk_tfunction f, const char **err) {
    if (!emit_type(b, f.return_type, err)) return false;
    cb(b, " ");
    cb_str(b, f.name);
    cb(b, "(");
    if (f.nparams == 0) {
        cb(b, "void");
    } else {
        for (size_t i = 0; i < f.nparams; i += 1) {
            if (i > 0) cb(b, ", ");
            // Param type annotations are syntactic (tk_type_expr); M0 has none, so
            // params are not yet supported (would require resolving the annotation).
            (void)f.params;
            return fail_node(err, "codegen: function parameters not yet supported");
        }
    }
    cb(b, ") {\n");
    if (!emit_block(b, f.body, f.nbody, /*in_main=*/false, "    ", err)) return false;
    cb(b, "}\n\n");
    return true;
}

// =========================================================================
// The program -> a full C translation unit.
// =========================================================================
static tk_cstr_result cg_err(const char *m) {
    return (tk_cstr_result){ .ok = false, .as.error = tk_error_make(m) };
}

tk_cstr_result tk_emit_c(tk_tprogram prog) {
    cbuf b = { .ptr = NULL, .len = 0, .cap = 0 };
    const char *err = NULL;

    cb(&b, "// generated by tekoc (F2 backend) — do not edit\n");
    cb(&b, "#include <stdint.h>\n");
    cb(&b, "#include <stdbool.h>\n\n");

    // First pass: emit every top-level function. Reject use/type-decls (not M0).
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        switch (it.tag) {
            case TK_TITEM_FUNCTION:
                if (!emit_function(&b, it.as.function, &err)) { free(b.ptr); return cg_err(err); }
                break;
            case TK_TITEM_USE:
                break;   // imports are a no-op at the C level (M0)
            case TK_TITEM_TYPE_DECL:
                free(b.ptr);
                return cg_err("codegen: type declaration not yet supported");
            case TK_TITEM_STATEMENT:
                break;   // virtual-main statements handled in the second pass
        }
    }

    // Second pass: the VIRTUAL-MAIN — the loose top-level statements, in order, become
    // the body of C main(); falling off the end -> `return 0;` (default exit 0).
    cb(&b, "int main(void) {\n");
    for (size_t i = 0; i < prog.nitems; i += 1) {
        tk_titem it = prog.items[i];
        if (it.tag != TK_TITEM_STATEMENT) continue;
        if (!emit_stmt(&b, &it.as.statement, /*in_main=*/true, "    ", &err)) {
            free(b.ptr);
            return cg_err(err);
        }
    }
    cb(&b, "    return 0;\n");
    cb(&b, "}\n");

    return (tk_cstr_result){ .ok = true, .as.value = b.ptr };
}
