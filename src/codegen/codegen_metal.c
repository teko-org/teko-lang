#include "codegen_metal.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target) {
    if (!output_asm_path) return NULL;

    FILE* f = fopen(output_asm_path, "w");
    if (!f) return NULL;

    MetalContext* ctx = (MetalContext*)malloc(sizeof(MetalContext));
    ctx->file = f;
    ctx->target = target;
    ctx->label_count = 0;
    return ctx;
}

static void teko_metal_route_instruction(MetalContext* ctx, OpCode op, int32_t arg) {
    auto os = ctx->target.os;
    auto arch = ctx->target.arch;

    if (os == OS_MACOS_DARWIN) {
        if (arch == ARCH_APPLE_SILICON || arch == ARCH_ARM64) {
            emit_darwin_arm64(ctx, op, arg);
            return;
        }
        if (arch == ARCH_X86_64) {
            emit_darwin_x86_64(ctx, op, arg);
            return;
        }
    }
    else if (os == OS_WINDOWS) {
        if (arch == ARCH_X86_64) { emit_win_x86_64(ctx, op, arg); return; }
        if (arch == ARCH_X86)    { emit_win_x86(ctx, op, arg); return; }
        if (arch == ARCH_ARM64)  { emit_win_arm64(ctx, op, arg); return; }
    }
    else if (os == OS_BARE_METAL || os == OS_WASI) {
        if (arch == ARCH_WASM32 || arch == ARCH_WASM64) { emit_wasm_pure(ctx, op, arg); return; }
    }
    else {
        if (os == OS_LINUX) {
            switch (arch) {
                case ARCH_X86_64: emit_linux_x86_64(ctx, op, arg); return;
                case ARCH_X86:    emit_linux_x86(ctx, op, arg); return;
                case ARCH_ARM64:  emit_linux_arm64(ctx, op, arg); return;
                case ARCH_ARM32:  emit_linux_arm32(ctx, op, arg); return;
                case ARCH_RISCV64:emit_linux_riscv64(ctx, op, arg); return;
                case ARCH_RISCV32:emit_linux_riscv32(ctx, op, arg); return;
                case ARCH_MIPS32:
                case ARCH_MIPS64: emit_linux_mips(ctx, op, arg); return;
                case ARCH_PPC64:  emit_linux_ppc64(ctx, op, arg); return;
                default: break;
            }
        }
        else {
            if (arch == ARCH_X86_64) { emit_freebsd_x86_64(ctx, op, arg); return; }
            if (arch == ARCH_ARM64)  { emit_freebsd_arm64(ctx, op, arg); return; }
        }
    }
}

static int32_t read_le_int32(const unsigned char* bytecode, uint32_t index) {
    return (bytecode[index + 0] << 0)  |
           (bytecode[index + 1] << 8)  |
           (bytecode[index + 2] << 16) |
           (bytecode[index + 3] << 24);
}

static void process_linear_il_bytes(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    uint32_t i = 0;
    bool dead_code_zone = false;

    bool accum_has_value = false;
    int32_t accum_last_value = 0;
    OpCode last_arith_op = (OpCode)0;

    unsigned char* local_il = (unsigned char*)malloc(size);
    if (!local_il) return;
    memcpy(local_il, bytecode, size);

    // STEP 1: CONSTANT FOLDING (Static collapse preprocessor)
    uint32_t scan = 0;
    while (scan < size) {
        OpCode op = (OpCode)local_il[scan];

        if (op == OP_ICONST && (scan + 10 < size)) {
            OpCode next_op = (OpCode)local_il[scan + 5];
            if (next_op == OP_ICONST) {
                OpCode arith_op = (OpCode)local_il[scan + 10];

                if (arith_op == OP_ADD || arith_op == OP_SUB || arith_op == OP_MUL || arith_op == OP_DIV) {
                    int32_t c1 = read_le_int32(local_il, scan + 1);
                    int32_t c2 = read_le_int32(local_il, scan + 6);
                    int32_t res = 0;
                    bool fold_ok = true;

                    if (arith_op == OP_ADD) res = c1 + c2;
                    else if (arith_op == OP_SUB) res = c1 - c2;
                    else if (arith_op == OP_MUL) res = c1 * c2;
                    else if (arith_op == OP_DIV) {
                        if (c2 != 0) res = c1 / c2;
                        else fold_ok = false;
                    }

                    if (fold_ok) {
                        local_il[scan + 1] = (res >> 0) & 0xFF;
                        local_il[scan + 2] = (res >> 8) & 0xFF;
                        local_il[scan + 3] = (res >> 16) & 0xFF;
                        local_il[scan + 4] = (res >> 24) & 0xFF;

                        // MASTER FIX: Fills collapsed positions with 0xCC (Neutral Marker)
                        // This protects the 0x00 byte of the legitimate OP_HALT!
                        memset(&local_il[scan + 5], 0xCC, 6);
                    }
                }
            }
        }

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) scan += 5;
        else scan += 1;
    }

    // STEP 2: COOPERATIVE EMITTER FILTER (DCE + CSE)
    while (i < size) {
        uint32_t current_op_index = i;
        OpCode op = (OpCode)local_il[i++];
        int32_t arg = 0;
        bool skip_by_cse = false;

        // STRICTLY ignores only the bytes marked by constant folding (0xCC)
        if (op == 0xCC) {
            continue;
        }

        if (op == OP_ICONST || op == OP_SCONST || op == OP_JMP || op == OP_JMP_IF_FALSE) {
            arg = read_le_int32(local_il, current_op_index + 1);
            i += 4;
        }

        if ((int)op >= 100) {
            dead_code_zone = false;
            accum_has_value = false;
            last_arith_op = (OpCode)0;
        }

        if (!dead_code_zone) {
            if (op == OP_ICONST) {
                if (accum_has_value && accum_last_value == arg && last_arith_op == (OpCode)0) {
                    skip_by_cse = true;
                } else {
                    accum_last_value = arg;
                    accum_has_value = true;
                    last_arith_op = (OpCode)0;
                }
            }
            else if (op == OP_ADD || op == OP_SUB || op == OP_MUL || op == OP_DIV) {
                if (last_arith_op == op) {
                    skip_by_cse = true;
                } else {
                    last_arith_op = op;
                    accum_has_value = false;
                }
            }
            else if (op == OP_STORE || op == OP_LOAD || op == OP_SPAWN_ASYNC || op == OP_CHAN_INIT) {
                last_arith_op = (OpCode)0;
            }
        }

        if (!dead_code_zone) {
            if (!skip_by_cse) {
                teko_metal_route_instruction(ctx, op, arg);
            }
        }

        if (op == OP_RETURN || op == OP_HALT) {
            dead_code_zone = true;
            accum_has_value = false;
            last_arith_op = (OpCode)0;
        }
    }

    free(local_il);
}

void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size) {
    if (!ctx || !bytecode || size == 0) return;

    teko_metal_route_instruction(ctx, OP_PROLOG, 0);
    process_linear_il_bytes(ctx, bytecode, size);
    teko_metal_route_instruction(ctx, OP_EPILOG, 0);
}

void teko_metal_close(MetalContext* ctx) {
    if (!ctx) return;
    if (ctx->file) {
        fclose(ctx->file);
    }
    free(ctx);
}
