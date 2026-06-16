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

// Phase 14 (14.B): OP_DUPLEX_* -> teko_rt_duplex_* symbol + arity (mirrors the WASM reactor
// import table). The duplex C runtime is the single source of truth; these wrappers adapt the
// integer-handle ABI onto it (linked from libteko_rt.a).
const char* teko_native_duplex_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_DUPLEX_OPEN:  sym = "teko_rt_duplex_open";  arity = 1; break; // (capacity)
        case OP_DUPLEX_SEND:  sym = "teko_rt_duplex_send";  arity = 3; break; // (handle, ep, value)
        case OP_DUPLEX_RECV:  sym = "teko_rt_duplex_recv";  arity = 2; break; // (handle, ep)
        case OP_DUPLEX_POLL:  sym = "teko_rt_duplex_poll";  arity = 2; break; // (handle, ep)
        case OP_DUPLEX_CLOSE: sym = "teko_rt_duplex_close"; arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.C): OP_DELAYED_* -> teko_rt_delayed_* symbol + arity (mirrors the WASM reactor).
const char* teko_native_delayed_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_DELAYED_OPEN:    sym = "teko_rt_delayed_open";    arity = 1; break; // (capacity)
        case OP_DELAYED_SEND:    sym = "teko_rt_delayed_send";    arity = 3; break; // (handle, value, delay)
        case OP_DELAYED_RECV:    sym = "teko_rt_delayed_recv";    arity = 1; break; // (handle)
        case OP_DELAYED_POLL:    sym = "teko_rt_delayed_poll";    arity = 1; break; // (handle)
        case OP_DELAYED_CLOSE:   sym = "teko_rt_delayed_close";   arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.D): OP_BCAST_* -> teko_rt_bcast_* symbol + arity (mirrors the WASM reactor).
const char* teko_native_bcast_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_BCAST_OPEN:      sym = "teko_rt_bcast_open";      arity = 1; break; // (capacity)
        case OP_BCAST_SUBSCRIBE: sym = "teko_rt_bcast_subscribe"; arity = 1; break; // (handle)
        case OP_BCAST_PUBLISH:   sym = "teko_rt_bcast_publish";   arity = 2; break; // (handle, value)
        case OP_BCAST_RECV:      sym = "teko_rt_bcast_recv";      arity = 2; break; // (handle, sub_id)
        case OP_BCAST_POLL:      sym = "teko_rt_bcast_poll";      arity = 2; break; // (handle, sub_id)
        case OP_BCAST_CLOSE:     sym = "teko_rt_bcast_close";     arity = 1; break; // (handle)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.E): OP_SHARED_*/OP_ATOMIC_* -> teko_shared_*/teko_atomic_* symbol + arity. These
// C functions already use the register-width ABI, so they are called directly (no rt wrapper).
const char* teko_native_shared_symbol(OpCode op, int* out_arity) {
    int arity = 0;
    const char* sym = NULL;
    switch (op) {
        case OP_SHARED_ENTER: sym = "teko_shared_enter"; arity = 0; break;
        case OP_SHARED_LEAVE: sym = "teko_shared_leave"; arity = 0; break;
        case OP_ATOMIC_CELL:  sym = "teko_atomic_cell";  arity = 1; break; // (initial)
        case OP_ATOMIC_ADD:   sym = "teko_atomic_add";   arity = 2; break; // (handle, delta)
        case OP_ATOMIC_LOAD:  sym = "teko_atomic_load";  arity = 1; break; // (handle)
        case OP_ATOMIC_STORE: sym = "teko_atomic_store"; arity = 2; break; // (handle, value)
        default: break;
    }
    if (out_arity) *out_arity = arity;
    return sym;
}

// Phase 14 (14.F): OP_RETRY_*/OP_CIRCUIT_* -> teko_rt_retry_*/teko_rt_circuit_* symbol + arity
// (mirrors the WASM reactor import table). The teko_retry C policy runtime is the source of truth.
const char* teko_native_retry_symbol(OpCode op, int* out_arity) {
    int arity = 1;
    const char* sym = NULL;
    switch (op) {
        case OP_RETRY_NEW:             sym = "teko_rt_retry_new";             arity = 4; break;
        case OP_RETRY_SHOULD_CONTINUE: sym = "teko_rt_retry_should_continue"; arity = 2; break; // real clock
        case OP_RETRY_NEXT_DELAY:      sym = "teko_rt_retry_next_delay";      arity = 2; break;
        case OP_CIRCUIT_NEW:           sym = "teko_rt_circuit_new";           arity = 2; break;
        case OP_CIRCUIT_ALLOW:         sym = "teko_rt_circuit_allow";         arity = 1; break; // real clock
        case OP_CIRCUIT_RECORD:        sym = "teko_rt_circuit_record";        arity = 2; break; // real clock
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
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS + 1; // +1 = the spawn-args pointer slot
    int f = 8 + 8 * slots;        // first slot at rbp-16 (rbx occupies rbp-8)
    if (f % 16 != 8) f += 8;      // after `push rbp; push rbx`, sub F keeps rsp%16==0 iff F%16==8
    return f;
}
static int frame_size_arm(const MetalContext* ctx) {
    int slots = frame_locals(ctx) + TEKO_HOSTED_STAGE_SLOTS + 1; // +1 = the spawn-args pointer slot
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
// Phase 14 (14.I): the reserved slot holding the routine's spawn-args pointer (one past the stage
// slots), so OP_LOAD_SPAWN_ARG can read args[idx] anywhere in the body (callee-saved across calls).
static int argsptr_off_x86(const MetalContext* ctx) { return stage_off_x86(ctx, TEKO_HOSTED_STAGE_SLOTS); }
static int argsptr_off_arm(const MetalContext* ctx) { return stage_off_arm(ctx, TEKO_HOSTED_STAGE_SLOTS); }

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

// Emit the register-save prolog (identical frame for $main and every routine): set up the
// frame pointer + saved-register area sized by frame_size_*.
static void emit_frame_enter(MetalContext* ctx) {
    FILE* f = ctx->file;
    if (is_arm64(ctx)) {
        int fr = frame_size_arm(ctx);
        fprintf(f, "    stp x29, x30, [sp, #-%d]!\n", fr);
        fprintf(f, "    mov x29, sp\n");
        fprintf(f, "    str x19, [x29, #16]\n");
    } else {
        int fr = frame_size_x86(ctx);
        fprintf(f, "    pushq %%rbp\n");
        fprintf(f, "    movq %%rsp, %%rbp\n");
        fprintf(f, "    pushq %%rbx\n");
        fprintf(f, "    subq $%d, %%rsp\n", fr);
    }
}

// Emit the matching epilog + return (recomputes the frame size identically).
static void emit_frame_leave(MetalContext* ctx) {
    FILE* f = ctx->file;
    if (is_arm64(ctx)) {
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
}

// Emit a no-argument runtime call (used for the Phase 14 scheduler drain teko_rt_run).
static void emit_call_noarg(MetalContext* ctx, const char* sym) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    if (is_arm64(ctx)) fprintf(f, "    bl %s%s\n", pre, sym);
    else               fprintf(f, "    call %s%s%s\n", pre, sym, is_macho(ctx) ? "" : "@PLT");
}

// Phase 14 (14.A): emit the routine function-pointer table + count the native scheduler
// (teko_rt_sched.c) walks. Slots are dense (0..count-1) in declaration order, matching the
// frontend's collect_functions slot assignment and the OP_SPAWN_ASYNC slot operand.
static void emit_routine_table(MetalContext* ctx) {
    FILE* f = ctx->file;
    const char* pre = sym_prefix(ctx);
    int n = ctx->wasm_routine_count;
    if (is_macho(ctx)) fprintf(f, "    .section __DATA,__data\n");
    else               fprintf(f, "    .data\n");
    fprintf(f, "    .p2align 3\n");
    fprintf(f, "    .globl %steko_routine_table\n", pre);
    fprintf(f, "%steko_routine_table:\n", pre);
    for (int k = 0; k < n && k < 64; k++)
        fprintf(f, "    .quad %steko_routine_%d\n", pre, ctx->wasm_routine_ids[k]);
    if (n <= 0) fprintf(f, "    .quad 0\n"); // never empty (defensive)
    fprintf(f, "    .globl %steko_routine_count\n", pre);
    fprintf(f, "%steko_routine_count:\n", pre);
    fprintf(f, "    .quad %d\n", n);
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
            emit_frame_enter(ctx);
            fprintf(f, "\n");
            ctx->wasm_open = 1;          // $main open (Phase 14 multi-function tracking)
            ctx->wasm_routine_count = 0; // routine table accumulated at FUNC_BEGIN
            break;
        }

        case OP_HALT:
            // Phase 14: drain fired background tasks before $main returns (run-to-completion).
            // The frame is still live here; the actual ret is emitted when $main closes.
            if (ctx->wasm_emit_spawn) emit_call_noarg(ctx, "teko_rt_run");
            // Set the process exit status to 0; the actual teardown/ret is at the close.
            if (arm) fprintf(f, "    mov w0, #0\n");
            else     fprintf(f, "    movl $0, %%eax\n");
            break;

        // Phase 14 (14.A): green-thread body boundaries. Each routine is emitted as a
        // separate native function `teko_routine_<slot>(long args)` AFTER $main returns, so
        // FUNC_BEGIN first closes the open function (main → ret, or the previous routine),
        // then opens the new one. On entry the arg register holds the spawn-args pointer (Phase
        // 14.I); save it to the reserved frame slot so OP_LOAD_SPAWN_ARG can read args[idx] in the
        // body. $w0 also gets it (legacy: arg-less/single-arg routines that ignore it).
        case OP_FUNC_BEGIN:
            if (ctx->wasm_open == 1)      emit_frame_leave(ctx); // close $main
            else if (ctx->wasm_open == 2) emit_frame_leave(ctx); // close previous routine
            fprintf(f, "\n    .p2align %d\n", arm ? 2 : 4);
            fprintf(f, "%steko_routine_%d:\n", pre, arg);
            emit_frame_enter(ctx);
            if (arm) {
                fprintf(f, "    str x0, [x29, #%d]\n", argsptr_off_arm(ctx)); // save spawn-args ptr
            } else {
                fprintf(f, "    movq %%rdi, %d(%%rbp)\n", argsptr_off_x86(ctx)); // save spawn-args ptr
                fprintf(f, "    movq %%rdi, %%rax\n");                            // $w0 = args ptr (legacy)
            }
            if (ctx->wasm_routine_count < 64)
                ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            ctx->wasm_routine_count++;
            ctx->wasm_open = 2;
            break;

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) { emit_frame_leave(ctx); ctx->wasm_open = 0; }
            break;

        case OP_EPILOG:
        case OP_RETURN:
            // Close whatever function is still open ($main when the program has no routines;
            // already 0 for a routines program — $main closed at the first FUNC_BEGIN).
            if (ctx->wasm_open != 0) { emit_frame_leave(ctx); ctx->wasm_open = 0; }
            // Phase 14: emit the routine function-pointer table the scheduler walks.
            if (ctx->wasm_emit_spawn) emit_routine_table(ctx);
            break;

        // Phase 14 (14.A): fire the routine whose table slot is in $w0 as a background task.
        case OP_SPAWN_ASYNC:
            if (arm) {
                fprintf(f, "    mov x1, #0\n");        // arg = 0 (slot already in x0 = $w0)
                fprintf(f, "    bl %steko_rt_spawn\n", pre);
            } else {
                fprintf(f, "    movq %%rax, %%rdi\n");  // slot
                fprintf(f, "    xorl %%esi, %%esi\n");  // arg = 0
                fprintf(f, "    call %steko_rt_spawn%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;

        // Phase 14 (14.I): fire a routine with `arg` (=argc) staged arguments. Push each staged
        // arg (stage slot i) into the scheduler's pending-args buffer via teko_rt_spawn_setarg(i,v),
        // then teko_rt_spawn_args(slot=$w0) captures them + enqueues. The task receives an args
        // pointer it reads with OP_LOAD_SPAWN_ARG.
        case OP_SPAWN_ASYNC_ARGS: {
            int argc = arg;
            // Preserve the slot ($w0) in the callee-saved $w1 across the setarg calls (each clobbers
            // the accumulator register), then restore it for teko_rt_spawn_args.
            if (arm) fprintf(f, "    mov x19, x0\n");
            else     fprintf(f, "    movq %%rax, %%rbx\n");
            for (int k = 0; k < argc; k++) {
                if (arm) {
                    fprintf(f, "    mov x0, #%d\n", k);
                    fprintf(f, "    ldr x1, [x29, #%d]\n", stage_off_arm(ctx, k));
                    fprintf(f, "    bl %steko_rt_spawn_setarg\n", pre);
                } else {
                    fprintf(f, "    movl $%d, %%edi\n", k);
                    fprintf(f, "    movq %d(%%rbp), %%rsi\n", stage_off_x86(ctx, k));
                    fprintf(f, "    call %steko_rt_spawn_setarg%s\n", pre, is_macho(ctx) ? "" : "@PLT");
                }
            }
            if (arm) {
                fprintf(f, "    mov x0, x19\n");                  // slot
                fprintf(f, "    bl %steko_rt_spawn_args\n", pre);
            } else {
                fprintf(f, "    movq %%rbx, %%rdi\n");            // slot
                fprintf(f, "    call %steko_rt_spawn_args%s\n", pre, is_macho(ctx) ? "" : "@PLT");
            }
            break;
        }

        // Phase 14 (14.I): in a routine, $w0 = the idx-th spawn argument (args pointer is in the
        // reserved frame slot; each arg is a register-width long).
        case OP_LOAD_SPAWN_ARG:
            if (arm) {
                fprintf(f, "    ldr x1, [x29, #%d]\n", argsptr_off_arm(ctx)); // args ptr
                fprintf(f, "    ldr x0, [x1, #%d]\n", 8 * arg);               // args[idx]
            } else {
                fprintf(f, "    movq %d(%%rbp), %%rcx\n", argsptr_off_x86(ctx)); // args ptr
                fprintf(f, "    movq %d(%%rcx), %%rax\n", 8 * arg);             // args[idx]
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

        // Phase 14 (14.B): duplex channel ops — lower to the teko_rt_duplex_* runtime calls
        // (staging slots + $w0 marshalled by emit_call, exactly like OP_CALL_RUNTIME).
        case OP_DUPLEX_OPEN:
        case OP_DUPLEX_SEND:
        case OP_DUPLEX_RECV:
        case OP_DUPLEX_POLL:
        case OP_DUPLEX_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_duplex_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.C): delayed (timed) channel ops -> teko_rt_delayed_* runtime calls.
        case OP_DELAYED_OPEN:
        case OP_DELAYED_SEND:
        case OP_DELAYED_RECV:
        case OP_DELAYED_POLL:
        case OP_DELAYED_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_delayed_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.D): broadcast pub-sub ops -> teko_rt_bcast_* runtime calls.
        case OP_BCAST_OPEN:
        case OP_BCAST_SUBSCRIBE:
        case OP_BCAST_PUBLISH:
        case OP_BCAST_RECV:
        case OP_BCAST_POLL:
        case OP_BCAST_CLOSE: {
            int arity = 1;
            const char* sym = teko_native_bcast_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.E): shared-block lock + atomic cell ops -> teko_shared_*/teko_atomic_*.
        case OP_SHARED_ENTER:
        case OP_SHARED_LEAVE:
        case OP_ATOMIC_CELL:
        case OP_ATOMIC_ADD:
        case OP_ATOMIC_LOAD:
        case OP_ATOMIC_STORE: {
            int arity = 0;
            const char* sym = teko_native_shared_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (14.G): timespan waiters. The ms delay is in $w0. `wait` is a real synchronous
        // sleep (teko_rt_sleep_ms, always linked); `await` is a cooperative yield that drains the
        // run queue (teko_rt_await_ms, in the scheduler TU — emit_await sets uses_spawn so the
        // routine table the scheduler walks is emitted). Both take ms in arg0, return void.
        case OP_WAIT:      emit_call(ctx, "teko_rt_wait_ns", 1); break;  // real-time cooperative wait
        case OP_AWAIT_FOR: emit_call(ctx, "teko_rt_await_ns", 1); break; // real-time wait + drain

        // Phase 14 (14.F): resilience policy ops -> teko_rt_retry_*/teko_rt_circuit_* runtime calls.
        case OP_RETRY_NEW:
        case OP_RETRY_SHOULD_CONTINUE:
        case OP_RETRY_NEXT_DELAY:
        case OP_CIRCUIT_NEW:
        case OP_CIRCUIT_ALLOW:
        case OP_CIRCUIT_RECORD: {
            int arity = 1;
            const char* sym = teko_native_retry_symbol(op, &arity);
            if (sym) emit_call(ctx, sym, arity);
            break;
        }

        // Phase 14 (control-flow foundation): structured loops + branches via local asm labels.
        // The loop top is `.Lcont_<id>` (continue target) and the exit is `.Lbrk_<id>` (break
        // target); `if` skips to `.Lendif_<id>`. The id stacks resolve break/continue to the
        // innermost loop. Conditions arrive in $w0 (0 = false).
        case OP_LOOP_BEGIN: {
            int id = ctx->cf_id_next++;
            if (ctx->cf_loop_sp < 64) ctx->cf_loop_stack[ctx->cf_loop_sp++] = id;
            fprintf(f, ".Lcont_%d:\n", id);
            break;
        }
        case OP_LOOP_END: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[--ctx->cf_loop_sp] : 0;
            fprintf(f, arm ? "    b .Lcont_%d\n.Lbrk_%d:\n" : "    jmp .Lcont_%d\n.Lbrk_%d:\n",
                    id, id);
            break;
        }
        case OP_BREAK: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            fprintf(f, arm ? "    b .Lbrk_%d\n" : "    jmp .Lbrk_%d\n", id);
            break;
        }
        case OP_CONTINUE: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            fprintf(f, arm ? "    b .Lcont_%d\n" : "    jmp .Lcont_%d\n", id);
            break;
        }
        case OP_BREAK_IF_FALSE: {
            int id = (ctx->cf_loop_sp > 0) ? ctx->cf_loop_stack[ctx->cf_loop_sp - 1] : 0;
            if (arm) fprintf(f, "    cmp w0, #0\n    b.eq .Lbrk_%d\n", id);
            else     fprintf(f, "    cmpl $0, %%eax\n    je .Lbrk_%d\n", id);
            break;
        }
        case OP_IF_BEGIN: {
            int id = ctx->cf_id_next++;
            if (ctx->cf_if_sp < 64) ctx->cf_if_stack[ctx->cf_if_sp++] = id;
            if (arm) fprintf(f, "    cmp w0, #0\n    b.eq .Lendif_%d\n", id);
            else     fprintf(f, "    cmpl $0, %%eax\n    je .Lendif_%d\n", id);
            break;
        }
        case OP_IF_END: {
            int id = (ctx->cf_if_sp > 0) ? ctx->cf_if_stack[--ctx->cf_if_sp] : 0;
            fprintf(f, ".Lendif_%d:\n", id);
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
