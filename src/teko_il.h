#ifndef TEKO_IL_H
#define TEKO_IL_H

#include <stdint.h>
#include "codegen_li.h"

// Exclusive magic number: "TEKO" in ASCII hexadecimal
#define TEKO_MAGIC 0x4F4B4554
#define TEKO_VERSION_MAJOR 1
#define TEKO_VERSION_MINOR 0

// Physical layout of the .tkb file header (24 bytes) - FIXED FOR CLANG/MACOS
typedef struct {
    uint32_t magic;              // Must equal TEKO_MAGIC
    uint16_t version_major;      // Compiler major version
    uint16_t version_minor;      // Compiler minor version
    uint32_t constant_pool_count;// Number of items in the Constant Pool
    uint32_t definitions_count;  // Number of mapped structs/handlers
    uint32_t code_section_size;  // Size in bytes of the opcode section
} __attribute__((packed)) TekoFileHeader;

// Representation of a structural definition on disk - FIXED FOR CLANG/MACOS
typedef struct {
    uint32_t name_pool_idx;      // Index of the structure name in the Constant Pool
    uint8_t structure_kind;      // 1 = Struct, 2 = Command, 3 = Query, 4 = Handler
    uint32_t fields_count;       // Number of internal properties
} __attribute__((packed)) TekoDefinitionEntry;

// Signatures for physical generation of the Intermediate Language file
void teko_il_serialize_binary(const BytecodeBuffer* buffer, const char* filename);

#endif // TEKO_IL_H