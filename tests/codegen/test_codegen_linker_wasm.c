#include "unity.h"
#include "codegen/tld_wasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_linker_wasm_leb128_compression_logic(void) {
    uint8_t buffer[5];

    // Scenario 1: Small integer that fits in 1 byte (less than 128)
    uint32_t bytes1 = tld_wasm_encode_leb128(42, buffer);
    TEST_ASSERT_EQUAL_UINT32(1, bytes1);
    TEST_ASSERT_EQUAL_HEX8(42, buffer[0]);

    // Scenario 2: Integer that requires size expansion (e.g.: 300)
    // 300 = 0x012C -> In LEB128 becomes: [0xAC, 0x02]
    uint32_t bytes2 = tld_wasm_encode_leb128(300, buffer);
    TEST_ASSERT_EQUAL_UINT32(2, bytes2);
    TEST_ASSERT_EQUAL_HEX8(0xAC, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x02, buffer[1]);
}

void test_teko_linker_wasm_module_generation_and_magic_check(void) {
    const char* filename = "output_wasm_linker_test.wasm";

    // Injects a test payload simulating the i32.const 100 instruction (0x41 0x64) in pure WASM opcodes
    uint8_t mock_wasm_opcodes[] = { 0x41, 0x64 };

    bool success = tld_wasm_write_module(filename, mock_wasm_opcodes, sizeof(mock_wasm_opcodes));
    TEST_ASSERT_TRUE(success);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);

    uint8_t magic[4];
    size_t read_bytes = fread(magic, 1, 4, f);
    TEST_ASSERT_EQUAL_size_t(4, read_bytes);
    fclose(f);

    // Validates the magic signature of the global WebAssembly binary standard (\x00asm)
    TEST_ASSERT_EQUAL_HEX8(0x00, magic[0]);
    TEST_ASSERT_EQUAL_HEX8(0x61, magic[1]);
    TEST_ASSERT_EQUAL_HEX8(0x73, magic[2]);
    TEST_ASSERT_EQUAL_HEX8(0x6D, magic[3]);

    remove(filename);
}
