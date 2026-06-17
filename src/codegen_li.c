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

    buffer->import_capacity = 8;
    buffer->import_count = 0;
    buffer->imports = (TekoILImport*)malloc(sizeof(TekoILImport) * buffer->import_capacity);

    buffer->local_count = 0;
    buffer->uses_codec = 0;
    buffer->uses_hash = 0;
    buffer->uses_random = 0;
    buffer->uses_uuid_rng = 0;
    buffer->uses_crypto_ext = 0;
    buffer->uses_spawn = 0;
    buffer->uses_duplex = 0;
    buffer->uses_delayed = 0;
    buffer->uses_bcast = 0;
    buffer->uses_shared = 0;
    buffer->uses_wait = 0;
    buffer->uses_await = 0;
    buffer->uses_retry = 0;
    buffer->uses_object = 0;
    buffer->uses_array = 0;
    buffer->uses_iarray = 0;
    buffer->uses_simd = 0;
    buffer->uses_vtable = 0;

    // Phase 17 (17.A): the float-constant pool starts empty; uses_float gates the WASM float locals.
    buffer->float_capacity = 8;
    buffer->float_count = 0;
    buffer->floats = (double*)malloc(sizeof(double) * buffer->float_capacity);
    buffer->uses_float = 0;

    // Phase 17.F.3: the decimal-constant pool (256-byte blobs) starts empty; uses_decimal gates the
    // WASM decimal linear-memory region + reactor imports and the native decimal frame region.
    buffer->decimal_capacity = 4;
    buffer->decimal_count = 0;
    buffer->decimals = (unsigned char*)malloc((size_t)256 * buffer->decimal_capacity);
    buffer->uses_decimal = 0;

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

// Phase 17 (17.A): add an f64 to the float pool (deduped by EXACT bit-equality so -0.0 and 0.0 are
// distinct, NaN payloads preserved), returning its index — the 4-byte arg for OP_FCONST.
int codegen_li_add_float_constant(BytecodeBuffer* buffer, double value) {
    if (!buffer) return -1;

    unsigned char vb[8];
    memcpy(vb, &value, 8); // read the value's bit pattern without aliasing UB
    for (int i = 0; i < buffer->float_count; i++) {
        unsigned char eb[8];
        memcpy(eb, &buffer->floats[i], 8);
        if (memcmp(vb, eb, 8) == 0) return i; // exact bit-equal constant already pooled
    }

    if (buffer->float_count >= buffer->float_capacity) {
        buffer->float_capacity *= 2;
        buffer->floats = (double*)realloc(buffer->floats, sizeof(double) * buffer->float_capacity);
    }
    buffer->floats[buffer->float_count] = value;
    return buffer->float_count++;
}

// Phase 17.F.3: add a 256-byte decimal constant to the decimal pool (deduped by 256-byte memcmp),
// returning its index — the 4-byte arg for OP_DCONST. `blob` is a teko_decimal value (by pointer).
int codegen_li_add_decimal_constant(BytecodeBuffer* buffer, const unsigned char* blob) {
    if (!buffer || !blob) return -1;

    for (int i = 0; i < buffer->decimal_count; i++) {
        if (memcmp(blob, buffer->decimals + (size_t)256 * i, 256) == 0) return i; // already pooled
    }

    if (buffer->decimal_count >= buffer->decimal_capacity) {
        buffer->decimal_capacity *= 2;
        buffer->decimals = (unsigned char*)realloc(buffer->decimals, (size_t)256 * buffer->decimal_capacity);
    }
    memcpy(buffer->decimals + (size_t)256 * buffer->decimal_count, blob, 256);
    return buffer->decimal_count++;
}

// Registers a host import (deduped by ns+name); returns its table index.
int codegen_li_add_import(BytecodeBuffer* buffer, const char* ns, const char* name,
                          int n_params, int has_result) {
    if (!buffer || !ns || !name) return -1;

    for (int i = 0; i < buffer->import_count; i++) {
        if (strcmp(buffer->imports[i].ns, ns) == 0 &&
            strcmp(buffer->imports[i].name, name) == 0) {
            return i;
        }
    }

    if (buffer->import_count >= buffer->import_capacity) {
        buffer->import_capacity *= 2;
        buffer->imports = (TekoILImport*)realloc(buffer->imports,
                                                 sizeof(TekoILImport) * buffer->import_capacity);
    }

    TekoILImport* im = &buffer->imports[buffer->import_count];
    im->ns = strdup(ns);
    im->name = strdup(name);
    im->n_params = n_params;
    im->has_result = has_result;
    return buffer->import_count++;
}

// --- IL emit helpers for the interop lowering (parser → IL) ---
void codegen_li_emit_iconst(BytecodeBuffer* buffer, int value) {
    if (!buffer) return;
    emit_byte(buffer, OP_ICONST);
    emit_int(buffer, value);
}

void codegen_li_emit_sconst(BytecodeBuffer* buffer, int pool_index) {
    if (!buffer) return;
    emit_byte(buffer, OP_SCONST);
    emit_int(buffer, pool_index);
}

void codegen_li_emit_setarg(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    emit_byte(buffer, OP_SETARG);
    emit_int(buffer, slot);
}

void codegen_li_emit_store(BytecodeBuffer* buffer) {
    if (!buffer) return;
    emit_byte(buffer, OP_STORE); // $w1 <- $w0
}

void codegen_li_emit_load(BytecodeBuffer* buffer) {
    if (!buffer) return;
    emit_byte(buffer, OP_LOAD);  // $w0 <- $w1
}

void codegen_li_emit_store_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    emit_byte(buffer, OP_STORE_LOCAL);
    emit_int(buffer, slot);
}

void codegen_li_emit_load_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    emit_byte(buffer, OP_LOAD_LOCAL);
    emit_int(buffer, slot);
}

void codegen_li_emit_binop(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    emit_byte(buffer, (unsigned char)op); // single-byte: ADD/SUB/MUL/DIV/MOD/EQ/NE/LT/LE/GT/GE
}

void codegen_li_emit_call_runtime(BytecodeBuffer* buffer, int codec_id) {
    if (!buffer) return;
    // Flag which runtime block the backend must emit: ids 0-3 are the base64/hex
    // codecs (Phase 12-G); ids >= 4 are the SHA hash primitives (Phase 13.1).
    // id 41 = random.bytes: a CSPRNG that needs a host entropy import on WASM (its own
    // runtime block), not the SHA family — flag it separately so hash-free programs that
    // only use randomness don't drag in the hash runtime.
    // WASM emission flags (native ignores these — it always links libteko_rt.a):
    //  - ids 0-3  : base64/hex codecs (in-module).
    //  - ids 4,6,7,8,9 : sha256 + legacy md5/sha1 + uuid v3/v5 — in-module WAT runtimes.
    //  - id 41 / 42,43 : CSPRNG / uuid v4,v7 — in-module + host entropy/time imports.
    //  - everything else >= 4 (sha512/384, sha3, shake, blake, HMAC, AEAD, KDF, X25519,
    //    Ed25519, ECDSA, RSA) : the compiled-C crypto reactor, imported on WASM.
    if (codec_id == 41) buffer->uses_random = 1;
    else if (codec_id == 42 || codec_id == 43) buffer->uses_uuid_rng = 1; // uuid.v4/v7
    else if (codec_id == 4 || codec_id == 6 || codec_id == 7 ||
             codec_id == 8 || codec_id == 9) buffer->uses_hash = 1;       // in-module set
    else if (codec_id >= 4) buffer->uses_crypto_ext = 1;                  // reactor set
    else buffer->uses_codec = 1;
    // Phase 17.D/17.E — id 50 (float->string, reads $f0) and id 54 (parse_float, WRITES $f0) are the
    // two f64-ABI runtime calls: both touch the float accumulator, so the WASM float locals MUST be
    // declared. Set uses_float (id 54 may be the SOLE float op in a module — e.g. a parse that only
    // feeds an int via convert.to_int — so this is the load-bearing flag, not merely defensive).
    if (codec_id == 50 || codec_id == 54) buffer->uses_float = 1;
    // Phase 17.F.4 — ids 59 (decimal.to_string) and 60 (decimal.parse) touch the 256-byte decimal
    // accumulator $d0 (by pointer), so the decimal frame region / WASM linear-memory slots + the
    // reactor teko_rt_decimal_* imports MUST be emitted. Set uses_decimal (id 59 may be the SOLE
    // decimal op in a module — e.g. an auto-to_string of a parsed decimal — so this is load-bearing).
    if (codec_id == 59 || codec_id == 60) buffer->uses_decimal = 1;
    emit_byte(buffer, OP_CALL_RUNTIME);
    emit_int(buffer, codec_id);
}

void codegen_li_emit_call_import(BytecodeBuffer* buffer, int import_index) {
    if (!buffer) return;
    emit_byte(buffer, OP_CALL_IMPORT);
    emit_int(buffer, import_index);
}

void codegen_li_emit_func_begin(BytecodeBuffer* buffer, int routine_id) {
    if (!buffer) return;
    emit_byte(buffer, OP_FUNC_BEGIN);
    emit_int(buffer, routine_id);
}

void codegen_li_emit_func_end(BytecodeBuffer* buffer) {
    if (!buffer) return;
    emit_byte(buffer, OP_FUNC_END);
}

void codegen_li_emit_spawn_async(BytecodeBuffer* buffer) {
    if (!buffer) return;
    buffer->uses_spawn = 1; // backends drain the scheduler before program exit
    emit_byte(buffer, OP_SPAWN_ASYNC);
}

void codegen_li_emit_spawn_async_args(BytecodeBuffer* buffer, int argc) {
    if (!buffer) return;
    buffer->uses_spawn = 1; // backends drain the scheduler + emit the routine table
    emit_byte(buffer, OP_SPAWN_ASYNC_ARGS);
    emit_int(buffer, argc);
}

void codegen_li_emit_load_spawn_arg(BytecodeBuffer* buffer, int idx) {
    if (!buffer) return;
    emit_byte(buffer, OP_LOAD_SPAWN_ARG);
    emit_int(buffer, idx);
}

void codegen_li_emit_duplex(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_duplex = 1; // backends link/import the duplex C runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_delayed(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_delayed = 1; // backends link/import the delayed-channel C runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_bcast(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_bcast = 1; // backends link/import the broadcast C runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_shared(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_shared = 1; // backends link/import the teko_shared runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_wait(BytecodeBuffer* buffer) {
    if (!buffer) return;
    buffer->uses_wait = 1; // WASM declares env.teko_sleep; native links teko_rt_sleep_ms
    emit_byte(buffer, OP_WAIT);
}

void codegen_li_emit_await(BytecodeBuffer* buffer) {
    if (!buffer) return;
    buffer->uses_await = 1; // WASM declares env.teko_await + drains the scheduler
    buffer->uses_spawn = 1; // native: emit the routine table the scheduler TU drains through
    emit_byte(buffer, OP_AWAIT_FOR);
}

void codegen_li_emit_cf(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    emit_byte(buffer, (unsigned char)op); // single-byte structured control-flow opcode
}

void codegen_li_emit_retry(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_retry = 1; // backends link/import the teko_retry policy runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_object(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_object = 1; // backends link/import the teko_object instance-store runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_array(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_array = 1; // backends link/import the teko_array fixed-size-array runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_iarray(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_iarray = 1; // backends link/import the teko_iarray packed-i32-array runtime
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_simd(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_simd = 1;   // backend emits the per-ISA vector kernel (teko_simd_sum_i32) once
    buffer->uses_iarray = 1; // the run is a typed i32[]: the data/len/scalar wrappers come from there
    emit_byte(buffer, (unsigned char)op);
}

void codegen_li_emit_call_func(BytecodeBuffer* buffer, int argc) {
    if (!buffer) return;
    buffer->uses_spawn = 1; // needs the routine table + the scheduler TU (teko_rt_call lives there)
    emit_byte(buffer, OP_CALL_FUNC);
    emit_int(buffer, argc);
}

void codegen_li_emit_vtable(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_vtable = 1; // backends link/import the teko_vtable static-dispatch runtime
    emit_byte(buffer, (unsigned char)op);
}

// Phase 17 (17.A): float value-model emit helpers. Each sets uses_float so the WASM backend
// declares the parallel float accumulator locals ($f0/$f1/$fvN); the integer path stays untouched.
void codegen_li_emit_fconst(BytecodeBuffer* buffer, double v) {
    if (!buffer) return;
    buffer->uses_float = 1;
    int idx = codegen_li_add_float_constant(buffer, v);
    emit_byte(buffer, OP_FCONST);
    emit_int(buffer, idx); // 4-byte pool index (NOT the 64-bit immediate)
}

void codegen_li_emit_funop(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_float = 1;
    emit_byte(buffer, (unsigned char)op); // single-byte: FADD..FGE / FMOD / FSTORE / FLOAD / I2F
}

// Phase 17 (17.B): emit OP_F2I — CHECKED float->int (truncate toward zero, fail-loud on
// NaN/±Inf/out-of-i32-range). Single-byte; sets uses_float so the float accumulator locals are
// declared on WASM even when the program only reads $f0 to produce an int.
void codegen_li_emit_f2i(BytecodeBuffer* buffer) {
    if (!buffer) return;
    buffer->uses_float = 1;
    emit_byte(buffer, OP_F2I);
}

void codegen_li_emit_fstore_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    buffer->uses_float = 1;
    emit_byte(buffer, OP_FSTORE_LOCAL);
    emit_int(buffer, slot);
}

void codegen_li_emit_fload_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    buffer->uses_float = 1;
    emit_byte(buffer, OP_FLOAD_LOCAL);
    emit_int(buffer, slot);
}

// Phase 17.F.3: decimal value-model emit helpers. Each sets uses_decimal (gates the WASM decimal
// linear-memory region + reactor imports / the native decimal frame region). Mirrors the float set.
void codegen_li_emit_dconst(BytecodeBuffer* buffer, const unsigned char* blob) {
    if (!buffer) return;
    buffer->uses_decimal = 1;
    int idx = codegen_li_add_decimal_constant(buffer, blob);
    emit_byte(buffer, OP_DCONST);
    emit_int(buffer, idx); // 4-byte decimal-pool index
}

void codegen_li_emit_dunop(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_decimal = 1;
    emit_byte(buffer, (unsigned char)op); // single-byte: DADD..DGE / DSTORE / DLOAD
}

void codegen_li_emit_dstore_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    buffer->uses_decimal = 1;
    emit_byte(buffer, OP_DSTORE_LOCAL);
    emit_int(buffer, slot);
}

void codegen_li_emit_dload_local(BytecodeBuffer* buffer, int slot) {
    if (!buffer) return;
    buffer->uses_decimal = 1;
    emit_byte(buffer, OP_DLOAD_LOCAL);
    emit_int(buffer, slot);
}

// Phase 17.F.4: emit a single-byte int/float↔decimal CAST opcode (OP_I2D/F2D/D2I/D2F). Sets
// uses_decimal so the decimal frame region / WASM slots + the teko_rt_decimal_* casts exist. I2D
// also reads $w0 / F2D reads $f0 / D2I writes $w0 / D2F writes $f0 — the backends marshal those.
void codegen_li_emit_dcast(BytecodeBuffer* buffer, OpCode op) {
    if (!buffer) return;
    buffer->uses_decimal = 1;
    // OP_F2D/OP_D2F also touch the float accumulator $f0, so the WASM float locals must exist.
    if (op == OP_F2D || op == OP_D2F) buffer->uses_float = 1;
    emit_byte(buffer, (unsigned char)op); // single-byte: OP_I2D / OP_F2D / OP_D2I / OP_D2F
}

void codegen_li_emit_halt(BytecodeBuffer* buffer) {
    if (!buffer) return;
    emit_byte(buffer, OP_HALT);
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
    for (int i = 0; i < buffer->import_count; i++) {
        if (buffer->imports[i].ns) free(buffer->imports[i].ns);
        if (buffer->imports[i].name) free(buffer->imports[i].name);
    }
    free(buffer->imports);
    free(buffer->floats); // Phase 17 (17.A): the float-constant pool
    free(buffer->decimals); // Phase 17.F.3: the decimal-constant pool (256-byte blobs)
    free(buffer);
}