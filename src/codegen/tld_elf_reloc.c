#include "tld_elf_reloc.h"
#include <stdio.h>

void tld_elf64_perform_relocations(uint8_t* code_buffer, uint32_t code_size,
                                   const TekoElfLabelMap* labels, uint32_t label_count,
                                   const TekoElfRelocEntry* relocs, uint32_t reloc_count,
                                   uint64_t base_entry_vaddr) {
    if (!code_buffer || code_size == 0) return;

    // Scans all entries that the compiler marked as needing a patch
    for (uint32_t i = 0; i < reloc_count; i++) {
        TekoElfRelocEntry reloc = relocs[i];
        uint32_t target_offset = 0;
        bool label_found = false;

        // 1. Locates the physical address of the target label in the symbol map
        for (uint32_t j = 0; j < label_count; j++) {
            if (labels[j].label_id == reloc.target_label_id) {
                target_offset = labels[j].bytecode_offset;
                label_found = true;
                break;
            }
        }

        if (!label_found) {
            fprintf(stderr, "[TLD Linker Error]: Undefined symbol. Label_%d not found.\n", reloc.target_label_id);
            continue;
        }

        // 2. Applies the binary patch mathematical rules
        if (reloc.is_relative) {
            // Relative JMP rule (standard x86_64 / ARM64):
            // Distance = Destination - (Current_Position + 4 bytes of operand size)
            int32_t relative_offset = (int32_t)target_offset - ((int32_t)reloc.patch_offset + 4);

            // Writes the 4 patch bytes in Little Endian format directly into the buffer
            code_buffer[reloc.patch_offset + 0] = (relative_offset >> 0)  & 0xFF;
            code_buffer[reloc.patch_offset + 1] = (relative_offset >> 8)  & 0xFF;
            code_buffer[reloc.patch_offset + 2] = (relative_offset >> 16) & 0xFF;
            code_buffer[reloc.patch_offset + 3] = (relative_offset >> 24) & 0xFF;
        }
        else {
            // Absolute Memory Load rule (standard for Strings/.rodata):
            // Final Virtual Address = RAM Virtual Base + Position in Bytecode
            uint64_t absolute_vaddr = base_entry_vaddr + target_offset;

            // Patches the instruction by atomically injecting the 64-bit virtual pointer
            code_buffer[reloc.patch_offset + 0] = (absolute_vaddr >> 0)  & 0xFF;
            code_buffer[reloc.patch_offset + 1] = (absolute_vaddr >> 8)  & 0xFF;
            code_buffer[reloc.patch_offset + 2] = (absolute_vaddr >> 16) & 0xFF;
            code_buffer[reloc.patch_offset + 3] = (absolute_vaddr >> 24) & 0xFF;
            // If the instruction uses 8 bytes, fills the upper part cleanly
            if (reloc.patch_offset + 7 < code_size) {
                code_buffer[reloc.patch_offset + 4] = (absolute_vaddr >> 32) & 0xFF;
                code_buffer[reloc.patch_offset + 5] = (absolute_vaddr >> 40) & 0xFF;
                code_buffer[reloc.patch_offset + 6] = (absolute_vaddr >> 48) & 0xFF;
                code_buffer[reloc.patch_offset + 7] = (absolute_vaddr >> 56) & 0xFF;
            }
        }
    }
}
