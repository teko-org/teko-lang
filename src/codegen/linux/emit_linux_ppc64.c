#include "../codegen_metal.h"
#include <stdio.h>

void emit_linux_ppc64(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;

    switch (op) {
        case OP_PROLOG:
            fprintf(ctx->file, ";; ==================================================\n");
            fprintf(ctx->file, ";; Teko AOT Compiler - Target: PowerPC 64-bit (Linux)\n");
            fprintf(ctx->file, ";; ==================================================\n\n");
            fprintf(ctx->file, ".global main\n\n");
            fprintf(ctx->file, "main:\n");
            fprintf(ctx->file, "    stdu 1, -48(1)\n");
            fprintf(ctx->file, "    mflr 0\n");
            fprintf(ctx->file, "    std 0, 64(1)\n");
            fprintf(ctx->file, "    std 31, 40(1)\n\n");
            break;

        case OP_HALT:
            fprintf(ctx->file, "    li 0, 1\n");
            fprintf(ctx->file, "    li 3, 0\n");
            fprintf(ctx->file, "    sc\n");
            break;

        case OP_ICONST:
            fprintf(ctx->file, "    li 3, %d\n", arg);
            break;

        case OP_SCONST:
            fprintf(ctx->file, "    la 3, .L_linux_str_%d@l(3)\n", arg);
            break;

        case OP_STORE:
            fprintf(ctx->file, "    mr 4, 3\n");
            break;

        case OP_LOAD:
            fprintf(ctx->file, "    mr 3, 4\n");
            break;

        case OP_ADD:
            fprintf(ctx->file, "    add 3, 3, 4\n");
            break;

        case OP_SUB:
            fprintf(ctx->file, "    subf 3, 4, 3\n");
            break;

        case OP_MUL:
            fprintf(ctx->file, "    mulld 3, 3, 4\n");
            break;

        case OP_DIV:
            ctx->label_count++;
            fprintf(ctx->file, "    cmpwi 4, 0\n");
            fprintf(ctx->file, "    beq .L_ppc64_div_zero_%d\n", ctx->label_count);
            fprintf(ctx->file, "    divd 3, 3, 4\n");
            fprintf(ctx->file, "    b .L_ppc64_div_ok_%d\n", ctx->label_count);
            fprintf(ctx->file, ".L_ppc64_div_zero_%d:\n", ctx->label_count);
            fprintf(ctx->file, "    li 3, -1\n");
            fprintf(ctx->file, ".L_ppc64_div_ok_%d:\n", ctx->label_count);
            break;

        case OP_ARENA_PUSH:
            fprintf(ctx->file, "    stdu 1, -1024(1)\n");
            fprintf(ctx->file, "    mr 31, 1\n");
            break;

        case OP_ARENA_POP:
            fprintf(ctx->file, "    ld 1, 0(1)\n");
            break;

        case OP_SPAWN_ASYNC:
            fprintf(ctx->file, "    mr 3, 1\n");
            fprintf(ctx->file, "    bl pthread_create\n");
            break;

        case OP_CHAN_INIT:
            fprintf(ctx->file, "    mr 3, 31\n");
            fprintf(ctx->file, "    addi 31, 31, 32\n");
            break;

        case OP_CHAN_PUT:
            fprintf(ctx->file, "    li 3, 1\n");
            break;

        case OP_AWAIT_INTENT:
            break;

        case OP_JMP:
            fprintf(ctx->file, "    b .L_ppc64_label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(ctx->file, "    cmpwi 3, 0\n");
            fprintf(ctx->file, "    beq .L_ppc64_label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG:
            fprintf(ctx->file, "    ld 31, 40(1)\n");
            fprintf(ctx->file, "    ld 0, 64(1)\n");
            fprintf(ctx->file, "    mtlr 0\n");
            fprintf(ctx->file, "    addi 1, 1, 48\n");
            fprintf(ctx->file, "    blr\n");
            break;

        default:
            // DCE RESURRECTION PPC64
            if ((int)op >= 100) {
                fprintf(ctx->file, ".L_ppc64_label_%d:\n", (int)op);
            }
            break;
    }
}
