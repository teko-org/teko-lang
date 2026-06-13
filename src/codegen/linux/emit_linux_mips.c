#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_mips(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: MIPS 32/64-bit (Linux)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");
            fprintf(ctx->file, "    addiu $sp, $sp, -32\n");
            fprintf(ctx->file, "    sw $ra, 28($sp)\n");
            fprintf(ctx->file, "    sw $s1, 24($sp)\n");
            fprintf(ctx->file, "    move $s1, $sp\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    li $a0, 0\n");
            fprintf(ctx->file, "    li $v0, 4001\n");
            fprintf(ctx->file, "    syscall\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    li $v0, %d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    la $v0, .L_linux_str_%d\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    move $v1, $v0\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    move $v0, $v1\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    addu $v0, $v0, $v1\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    subu $v0, $v0, $v1\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mul $v0, $v0, $v1\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    beq $v1, $0, .L_mips_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    nop\n");
            fprintf(ctx->file, "    div $v0, $v1\n");
            fprintf(ctx->file, "    mflo $v0\n");
            fprintf(ctx->file, "    j .L_mips_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, "    nop\n");
            fprintf(ctx->file, ".L_mips_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    li $v0, -1\n");
            fprintf(ctx->file, ".L_mips_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    addiu $sp, $sp, -1024\n");
            fprintf(ctx->file, "    move $s1, $sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    addiu $sp, $sp, 1024\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    move $a0, $sp\n");
            fprintf(ctx->file, "    jal pthread_create\n");
            fprintf(ctx->file, "    nop\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    move $v0, $s1\n");
            fprintf(ctx->file, "    addiu $s1, $s1, 32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    li $v0, 1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    j .L_mips_label_%d\n", arg);
            fprintf(ctx->file, "    nop\n");
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    beq $v0, $0, .L_mips_label_%d\n", arg);
            fprintf(ctx->file, "    nop\n");
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    lw $ra, 28($sp)\n");
            fprintf(ctx->file, "    lw $s1, 24($sp)\n");
            fprintf(ctx->file, "    addiu $sp, $sp, 32\n");
            fprintf(ctx->file, "    jr $ra\n");
            fprintf(ctx->file, "    nop\n");
            break;

        default:
            // DCE RESURRECTION MIPS
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_mips_label_%d:\n", (int)op);
            }
            break;
    }
}
