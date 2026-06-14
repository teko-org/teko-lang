#include "unity.h"
#include "codegen/codegen_metal.h"
#include "teko_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ====================================================================
// 1. NATIVE WEBASSEMBLY ENGINE TEST (WASM / WAT BARE-METAL)
// ====================================================================
void test_teko_aot_wasm_pure_emission_integrity(void) {
    const char* asm_path = "output_wasm_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32; // Configures the WASM virtual processor
    target.os = OS_WASI;      // Abstract OS for Web/Servers
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // Injects the contiguous sequence of our ISA: Constant Load -> Channel -> Halt
    unsigned char mock_wasm_bytes[] = {
        0x01, 0x2A, 0x00, 0x00, 0x00, // OP_ICONST 42 (5 bytes)
        0x12                          // OP_CHAN_INIT (1 byte)
    };

    teko_metal_emit_program(ctx, mock_wasm_bytes, sizeof(mock_wasm_bytes));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);

    char* buffer = (char*)malloc(4096);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 4096);

    size_t bytes = fread(buffer, 1, 4095, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Structured assertions validating the fidelity of the WebAssembly S-Expression grammar
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(module"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 42"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(memory 1)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(export \"main\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "Hello Teko"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 2. WASM ARENA ALLOCATOR + IN-MODULE CHANNELS + COOPERATIVE CONCURRENCY
// ====================================================================
// The O(1) arena is real linear-memory bump code. Phase 10.1: channels are real
// in-module ring buffers. Phase 10.2b: spawn/await are now real cooperative
// primitives compiled into the module (run queue + scheduler), with no host
// runtime imports at all — so the module instantiates standalone (verified
// executably by the wasm-emit CI job).
void test_teko_aot_wasm_arena_and_concurrency_hooks(void) {
    const char* asm_path = "output_wasm_arena_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // ARENA_PUSH, ARENA_POP, SPAWN_ASYNC, CHAN_INIT, CHAN_PUT, AWAIT_INTENT, HALT
    unsigned char mock[] = { 0x30, 0x31, 0x10, 0x12, 0x13, 0x11, 0x00 };
    teko_metal_emit_program(ctx, mock, sizeof(mock));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(8192);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 8192);
    size_t bytes = fread(buffer, 1, 8191, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Real arena allocator: a mutable global bumped by 1024-byte frames.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(global $arena_sp (mut i32)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "global.set $arena_sp"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 1024"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.add"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.sub"));

    // Phase 10.1: channels are real in-module ring buffers (no host import).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=8"));   // CHAN_INIT writes cap
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=12"));  // CHAN_PUT writes buf[tail]
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.rem_u"));            // CHAN_PUT tail wrap (mod cap)
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=4"));   // CHAN_PUT advances tail
    // The channel host imports are gone (channels are in-module now).
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_chan_init"));
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_chan_put"));
    TEST_ASSERT_NULL(strstr(buffer, "\"chan_init\""));

    // Phase 10.2b: spawn/await are real cooperative primitives — SPAWN enqueues
    // into the in-module run queue, AWAIT yields to the in-module scheduler.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(type $task (func (param i32 i32 i32) (result i32)))")); // (arg,state,frame)->next state
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $teko_enqueue"));             // run-queue append
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $teko_sched_run"));           // cooperative scheduler
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_enqueue"));              // SPAWN_ASYNC -> enqueue
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_sched_run"));            // AWAIT_INTENT -> yield
    // No host runtime: the legacy spawn/await imports are gone, so the module
    // instantiates with no imports.
    TEST_ASSERT_NULL(strstr(buffer, "(import \"teko_rt\" \"spawn\""));
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_spawn"));
    TEST_ASSERT_NULL(strstr(buffer, "call $teko_await"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 3. WASM MULTI-FUNCTION GREEN-THREAD LOWERING (Phase 10.2b)
// ====================================================================
// Each routine body is emitted as a SEPARATE WASM function indexed in a table,
// and SPAWN dispatches it via call_indirect. This pins the structural shape of
// the lowering; the wasm-emit CI job verifies it actually runs (channel value
// round-trips through a spawned green thread -> main() == 7).
void test_teko_aot_wasm_multifunction_spawn_lowering(void) {
    const char* asm_path = "output_wasm_multifn_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // main: CHAN_INIT, ICONST 0, SPAWN_ASYNC, CHAN_GET, HALT
    // routine 0: FUNC_BEGIN(0), ICONST 7, CHAN_PUT, FUNC_END
    unsigned char prog[] = {
        0x12,                         // OP_CHAN_INIT
        0x01, 0x00, 0x00, 0x00, 0x00, // OP_ICONST 0 (routine index)
        0x10,                         // OP_SPAWN_ASYNC
        0x14,                         // OP_CHAN_GET (blocking receive)
        0x00,                         // OP_HALT
        0x40, 0x00, 0x00, 0x00, 0x00, // OP_FUNC_BEGIN id=0
        0x01, 0x07, 0x00, 0x00, 0x00, // OP_ICONST 7
        0x13,                         // OP_CHAN_PUT
        0x41                          // OP_FUNC_END
    };
    teko_metal_emit_program(ctx, prog, sizeof(prog));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(8192);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 8192);
    size_t bytes = fread(buffer, 1, 8191, file);
    buffer[bytes] = '\0';
    fclose(file);

    // The green-thread body is its own function with a param (the channel base).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $routine_0 (param $arg i32)"));
    // SPAWN is dispatched indirectly through the function table.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call_indirect (type $task)"));
    // The table + its initialiser list the routine as table slot 0.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(table 1 funcref)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(elem (i32.const 0) $routine_0)"));
    // The blocking receive yields to the scheduler when the channel is empty.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_sched_run"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 4. WASM MID-FUNCTION SUSPENSION — STATE MACHINE (Phase 10.3)
// ====================================================================
// A green thread that blocks mid-body is lowered to a state machine: it can
// SUSPEND at a channel receive (spill its live registers to a per-task frame and
// return the resume state to the scheduler) and later RESUME at the same point.
// This pins the structural shape; the wasm-emit CI job verifies it actually runs
// (consumer suspended, producer ran in between, consumer resumed -> main() == 30).
void test_teko_aot_wasm_midfunction_suspension(void) {
    const char* asm_path = "output_wasm_suspend_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // main: CHAN_INIT, ICONST 0, SPAWN_ASYNC, CHAN_GET, HALT
    // routine 0: FUNC_BEGIN(0), CHAN_GET (yield 1), CHAN_PUT, FUNC_END
    unsigned char prog[] = {
        0x12,                         // OP_CHAN_INIT
        0x01, 0x00, 0x00, 0x00, 0x00, // OP_ICONST 0
        0x10,                         // OP_SPAWN_ASYNC
        0x14,                         // OP_CHAN_GET
        0x00,                         // OP_HALT
        0x40, 0x00, 0x00, 0x00, 0x00, // OP_FUNC_BEGIN id=0
        0x14,                         // OP_CHAN_GET (the suspension point)
        0x13,                         // OP_CHAN_PUT
        0x41                          // OP_FUNC_END
    };
    teko_metal_emit_program(ctx, prog, sizeof(prog));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(8192);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 8192);
    size_t bytes = fread(buffer, 1, 8191, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Green thread is a resumable state machine: (arg, state, frame) -> next state.
    TEST_ASSERT_NOT_NULL(strstr(buffer,
        "(func $routine_0 (param $arg i32) (param $state i32) (param $frame i32) (result i32)"));
    // Entry reloads the spilled registers and dispatches on the resume state.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "br_table $s0 $s1"));     // 1 yield -> s0 (start) + s1 (resume)
    TEST_ASSERT_NOT_NULL(strstr(buffer, "local.get $state"));
    // The suspension point spills $w0/$w1 to the frame and returns the state.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.store offset=0"));   // spill $w0 -> frame[0]
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.const 1\n      return")); // suspend: return resume state 1
    // The scheduler re-enqueues a suspended task so it resumes later.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(then (call $teko_enqueue"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 6. WASM MULTI-SPAWN CONTENTION — N ROUTINES, ONE CHANNEL (Phase 10 stability)
// ====================================================================
// Five green-thread producers compete on one channel; the consumer drains and
// sums them. Pins the multi-routine lowering (a 5-entry function table). The
// wasm-exec CI jobs verify it runs deterministically (main() == 15).
void test_teko_aot_wasm_multispawn_contention(void) {
    const char* asm_path = "output_wasm_multispawn_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi", sizeof(target.target_string) - 1);

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // main: CHAN_INIT, (ICONST k, SPAWN)x5, CHAN_GET STORE, (CHAN_GET ADD STORE)x3,
    //       CHAN_GET ADD, HALT ; then 5 producer routines putting 1..5.
    unsigned char prog[256];
    int n = 0;
    prog[n++] = 0x12; // CHAN_INIT
    for (int k = 0; k < 5; k++) {
        prog[n++] = 0x01; prog[n++] = (unsigned char)k; prog[n++] = 0; prog[n++] = 0; prog[n++] = 0; // ICONST k
        prog[n++] = 0x10; // SPAWN_ASYNC
    }
    prog[n++] = 0x14; prog[n++] = 0x04;             // CHAN_GET, STORE
    for (int k = 1; k < 4; k++) { prog[n++] = 0x14; prog[n++] = 0x05; prog[n++] = 0x04; } // GET ADD STORE
    prog[n++] = 0x14; prog[n++] = 0x05;             // CHAN_GET, ADD
    prog[n++] = 0x00;                               // HALT
    for (int k = 0; k < 5; k++) {
        prog[n++] = 0x40; prog[n++] = (unsigned char)k; prog[n++] = 0; prog[n++] = 0; prog[n++] = 0; // FUNC_BEGIN k
        prog[n++] = 0x01; prog[n++] = (unsigned char)(k + 1); prog[n++] = 0; prog[n++] = 0; prog[n++] = 0; // ICONST k+1
        prog[n++] = 0x13;                           // CHAN_PUT
        prog[n++] = 0x41;                           // FUNC_END
    }
    teko_metal_emit_program(ctx, prog, (uint32_t)n);
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(16384);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 16384);
    size_t bytes = fread(buffer, 1, 16383, file);
    buffer[bytes] = '\0';
    fclose(file);

    // All five green threads are emitted and indexed in a 5-entry table.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $routine_0 (param $arg i32)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $routine_4 (param $arg i32)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(table 5 funcref)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(elem (i32.const 0) $routine_0 $routine_1 $routine_2 $routine_3 $routine_4)"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call_indirect (type $task)"));

    free(buffer);
    remove(asm_path);
}

// ====================================================================
// 5. WASM-THREADS (LAYER B): SHARED MEMORY + ATOMICS + HOST SPAWN (Phase 10.4)
// ====================================================================
// With a `...-wasm-threads` target the backend emits the opt-in real-multicore
// lowering: a shared memory import, atomic channel ops, and SPAWN delegated to a
// host Worker. The wasm-threads CI jobs verify it runs on a real OS thread
// (worker_threads / Web Worker), main() == 99.
void test_teko_aot_wasm_threads_layer_b_emission(void) {
    const char* asm_path = "output_wasm_threads_test.wat";

    TekoTarget target;
    target.arch = ARCH_WASM32;
    target.os = OS_WASI;
    strncpy(target.target_string, "wasm32-wasi-threads", sizeof(target.target_string) - 1); // Layer B flag

    MetalContext* ctx = teko_metal_create(asm_path, target);
    TEST_ASSERT_NOT_NULL(ctx);

    // main: CHAN_INIT, ICONST 0, SPAWN_ASYNC, CHAN_GET, HALT
    // routine 0: FUNC_BEGIN(0), ICONST 99, CHAN_PUT, FUNC_END
    unsigned char prog[] = {
        0x12, 0x01, 0x00, 0x00, 0x00, 0x00, 0x10, 0x14, 0x00,
        0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x63, 0x00, 0x00, 0x00, 0x13, 0x41
    };
    teko_metal_emit_program(ctx, prog, sizeof(prog));
    teko_metal_close(ctx);

    FILE* file = fopen(asm_path, "r");
    TEST_ASSERT_NOT_NULL(file);
    char* buffer = (char*)malloc(8192);
    TEST_ASSERT_NOT_NULL(buffer);
    memset(buffer, 0, 8192);
    size_t bytes = fread(buffer, 1, 8191, file);
    buffer[bytes] = '\0';
    fclose(file);

    // Shared memory import (the prerequisite for the atomics proposal).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(import \"env\" \"memory\" (memory 1 1 shared))"));
    // Atomic channel ops: the producer publishes (store) + notifies; the consumer
    // busy-polls the flag with an atomic load (notify-free — see emit_wasm.c).
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.atomic.store"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "memory.atomic.notify"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "i32.atomic.load"));
    // SPAWN delegates to the host Worker; a dispatcher is exported for it to call.
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(import \"teko_rt\" \"spawn\""));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "call $teko_spawn"));
    TEST_ASSERT_NOT_NULL(strstr(buffer, "(func $teko_invoke (export \"teko_invoke\")"));
    // Layer B has no in-module cooperative scheduler (parallelism is real).
    TEST_ASSERT_NULL(strstr(buffer, "$teko_sched_run"));

    free(buffer);
    remove(asm_path);
}
