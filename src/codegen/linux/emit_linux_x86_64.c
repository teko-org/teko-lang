#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        // ====================================================================
        // 1. INITIALIZATION DIRECTIVES, SYMBOLS AND PROLOGUE (ELF ABI LINUX)
        // ====================================================================
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: Intel/AMD (64-bit Linux)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");

            // System V AMD64 ABI Prologue: Preserves the Frame Pointer and the Arena register (%r12)
            fprintf(ctx->file, "    pushq %%rbp\n");
            fprintf(ctx->file, "    movq %%rsp, %%rbp\n");
            fprintf(ctx->file, "    pushq %%r12\n\n"); // %r12 is the callee-saved register of our Active Arena
            break;

        // ====================================================================
        // 2. LITERAL, REGISTER AND CONVERSION OPCODES
        // ====================================================================
        case OP_HALT:
            fprintf(ctx->file, "    ;; [Linux x86_64 Halt]: native sys_exit via interrupt\n");
            fprintf(ctx->file, "    movq $60, %%rax\n"); // Linux x86_64 exit syscall number is 60
            fprintf(ctx->file, "    movq $0, %%rdi\n");  // Exit code 0
            fprintf(ctx->file, "    syscall\n");
            break;

        case OP_ICONST:
            // Loads an immediate value directly into the primary accumulator %eax
            fprintf(ctx->file, "    movl $%d, %%eax\n", arg);
            break;

        case OP_SCONST:
            // Native RIP-relative addressing, mandatory for ELF PIE on Linux
            fprintf(ctx->file, "    leaq .L_linux_str_%d(%%rip), %%rax\n", arg);
            break;

        case OP_STORE:
            // Transfers the primary accumulator %eax to the base register %ebx
            fprintf(ctx->file, "    movl %%eax, %%ebx\n");
            break;

        case OP_LOAD:
            // Restores the saved value from %ebx back into %eax
            fprintf(ctx->file, "    movl %%ebx, %%eax\n");
            break;

        // ====================================================================
        // 3. CORE ARITHMETIC ENGINE AND DIVISION-BY-ZERO GUARD
        // ====================================================================
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
            // HARDWARE BARRIER: Compares the divisor %ebx with zero
            fprintf(ctx->file, "    cmpl $0, %%ebx\n");
            fprintf(ctx->file, "    je .L_linux_div_zero_%d\n", ctx->label_count);

            // x86_64 preparation: Sign-extends %eax into the %edx:%eax pair before division
            fprintf(ctx->file, "    cltd\n");
            fprintf(ctx->file, "    idivl %%ebx\n");
            fprintf(ctx->file, "    jmp .L_linux_div_ok_%d\n", ctx->label_count);

            fprintf(ctx->file, ".L_linux_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    movl $-1, %%eax\n"); // Avoids crash by returning fallback value
            fprintf(ctx->file, ".L_linux_div_ok_%d:\n", ctx->label_count);
            break;

        // ====================================================================
        // 4. REGION-BASED MEMORY MANAGEMENT (NATIVE ARENA O(1))
        // ====================================================================
        case OP_ARENA_PUSH:
            // Allocates a contiguous 1KB block by decrementing the Stack Pointer (%rsp)
            fprintf(ctx->file, "    subq $1024, %%rsp\n");
            // %r12 becomes the contiguous static cursor of the Arena
            fprintf(ctx->file, "    movq %%rsp, %%r12\n");
            break;

        case OP_ARENA_POP:
            // Atomic O(1) deallocation by restoring the stack
            fprintf(ctx->file, "    addq $1024, %%rsp\n");
            break;

        // ====================================================================
        // 5. PARALLELISM AND CHANNELS (INTRINSICS INTEGRATED WITH LINUX THREADS)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    ;; --- [Linux AOT Coroutine Spawn via pthread_create] ---\n");
            fprintf(ctx->file, "    movq %%rsp, %%rdi\n");
            fprintf(ctx->file, "    call pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    ;; --- [Linux AOT Arena-Based Channel Allocation] ---\n");
            fprintf(ctx->file, "    movq %%r12, %%rax\n");
            fprintf(ctx->file, "    addq $32, %%r12\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    movl $1, %%eax\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        // ====================================================================
        // 6. CONTROL FLOW AND SUBROUTINE RETURN
        // ====================================================================
        case OP_JMP:
            fprintf(ctx->file, "    jmp .L_linux_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpl $0, %%eax\n");
            fprintf(ctx->file, "    je .L_linux_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            // Epilogue: Restores the original Arena %r12 and tears down the stack frame
            fprintf(ctx->file, "    popq %%r12\n");
            fprintf(ctx->file, "    popq %%rbp\n");
            fprintf(ctx->file, "    ret\n");
            break;

        default:
            // DCE RESURRECTION LINUX X86_64
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_linux_label_%d:\n", (int)op);
            }
            break;
    }
}
