#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ====================================================================
// 1. NATIVE WEBASSEMBLY ENGINE TEST (WASM / WAT BARE-METAL)
// ====================================================================
void test_teko_aot_wasm_pure_emission_integrity(void) {
    const char* asm_path = "output_wasm_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32; // Configures the WASM virtual processor
    target.os = OS_WASI;      // Abstract OS for Web/Servers
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injects the contiguous sequence of our ISA: Constant Load -> Channel -> Halt
    unsigned char mock_wasm_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42 (5 bytes)
        0x12                          // OP_CHAN_INIT (1 byte)
    };

    teko_metal_emit_program(ctx, mock_wasm_bytes, sizeof(mock_wasm_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 4096);

    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Structured assertions validating the fidelity of the WebAssembly S-Expression grammar
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(module"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 42"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(memory 1)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(export \"main\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Hello Teko"));

    free(buffer);
    remove(asm_path);
}
