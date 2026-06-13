#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_x86(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Intel x86 (32-bit Linux)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");
            fprintf(ctx->file, "    pushl %%ebp\n");
            fprintf(ctx->file, "    movl %%esp, %%ebp\n");
            fprintf(ctx->file, "    pushl %%edi\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            fprintf(ctx->file, "    movl $0, %%ebx\n");
            fprintf(ctx->file, "    int $0x80\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    movl $%d, %%eax\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    movl $.L_linux_str_%d, %%eax\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    movl %%eax, %%ebx\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    movl %%ebx, %%eax\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    addl %%ebx, %%eax\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    subl %%ebx, %%eax\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    imull %%ebx, %%eax\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cmpl $0, %%ebx\n");
            fprintf(ctx->file, "    je .L_linux32_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    cdq\n");
            fprintf(ctx->file, "    idivl %%ebx\n");
            fprintf(ctx->file, "    jmp .L_linux32_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_linux32_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    movl $-1, %%eax\n");
            fprintf(ctx->file, ".L_linux32_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    subl $512, %%esp\n");
            fprintf(ctx->file, "    movl %%esp, %%edi\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    addl $512, %%esp\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    pushl $0\n");
            fprintf(ctx->file, "    pushl %%esp\n");
            fprintf(ctx->file, "    call pthread_create\n");
            fprintf(ctx->file, "    addl $8, %%esp\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    movl %%edi, %%eax\n");
            fprintf(ctx->file, "    addl $16, %%edi\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_linux32_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpl $0, %%eax\n");
            fprintf(ctx->file, "    je .L_linux32_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    popl %%edi\n");
            fprintf(ctx->file, "    movl %%ebp, %%esp\n");
            fprintf(ctx->file, "    popl %%ebp\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION LINUX 32
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_linux32_label_%d:\n", (int)op);
            }
            break;
    }
}
