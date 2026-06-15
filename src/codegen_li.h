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

    // Phase 11 (Browser FFI): call a host import declared via `extern fn … from
    // "ns" as "name"`. Carries a 4-byte little-endian import index (into the
    // module's import table); the WASM emitter lowers it to `call $import_<idx>`.
    OP_CALL_IMPORT = 0x09,

    // Phase 11 (Browser FFI MVP-2): stage an argument for a multi-param host
    // import. Carries a 4-byte little-endian arg-slot index; the WASM emitter
    // copies the accumulator ($w0) into staging local $a<slot>. OP_CALL_IMPORT
    // then pushes $a0..$a(n-2) followed by $w0 (the last arg stays in the
    // accumulator), so an N-param import needs N-1 preceding OP_SETARGs.
    OP_SETARG = 0x0A,

    // Concurrency and Channels
    OP_SPAWN_ASYNC = 0x10,
    OP_AWAIT_INTENT = 0x11,
    OP_CHAN_INIT = 0x12,
    OP_CHAN_PUT = 0x13,
    OP_CHAN_GET = 0x14, // Blocking channel receive (yields to the scheduler if empty)

    // Phase 10.2b: function-boundary opcodes for green-thread lowering. Each
    // routine body is emitted as a separate WASM function indexed in a table so
    // SPAWN_ASYNC can dispatch it via call_indirect. OP_FUNC_BEGIN carries a
    // 4-byte little-endian routine id (its table slot); OP_FUNC_END closes it.
    OP_FUNC_BEGIN = 0x40,
    OP_FUNC_END = 0x41,

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

// Phase 11 (Browser FFI frontend): a host import the IL declares, lowered from an
// `extern fn … from "ns" as "name"` (or a `@dom`/`@js` intrinsic). The WASM backend
// turns each into an `(import "ns" "name" (func …))`; OP_CALL_IMPORT carries the
// index into this table. Kept frontend-side so the parser populates it directly.
typedef struct {
    char* ns;        // import module/namespace, e.g. "env" or "dom"
    char* name;      // imported field, e.g. "log" or "setText"
    int n_params;    // i32 params
    int has_result;  // 1 if it returns an i32
} TekoILImport;

// Structure representing the final binary bytecode buffer in memory
typedef struct {
    unsigned char* code;
    int size;
    int capacity;
    ConstantStringPool pool;
    // Phase 11: the import table populated from `extern`/`@dom` declarations.
    TekoILImport* imports;
    int import_count;
    int import_capacity;
} BytecodeBuffer;

// Public functions of the IL Bytecode Emitter
BytecodeBuffer* codegen_li_create_context(void);
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt);
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str);
void codegen_li_write_to_file(const BytecodeBuffer* buffer, const char* filename);
void codegen_li_free_context(BytecodeBuffer* buffer);

// Phase 11 (Browser FFI frontend): register a host import (deduped by ns+name) and
// return its table index — the arg for OP_CALL_IMPORT.
int codegen_li_add_import(BytecodeBuffer* buffer, const char* ns, const char* name,
                          int n_params, int has_result);

// Phase 11 (Browser FFI frontend): IL emit helpers used by the parser→IL lowering of
// the interop surface. Each appends to `buffer->code`.
void codegen_li_emit_iconst(BytecodeBuffer* buffer, int value);
void codegen_li_emit_sconst(BytecodeBuffer* buffer, int pool_index);
void codegen_li_emit_store(BytecodeBuffer* buffer); // $w1 <- $w0
void codegen_li_emit_load(BytecodeBuffer* buffer);  // $w0 <- $w1
void codegen_li_emit_setarg(BytecodeBuffer* buffer, int slot);
void codegen_li_emit_call_import(BytecodeBuffer* buffer, int import_index);
void codegen_li_emit_func_begin(BytecodeBuffer* buffer, int routine_id);
void codegen_li_emit_func_end(BytecodeBuffer* buffer);
void codegen_li_emit_halt(BytecodeBuffer* buffer);

#endif // CODEGEN_LI_H