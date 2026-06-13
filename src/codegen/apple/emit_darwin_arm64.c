#include "../codegen_metal.h"
#include <stdio.h>

void emit_darwin_arm64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Apple Silicon (macOS)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global _main\n");
            fprintf(ctx->file, ".align 4\n\n");
            fprintf(ctx->file, "_main:\n");
            fprintf(ctx->file, "    stp x29, x30, [sp, #-32]!\n");
            fprintf(ctx->file, "    str x19, [sp, #16]\n");
            fprintf(ctx->file, "    mov x29, sp\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    ;; [AOT Halt]: Terminates program execution.\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    mov w0, #%d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    adrp x0, .L_str_%d@PAGE\n", arg);
            fprintf(ctx->file, "    add x0, x0, .L_str_%d@PAGEOFF\n", arg);
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
            fprintf(ctx->file, "    cbz w1, .L_arm_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    sdiv w0, w0, w1\n");
            fprintf(ctx->file, "    b .L_arm_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_arm_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    mov w0, #-1\n");
            fprintf(ctx->file, ".L_arm_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    sub sp, sp, #1024\n");
            fprintf(ctx->file, "    mov x19, sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    add sp, sp, #1024\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [AOT Async Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    mov x0, sp\n");
            fprintf(ctx->file, "    bl _pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    ;; --- [AOT Channel Allocation in the Arena] ---\n");
            fprintf(ctx->file, "    mov x0, x19\n");
            fprintf(ctx->file, "    add x19, x19, #32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    mov w0, #1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    b .L_arm_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cbz w0, .L_arm_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    ldr x19, [sp, #16]\n");
            fprintf(ctx->file, "    ldp x29, x30, [sp], #32\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION: Emits the label if it is a logical instruction mapped above 100
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_arm_label_%d:\n", (int)op);
            }
            break;
    }
}
