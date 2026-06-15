// Phase 14 (14.A) — native cooperative scheduler for `routines` (background tasks).
//
// This translation unit is the native counterpart of the WASM Layer A run queue
// (emit_wasm.c: $teko_enqueue / $teko_sched_run). It is a SEPARATE object file in
// libteko_rt.a so the linker pulls it in ONLY when a produced binary references
// teko_rt_spawn / teko_rt_run — i.e. only programs that use `routines`. Spawn-free
// programs (e.g. the crypto surface) never reference these symbols, so this TU is not
// linked and their emitted assembly + the teko_routine_table externs below are never
// required — keeping non-concurrent native output byte-identical to before Phase 14.
//
// The hosted native emitter (src/codegen/emit_native_hosted.c) emits, for a routines
// program: each `fn` body as a function `teko_routine_<slot>`, a function-pointer table
// `teko_routine_table` (in declaration/slot order) and a count `teko_routine_count`,
// lowers OP_SPAWN_ASYNC to `call teko_rt_spawn` (slot in arg0), and calls teko_rt_run at
// `$main`'s exit (OP_HALT) to drain the queue before the program returns.
//
// Semantics: run-to-completion cooperative scheduling (the same MVP the WASM Layer A
// scheduler implements). A routine runs to its end; a routine may itself spawn more
// tasks, which are appended and drained in the same pass (FIFO). Blocking/suspending
// channel rendezvous between routines is later Phase 14 work (14.B duplex etc.).

#include "teko_rt.h"

typedef void (*teko_routine_fn)(long);

// Provided by the emitted assembly of a `routines` program (see above). Only referenced
// from this TU, which is only linked when teko_rt_spawn/teko_rt_run are referenced — so a
// program that defines them is exactly a program that pulls this TU in.
extern teko_routine_fn teko_routine_table[];
extern long teko_routine_count;

#define TEKO_RT_RUNQ_CAP 1024 // bounded run queue (matches the WASM run-queue spirit)

static long teko_rt_rq_slot[TEKO_RT_RUNQ_CAP];
static long teko_rt_rq_arg[TEKO_RT_RUNQ_CAP];
static int  teko_rt_rq_head = 0;
static int  teko_rt_rq_tail = 0;

// Enqueue a background task: the routine at table slot `slot`, invoked with `arg`.
// Lowered from OP_SPAWN_ASYNC (slot in the accumulator). Silently drops if the bounded
// queue is full (a hard cap mirroring the WASM emitter's fixed run-queue region).
void teko_rt_spawn(long slot, long arg) {
    if (teko_rt_rq_tail < TEKO_RT_RUNQ_CAP) {
        teko_rt_rq_slot[teko_rt_rq_tail] = slot;
        teko_rt_rq_arg[teko_rt_rq_tail] = arg;
        teko_rt_rq_tail++;
    }
}

// Drain the run queue to completion. Called at `$main`'s exit so fired routines run
// before the program returns. Re-entrant-safe for self-spawning routines: new tasks are
// appended at the tail and picked up by the same loop.
void teko_rt_run(void) {
    while (teko_rt_rq_head < teko_rt_rq_tail) {
        long slot = teko_rt_rq_slot[teko_rt_rq_head];
        long arg  = teko_rt_rq_arg[teko_rt_rq_head];
        teko_rt_rq_head++;
        if (slot >= 0 && slot < teko_routine_count && teko_routine_table[slot]) {
            teko_routine_table[slot](arg);
        }
    }
}
