#include "../codegen_metal.h"
#include <stdio.h>
#include <string.h>

// ===========================================================================
// WASM (WAT) emitter — Phase 10.3: cooperative concurrency with mid-function
// suspension. Green threads are compiled to state machines that can suspend in
// the *middle* of their body (at a blocking channel receive), return control to
// the scheduler so other tasks run, and later resume exactly where they left off
// — WebAssembly has no stack-switching, so this is done with an explicit state
// machine + a per-task spill frame in linear memory, not a native stack switch.
//
// Register model (every function declares the same three i32 locals so the linear
// IL is stack-neutral and each function ends with exactly one value):
//   $w0  accumulator / result      $w1  scratch / running value      $cp  channel ptr
//
// Green-thread ABI:  (func $routine_N (param $arg i32) (param $state i32)
//                                     (param $frame i32) (result i32))
//   $arg    the channel base handed to the thread at spawn
//   $state  resume point (0 = start); the body br_tables on it
//   $frame  per-task spill area: [0]=$w0 [4]=$w1, preserved across suspensions
//   result  the next state: 0 = completed, k>0 = suspended at yield point k
//
// Scheduler:  $teko_sched_run drains a run queue (16-byte slots at offset 64:
//   {fn@0, arg@4, state@8, frame@12}); a task that returns state>0 is re-enqueued
//   to resume later. $main is the root (not a task) and blocks re-entrantly.
//
// Memory map (1 page = 64 KiB): [64..) run queue · [1024] .data · [2048..) arena
// (channels + per-task frames bump-allocated here).
// ===========================================================================

#define TEKO_WASM_FRAME_BYTES 64

static void emit_wasm_scheduler_runtime(FILE* f) {
    fprintf(f, "  (global $rq_head (mut i32) (i32.const 0))\n");
    fprintf(f, "  (global $rq_tail (mut i32) (i32.const 0))\n");
    fprintf(f, "  (type $task (func (param i32 i32 i32) (result i32)))\n");

    // enqueue {fn, arg, state, frame} into run-queue slot rq_tail (16-byte slots).
    fprintf(f, "  (func $teko_enqueue (param $fn i32) (param $arg i32) (param $st i32) (param $fr i32) (local $b i32)\n");
    fprintf(f, "    (local.set $b (i32.add (i32.const 64) (i32.mul (global.get $rq_tail) (i32.const 16))))\n");
    fprintf(f, "    (i32.store offset=0  (local.get $b) (local.get $fn))\n");
    fprintf(f, "    (i32.store offset=4  (local.get $b) (local.get $arg))\n");
    fprintf(f, "    (i32.store offset=8  (local.get $b) (local.get $st))\n");
    fprintf(f, "    (i32.store offset=12 (local.get $b) (local.get $fr))\n");
    fprintf(f, "    (global.set $rq_tail (i32.add (global.get $rq_tail) (i32.const 1))))\n");

    // Cooperative scheduler: dispatch each ready task via call_indirect; if it
    // returns a suspended state (>0) re-enqueue it (with the same arg+frame) so
    // it resumes once a producer has made progress. rq_head advances *before* the
    // call so a re-entrant scheduler call never re-runs the in-flight task.
    fprintf(f, "  (func $teko_sched_run (local $b i32) (local $f i32) (local $a i32) (local $st i32) (local $fr i32) (local $ret i32)\n");
    fprintf(f, "    (block $done (loop $L\n");
    fprintf(f, "      (br_if $done (i32.ge_u (global.get $rq_head) (global.get $rq_tail)))\n");
    fprintf(f, "      (local.set $b (i32.add (i32.const 64) (i32.mul (global.get $rq_head) (i32.const 16))))\n");
    fprintf(f, "      (local.set $f  (i32.load offset=0  (local.get $b)))\n");
    fprintf(f, "      (local.set $a  (i32.load offset=4  (local.get $b)))\n");
    fprintf(f, "      (local.set $st (i32.load offset=8  (local.get $b)))\n");
    fprintf(f, "      (local.set $fr (i32.load offset=12 (local.get $b)))\n");
    fprintf(f, "      (global.set $rq_head (i32.add (global.get $rq_head) (i32.const 1)))\n");
    fprintf(f, "      (local.set $ret (call_indirect (type $task) (local.get $a) (local.get $st) (local.get $fr) (local.get $f)))\n");
    fprintf(f, "      (if (i32.gt_u (local.get $ret) (i32.const 0))\n");
    fprintf(f, "        (then (call $teko_enqueue (local.get $f) (local.get $a) (local.get $ret) (local.get $fr))))\n");
    fprintf(f, "      (br $L))))\n");
}

// The non-suspending half of a channel receive: read buf[head] -> $w0, advance
// head = (head+1) % cap. Channel base is in $cp.
static void emit_wasm_chan_read(FILE* f) {
    fprintf(f, "    local.get $cp\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 4\n    i32.mul\n    i32.add\n"); // cp + head*4
    fprintf(f, "    i32.load offset=12\n    local.set $w0\n");                                            // $w0 = mem[cp+head*4+12]
    fprintf(f, "    local.get $cp\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 1\n    i32.add\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
    fprintf(f, "    i32.store offset=0\n");                                                               // head = (head+1) % cap
}

// ===========================================================================
// Layer B — `--target=...-wasm-threads`: real multicore. The module imports a
// SHARED memory and channels use the atomics proposal (memory.atomic.wait32 /
// .notify + i32.atomic.*) instead of the cooperative scheduler. SPAWN delegates
// to a host `teko_rt.spawn(fn_index, arg)` that starts a Web Worker /
// worker_threads thread; that thread re-instantiates the same module against the
// shared memory and calls the exported `teko_invoke` dispatcher. Green threads
// are 1:1 OS threads here (opt-in parallelism on top of Layer A, not a
// replacement). Channel cell layout: [0]=ready flag, [4]=value.
// ===========================================================================
static void emit_wasm_threads(MetalContext* ctx, OpCode op, int32_t arg) {
    FILE* f = ctx->file;
    switch (op) {
        case OP_PROLOG:
            fprintf(f, "(module\n");
            fprintf(f, "  ;; --- Target: WebAssembly Text Format (wasm-threads / Layer B, Phase 10.4) ---\n");
            fprintf(f, "  (import \"env\" \"memory\" (memory 1 1 shared))\n");
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            fprintf(f, "  (import \"teko_rt\" \"spawn\" (func $teko_spawn (param i32 i32)))\n");
            fprintf(f, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            fprintf(f, "  (type $task (func (param i32)))\n");
            fprintf(f, "  (func $main (result i32)\n");
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32) (local $spins i32)\n");
            ctx->wasm_open = 1;
            break;

        case OP_HALT:
            fprintf(f, "    ;; [WASM Halt]: result is $w0\n");
            break;
        case OP_ICONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg);
            break;
        case OP_SCONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg * 32);
            break;
        case OP_STORE:
            fprintf(f, "    local.get $w0\n    local.set $w1\n");
            break;
        case OP_LOAD:
            fprintf(f, "    local.get $w1\n    local.set $w0\n");
            break;
        case OP_ADD:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.add\n    local.set $w0\n");
            break;
        case OP_SUB:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.sub\n    local.set $w0\n");
            break;
        case OP_MUL:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.mul\n    local.set $w0\n");
            break;
        case OP_DIV:
            fprintf(f, "    local.get $w1\n    i32.eqz\n    if (result i32)\n      i32.const -1\n    else\n");
            fprintf(f, "      local.get $w0\n      local.get $w1\n      i32.div_s\n    end\n    local.set $w0\n");
            break;
        case OP_ARENA_PUSH:
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.add\n    global.set $arena_sp\n");
            break;
        case OP_ARENA_POP:
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.sub\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_INIT:
            // Channel cell {flag@0, value@4} in shared memory; flag starts empty.
            fprintf(f, "    ;; [WASM-threads Channel Init]: atomic cell in shared memory\n");
            fprintf(f, "    global.get $arena_sp\n    local.set $cp\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.atomic.store offset=0\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 8\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_PUT:
            // Atomically publish the value, then wake one waiter.
            fprintf(f, "    ;; [WASM-threads Channel Put]: atomic store + notify\n");
            fprintf(f, "    local.get $cp\n    local.get $w0\n    i32.atomic.store offset=4\n");
            fprintf(f, "    local.get $cp\n    i32.const 1\n    i32.atomic.store offset=0\n");
            fprintf(f, "    local.get $cp\n    i32.const 1\n    memory.atomic.notify offset=0\n    drop\n");
            break;

        case OP_CHAN_GET:
            // Busy-poll the channel flag with an ATOMIC load until the producer
            // sets it, then read the value. This deliberately does NOT use
            // memory.atomic.wait32/notify: a cross-instance notify was observed to
            // never reach the waiter on some runtimes (the wait blocks forever).
            // Cross-thread visibility of the shared memory is guaranteed by the
            // memory model, so the atomic load is certain to observe the store —
            // no notify required. (A production scheduler would back off; this is
            // the correctness-first form for the cooperative-over-threads MVP.)
            // Bounded: spin at most ~2e9 iterations (~1-2s of CPU) so a producer
            // that never publishes traps with `unreachable` instead of spinning
            // forever / pegging a core indefinitely. The harness watchdog and the
            // job timeout-minutes are outer backstops; this makes the module itself
            // terminate with a clear hard error.
            fprintf(f, "    ;; [WASM-threads Channel Get]: notify-free atomic busy-poll (bounded) on the flag\n");
            fprintf(f, "    i32.const 0\n    local.set $spins\n");
            fprintf(f, "    (block $ready\n      (loop $spin\n");
            fprintf(f, "        (br_if $ready (i32.eq (i32.atomic.load offset=0 (local.get $cp)) (i32.const 1)))\n");
            fprintf(f, "        (local.set $spins (i32.add (local.get $spins) (i32.const 1)))\n");
            fprintf(f, "        (if (i32.gt_u (local.get $spins) (i32.const 2000000000)) (then unreachable))\n");
            fprintf(f, "        (br $spin)))\n");
            fprintf(f, "    local.get $cp\n    i32.atomic.load offset=4\n    local.set $w0\n");
            break;

        case OP_SPAWN_ASYNC:
            // Hand off to the host: start a real Worker for routine $w0 with arg $cp.
            fprintf(f, "    ;; [WASM-threads Spawn]: host starts a Worker (fn=$w0, arg=$cp)\n");
            fprintf(f, "    local.get $w0\n    local.get $cp\n    call $teko_spawn\n");
            break;

        case OP_AWAIT_INTENT:
            fprintf(f, "    ;; [WASM-threads Await]: parallelism is real; no cooperative yield\n");
            break;

        case OP_FUNC_BEGIN:
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
            }
            fprintf(f, "  (func $routine_%d (param $arg i32)\n", arg);
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32) (local $spins i32)\n");
            fprintf(f, "    local.get $arg\n    local.set $cp\n");
            ctx->wasm_open = 2;
            if (ctx->wasm_routine_count < 64) ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            ctx->wasm_routine_count++;
            break;

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
                ctx->wasm_open = 0;
            }
            break;

        case OP_RETURN:
        case OP_EPILOG: {
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
            }
            ctx->wasm_open = 0;
            int n = ctx->wasm_routine_count;
            fprintf(f, "  (table %d funcref)\n", n > 0 ? n : 1);
            if (n > 0) {
                fprintf(f, "  (elem (i32.const 0)");
                for (int k = 0; k < n && k < 64; k++) fprintf(f, " $routine_%d", ctx->wasm_routine_ids[k]);
                fprintf(f, ")\n");
            }
            // Exported dispatcher so a host Worker can run a routine by table index.
            fprintf(f, "  (func $teko_invoke (export \"teko_invoke\") (param $fn i32) (param $arg i32)\n");
            fprintf(f, "    (call_indirect (type $task) (local.get $arg) (local.get $fn)))\n");
            fprintf(f, "  (export \"main\" (func $main))\n");
            fprintf(f, ")\n");
            break;
        }

        default:
            break;
    }
}

void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;
    // Layer B: opt-in real multicore via `--target=...-wasm-threads`.
    if (ctx->target.target_string[0] && strstr(ctx->target.target_string, "threads")) {
        emit_wasm_threads(ctx, op, arg);
        return;
    }
    FILE* f = ctx->file;

    switch (op) {
        // ====================================================================
        // 1. MODULE INIT + SCHEDULER RUNTIME + $main OPEN
        // ====================================================================
        case OP_PROLOG:
            fprintf(f, "(module\n");
            fprintf(f, "  ;; --- Target: WebAssembly Text Format (cooperative concurrency, Phase 10.3) ---\n");
            fprintf(f, "  (memory 1)\n");
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            fprintf(f, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            emit_wasm_scheduler_runtime(f);
            fprintf(f, "  (func $main (result i32)\n");
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            ctx->wasm_open = 1;
            break;

        // ====================================================================
        // 2. LITERALS / MEMORY (accumulator model: results land in $w0)
        // ====================================================================
        case OP_HALT:
            fprintf(f, "    ;; [WASM Halt]: result is $w0, returned at function close\n");
            break;

        case OP_ICONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg);
            break;

        case OP_SCONST:
            fprintf(f, "    i32.const %d ;; Offset of Constant Pool in Linear Memory\n    local.set $w0\n", arg * 32);
            break;

        case OP_STORE:
            fprintf(f, "    local.get $w0\n    local.set $w1\n");
            break;

        case OP_LOAD:
            fprintf(f, "    local.get $w1\n    local.set $w0\n");
            break;

        // ====================================================================
        // 3. ARITHMETIC ($w0 = $w0 <op> $w1)
        // ====================================================================
        case OP_ADD:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.add\n    local.set $w0\n");
            break;

        case OP_SUB:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.sub\n    local.set $w0\n");
            break;

        case OP_MUL:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.mul\n    local.set $w0\n");
            break;

        case OP_DIV:
            fprintf(f, "    local.get $w1\n    i32.eqz\n    if (result i32)\n");
            fprintf(f, "      i32.const -1\n    else\n");
            fprintf(f, "      local.get $w0\n      local.get $w1\n      i32.div_s\n    end\n");
            fprintf(f, "    local.set $w0\n");
            break;

        // ====================================================================
        // 4. NATIVE ARENA ALLOCATOR (O(1) bump)
        // ====================================================================
        case OP_ARENA_PUSH:
            fprintf(f, "    ;; --- [WASM Arena Push]: O(1) bump of a 1024-byte frame ---\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(f, "    ;; --- [WASM Arena Pop]: O(1) reclaim of the 1024-byte frame ---\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.sub\n    global.set $arena_sp\n");
            break;

        // ====================================================================
        // 5. CHANNELS (in-module linear-memory ring buffers)
        //    Header (i32 cells): [0]=head [4]=tail [8]=cap, data slots at +12.
        //    Channel base lives in $cp.
        // ====================================================================
        case OP_CHAN_INIT:
            fprintf(f, "    ;; [WASM Channel Init]: ring buffer in linear memory (cap 8)\n");
            fprintf(f, "    global.get $arena_sp\n    local.set $cp\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=0\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=4\n");
            fprintf(f, "    local.get $cp\n    i32.const 8\n    i32.store offset=8\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 44\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_PUT:
            fprintf(f, "    ;; [WASM Channel Put]: non-blocking ring-buffer store at tail\n");
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 4\n    i32.mul\n    i32.add\n");
            fprintf(f, "    local.get $w0\n    i32.store offset=12\n");
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 1\n    i32.add\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
            fprintf(f, "    i32.store offset=4\n");
            break;

        case OP_CHAN_GET:
            if (ctx->wasm_open == 2) {
                // Inside a green thread: a true suspension point. Close the state
                // block for this segment; if the channel is empty, spill the live
                // registers and RETURN the resume state to the scheduler.
                int k = ++ctx->wasm_yield_idx;
                fprintf(f, "    )\n");                                          // close (block $s<k>)
                fprintf(f, "    ;; [WASM Channel Get @ yield %d]: suspend to scheduler if empty\n", k);
                fprintf(f, "    local.get $cp\n    i32.load offset=0\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=4\n");
                fprintf(f, "    i32.eq\n    if\n");
                fprintf(f, "      local.get $frame\n      local.get $w0\n      i32.store offset=0\n"); // spill $w0
                fprintf(f, "      local.get $frame\n      local.get $w1\n      i32.store offset=4\n"); // spill $w1
                fprintf(f, "      i32.const %d\n      return\n    end\n", k);
                emit_wasm_chan_read(f);
            } else {
                // In $main (the root): block re-entrantly — drain the scheduler
                // once, then read. $main cannot suspend (nothing to return to).
                fprintf(f, "    ;; [WASM Channel Get]: root blocking receive (drains the scheduler)\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=0\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=4\n");
                fprintf(f, "    i32.eq\n    if\n      call $teko_sched_run\n    end\n");
                emit_wasm_chan_read(f);
            }
            break;

        // ====================================================================
        // 6. CONCURRENCY: real cooperative spawn + yield (no host runtime)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            // Allocate a per-task spill frame and enqueue the green thread:
            // {fn_index=$w0, arg=$cp (channel base), state=0, frame}. The
            // scheduler dispatches it via call_indirect.
            fprintf(f, "    ;; [WASM Spawn]: allocate frame + enqueue {fn=$w0, arg=$cp, state=0}\n");
            fprintf(f, "    local.get $w0\n    local.get $cp\n    i32.const 0\n    global.get $arena_sp\n    call $teko_enqueue\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const %d\n    i32.add\n    global.set $arena_sp\n", TEKO_WASM_FRAME_BYTES);
            break;

        case OP_AWAIT_INTENT:
            fprintf(f, "    ;; [WASM Await]: cooperative yield to the scheduler\n");
            fprintf(f, "    call $teko_sched_run\n");
            break;

        // ====================================================================
        // 7. FUNCTION BOUNDARIES (green-thread bodies as state machines)
        // ====================================================================
        case OP_FUNC_BEGIN: {
            // Close whatever function is currently open, then open $routine_<id>.
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");          // close $main (result i32)
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");            // close previous routine (state 0)
            }
            int n = ctx->wasm_routine_yields;                    // yield points in this routine
            fprintf(f, "  (func $routine_%d (param $arg i32) (param $state i32) (param $frame i32) (result i32)\n", arg);
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            fprintf(f, "    local.get $arg\n    local.set $cp\n");                       // channel base
            fprintf(f, "    local.get $frame\n    i32.load offset=0\n    local.set $w0\n"); // reload spilled $w0
            fprintf(f, "    local.get $frame\n    i32.load offset=4\n    local.set $w1\n"); // reload spilled $w1
            // State dispatch: open (n+1) nested blocks $s_n .. $s0 and br_table on
            // $state. Each yield point (CHAN_GET) closes one block; resuming at
            // state k lands right at yield point k.
            for (int k = n; k >= 0; k--) fprintf(f, "    (block $s%d\n", k);
            fprintf(f, "    local.get $state\n    br_table");
            for (int k = 0; k <= n; k++) fprintf(f, " $s%d", k); // plain (stack-form) instruction, no parens
            fprintf(f, "\n");
            fprintf(f, "    )\n");                                // close (block $s0): start segment follows
            ctx->wasm_open = 2;
            if (ctx->wasm_routine_count < 64) {
                ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            }
            ctx->wasm_routine_count++;
            break;
        }

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");            // completed: return state 0, close routine
                ctx->wasm_open = 0;
            }
            break;

        // ====================================================================
        // 8. CONTROL FLOW + MODULE CLOSE
        // ====================================================================
        case OP_JMP:
            fprintf(f, "    br $label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(f, "    br_if $label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG: {
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");
            }
            ctx->wasm_open = 0;
            int n = ctx->wasm_routine_count;
            fprintf(f, "  (table %d funcref)\n", n > 0 ? n : 1);
            if (n > 0) {
                fprintf(f, "  (elem (i32.const 0)");
                for (int k = 0; k < n && k < 64; k++) {
                    fprintf(f, " $routine_%d", ctx->wasm_routine_ids[k]);
                }
                fprintf(f, ")\n");
            }
            fprintf(f, "  (export \"main\" (func $main))\n");
            fprintf(f, "  (data (i32.const 1024) \"Hello Teko\\00\")\n");
            fprintf(f, ")\n");
            break;
        }

        default:
            if ((int)op >= 100) {
                fprintf(f, "    ;; Label marker: $label_%d\n", (int)op);
            }
            break;
    }
}
