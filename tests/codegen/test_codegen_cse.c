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

    // Adds a genuinely accumulator-neutral instruction (OP_STORE = 0x04: $w0 -> $w1,
    // leaving $w0 intact) between two identical constants, so CSE may drop the
    // second load. (OP_LOAD = 0x03 would *clobber* $w0 from $w1 and is intentionally
    // NOT eliminable across — see the accumulator-clobber guard in codegen_metal.c.)
    unsigned char mock_cse_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42
        0x04,                         // OP_STORE ($w0 -> $w1; $w0 preserved)
        0x01, 0x2A, 0x00, 0x00, 0x00  // OP_ICONST 42 (redundant copy captured by CSE)
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

