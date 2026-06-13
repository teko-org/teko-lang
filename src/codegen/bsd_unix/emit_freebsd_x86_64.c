#include "../codegen_metal.h"
#include <stdio.h>

void emit_freebsd_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Intel/AMD (64-bit FreeBSD)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");

            fprintf(ctx->file, "    pushq %%rbp\n");
            fprintf(ctx->file, "    movq %%rsp, %%rbp\n");
            fprintf(ctx->file, "    pushq %%r12\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    ;; [FreeBSD Halt]: native sys_exit via BSD interrupt\n");
            fprintf(ctx->file, "    movq $1, %%rax\n");
            fprintf(ctx->file, "    movq $0, %%rdi\n");
            fprintf(ctx->file, "    int $0x80\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    movl $%d, %%eax\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    leaq .L_str_%d(%%rip), %%rax\n", arg);
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
            fprintf(ctx->file, "    je .L_bsd_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    cltd\n");
            fprintf(ctx->file, "    idivl %%ebx\n");
            fprintf(ctx->file, "    jmp .L_bsd_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_bsd_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    movl $-1, %%eax\n");
            fprintf(ctx->file, ".L_bsd_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    subq $1024, %%rsp\n");
            fprintf(ctx->file, "    movq %%rsp, %%r12\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    addq $1024, %%rsp\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [FreeBSD AOT Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    movq %%rsp, %%rdi\n");
            fprintf(ctx->file, "    call pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    ;; --- [FreeBSD x86_64 Arena-Based Channel Allocation] ---\n");
            fprintf(ctx->file, "    movq %%r12, %%rax\n");
            fprintf(ctx->file, "    addq $32, %%r12\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_bsd_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpl $0, %%eax\n");
            fprintf(ctx->file, "    je .L_bsd_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    popq %%r12\n");
            fprintf(ctx->file, "    popq %%rbp\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION: Ensures label generation when the byte is a logical instruction
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_bsd_label_%d:\n", (int)op);
            }
            break;
    }
}
