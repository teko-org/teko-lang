#include "unity.h"
#include "codegen/tld_elf.h"
#include "codegen/tld_elf_arch.h"
#include "codegen/tld_symbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void test_teko_linker_e2e_extern_service_injection_and_elf_generation(void) {
    const char* output_binary = "output_teko_e2e_exec";
    TekoSymbolTable symbol_table;
    tld_symbols_init(&symbol_table);

    // Unified buffer to simulate the contiguous merge of code sections in Linker memory
    uint8_t final_code_segment[512];
    memset(final_code_segment, 0x90, sizeof(final_code_segment)); // Initialized with native NOPs
    uint32_t current_offset = 0;

    // ====================================================================
    // STEP 1: COMPILATION OF FRAGMENT 1 (body of the Flows.notify service)
    // ====================================================================
    uint32_t service_start_offset = current_offset;

    // Registers and defines the dependency symbol in the Linker's global table
    bool def_ok = tld_symbols_define(&symbol_table, "@flows.notify", SYM_SERVICE, service_start_offset);
    TEST_ASSERT_TRUE(def_ok);

    // Emits the binary body of the subroutine for the Intel x86_64 CPU: ICONST 42 -> RETURN (mapped via 1-byte OP_STORE)
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_ICONST, 42, &final_code_segment[current_offset]);
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_STORE, 0, &final_code_segment[current_offset]);

    // ====================================================================
    // STEP 2: COMPILATION OF FRAGMENT 2 (executable main calling the extern)
    // ====================================================================
    uint32_t main_start_offset = current_offset;
    bool main_def_ok = tld_symbols_define(&symbol_table, "main", SYM_FUNC, main_start_offset);
    TEST_ASSERT_TRUE(main_def_ok);

    // main performs a hardware 'call'. We reserve 1 byte for the call opcode (0xE8)
    final_code_segment[current_offset++] = 0xE8;

    // The exact point where the 4 bytes of the dependency's relative offset must be injected
    uint32_t patch_target_offset = current_offset;

    // Registers the pending reference linking the 'call site' to the extern's text name
    tld_symbols_reference(&symbol_table, "@flows.notify", patch_target_offset);
    current_offset += 4; // Advances the space reserved for the 32-bit address operand

    // Finishes main by emitting the native CPU shutdown HALT
    current_offset += tld_arch_encode_instruction(ARCH_X86_64, OP_HALT, 0, &final_code_segment[current_offset]);

    // ====================================================================
    // STEP 3: LINK-TIME RESOLUTION AND COORDINATED DEPENDENCY INJECTION
    // ====================================================================
    uint64_t linux_base_vaddr = 0x400000; // Canonical VMA of the 64-bit Linux Kernel

    // Invokes the symbol resolver to patch the null bytes of the 'call' in memory
    bool resolution_ok = tld_symbols_resolve_and_inject(&symbol_table, final_code_segment, linux_base_vaddr);
    TEST_ASSERT_TRUE(resolution_ok);

    // MATHEMATICAL VALIDATION BEFORE WRITING TO DISK:
    // Per the Linker algorithm: Offset = Destination (0) - (Patch_Offset (6) + 4) = 0 - 10 = -10 bytes!
    // -10 in 32-bit two's complement is expressed in hexadecimal as 0xFFFFFFF6
    TEST_ASSERT_EQUAL_HEX8(0xF4, final_code_segment[patch_target_offset + 0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, final_code_segment[patch_target_offset + 3]);

    // ====================================================================
    // STEP 4: WRITING THE FINAL STANDALONE EXECUTABLE (ELF64 TO DISK)
    // ====================================================================
    bool write_ok = tld_elf64_write_executable(output_binary, final_code_segment, current_offset, ELF_ARCH_X86_64);
    TEST_ASSERT_TRUE(write_ok);

    // Final audit opening the generated real executable to check byte integrity
    FILE* executable = fopen(output_binary, "rb");
    TEST_ASSERT_NOT_NULL(executable);

    // Captures the exact on-disk size of the generated binary executable
    fseek(executable, 0, SEEK_END);
    long binary_size = ftell(executable);
    fseek(executable, 0, SEEK_SET);

    // Allocates a raw byte buffer (uint8_t) to prevent truncation by '\0'
    uint8_t* full_binary_buffer = (uint8_t*)malloc(binary_size);
    TEST_ASSERT_NOT_NULL(full_binary_buffer);
    size_t read_bytes = fread(full_binary_buffer, 1, binary_size, executable);
    TEST_ASSERT_EQUAL_size_t(binary_size, read_bytes);
    fclose(executable);

    // 1. Certifies the existence of the binary molecular signature of the ELF format (\x7fELF)
    TEST_ASSERT_EQUAL_HEX8(0x7F, full_binary_buffer[0]);
    TEST_ASSERT_EQUAL_HEX8('E',  full_binary_buffer[1]);
    TEST_ASSERT_EQUAL_HEX8('L',  full_binary_buffer[2]);
    TEST_ASSERT_EQUAL_HEX8('F',  full_binary_buffer[3]);

    // 2. .RODATA SECTION AUDIT: Brute-force byte scan in memory to locate the strings
    bool found_hello = false;
    bool found_cqrs = false;

    // Scans the bytes looking for the contiguous sequences of text signatures at any offset in the binary.
    // Each memcmp is bounded by its own length so neither reads past the buffer (the "Hello Teko" scan is
    // 10 bytes, the "CQRS_Service_Active" scan is 19 bytes — a single `binary_size - 10` bound is NOT enough
    // for the 19-byte compare and overflows the heap buffer near the end).
    for (long idx = 0; idx < binary_size; idx++) {
        if (idx + 10 <= binary_size && memcmp(&full_binary_buffer[idx], "Hello Teko", 10) == 0) {
            found_hello = true;
        }
        if (idx + 19 <= binary_size && memcmp(&full_binary_buffer[idx], "CQRS_Service_Active", 19) == 0) {
            found_cqrs = true;
        }
    }
    TEST_ASSERT_EQUAL_HEX8(9, full_binary_buffer[7]);

    // Unity assertions confirming that the static data was embedded with physical perfection
    TEST_ASSERT_TRUE(found_hello);
    TEST_ASSERT_TRUE(found_cqrs);

    // Clean release and smoke cycle closure
    free(full_binary_buffer);
    remove(output_binary);
}
