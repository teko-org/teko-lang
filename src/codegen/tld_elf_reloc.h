#ifndef TLD_ELF_RELOC_H
#define TLD_ELF_RELOC_H

#include <stdint.h>
#include <stdbool.h>

// Structure to record where physical labels are positioned in the bytecode
typedef struct {
    uint32_t label_id;
    uint32_t bytecode_offset; // Exact byte position of the label in the array
} TekoElfLabelMap;

// Structure to record instructions that need an address patch
typedef struct {
    uint32_t patch_offset;     // Where in memory we need to inject the final address
    uint32_t target_label_id;  // ID of the label this instruction is looking for
    bool is_relative;          // True for JMP (relative), False for String (absolute)
} TekoElfRelocEntry;

// ELF64 Linker Relocation Engine
void tld_elf64_perform_relocations(uint8_t* code_buffer, uint32_t code_size,
                                   const TekoElfLabelMap* labels, uint32_t label_count,
                                   const TekoElfRelocEntry* relocs, uint32_t reloc_count,
                                   uint64_t base_entry_vaddr);

#endif // TLD_ELF_RELOC_H
