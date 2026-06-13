#include "../codegen_metal.h"
#include <stdio.h>

// ===========================================================================
// WASM (WAT) emitter — Phase 10.2b: cooperative concurrency backend.
//
// Register model (every function declares the same three i32 locals so the
// linear IL is stack-neutral and each function ends with exactly one value on
// the operand stack — a hard requirement for a valid WASM module):
//   $w0  accumulator / result
//   $w1  scratch (second arithmetic operand, channel-receive temp)
//   $cp  channel pointer (current channel base; also the spawn argument)
//
// Module shape:
//   (module
//     (memory 1) (export "memory")
//     (global $arena_sp ...) (global $rq_head ...) (global $rq_tail ...)
//     (type $task (func (param i32)))
//     (func $teko_enqueue ...)        ;; append {fn_index, arg} to the run queue
//     (func $teko_sched_run ...)      ;; drain the run queue via call_indirect
//     (func $main (result i32) ...)   ;; entry; SPAWN enqueues, AWAIT/GET yield
//     (func $routine_0 (param i32) ...) ...   ;; green-thread bodies
//     (table N funcref) (elem (i32.const 0) $routine_0 ...)
//     (export "main") (data ...))
//
// Memory map (linear memory, 1 page = 64 KiB):
//   [64 .. )   run queue: slot i at 64 + i*8 -> { fn@+0, arg@+4 }
//   [1024]     .data ("Hello Teko")
//   [2048 .. ) arena: channels bump-allocated here (CHAN_INIT)
// ===========================================================================

static void emit_wasm_scheduler_runtime(FILE* f) {
    // Run-queue cursors and the indirect-call task signature.
    fprintf(f, "  (global $rq_head (mut i32) (i32.const 0))\n");
    fprintf(f, "  (global $rq_tail (mut i32) (i32.const 0))\n");
    fprintf(f, "  (type $task (func (param i32)))\n");

    // enqueue {fn_index, arg} at run-queue slot rq_tail (8-byte slots from 64).
    fprintf(f, "  (func $teko_enqueue (param $fn i32) (param $arg i32)\n");
    fprintf(f, "    (i32.store offset=64 (i32.mul (global.get $rq_tail) (i32.const 8)) (local.get $fn))\n");
    fprintf(f, "    (i32.store offset=68 (i32.mul (global.get $rq_tail) (i32.const 8)) (local.get $arg))\n");
    fprintf(f, "    (global.set $rq_tail (i32.add (global.get $rq_tail) (i32.const 1))))\n");

    // Cooperative scheduler: drain ready tasks run-to-completion. rq_head is
    // advanced *before* the call so a task that re-enters $teko_sched_run (a
    // blocking channel receive that yields) does not re-run itself.
    fprintf(f, "  (func $teko_sched_run (local $f i32) (local $a i32)\n");
    fprintf(f, "    (block $done\n");
    fprintf(f, "      (loop $L\n");
    fprintf(f, "        (br_if $done (i32.ge_u (global.get $rq_head) (global.get $rq_tail)))\n");
    fprintf(f, "        (local.set $a (i32.load offset=68 (i32.mul (global.get $rq_head) (i32.const 8))))\n");
    fprintf(f, "        (local.set $f (i32.load offset=64 (i32.mul (global.get $rq_head) (i32.const 8))))\n");
    fprintf(f, "        (global.set $rq_head (i32.add (global.get $rq_head) (i32.const 1)))\n");
    fprintf(f, "        (call_indirect (type $task) (local.get $a) (local.get $f))\n");
    fprintf(f, "        (br $L))))\n");
}

void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;
    FILE* f = ctx->file;

    switch (op) {
        // ====================================================================
        // 1. MODULE INITIALIZATION + SCHEDULER RUNTIME + $main OPEN
        // ====================================================================
        case OP_PROLOG:
            fprintf(f, "(module\n");
            fprintf(f, "  ;; --- Target: WebAssembly Text Format (cooperative concurrency, Phase 10.2b) ---\n");
            fprintf(f, "  (memory 1)\n");
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            // O(1) region allocator: a bump pointer into linear memory, above
            // the .data region (1024) and the run queue (64..).
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
            // No emission: the function epilogue ($main close / OP_EPILOG)
            // provides the single result value (local.get $w0).
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
        // 5. CHANNELS (in-module linear-memory ring buffers, Phase 10.1/10.2b)
        //    Header (i32 cells): [0]=head [4]=tail [8]=cap, then cap data slots
        //    at +12. The channel base lives in $cp.
        // ====================================================================
        case OP_CHAN_INIT:
            fprintf(f, "    ;; [WASM Channel Init]: ring buffer in linear memory (cap 8)\n");
            fprintf(f, "    global.get $arena_sp\n    local.set $cp\n");          // $cp = channel base
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=0\n");  // head = 0
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=4\n");  // tail = 0
            fprintf(f, "    local.get $cp\n    i32.const 8\n    i32.store offset=8\n");  // cap  = 8
            fprintf(f, "    global.get $arena_sp\n    i32.const 44\n    i32.add\n    global.set $arena_sp\n"); // 12 + 8*4
            break;

        case OP_CHAN_PUT:
            // buf[tail] = $w0 ; tail = (tail+1) % cap. Channel base in $cp.
            fprintf(f, "    ;; [WASM Channel Put]: non-blocking ring-buffer store at tail\n");
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 4\n    i32.mul\n    i32.add\n"); // cp + tail*4
            fprintf(f, "    local.get $w0\n    i32.store offset=12\n");           // mem[cp + tail*4 + 12] = $w0
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 1\n    i32.add\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
            fprintf(f, "    i32.store offset=4\n");                                // tail = (tail+1) % cap
            break;

        case OP_CHAN_GET:
            // Blocking receive: if empty (head==tail) yield to the scheduler so
            // a producer can run, then read buf[head] -> $w0 ; head=(head+1)%cap.
            fprintf(f, "    ;; [WASM Channel Get]: blocking receive via cooperative yield\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=0\n");             // head
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n");             // tail
            fprintf(f, "    i32.eq\n    if\n      call $teko_sched_run\n    end\n"); // empty -> yield
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 4\n    i32.mul\n    i32.add\n"); // cp + head*4
            fprintf(f, "    i32.load offset=12\n    local.set $w0\n");            // $w0 = mem[cp + head*4 + 12]
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 1\n    i32.add\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
            fprintf(f, "    i32.store offset=0\n");                                // head = (head+1) % cap
            break;

        // ====================================================================
        // 6. CONCURRENCY: real cooperative spawn + yield (no host runtime)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            // Enqueue a green thread: fn_index from $w0, argument from $cp (the
            // current channel base). The scheduler dispatches it via call_indirect.
            fprintf(f, "    ;; [WASM Spawn]: enqueue {fn_index=$w0, arg=$cp} into the run queue\n");
            fprintf(f, "    local.get $w0\n    local.get $cp\n    call $teko_enqueue\n");
            break;

        case OP_AWAIT_INTENT:
            // Cooperative yield: hand control to the scheduler to drain ready tasks.
            fprintf(f, "    ;; [WASM Await]: cooperative yield to the scheduler\n");
            fprintf(f, "    call $teko_sched_run\n");
            break;

        // ====================================================================
        // 7. FUNCTION BOUNDARIES (green-thread bodies as separate functions)
        // ====================================================================
        case OP_FUNC_BEGIN:
            // Close whatever function is currently open, then open $routine_<id>.
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");          // close $main (result i32)
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");                              // close previous routine
            }
            fprintf(f, "  (func $routine_%d (param $arg i32)\n", arg);
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            fprintf(f, "    local.get $arg\n    local.set $cp\n"); // green thread receives its channel base
            ctx->wasm_open = 2;
            if (ctx->wasm_routine_count < 64) {
                ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            }
            ctx->wasm_routine_count++;
            break;

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");                              // close routine (no result)
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
            // Close any still-open function (epilogue provides $main's result).
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
            }
            ctx->wasm_open = 0;
            // Function table for call_indirect spawn. Always declare a table so
            // $teko_sched_run's call_indirect is valid even with zero routines.
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
            // DCE resurrection marker for logical instructions mapped above 100.
            if ((int)op >= 100) {
                fprintf(f, "    ;; Label marker: $label_%d\n", (int)op);
            }
            break;
    }
}
