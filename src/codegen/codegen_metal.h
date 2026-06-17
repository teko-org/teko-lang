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
    // Phase 14 (14.A): 1 when the program fires background tasks (`routines { … }` →
    // OP_SPAWN_ASYNC). WASM emits `call $teko_sched_run` at $main close so spawned routines
    // run before exit; the native runner emits the routine function-pointer table + a
    // `teko_rt_run` drain at HALT. Spawn-free programs are byte-identical (flag stays 0).
    int wasm_emit_spawn;
    // Phase 14 (14.B): 1 when the program uses a duplex channel op (OP_DUPLEX_*). The WASM
    // backend then imports the duplex entry points (teko_rt_duplex_*) from the runtime reactor
    // and shares its linear memory (same mechanism as wasm_emit_crypto_ext). Native ignores it.
    int wasm_emit_duplex;
    // Phase 14 (14.C): 1 when the program uses a delayed-channel op (OP_DELAYED_*). Same
    // reactor-import + shared-memory wiring as wasm_emit_duplex.
    int wasm_emit_delayed;
    // Phase 14 (14.D): 1 when the program uses a broadcast op (OP_BCAST_*). Same reactor-import
    // + shared-memory wiring as wasm_emit_duplex/delayed.
    int wasm_emit_bcast;
    // Phase 14 (14.E): 1 when the program uses a `shared { }` block / `atomic.*` op. Imports the
    // teko_shared_*/teko_atomic_* entry points from the reactor + shares memory.
    int wasm_emit_shared;
    // Phase 14 (14.G): 1 when the program uses `wait <ts>;` (OP_WAIT) → declare the host sleep
    // import (env.teko_sleep). Native ignores it (links teko_rt_sleep_ms unconditionally).
    int wasm_emit_wait;
    // Phase 14 (14.G): 1 when the program uses `await <ts>;` (OP_AWAIT_FOR) → declare the host
    // import (env.teko_await) and drain $teko_sched_run. Native ignores it (links teko_rt_await_ms).
    int wasm_emit_await;
    // Phase 14 (14.F): 1 when the program uses a `retry`/`circuit` resilience block (OP_RETRY_*/
    // OP_CIRCUIT_*). The WASM backend imports the teko_rt_retry_*/teko_rt_circuit_* entry points
    // from the runtime reactor + shares its linear memory (same wiring as the channel families).
    int wasm_emit_retry;
    // Phase 15 (15.A): 1 when the program uses an object op (OP_OBJ_*) — i.e. instantiates a
    // `class`. The WASM backend imports the teko_rt_object_* entry points from the runtime
    // reactor + shares its linear memory (same wiring as the channel families). Native ignores
    // it (links teko_rt_object_* via libteko_rt.a).
    int wasm_emit_object;
    // Phase 15 (15.B): 1 when the program uses a static-vtable op (OP_VTABLE_*) — abstract/trait
    // dynamic dispatch. WASM imports teko_rt_vtable_* from the reactor + shares memory; native
    // links them via libteko_rt.a.
    int wasm_emit_vtable;
    // Phase 18 (18.E.1): 1 when the program uses a fixed-size array op (OP_ARR_*). The WASM backend
    // imports the teko_rt_array_* entry points from the runtime reactor + shares its linear memory
    // (same wiring as OP_OBJ_*). Native ignores it (links teko_rt_array_* via libteko_rt.a). Gating
    // ALL array emission on this keeps array-free output byte-identical (the 16 freestanding goldens).
    int wasm_emit_array;
    // Phase 18 (18.E.2): 1 when the program uses a TYPED `i32[]` packed-array op (OP_IARR_*). The
    // WASM backend imports the teko_rt_iarray_* entry points from the runtime reactor + shares its
    // linear memory (same wiring as OP_ARR_*). Native ignores it (links teko_rt_iarray_* via
    // libteko_rt.a). Gating ALL iarray emission on this keeps iarray-free output byte-identical.
    int wasm_emit_iarray;
    // Phase 18 (18.E.4): 1 when the program uses the REAL SIMD reduction (OP_SIMD_SUM). The backend
    // emits ONE per-ISA vector kernel function (teko_simd_sum_i32) — native SSE2/NEON asm (scalar on
    // other arches), WASM a simd128 func — and lowers each OP_SIMD_SUM to a call into it after fetching
    // the run's data pointer + length. WASM additionally imports teko_rt_iarray_data from the reactor.
    // Gating ALL SIMD emission on this keeps simd-free output byte-identical (the 16 goldens never see it).
    int wasm_emit_simd;
    // Phase 14 (control-flow foundation): structured loop/if lowering state, shared by the native
    // hosted emitter and the WASM emitter. cf_id_next assigns a fresh monotonic id to each
    // LOOP_BEGIN/IF_BEGIN; cf_loop_stack/cf_if_stack track the active (nesting) ids so
    // BREAK/CONTINUE/BREAK_IF_FALSE/LOOP_END and IF_END resolve to the innermost construct.
    int cf_id_next;
    int cf_loop_stack[64];
    int cf_loop_sp;
    int cf_if_stack[64];
    int cf_if_sp;
    // Phase 17 (17.A): the float-constant pool (f64 bit patterns), threaded in so the hosted
    // native + WASM emitters resolve OP_FCONST's 4-byte index to the actual double. NULL/0 when
    // unset (non-float programs). wasm_emit_float gates the WASM `(local $f0/$f1/$fvN f64)`
    // declarations — off keeps non-float modules byte-identical. Unused by the freestanding emitters.
    const double* float_pool;
    int float_count;
    int wasm_emit_float;
    // Phase 17.F.3: the decimal-constant pool (256-byte teko_decimal blobs, decimal_count of them),
    // threaded in so the hosted native + WASM emitters resolve OP_DCONST's 4-byte index to the
    // 256-byte value. NULL/0 when unset (non-decimal programs). wasm_emit_decimal gates the WASM
    // decimal linear-memory region + reactor imports (off keeps non-decimal modules byte-identical);
    // the native emitter uses it to size the decimal frame region. Unused by the freestanding emitters.
    const unsigned char* decimal_pool;
    int decimal_count;
    int wasm_emit_decimal;
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
void teko_metal_set_emit_spawn(MetalContext* ctx, int enabled);
void teko_metal_set_emit_duplex(MetalContext* ctx, int enabled);
void teko_metal_set_emit_delayed(MetalContext* ctx, int enabled);
void teko_metal_set_emit_bcast(MetalContext* ctx, int enabled);
void teko_metal_set_emit_shared(MetalContext* ctx, int enabled);
void teko_metal_set_emit_wait(MetalContext* ctx, int enabled);
void teko_metal_set_emit_await(MetalContext* ctx, int enabled);
void teko_metal_set_emit_retry(MetalContext* ctx, int enabled);
void teko_metal_set_emit_object(MetalContext* ctx, int enabled);
void teko_metal_set_emit_vtable(MetalContext* ctx, int enabled);
void teko_metal_set_emit_array(MetalContext* ctx, int enabled);
void teko_metal_set_emit_iarray(MetalContext* ctx, int enabled);
void teko_metal_set_emit_simd(MetalContext* ctx, int enabled);

// Phase 17 (17.A): hand the backend the float-constant pool (OP_FCONST's index space). `floats`
// must outlive teko_metal_emit_program. teko_metal_set_emit_float gates the WASM float locals.
void teko_metal_set_floats(MetalContext* ctx, const double* floats, int count);
void teko_metal_set_emit_float(MetalContext* ctx, int enabled);

// Phase 17.F.3: hand the backend the decimal-constant pool (OP_DCONST's index space; 256-byte
// blobs). `decimals` (decimal_count*256 bytes) must outlive teko_metal_emit_program.
// teko_metal_set_emit_decimal gates the WASM decimal linear-memory region + reactor imports.
void teko_metal_set_decimals(MetalContext* ctx, const unsigned char* decimals, int count);
void teko_metal_set_emit_decimal(MetalContext* ctx, int enabled);

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
