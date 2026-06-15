#include "frontend_interop.h"
#include "lexer.h"
#include "parser.h"
#include "parser_ffi.h"
#include <stdlib.h>
#include <string.h>

// Local token advance (parser.c's is static): current <- peek <- next.
static void fe_advance(Parser* p) {
    p->current_token = p->peek_token;
    p->peek_token = lexer_next_token(p->lexer);
}

// String-literal lexemes arrive with their surrounding delimiters ("…", `…`, '…').
// Return a malloc'd copy with one matching leading/trailing delimiter removed.
static char* strip_quotes(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len >= 2) {
        char q = s[0];
        if ((q == '"' || q == '`' || q == '\'') && s[len - 1] == q) {
            char* out = (char*)malloc(len - 1);
            memcpy(out, s + 1, len - 2);
            out[len - 2] = '\0';
            return out;
        }
    }
    return strdup(s);
}

// Teko-visible callee name -> import-table index.
typedef struct { char* name; int idx; } ImportBinding;

static void bind_add(ImportBinding** binds, int* n, int* cap, const char* name, int idx) {
    if (!name) return;
    if (*n >= *cap) {
        *cap = (*cap == 0) ? 8 : (*cap * 2);
        *binds = (ImportBinding*)realloc(*binds, sizeof(ImportBinding) * (*cap));
    }
    (*binds)[*n].name = strdup(name);
    (*binds)[*n].idx = idx;
    (*n)++;
}

static int bind_lookup(ImportBinding* binds, int n, const char* name) {
    for (int i = 0; i < n; i++) {
        if (strcmp(binds[i].name, name) == 0) return binds[i].idx;
    }
    return -1;
}

// Register one extern function as an import + a name->index binding.
static void register_extern_fn(BytecodeBuffer* buffer, const char* from_lib,
                               const FFIFunctionNode* fn,
                               ImportBinding** binds, int* nb, int* capb) {
    if (!fn || !fn->fn_name) return;
    // from_lib / alias are string-literal lexemes (quoted); the fn_name is a bare
    // identifier. Strip delimiters off the literals before they become ns/name.
    char* ns = from_lib ? strip_quotes(from_lib) : strdup("env");
    char* name = fn->alias ? strip_quotes(fn->alias) : strdup(fn->fn_name);
    int has_result = (fn->return_type && strcmp(fn->return_type, "void") != 0) ? 1 : 0;
    int idx = codegen_li_add_import(buffer, ns, name, fn->param_count, has_result);
    bind_add(binds, nb, capb, fn->fn_name, idx);
    free(ns);
    free(name);
}

static void register_extern(BytecodeBuffer* buffer, const FFIASTNode* node,
                            ImportBinding** binds, int* nb, int* capb) {
    if (!node) return;
    if (node->type == NODE_FFI_FUNCTION) {
        register_extern_fn(buffer, node->from_lib, &node->data.ffi_function, binds, nb, capb);
    } else if (node->type == NODE_FFI_BLOCK) {
        for (int i = 0; i < node->data.ffi_block.function_count; i++) {
            const FFIASTNode* inner = node->data.ffi_block.functions[i];
            if (inner && inner->type == NODE_FFI_FUNCTION) {
                // Block-level `from "ns"` applies to the inner functions.
                register_extern_fn(buffer, node->from_lib, &inner->data.ffi_function, binds, nb, capb);
            }
        }
    }
}

// A literal call argument.
typedef struct { int is_string; char* sval; int ival; } CallArg;

// Lower a resolved call: args 0..n-2 staged via OP_SETARG, the last left in $w0,
// then OP_CALL_IMPORT. String args are pooled; int args are immediates.
static void lower_call(BytecodeBuffer* buffer, int import_index, CallArg* args, int n) {
    for (int i = 0; i < n; i++) {
        if (args[i].is_string) {
            int s = codegen_li_add_string_constant(buffer, args[i].sval);
            codegen_li_emit_sconst(buffer, s);
        } else {
            codegen_li_emit_iconst(buffer, args[i].ival);
        }
        if (i != n - 1) codegen_li_emit_setarg(buffer, i);
    }
    codegen_li_emit_call_import(buffer, import_index);
}

// --- @dom/@js intrinsics (FE-E) -------------------------------------------------
// A `@dom.method(args)` / `@js.method(args)` call auto-registers a host import in the
// `dom`/`js` namespace and lowers like an extern call, with two refinements:
//   • a string argument expands to TWO wasm params (ptr, len) — the (ptr,len) ABI the
//     dom.* glue marshals — so n_params is computed from the args, and
//   • the FIRST argument may be a nested `@dom.…()` call (its result handle feeds the
//     outer call), which covers e.g. setText(getElementById("out"), "…").
// Only the leading arg may be nested (one level of staging-slot reuse is safe); other
// args must be string/int literals. The call result (a handle, when any) lands in $w0.

// Which dom/js intrinsics return a value (an i32 handle).
static int intrinsic_has_result(const char* method) {
    return (strcmp(method, "getElementById") == 0 ||
            strcmp(method, "createElement") == 0) ? 1 : 0;
}

// Lowering context threaded through @dom calls: the current handler param name (an
// identifier arg matching it loads the event arg from $w1) and the handler name->table
// slot map (an identifier arg matching one is a function reference -> its table index).
typedef struct {
    const char* param_name; // current handler param, or NULL at top level
    ImportBinding* fns;      // handler name -> table slot (idx = slot)
    int nfns;
} LowerCtx;

// A flat "producer" (one wasm param). kind: 0=ICONST payload, 1=SCONST pool idx,
// 2=LOAD the handler param ($w0 <- $w1, no payload).
typedef struct { int kind; int payload; } Prod;

static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p, const LowerCtx* ctx); // recursive

static void lower_intrinsic_call(BytecodeBuffer* buffer, Parser* p, const LowerCtx* ctx) {
    // current token is the MACRO_IDENT, e.g. "@dom.setText". Split into ns + method.
    char full[128];
    strncpy(full, p->current_token.lexeme, sizeof(full) - 1);
    full[sizeof(full) - 1] = '\0';
    char* dot = strchr(full, '.');
    if (full[0] != '@' || !dot) { fe_advance(p); return; }
    char ns[32];
    size_t nslen = (size_t)(dot - (full + 1));
    if (nslen >= sizeof(ns)) nslen = sizeof(ns) - 1;
    memcpy(ns, full + 1, nslen);
    ns[nslen] = '\0';
    char method[96];
    strncpy(method, dot + 1, sizeof(method) - 1);
    method[sizeof(method) - 1] = '\0';

    fe_advance(p); // consume the macro ident
    if (p->current_token.type != TOKEN_LPAREN) return;
    fe_advance(p); // consume '('

    // Optional leading nested intrinsic call as arg0 (its result -> $w0).
    int has_nested = 0;
    if (p->current_token.type == TOKEN_MACRO_IDENT) {
        lower_intrinsic_call(buffer, p, ctx); // emits inner; result in $w0
        has_nested = 1;
        if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
    }

    // Collect the remaining args, expanded to flat producers (string -> ptr+len).
    Prod prods[32];
    int np = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if ((p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) && np + 2 <= 32) {
            char* s = strip_quotes(p->current_token.lexeme);
            int idx = codegen_li_add_string_constant(buffer, s);
            int len = (int)strlen(s);
            free(s);
            prods[np].kind = 1; prods[np].payload = idx; np++;   // ptr (SCONST)
            prods[np].kind = 0; prods[np].payload = len; np++;   // len (ICONST)
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_LIT_INT && np + 1 <= 32) {
            prods[np].kind = 0; prods[np].payload = atoi(p->current_token.lexeme); np++;
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_IDENTIFIER && np + 1 <= 32) {
            const char* id = p->current_token.lexeme;
            if (ctx && ctx->param_name && strcmp(id, ctx->param_name) == 0) {
                prods[np].kind = 2; prods[np].payload = 0; np++;            // LOAD handler param
            } else {
                int slot = ctx ? bind_lookup(ctx->fns, ctx->nfns, id) : -1;
                if (slot >= 0) { prods[np].kind = 0; prods[np].payload = slot; np++; } // fn ref -> ICONST slot
                // else: unknown identifier in this subset — produces nothing
            }
            fe_advance(p);
        } else if (p->current_token.type == TOKEN_COMMA) {
            fe_advance(p);
        } else {
            fe_advance(p); // skip unsupported tokens in this subset
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);

    int total = (has_nested ? 1 : 0) + np;

    // Stage the nested result (param 0) unless it is the only/last param.
    if (has_nested && total > 1) codegen_li_emit_setarg(buffer, 0);

    // Emit the producers at absolute slots [has_nested .. total-1]; the last param
    // stays in $w0.
    for (int k = 0; k < np; k++) {
        int slot = (has_nested ? 1 : 0) + k;
        switch (prods[k].kind) {
            case 1: codegen_li_emit_sconst(buffer, prods[k].payload); break;
            case 2: codegen_li_emit_load(buffer); break;
            default: codegen_li_emit_iconst(buffer, prods[k].payload); break;
        }
        if (slot < total - 1) codegen_li_emit_setarg(buffer, slot);
    }

    int import_index = codegen_li_add_import(buffer, ns, method, total, intrinsic_has_result(method));
    codegen_li_emit_call_import(buffer, import_index);
}

static int is_dom_macro(const char* lexeme) {
    return lexeme && (strncmp(lexeme, "@dom.", 5) == 0 || strncmp(lexeme, "@js.", 4) == 0);
}

// Skip a whole `extern …;` / `extern { … }` declaration. Needed by the fn scanners
// below so the `fn` token INSIDE `extern fn …` is not mistaken for a handler.
static void skip_extern_decl(Parser* p) {
    fe_advance(p); // consume 'extern'
    while (p->current_token.type != TOKEN_LBRACE &&
           p->current_token.type != TOKEN_SEMICOLON &&
           p->current_token.type != TOKEN_EOF) {
        fe_advance(p);
    }
    if (p->current_token.type == TOKEN_LBRACE) {
        int depth = 0;
        while (p->current_token.type != TOKEN_EOF) {
            if (p->current_token.type == TOKEN_LBRACE) { depth++; fe_advance(p); }
            else if (p->current_token.type == TOKEN_RBRACE) {
                depth--; fe_advance(p);
                if (depth == 0) break;
            } else {
                fe_advance(p);
            }
        }
        if (p->current_token.type == TOKEN_SEMICOLON) fe_advance(p);
    } else if (p->current_token.type == TOKEN_SEMICOLON) {
        fe_advance(p);
    }
}

// Pre-pass: assign each top-level `fn NAME` a table slot (declaration order), so a
// main-level @dom.on(…, NAME) can resolve the handler reference before its body.
static void collect_functions(const char* source, ImportBinding** fns, int* nfns, int* capfns) {
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);
    int slot = 0;
    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_EXTERN) {
            skip_extern_decl(&p); // do not treat `extern fn` as a handler
        } else if (p.current_token.type == TOKEN_FN && p.peek_token.type == TOKEN_IDENTIFIER) {
            bind_add(fns, nfns, capfns, p.peek_token.lexeme, slot++);
            fe_advance(&p); // consume 'fn'
            fe_advance(&p); // consume name
        } else {
            fe_advance(&p);
        }
    }
}

// Routine pass: emit each `fn NAME(param) { body }` as a table routine. The handler is
// invoked via teko_invoke(slot, event_arg): on entry $w0 = the arg, which we stash to
// $w1 so `param` references (LOAD) survive across the body's @dom calls.
static void emit_handler_routines(const char* source, BytecodeBuffer* buffer,
                                  ImportBinding* fns, int nfns) {
    Lexer lx; lexer_init(&lx, source);
    Parser p; parser_init(&p, &lx);

    while (p.current_token.type != TOKEN_EOF) {
        if (p.current_token.type == TOKEN_EXTERN) { skip_extern_decl(&p); continue; }
        if (p.current_token.type != TOKEN_FN) { fe_advance(&p); continue; }
        fe_advance(&p); // consume 'fn'
        if (p.current_token.type != TOKEN_IDENTIFIER) continue;

        char fn_name[96];
        strncpy(fn_name, p.current_token.lexeme, sizeof(fn_name) - 1);
        fn_name[sizeof(fn_name) - 1] = '\0';
        int slot = bind_lookup(fns, nfns, fn_name);
        fe_advance(&p); // consume name

        // Parameter list: capture the single event param name (if any).
        char param[96]; param[0] = '\0';
        if (p.current_token.type == TOKEN_LPAREN) {
            fe_advance(&p);
            if (p.current_token.type == TOKEN_IDENTIFIER) {
                strncpy(param, p.current_token.lexeme, sizeof(param) - 1);
                param[sizeof(param) - 1] = '\0';
            }
            while (p.current_token.type != TOKEN_RPAREN && p.current_token.type != TOKEN_EOF) {
                fe_advance(&p); // skip rest of params / types
            }
            if (p.current_token.type == TOKEN_RPAREN) fe_advance(&p);
        }
        // Body open brace.
        while (p.current_token.type != TOKEN_LBRACE && p.current_token.type != TOKEN_EOF) fe_advance(&p);
        if (p.current_token.type == TOKEN_LBRACE) fe_advance(&p);

        codegen_li_emit_func_begin(buffer, slot >= 0 ? slot : 0);
        if (param[0]) codegen_li_emit_store(buffer); // $w1 = event arg

        LowerCtx ctx;
        ctx.param_name = param[0] ? param : NULL;
        ctx.fns = fns;
        ctx.nfns = nfns;

        int depth = 1;
        while (p.current_token.type != TOKEN_EOF && depth > 0) {
            if (p.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&p); }
            else if (p.current_token.type == TOKEN_RBRACE) { depth--; fe_advance(&p); }
            else if (p.current_token.type == TOKEN_MACRO_IDENT &&
                     is_dom_macro(p.current_token.lexeme) &&
                     p.peek_token.type == TOKEN_LPAREN) {
                lower_intrinsic_call(buffer, &p, &ctx);
            } else {
                fe_advance(&p);
            }
        }
        codegen_li_emit_func_end(buffer);
    }
}

int teko_compile_interop(const char* source, BytecodeBuffer* buffer) {
    if (!source || !buffer) return 1;

    Lexer lexer;
    lexer_init(&lexer, source);
    Parser parser;
    parser_init(&parser, &lexer);

    ImportBinding* binds = NULL;
    int nb = 0, capb = 0;

    // Pre-pass: map every handler `fn NAME` to a table slot so main-level
    // @dom.on(…, NAME) can reference it even if declared later.
    ImportBinding* fns = NULL;
    int nfns = 0, capfns = 0;
    collect_functions(source, &fns, &nfns, &capfns);
    LowerCtx top_ctx; top_ctx.param_name = NULL; top_ctx.fns = fns; top_ctx.nfns = nfns;

    while (parser.current_token.type != TOKEN_EOF) {
        if (parser.current_token.type == TOKEN_EXTERN) {
            FFIASTNode* node = parse_extern_declaration(&parser);
            if (node) {
                register_extern(buffer, node, &binds, &nb, &capb);
                free_ffi_ast_node(node);
            }
        } else if (parser.current_token.type == TOKEN_FN) {
            // Handler declaration: skip its body in the main pass; it is emitted as a
            // table routine after main's HALT (emit_handler_routines).
            while (parser.current_token.type != TOKEN_LBRACE &&
                   parser.current_token.type != TOKEN_EOF) fe_advance(&parser);
            int depth = 0;
            while (parser.current_token.type != TOKEN_EOF) {
                if (parser.current_token.type == TOKEN_LBRACE) { depth++; fe_advance(&parser); }
                else if (parser.current_token.type == TOKEN_RBRACE) {
                    depth--; fe_advance(&parser);
                    if (depth == 0) break;
                } else {
                    fe_advance(&parser);
                }
            }
        } else if (parser.current_token.type == TOKEN_MACRO_IDENT &&
                   is_dom_macro(parser.current_token.lexeme) &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level @dom/@js intrinsic call statement.
            lower_intrinsic_call(buffer, &parser, &top_ctx);
        } else if (parser.current_token.type == TOKEN_IDENTIFIER &&
                   parser.peek_token.type == TOKEN_LPAREN) {
            // Top-level call statement: NAME ( arg, … )
            char* callee = strdup(parser.current_token.lexeme);
            fe_advance(&parser); // consume NAME
            fe_advance(&parser); // consume '('

            CallArg args[16];
            int nargs = 0;
            while (parser.current_token.type != TOKEN_RPAREN &&
                   parser.current_token.type != TOKEN_EOF) {
                if (nargs < 16 &&
                    (parser.current_token.type == TOKEN_LIT_STR ||
                     parser.current_token.type == TOKEN_STRING_LIT)) {
                    args[nargs].is_string = 1;
                    args[nargs].sval = strip_quotes(parser.current_token.lexeme);
                    args[nargs].ival = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_LIT_INT) {
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = atoi(parser.current_token.lexeme);
                    nargs++;
                    fe_advance(&parser);
                } else if (parser.current_token.type == TOKEN_COMMA) {
                    fe_advance(&parser);
                } else {
                    fe_advance(&parser); // skip anything else in the interop subset
                }
            }
            if (parser.current_token.type == TOKEN_RPAREN) fe_advance(&parser);
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);

            int idx = bind_lookup(binds, nb, callee);
            if (idx >= 0) lower_call(buffer, idx, args, nargs);

            for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
            free(callee);
        } else {
            fe_advance(&parser);
        }
    }

    codegen_li_emit_halt(buffer); // close main

    // Emit handler bodies as table routines (after main), so @dom.on references resolve.
    emit_handler_routines(source, buffer, fns, nfns);

    for (int i = 0; i < nb; i++) free(binds[i].name);
    free(binds);
    for (int i = 0; i < nfns; i++) free(fns[i].name);
    free(fns);
    return 0;
}
