#include "../codegen_metal.h"
#include <stdio.h>

void emit_freebsd_arm64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: ARM64 (FreeBSD OS)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n.align 4\n\nmain:\n");
            fprintf(ctx->file, "    stp x29, x30, [sp, #-32]!\n");
            fprintf(ctx->file, "    str x19, [sp, #16]\n");
            fprintf(ctx->file, "    mov x29, sp\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    ;; [FreeBSD ARM64 Halt]: native sys_exit via svc interrupt\n");
            fprintf(ctx->file, "    mov w0, #0\n");
            fprintf(ctx->file, "    mov x8, #1\n");
            fprintf(ctx->file, "    svc #0\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    mov w0, #%d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    adrp x0, .L_bsd_str_%d\n", arg);
            fprintf(ctx->file, "    add x0, x0, :lo12:.L_bsd_str_%d\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mov w1, w0\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mov w0, w1\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    add w0, w0, w1\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    sub w0, w0, w1\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mul w0, w0, w1\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cbz w1, .L_bsd_arm_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    sdiv w0, w0, w1\n");
            fprintf(ctx->file, "    b .L_bsd_arm_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_bsd_arm_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    mov w0, #-1\n");
            fprintf(ctx->file, ".L_bsd_arm_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    sub sp, sp, #1024\n");
            fprintf(ctx->file, "    mov x19, sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    add sp, sp, #1024\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [FreeBSD ARM64 Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    mov x0, sp\n");
            fprintf(ctx->file, "    bl pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    ;; --- [FreeBSD ARM64 Arena-Based Channel Allocation] ---\n");
            fprintf(ctx->file, "    mov x0, x19\n");
            fprintf(ctx->file, "    add x19, x19, #32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    mov w0, #1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    b .L_bsd_arm_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cbz w0, .L_bsd_arm_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    ldr x19, [sp, #16]\n");
            fprintf(ctx->file, "    ldp x29, x30, [sp], #32\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION: Maps the FreeBSD RISC label to above 100
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_bsd_arm_label_%d:\n", (int)op);
            }
            break;
    }
}
