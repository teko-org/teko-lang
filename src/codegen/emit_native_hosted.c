#include "emit_native_hosted.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

// See emit_native_hosted.h. Self-contained, libc-hosted, assemble-able emitter for the
// host arches. Selected by ctx->hosted; the freestanding emitters are left untouched.

#define TEKO_HOSTED_STAGE_SLOTS 8 // OP_SETARG staging slots (max import/runtime arity)

// --- OP_CALL_RUNTIME dispatch table (mirrors emit_wasm.c) ------------------------
const char* teko_native_runtime_symbol(int32_t id, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (id) {
        case 0: sym = "teko_rt_base64_encode"; break;
        case 1: sym = "teko_rt_base64_decode"; break;
        case 2: sym = "teko_rt_hex_encode";    break;
        case 3: sym = "teko_rt_hex_decode";    break;
        case 4: sym = "teko_rt_sha256_hex";    break; // Phase 13.1
        case 5: sym = "teko_rt_sha512_hex";    break;
        case 10: sym = "teko_rt_sha384_hex";   break;
        case 11: sym = "teko_rt_sha3_256_hex"; break;
        case 12: sym = "teko_rt_sha3_512_hex"; break;
        case 15: sym = "teko_rt_blake3_hex";   break;
        case 16: sym = "teko_rt_blake2b_hex";  break;
        case 17: sym = "teko_rt_hmac_sha256";  arity = 2; break; // (hexKey, msg)
        case 18: sym = "teko_rt_hmac_sha384";  arity = 2; break;
        case 19: sym = "teko_rt_hmac_sha512";  arity = 2; break;
        case 20: sym = "teko_rt_aes_gcm_seal";            arity = 4; break; // (key,nonce,aad,pt)
        case 21: sym = "teko_rt_aes_gcm_open";            arity = 4; break; // (key,nonce,aad,ct‖tag)
        case 22: sym = "teko_rt_chacha20poly1305_seal";   arity = 4; break;
        case 23: sym = "teko_rt_chacha20poly1305_open";   arity = 4; break;
        case 24: sym = "teko_rt_ed25519_sign";   arity = 2; break; // (seed, msg)
        case 25: sym = "teko_rt_ed25519_verify"; arity = 3; break; // (pub, msg, sig)
        case 26: sym = "teko_rt_x25519";         arity = 2; break; // (scalar, u)
        case 27: sym = "teko_rt_hkdf_sha256";    arity = 4; break; // (ikm, salt, info, len)
        case 28: sym = "teko_rt_pbkdf2_sha256";  arity = 4; break; // (pass, salt, iters, len)
        case 29: sym = "teko_rt_ecdsa_p256_sign";   arity = 2; break; // (priv, hash)
        case 30: sym = "teko_rt_ecdsa_p256_verify"; arity = 3; break; // (pub, hash, sig)
        case 31: sym = "teko_rt_ecdsa_p384_sign";   arity = 2; break;
        case 32: sym = "teko_rt_ecdsa_p384_verify"; arity = 3; break;
        case 33: sym = "teko_rt_shake128"; arity = 2; break; // (msg, out_len)
        case 34: sym = "teko_rt_shake256"; arity = 2; break;
        case 37: sym = "teko_rt_rsa_pss_sign";     arity = 3; break; // (n, d, mhash)
        case 38: sym = "teko_rt_rsa_pss_verify";   arity = 4; break; // (n, e, mhash, sig)
        case 39: sym = "teko_rt_rsa_oaep_encrypt"; arity = 3; break; // (n, e, msg)
        case 40: sym = "teko_rt_rsa_oaep_decrypt"; arity = 3; break; // (n, d, ct)
        case 41: sym = "teko_rt_random_bytes";     arity = 1; break; // (n)
        case 42: sym = "teko_rt_uuid_v4";          arity = 1; break; // ignored arg
        case 43: sym = "teko_rt_uuid_v7";          arity = 1; break; // ignored arg
        case 6: sym = "teko_rt_md5_hex";       break; // legacy
        case 7: sym = "teko_rt_sha1_hex";      break; // legacy
        case 8: sym = "teko_rt_uuid_v3";       break;
        case 9: sym = "teko_rt_uuid_v5";       break;
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// --- helpers ---------------------------------------------------------------------
static int is_macho(const MetalContext* ctx) { return ctx->target.os == OS_MACOS_DARWIN; }
static int is_arm64(const MetalContext* ctx) {
    return ctx->target.arch == ARCH_ARM64 || ctx->target.arch == ARCH_APPLE_SILICON;
}
static const char* sym_prefix(const MetalContext* ctx) { return is_macho(ctx) ? "_" : ""; }

// Stack-frame geometry (recomputed identically at PROLOG and EPILOG; deterministic).
static int frame_locals(const MetalContext* ctx) {
    return ctx->wasm_local_count > 0 ? ctx->wasm_local_count : 0;
}
static int frame_size_x86(const MetalContext* ctx) {
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS;
    int f = 8 + 8 * slots;        // first slot at rbp-16 (rbx occupies rbp-8)
    if (f % 16 != 8) f += 8;      // after `push rbp; push rbx`, sub F keeps rsp%16==0 iff F%16==8
    return f;
}
static int frame_size_arm(const MetalContext* ctx) {
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS;
    int f = 32 + 8 * slots;       // saved x29/x30/x19 occupy 0/8/16
    return (f + 15) & ~15;        // 16-aligned for stp/AAPCS
}
static int local_off_x86(int n) { return -(16 + 8 * n); }
static int stage_off_x86(const MetalContext* ctx, int i) {
    return -(16 + 8 * frame_locals(ctx) + 8 * i);
}
static int local_off_arm(int n) { return 32 + 8 * n; }
static int stage_off_arm(const MetalContext* ctx, int i) {
    return 32 + 8 * frame_locals(ctx) + 8 * i;
}

// Emit one C string into the active data section as an escaped .asciz directive.
static void emit_asciz(FILE* f, const char* s) {
    fputs("    .asciz \"", f);
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        if (c == '\\' || c == '"') { fputc('\\', f); fputc(c, f); }
        else if (c >= 0x20 && c < 0x7f) { fputc(c, f); }
        else { fprintf(f, "\\%03o", c); } // octal escape — portable across GAS/clang
    }
    fputs("\"\n", f);
}

// SysV/AAPCS argument registers.
static const char* X86_ARG_REGS[6] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };

// Emit a marshalled call: staging slots 0..arity-2 -> arg regs 0..arity-2, the live
// accumulator ($w0) -> arg reg arity-1, then call `sym`. Result (if any) stays in $w0.
static void emit_call(MetalContext* ctx, const char* sym, int arity) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    if (arity < 0) arity = 0;
    if (arity > TEKO_HOSTED_STAGE_SLOTS) arity = TEKO_HOSTED_STAGE_SLOTS;

    if (is_arm64(ctx)) {
        // live arg ($w0 = x0) -> last reg; skip the x0->x0 self-move for arity 1.
        if (arity >= 2) fprintf(f, "    mov x%d, x0\n", arity - 1);
        for (int i = 0; i < arity - 1; i++)
            fprintf(f, "    ldr x%d, [x29, #%d]\n", i, stage_off_arm(ctx, i));
        fprintf(f, "    bl %s%s\n", pre, sym);
    } else {
        if (arity >= 1) fprintf(f, "    movq %%rax, %s\n", X86_ARG_REGS[arity - 1]);
        for (int i = 0; i < arity - 1; i++)
            fprintf(f, "    movq %d(%%rbp), %s\n", stage_off_x86(ctx, i), X86_ARG_REGS[i]);
        // PLT on ELF (PIE-safe); direct on Mach-O (PC-relative stub).
        fprintf(f, "    call %s%s%s\n", pre, sym, is_macho(ctx) ? "" : "@PLT");
    }
}

// --- the emitter -----------------------------------------------------------------
void emit_native_hosted(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;
    FILE* f = ctx->file;
    int arm = is_arm64(ctx);
    const char* pre = sym_prefix(ctx);

    switch (op) {
        case OP_PROLOG: {
            // Comment leader: '#' for x86 AT&T/GAS, '//' for arm64 (both ELF & Mach-O).
            // ';;' is invalid on the GNU x86_64 assembler, so do not reuse the
            // freestanding emitters' banner style here (this output is really assembled).
            const char* cm = arm ? "//" : "#";
            fprintf(f, "%s ==================================================\n", cm);
            fprintf(f, "%s Teko AOT Compiler - Hosted Native Runner (%s)\n", cm, ctx->target.target_string);
            fprintf(f, "%s ==================================================\n\n", cm);
            // Constant string pool -> read-only data.
            if (ctx->wasm_string_count > 0 && ctx->wasm_strings) {
                if (is_macho(ctx)) fprintf(f, "    .section __TEXT,__cstring,cstring_literals\n");
                else               fprintf(f, "    .section .rodata\n");
                for (int i = 0; i < ctx->wasm_string_count; i++) {
                    fprintf(f, ".L_str_%d:\n", i);
                    emit_asciz(f, ctx->wasm_strings[i] ? ctx->wasm_strings[i] : "");
                }
                fprintf(f, "\n");
            }
            // Text section + main.
            fprintf(f, "    .text\n");
            fprintf(f, "    .globl %smain\n", pre);
            fprintf(f, "    .p2align %d\n", arm ? 2 : 4);
            fprintf(f, "%smain:\n", pre);
            if (arm) {
                int fr = frame_size_arm(ctx);
                fprintf(f, "    stp x29, x30, [sp, #-%d]!\n", fr);
                fprintf(f, "    mov x29, sp\n");
                fprintf(f, "    str x19, [x29, #16]\n\n");
            } else {
                int fr = frame_size_x86(ctx);
                fprintf(f, "    pushq %%rbp\n");
                fprintf(f, "    movq %%rsp, %%rbp\n");
                fprintf(f, "    pushq %%rbx\n");
                fprintf(f, "    subq $%d, %%rsp\n\n", fr);
            }
            break;
        }

        case OP_HALT:
            // Set the process exit status to 0; the actual teardown/ret is at EPILOG.
            if (arm) fprintf(f, "    mov w0, #0\n");
            else     fprintf(f, "    movl $0, %%eax\n");
            break;

        case OP_EPILOG:
        case OP_RETURN:
            if (arm) {
                int fr = frame_size_arm(ctx);
                fprintf(f, "    ldr x19, [x29, #16]\n");
                fprintf(f, "    ldp x29, x30, [sp], #%d\n", fr);
                fprintf(f, "    ret\n");
            } else {
                int fr = frame_size_x86(ctx);
                fprintf(f, "    addq $%d, %%rsp\n", fr);
                fprintf(f, "    popq %%rbx\n");
                fprintf(f, "    popq %%rbp\n");
                fprintf(f, "    ret\n");
            }
            break;

        case OP_ICONST:
            if (arm) {
                uint32_t v = (uint32_t)arg;
                fprintf(f, "    movz w0, #%u\n", v & 0xFFFFu);
                if (v >> 16) fprintf(f, "    movk w0, #%u, lsl #16\n", v >> 16);
            } else {
                fprintf(f, "    movl $%d, %%eax\n", arg);
            }
            break;

        case OP_SCONST:
            if (arm) {
                if (is_macho(ctx)) {
                    fprintf(f, "    adrp x0, .L_str_%d@PAGE\n", arg);
                    fprintf(f, "    add x0, x0, .L_str_%d@PAGEOFF\n", arg);
                } else {
                    fprintf(f, "    adrp x0, .L_str_%d\n", arg);
                    fprintf(f, "    add x0, x0, :lo12:.L_str_%d\n", arg);
                }
            } else {
                fprintf(f, "    leaq .L_str_%d(%%rip), %%rax\n", arg);
            }
            break;

        case OP_STORE: // $w1 <- $w0
            if (arm) fprintf(f, "    mov x19, x0\n");
            else     fprintf(f, "    movq %%rax, %%rbx\n");
            break;

        case OP_LOAD: // $w0 <- $w1
            if (arm) fprintf(f, "    mov x0, x19\n");
            else     fprintf(f, "    movq %%rbx, %%rax\n");
            break;

        case OP_STORE_LOCAL:
            if (arm) fprintf(f, "    str x0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movq %%rax, %d(%%rbp)\n", local_off_x86(arg));
            break;

        case OP_LOAD_LOCAL:
            if (arm) fprintf(f, "    ldr x0, [x29, #%d]\n", local_off_arm(arg));
            else     fprintf(f, "    movq %d(%%rbp), %%rax\n", local_off_x86(arg));
            break;

        case OP_SETARG:
            if (arm) fprintf(f, "    str x0, [x29, #%d]\n", stage_off_arm(ctx, arg));
            else     fprintf(f, "    movq %%rax, %d(%%rbp)\n", stage_off_x86(ctx, arg));
            break;

        case OP_CALL_IMPORT: {
            int arity = 1;
            const char* name = NULL;
            if (ctx->wasm_imports && arg >= 0 && arg < ctx->wasm_import_count) {
                arity = ctx->wasm_imports[arg].n_params;
                name = ctx->wasm_imports[arg].name;
            }
            if (name) emit_call(ctx, name, arity);
            break;
        }

        case OP_CALL_RUNTIME: {
            int arity = 1;
            const char* sym = teko_native_runtime_symbol(arg, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Integer ALU ($w0 = $w0 <op> $w1). Pointers use x; arithmetic uses w.
        case OP_ADD: fprintf(f, arm ? "    add w0, w0, w19\n"  : "    addl %%ebx, %%eax\n"); break;
        case OP_SUB: fprintf(f, arm ? "    sub w0, w0, w19\n"  : "    subl %%ebx, %%eax\n"); break;
        case OP_MUL: fprintf(f, arm ? "    mul w0, w0, w19\n"  : "    imull %%ebx, %%eax\n"); break;
        case OP_DIV:
            if (arm) {
                fprintf(f, "    sdiv w0, w0, w19\n");
            } else {
                fprintf(f, "    cltd\n    idivl %%ebx\n");
            }
            break;
        case OP_MOD:
            if (arm) {
                fprintf(f, "    sdiv w1, w0, w19\n    msub w0, w1, w19, w0\n");
            } else {
                fprintf(f, "    cltd\n    idivl %%ebx\n    movl %%edx, %%eax\n");
            }
            break;
        case OP_EQ: case OP_NE: case OP_LT: case OP_LE: case OP_GT: case OP_GE: {
            // $w0 = ($w0 <cmp> $w1) ? 1 : 0
            if (arm) {
                const char* cc = (op == OP_EQ) ? "eq" : (op == OP_NE) ? "ne" :
                                 (op == OP_LT) ? "lt" : (op == OP_LE) ? "le" :
                                 (op == OP_GT) ? "gt" : "ge";
                fprintf(f, "    cmp w0, w19\n    cset w0, %s\n", cc);
            } else {
                const char* cc = (op == OP_EQ) ? "sete" : (op == OP_NE) ? "setne" :
                                 (op == OP_LT) ? "setl" : (op == OP_LE) ? "setle" :
                                 (op == OP_GT) ? "setg" : "setge";
                fprintf(f, "    cmpl %%ebx, %%eax\n    %s %%al\n    movzbl %%al, %%eax\n", cc);
            }
            break;
        }

        default:
            break; // unsupported opcode in the hosted subset (straight-line surface)
    }
}
