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

// A call argument: a string literal, an int literal, or a named local (Phase 12).
typedef struct { int is_string; char* sval; int ival; int is_local; int slot; } CallArg;

// Lower a resolved call: args 0..n-2 staged via OP_SETARG, the last left in $w0,
// then OP_CALL_IMPORT. String args are pooled; int args are immediates; local args
// load from their named slot.
static void lower_call(BytecodeBuffer* buffer, int import_index, CallArg* args, int n) {
    for (int i = 0; i < n; i++) {
        if (args[i].is_local) {
            codegen_li_emit_load_local(buffer, args[i].slot);
        } else if (args[i].is_string) {
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

// Temp-local allocator (P12-E/F): named-local slots ($v) double as expression and
// nested-arg spill temps. next_temp = next free slot; hw = high-water for the count
// the backend must declare.
typedef struct { int next_temp; int hw; } TempAlloc;

// Lowering context threaded through @dom calls: the current handler param name (an
// identifier arg matching it loads the event arg from $w1), the handler name->table
// slot map (an identifier arg matching one is a function reference -> its table index),
// the named-local table, and the temp allocator (for nested-arg spills, P12-F).
typedef struct {
    const char* param_name; // current handler param, or NULL at top level
    ImportBinding* fns;      // handler name -> table slot (idx = slot)
    int nfns;
    ImportBinding* locals;   // Phase 12: named local -> slot (idx = $v slot)
    int nlocals;
    TempAlloc* ta;           // P12-F: temp-local allocator for nested-arg spills
} LowerCtx;

// A flat "producer" (one wasm param). kind: 0=ICONST payload, 1=SCONST pool idx,
// 2=LOAD the handler param ($w0 <- $w1), 3=LOAD_LOCAL payload (a named local).
typedef struct { int kind; int payload; } Prod;

static int is_dom_macro(const char* lexeme); // defined below
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

    // Collect args as flat producers (string -> ptr+len). A nested `@dom.…()` arg in
    // ANY position (P12-F) is lowered eagerly and its result spilled to a fresh temp
    // local; the producer then LOAD_LOCALs it during staging. So multiple nested handle
    // args work (e.g. appendChild(getElementById(…), createElement(…))).
    Prod prods[32];
    int np = 0;
    int temps_used = 0;
    while (p->current_token.type != TOKEN_RPAREN && p->current_token.type != TOKEN_EOF) {
        if (p->current_token.type == TOKEN_MACRO_IDENT && is_dom_macro(p->current_token.lexeme) &&
            np + 1 <= 32) {
            lower_intrinsic_call(buffer, p, ctx);              // inner result -> $w0
            int t = (ctx && ctx->ta) ? ctx->ta->next_temp++ : 0;
            if (ctx && ctx->ta && ctx->ta->next_temp > ctx->ta->hw) ctx->ta->hw = ctx->ta->next_temp;
            codegen_li_emit_store_local(buffer, t);            // park the handle in a temp
            prods[np].kind = 3; prods[np].payload = t; np++;   // producer: LOAD_LOCAL temp
            temps_used++;
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        } else if ((p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) && np + 2 <= 32) {
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
            int lslot = ctx ? bind_lookup(ctx->locals, ctx->nlocals, id) : -1;
            if (ctx && ctx->param_name && strcmp(id, ctx->param_name) == 0) {
                prods[np].kind = 2; prods[np].payload = 0; np++;            // LOAD handler param
            } else if (lslot >= 0) {
                prods[np].kind = 3; prods[np].payload = lslot; np++;        // LOAD_LOCAL (named local)
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

    // Emit the producers at slots [0..np-1]; the last param stays in $w0.
    for (int k = 0; k < np; k++) {
        switch (prods[k].kind) {
            case 1: codegen_li_emit_sconst(buffer, prods[k].payload); break;
            case 2: codegen_li_emit_load(buffer); break;
            case 3: codegen_li_emit_load_local(buffer, prods[k].payload); break;
            default: codegen_li_emit_iconst(buffer, prods[k].payload); break;
        }
        if (k < np - 1) codegen_li_emit_setarg(buffer, k);
    }

    // Free the nested-arg temps reserved for this call.
    if (ctx && ctx->ta) ctx->ta->next_temp -= temps_used;

    int import_index = codegen_li_add_import(buffer, ns, method, np, intrinsic_has_result(method));
    codegen_li_emit_call_import(buffer, import_index);
}

static int is_dom_macro(const char* lexeme) {
    return lexeme && (strncmp(lexeme, "@dom.", 5) == 0 || strncmp(lexeme, "@js.", 4) == 0);
}

// --- integer expression parser (FE P12-E) ---------------------------------------
// A precedence-climbing (Pratt) parser for integer arithmetic + comparisons. Each
// binary node spills its left operand to a fresh temporary named-local so arbitrary
// nesting works in the accumulator model: result lands in $w0. Operand/temp slots
// live in the same $v file as named locals (P12-D); `hw` tracks the high-water so the
// backend declares enough locals. Scope: int literals, named locals, parentheses, and
// `+ - * / % == != < <= > >=` (left-assoc). Float / `&&`/`||` are future work.
static int p12_tok_prec(TokenType t) {
    switch (t) {
        case TOKEN_MUL: case TOKEN_DIV: case TOKEN_MOD: return 3;
        case TOKEN_PLUS: case TOKEN_MINUS: return 2;
        case TOKEN_EQ: case TOKEN_NE: case TOKEN_LT:
        case TOKEN_LE: case TOKEN_GT: case TOKEN_GE: return 1;
        default: return 0;
    }
}

static OpCode p12_tok_op(TokenType t) {
    switch (t) {
        case TOKEN_PLUS:  return OP_ADD; case TOKEN_MINUS: return OP_SUB;
        case TOKEN_MUL:   return OP_MUL; case TOKEN_DIV:   return OP_DIV;
        case TOKEN_MOD:   return OP_MOD;
        case TOKEN_EQ:    return OP_EQ;  case TOKEN_NE:    return OP_NE;
        case TOKEN_LT:    return OP_LT;  case TOKEN_LE:    return OP_LE;
        case TOKEN_GT:    return OP_GT;  case TOKEN_GE:    return OP_GE;
        default:          return OP_ADD;
    }
}

static void eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                           int min_prec, TempAlloc* ta);

static void eval_primary(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx, TempAlloc* ta) {
    if (p->current_token.type == TOKEN_LIT_INT) {
        codegen_li_emit_iconst(b, atoi(p->current_token.lexeme));
        fe_advance(p);
    } else if (p->current_token.type == TOKEN_LPAREN) {
        fe_advance(p);
        eval_expr_prec(b, p, ctx, 1, ta);
        if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
        if (s >= 0) codegen_li_emit_load_local(b, s);
        else codegen_li_emit_iconst(b, 0); // unknown identifier in this subset → 0
        fe_advance(p);
    } else {
        codegen_li_emit_iconst(b, 0); // empty/unsupported primary → 0
    }
}

static void eval_expr_prec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx,
                           int min_prec, TempAlloc* ta) {
    eval_primary(b, p, ctx, ta); // left operand → $w0
    while (p12_tok_prec(p->current_token.type) >= min_prec &&
           p12_tok_prec(p->current_token.type) > 0) {
        int prec = p12_tok_prec(p->current_token.type);
        OpCode op = p12_tok_op(p->current_token.type);
        fe_advance(p); // consume the operator
        int t = ta->next_temp++;
        if (ta->next_temp > ta->hw) ta->hw = ta->next_temp;
        codegen_li_emit_store_local(b, t);        // temp = left
        eval_expr_prec(b, p, ctx, prec + 1, ta);  // right → $w0 (left-associative)
        codegen_li_emit_store(b);                 // $w1 = right
        codegen_li_emit_load_local(b, t);         // $w0 = left
        codegen_li_emit_binop(b, op);             // $w0 = left <op> right
        ta->next_temp--;                          // free temp
    }
}

// --- base-encoding codecs (P12-G) -----------------------------------------------
// `base64.encode(x)` / `.decode`, `hex.encode/.decode`. Lexes as
// <TOKEN_BASE64|TOKEN_HEX> TOKEN_DOT <TOKEN_ENCODE|TOKEN_DECODE> '(' arg ')'. The arg is
// a string literal, a named local, or a nested codec call; it is lowered to a pointer
// in $w0, then OP_CALL_RUNTIME invokes the native codec (result pointer -> $w0).
// The lexer folds `base64.encode` into a single dotted identifier (the same rule that
// makes `@marshall.to_ptr` one macro token), so a codec call is an IDENTIFIER whose
// lexeme is "<base64|hex>.<encode|decode>" followed by '('. Returns the codec id
// (0=b64e,1=b64d,2=hexe,3=hexd) or -1.
static int codec_id_for(const char* lex) {
    if (!lex) return -1;
    if (strcmp(lex, "base64.encode") == 0) return 0;
    if (strcmp(lex, "base64.decode") == 0) return 1;
    if (strcmp(lex, "hex.encode") == 0) return 2;
    if (strcmp(lex, "hex.decode") == 0) return 3;
    // Phase 13 (13.1): native hash primitives. Same dotted-identifier lowering as the
    // base codecs — `hash.sha256(x)` takes a string/local/nested value, lowers it to a
    // NUL-terminated pointer in $w0, then OP_CALL_RUNTIME invokes the in-module SHA-256
    // runtime which returns a pointer to the lowercase hex digest. (hash.sha512 = id 5,
    // wired in the next increment alongside its i64 WAT runtime.)
    if (strcmp(lex, "hash.sha256") == 0) return 4;
    // Phase 13 native runner — rest of the fixed-size hash family (native surface; the
    // WASM lowering of these is deferred to Sub-phase C, where the WASM emitter traps).
    if (strcmp(lex, "hash.sha512") == 0) return 5;
    if (strcmp(lex, "hash.sha384") == 0) return 10;
    if (strcmp(lex, "hash.sha3_256") == 0) return 11;
    if (strcmp(lex, "hash.sha3_512") == 0) return 12;
    if (strcmp(lex, "hash.blake3") == 0) return 15;
    if (strcmp(lex, "hash.blake2b") == 0) return 16;
    // HMAC (two args: hex key, message) — Phase 13 native surface, ids 17-19.
    if (strcmp(lex, "hmac.sha256") == 0) return 17;
    if (strcmp(lex, "hmac.sha384") == 0) return 18;
    if (strcmp(lex, "hmac.sha512") == 0) return 19;
    // AEAD (four args: hex key, nonce, aad, plaintext|cipher‖tag) — ids 20-23.
    // seal -> (ct‖tag) hex; open -> plaintext hex, or "REJECT" on auth failure.
    if (strcmp(lex, "crypto.aes_gcm_seal") == 0) return 20;
    if (strcmp(lex, "crypto.aes_gcm_open") == 0) return 21;
    if (strcmp(lex, "crypto.chacha20poly1305_seal") == 0) return 22;
    if (strcmp(lex, "crypto.chacha20poly1305_open") == 0) return 23;
    // Signatures — Ed25519 (ids 24/25). sign(seedHex, msgHex) -> sigHex;
    // verify(pubHex, msgHex, sigHex) -> "1" (valid) | "0" (invalid).
    if (strcmp(lex, "crypto.ed25519_sign") == 0) return 24;
    if (strcmp(lex, "crypto.ed25519_verify") == 0) return 25;
    // Key exchange — X25519 (RFC 7748). x25519(scalarHex, uHex) -> sharedHex. id 26.
    if (strcmp(lex, "crypto.x25519") == 0) return 26;
    // KDF — HKDF / PBKDF2 over SHA-256. Hex inputs + integer length/iteration args. ids 27/28.
    // hkdf_sha256(ikmHex, saltHex, infoHex, len); pbkdf2_sha256(passHex, saltHex, iters, len).
    if (strcmp(lex, "kdf.hkdf_sha256") == 0) return 27;
    if (strcmp(lex, "kdf.pbkdf2_sha256") == 0) return 28;
    // ECDSA over the NIST P-curves (RFC 6979 deterministic). sign(privHex, hashHex) -> r‖s hex;
    // verify(pubHex, hashHex, sigHex) -> "1"/"0". The hash is the message digest. ids 29-32.
    if (strcmp(lex, "crypto.ecdsa_p256_sign") == 0) return 29;
    if (strcmp(lex, "crypto.ecdsa_p256_verify") == 0) return 30;
    if (strcmp(lex, "crypto.ecdsa_p384_sign") == 0) return 31;
    if (strcmp(lex, "crypto.ecdsa_p384_verify") == 0) return 32;
    // SHAKE128/256 XOF (msg + output length). ids 33/34.
    if (strcmp(lex, "hash.shake128") == 0) return 33;
    if (strcmp(lex, "hash.shake256") == 0) return 34;
    // RSA (RFC 8017) — keys are big-endian hex (modulus n, exponent e/d), SHA-256 + MGF1.
    // PSS sign/verify (salt_len=hLen, random salt); OAEP encrypt/decrypt (random seed, empty
    // label). ids 37-40. sign->sigHex; verify->"1"/"0"; encrypt->ctHex; decrypt->msgHex|REJECT.
    if (strcmp(lex, "crypto.rsa_pss_sign") == 0) return 37;
    if (strcmp(lex, "crypto.rsa_pss_verify") == 0) return 38;
    if (strcmp(lex, "crypto.rsa_oaep_encrypt") == 0) return 39;
    if (strcmp(lex, "crypto.rsa_oaep_decrypt") == 0) return 40;
    // CSPRNG — random.bytes(n) -> n random bytes as hex. id 41.
    if (strcmp(lex, "random.bytes") == 0) return 41;
    // UUID v4 (random) / v7 (time-ordered + random) -> canonical UUID string. ids 42/43.
    // No surface args (entropy/time come from the runtime/host); the lowered $w0 is ignored.
    if (strcmp(lex, "uuid.v4") == 0) return 42;
    if (strcmp(lex, "uuid.v7") == 0) return 43;
    // Legacy hashes (insecure — interop only): in-module WAT runtimes, ids 6/7.
    if (strcmp(lex, "hash.md5") == 0) return 6;
    if (strcmp(lex, "hash.sha1") == 0) return 7;
    // UUID name-based generators (deterministic; DNS namespace) — ids 8/9.
    if (strcmp(lex, "uuid.v3") == 0) return 8;
    if (strcmp(lex, "uuid.v5") == 0) return 9;
    return -1;
}

static int is_codec_head(const Parser* p) {
    return p->current_token.type == TOKEN_IDENTIFIER &&
           codec_id_for(p->current_token.lexeme) >= 0 &&
           p->peek_token.type == TOKEN_LPAREN;
}

// Arg count of a runtime primitive (OP_CALL_RUNTIME id). Most are single-arg (codecs,
// hashes); HMAC takes (hexKey, msg). Multi-arg calls stage args 0..n-2 via OP_SETARG and
// leave the last in $w0 — the same convention OP_CALL_IMPORT uses.
static int runtime_arity(int id) {
    switch (id) {
        case 17: case 18: case 19: return 2; // hmac.sha256/384/512
        case 20: case 21: case 22: case 23: return 4; // AEAD: key, nonce, aad, msg/ct‖tag
        case 24: return 2; // ed25519_sign(seed, msg)
        case 25: return 3; // ed25519_verify(pub, msg, sig)
        case 26: return 2; // x25519(scalar, u)
        case 27: return 4; // hkdf_sha256(ikm, salt, info, len)
        case 28: return 4; // pbkdf2_sha256(pass, salt, iters, len)
        case 29: case 31: return 2; // ecdsa_p256/p384_sign(priv, hash)
        case 30: case 32: return 3; // ecdsa_p256/p384_verify(pub, hash, sig)
        case 33: case 34: return 2; // shake128/256(msg, out_len)
        case 37: return 3; // rsa_pss_sign(n, d, mhash)
        case 38: return 4; // rsa_pss_verify(n, e, mhash, sig)
        case 39: return 3; // rsa_oaep_encrypt(n, e, msg)
        case 40: return 3; // rsa_oaep_decrypt(n, d, ct)
        default: return 1;
    }
}

static void lower_base_codec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx); // recursive

// Lower a codec argument (string literal / int literal / named local / nested codec) to
// $w0. Strings/hex-strings become pointers; integer literals become immediates (e.g. the
// KDF length / iteration count). The hosted emitter marshals $w0 into the ABI arg register
// regardless of whether it is a pointer or an integer.
static void lower_codec_value(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    if (p->current_token.type == TOKEN_LIT_STR || p->current_token.type == TOKEN_STRING_LIT) {
        char* s = strip_quotes(p->current_token.lexeme);
        codegen_li_emit_sconst(b, codegen_li_add_string_constant(b, s));
        free(s);
        fe_advance(p);
    } else if (p->current_token.type == TOKEN_LIT_INT) {
        codegen_li_emit_iconst(b, atoi(p->current_token.lexeme));
        fe_advance(p);
    } else if (is_codec_head(p)) {
        lower_base_codec(b, p, ctx); // nested: base64.decode(base64.encode(x))
    } else if (p->current_token.type == TOKEN_IDENTIFIER) {
        int s = ctx ? bind_lookup(ctx->locals, ctx->nlocals, p->current_token.lexeme) : -1;
        if (s >= 0) codegen_li_emit_load_local(b, s);
        else codegen_li_emit_iconst(b, 0);
        fe_advance(p);
    } else {
        codegen_li_emit_iconst(b, 0); // unsupported arg in this subset
    }
}

static void lower_base_codec(BytecodeBuffer* b, Parser* p, const LowerCtx* ctx) {
    int id = codec_id_for(p->current_token.lexeme);      // current = "base64.encode" etc.
    if (id < 0) id = 0;
    int arity = runtime_arity(id);
    fe_advance(p);                                       // consume the dotted identifier
    if (p->current_token.type == TOKEN_LPAREN) fe_advance(p);
    // Lower each arg to $w0; stage args 0..n-2 into $a<i> via OP_SETARG, leave the last
    // in $w0. emit_native_hosted marshals staging slots + $w0 into the ABI arg registers.
    for (int i = 0; i < arity; i++) {
        lower_codec_value(b, p, ctx);                    // arg i -> $w0
        if (i < arity - 1) {
            codegen_li_emit_setarg(b, i);
            if (p->current_token.type == TOKEN_COMMA) fe_advance(p);
        }
    }
    if (p->current_token.type == TOKEN_RPAREN) fe_advance(p);
    codegen_li_emit_call_runtime(b, id);                 // $w0 = rt(args); sets uses_codec/uses_hash by id
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
                                  ImportBinding* fns, int nfns, TempAlloc* ta) {
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
        ctx.locals = NULL; // $main's named locals are a different WASM scope
        ctx.nlocals = 0;
        ctx.ta = ta;       // nested-arg spill temps (routine has no named locals)
        ta->next_temp = 0; // a routine's $v file starts fresh

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

    // Phase 12: named local variables ($v0..) declared with `let`/`mut` at top level.
    ImportBinding* locals = NULL;
    int nlocals = 0, caplocals = 0;
    TempAlloc ta; ta.next_temp = 0; ta.hw = 0; // expression-temp allocator (P12-E)
    LowerCtx top_ctx;
    top_ctx.param_name = NULL;
    top_ctx.fns = fns; top_ctx.nfns = nfns;
    top_ctx.locals = NULL; top_ctx.nlocals = 0; // refreshed before each use below
    top_ctx.ta = &ta;

    while (parser.current_token.type != TOKEN_EOF) {
        // Keep the top-level lowering context's local view current, and start temp
        // slots above the named locals so spills never clobber a `let` binding.
        top_ctx.locals = locals; top_ctx.nlocals = nlocals;
        ta.next_temp = nlocals;
        if (nlocals > ta.hw) ta.hw = nlocals;

        if ((parser.current_token.type == TOKEN_LET || parser.current_token.type == TOKEN_MUT) &&
            parser.peek_token.type == TOKEN_IDENTIFIER) {
            // `let`/`mut NAME [: type] = <initializer>` — a named local binding.
            fe_advance(&parser); // consume let/mut
            char lname[96];
            strncpy(lname, parser.current_token.lexeme, sizeof(lname) - 1);
            lname[sizeof(lname) - 1] = '\0';
            fe_advance(&parser); // consume NAME
            if (parser.current_token.type == TOKEN_COLON) { // optional ': type'
                fe_advance(&parser);
                while (parser.current_token.type != TOKEN_ASSIGN &&
                       parser.current_token.type != TOKEN_QUICK_ASSIGN &&
                       parser.current_token.type != TOKEN_SEMICOLON &&
                       parser.current_token.type != TOKEN_EOF) fe_advance(&parser);
            }
            if (parser.current_token.type == TOKEN_ASSIGN ||
                parser.current_token.type == TOKEN_QUICK_ASSIGN) fe_advance(&parser);

            // Assign (or reuse) this name's slot.
            int s = bind_lookup(locals, nlocals, lname);
            if (s < 0) { s = nlocals; bind_add(&locals, &nlocals, &caplocals, lname, s); }
            top_ctx.locals = locals; top_ctx.nlocals = nlocals;

            // Lower the initializer into $w0.
            if (parser.current_token.type == TOKEN_LIT_STR ||
                parser.current_token.type == TOKEN_STRING_LIT) {
                char* sv = strip_quotes(parser.current_token.lexeme);
                codegen_li_emit_sconst(buffer, codegen_li_add_string_constant(buffer, sv));
                free(sv);
                fe_advance(&parser);
            } else if (parser.current_token.type == TOKEN_MACRO_IDENT &&
                       is_dom_macro(parser.current_token.lexeme) &&
                       parser.peek_token.type == TOKEN_LPAREN) {
                lower_intrinsic_call(buffer, &parser, &top_ctx); // result handle -> $w0
            } else if (is_codec_head(&parser)) {
                lower_base_codec(buffer, &parser, &top_ctx);      // P12-G: result ptr -> $w0
            } else {
                // Integer expression (P12-E): literals, locals, parens, + - * / % and
                // comparisons. Temps live above the named locals ($v{nlocals}+).
                ta.next_temp = nlocals;
                if (nlocals > ta.hw) ta.hw = nlocals;
                eval_expr_prec(buffer, &parser, &top_ctx, 1, &ta);
            }
            codegen_li_emit_store_local(buffer, s); // $vs = $w0
            if (parser.current_token.type == TOKEN_SEMICOLON) fe_advance(&parser);
        } else if (parser.current_token.type == TOKEN_EXTERN) {
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
            int temps_used = 0; // codec-arg results spilled to temp locals
            while (parser.current_token.type != TOKEN_RPAREN &&
                   parser.current_token.type != TOKEN_EOF) {
                if (nargs < 16 && is_codec_head(&parser)) {
                    // base64/hex codec call as an argument (P12-G): lower eagerly and
                    // spill the result pointer to a temp local, then pass the local.
                    lower_base_codec(buffer, &parser, &top_ctx); // $w0 = result ptr
                    int t = ta.next_temp++;
                    if (ta.next_temp > ta.hw) ta.hw = ta.next_temp;
                    codegen_li_emit_store_local(buffer, t);
                    args[nargs].is_string = 0; args[nargs].sval = NULL; args[nargs].ival = 0;
                    args[nargs].is_local = 1; args[nargs].slot = t;
                    nargs++; temps_used++;
                } else if (nargs < 16 &&
                    (parser.current_token.type == TOKEN_LIT_STR ||
                     parser.current_token.type == TOKEN_STRING_LIT)) {
                    args[nargs].is_string = 1;
                    args[nargs].sval = strip_quotes(parser.current_token.lexeme);
                    args[nargs].ival = 0;
                    args[nargs].is_local = 0; args[nargs].slot = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_LIT_INT) {
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = atoi(parser.current_token.lexeme);
                    args[nargs].is_local = 0; args[nargs].slot = 0;
                    nargs++;
                    fe_advance(&parser);
                } else if (nargs < 16 && parser.current_token.type == TOKEN_IDENTIFIER &&
                           bind_lookup(locals, nlocals, parser.current_token.lexeme) >= 0) {
                    // A named local passed as a call argument (Phase 12).
                    args[nargs].is_string = 0;
                    args[nargs].sval = NULL;
                    args[nargs].ival = 0;
                    args[nargs].is_local = 1;
                    args[nargs].slot = bind_lookup(locals, nlocals, parser.current_token.lexeme);
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
            ta.next_temp -= temps_used; // free codec-arg temps

            for (int i = 0; i < nargs; i++) if (args[i].sval) free(args[i].sval);
            free(callee);
        } else {
            fe_advance(&parser);
        }
    }

    codegen_li_emit_halt(buffer); // close main

    // Emit handler bodies as table routines (after main), so @dom.on references resolve.
    // (May raise the temp high-water for nested args inside handler bodies.)
    emit_handler_routines(source, buffer, fns, nfns, &ta);

    // Phase 12: how many $v locals to declare per function — named locals plus the
    // expression/nested-arg temp high-water (across $main and the handler routines).
    buffer->local_count = (ta.hw > nlocals) ? ta.hw : nlocals;

    for (int i = 0; i < nb; i++) free(binds[i].name);
    free(binds);
    for (int i = 0; i < nfns; i++) free(fns[i].name);
    free(fns);
    for (int i = 0; i < nlocals; i++) free(locals[i].name);
    free(locals);
    return 0;
}
