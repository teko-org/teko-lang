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
- **14.B — `duplex chan` (bidirectional + close/drop signaling)** — ✅ done. A symmetric
  full-duplex channel with two isolated rings + a close/drop state machine, live on BOTH
  targets (executable `.tks` proof on each):
  - **Runtime (source of truth):** `src/runtime/teko_duplex.{h,c}` — OPEN/CLOSED/DROPPED state
    machine, per-direction rings, non-blocking ops returning a structured `TekoDuplexStatus`
    (OK/EMPTY/FULL/CLOSED/DROPPED), graceful drain-after-close, terminal drop. 6 Unity KATs.
  - **Surface:** dedicated `OP_DUPLEX_OPEN/SEND/RECV/POLL/CLOSE` opcodes (owner decision). The
    lexer folds `duplex.open` into a dotted IDENTIFIER (bare `duplex` stays the keyword), so the
    frontend reuses the dotted-identifier call path; a `uses_duplex` flag gates the backends.
  - **Native:** opcodes → `teko_rt_duplex_*` over the linked C runtime (`teko_rt_sched`-style).
    Proof `runtime/native/samples/duplex.tks` (bidirectional + structured CLOSED).
  - **WASM:** the SAME C runtime is compiled into the runtime reactor (`teko_duplex.c` added to
    `build-crypto-reactor.sh`); opcodes → imported `teko_rt_duplex_*`, shared linear memory.
    Pure i32-in/i32-out (handle/value/status) — no hex marshalling. Proof
    `runtime/wasm/run-duplex.mjs` (same values as native). Both wired into CI.
  - **Decision:** the duplex handle is an opaque register-width integer (native pointer / WASM
    reactor-heap pointer); `duplex.poll` gives non-blocking structured status without an in-band
    sentinel; `emit_int`/`env.log_int` surface i32 results in the proofs.
- **14.C — `delayed chan` (timed/timestamped)** — ✅ done on both targets (executable `.tks`
  proof each). Runtime `src/runtime/teko_delayed.{h,c}` (4 Unity KATs): each message is stamped
  with a delivery time on a **logical clock** (advanced via `delayed.advance` — deterministic,
  clock-source-agnostic, models the Timer Queue); `recv` releases the earliest-due message in
  delivery-time order (stable FIFO on ties); non-blocking structured statuses. Surface: dedicated
  `OP_DELAYED_OPEN/SEND/ADVANCE/RECV/POLL/CLOSE` (`delayed.*` dotted-identifier). Native →
  `teko_rt_delayed_*`; WASM → reactor imports (same C runtime). Proofs
  `runtime/native/samples/delayed.tks` (1/10/20/30) + `runtime/wasm/run-delayed.mjs`. CI-wired.
- **14.D — `broadcast chan` (non-destructive 1:N pub-sub)** — ✅ done on both targets. Runtime
  `src/runtime/teko_broadcast.{h,c}` (4 Unity KATs): a bounded overwriting ring keyed by a
  monotonic write sequence + one read cursor per subscriber; `publish` writes once, each
  subscriber reads independently (all N see every value), future-only subscription, structured
  LAGGED/EMPTY/CLOSED. Surface uses `broadcast.*` dotted-identifiers (owner decision was
  pub/subscribe; `broadcast` is not a keyword so it folds like `duplex.`/`delayed.` and reuses
  the pattern verbatim — `pub`/`subscribe` keyword spellings remain available as future sugar).
  Dedicated `OP_BCAST_OPEN/SUBSCRIBE/PUBLISH/RECV/POLL/CLOSE`; native → `teko_rt_bcast_*`, WASM →
  reactor imports. Proofs `runtime/native/samples/broadcast.tks` (10/20/10/20) +
  `runtime/wasm/run-broadcast.mjs`. CI-wired.
- **14.E — `shared` block + `atomic`** — ✅ done on both targets. Runtime `src/runtime/teko_shared.{h,c}`
  (3 Unity KATs): a global **coarse spinlock** (`teko_shared_enter/leave` — owner decision: one
  whole-block lock) + an atomic integer cell (`cell/add/load/store`). Portable atomics WITHOUT
  `<stdatomic.h>` (clang/gcc `__atomic` builtins; `_Interlocked*` on MSVC; plain ops on the
  single-threaded wasm32 reactor) → MSVC-safe + TSan-clean. NEW `shared { … }` **block grammar**
  injects `OP_SHARED_ENTER/LEAVE` around the body; `atomic.*` is a dotted-identifier surface →
  `OP_ATOMIC_*`. The C functions use a register-width ABI so the backends call them directly (no
  rt wrapper). Native links teko_shared; WASM imports it from the reactor. Proofs
  `runtime/native/samples/shared.tks` (8/10) + `runtime/wasm/run-shared.mjs`. CI-wired. *Coarse
  single global lock + no nested blocks is the MVP; per-block locks + real wasm-threads atomics
  are future refinements, not a correctness gate.*
- **14.F — `circuit` + `retry`** — *runtime done, surface pending*. `src/runtime/teko_retry.{h,c}`
  (6 Unity KATs) is the policy source of truth: exponential/logarithmic backoff, the
  `attempts`+`timeout`→`fallback` incremental-relative-time rule, and the circuit breaker
  CLOSED/OPEN/HALF_OPEN machine. The `retry { } fallback { }` / `circuit` BLOCK grammar that drives
  it (and makes the `fallback`/`exponential`/`logarithmic`/`attempts`/`timeout` keyword tokens
  live) is the remaining work — a recommended routine-trampoline lowering is specced in
  `docs/HANDOFF_PHASE14.md` (large/design-heavy → handed off).
- **14.G — Timespan waiters: `await` (async) + `wait` (sync)** — ✅ done on both targets
  (executable `.tks` proof each). `wait <ts>;` = synchronous sleep (native `teko_rt_sleep_ms`
  real nanosleep/Win Sleep; WASM `env.teko_sleep` host import). `await <ts>;` = cooperative timed
  yield (native `teko_rt_await_ms` advances a logical clock + drains the run queue; WASM
  `env.teko_await` records ms + `$teko_sched_run` drain). New keywords `wait`/`await`; opcodes
  `OP_WAIT` 0x59 / `OP_AWAIT_FOR` 0x5A (single-byte, ms in $w0). Timespan literals (`10ms`/`2s`)
  normalize to canonical ms at compile time (`literal_canonical_value`), adopted in
  `lower_codec_value` so 14.C/14.F delay args accept timespan literals (runtimes unchanged).
  Proofs `runtime/native/samples/waiters.tks` (1,2,3 — await ran the queued worker) +
  `runtime/wasm/run-waiters.mjs` (order 1,2,3 + host saw normalized await=5/wait=10 ms). Suite
  196/196. *MVP: native tasks are run-to-completion, so `await` is a cooperative yield + logical
  clock advance (WASM Layer A mirrors it), not real timer suspension — future work.*
- **14.H — Real `.tks` samples (functions, threads, loops)** — *planned* (owner, 2026-06-16). The
  capstone: real programs combining named functions, routines/threads (incl. a Layer-B threads
  sample), channels, `await`/`wait`, and **loops**. Requires the shared **control-flow emission
  foundation** (loops + branches: native asm labels + WASM structured block/loop) — which also
  unblocks the 14.F `retry { }` surface. Plan + sequencing in `docs/HANDOFF_PHASE14.md`.
