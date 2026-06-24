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

// Compare a tk_str span (no NUL terminator) to a C string literal.
static bool seg_is(tk_str s, const char *lit) {
    size_t n = strlen(lit);
    return s.len == n && memcmp(s.ptr, lit, n) == 0;
}

// Append `s` C-escaped, INSIDE an already-open C string literal. The bytes are raw
// (may contain NUL, quotes, newlines) — see TK_TEXPR_STR. We never rely on length here;
// the caller emits the explicit byte count separately (M.1).
static void cb_cstr_escaped(cbuf *b, tk_str s) {
    char tmp[8];
    for (size_t i = 0; i < s.len; i += 1) {
        unsigned char c = s.ptr[i];
        switch (c) {
            case '\\': cb(b, "\\\\"); break;
            case '"':  cb(b, "\\\"");  break;
            case '\n': cb(b, "\\n"); break;
            case '\t': cb(b, "\\t"); break;
            case '\r': cb(b, "\\r"); break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    // 3-digit octal — fixed width dodges the hex "run-on digit" hazard.
                    snprintf(tmp, sizeof tmp, "\\%03o", c);
                    cb(b, tmp);
                } else {
                    tmp[0] = (char)c; tmp[1] = '\0';
                    cb(b, tmp);
                }
                break;
        }
    }
}

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
        case TK_TYPE_STR:     cb(b, "tk_str"); return true;
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

// =========================================================================
// F3 GUARDS (M.1 — fail loud): the prim helpers that pick the right runtime
// guard. Integer prims only; bool is never a guard target.
// =========================================================================
static bool prim_is_int(tk_prim_kind k) { return k != TK_PRIM_BOOL; }
static bool prim_is_signed(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_I8: case TK_PRIM_I16: case TK_PRIM_I32: case TK_PRIM_I64: return true;
        default: return false;
    }
}
// rank: bit-width ordinal (8/16/32/64) for narrowing analysis.
static int prim_width(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:  case TK_PRIM_I8:  return 8;
        case TK_PRIM_U16: case TK_PRIM_I16: return 16;
        case TK_PRIM_U32: case TK_PRIM_I32: return 32;
        case TK_PRIM_U64: case TK_PRIM_I64: return 64;
        case TK_PRIM_BOOL: return 1;
    }
    return 0;
}
// The width tag ("u8".."i64") used to name a div/mod helper, e.g. tk_div_u32.
static const char *prim_div_tag(tk_prim_kind k) {
    switch (k) {
        case TK_PRIM_U8:  return "u8";  case TK_PRIM_U16: return "u16";
        case TK_PRIM_U32: return "u32"; case TK_PRIM_U64: return "u64";
        case TK_PRIM_I8:  return "i8";  case TK_PRIM_I16: return "i16";
        case TK_PRIM_I32: return "i32"; case TK_PRIM_I64: return "i64";
        case TK_PRIM_BOOL: return NULL;
    }
    return NULL;
}

// A cast src->dst may lose data (needs a runtime guard) when SOME value of the
// source type falls outside the destination's representable range. Conservative:
// if we can't prove the source range ⊆ dest range, guard.
static bool cast_may_lose(tk_prim_kind src, tk_prim_kind dst) {
    bool ss = prim_is_signed(src), ds = prim_is_signed(dst);
    int sw = prim_width(src), dw = prim_width(dst);
    if (ss == ds)          return sw > dw;          // same signedness: only true narrowing loses
    if (!ss && ds)         return sw >= dw;         // u->i: dest loses one bit of magnitude at equal width
    /* ss && !ds */        return true;             // i->u: negatives never fit
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
            tk_token_kind bop = e->as.binary.op;
            // F3 (M.1): `/` and `%` go through a checked runtime helper that PANICS on a
            // zero divisor instead of UB/SIGFPE. Single-eval: each operand emitted once,
            // passed by value. The helper width comes from the node's result prim.
            if (bop == TK_TOKEN_SLASH || bop == TK_TOKEN_PERCENT) {
                if (e->type.tag != TK_TYPE_PRIM)
                    return fail_node(err, "codegen: division/modulo on a non-primitive type not yet supported");
                const char *tag = prim_div_tag(e->type.as.prim);
                if (tag == NULL)
                    return fail_node(err, "codegen: division/modulo on a non-integer type not yet supported");
                cb(b, bop == TK_TOKEN_SLASH ? "tk_div_" : "tk_mod_");
                cb(b, tag);
                cb(b, "(");
                if (!emit_expr(b, e->as.binary.left, err)) return false;
                cb(b, ", ");
                if (!emit_expr(b, e->as.binary.right, err)) return false;
                cb(b, ")");
                return true;
            }
            // NOTE (out of scope): +,-,* stay plain C — overflow guarding is DEFERRED to
            // build profiles (panic-debug / wrap-release), which don't exist yet.
            const char *op = binop_c(bop);
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
            // F3 (M.1): a narrowing `x to T` is range-checked at runtime and PANICS if the
            // value doesn't fit T. Widening / same-type casts where no loss is possible
            // emit the plain C cast (no needless guard). Single-eval: operand emitted once.
            const tk_texpr *inner = e->as.cast.expr;
            bool guard = e->type.tag == TK_TYPE_PRIM && inner->type.tag == TK_TYPE_PRIM
                      && prim_is_int(e->type.as.prim) && prim_is_int(inner->type.as.prim)
                      && cast_may_lose(inner->type.as.prim, e->type.as.prim);
            if (guard) {
                tk_prim_kind dst = e->type.as.prim, src = inner->type.as.prim;
                const char *dtag = prim_div_tag(dst);   // reuse the "u8".."i64" tag table
                if (dtag == NULL) return fail_node(err, "codegen: cast to a non-integer type not yet supported");
                // Carrier picked by SOURCE signedness so it holds the source losslessly:
                //   signed source -> int64_t carrier ("_s"); unsigned -> uint64_t ("_u").
                cb(b, "tk_to_");
                cb(b, dtag);
                cb(b, prim_is_signed(src) ? "_s(" : "_u(");
                if (!emit_expr(b, inner, err)) return false;
                cb(b, ")");
                return true;
            }
            cb(b, "((");
            if (!emit_type(b, e->type, err)) return false;   // target rides the node's .type
            cb(b, ")(");
            if (!emit_expr(b, inner, err)) return false;
            cb(b, "))");
            return true;
        }

        case TK_TEXPR_CALL: {
            // callee path -> C identifier joined by "__" (single-segment in M0).
            tk_path p = e->as.call.callee;
            // Non-shadowable built-ins: `print`/`println`, either bare or under `teko`.
            const char *builtin = NULL;
            if (p.len >= 1) {
                tk_str last = p.segments[p.len - 1].name;
                bool addressable = (p.len == 1) || seg_is(p.segments[0].name, "teko");
                if (addressable) {
                    if      (seg_is(last, "print"))   builtin = "tk_print";
                    else if (seg_is(last, "println")) builtin = "tk_println";
                }
            }
            if (builtin != NULL) {
                cb(b, builtin);
            } else for (size_t i = 0; i < p.len; i += 1) {
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

        case TK_TEXPR_STR: {
            // tk_str compound literal with an EXPLICIT length — the decoded bytes may
            // contain NUL, so we never lean on strlen/sizeof (M.1).
            cb(b, "(tk_str){ (const tk_byte *)\"");
            cb_cstr_escaped(b, e->as.str.text);
            cb(b, "\", ");
            cb_i64(b, (int64_t)e->as.str.text.len);
            cb(b, " }");
            return true;
        }

        case TK_TEXPR_BYTE:
            // byte == uint8_t; emit its decimal value (0..255).
            cb_i64(b, (int64_t)e->as.byte.value);
            return true;
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

    cb(&b, "// generated by teko (F2 backend) — do not edit\n");
    cb(&b, "#include <stdint.h>\n");
    cb(&b, "#include <stdbool.h>\n");
    cb(&b, "#include \"teko_rt.h\"\n\n");

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
