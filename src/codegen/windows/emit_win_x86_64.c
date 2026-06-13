#include "../codegen_metal.h"
#include <stdio.h>

void emit_win_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Intel/AMD (64-bit Windows)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".code\n");
            fprintf(ctx->file, "main proc\n");
            fprintf(ctx->file, "    push rbp\n");
            fprintf(ctx->file, "    mov rbp, rsp\n");
            fprintf(ctx->file, "    push r12\n");
            fprintf(ctx->file, "    sub rsp, 32\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    mov rcx, 0\n");
            fprintf(ctx->file, "    call ExitProcess\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    mov eax, %d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    lea rax, [rip + .L_str_%d]\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mov ebx, eax\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mov eax, ebx\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    add eax, ebx\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    sub eax, ebx\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    imul eax, ebx\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cmp ebx, 0\n");
            fprintf(ctx->file, "    je .L_win64_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    cdq\n");
            fprintf(ctx->file, "    idiv ebx\n");
            fprintf(ctx->file, "    jmp .L_win64_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_win64_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    mov eax, -1\n");
            fprintf(ctx->file, ".L_win64_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    sub rsp, 1024\n");
            fprintf(ctx->file, "    mov r12, rsp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    add rsp, 1024\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    mov rcx, 0\n");
            fprintf(ctx->file, "    mov rdx, 0\n");
            fprintf(ctx->file, "    call CreateThread\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    mov rax, r12\n");
            fprintf(ctx->file, "    add r12, 32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    mov eax, 1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_win64_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmp eax, 0\n");
            fprintf(ctx->file, "    je .L_win64_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    add rsp, 32\n");
            fprintf(ctx->file, "    pop r12\n");
            fprintf(ctx->file, "    mov rsp, rbp\n");
            fprintf(ctx->file, "    pop rbp\n");
            fprintf(ctx->file, "    ret\n");
            fprintf(ctx->file, "main endp\n");
            break;

        default:
            // DCE RESURRECTION: Injects the x64 label above 100
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_win64_label_%d:\n", (int)op);
            }
            break;
    }
}
