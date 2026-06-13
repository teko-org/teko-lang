#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_constant_folding_math_collapse(void) {
    const char* asm_path = "output_folding_smoke_test.s";

    TekoTarget target;
    target.arch = ARCH_APPLE_SILICON;
    target.os = OS_MACOS_DARWIN;

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injects the sequence: OP_ICONST 20 (0x01) -> OP_ICONST 30 (0x01) -> OP_ADD (0x05)
    // The compiler MUST collapse into a single instruction: OP_ICONST 50
    unsigned char mock_folding_bytes[] = {
        0x01, 0x14, 0x00, 0x00, 0x00, // OP_ICONST 20
        0x01, 0x1E, 0x00, 0x00, 0x00, // OP_ICONST 30
        0x05                          // OP_ADD
    };

    teko_metal_emit_program(ctx, mock_folding_bytes, sizeof(mock_folding_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    memset(buffer, 0, 4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // FOLDING SUCCESS ASSERTION:
    // The load mnemonic with the collapsed value "50" MUST exist!
    TEST_ASSERT_NOT_NULL(strstr(buffer, "mov w0, #50"));

    // The original individual instructions must not have been written to the final file!
    TEST_ASSERT_NULL(strstr(buffer, "mov w0, #20"));
    TEST_ASSERT_NULL(strstr(buffer, "mov w0, #30"));

    // The physical CPU addition mnemonic was also removed because the expression died at compile time!
    TEST_ASSERT_NULL(strstr(buffer, "add w0, w0, w1"));

    free(buffer);
    remove(asm_path);
}
