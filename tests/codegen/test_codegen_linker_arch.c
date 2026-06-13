#include "unity.h"
#include "codegen/tld_elf_arch.h"
#include <string.h>

void test_teko_linker_arch_x64_mov_encoding_integrity(void) {
    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));

    // Tests the encoder for x86_64 loading the constant 42
    uint32_t bytes = tld_arch_encode_instruction(ARCH_X86_64, OP_ICONST, 42, buffer);

    TEST_ASSERT_EQUAL_UINT32(5, bytes);
    TEST_ASSERT_EQUAL_HEX8(0xB8, buffer[0]); // Immediate load opcode into %eax
    TEST_ASSERT_EQUAL_INT32(42, buffer[1] | (buffer[2] << 8));
}

void test_teko_linker_arch_arm64_add_encoding_integrity(void) {
    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));

    // Tests the encoder for ARM64 applying the ADD instruction
    uint32_t bytes = tld_arch_encode_instruction(ARCH_ARM64, OP_ADD, 0, buffer);

    TEST_ASSERT_EQUAL_UINT32(4, bytes);
    // Validates the fixed 32-bit binary pattern of the ADD w0, w0, w1 instruction in Little Endian
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[1]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buffer[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, buffer[3]);
}

void test_teko_linker_arch_x86_32_halt_encoding(void) {
    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));

    uint32_t bytes = tld_arch_encode_instruction(ARCH_X86, OP_HALT, 0, buffer);
    TEST_ASSERT_EQUAL_UINT32(12, bytes);
    TEST_ASSERT_EQUAL_HEX8(0xB8, buffer[0]); // movl imm
    TEST_ASSERT_EQUAL_HEX8(0xCD, buffer[10]); // int
    TEST_ASSERT_EQUAL_HEX8(0x80, buffer[11]); // 0x80
}

void test_teko_linker_arch_arm32_mov_encoding(void) {
    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));

    uint32_t bytes = tld_arch_encode_instruction(ARCH_ARM32, OP_ICONST, 5, buffer);
    TEST_ASSERT_EQUAL_UINT32(4, bytes);
    // Validates mov r0, #5 in fixed 32-bit RISC Little Endian format
    TEST_ASSERT_EQUAL_HEX8(0x05, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buffer[1]);
    TEST_ASSERT_EQUAL_HEX8(0xA0, buffer[2]);
    TEST_ASSERT_EQUAL_HEX8(0xE3, buffer[3]);
}

void test_teko_linker_arch_ppc64_add_encoding(void) {
    uint8_t buffer[32];
    memset(buffer, 0, sizeof(buffer));

    uint32_t bytes = tld_arch_encode_instruction(ARCH_PPC64, OP_ADD, 0, buffer);
    TEST_ASSERT_EQUAL_UINT32(4, bytes);
    // add r3, r3, r4 fixed binary for PowerPC
    TEST_ASSERT_EQUAL_HEX8(0x14, buffer[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22, buffer[1]);
    TEST_ASSERT_EQUAL_HEX8(0x63, buffer[2]);
    TEST_ASSERT_EQUAL_HEX8(0x7C, buffer[3]);
}