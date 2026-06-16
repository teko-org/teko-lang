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
#include <stdint.h>

// Phase 15 (15.A): routine functions RETURN a register-width value (their body's last $w0,
// left in rax/x0 across `ret`). The spawn/run path ignores the return (background tasks); the
// synchronous method-call path (teko_rt_call) propagates it. Before Phase 15 this was
// `void (*)(long)`; widening the return type is ABI-compatible (the caller just reads rax/x0).
typedef long (*teko_routine_fn)(long);

// Provided by the emitted assembly of a `routines` program (see above). Only referenced
// from this TU, which is only linked when teko_rt_spawn/teko_rt_run are referenced — so a
// program that defines them is exactly a program that pulls this TU in.
extern teko_routine_fn teko_routine_table[];
extern long teko_routine_count;

#define TEKO_RT_RUNQ_CAP 1024 // bounded run queue (matches the WASM run-queue spirit)
#define TEKO_RT_MAX_ARGS 8    // Phase 14 (14.I): max routine arguments per spawn

static long teko_rt_rq_slot[TEKO_RT_RUNQ_CAP];
static long teko_rt_rq_args[TEKO_RT_RUNQ_CAP][TEKO_RT_MAX_ARGS]; // per-task argument vector
static int  teko_rt_rq_head = 0;
static int  teko_rt_rq_tail = 0;

// Phase 14 (14.I): the routine ABI is teko_routine_<slot>(long args), where `args` is a pointer
// to this task's argument vector; the routine reads args[i] (OP_LOAD_SPAWN_ARG). Each spawn passes
// a pointer to the queue entry's row, so arguments survive until the task runs.

// Enqueue a background task with no arguments (or one legacy ignored value): the routine at table
// slot `slot`. Lowered from OP_SPAWN_ASYNC. `arg` is stored as args[0] for any 1-arg legacy use.
void teko_rt_spawn(long slot, long arg) {
    if (teko_rt_rq_tail < TEKO_RT_RUNQ_CAP) {
        teko_rt_rq_slot[teko_rt_rq_tail] = slot;
        teko_rt_rq_args[teko_rt_rq_tail][0] = arg;
        teko_rt_rq_tail++;
    }
}

// Phase 14 (14.I): stage the idx-th argument for the NEXT teko_rt_spawn_args call (Go-style
// multi-arg routine spawn, lowered from OP_SPAWN_ASYNC_ARGS). Bounded by TEKO_RT_MAX_ARGS.
static long teko_rt_pending_args[TEKO_RT_MAX_ARGS];
void teko_rt_spawn_setarg(long idx, long val) {
    if (idx >= 0 && idx < TEKO_RT_MAX_ARGS) teko_rt_pending_args[idx] = val;
}

// Enqueue the routine at `slot` with the arguments staged via teko_rt_spawn_setarg. Copies the
// pending vector into the task's row so concurrent/nested spawns don't clobber it.
void teko_rt_spawn_args(long slot) {
    if (teko_rt_rq_tail < TEKO_RT_RUNQ_CAP) {
        teko_rt_rq_slot[teko_rt_rq_tail] = slot;
        for (int i = 0; i < TEKO_RT_MAX_ARGS; i++)
            teko_rt_rq_args[teko_rt_rq_tail][i] = teko_rt_pending_args[i];
        teko_rt_rq_tail++;
    }
    for (int i = 0; i < TEKO_RT_MAX_ARGS; i++) teko_rt_pending_args[i] = 0; // reset for the next spawn
}

// Phase 15 (15.A): SYNCHRONOUSLY call the routine at `slot` with the args staged via
// teko_rt_spawn_setarg, and RETURN its result (method dispatch — lowered from OP_CALL_FUNC).
// The staged args are copied to a STACK-LOCAL vector before the call so nested/recursive method
// calls (which re-stage into the global pending buffer) don't corrupt this call's arguments.
long teko_rt_call(long slot) {
    long local_args[TEKO_RT_MAX_ARGS];
    for (int i = 0; i < TEKO_RT_MAX_ARGS; i++) {
        local_args[i] = teko_rt_pending_args[i];
        teko_rt_pending_args[i] = 0; // consume (so a sibling spawn after the call starts clean)
    }
    if (slot >= 0 && slot < teko_routine_count && teko_routine_table[slot]) {
        return teko_routine_table[slot]((long)(intptr_t)local_args);
    }
    return 0;
}

// Drain the run queue to completion. Called at `$main`'s exit so fired routines run
// before the program returns. Re-entrant-safe for self-spawning routines: new tasks are
// appended at the tail and picked up by the same loop.
void teko_rt_run(void) {
    while (teko_rt_rq_head < teko_rt_rq_tail) {
        long slot = teko_rt_rq_slot[teko_rt_rq_head];
        long* args = teko_rt_rq_args[teko_rt_rq_head]; // task's argument vector (read via args[i])
        teko_rt_rq_head++;
        if (slot >= 0 && slot < teko_routine_count && teko_routine_table[slot]) {
            teko_routine_table[slot]((long)(intptr_t)args);
        }
    }
}

// Phase 14 (14.G): `await <ts>;` — a cooperative REAL-TIME timed yield (OP_AWAIT_FOR; ms in arg0).
// It drains the run queue (so queued background tasks run at the await point) and returns only once
// the real MONOTONIC clock has advanced by at least `ms` ms. Cooperative + non-blocking: the OS
// thread is never blocked in the kernel — it keeps draining the scheduler and re-checks the real
// clock (owner decision: cooperative scheduling, real time source). Compare teko_rt_wait_ns, which
// waits the same real span WITHOUT draining the queue.
void teko_rt_await_ns(long ms) {
    long long deadline = teko_rt_now_ns() + (ms > 0 ? (long long)ms * 1000000LL : 0);
    do {
        teko_rt_run(); // cooperative drain: queued routines run at the await point
    } while (teko_rt_now_ns() < deadline);
}
