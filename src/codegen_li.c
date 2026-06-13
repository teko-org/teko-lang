#include "codegen_li.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Initializes the bytecode emission environment and the constant pool
BytecodeBuffer* codegen_li_create_context(void) {
    BytecodeBuffer* buffer = (BytecodeBuffer*)malloc(sizeof(BytecodeBuffer));
    if (!buffer) return NULL;

    buffer->capacity = 64;
    buffer->size = 0;
    buffer->code = (unsigned char*)malloc(buffer->capacity);

    buffer->pool.capacity = 16;
    buffer->pool.count = 0;
    buffer->pool.strings = (char**)malloc(sizeof(char*) * buffer->pool.capacity);

    return buffer;
}

// Helper to inject a raw opcode byte into the binary stream
static void emit_byte(BytecodeBuffer* buffer, unsigned char byte) {
    if (buffer->size >= buffer->capacity) {
        buffer->capacity *= 2;
        buffer->code = (unsigned char*)realloc(buffer->code, buffer->capacity);
    }
    buffer->code[buffer->size++] = byte;
}

// Helper to inject a 32-bit integer value (Little Endian by default on arm64/x86 hosts)
static void emit_int(BytecodeBuffer* buffer, int val) {
    emit_byte(buffer, (val >> 0) & 0xFF);
    emit_byte(buffer, (val >> 8) & 0xFF);
    emit_byte(buffer, (val >> 16) & 0xFF);
    emit_byte(buffer, (val >> 24) & 0xFF);
}

// Adds a string literal to the constant pool without duplicating it, and returns its index
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str) {
    if (!buffer || !str) return -1;

    // Check if the string already exists in the constant pool (deduplication)
    for (int i = 0; i < buffer->pool.count; i++) {
        if (strcmp(buffer->pool.strings[i], str) == 0) {
            return i;
        }
    }

    if (buffer->pool.count >= buffer->pool.capacity) {
        buffer->pool.capacity *= 2;
        buffer->pool.strings = (char**)realloc(buffer->pool.strings, sizeof(char*) * buffer->pool.capacity);
    }

    buffer->pool.strings[buffer->pool.count] = strdup(str);
    return buffer->pool.count++;
}

// Recursively traverses the AST and emits ISA instructions based on virtual registers
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt) {
    if (!buffer || !stmt) return;

    switch (stmt->type) {
        case NODE_VAR_DECL: {
            // If there is an initialization expression/literal
            if (stmt->data.var_decl.initializer_raw) {
                const char* init_val = stmt->data.var_decl.initializer_raw;

                // If the initializer is a string, emit the corresponding pool index
                if (init_val[0] == '"' || init_val[0] == '`') {
                    int pool_index = codegen_li_add_string_constant(buffer, init_val);
                    emit_byte(buffer, OP_SCONST);
                    emit_int(buffer, pool_index);
                } else {
                    // Treat as a basic integer for the IL by default at this stage
                    int num = atoi(init_val);
                    emit_byte(buffer, OP_ICONST);
                    emit_int(buffer, num);
                }

                // Emit the store instruction for the local variable
                emit_byte(buffer, OP_STORE);
                int var_id = codegen_li_add_string_constant(buffer, stmt->data.var_decl.var_name);
                emit_int(buffer, var_id);
            }
            break;
        }

        case NODE_FOR_LOOP: {
            // 1. Execute the loop initialization statement (e.g.: mut i: i32)
            if (stmt->data.for_loop.init_stmt) {
                codegen_li_emit_statement(buffer, stmt->data.for_loop.init_stmt);
            }

            int loop_condition_address = buffer->size;

            // 2. Emit a simulation of the condition read as raw text (resolved in bytecode via jumps)
            emit_byte(buffer, OP_LOAD);
            emit_int(buffer, 0); // Assume temporary loop register
            emit_byte(buffer, OP_JMP_IF_FALSE);
            int jump_patch_address = buffer->size;
            emit_int(buffer, 0); // Reserved slot (patch) to jump out of the loop

            // 3. Recursively emit the statements contained in the for-loop body block
            if (stmt->data.for_loop.body_statements) {
                for (int i = 0; i < stmt->data.for_loop.body_count; i++) {
                    codegen_li_emit_statement(buffer, stmt->data.for_loop.body_statements[i]);
                }
            }

            // 4. Jump back to re-evaluate the condition
            emit_byte(buffer, OP_JMP);
            emit_int(buffer, loop_condition_address);

            // 5. Fix the loop exit jump address (backpatching)
            int loop_exit_address = buffer->size;
            buffer->code[jump_patch_address] = (loop_exit_address >> 0) & 0xFF;
            buffer->code[jump_patch_address + 1] = (loop_exit_address >> 8) & 0xFF;
            buffer->code[jump_patch_address + 2] = (loop_exit_address >> 16) & 0xFF;
            buffer->code[jump_patch_address + 3] = (loop_exit_address >> 24) & 0xFF;
            break;
        }

        default:
            // Intercept residual expressions and emit an implicit safety return
            emit_byte(buffer, OP_RETURN);
            break;
    }
}

// Serializes the portable compiled binary file (.tkb), structuring the header
void codegen_li_write_to_file(const BytecodeBuffer* buffer, const char* filename) {
    if (!buffer || !filename) return;

    FILE* file = fopen(filename, "wb");
    if (!file) return;

    // 1. Write the identification Magic Header (Teko Magic Number: TEKO = 0x54454B4F)
    unsigned char magic[4] = {0x54, 0x45, 0x4B, 0x4F};
    fwrite(magic, 1, 4, file);

    // 2. Serialize the record count and the body of the String Pool
    fwrite(&buffer->pool.count, sizeof(int), 1, file);
    for (int i = 0; i < buffer->pool.count; i++) {
        int str_len = (int)strlen(buffer->pool.strings[i]);
        fwrite(&str_len, sizeof(int), 1, file);
        fwrite(buffer->pool.strings[i], 1, str_len, file);
    }

    // 3. Serialize the contiguous linear block of IL opcodes
    fwrite(&buffer->size, sizeof(int), 1, file);
    fwrite(buffer->code, 1, buffer->size, file);

    fclose(file);
}

// Thorough heap memory cleanup of the emitter subsystem
void codegen_li_free_context(BytecodeBuffer* buffer) {
    if (!buffer) return;

    if (buffer->code) free(buffer->code);
    for (int i = 0; i < buffer->pool.count; i++) {
        if (buffer->pool.strings[i]) free(buffer->pool.strings[i]);
    }
    free(buffer->pool.strings);
    free(buffer);
}