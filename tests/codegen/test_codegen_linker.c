#include "unity.h"
#include "codegen/tld_elf.h"
#include <stdio.h>
#include <stdlib.h>

void test_teko_linker_elf64_binary_header_generation(void) {
    const char* filename = "output_linker_test_bin";

    // Simulates a 4-byte payload of raw CPU instructions
    uint8_t mock_machine_code[] = { 0x90, 0x90, 0x90, 0xC3 }; // NOP, NOP, NOP, RET

    bool success = tld_elf64_write_executable(filename, mock_machine_code, sizeof(mock_machine_code), ELF_ARCH_X86_64);
    TEST_ASSERT_TRUE(success);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);

    unsigned char ident[4];
    size_t read_bytes = fread(ident, 1, 4, f);
    TEST_ASSERT_EQUAL_size_t(4, read_bytes);

    // FIXED: Validates the mandatory binary magic signature of the ELF format (\x7fELF)
    TEST_ASSERT_EQUAL_HEX8(0x7F, ident[0]);
    TEST_ASSERT_EQUAL_HEX8('E',  ident[1]);
    TEST_ASSERT_EQUAL_HEX8('L',  ident[2]);
    TEST_ASSERT_EQUAL_HEX8('F',  ident[3]); // Fixed from 'M' to 'F' to match the specification

    fclose(f);
    remove(filename);
}

void test_teko_linker_elf32_binary_header_and_class_validation(void) {
    const char* filename = "output_linker_test_bin_32";
    uint8_t mock_machine_code[] = { 0x90, 0xC3 }; // NOP, RET

    // Invokes the 32-bit ELF generator simulating the Intel i386 architecture
    bool success = tld_elf32_write_executable(filename, mock_machine_code, sizeof(mock_machine_code), ELF_ARCH_386);
    TEST_ASSERT_TRUE(success);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);

    unsigned char ident[5];
    size_t read_bytes = fread(ident, 1, 5, f);
    TEST_ASSERT_EQUAL_size_t(5, read_bytes);
    fclose(f);

    // Unity assertions confirming that the header is a legitimate ELF32
    TEST_ASSERT_EQUAL_HEX8(0x7F, ident[0]);
    TEST_ASSERT_EQUAL_HEX8('E',  ident[1]);
    TEST_ASSERT_EQUAL_HEX8('L',  ident[2]);
    TEST_ASSERT_EQUAL_HEX8('F',  ident[3]);

    // CRITICAL CLASS VERIFICATION: The fifth byte MUST be 1 (ELF_CLASS_32)
    TEST_ASSERT_EQUAL_HEX8(1, ident[4]);

    remove(filename);
}
