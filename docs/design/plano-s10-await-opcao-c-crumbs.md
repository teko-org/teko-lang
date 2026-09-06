# §10 `await` — OPTION (c): stackful coroutines over OS primitives via FFI — implementation-depth crumbs

**Status:** DESIGN-AHEAD (architect). No product `.tks`/`.c` changed by this doc.
**Ground truth:** HEAD `deb76e5a` (`git rev-parse HEAD` → `deb76e5adf5c37b82d0d398e7e3d2a964ad57680`).
**Owner ruling this doc designs to:** the `await` suspension model is **OPTION (c) — stackful coroutines over OS
primitives via FFI** (supersedes the D2 "thread-per-await" recommendation in
`plano-s10-concorrencia-crumbs.md` L27-40). Cancel semantics are SETTLED by the owner (reproduced in §CN1).
**Builds on (LANDED):**
- spawn — `tk_thread_spawn`/join twin (`teko_rt.c:2554`/`:2567`, header `teko_rt.h:227/:229`), S1-S3 surface,
  per-site ctx-blob + deep-copy of args by value (`plano-s10-spawn-batch-detalhe.md`).
- channels — `tk_memchan_*` (`teko_rt.c:2605+`), `tk_oschan_*` (`:2840+`), `tk_waitgroup_*`
  (`:2758-2789`, the futex/condvar coordination v1 leans on), `tk_region_deregister`, `src/threads/threads.tks`
  (`IChannelKind<T>`/`Closed`/`Rx<T>`/`Tx<T>`/`Ctx` scaffolding). `plano-s10-channels-batch-detalhe.md`.
- runtime — `tk_task_begin`/`tk_task_end` (arena/task bracket, `teko_rt.c:2499`/`:2510`, header `:215`/`:216`),
  `tk_region_program` (F2 singleton, `teko_rt.c:2177`), the `_Thread_local tk_g_current_task` per-thread arena
  root, C11 `__atomic_*`, `-pthread` in the C ladder.
- generics — 9-ops / #254 / fase1b + F1/F3/F4; **Gap-2 PROVEN** (a generic union-over-`T` like
  `Rx<T>.pop(): T | Closed` and `Intent<T>._value: T | null` stamps correctly).

**Spec:** `docs/design/mudancas-superficie-0.3.1.md` §10.3 (L842-943, sealed), §10.5 (L1026-1040), §10.6
(L1041-1047 — the `ref` UAF ruling).

**Scope of THIS doc:** the SUSPENSION PRIMITIVE (`tk_coro_*`), the MINI-SCHEDULER (`teko::threads`), the
**A1-A4** await crumbs and the **CN1** cancel crumb, plus the `marshal<T>` INTERFACE (call sites only; internals are
the owner-deferred §5 ruling). `teko::journal` (J1) stays out of scope.

---

## Corpus-leaf invariant (holds for EVERY crumb here) — §10 stays a LEAF

`grep -rn '\bawait\b' src/ --include=*.tks` finds **0** statement-head hits (the token is unused as an identifier
too); the compiler's own axis-2 parallel codegen uses the SEPARATE internal `fork_join` (§10.2 L757), never
`await`/coroutines. So the corpus NEVER writes `await`, never instantiates `Intent<T>`, never calls a `tk_coro_*`
primitive. Consequences that gate the reseed table below:
- every new grammar/checker/lowering path is DEAD CODE while the compiler compiles `src/` — the 2-pass fixpoint
  emits a byte-identical `teko.c` (the new `tk_coro_*` calls appear in an emitted `teko.c` ONLY when a user program
  writes `await`/`cancel`);
- the maintained-C primitives (`tk_coro_*`) are compiled+linked into the runtime object but never EMITTED by
  codegen, so adding them changes no byte of any emitted `teko.c` — a RUNTIME REBUILD, not a codegen reseed (the
  exact C0a/C0b argument, `plano-s10-spawn-batch-detalhe.md` L541-549).

**Do NOT let the compiler adopt the `await`/coroutine surface during this issue** — it would put `tk_coro_*` and
`Intent<T>` monomorphizations into the compiler binary and make the coroutine model load-bearing for the compiler's
own build. Keep `fork_join` as axis-2's mechanism.

---

## 1. The SUSPENSION PRIMITIVE — `tk_coro_*` (maintained-C, per-`#os` via FFI) — the FIRST implementable crumb

**Crumb id: CO0.** **Files:** `src/runtime/teko_rt.{c,h}` (maintained-C, Teko-only-law-exempt). **Kind:** `[C-rt]`
— runtime rebuild only, NO fixpoint reseed (the corpus never coroutines). **Precedent:** the per-`#os` split
already in this file (`#if defined(_WIN32)` … `#else` …, e.g. `teko_rt.c:9`/`:144`/`:292`, and the whole
`tk_oschan_*` Windows honest-stop block at `:2989-3005`); the arena bracket ownership `tk_thread_start` holds
(`teko_rt.c:2545`).

### CO0.1 — the model (stackful, arena-per-coroutine)

A `tk_coro` is a stackful execution context: its OWN machine stack + register file, switchable by a
`swapcontext`-class primitive. **The arena rule (the whole point):** each coroutine gets its OWN `tk_task_begin`
sub-root the instant it first runs, and its `tk_task_end` at finish — the identical bracket `tk_thread_start`
installs for a spawned thread (`teko_rt.c:2545-2551`), so a coroutine's `tk_arena_pop`/`tk_task_end` can NEVER reach
another coroutine's or thread's allocations (the `_Thread_local tk_g_current_task` is swapped in lock-step with the
stack, see CO0.4). **The copy IS the isolation, again** — a value entering a coroutine crosses by `marshal<T>`
(§marshal), never by a shared arena pointer.

### CO0.2 — the per-`#os` transport of the stack-switch (FFI/`#os`)

| target | mechanism | primitives |
|--------|-----------|------------|
| POSIX (Linux, macOS) | `ucontext` | `getcontext` / `makecontext` / `swapcontext` (`<ucontext.h>`) |
| Windows | Fibers | `ConvertThreadToFiber` / `CreateFiber` / `SwitchToFiber` / `DeleteFiber` (`<windows.h>`) |

Selected by `#if defined(_WIN32)` inside `teko_rt.c` — the same conditional-compilation the file already uses;
Teko code and codegen NEVER see which is chosen (they emit only the `tk_coro_*` calls). **Two performance notes,
recorded so they are not re-litigated:**
- `ucontext`'s `swapcontext` calls `sigprocmask` (a syscall) on every switch — measurable but acceptable for v1.
  A custom-asm stack-switch (no signal-mask save) is a **later optimization** INSIDE `teko_rt.c`, zero surface
  impact.
- both mechanisms are §16/§17-**TRANSITIONAL**: when `teko_rt.c` retires to full OS-FFI (§16→§17), `tk_coro_*`
  becomes direct FFI to the OS calls (or the custom-asm switch). Marked in the doc-comments exactly like
  `pthread_create` is (`teko_rt.h:224`).

### CO0.3 — signatures (add to `teko_rt.h`, near the task/thread decls at `:213-238`)

```c
/* tk_coro — an opaque stackful coroutine handle: its own machine stack + saved context, plus the OWN sub-root
 * arena it runs on (a tk_task_begin/tk_task_end bracket, exactly as tk_thread_start owns for a spawned thread,
 * teko_rt.c:2545). Type-erased: it runs `entry(arg)` where the codegen-emitted `entry` unpacks a marshalled arg
 * blob. Resident in the F2 program region (tk_region_program) so the handle outlives the creating task/scope — a
 * coroutine is cross-scope by construction (an await arm outlives the statement that spawned it until joined).
 *
 * §16/§17 NOTE: ucontext (POSIX) / Fibers (Windows) are the TRANSITIONAL stack-switch. When teko_rt.c retires to
 * OS-FFI, tk_coro_* becomes direct FFI (or a custom-asm switch that skips ucontext's sigprocmask). Nothing outside
 * this file may assume which mechanism is in use. */
typedef struct tk_coro tk_coro;

/* tk_coro_make — allocate a coroutine that will run `entry(arg)` on a FRESH sub-root arena, with a stack of
 * `stack_bytes` (0 => a runtime default, e.g. 128 KiB). Does NOT start it — the coroutine is READY but unscheduled;
 * the scheduler (teko::threads) starts it with tk_coro_switch. `arg` is a single self-contained block the caller
 * marshalled (ownership passes to the coroutine, which frees it after `entry` returns — the ctx-blob discipline of
 * S3). OOM / makecontext failure => tk_panic (a coroutine that cannot start is program-fatal, matching
 * tk_thread_spawn). */
tk_coro *tk_coro_make(void (*entry)(void *), void *arg, uint64_t stack_bytes);

/* tk_coro_switch — SUSPEND the current execution context and RESUME `to`, returning to the caller only when some
 * other switch targets THIS context again. This is the one stack-switch primitive: the scheduler's run-loop uses
 * it to enter a ready coroutine; a coroutine uses it (via tk_coro_yield) to hand control back to the scheduler. It
 * swaps tk_g_current_task in lock-step (CO0.4), so each side always runs on its own arena. `to` must be a live
 * coroutine or the scheduler's root context (tk_coro_root). */
void tk_coro_switch(tk_coro *from, tk_coro *to);

/* tk_coro_yield — the current coroutine cedes to `sched` (its scheduler context). Sugar over tk_coro_switch with
 * `from` = the current coroutine (looked up in a _Thread_local `tk_g_current_coro`). Returns when the scheduler
 * next resumes this coroutine. NEVER blocks the OS thread — it stack-switches away; the thread keeps running other
 * coroutines. */
void tk_coro_yield(tk_coro *sched);

/* tk_coro_finish — called by the coroutine trampoline AFTER `entry` returns (normal completion) OR by the cancel
 * unwind (CN1): tears down the coroutine's arena (tk_task_end on its sub-root) and switches CONTROL back to the
 * scheduler with `status` recorded (0 = normal, 1 = canceled). Does not return. The handle is reclaimed by the
 * scheduler after the join edge (tk_coro_status is read first). */
_Noreturn void tk_coro_finish(tk_coro *self, tk_coro *sched, int status);

/* tk_coro_status — the finish status of a coroutine that has completed (0 normal, 1 canceled); undefined before
 * finish. The scheduler / await join reads it to decide the arm's Intent shape. */
int tk_coro_status(tk_coro *co);

/* tk_coro_root — the scheduler's ROOT context for THIS thread: converts the current OS stack into a switchable
 * context (getcontext / ConvertThreadToFiber) the first time it is called, cached _Thread_local. The run-loop
 * switches from here into ready coroutines and back. Idempotent. */
tk_coro *tk_coro_root(void);

/* tk_coro_current — the coroutine THIS execution context is running (NULL on the root/scheduler context). Reads a
 * _Thread_local set by tk_coro_switch. cancel() consults it to decide suspended (a coroutine is current) vs
 * outside-suspension (NULL => immediate panic, §10.3 L916). */
tk_coro *tk_coro_current(void);

/* tk_coro_free — release a FINISHED coroutine's stack + handle (its arena is already gone via tk_coro_finish's
 * tk_task_end). Called by the scheduler after the join edge. */
void tk_coro_free(tk_coro *co);

/* tk_coro_selftest — the C-owned probe body the `coro_pingpong` fixture reaches through ONE plain-scalar extern
 * (the tk_thread_spawn_selftest pattern, teko_rt.c C0a). Makes `n` coroutines each of which yields k times while a
 * driver loop round-robins them to completion, asserts every coroutine ran on its OWN arena (a scratch alloc
 * survives across its yields but never leaks into a sibling), then returns tk_names_live_count() delta. Under
 * TEKO_MEM_PARANOID=1 the caller asserts the delta is 0. */
uint64_t tk_coro_selftest(int64_t n);
```

### CO0.4 — the arena lock-step (why an isolated arena survives a yield)

`tk_g_current_task` is `_Thread_local` (`teko_rt.c:1349` struct, the per-thread root). A stackful switch that did not
also swap the current task would let coroutine B run on coroutine A's arena. So `tk_coro_switch` swaps BOTH the
machine context AND `tk_g_current_task` (saving the outgoing coroutine's task on its handle, installing the incoming
one's) in a single primitive — the arena "follows" the stack. A coroutine's first entry does `tk_task_begin` (its
own sub-root); every subsequent resume re-installs that same task; `tk_coro_finish` does `tk_task_end`. Result: a
value the coroutine bump-allocated before a yield is still live after the yield (same arena), and is freed wholesale
at finish — this is what makes "the parent yields, the arm runs, the arm's result is `marshal`led out, the arm's
arena dies" sound (§A4).

### CO0.5 — fixture `coro_pingpong` (drives the primitive BEFORE any surface exists)

New probe `examples/probes/coro/` — Teko side reaches `tk_coro_selftest` through one scalar extern (the C0a idiom;
no `cabi fn` — the extern takes only `i64`→`u64`):

```
/**
 * coro_pingpong — drive the maintained-C tk_coro primitive before the await surface exists: make N coroutines,
 * round-robin them to completion, and assert every coroutine ran on its OWN arena with a balanced live-count.
 * The success sentinel is 42 (the thread_spawn / chan_dgram convention); any arena drift exits 1.
 *
 * @return  process exit 42 on success, 1 on any drift
 * @since   0.3.1 §10 CO0
 */
fn main() {
    var drift = coro_selftest(64)
    if drift != 0 { teko::process::exit(1) }
    teko::process::exit(42)
}
```

Driven `TEKO_MEM_PARANOID=1 ./…/coro` → exit **42**; any live-count drift → **1**. **This is the first implementable
crumb** — maintained-C, zero new Teko surface, no reseed, unblocks the whole await spine.

---

## 2. The MINI-SCHEDULER (Teko, `teko::threads`) + the v1 REACTOR scoping

**Crumb id: SCH1.** **File:** new `src/threads/scheduler.tks` (namespace `teko::threads`, beside the C1
scaffolding). **Kind:** `[L]` stdlib leaf (the corpus never runs the scheduler; the reseed is
byte-identical/mechanical). It is thin Teko over the CO0 externs.

### SCH1.1 — the shape (a ready-queue + a run-loop over `tk_coro_*`)

```
/**
 * Scheduler — the per-thread cooperative run-loop over stackful coroutines (§10 option c). Holds a FIFO ready-queue
 * of runnable coroutine ids and drives them with tk_coro_switch: dequeue a ready coroutine, switch into it, and
 * when it yields (or finishes) reclaim control on the root context and pick the next. v1 is SINGLE-THREADED per
 * scheduler (parallelism comes from spawning the await arms as threads/coroutines — §A4 — not from the loop). A
 * coroutine blocked on a channel/waitgroup is NOT on the ready-queue; it is re-queued when the coordination
 * primitive wakes (SCH1.3). Resident in the program root (F2) so it outlives any single await statement.
 *
 * @field _root   the scheduler's root context id (tk_coro_root)
 * @field _ready  the FIFO of runnable coroutine ids
 * @since 0.3.1 §10 SCH1
 */
pub type Scheduler = struct {
    _root: u64
    _ready: teko::collections::Queue<u64>

    /**
     * enqueue — mark a coroutine RUNNABLE (append to the ready FIFO). Called when a coroutine is first made and
     * whenever a coordination primitive wakes it (SCH1.3).
     *
     * @param co  the coroutine id to make runnable
     */
    pub fn enqueue(co: u64) { /* SCH1.2 lowers to _ready.push(co) */ }

    /**
     * run_until_idle — the run-loop: switch into each ready coroutine in FIFO order until the ready-queue is empty
     * (every coroutine has either finished or parked on a coordination primitive). Returns to the caller (an await
     * statement's parent) when its awaited arms have all finished — the await context re-queues the parent, which
     * this loop then resumes (§A4).
     */
    pub fn run_until_idle() { /* SCH1.2 lowers to the tk_coro_switch loop */ }
}

/**
 * scheduler — the current thread's Scheduler singleton, materialized on first await (F2-resident, one per OS
 * thread; a spawned worker thread that itself awaits gets its own). Reached like svc — by a fixed key into the
 * program region.
 *
 * @return  this thread's scheduler
 * @since 0.3.1 §10 SCH1
 */
pub fn scheduler(): Scheduler { /* loads/creates the F2 singleton */ }
```

`Queue<u64>` is the fase1b collection (or a tiny local ring if a FIFO is not yet in `teko::collections` — honest-stop
the missing piece pointing at fase1b, but a ring over `[]u64` compiles today).

### SCH1.2 — yield / resume

- **yield:** a coroutine calls `teko::threads::yield()` → `tk_coro_yield(scheduler()._root_coro)`. The OS thread is
  NOT blocked; control returns to `run_until_idle`, which picks the next ready coroutine.
- **resume:** `run_until_idle` dequeues a coroutine id and `tk_coro_switch(root, co)`. On the coroutine finishing
  (`tk_coro_finish` switched back with a status), the loop reads `tk_coro_status`, runs the finish callback (the
  await join decrements the waitgroup / fills the slot — §A4), frees the coroutine, and continues.

### SCH1.3 — **v1 REACTOR IS MINIMAL — futex/condvar now, epoll/io_uring LATER**

The v1 scope is **compute + channel/waitgroup coordination ONLY**. When a coroutine must wait for another arm (the
await join) it waits on the **already-landed** `tk_waitgroup_*` (`teko_rt.c:2758-2789`, condvar-backed); when it
must wait for a value it waits on the **already-landed** `tk_memchan_*`/`tk_oschan_*`. So the v1 reactor is:

> **the futex/condvar the landed channel + waitgroup primitives already provide.** NO epoll, NO io_uring, NO kqueue,
> NO IOCP in v1.

Concretely: the await-context's waitgroup-0 edge (§A4) is what re-queues the parent coroutine; each arm's completion
decrements it; the scheduler's `run_until_idle` returns to the parent's caller once the parent is re-queued and
resumed. A coroutine that blocks on a channel `recv` blocks its OS thread on the condvar in v1 (acceptable — the
arm is its own coroutine, and v1's parallelism is the arms-are-threads spawn, not loop concurrency). The single
subtlety recorded: if two arms on the SAME scheduler thread both block on condvars, v1 relies on the arms being
SPAWNED (own thread/coroutine, §A4 point) so a condvar wait does not deadlock the loop — the arms run on their own
threads' schedulers, and the parent's waitgroup wait is the only cross-thread block.

**The FULL I/O REACTOR is a LATER crumb (`RC1`, out of scope here):** io_uring (Linux) / epoll (Linux fallback) /
kqueue (macOS) / IOCP (Windows), per-`#os` via FFI, needed ONLY when `await` drives OS I/O directly (a socket
read/timer completing without a blocked thread). It is §16/§17-adjacent (it is more OS-FFI in `teko_rt.c`) and is
explicitly NOT required for the v1 await model to be correct — it is a THROUGHPUT/scaling upgrade. Be explicit in
the crumb: v1 ships without it; the `await` observable of §10.3 ("cede, nunca bloqueia" for the AWAITING coroutine)
is satisfied by the coroutine yield + waitgroup join, since the awaiting coroutine yields (does not block) while its
arms run on their own threads.

---

## A1 — `Intent<T>` / `Intent` (stdlib) — `[L]` leaf, UNBLOCKED (leans on Gap-2, proven)

**Crumb id: A1.** **File:** `src/threads/threads.tks` (beside the channel scaffolding). The spec's protected
struct, transcribed verbatim from §10.3 L873-903 (the `exp`/`pub` face is forward-compatible; today all read as
`exp`, §11 enforcement auto-corrects). **Gap-2 is proven**, so `Intent<T>._value: T | null` stamps
(`Intent__g__i32` etc.) — the same generic-union-over-`T` the landed `rx_pop_closed` fixture pins.

```
/**
 * Intent — the outcome of awaiting a function with NO return: just the cancel outcome (§10.3 L874). PROTECTED — the
 * dev READS, the runtime WRITES, nobody hand-initializes. Backing fields private (runtime/compiler only); read face
 * via `exp get`; the outcome written via `pub set`; the only construction is `pub static fn new` (compiler-internal,
 * emitted by the await lowering — §A4). Value-struct + runtime fill requires the `set` to hit the SAME instance the
 * awaiter holds (no copy between fill and read — F1/F2 residence, §10.2 L910).
 *
 * @since 0.3.1 §10 A1
 */
exp type Intent = struct {
    /** _canceled — set true by the cancel unwind (CN1); read via `canceled`. */
    _canceled: bool
    /** _failure — the error carried by the cancel, or null on normal completion; read via `failure`. */
    _failure: error | null

    /**
     * canceled — did the awaited task cancel?
     * @return  true iff a cancel unwound this arm
     */
    exp get canceled(): bool { self._canceled }
    /**
     * failure — the error the cancel carried (null on normal completion).
     * @return  the cancel's error, or null
     */
    exp get failure(): error | null { self._failure }
    /**
     * canceled — runtime writes the cancel flag.
     * @param v  the new cancel flag
     */
    pub set canceled(v: bool) { self._canceled = v }
    /**
     * failure — runtime writes the cancel error.
     * @param v  the new failure
     */
    pub set failure(v: error | null) { self._failure = v }

    /**
     * new — the compiler-internal factory the await lowering calls to materialize an arm's Intent (§A4). Never
     * written by hand.
     *
     * @param c  the cancel flag
     * @param f  the cancel error (or null)
     * @return   a fresh Intent
     */
    pub static fn new(c: bool, f: error | null): self { .{ _canceled = c; _failure = f } }
}

/**
 * Intent<T> — the outcome of awaiting a function that RETURNS T: the value falls into `.value`, plus the cancel
 * outcome (§10.3 L888). PROTECTED exactly as `Intent`. `_value: T | null` is a generic union-over-`T` (Gap-2,
 * proven — stamps like the landed `Dictionary<K,V>.get(k): V | null`). Business errors ride in `.value` (a
 * `T = R | error` domain type); cancellation is `.canceled` + `.failure`.
 *
 * @param T  the awaited function's normal return type
 * @since 0.3.1 §10 A1
 */
exp type Intent<T> = struct {
    /** _value — the awaited value, or null before fill / on cancel; read via `value`. */
    _value: T | null
    /** _canceled — set true by the cancel unwind; read via `canceled`. */
    _canceled: bool
    /** _failure — the cancel error, or null; read via `failure`. */
    _failure: error | null

    /**
     * value — the awaited value (null before fill or on cancel).
     * @return  the awaited `T`, or null
     */
    exp get value(): T | null { self._value }
    /**
     * canceled — did the awaited task cancel?
     * @return  true iff a cancel unwound this arm
     */
    exp get canceled(): bool { self._canceled }
    /**
     * failure — the cancel error (null on normal completion).
     * @return  the cancel's error, or null
     */
    exp get failure(): error | null { self._failure }
    /**
     * value — runtime writes the awaited value.
     * @param v  the value (or null)
     */
    pub set value(v: T | null) { self._value = v }
    /**
     * canceled — runtime writes the cancel flag.
     * @param v  the flag
     */
    pub set canceled(v: bool) { self._canceled = v }
    /**
     * failure — runtime writes the cancel error.
     * @param v  the error (or null)
     */
    pub set failure(v: error | null) { self._failure = v }

    /**
     * new — the compiler-internal factory the await lowering calls per arm (§A4). Never written by hand.
     *
     * @param v  the awaited value (or null)
     * @param c  the cancel flag
     * @param f  the cancel error (or null)
     * @return   a fresh Intent<T>
     */
    pub static fn new(v: T | null, c: bool, f: error | null): self { .{ _value = v; _canceled = c; _failure = f } }
}
```

**Overload coexistence** (§ spec L333-334): `Intent` (arity-0, no data) and `Intent<T>` (arity-1) coexist — the
generic-arity overload the owner already relies on. **Fixture `intent_stamp` (`[L]`):** materialize an
`Intent<i32>` via `Intent<i32>::new(7, false, null)`, read `.value`/`.canceled`/`.failure`, two-level-match
`_value: i32 | null` → exit **0** on the round-trip, **1** on a stamp/read failure. This is the A1 de-risk (composes
the proven Gap-2 stamp with the `error | null` member); expected green.

---

## A2 — the `await` PARSER (prefix binding form) — `[C]` compiler-touching, UNBLOCKED

**Crumb id: A2.** **Files:** `src/parser/parse_stmt.tks` (dispatch at `:117`, the `is_binding_head` gate) +
`src/parser/ast.tks` (the `awaited` flag on `Binding` `:340` and `MultiBind` `:521`). `await` is a CONTEXTUAL
statement-head keyword (Ruling A, §10.1 L677) — recognized ONLY as `Ident("await")` immediately followed by a
binding head (`var`/`let`/`mut`/`const`/`_`) or a bare re-assign target. Zero corpus collisions (the leaf invariant).

### A2.1 — the AST delta (an additive flag, NOT a new node)

The spec makes `await` **widen an existing binding/multibind** (`await var a = f()`, `await var a, b = fa(), fb()`,
`await a = fa()` re-assign, `await _ = f()` discard). So it is a boolean on the existing nodes, not a new statement
type — keeping every `@Statement()` walker unchanged except the ones that already visit `Binding`/`MultiBind`.

```
pub type Binding = struct {
    kind: BindKind
    target: @BindTarget()
    has_type: bool
    type_ann: @TypeExpr()
    is_ref: bool
    value: Expr
    awaited: bool   /* §10.3: `await var a = f()` — the checker widens `a`'s type T -> Intent<T> and A4 lowers it */
}
```

and the identical additive `awaited: bool` on `MultiBind` (`ast.tks:521`). Add the field LAST so the TKB byte layout
of the pre-existing fields is unchanged (the `AssignKind::Index`-appended-last discipline, `ast.tks:357`). A bare
re-assign `await a = fa()` (§10.3 L857) is an `Assign` (`ast.tks:363`), so `Assign` gains the same `awaited: bool`
LAST too.

> **Doc-comment note for the implementer:** each of `Binding`/`MultiBind`/`Assign`'s type doc-comment gains an
> `@field awaited …` line (full-Javadoc); the three are mechanical.

### A2.2 — recognition + parse (add to `src/parser/parse_stmt.tks`)

```
/**
 * is_await_head — is the token at `pos` a CONTEXTUAL `await` binding head? True ONLY for `Ident("await")` FOLLOWED
 * BY a binding head (`var`/`let`/`mut`/`const`), a discard `_`, or an identifier that begins a re-assign (`await a
 * = …`). Any other use of a symbol named `await` (a bare expr, a call `await(...)`, a field `await.x`) is NOT a head
 * and falls through to ordinary parsing (the contextual-keyword invariant, Ruling A). `await(` is EXCLUDED (an
 * LParen means `await` is the callee).
 *
 * @param []lexer::Token tokens  the token stream
 * @param u64 pos  the candidate statement-head index
 * @return bool  true iff `pos` opens an `await <binding|reassign|discard>` statement
 * @since 0.3.1 §10 A2
 */
fn is_await_head(tokens: []lexer::Token, pos: u64): bool {
    if !is_kind_at(tokens, pos, lexer::TokenKind::Ident) { return false }
    if tokens[pos].text != "await" { return false }
    if is_binding_head(tokens, pos + 1) { return true }               // `await var …` / `await let …` / …
    if is_kind_at(tokens, pos + 1, lexer::TokenKind::Underscore) { return true }  // `await _ = …`
    is_kind_at(tokens, pos + 1, lexer::TokenKind::Ident)             // `await a = …` re-assign
}

/**
 * parse_await — parse a contextual `await <binding|multibind|reassign|discard>` at `pos` (the `await` ident;
 * is_await_head true). Delegates to the EXISTING binding/multibind/assign parser one token past `await`, then sets
 * the `awaited` flag on the produced node. `await _ = f()` parses as a discard binding (no variable materialized —
 * §10.3 L929); `await _, x = fa(), fb()` as a multibind with a discard element. No new grammar beyond the flag.
 *
 * @param []lexer::Token tokens  the token stream
 * @param u64 pos  the index of the `await` ident
 * @return Parsed<@Statement()> | error  the awaited binding/multibind/assign, or a parse error
 * @throws  the token after `await` is not a binding/reassign/discard head, or a parse error inside it
 * @since 0.3.1 §10 A2
 */
fn parse_await(tokens: []lexer::Token, pos: u64): Parsed<@Statement()> | error {
    var inner = match parse_statement_at(tokens, pos + 1) { Parsed<@Statement()> as x => x; error as e => return e }
    match inner.node {
        Binding   as b  => Parsed<@Statement()> { node = with_awaited_binding(b);   next = inner.next }
        MultiBind as mb => Parsed<@Statement()> { node = with_awaited_multibind(mb); next = inner.next }
        Assign    as a  => Parsed<@Statement()> { node = with_awaited_assign(a);     next = inner.next }
        _ => return err_at(tokens, pos + 1, "`await` must prefix a binding or re-assignment, e.g. `await var a = f()`")
    }
}
```

`with_awaited_*` are tiny local helpers rebuilding the node with `awaited = true`. `parse_statement_at` is the
existing per-position statement parser the dispatch at `:117` already calls. **Dispatch wiring** (in
`parse_statement`, before the `is_binding_head` test at `:117`): `if is_await_head(tokens, pos) { return
parse_await(tokens, pos) }`.

### A2.3 — collision confirmation & fixtures

`is_await_head` requires `Ident("await")` FOLLOWED BY a binding head / `_` / ident — the corpus never writes that
(0 hits), so the branch is never taken while compiling `src/`. Fixtures: parse-only golden `await_parse` (each of
`await var a = f()`, `await var a, b = fa(), fb()`, `await a = fa()`, `await _ = f()`, `await _, x = fa(), fb()`
round-trips to the `awaited=true` node); reject `await_not_binding` (`await 1 + 2`, `await foo.bar`) → the A2 parse
error.

---

## A3 — the `await` CHECKER (widen `T` → `Intent<T>`, ref-guard, arity-match) — `[C]`, UNBLOCKED

**Crumb id: A3.** **File:** `src/checker/typer.tks` (`type_binding` `:6558`, `type_multibind_*` `:7141`/`:7225`,
`type_binding_value` `:6496`). Rules:
1. **widen:** each awaited arm's call return type `T` becomes the binding's type `Intent<T>` (a no-return `f(): null`
   / `f()` widens to the arity-0 `Intent`). The compiler knows N statically (the multibind arm count).
2. **ref-guard (§10.6 L1047):** NO `ref` parameter may cross the await boundary (an awaited function rejects a `ref`
   param/value) — the SAME guard S2 applies to spawn (`tspawn_reject_ref_params`,
   `plano-s10-spawn-batch-detalhe.md` S2.2), reused verbatim per arm. Also: no `ref` may be RETURNED across (the
   arm's arena dies at finish — a returned `ref` would dangle).
3. **arity-match:** N targets ↔ N call expressions (parallel multibind, §9.3) — already enforced by
   `type_multibind_parallel` (`:7225`); the await path adds only the widen + ref-guard on top.
4. **each arm's return `T` must be `marshal<T>`-copyable** (the result crosses the arm's arena→parent's slot — §A4).
   Same per-type predicate S2 gates spawn args on; honest-stop the v1 boundary (class/closure/nested-pointer graph)
   pointing at the §5 marshalling ruling.

### A3.1 — the widen helper (add beside `type_binding` at `typer.tks:6558`)

```
/**
 * intent_widen — widen an awaited arm's normal return type into the `Intent` the binding receives (§10.3 L845): a
 * function returning `T` widens to `Intent<T>`; a no-return function (`: null` or bare) widens to the arity-0
 * `Intent`. This is the ONE place the await surface's "alarga, não estreita" rule (widen, don't narrow) is realized
 * — the awaited function's own signature is untouched; only the BINDING's type changes.
 *
 * @param @Type() ret  the awaited call's normal return type (the un-widened `T`, or null/void for no-return)
 * @param TypeTable table  the program type table (to resolve the `Intent`/`Intent<T>` named types)
 * @return @Type()  `Intent<T>` for a value-returning arm, `Intent` for a no-return arm
 * @since 0.3.1 §10 A3
 */
fn intent_widen(ret: @Type(), table: TypeTable): @Type() {
    match ret {
        Null => named_type("Intent", table)                       // no-return arm -> arity-0 Intent
        _    => generic_named_type("Intent", [ret], table)        // value arm -> Intent<T> (stamps via Gap-2)
    }
}
```

### A3.2 — wiring into the binding/multibind typers

- `type_binding` (`:6558`): when `b.awaited`, type the value as a CALL (require `b.value.kind` ∈ {Call, MethodCall}
  — an awaited arm must be a call, §10.3 L860), apply the ref-guard + marshal-copyable gate to the callee, then set
  the binding's bound type to `intent_widen(call.type, table)`. The typed node carries an `awaited` flag through to
  `TBinding` (add `awaited: bool` to `TBinding` in `tast.tks`, LAST field).
- `type_multibind_parallel` (`:7225`): when `mb.awaited`, apply the same per-arm treatment to each of the N
  call expressions; each target's type becomes `intent_widen(arm_i.type, table)`. Reject `ArrayDecomp` shape under
  `await` (§10.3 L927: "sem `await` de array" — array decomposition is not an await form) with a named honest-stop.
- **discard `_`:** an awaited discard element (`await _ = f()`, `await _, x = …`) types the call (so its effects +
  the wait happen) but materializes NO binding and NO Intent (§10.3 L929-931) — the multibind path already models a
  discard element; the await path just still spawns+joins the arm (§A4) without a result slot.
- `TAssign` gains `awaited: bool` too, for the re-assign form (`await a = fa()`), widening the RHS identically.

### A3.3 — fixtures

- `await_widen` (regression, C backend): `fn calc(x: i32): i32 { x + 1 }`; `await var a = calc(41)`; `a` must type as
  `Intent<i32>`; read `a.value` → **42**; a mistyped `a` (e.g. `a.value` typed as bare `i32`) is a compile error.
- reject `await_ref_arg` (compile-must-fail): `await var a = f(r)` where `f(ref x: i32)` → the ref-guard message.
- reject `await_uncopyable` (compile-must-fail): an awaited arm returning a class instance → the marshal-copyable
  honest-stop.
- reject `await_array` (compile-must-fail): `await var [a] = xs` → the "no `await` de array" honest-stop.

---

## A4 — the `await` LOWERING (the heart) — `[C]`, BACKEND-AGNOSTIC runtime calls

**Crumb id: A4.** **File:** `src/codegen/codegen.tks` (the C leg — spawn's shipping path) + the native honest-stop
at `src/lir/lower.tks` (`lower_stmt`, the `_ => error{…}` fall-through at the pattern seen at `lower.tks:1853`).
**The rule the owner pinned: lower to runtime CALLS (coro make/switch/finish, waitgroup, marshal), NOT a codegen
state-machine and NOT setjmp-in-C.** Both the C leg and (later) the native leg emit the SAME call-shape; a piece
may native-honest-stop per-crumb, but the await lowering itself is a call sequence both legs can emit.

### A4.1 — what `await var a, b, c = f1(), f2(), f3()` lowers to (N arms, N statically known)

The **await CONTEXT** (not the parent) drives it. Per await statement, keyed by a fresh id `<k>` (`fresh_named`, the
S3 pattern), codegen emits, INLINE where the statement sits (on the parent coroutine, its arena live):

1. **create the await-context:** a `waitgroup(N)` (`tk_waitgroup_make` + `tk_waitgroup_add(wg, N)`,
   `teko_rt.c:2758/:2765`) and **N result slots** (one typed cell per arm, in the parent's arena — the slot receives
   the `marshal`led value + the arm's Intent shape).
2. **spawn N arms** — each arm is IDENTICAL to `spawn` (own thread/coroutine + own sub-root arena, S3's ctx-blob +
   `marshal` of the arm's args): each arm runs `fi()` wrapped in the compiler-generated `on canceled` (§CN1) that
   populates arm `i`'s Intent; **parallelism comes from spawning the arms** (the S3 `tk_thread_spawn` +
   `tk_coro_make` pairing), NOT from the await model. Each arm, on:
   - **normal completion:** `marshal<Ti>`s its result into slot `i`, writes `Intent<Ti>::new(value, false, null)`,
     then `tk_waitgroup_done(wg)`;
   - **cancel (the compiler-generated `on canceled as c`):** writes `Intent<Ti>::new(null, true, c)` into slot `i`,
     then `tk_waitgroup_done(wg)` — a canceled arm STILL decrements (the await joins ALL arms; §10.3 L925 "esperar
     todas é seguro").
3. **the parent yields:** `tk_coro_yield(scheduler()._root)` — the parent coroutine stack-switches AWAY (never
   blocks its OS thread). The await-context registers a wake on the waitgroup-0 edge that re-queues the parent.
4. **on waitgroup-0** (all N arms `done`): the await-context re-queues the parent; the scheduler resumes it.
5. **the parent reads the N Intents:** each binding target `a`/`b`/`c` is bound to slot `i`'s `Intent<Ti>` (a discard
   `_` slot is joined but not bound). Control falls through to the next statement.

### A4.2 — the emitted call-shape (both legs can emit this; here the C leg)

```c
{   /* await-context <k> for `await var a, b, c = f1(), f2(), f3()` */
    tk_waitgroup *wg_<k> = tk_waitgroup_make();
    tk_waitgroup_add(wg_<k>, 3);
    /* N result slots — typed Intent cells in the PARENT arena (marshal target) */
    Intent__g__T0 slot0_<k>; Intent__g__T1 slot1_<k>; Intent__g__T2 slot2_<k>;
    /* spawn each arm as a coroutine on its own sub-root arena (the S3 ctx-blob + tk_coro_make pairing) */
    tk_await_spawn_arm_<k>_0(wg_<k>, &slot0_<k>, /*marshalled args*/ …);
    tk_await_spawn_arm_<k>_1(wg_<k>, &slot1_<k>, …);
    tk_await_spawn_arm_<k>_2(wg_<k>, &slot2_<k>, …);
    /* the parent yields; re-queued on wg-0 by the await-context */
    tk_await_join_<k>(wg_<k>, scheduler_root());   /* yields; returns after wg hits 0 */
    tk_waitgroup_end(wg_<k>);
}
/* the parent then reads: a = slot0_<k>; b = slot1_<k>; c = slot2_<k>;  (discard `_` slots are dropped) */
```

- each `tk_await_spawn_arm_<k>_i` is the S3-style synthesized trampoline: pack+`marshal` the arm's args, `tk_coro_make`
  the arm, enqueue it on the scheduler; the arm body runs `fi()` inside the compiler-generated `on canceled`, then
  `marshal`s the result into `*slot` + `tk_waitgroup_done(wg)`.
- `tk_await_join_<k>` is a thin call over `tk_coro_yield` + the waitgroup-0 wake: it yields the parent and returns
  once the scheduler re-queues+resumes it (the waitgroup reaching 0 fires the wake). **Backend-agnostic:** it is a
  runtime call, no C `switch`/label state-machine, no `setjmp`.
- the `Intent<Ti>::new` calls and the `marshal<Ti>` calls are ordinary emitted calls — the native leg emits the same
  once the arm-spawn + coro externs are lowered; until then the native leg honest-stops `await` at `lower_stmt`
  (sound — the corpus never awaits, so the compiler's own native build is unaffected; a user program that awaits
  builds on the C leg — the S3 precedent, `plano-s10-spawn-batch-detalhe.md` S3 intro).

### A4.3 — residence (why the slot fill is UAF-safe)

The N slots live in the PARENT's arena and the parent is SUSPENDED (yielded, not returned) across the arms' run — so
its arena is live the whole time. Each arm `marshal`s its result INTO the parent's slot (a copy across the arm-arena
→ parent-arena boundary; the arm's own arena then dies at `tk_coro_finish`). The Intent instance the awaiter reads is
the SAME instance the arm filled (§10.3 L910 — no copy between fill and read), because the fill writes the parent's
slot directly. `ref` cannot cross (A3 ref-guard), so no arm result aliases the arm's dying arena.

### A4.4 — dispatch wiring + fixtures

- **wiring:** `emit_stmt_dispatch` (`codegen.tks:10233`) gains an `awaited`-guarded branch on `TBinding`/`TMultiBind`
  /`TAssign` → `emit_await(...)`; the un-awaited path is byte-identical to today.
- **fixtures:**
  - `await_single` (regression, C backend): `await var a = calc(41)`; assert `a.value == 42`, `!a.canceled` →
    exit **0**; a lost/corrupt value → **1**.
  - `await_when_all` (regression): `await var a, b, c = f1(), f2(), f3()` with three arms returning distinct values;
    assert all three Intents filled with the right values in any completion order → exit **0**; a missing/duplicated
    fill → **1**. (The when-all mechanic, §10.3 L922.)
  - `await_discard` (regression): `await _ = side_effecting()`; assert the effect happened (execution GUARANTEED by
    suspension, §10.3 L929) with NO Intent materialized → exit **0**.
  - `await_arm_cancel` (regression, pairs with CN1): one arm calls `cancel(reason)`; assert THAT arm's Intent has
    `.canceled == true`, `.failure == reason`, the OTHER arms' Intents are normal → exit **0**.

---

## CN1 — `cancel(reason)` + `on canceled as c { }` (parse + check + lowering) — `[C]` + `[C-rt]`

**Crumb id: CN1.** **Files:** `src/parser/*.tks` (the postfix `on canceled` + the `cancel` head), `src/checker/*.tks`
(check the construct + the carried-error binding), `src/codegen/codegen.tks` (the catch-frame lowering), and
`teko_rt.{c,h}` (the cancel-raise primitive `tk_cancel_*` riding the coroutine finish path). The owner's SETTLED
semantics (design to these EXACTLY):

- **`<expr> on canceled as c { <handler> }`** — a general developer construct AND the form the await lowering emits
  per arm. POSTFIX on an expression. On a cancel-unwind during `expr`, the handler runs; `c` binds the **error
  carried by the cancel**; the block's final expression is `expr`'s fallback value (with-value form). Owner examples:
  ```
  intent.value = user_func() on canceled as c { intent.canceled = true; intent.failure = c; null }
  user_func() on canceled as c { intent.canceled = true; intent.failure = c }
  ```
- **`cancel(reason)`** triggers a **non-local unwind that KILLS ARENAS** from the cancel point up to the nearest
  `on canceled` — **reusing the panic/exit arena-teardown machinery** plus a **catch frame** at `on canceled`.
  Backend-agnostic — NOT a `setjmp`-in-C trick.
- **uncaught → keeps unwinding to the root**; the runtime true-start wraps `main` in an `on canceled` that CONVERTS
  the uncaught cancel to a `panic` → whole program down. "uncaught cancel = panic" is realized by that root handler.
- **`cancel()` outside any suspension** = immediate `panic` (no coroutine to unwind, §10.3 L916).

### CN1.1 — how the non-local unwind is realized WITHOUT setjmp (the key design)

Panic today does NOT frame-unwind — it calls `tk_regions_free_all()` then `abort()` (`teko_rt.c:3485-3499`). The
"arena-teardown machinery" cancel reuses is (a) the per-scope `tk_region_drop(region)` emission the codegen already
does at scope exits / returns / break / continue (`codegen.tks:6156`/`:8283`/`:9205`), and (b) `tk_task_end`'s
wholesale sub-root free (`teko_rt.c:2510`). The backend-agnostic, no-setjmp mechanism uses the STACKFUL COROUTINE we
already have:

> **`on canceled` establishes a CATCH FRAME by running its guarded `expr` on a CHILD coroutine with its own sub-root
> arena. `cancel(reason)` calls `tk_cancel_raise(reason)`, which records the reason on the current coroutine and
> `tk_coro_finish(self, catch_sched, status=1)` — a stack-switch back to the nearest catch frame's context, whose
> `tk_task_end` frees the child coroutine's ENTIRE arena subtree in one shot (every arena between the cancel point
> and the catch is a sub-root under that child's task — "KILLS ARENAS from the cancel point up"). The catch frame,
> on regaining control with status=1, runs the handler with `c` bound to the recorded reason.**

This is exactly "reuse the arena-teardown (tk_task_end) + a catch frame", is a runtime CALL sequence
(`tk_cancel_raise`/`tk_coro_finish`/`tk_coro_switch`) both legs emit, and is NOT setjmp. Nested `on canceled` = nested
child coroutines; cancel reaches the NEAREST because the innermost enclosing catch frame is the current coroutine's
finish target. **Recorded limitation (risk R3):** abandoning the child coroutine's OS stack does not run
intervening frames' `defer` blocks — only their ARENAS are reclaimed (which is the whole point of "kills arenas").
For v1, cancel-unwind is arena-teardown-complete but defer-blind between the cancel point and the catch; running
intervening defers on cancel is a later refinement (a runtime cleanup-chain registered per scope), reported as a
scope line, NOT a new issue.

### CN1.2 — `tk_cancel_*` primitives (add to `teko_rt.{c,h}`, `[C-rt]`)

```c
/* tk_cancel_raise — the cancel trigger (§10.3). If a coroutine is current (tk_coro_current() != NULL — i.e. under
 * a suspension / inside an on-canceled-guarded child coroutine), records `reason` on it and tk_coro_finish's it
 * with status=1: a stack-switch back to the nearest catch frame, whose tk_task_end frees this coroutine's arena
 * subtree. If NO coroutine is current (cancel outside any suspension, §10.3 L916), it is an immediate tk_panic —
 * there is nothing to unwind. Does not return in the coroutine case (control resumes at the catch frame). */
_Noreturn void tk_cancel_raise(tk_error reason);

/* tk_cancel_reason — read the error a canceled coroutine carried (valid at the catch frame after a status=1
 * finish); the codegen binds it to the `c` of `on canceled as c`. */
tk_error tk_cancel_reason(tk_coro *co);
```

### CN1.3 — parser (the postfix `on canceled` + the `cancel` head)

- **`on canceled as c { … }`** is a POSTFIX operator on an expression — a new low-precedence postfix in the
  expression grammar (`src/parser/parse_expr.tks`), producing an `OnCanceled` node:
  ```
  /**
   * OnCanceled — a `<expr> on canceled as <c> { <handler> }` postfix (§10 cancel): run `expr`; if a cancel unwinds
   * it, run `handler` with `c` bound to the carried error, and the whole form yields `expr`'s fallback (the
   * handler's final expression). Also emitted by the await lowering per arm (§A4). Contextual — `on`/`canceled` are
   * recognized only in this postfix position; identifiers named `on`/`canceled` stay legal elsewhere.
   *
   * @field guarded  the guarded expression
   * @field cbind    the name bound to the carried error in the handler
   * @field handler  the handler block (its final expression is the fallback value)
   * @since 0.3.1 §10 CN1
   */
  pub type OnCanceled = struct { guarded: Expr; cbind: str; handler: []@Statement() }
  ```
  Recognized by `is_on_canceled_tail` after a full expression: `Ident("on")` `Ident("canceled")` `as` Ident `{`.
- **`cancel(reason)`** is an ordinary CALL to a compiler-known intrinsic (`cancel` is a contextual head like
  `spawn`, §10.1 L677) — recognized as `Ident("cancel")` followed by `(`; lowered to `tk_cancel_raise`. Zero corpus
  collisions (leaf invariant).

### CN1.4 — checker

- `type_on_canceled`: type `guarded` (its type T is the form's value type); the handler block is typed with `cbind`
  bound to `error`; the handler's final expression must be assignable to T (the with-value fallback) — or, in the
  statement form (`user_func() on canceled as c { … }` with no captured value), the fallback is discarded.
- `cancel(reason)`: `reason` must be an `error`; the call types as the never/`Null` type (it diverges).
- add `OnCanceled` to `@Expr()` and its `TOnCanceled` twin to `@TExpr()`, with arms in every expr walker (mechanical,
  exhaustiveness-forced — the S2 walker-arm discipline).

### CN1.5 — lowering (the catch frame) + the root-start wrapper

- `emit_on_canceled`: lower `<expr> on canceled as c { h }` to: `tk_coro_make` a child coroutine running `expr` on
  its own sub-root arena, enqueue+switch into it; on its finish read `tk_coro_status` — status 0 → the form's value
  is `expr`'s value (marshalled out of the child arena into the parent slot); status 1 → bind `c =
  tk_cancel_reason(child)`, run `h`, the form's value is `h`'s fallback. The child's arena is freed by
  `tk_coro_finish`'s `tk_task_end`. Backend-agnostic runtime calls; native honest-stops until its coro externs land.
- **the root-start wrapper:** the runtime true-start (the entry that calls user `main`, the codegen'd `main` prologue
  / `tk_coro_root` bootstrap) wraps the `main` call in an implicit `on canceled as c { tk_panic_str(c.message) }`
  (an uncaught cancel that unwinds to the root converts to a panic → the whole program down, §10.3 L915). This is
  ONE synthesized catch frame at the root; realizes "uncaught cancel = panic".

### CN1.6 — fixtures

- `cancel_caught` (regression): `var x = risky() on canceled as c { 7 }` where `risky()` calls `cancel(err)`; assert
  `x == 7` (the fallback) and the arena is balanced (paranoid) → exit **0**.
- `cancel_uncaught_panics` (regression, must-abort): `cancel(err)` with no enclosing `on canceled` under a
  suspension → unwinds to the root wrapper → panic (nonzero/abort exit); the fixture asserts the panic marker.
- `cancel_outside_suspension_panics` (regression, must-abort): `cancel(err)` at top level with NO coroutine current
  → immediate `tk_panic` (§10.3 L916); assert the panic marker.
- `cancel_arena_paranoid` (regression under `TEKO_MEM_PARANOID=1`): a deep call chain cancels; assert
  `tk_names_live_count` returns to baseline (the child coroutine's whole arena subtree freed by `tk_task_end`) →
  exit **42**; drift → **1**.

---

## `marshal<T>` — INTERFACE ONLY (internals are the owner-deferred §5 ruling)

`marshal<T>` is the black-box primitive that deep-copies/serializes a `T` across an arena/thread/coroutine boundary.
This doc pins its SIGNATURE + CALL SITES only; its INTERNALS (POD-first via monomorph-`sizeof` vs
recursive-deep-copy/serialize) are the **owner-deferred §5 marshalling ruling** (`docs/design/marshall-spec.md` — the
`ptr↔ref` half is ratified; the value deep-copy half for `T` across a boundary is the open ruling) — OUT OF SCOPE
here. The await design works with it as a black box.

```
/**
 * marshal — deep-copy/serialize a value of `T` ACROSS an arena/thread/coroutine boundary into `dst` (a caller-owned
 * cell in the DESTINATION arena), so `dst` shares no pointer with the source arena. The copy IS the isolation
 * (§10.6): a fat `T` (str/slice/struct-with-interior-pointers) has its transitive bytes copied; a POD `T` is a
 * bitwise move. INTERFACE ONLY here — the internals (POD-first monomorph-`sizeof` vs recursive serialize) are the
 * owner-deferred §5 marshalling ruling; this signature lets the await/spawn/channel lowerings call it as a black
 * box that will be filled by §5.
 *
 * @param T    the value type crossing the boundary (never a `ref` — §10.6 forbids ref crossing)
 * @param src  the source value (in the source arena)
 * @param dst  the destination cell (in the destination arena) that receives the deep copy
 * @since 0.3.1 §5 (interface pinned by §10 await/spawn/channel; internals deferred to the §5 ruling)
 */
pub fn marshal<T>(src: T, dst: ref T)
```

**Call sites (all already implied by landed/here-designed lowerings):**
1. **await result-copy (A4):** each arm `marshal<Ti>`s its result out of the arm's dying arena into the parent's slot.
2. **await arg-copy (A4):** each arm's args are `marshal`led into the arm's ctx-blob (the S3 deep-copy generalized).
3. **spawn args (S3, landed):** the per-site `tk_spawn_pack_<k>` deep-copy IS a hand-rolled `marshal` — when §5
   lands, S3's packer becomes a `marshal<T>` call, removing the bespoke packer (a later, mechanical convergence,
   reported not built).
4. **channel `T` (C2/C3, landed scaffolding):** `MemChan<T>.send`/`recv` move `size_of<T>` bytes — the same
   `marshal` boundary (fat `T` across `OsChan` is the deferred length-prefixed frame, C3's honest-stop).
5. **`on canceled` value fallback (CN1):** the guarded expr's value is `marshal`led out of the child coroutine's
   arena into the parent slot.

Until §5 rules, the await/cancel lowerings use the SAME per-type deep-copy predicate S2 gates spawn args on
(scalars / `str` / `[]T` of copyable `T` / flat structs), and honest-stop the rest (class/closure/nested graph)
pointing at the §5 ruling — the identical gated-scope discipline S2 used. This keeps A4/CN1 shippable without §5.

---

## Ritual per crumb — reseed table (HEAD `deb76e5a`)

| Crumb | Kind | Reseed | Corpus byte-identical? |
|-------|------|--------|------------------------|
| **CO0** `tk_coro_*` (maintained-C, per-`#os`) | `[C-rt]` | **runtime rebuild only; NO fixpoint reseed** | YES — new C symbols, never emitted (the C0a/C0b argument) |
| **SCH1** scheduler (`teko::threads`) | `[L]` stdlib leaf | **additive leaf reseed** | YES — corpus never runs the scheduler |
| **A1** `Intent<T>`/`Intent` | `[L]` stdlib leaf | **additive leaf reseed** | YES — corpus never instantiates Intent |
| **A2** await parser (`awaited` flag) | `[C]` parser | **fixpoint + reseed** | YES — new grammar never exercised by `src/` |
| **A3** await checker (widen + ref-guard) | `[C]` checker | **fixpoint + reseed** | YES — new checker path never exercised |
| **A4** await lowering | `[C]` codegen | **fixpoint + reseed** | YES — no emitted `teko.c` calls `tk_coro_*`/`tk_await_*` until a user program awaits |
| **CN1** cancel + on-canceled (+ `tk_cancel_*`) | `[C]` + `[C-rt]` | **runtime rebuild + fixpoint + reseed** | YES — corpus never cancels |
| **RC1** full I/O reactor (io_uring/epoll/kqueue/IOCP) | `[C-rt]` §16/§17-adjacent | **runtime rebuild; LATER — out of scope** | YES (not built here) |

**Ritual points (full gate MUST pass):** after **A2**, after **A3**, after **A4**, after **CN1** (each is a
compiler-touching crumb → fixpoint byte-identity + full regression). CO0 gates via `coro_pingpong` under
`TEKO_MEM_PARANOID=1` (exit 42) + a clean runtime rebuild — no compiler fixpoint. SCH1/A1 gate via the additive leaf
reseed + their fixtures. **NEVER run `teko test .` (OOM)** — gate via the sharded/regression runners.

---

## Ordered sequence + the FIRST implementable crumb

Dependency spine: **CO0** (runtime coro) → **SCH1** (scheduler over CO0) · **A1** (Intent, needs nothing) → **A2**
(parser) → **A3** (checker, needs A1's Intent + A2) → **A4** (lowering, needs CO0 + SCH1 + A1 + A3 + the landed
spawn/waitgroup) → **CN1** (cancel, needs CO0 + A4's on-canceled emission).

Ordered, each independently gate-able:
1. **CO0** `tk_coro_*` (maintained-C, per-`#os` ucontext/Fibers) + `coro_pingpong` probe. Runtime rebuild, no reseed.
   **← FIRST implementable crumb** (fully unblocked, smallest, no surface, drives the whole spine).
2. **SCH1** `teko::threads` scheduler (ready-queue + run-loop over CO0; v1 minimal reactor = futex/condvar). Leaf.
3. **A1** `Intent<T>`/`Intent` protected structs + `intent_stamp` fixture. Leaf.
4. **A2** await-prefix parser (`awaited` flag on Binding/MultiBind/Assign) + `await_parse`/`await_not_binding`.
   Fixpoint + reseed.
5. **A3** await checker (widen `T`→`Intent<T>`, ref-guard, arity, marshal-copyable) + widen/reject fixtures.
   Fixpoint + reseed.
6. **A4** await lowering (context + N arms + coro yield + waitgroup-0 join + Intent read) + when-all/discard/cancel
   fixtures. Fixpoint + reseed. **The heart.**
7. **CN1** `cancel(reason)` + `on canceled as c {}` (parse + check + catch-frame lowering + `tk_cancel_*` + the
   root-start panic wrapper) + the caught/uncaught/outside/paranoid fixtures. Runtime rebuild + fixpoint + reseed.

**First implementable crumb: CO0 (`tk_coro_*`)** — maintained-C, per-`#os` ucontext/Fibers, zero Teko surface, no
reseed; `coro_pingpong` under `TEKO_MEM_PARANOID=1` gates it to exit **42**.

---

## §16 / §17 dependencies (recorded, not on this issue's critical path)

- **`tk_coro_*` stack-switch** — ucontext (POSIX) / Fibers (Windows) are §16/§17-**TRANSITIONAL** (like
  `pthread_create`, `teko_rt.h:224`). At §16→§17 they become direct OS-FFI (or a custom-asm switch that skips
  ucontext's `sigprocmask`). Zero surface impact; the `tk_coro_*` doc-comments carry the §16/§17 note.
- **`tk_cancel_*`** — rides `tk_coro_finish`; same §16/§17 boundary.
- **RC1 full I/O reactor** — io_uring/epoll/kqueue/IOCP via FFI, per-`#os`, is §16/§17-adjacent (more OS-FFI in
  `teko_rt.c`). Needed ONLY when `await` drives OS I/O directly; NOT required for v1 correctness (v1 = compute +
  channel/waitgroup coordination). Explicitly deferred.
- **user-pluggable transports** (Kafka/Rabbit/WS) — the dynamic-FFI extension (§10.2 L809), §16→§17-gated, unrelated
  to the await model.

---

## Risks + law tensions (recommended resolutions)

- **R1 — the await lowering must be backend-agnostic, but the native leg cannot emit `tk_coro_*` yet.** RESOLUTION:
  the await lowering IS a runtime-CALL sequence (coro make/switch/finish, waitgroup, marshal) — a shape BOTH legs can
  emit; the C leg ships it now, the native leg honest-stops `await` at `lower_stmt` until its coro externs land
  (sound — the corpus never awaits, so the compiler's own native build is unaffected; a user program that awaits
  builds on the C leg — the S3 precedent). No state-machine, no setjmp. Not a law tension — a per-crumb scope line.
- **R2 — cancel must be a NON-LOCAL unwind, backend-agnostic, no setjmp.** RESOLUTION: realized via the stackful
  coroutine (`on canceled` = a child-coroutine catch frame; `cancel` = `tk_cancel_raise` → `tk_coro_finish` back to
  the nearest catch; `tk_task_end` frees the arena subtree — "kills arenas from the cancel point up"). Reuses the
  arena-teardown machinery + a catch frame exactly as the owner specified; is a runtime-call sequence both legs emit;
  is NOT setjmp. Resolved.
- **R3 — cancel-unwind is arena-teardown-complete but `defer`-BLIND between the cancel point and the catch.**
  Abandoning the child coroutine's OS stack reclaims every intervening arena (the point of "kills arenas") but does
  not run intervening `defer` blocks. RESOLUTION: acceptable for v1 (arena free IS the dominant cleanup in a no-GC
  arena model); running intervening defers on cancel is a later refinement (a per-scope runtime cleanup-chain), a
  gated scope line — reported, NOT a new issue.
- **R4 — `marshal<T>` internals are the owner-deferred §5 ruling.** RESOLUTION: this doc pins the marshal INTERFACE +
  call sites only; the await/cancel lowerings use the SAME v1 deep-copy predicate S2 gates spawn args on (scalars/
  str/[]T/flat structs) and honest-stop the rest pointing at §5 — the identical gated-scope discipline. A4/CN1 ship
  without §5; when §5 rules, the bespoke packers converge to `marshal<T>` calls (mechanical, reported). Not a law
  tension.
- **R5 — v1 has no I/O reactor (futex/condvar only).** RESOLUTION: v1 scope is compute + channel/waitgroup
  coordination; the awaiting coroutine YIELDS (never blocks its OS thread) while arms run on their own threads, so
  §10.3's "cede, nunca bloqueia" for the awaiter holds. Direct-`await`-on-OS-I/O is the LATER RC1 reactor — an
  explicit, scoped deferral. Not a tension.
- **R6 — `ucontext`'s `sigprocmask` overhead.** RESOLUTION: acceptable for v1; a custom-asm switch is a later
  optimization INSIDE `teko_rt.c` (zero surface impact). Recorded in the CO0 doc-comment. Not a tension.
- **R7 — Teko-only law vs the maintained-C `tk_coro_*`/`tk_cancel_*`.** RESOLUTION: `teko_rt.{c,h}` is the
  Teko-only-law EXEMPT maintained runtime (the same exemption `tk_thread_spawn`/`tk_memchan_*` ride); the arena
  bracket (`tk_task_begin`/`tk_task_end`) and the stack-switch cannot be expressed in Teko today (the same reason
  D1 forced `tk_thread_spawn` into C, `plano-s10-concorrencia-crumbs.md` L16-25). No tension.

**No unresolved tension — nothing HALTs in this batch.** The owner has SETTLED both the model (option c) and the
cancel semantics; every remaining fork (marshal internals §5, the full reactor RC1, defer-on-cancel) is an
explicitly-scoped later crumb, reported not spun into a new issue.

---

## Final anchors (file:line on HEAD `deb76e5a`)

- **Runtime (teko_rt.c):** `:2499` `tk_task_begin`, `:2510` `tk_task_end`, `:1349` `struct tk_task`
  (`_Thread_local tk_g_current_task`), `:2177` `tk_region_program` (F2), `:2545` `tk_thread_start` (the arena
  bracket the coro trampoline mirrors), `:2554` `tk_thread_spawn`, `:2758-2789` `tk_waitgroup_make/add/done/wait`
  (+`_u` binders — the v1 join primitive), `:2605+` `tk_memchan_*`, `:2840+` `tk_oschan_*`, `:2989-3005` the
  `tk_oschan_*` Windows honest-stop (the per-`#os` split pattern `tk_coro_*` copies), `:3485-3499` `tk_panic`
  (arena-teardown + abort — the machinery cancel reuses), `:3528-3531` `tk_exit`, `:9`/`:144`/`:292`/`:1265`
  `#if defined(_WIN32)` splits. Header `teko_rt.h:213-238` task/thread decls (where `tk_coro_*`/`tk_cancel_*`
  signatures land), `:243-259` `tk_memchan_*`.
- **Parser:** `parse_stmt.tks:117` `is_binding_head` dispatch (insert `is_await_head` before it), `:349`
  `parse_binding`, `:383` `parse_multibind`, `:642` `is_binding_head`; `ast.tks:340` `Binding` (add `awaited`),
  `:363` `Assign` (add `awaited`), `:521` `MultiBind` (add `awaited`), `:554` `@Statement()` union; the `Spawn`
  contextual-head precedent at `parse_stmt.tks` (S1, spawn batch).
- **Checker:** `typer.tks:6496` `type_binding_value`, `:6558` `type_binding`, `:7141` `type_multibind_mret`, `:7225`
  `type_multibind_parallel`, `:7275` `type_multibind_array`; the spawn ref-guard `tspawn_reject_ref_params`
  (`plano-s10-spawn-batch-detalhe.md` S2.2) reused per arm.
- **Codegen/LIR:** `codegen.tks:10233` `emit_stmt_dispatch` (add the `awaited` branch), `:6156`/`:8283`/`:9205`
  the `tk_region_drop` scope-teardown emission (the arena-teardown machinery), `:6290` the lifted top-level section
  (where arm trampolines/`Intent` factories append); `lower.tks:1853` the native honest-stop fall-through
  (`await`/`on canceled`/`cancel` native-stop site).
- **Stdlib scaffolding:** `src/threads/threads.tks` — `IChannelKind<T>` (`:12`), `Closed` (`:44`), `Rx<T>` (`:57`),
  `Tx<T>` (`:85`), `Ctx` (`:126`); the new `Intent`/`Intent<T>` (A1) and `Scheduler` (SCH1) land beside them.
- **Spec:** `mudancas-superficie-0.3.1.md` §10.3 L842-943 (await/Intent/cancel), L910 (same-instance fill), L916
  (cancel outside suspension = panic), L925-931 (when-all + discard), §10.6 L1041-1047 (ref UAF ruling).
- **Marshal:** `docs/design/marshall-spec.md` (§6 namespace form, the `ptr↔ref` ratified half; the value deep-copy
  half for `T` across a boundary = the open §5 ruling `marshal<T>` fills).
- **Prior §10 batches:** `plano-s10-spawn-batch-detalhe.md` (S3 ctx-blob/trampoline/deep-copy the arm-spawn reuses),
  `plano-s10-channels-batch-detalhe.md` (waitgroup + F2 residence + the reseed argument), `plano-s10-concorrencia-
  crumbs.md` (the D2 fork this doc's option (c) ruling settles).
</content>
