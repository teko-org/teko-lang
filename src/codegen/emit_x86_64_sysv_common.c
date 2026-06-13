#include "emit_x86_64_sysv_common.h"
#include <stdio.h>

// Shared x86_64 System V emitter for Linux, FreeBSD and macOS. The three targets
// emit identical instructions apart from the banner, symbol decoration, string
// label, local-label tag, the halt/exit sequence, and a couple of comment lines —
// all carried in `p`.
void emit_x86_64_sysv_common(MetalContext* ctx, OpCode op, int32_t arg, const X86SysvEmitParams* p) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: %s\n", p->banner);
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global %smain\n\n", p->sym_prefix);
            fprintf(ctx->file, "%smain:\n", p->sym_prefix);
            fprintf(ctx->file, "    pushq %%rbp\n");
            fprintf(ctx->file, "    movq %%rsp, %%rbp\n");
            fprintf(ctx->file, "    pushq %%r12\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "%s", p->halt_comment);
            if (p->halt_exit_num >= 0) {
                fprintf(ctx->file, "    movq $%d, %%rax\n", p->halt_exit_num);
                fprintf(ctx->file, "    movq $0, %%rdi\n");
                fprintf(ctx->file, "    %s\n", p->halt_trap);
            }
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    movl $%d, %%eax\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    leaq %s%d(%%rip), %%rax\n", p->str_label, arg);
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
            fprintf(ctx->file, "    je .L_%s_div_zero_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    cltd\n");
            fprintf(ctx->file, "    idivl %%ebx\n");
            fprintf(ctx->file, "    jmp .L_%s_div_ok_%d\n", p->tag, ctx->label_count);
            fprintf(ctx->file, ".L_%s_div_zero_%d:\n", p->tag, ctx->label_count);
            fprintf(ctx->file, "    movl $-1, %%eax\n");
            fprintf(ctx->file, ".L_%s_div_ok_%d:\n", p->tag, ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    subq $1024, %%rsp\n");
            fprintf(ctx->file, "    movq %%rsp, %%r12\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    addq $1024, %%rsp\n");
            break;

        case OP_SPAWN_ASYNC:
            if (p->spawn_comment) fprintf(ctx->file, "%s", p->spawn_comment);
            fprintf(ctx->file, "    movq %%rsp, %%rdi\n");
            fprintf(ctx->file, "    call %spthread_create\n", p->sym_prefix);
            break;

        case OP_CHAN_INIT:
            if (p->chan_comment) fprintf(ctx->file, "%s", p->chan_comment);
            fprintf(ctx->file, "    movq %%r12, %%rax\n");
            fprintf(ctx->file, "    addq $32, %%r12\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpl $0, %%eax\n");
            fprintf(ctx->file, "    je .L_%s_label_%d\n", p->tag, arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    popq %%r12\n");
            fprintf(ctx->file, "    popq %%rbp\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_%s_label_%d:\n", p->tag, (int)op);
            }
            break;
    }
}
