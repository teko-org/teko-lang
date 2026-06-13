#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_aot_common_subexpression_elimination_filter(void) {
    const char* asm_path = "output_cse_smoke_test.s";
    TekoTarget target = { .arch = ARCH_APPLE_SILICON, .os = OS_MACOS_DARWIN };

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Adds a neutral instruction marker (OP_STORE) to isolate the bus and certify CSE
    unsigned char mock_cse_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42
        0x03,                         // OP_STORE
        0x01, 0x2A, 0x00, 0x00, 0x00  // OP_ICONST 42 (Redundant copy captured by CSE)
    };

    teko_metal_emit_program(ctx, mock_cse_bytes, sizeof(mock_cse_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(4096);
    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    char* first_mov = strstr(buffer, "mov w0, #42");
    TEST_ASSERT_NOT_NULL(first_mov);
    char* second_mov = strstr(first_mov + 11, "mov w0, #42");
    TEST_ASSERT_NULL(second_mov);

    free(buffer);
    remove(asm_path);
}

