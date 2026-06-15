#ifndef CODEGEN_METAL_H
#define CODEGEN_METAL_H

#include <stdio.h>
#include <stdint.h>
#include "../teko_target.h"
#include "../codegen_li.h"

// Phase 11 (Browser FFI): a host import (from `extern fn … from "ns" as "name"`).
// Params 0..n_params-2 are staged via OP_SETARG into $a0..$a(n-2); the last param
// comes from the accumulator $w0 (MVP-2). has_result stores the call result back
// into $w0. The `ns` doubles as the glue group ("dom" imports get DOM marshalling).
typedef struct {
    const char* ns;     // import module/namespace, e.g. "env"
    const char* name;   // imported field name, e.g. "log"
    int n_params;       // i32 params
    int has_result;     // 1 if the import returns an i32
} TekoWasmImport;

// Context of the contiguous bare-metal emitter
typedef struct {
    FILE* file;
    TekoTarget target;
    uint32_t label_count;
    // Phase 10.2b/10.3 WASM multi-function state (unused by the native emitters):
    //  wasm_open       - 0 nothing open, 1 $main open, 2 a $routine_N open
    //  routine_count   - number of green-thread functions emitted (table size)
    //  routine_ids     - their ids, for the (elem ...) table-init at module close
    //  routine_yields  - yield points (suspending CHAN_GETs) in the current
    //                    routine, counted by the orchestrator before FUNC_BEGIN;
    //                    sizes the state-machine block nest + br_table (10.3)
    //  yield_idx       - which yield point we are emitting within the routine
    int wasm_open;
    int wasm_routine_count;
    int wasm_routine_ids[64];
    int wasm_routine_yields;
    int wasm_yield_idx;
    // Phase 11 (Browser FFI): the IL string constant pool, threaded in so the WASM
    // emitter can lay it out as a real (data ...) segment and resolve OP_SCONST to a
    // true byte offset (instead of a placeholder). NULL/0 when unset — the emitter
    // then falls back to its legacy hardcoded data. Unused by the native emitters.
    const char** wasm_strings;
    int wasm_string_count;
    // Phase 11 (Browser FFI): host import table (from `extern fn … from "ns" as
    // "name"`). The WASM emitter declares `(import …)` per entry and lowers
    // OP_CALL_IMPORT to `call $import_<idx>`. Unused by the native emitters.
    const TekoWasmImport* wasm_imports;
    int wasm_import_count;
    // Phase 12 (Frontend Grammar): number of named local variables ($v0..$v{n-1}) to
    // declare in $main. 0 = none (legacy/emit-demo programs). Unused by native emitters.
    int wasm_local_count;
    // Phase 12 (P12-G): 1 to emit the base64/hex codec runtime (only when the program
    // uses it). Unused by the native emitters.
    int wasm_emit_codecs;
    // Phase 13 (13.1): 1 to emit the in-module SHA hash runtime (only when the program
    // calls hash.sha256/.sha512). Unused by the native emitters.
    int wasm_emit_hash;
    // Phase 13 (Sub-phase C): 1 to declare the host entropy import (env.teko_random) + the
    // in-module CSPRNG hex wrapper (only when the program calls random.bytes). Native
    // emitters ignore this (they link the C CSPRNG into the binary via teko_rt).
    int wasm_emit_random;
    // Phase 13 (Sub-phase C): 1 to declare the host entropy + time imports (env.teko_random,
    // env.teko_now) + the self-contained in-module uuid.v4/v7 runtime. Native emitters ignore
    // this (they link the C uuid runtime + CSPRNG into the binary via teko_rt).
    int wasm_emit_uuid_rng;
    // Phase 13 (Sub-phase C, "big step"): 1 to import the compiled-C crypto reactor
    // (crypto.wasm) — declare its teko_rt_* entry points from the "crypto" module and
    // share one linear memory with it (memory imported from env instead of module-owned).
    // Set when the program uses a crypto primitive beyond the in-module hash/uuid set
    // (ids 5,10-40). Native emitters ignore this (they link the same C runtime directly).
    int wasm_emit_crypto_ext;
    // Phase 13 (native runner): 1 routes x86_64/arm64 emission to the libc-hosted,
    // assemble-able emitter (emit_native_hosted.c) instead of the freestanding "metal"
    // emitters — produces a binary the system `cc` links against teko_rt and RUNS. The
    // wasm_strings / wasm_imports / wasm_local_count fields above are reused as the
    // generic IL string pool / import table / local count for this path too.
    int hosted;
} MetalContext;

// Phase 11: hand the WASM emitter the IL string pool before teko_metal_emit_program
// so it can emit the (data ...) segment and correct OP_SCONST offsets. `strings`
// must outlive the emit call; pass count 0 (or do not call) to keep legacy behavior.
void teko_metal_set_strings(MetalContext* ctx, const char** strings, int count);

// Phase 11: hand the WASM emitter the host import table before teko_metal_emit_program
// so it can declare `(import …)` and lower OP_CALL_IMPORT. `imports` must outlive the
// emit call.
void teko_metal_set_imports(MetalContext* ctx, const TekoWasmImport* imports, int count);

// Phase 12: declare `count` named local i32 variables ($v0..$v{count-1}) in $main.
// Call before teko_metal_emit_program; 0 (or not calling) keeps the legacy behavior.
void teko_metal_set_local_count(MetalContext* ctx, int count);

// Phase 12 (P12-G): request the base64/hex codec runtime functions be emitted.
void teko_metal_set_emit_codecs(MetalContext* ctx, int enabled);
void teko_metal_set_emit_hash(MetalContext* ctx, int enabled);
void teko_metal_set_emit_random(MetalContext* ctx, int enabled);
void teko_metal_set_emit_uuid_rng(MetalContext* ctx, int enabled);
void teko_metal_set_emit_crypto_ext(MetalContext* ctx, int enabled);

// Phase 13 (native runner): route x86_64/arm64 emission to the libc-hosted emitter.
void teko_metal_set_hosted(MetalContext* ctx, int enabled);

// Phase 11 (Browser FFI MVP-2): write an auto-generated JS glue module to `path`
// that implements the `dom.*` host imports currently set on `ctx` (via
// teko_metal_set_imports) — (ptr,len) string marshalling over the module's linear
// memory plus an i32->Element handle table. No dev boilerplate. Returns 0 on
// success, non-zero if the file cannot be opened. Imports outside the "dom"
// namespace are ignored. Call after teko_metal_set_imports.
int teko_metal_emit_dom_glue(MetalContext* ctx, const char* path);

// Phase 11 (Browser FFI MVP-4): one ergonomic facade method — `name` calls Teko
// table slot `fn_index` via teko_invoke2 with a single JS string arg marshalled
// automatically (alloc + write + call). (MVP supports the one-string-arg shape.)
typedef struct {
    const char* name;   // facade method name, e.g. "showMessage"
    int fn_index;       // Teko callback routine's table slot
} TekoWasmFacadeEntry;

// Phase 11 (Browser FFI MVP-4): write an auto-generated ergonomic facade module to
// `path`. It imports the glue module `glue_module` (a sibling path, e.g.
// "./foo.glue.mjs"), exposes `async instantiate(wasmBytes)`, and returns an object
// whose `entries[i].name(str)` marshals the JS string into wasm memory via the real
// allocator and invokes the Teko routine — no dev boilerplate. Returns 0 on success.
int teko_metal_emit_facade(MetalContext* ctx, const char* path, const char* glue_module,
                           const TekoWasmFacadeEntry* entries, int count);

// 1. APPLE ECOSYSTEM (Darwin Kernel)
void emit_darwin_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_darwin_x86_64(MetalContext* ctx, OpCode op, int32_t arg);

// 2. LINUX ECOSYSTEM (ELF ABI)
void emit_linux_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_arm32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_riscv32(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_mips(MetalContext* ctx, OpCode op, int32_t arg);
void emit_linux_ppc64(MetalContext* ctx, OpCode op, int32_t arg);

// 3. WINDOWS ECOSYSTEM (PE/COFF Vector)
void emit_win_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_x86(MetalContext* ctx, OpCode op, int32_t arg);
void emit_win_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 4. BSD UNIX ECOSYSTEM (Native FreeBSD)
void emit_freebsd_x86_64(MetalContext* ctx, OpCode op, int32_t arg);
void emit_freebsd_arm64(MetalContext* ctx, OpCode op, int32_t arg);

// 5. EMBEDDED AND VIRTUAL ENVIRONMENTS (Bare-Metal)
void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg);

// Lifecycle signatures of the Central Orchestrator
MetalContext* teko_metal_create(const char* output_asm_path, TekoTarget target);
void teko_metal_emit_program(MetalContext* ctx, const unsigned char* bytecode, uint32_t size);
void teko_metal_close(MetalContext* ctx);

#endif // CODEGEN_METAL_H
