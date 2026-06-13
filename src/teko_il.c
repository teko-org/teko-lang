#include "teko_il.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void teko_il_serialize_binary(const BytecodeBuffer* buffer, const char* filename) {
    if (!buffer || !filename) return;

    FILE* file = fopen(filename, "wb");
    if (!file) {
        fprintf(stderr, "[System Error]: Could not create output file %s\n", filename);
        return;
    }

    // 1. Assemble and write the Physical Header
    TekoFileHeader header;
    header.magic = TEKO_MAGIC;
    header.version_major = TEKO_VERSION_MAJOR;
    header.version_minor = TEKO_VERSION_MINOR;
    header.constant_pool_count = buffer->pool.count;
    header.definitions_count = 0; // Computed semantically in later phases
    header.code_section_size = buffer->size;

    fwrite(&header, sizeof(TekoFileHeader), 1, file);

    // 2. Write the Constant Pool (String and Arbitrary Number Pool)
    // For each string, write its length as uint32_t first, then the raw characters
    for (int i = 0; i < buffer->pool.count; i++) {
        uint32_t str_len = (uint32_t)strlen(buffer->pool.strings[i]);
        fwrite(&str_len, sizeof(uint32_t), 1, file);
        fwrite(buffer->pool.strings[i], 1, str_len, file);
    }

    // 3. Write the Code Section (ISA Opcodes and Arguments)
    if (buffer->size > 0 && buffer->code) {
        fwrite(buffer->code, 1, buffer->size, file);
    }

    fclose(file);
    printf("[IL Codegen]: Standalone binary file '%s' successfully generated (%d bytes).\n", filename, buffer->size);
}