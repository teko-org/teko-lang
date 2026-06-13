#include "tld_elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ====================================================================
// NATIVE EMITTER FOR 32-BIT ARCHITECTURES (ELF32)
// ====================================================================
bool tld_elf32_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoElf32_Ehdr ehdr;
    TekoElf32_Phdr phdr;

    // 1. ELF32 HEADER CONFIGURATION
    memset(&ehdr, 0, sizeof(TekoElf32_Ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELF_CLASS_32; // Identifies as 32-bit class
    ehdr.e_ident[5] = ELF_DATA_2LSB;
    ehdr.e_ident[6] = ELF_VERSION_CUR;
    ehdr.e_ident[7] = ELF_OS_LINUX;

    ehdr.e_type      = ELF_TYPE_EXEC;
    ehdr.e_machine   = architecture; // ELF_ARCH_386 or ELF_ARCH_ARM32
    ehdr.e_version   = ELF_VERSION_CUR;

    // Default base virtual load address for 32-bit Linux systems (VMA)
    uint32_t base_vaddr = 0x08048000;

    ehdr.e_ehsize    = sizeof(TekoElf32_Ehdr); // 52 physical bytes
    ehdr.e_phentsize = sizeof(TekoElf32_Phdr); // 32 physical bytes
    ehdr.e_phnum     = 1;
    ehdr.e_phoff     = ehdr.e_ehsize;

    ehdr.e_entry     = base_vaddr + ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize);

    // Simplified static .rodata pool for language feature parity
    const char* mock_strings[] = { "Hello Teko", "32Bit_Core_Active" };
    uint32_t rodata_size = 0;
    for (int i = 0; i < 2; i++) rodata_size += strlen(mock_strings[i]) + 1;
    uint8_t* rodata_buffer = (uint8_t*)malloc(rodata_size);
    if (rodata_buffer) {
        uint32_t cursor = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t len = strlen(mock_strings[i]) + 1;
            memcpy(&rodata_buffer[cursor], mock_strings[i], len);
            cursor += len;
        }
    }

    // 2. PROGRAM HEADER CONSTRUCTION (ELF32 layout)
    memset(&phdr, 0, sizeof(TekoElf32_Phdr));
    phdr.p_type   = PT_LOAD;
    phdr.p_offset = 0;
    phdr.p_vaddr  = base_vaddr;
    phdr.p_paddr  = base_vaddr;
    phdr.p_flags  = PF_R | PF_W | PF_X; // Unified permissions

    uint32_t total_payload_size = code_size + rodata_size;
    uint32_t total_file_size = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + total_payload_size;

    phdr.p_filesz = total_file_size;
    phdr.p_memsz  = total_file_size;
    phdr.p_align  = 0x1000; // Default 4KB page alignment

    // 3. WRITING THE 32-BIT ATOMIC BLOCKS
    fwrite(&ehdr, 1, sizeof(TekoElf32_Ehdr), out);
    fwrite(&phdr, 1, sizeof(TekoElf32_Phdr), out);
    fwrite(machine_code, 1, code_size, out);

    if (rodata_buffer) {
        fwrite(rodata_buffer, 1, rodata_size, out);
        free(rodata_buffer);
    }

    fclose(out);
    return true;
}

// ====================================================================
// NATIVE EMITTER FOR 64-BIT ARCHITECTURES (ELF64)
// ====================================================================
bool tld_elf64_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t architecture) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoElf64_Ehdr ehdr;
    TekoElf64_Phdr phdr[2]; // Expanded to 2 Program Headers (1 for LOAD, 1 for FreeBSD NOTE)

    memset(&ehdr, 0, sizeof(TekoElf64_Ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELF_CLASS_64;
    ehdr.e_ident[5] = ELF_DATA_2LSB;
    ehdr.e_ident[6] = ELF_VERSION_CUR;

    // UNIX/FREEBSD MATCHING: Sets the native ABI identification field
    ehdr.e_ident[7] = ELF_OS_FREEBSD;

    ehdr.e_type      = ELF_TYPE_EXEC;
    ehdr.e_machine   = architecture;
    ehdr.e_version   = ELF_VERSION_CUR;

    uint64_t base_vaddr = 0x400000;

    ehdr.e_ehsize    = sizeof(TekoElf64_Ehdr);
    ehdr.e_phentsize = sizeof(TekoElf64_Phdr);
    ehdr.e_phnum     = 2; // We now have 2 Program Headers to load the UNIX binary correctly
    ehdr.e_phoff     = ehdr.e_ehsize;

    ehdr.e_entry     = base_vaddr + ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize);

    // Exact physical structure of the ABI note required by the FreeBSD Kernel (16 structural bytes + payload)
    uint32_t freebsd_note[] = {
        4,             // Namesz: "FreeBSD" will occupy 4 aligned bytes (with \0)
        4,             // Descsz: Size of the note value (4 bytes of the version integer)
        NT_FREEBSD_ABI_TAG, // Type: Identifier 1
        0x45455246,    // Name: "FREE" in Little Endian
        0x00445342,    // Name: "BSD\0" in Little Endian
        1400000        // Desc: Target stable kernel version (e.g.: FreeBSD 14)
    };
    uint32_t note_size = sizeof(freebsd_note);

    const char* mock_rodata_strings[] = { "Hello Teko", "CQRS_Service_Active" };
    uint32_t rodata_size = 0;
    for (int i = 0; i < 2; i++) rodata_size += strlen(mock_rodata_strings[i]) + 1;

    uint8_t* rodata_buffer = (uint8_t*)malloc(rodata_size);
    if (rodata_buffer) {
        uint32_t cursor = 0;
        for (int i = 0; i < 2; i++) {
            uint32_t len = strlen(mock_rodata_strings[i]) + 1;
            memcpy(&rodata_buffer[cursor], mock_rodata_strings[i], len);
            cursor += len;
        }
    }

    // PROGRAM HEADER 1: The sovereign PT_LOAD segment (.text + .rodata + .note)
    memset(&phdr[0], 0, sizeof(TekoElf64_Phdr));
    phdr[0].p_type   = PT_LOAD;
    phdr[0].p_flags  = PF_R | PF_W | PF_X;
    phdr[0].p_offset = 0;
    phdr[0].p_vaddr  = base_vaddr;
    phdr[0].p_paddr  = base_vaddr;

    uint32_t total_payload_size = code_size + rodata_size + note_size;
    uint32_t total_file_size = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + total_payload_size;

    phdr[0].p_filesz = total_file_size;
    phdr[0].p_memsz  = total_file_size;
    phdr[0].p_align  = 0x1000;

    // PROGRAM HEADER 2: The PT_NOTE segment (Explicitly tells the UNIX Kernel that the binary is FreeBSD)
    memset(&phdr[1], 0, sizeof(TekoElf64_Phdr));
    phdr[1].p_type   = 4; // PT_NOTE
    phdr[1].p_flags  = PF_R; // Read-only
    phdr[1].p_offset = ehdr.e_ehsize + (ehdr.e_phnum * ehdr.e_phentsize) + code_size + rodata_size;
    phdr[1].p_vaddr  = base_vaddr + phdr[1].p_offset;
    phdr[1].p_paddr  = phdr[1].p_vaddr;
    phdr[1].p_filesz = note_size;
    phdr[1].p_memsz  = note_size;
    phdr[1].p_align  = 4;

    // ATOMIC WRITE
    fwrite(&ehdr, 1, sizeof(TekoElf64_Ehdr), out);
    fwrite(&phdr, 1, sizeof(TekoElf64_Phdr) * 2, out);
    fwrite(machine_code, 1, code_size, out);

    if (rodata_buffer) {
        fwrite(rodata_buffer, 1, rodata_size, out);
        free(rodata_buffer);
    }

    fwrite(freebsd_note, 1, note_size, out);

    fclose(out);
    return true;
}
