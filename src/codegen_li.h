#ifndef CODEGEN_LI_H
#define CODEGEN_LI_H

#include "parser_statements.h"
#include "parser_visibility.h"

// --- TEKO IL ISA (OPCODES) DEFINITION ---
typedef enum {
    OP_PROLOG = 0xFE,
    OP_EPILOG = 0xFF,

    OP_HALT = 0x00,
    OP_ICONST = 0x01,
    OP_SCONST = 0x02,
    OP_LOAD = 0x03,
    OP_STORE = 0x04,
    OP_ADD = 0x05,
    OP_SUB = 0x06,
    OP_MUL = 0x07,
    OP_DIV = 0x08,

    // Concurrency and Channels
    OP_SPAWN_ASYNC = 0x10,
    OP_AWAIT_INTENT = 0x11,
    OP_CHAN_INIT = 0x12,
    OP_CHAN_PUT = 0x13,

    // Control Flow and Branches
    OP_JMP = 0x20,
    OP_JMP_IF_FALSE = 0x21,
    OP_RETURN = 0x22,

    // --- NEW: BARE-METAL COMPATIBLE ARENA MANAGEMENT ---
    OP_ARENA_PUSH = 0x30, // Starts a contiguous region on silicon
    OP_ARENA_POP = 0x31   // O(1) batch cleanup via hardware
} OpCode;

// Internal structure for the constant string symbol table (String Pool)
typedef struct {
    char** strings;
    int count;
    int capacity;
} ConstantStringPool;

// Structure representing the final binary bytecode buffer in memory
typedef struct {
    unsigned char* code;
    int size;
    int capacity;
    ConstantStringPool pool;
} BytecodeBuffer;

// Public functions of the IL Bytecode Emitter
BytecodeBuffer* codegen_li_create_context(void);
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt);
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str);
void codegen_li_write_to_file(const BytecodeBuffer* buffer, const char* filename);
void codegen_li_free_context(BytecodeBuffer* buffer);

#endif // CODEGEN_LI_H