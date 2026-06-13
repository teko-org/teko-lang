#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 1. LINUX INTEL 64-BIT TEST
void test_teko_aot_linux_x86_64_pure_emission(void) {
    const char* asm_path = "output_linux_test.s";
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_LINUX };
    strncpy(target.target_string, "x86_64-unknown-linux-gnu", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_program[] = { 0x10, 0x12, 0x00 }; // Spawn -> Channel -> Halt
    teko_metal_emit_program(ctx, mock_linux_program, sizeof(mock_linux_program));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "64-bit Linux"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "syscall"));

    free(buffer);
    remove(asm_path);
}

// 2. LINUX INTEL 32-BIT TEST
void test_teko_aot_linux_x86_32_pure_emission(void) {
    const char* asm_path = "output_linux_x86_test.s";
    TekoTarget target = { .arch = ARCH_X86, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injects a constant and operations to ensure correct loading in the 32-bit x86 accumulator
    unsigned char mock_linux32_bytes[] = {
        0x01, 0x0A, 0x00, 0x00, 0x00, // OP_ICONST 10 (5 bytes)
        0x10,                         // OP_SPAWN_ASYNC (1 byte)
        0x00                          // OP_HALT (1 byte)
    };
    teko_metal_emit_program(ctx, mock_linux32_bytes, sizeof(mock_linux32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "Intel x86 (32-bit Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "pushl %edi"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "int $0x80"));

    free(buffer);
    remove(asm_path);
}

// 3. LINUX RISC-V 64-BIT TEST
void test_teko_aot_linux_riscv64_pure_emission(void) {
    const char* asm_path = "output_linux_rv64_test.s";
    TekoTarget target = { .arch = ARCH_RISCV64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_rv64_bytes[] = {
        0x30,                         // OP_ARENA_PUSH
        0x01, 0x05, 0x00, 0x00, 0x00, // OP_ICONST 5
        0x00                          // OP_HALT
    };
    teko_metal_emit_program(ctx, mock_rv64_bytes, sizeof(mock_rv64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "RISC-V 64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sd ra, 24(sp)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "addi sp, sp, -1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "ecall"));

    free(buffer);
    remove(asm_path);
}

// 4. LINUX RISC-V 32-BIT TEST
void test_teko_aot_linux_riscv32_pure_emission(void) {
    const char* asm_path = "output_linux_rv32_test.s";
    TekoTarget target = { .arch = ARCH_RISCV32, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_rv32_bytes[] = { 0x01, 0x0A, 0x00, 0x00, 0x00, 0x30, 0x00 };
    teko_metal_emit_program(ctx, mock_rv32_bytes, sizeof(mock_rv32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "RISC-V 32-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sw ra, 12(sp)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "li a0, 10"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "lw ra, 12(sp)"));

    free(buffer);
    remove(asm_path);
}

// 5. LINUX POWERPC 64-BIT TEST
void test_teko_aot_linux_ppc64_pure_emission(void) {
    const char* asm_path = "output_linux_ppc64_test.s";
    TekoTarget target = { .arch = ARCH_PPC64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_ppc64_bytes[] = {
        0x01, 0x4D, 0x00, 0x00, 0x00, // OP_ICONST 77
        0x03,                         // OP_STORE
        0x00                          // OP_HALT
    };
    teko_metal_emit_program(ctx, mock_ppc64_bytes, sizeof(mock_ppc64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "PowerPC 64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stdu 1, -48(1)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "li 3, 77"));
    // Validates that the PPC64 sc interrupt was generated by Halt
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sc"));

    free(buffer);
    remove(asm_path);
}

// 6. LINUX MIPS TEST
void test_teko_aot_linux_mips_pure_emission(void) {
    const char* asm_path = "output_linux_mips_test.s";
    TekoTarget target = { .arch = ARCH_MIPS64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_mips_bytes[] = { 0x01, 0x1E, 0x00, 0x00, 0x00, 0x00 }; // Iconst 30 -> Halt
    teko_metal_emit_program(ctx, mock_mips_bytes, sizeof(mock_mips_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "MIPS 32/64-bit (Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "addiu $sp, $sp, -32"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "syscall"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "nop"));

    free(buffer);
    remove(asm_path);
}

// 7. LINUX ARM64 TEST
void test_teko_aot_linux_arm64_pure_emission(void) {
    const char* asm_path = "output_linux_arm64_test.s";
    TekoTarget target = { .arch = ARCH_ARM64, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_arm64_bytes[] = {
        0x12,                         // OP_CHAN_INIT
        0x01, 0x0F, 0x00, 0x00, 0x00, // OP_ICONST 15
        0x00                          // OP_HALT
    };
    teko_metal_emit_program(ctx, mock_linux_arm64_bytes, sizeof(mock_linux_arm64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM64 (64-bit Linux)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stp x29, x30, [sp, #-32]!"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov w0, #15"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov x8, #93"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "svc #0"));

    free(buffer);
    remove(asm_path);
}

// 8. LINUX ARM32 TEST
void test_teko_aot_linux_arm32_pure_emission(void) {
    const char* asm_path = "output_linux_arm32_test.s";
    TekoTarget target = { .arch = ARCH_ARM32, .os = OS_LINUX };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_linux_arm32_bytes[] = {
        0x12,                         // OP_CHAN_INIT
        0x01, 0x05, 0x00, 0x00, 0x00, // OP_ICONST 5
        0x00                          // OP_HALT
    };
    teko_metal_emit_program(ctx, mock_linux_arm32_bytes, sizeof(mock_linux_arm32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM32 (32-bit Linux ARMv7)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "push {r4, fp, lr}"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov r7, #1"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "svc 0"));

    free(buffer);
    remove(asm_path);
}
