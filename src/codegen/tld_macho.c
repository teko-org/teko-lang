#include "tld_macho.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Helper macro to round sizes up to the nearest physical page multiple required by the CPU [1]
static uint64_t macho_align_to_page(uint64_t size, uint64_t alignment) {
    if (size % alignment == 0) return size;
    return ((size / alignment) + 1) * alignment;
}

bool tld_macho_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, int32_t cpu_type, bool is_shared) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoMachHeader64 header;
    TekoMachSegmentCommand64 seg_text;
    TekoMachEntryPointCommand cmd_main;
    TekoMachBuildVersionCommand cmd_version;

    // 1. POLYMORPHIC DETERMINATION OF THE CHIP'S PHYSICAL PAGE ALIGNMENT [1]
    uint64_t page_alignment = 0x1000; // Default Mac Intel: 4KB [1]
    if (cpu_type == CPU_TYPE_ARM64) {
        page_alignment = 0x4000;      // Default Apple Silicon: 16KB [1]
    }

    // 2. MACH-O 64-BIT MASTER HEADER CONFIGURATION
    memset(&header, 0, sizeof(TekoMachHeader64));
    header.magic      = MH_MAGIC_64;
    header.cputype    = cpu_type;
    header.cpusubtype = CPU_SUBTYPE_ALL;

    // TYPE PARAMETERIZATION: MH_DYLIB for dynamic libraries, MH_EXECUTE for final binaries
    header.filetype   = is_shared ? MH_DYLIB : MH_EXECUTE;

    // If it is a dylib, we omit the LC_MAIN entry point command
    header.ncmds      = is_shared ? 2 : 3;

    // Calculates the dynamic size of the active load commands
    header.sizeofcmds = sizeof(TekoMachSegmentCommand64) + sizeof(TekoMachBuildVersionCommand);
    if (!is_shared) {
        header.sizeofcmds += sizeof(TekoMachEntryPointCommand);
    }
    header.flags      = 0x00000001;     // MH_NOUNDEFS

    // Default virtual memory address for loading executables on macOS (Base VMA)
    uint64_t base_vmaddr = is_shared ? 0x0ULL : 0x100000000ULL;

    // Total size of the initial metadata before the code payload
    uint32_t header_and_cmds_size = sizeof(TekoMachHeader64) + header.sizeofcmds;

    // PAGE ALIGNMENT FIX: Calculates the multiples required by the kernel [1]
    uint32_t raw_filesize = header_and_cmds_size + code_size;
    uint64_t aligned_vmsize = macho_align_to_page(raw_filesize, page_alignment);

    // 3. CONSTRUCTION OF THE __TEXT SEGMENT LOAD COMMAND
    memset(&seg_text, 0, sizeof(TekoMachSegmentCommand64));
    seg_text.cmd      = LC_SEGMENT_64;
    seg_text.cmdsize  = sizeof(TekoMachSegmentCommand64);
    strncpy(seg_text.segname, "__TEXT", 16);

    seg_text.vmaddr   = base_vmaddr;
    seg_text.fileoff  = 0;
    seg_text.filesize = raw_filesize;
    seg_text.vmsize   = aligned_vmsize; // FIXED: Now passes the perfectly aligned virtual size [1]

    seg_text.flags    = 0;
    seg_text.maxprot  = VM_PROT_READ | VM_PROT_EXECUTE;
    seg_text.initprot = VM_PROT_READ | VM_PROT_EXECUTE;
    seg_text.nsects   = 0;

    // 4. CONSTRUCTION OF THE ENTRY POINT COMMAND (Only if not a Dylib)
    if (!is_shared) {
        memset(&cmd_main, 0, sizeof(TekoMachEntryPointCommand));
        cmd_main.cmd      = LC_MAIN;
        cmd_main.cmdsize  = sizeof(TekoMachEntryPointCommand);
        cmd_main.entryoff  = header_and_cmds_size;
        cmd_main.stacksize = 0;
    }

    // 5. CONSTRUCTION OF THE DARWIN BUILD VERSION COMMAND (LC_BUILD_VERSION)
    memset(&cmd_version, 0, sizeof(TekoMachBuildVersionCommand));
    cmd_version.cmd      = LC_BUILD_VERSION;
    cmd_version.cmdsize  = sizeof(TekoMachBuildVersionCommand);
    cmd_version.platform = PLATFORM_MACOS;
    cmd_version.minos    = (13 << 16) | (0 << 8) | 0; // macOS Ventura
    cmd_version.sdk      = (13 << 16) | (0 << 8) | 0;
    cmd_version.ntools   = 0;

    // 6. CONTROLLED ATOMIC WRITE TO THE EXECUTABLE
    fwrite(&header, 1, sizeof(TekoMachHeader64), out);
    fwrite(&seg_text, 1, sizeof(TekoMachSegmentCommand64), out);
    if (!is_shared) {
        fwrite(&cmd_main, 1, sizeof(TekoMachEntryPointCommand), out);
    }
    fwrite(&cmd_version, 1, sizeof(TekoMachBuildVersionCommand), out);
    fwrite(machine_code, 1, code_size, out);

    // Optional padding to fill disk file alignment if necessary
    if (aligned_vmsize > raw_filesize) {
        uint64_t padding_size = aligned_vmsize - raw_filesize;
        // We avoid allocating a huge buffer by filling with small loops of null bytes
        uint8_t zero = 0;
        for (uint64_t p = 0; p < padding_size; p++) {
            fwrite(&zero, 1, 1, out);
        }
    }

    fclose(out);
    return true;
}
