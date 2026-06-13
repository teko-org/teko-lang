#include "unity.h"
#include "codegen/tld_elf_reloc.h"
#include <stdlib.h>
#include <string.h>

void test_teko_linker_elf64_relative_jmp_patch_calculation(void) {
    // Simulates a fictional 20-byte machine code buffer
    // Where position 5 holds the empty JMP operand we want to patch [0x00, 0x00, 0x00, 0x00]
    uint8_t code_buffer[20];
    memset(code_buffer, 0x90, sizeof(code_buffer)); // Fills with NOPs

    // Configures the symbol map: Label 42 is positioned at byte 14
    TekoElfLabelMap labels[1];
    labels[0].label_id = 42;
    labels[0].bytecode_offset = 14;

    // Configures the relocation entry: Instruction at byte 5 references Label 42 relatively
    TekoElfRelocEntry relocs[1];
    relocs[0].patch_offset = 5;
    relocs[0].target_label_id = 42;
    relocs[0].is_relative = true;

    // Runs the Linker patch engine
    tld_elf64_perform_relocations(code_buffer, sizeof(code_buffer), labels, 1, relocs, 1, 0x400000);

    // EXPECTED DISTANCE: Destination (14) - (Patch_offset (5) + 4) = 14 - 9 = 5 bytes!
    TEST_ASSERT_EQUAL_HEX8(0x05, code_buffer[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[6]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[7]);
    TEST_ASSERT_EQUAL_HEX8(0x00, code_buffer[8]);
}
