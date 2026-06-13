#include "tld_pe.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint32_t pe_align(uint32_t size, uint32_t alignment) {
    if (size % alignment == 0) return size;
    return ((size / alignment) + 1) * alignment;
}

bool tld_pe_write_executable(const char* filename, const uint8_t* machine_code, uint32_t code_size, uint16_t machine_type, bool is_shared) {
    if (!filename || !machine_code || code_size == 0) return false;

    FILE* out = fopen(filename, "wb");
    if (!out) return false;

    TekoImageDosHeader dos;
    uint32_t pe_sig = PE_SIGNATURE;
    TekoImageFileHeader coff;
    TekoImageSectionHeader text_sec;

    // 1. LEGACY DOS HEADER CONSTRUCTION
    memset(&dos, 0, sizeof(TekoImageDosHeader));
    dos.e_magic = PE_DOS_MAGIC;
    dos.e_lfanew = sizeof(TekoImageDosHeader);

    // 2. MASTER COFF HEADER CONSTRUCTION
    memset(&coff, 0, sizeof(TekoImageFileHeader));
    coff.Machine = machine_type;
    coff.NumberOfSections = 1;
    coff.TimeDateStamp = 0;

    // ARTIFACT PARAMETERIZATION: Enables PE_CHAR_DLL if the project is a dynamic library (.dll) [INDEX]
    coff.Characteristics = PE_CHAR_EXECUTABLE;
    if (is_shared) {
        coff.Characteristics |= PE_CHAR_DLL;
    }
    if (machine_type == PE_MACHINE_I386) {
        coff.Characteristics |= PE_CHAR_32BIT_MACH;
        coff.SizeOfOptionalHeader = sizeof(TekoImageOptionalHeader32) + (16 * 8);
    } else {
        coff.SizeOfOptionalHeader = sizeof(TekoImageOptionalHeader64) + (16 * 8);
    }

    uint32_t file_align = 0x200;
    uint32_t section_align = 0x1000;

    uint32_t size_of_headers = sizeof(TekoImageDosHeader) + sizeof(uint32_t) +
                               sizeof(TekoImageFileHeader) + coff.SizeOfOptionalHeader +
                               sizeof(TekoImageSectionHeader);
    uint32_t aligned_headers_size = pe_align(size_of_headers, file_align);
    uint32_t aligned_code_size = pe_align(code_size, file_align);

    // 3. CONSTRUCTION OF THE CODE SECTION TABLE (.text)
    memset(&text_sec, 0, sizeof(TekoImageSectionHeader));
    strncpy(text_sec.Name, ".text", 8);
    text_sec.VirtualSize = code_size;
    text_sec.VirtualAddress = section_align;
    text_sec.SizeOfRawData = aligned_code_size;
    text_sec.PointerToRawData = aligned_headers_size;
    text_sec.Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;

    // 4. WRITING THE GENERAL HEADERS
    fwrite(&dos, 1, sizeof(TekoImageDosHeader), out);
    fwrite(&pe_sig, 1, sizeof(uint32_t), out);
    fwrite(&coff, 1, sizeof(TekoImageFileHeader), out);

    uint32_t total_image_size = pe_align(text_sec.VirtualAddress + text_sec.VirtualSize, section_align);

    // 5. WRITES THE OPTIONAL HEADER BASED ON THE ISA
    if (machine_type == PE_MACHINE_I386) {
        TekoImageOptionalHeader32 opt32;
        memset(&opt32, 0, sizeof(TekoImageOptionalHeader32));
        opt32.Magic = PE_MAGIC_PE32;
        opt32.SizeOfCode = aligned_code_size;
        opt32.AddressOfEntryPoint = is_shared ? 0 : text_sec.VirtualAddress; // DLLs may have a zero entry point if not initializable
        opt32.BaseOfCode = text_sec.VirtualAddress;
        opt32.ImageBase = 0x00400000;
        opt32.SectionAlignment = section_align;
        opt32.FileAlignment = file_align;
        opt32.MajorSubsystemVersion = 4;
        opt32.SizeOfImage = total_image_size;
        opt32.SizeOfHeaders = aligned_headers_size;
        opt32.Subsystem = 3; // Console App
        opt32.NumberOfRvaAndSizes = 16;
        fwrite(&opt32, 1, sizeof(TekoImageOptionalHeader32), out);
    } else {
        TekoImageOptionalHeader64 opt64;
        memset(&opt64, 0, sizeof(TekoImageOptionalHeader64));
        opt64.Magic = PE_MAGIC_PE32_PLUS;
        opt64.SizeOfCode = aligned_code_size;
        opt64.AddressOfEntryPoint = is_shared ? 0 : text_sec.VirtualAddress;
        opt64.BaseOfCode = text_sec.VirtualAddress;
        opt64.ImageBase = 0x0000000140000000ULL;
        opt64.SectionAlignment = section_align;
        opt64.FileAlignment = file_align;
        opt64.MajorSubsystemVersion = 6;
        opt64.SizeOfImage = total_image_size;
        opt64.SizeOfHeaders = aligned_headers_size;
        opt64.Subsystem = 3;
        opt64.NumberOfRvaAndSizes = 16;
        fwrite(&opt64, 1, sizeof(TekoImageOptionalHeader64), out);
    }

    // Writes the empty filler data directories (16 x 8 = 128 bytes)
    uint8_t zero_dirs[128] = {0};
    fwrite(zero_dirs, 1, 128, out);

    // Writes the section table metadata
    fwrite(&text_sec, 1, sizeof(TekoImageSectionHeader), out);

    // FIX: Clean header padding on disk to respect FileAlignment
    uint32_t current_file_pos = ftell(out);
    if (aligned_headers_size > current_file_pos) {
        uint32_t pad_size = aligned_headers_size - current_file_pos;
        uint8_t* pad = (uint8_t*)calloc(pad_size, 1);
        if (pad) {
            fwrite(pad, 1, pad_size, out);
            free(pad);
        }
    }

    // 6. WRITES THE NATIVE CODE FROM THE METAL BACKEND
    fwrite(machine_code, 1, code_size, out);

    // FIX: Final physical file alignment on disk (exact multiple of 512 bytes) [INDEX]
    uint32_t end_file_pos = ftell(out);
    uint32_t expected_total_size = aligned_headers_size + aligned_code_size;
    if (expected_total_size > end_file_pos) {
        uint32_t pad_size = expected_total_size - end_file_pos;
        uint8_t* pad = (uint8_t*)calloc(pad_size, 1);
        if (pad) {
            fwrite(pad, 1, pad_size, out);
            free(pad);
        }
    }

    fclose(out);
    return true;
}
