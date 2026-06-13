#include "unity.h"
#include "codegen/tld_macho.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_linker_macho_64_binary_signature_integrity(void) {
    const char* filename = "output_macho_linker_test_arm";
    uint8_t mock_macho_opcodes[] = { 0x90, 0xC3 };

    // 1. TESTS EXECUTABLE BINARY ON APPLE SILICON (is_shared = false)
    bool success_arm = tld_macho_write_executable(filename, mock_macho_opcodes, sizeof(mock_macho_opcodes), CPU_TYPE_ARM64, false);
    TEST_ASSERT_TRUE(success_arm);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);
    uint32_t magic_read = 0;
    fread(&magic_read, 4, 1, f);

    // Skips the next 8 bytes to read the 32-bit 'filetype' field directly
    fseek(f, 12, SEEK_SET);
    uint32_t filetype_read = 0;
    fread(&filetype_read, 4, 1, f);
    fclose(f);

    TEST_ASSERT_EQUAL_HEX32(MH_MAGIC_64, magic_read);
    TEST_ASSERT_EQUAL_UINT32(MH_EXECUTE, filetype_read); // Should be an ordinary executable (2)
    remove(filename);

    // 2. TESTS SHARED LIBRARY .DYLIB ON INTEL MAC (is_shared = true)
    const char* filename_intel = "output_macho_linker_test_intel.dylib";
    bool success_intel = tld_macho_write_executable(filename_intel, mock_macho_opcodes, sizeof(mock_macho_opcodes), CPU_TYPE_X86_64, true);
    TEST_ASSERT_TRUE(success_intel);

    FILE* f_intel = fopen(filename_intel, "rb");
    TEST_ASSERT_NOT_NULL(f_intel);
    uint32_t magic_intel = 0;
    fread(&magic_intel, 4, 1, f_intel);

    fseek(f_intel, 12, SEEK_SET);
    uint32_t filetype_intel = 0;
    fread(&filetype_intel, 4, 1, f_intel);
    fclose(f_intel);

    TEST_ASSERT_EQUAL_HEX32(MH_MAGIC_64, magic_intel);
    TEST_ASSERT_EQUAL_UINT32(MH_DYLIB, filetype_intel); // Should be a configured dynamic library (6)
    remove(filename_intel);
}
