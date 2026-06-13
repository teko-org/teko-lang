#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 1. WINDOWS 32-BIT TEST (x86)
void test_teko_aot_windows_x86_32_pure_emission(void) {
    const char* asm_path = "output_win32_test.asm";
    TekoTarget target = { .arch = ARCH_X86, .os = OS_WINDOWS };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // OP_STORE in the middle shields the literals against the folding pre-processor
    unsigned char mock_win32_bytes[] = {
        0x01, 0x64, 0x00, 0x00, 0x00, // OP_ICONST 100
        0x03,                         // OP_STORE
        0x08,                         // OP_DIV
        0x00                          // OP_HALT
    };
    teko_metal_emit_program(ctx, mock_win32_bytes, sizeof(mock_win32_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "32-bit Windows"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov eax, 100"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "cdq"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "idiv ebx"));

    free(buffer);
    remove(asm_path);
}

// 2. WINDOWS 64-BIT TEST (x86_64)
void test_teko_aot_windows_x86_64_pure_emission(void) {
    const char* asm_path = "output_win64_test.asm";
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_WINDOWS };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_win64_bytes[] = { 0x12, 0x00 };
    teko_metal_emit_program(ctx, mock_win64_bytes, sizeof(mock_win64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "64-bit Windows"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sub rsp, 32"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call ExitProcess"));

    free(buffer);
    remove(asm_path);
}

// 3. WINDOWS ARM64 TEST
void test_teko_aot_windows_arm64_pure_emission(void) {
    const char* asm_path = "output_win_arm64_test.asm";

    TekoTarget target;
    target.arch = ARCH_ARM64;
    target.os = OS_WINDOWS;

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_win_arm64_bytes[] = {
        0x10,                         // OP_SPAWN_ASYNC
        0x12,                         // OP_CHAN_INIT
        0x30                          // OP_ARENA_PUSH
    };

    teko_metal_emit_program(ctx, mock_win_arm64_bytes, sizeof(mock_win_arm64_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    memset(buffer, 0, 4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM64 (Windows on ARM)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "AREA |.text|, CODE"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "stp x29, x30, [sp, #-32]!"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "bl CreateThread"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "sub sp, sp, #1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov x19, sp"));

    free(buffer);
    remove(asm_path);
}
