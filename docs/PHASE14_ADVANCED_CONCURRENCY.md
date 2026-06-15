# Phase 14 — Advanced Concurrency, Signaling & Duplex Channels (design & plan)

> Feature branch: `feat/phase-14-advanced-concurrency` (PR pending). Builds on the
> **Phase 8 native M:N green-thread scheduler + channels** (`src/runtime/teko_thread.c`,
> `src/runtime/teko_channel.c`) and the **Phase 10 WASM concurrency backend**
> (`docs/PHASE10_WASM_CONCURRENCY.md`: Layer A cooperative + Layer B threads). Roadmap:
> `docs/plan.md` → Phase 14.

## 1. Goal

Deliver the advanced concurrency surface whose keyword tokens were reserved in Phase 12
(`routines`, `duplex`, `delayed`, `pub`/`subscribe`, `shared`, `atomic`, `circuit`,
`fallback`, `retry`, `exponential`, `logarithmic`, `attempts`, `timeout`) — as **functional,
executable** language features on BOTH the native runner and WASM (no dead tokens). Every
surface ships with an executable `.tks` proof (native via `run-native.sh`; WASM via a
`run-*.mjs` harness), exactly as the Phase 13 crypto surface did.

## 2. Architecture — two substrates, one semantic model (inherited)

The Phase 13 split is the template and continues to apply:

- **Native:** primitives live as **portable C23 in the embedded runtime** (`src/runtime/`),
  unit-tested in the Unity suite, and lowered from IL via `OP_CALL_RUNTIME` →
  `teko_rt_*` wrappers (`runtime/native/teko_rt.c`, dispatch in
  `src/codegen/emit_native_hosted.c`). This is the single source of truth for semantics.
- **WASM:** the scheduler/channels are **emitted WAT** in `src/codegen/bare_metal/emit_wasm.c`
  (Layer A run queue + ring-buffer channels + `$teko_sched_run`; Layer B atomics + Workers).
  Per the Phase 10 decision, the native `teko_thread.c` scheduler is a *simulated* scheduler
  and is **not** directly reusable in-module — the WASM scheduler is a separate emitted
  artifact. Phase 14 extends both consistently.

New IL opcodes use free slots `0x23–0x2F`, `0x35–0x3F`, `0x42–0xFD`. New `OP_CALL_RUNTIME`
ids continue the Phase 13 table (next free id block: **50+**, leaving 44–49 as headroom).

## 3. Sub-blocks — scope, sizing, dependency order

Ordered dependency-first. Each sub-block is a reviewed mini-project closed with a report.

### 14.0 — Scaffolding (this increment) — *bounded*
This design doc; branch + draft PR; baseline goldens/suite pinned green (167/167). No
behavior change.

### 14.A — `routines` (background tasks) — *bounded, START HERE*
Fire-and-forget background tasks at the runtime level. Builds **directly** on the existing
`OP_SPAWN_ASYNC` + `OP_FUNC_BEGIN/END` + scheduler — the smallest functional increment and
the one that exercises the full frontend→IL→both-backends path end to end.
- **Frontend:** grammar for `routines { … }` / `routines name(args) { … }`; lower the routine
  body to a table-indexed function (`OP_FUNC_BEGIN/END`) and emit `OP_SPAWN_ASYNC` at the
  fire site. (The `parser_concurrent.c` scaffolding parses channel/await today but does **not**
  yet lower to IL — wiring that path through is part of this block, analogous to the Phase 13
  native-runner FE wiring.)
- **Native:** `teko_rt_spawn`/`teko_rt_join`-style wrappers over `tld_thread_spawn` +
  `tld_scheduler_run`.
- **WASM:** enqueue `{fn_index,arg}` into the existing run queue (Layer A) / `teko_spawn`
  host delegate (Layer B) — already emitted.
- **Proof:** `routines.tks` spawns N background tasks that each push to a channel; main drains
  and asserts the sum. Native + WASM.

### 14.B — `duplex chan` (symmetric bidirectional + close/drop signaling) — *medium*
A channel carrying **two isolated rings** (RX + TX) plus an internal **state machine**
(`OPEN → CLOSED | DROPPED`). `.close()` signals a legitimate close; a producer/consumer drop
(panic) transitions to `DROPPED`, **unblocks and wakes** any thread parked on the channel, and
returns a **structured error** (not a hang). New runtime type `TekoDuplexChannel`; new opcodes
`OP_DUPLEX_INIT/SEND/RECV/CLOSE` (or a `duplex` flag on the channel ops). Native runtime +
WASM emit. Surface: `dx := duplex chan<i32>`; `dx.send(v)` / `dx.recv()` / `dx.close()`.
- **Proof:** bidirectional ping-pong; then a `.close()` mid-stream and an asserted structured
  error on the next `recv` instead of a deadlock.

### 14.C — `delayed chan` (timed/timestamped) — *medium; needs a clock source*
Messages are stamped with an enqueue timestamp; a consumer is **suspended on a timer queue**
and woken when its delay elapses. Needs a monotonic clock: native reuses the portable Unix-ms
source already added for `teko_rt_uuid_v7` (MSVC-safe); WASM imports `env.teko_now_ms` (host)
or an in-module monotonic counter for the cooperative engine. Timer queue is a min-by-deadline
list the scheduler drains before idling.
- **Proof:** schedule three messages with staggered delays; assert delivery order + that the
  observed gap is ≥ the requested delay.

### 14.D — `broadcast chan` (non-destructive 1:N pub-sub) — *medium*
Register-based pub-sub: a publish writes **once**; each subscriber holds its **own read
cursor** and reads non-destructively (no consume races). Surface uses the already-reserved
`pub` / `subscribe` tokens (see §5 — there is no `broadcast` keyword token; **decision point:
reuse `pub`/`subscribe` vs. add a `broadcast` keyword**). New runtime type
`TekoBroadcastChannel` (slot buffer + per-subscriber cursors). Native + WASM.
- **Proof:** one publisher, three subscribers; assert all three observe every published value.

### 14.E — Automated shared memory (`shared` block + `atomic`) — *medium-large; possible fork*
A `shared { … }` block whose enclosed state the compiler treats as concurrently accessed,
**transparently injecting lightweight locks (spinlocks) / memory fences**; `atomic`
qualifies an individual op to a fence-guarded atomic RMW. Native: real `<stdatomic.h>` /
spinlock primitives. WASM: Layer B atomics (`i32.atomic.*`, already emitted) for the threaded
target; Layer A is single-threaded so the injected lock is a no-op/fence. **This is the
deepest block** — the transparent lock-injection / minimal-critical-section analysis is the
risk. *Will report a scoped sub-plan before starting and flag any genuine fork* (e.g.
whole-block lock vs. per-field; escape analysis depth).

### 14.F — `circuit` breaker + `retry`/backoff/`fallback` — *medium; shares the clock*
Resilience control flow, largely independent of channels. `retry { … } fallback { … }` with
`attempts` and/or global `timeout` limits and `exponential`/`logarithmic` backoff between
attempts; `circuit` wraps a call with open/half-open/closed breaker state. When both
`attempts` and `timeout` are given, the compiler computes the incremental relative retry time
and **branches straight to `fallback`** once the projected next-attempt delay would exceed the
time budget (per `plan.md`). Backoff delay uses the 14.C clock source. Runtime helper computes
the schedule; the branch/loop structure is emitted IL.
- **Proof:** a flaky call that fails K times then succeeds (assert it retries with growing
  backoff and returns the success); a permanently-failing call that exhausts `attempts`/
  `timeout` and lands in `fallback`.

### Dependency graph
```
14.0 ─▶ 14.A ─▶ 14.B
                 └▶ 14.C ─▶ 14.F
        14.A ────▶ 14.D
        14.A ────▶ 14.E   (independent of B/C/D; deepest, may fork)
```
**Bounded:** 14.0, 14.A.  **Medium:** 14.B, 14.C, 14.D, 14.F.  **Large / fork-risk:** 14.E.

## 4. Discipline (every increment)
One increment per commit; build Release + run the Unity suite; **ASan + UBSan on BOTH VM
dispatch paths + TSan clean** (TSan especially matters here — this phase introduces real
concurrency/shared-state code paths); the **16 native emitter goldens never regress**; all
four CI gates (`native`/`wasm`/`wasm-threads`/`sanitizers`) green before any block is "done";
**no dead tokens** (executable `.tks` proof per surface); docs + CLAUDE.md updated; **no
merge/force-push** (the human merges).

## 5. Open decisions / forks to confirm
1. **`broadcast` spelling (14.D):** no `broadcast` keyword token exists — only `pub`
   (`TOKEN_PUB`) and `subscribe` (`TOKEN_SUBSCRIBE`). Recommendation: build the broadcast
   surface on `pub`/`subscribe` (no lexer change) unless the owner wants a literal
   `broadcast chan` spelling (a one-line keyword addition).
2. **Shared-memory lock granularity (14.E):** whole-`shared`-block lock (simple, correct,
   coarser) vs. per-field/per-access fences (finer, more analysis). Recommend start coarse,
   refine if needed — will sub-plan before coding.
3. **Duplex opcode encoding (14.B):** dedicated `OP_DUPLEX_*` opcodes vs. a `duplex` flag on
   the existing `OP_CHAN_*`. Recommend dedicated opcodes (clearer goldens, free slots ample).

## 6. Status
- **14.0** — ✅ done (this doc + branch + draft PR #7 + pinned green baseline 167/167).
- **14.A — `routines` (background tasks)** — ✅ done. `routines { foo(); bar(); }` fires each
  enclosed call as a cooperative background task on BOTH targets, with an executable `.tks`
  proof on each (no dead token):
  - **IL:** `OP_SPAWN_ASYNC` (0x10, reused) + `codegen_li_emit_spawn_async`; a `uses_spawn`
    flag gates all backend additions so spawn-free programs stay byte-identical.
  - **Frontend (`frontend_interop.c`):** the `routines { … }` block resolves each `NAME()` to
    a top-level `fn NAME`'s table slot → `ICONST slot; OP_SPAWN_ASYNC`. Routine/handler bodies
    now lower plain extern calls (e.g. `emit`) + codec/crypto calls, not only `@dom`, so a
    fired task can do real work.
  - **WASM (`emit_wasm.c`):** Layer A run queue (`$teko_enqueue`) reused; `call $teko_sched_run`
    is emitted at `$main` close when `uses_spawn`, draining fired routines before exit. Proof:
    `runtime/wasm/samples/routines.tks` + `run-routines.mjs` (order `[main start, main end,
    worker×3]` — deferred, not inline). Wired into `wasm.yml`.
  - **Native runner (`emit_native_hosted.c` + `runtime/native/teko_rt_sched.c`):** the hosted
    emitter became multi-function — `OP_FUNC_BEGIN/END` emit `teko_routine_<slot>` functions
    after `$main` (which now `ret`s before them), `OP_SPAWN_ASYNC` → `teko_rt_spawn`, a routine
    function-pointer table is emitted at EPILOG, and `teko_rt_run` drains at HALT — all gated by
    `uses_spawn`. The scheduler lives in a **separate TU** (`teko_rt_sched.c`) so the linker
    pulls it (and needs the table externs) ONLY for routines programs; crypto binaries are
    untouched (run-native.sh: all 13 crypto/uuid/hello proofs still green). Proof:
    `runtime/native/samples/routines.tks` in `run-native.sh`.
  - **Semantics (MVP):** run-to-completion cooperative tasks, drained at program exit (an
    implicit join-at-exit). Blocking/suspending rendezvous between routines is 14.B+ work.
- **14.B–14.F** — not started.
</content>
</invoke>
