#ifndef CODEGEN_METAL_H
#define CODEGEN_METAL_H

#include <stdio.h>
#include <stdint.h>
#include "../teko_target.h"
#include "../codegen_li.h"

// Context of the contiguous bare-metal emitter
typedef struct {
    FILE* file;
    TekoTarget target;
    uint32_t label_count;
} MetalContext;

// 1. APPLE ECOSYSTEM (Darwin Kernel)
void emit_darwin_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_darwin_x86_64(MetalContext* ctx, OpCode op, int32_t arg);

// 2. LINUX ECOSYSTEM (ELF ABI)
void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_mips(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_ppc64(MetalContext* ctx, OpCode op, int32_t arg);

// 3. WINDOWS ECOSYSTEM (PE/COFF Vector)
void emit_win_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 4. BSD UNIX ECOSYSTEM (Native FreeBSD)
void emit_freebsd_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_freebsd_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 5. EMBEDDED AND VIRTUAL ENVIRONMENTS (Bare-Metal)
void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg);

// Lifecycle signatures of the Central Orchestrator
MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target);
void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size);
void teko_metal_close(MetalContext* ctx);

#endif // CODEGEN_METAL_H
