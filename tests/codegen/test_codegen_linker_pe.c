#include "unity.h"
#include "codegen/tld_pe.h"
#include <stdio.h>
#include <stdlib.h>

void test_teko_linker_pe_coff_multi_architecture_signatures(void) {
    const char* filename = "output_pe_linker_test.exe";
    uint8_t mock_opcodes[] = { 0x90, 0xC3 };

    // 1. WINDOWS X86_64 EXECUTABLE CERTIFICATION
    bool success_x64 = tld_pe_write_executable(filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_AMD64, false);
    TEST_ASSERT_TRUE(success_x64);

    FILE* f = fopen(filename, "rb");
    TEST_ASSERT_NOT_NULL(f);
    TekoImageDosHeader dos;
    fread(&dos, sizeof(TekoImageDosHeader), 1, f);
    TEST_ASSERT_EQUAL_HEX16(PE_DOS_MAGIC, dos.e_magic);

    fseek(f, dos.e_lfanew, SEEK_SET);
    uint32_t pe_sig = 0;
    fread(&pe_sig, 4, 1, f);
    TEST_ASSERT_EQUAL_HEX32(PE_SIGNATURE, pe_sig);
    fclose(f);
    remove(filename);

    // 2. WINDOWS X86 (32-bit) DYNAMIC LIBRARY DLL CERTIFICATION (is_shared = true)
    const char* dll_filename = "output_pe_linker_test.dll";
    bool success_x82 = tld_pe_write_executable(dll_filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_I386, true);
    TEST_ASSERT_TRUE(success_x82);

    FILE* f_dll = fopen(dll_filename, "rb");
    TEST_ASSERT_NOT_NULL(f_dll);
    TekoImageDosHeader dos_dll;
    fread(&dos_dll, sizeof(TekoImageDosHeader), 1, f_dll);

    // Advances the pointer past the PE Signature and reads the COFF header (File Header)
    fseek(f_dll, dos_dll.e_lfanew + 4, SEEK_SET);
    TekoImageFileHeader coff;
    fread(&coff, sizeof(TekoImageFileHeader), 1, f_dll);
    fclose(f_dll);

    // The characteristics flag MUST contain the DLL bit set (Characteristics & 0x2000) [INDEX]
    TEST_ASSERT_BITS(PE_CHAR_DLL, PE_CHAR_DLL, coff.Characteristics);
    remove(dll_filename);

    // 3. WINDOWS ON ARM (ARM64 64-bit) EXECUTABLE CERTIFICATION
    bool success_arm64 = tld_pe_write_executable(filename, mock_opcodes, sizeof(mock_opcodes), PE_MACHINE_ARM64, false);
    TEST_ASSERT_TRUE(success_arm64);
    remove(filename);
}
