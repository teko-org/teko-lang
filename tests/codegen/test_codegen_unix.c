#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_freebsd_x86_64_pure_emission(void) {
    const char* asm_path = "output_freebsd_test.s";
    TekoTarget target = { .arch = ARCH_X86_64, .os = OS_UNKNOWN };
    strncpy(target.target_string, "x86_64-unknown-freebsd", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Loads channel and concurrency without stacking vulnerable numeric constants
    unsigned char mock_bsd_bytes[] = { 0x12, 0x10, 0x00 };
    teko_metal_emit_program(ctx, mock_bsd_bytes, sizeof(mock_bsd_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "64-bit FreeBSD"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call pthread_create"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "int $0x80"));

    free(buffer);
    remove(asm_path);
}

void test_teko_aot_freebsd_arm64_pure_emission(void) {
    const char* asm_path = "output_freebsd_arm64_test.s";
    TekoTarget target = { .arch = ARCH_ARM64, .os = OS_UNKNOWN };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    unsigned char mock_bsd_bytes[] = { 0x12, 0x00 };
    teko_metal_emit_program(ctx, mock_bsd_bytes, sizeof(mock_bsd_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    TEST_ASSERT_NOT_NULL(strstr(buffer, "ARM64 (FreeBSD OS)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "main:"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov x8, #1"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "svc #0"));

    free(buffer);
    remove(asm_path);
}
