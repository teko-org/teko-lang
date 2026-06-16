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
  with an absolute **ns DEADLINE on the REAL monotonic clock** (now + delay; the runtime stays
  clock-agnostic — `now_ns` is passed in, KAT-deterministic); `recv` releases the earliest-due
  message in deadline order (stable FIFO on ties, timing-robust); non-blocking structured statuses.
  Surface: dedicated `OP_DELAYED_OPEN/SEND/RECV/POLL/CLOSE` (`delayed.*` dotted-identifier; the
  former logical `delayed.advance` was removed in the real-time-clock correction). Native →
  `teko_rt_delayed_*` (read teko_rt_now_ns); WASM → reactor imports (read env.teko_now_ns). Proofs
  `runtime/native/samples/delayed.tks` + `runtime/wasm/run-delayed.mjs` (real-deadline order
  [10,30,20] + elapsed ≥ ~5ms). CI-wired.
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
- **14.F — `circuit` + `retry`** — ✅ done on both targets. The `teko_retry.{h,c}` policy runtime
  (6 KATs: exp/log backoff, attempts+timeout→fallback rule, CLOSED/OPEN/HALF_OPEN breaker) is the
  source of truth (14.F.1); the `retry (attempts N, [timeout T,] exponential|logarithmic, base B)
  { body } fallback { fb }` and `circuit cb { body } fallback { fb }` BLOCK grammar now drives it
  via the **control-flow foundation** (a should_continue-gated attempt loop; circuit_allow guard +
  circuit_record). `circuit(threshold K, cooldown C)` is a breaker constructor (let-init). All 7
  reserved tokens are now LIVE (retry/fallback/attempts/timeout/exponential/logarithmic/circuit).
  Opcodes `OP_RETRY_NEW/SHOULD_CONTINUE/NEXT_DELAY` (0x62-0x64) + `OP_CIRCUIT_NEW/ALLOW/RECORD`
  (0x65-0x67) → native `teko_rt_retry_*`/`teko_rt_circuit_*`, WASM reactor imports. Proofs
  `runtime/native/samples/resilience.tks` + `runtime/wasm/run-resilience.mjs`
  ([3,777,2,555,1,444,3,2,5]). Suite 198/198.
- **14.G — Timespan waiters: `await` (async) + `wait` (sync)** — ✅ done on both targets, on the
  **REAL monotonic clock** (executable `.tks` proof each). `wait <ts>;` spins on the real clock
  until now+ms elapses; `await <ts>;` ALSO drains the run queue each turn (cooperative). Both are
  NON-BLOCKING — the OS thread is never blocked in the kernel; only the time source is real (owner
  decision). Native `teko_rt_wait_ns` / `teko_rt_await_ns`; WASM emits an in-module i64 deadline
  loop reading `env.teko_now_ns` (+ `$teko_sched_run` for await). Keywords `wait`/`await`; opcodes
  `OP_WAIT` 0x59 / `OP_AWAIT_FOR` 0x5A (ms in $w0). Timespan literals (`10ms`/`2s`) normalize to
  canonical ms at compile time (`literal_canonical_value`), adopted in `lower_codec_value` so
  14.C/14.F delay args accept timespan literals. Proofs assert a LOWER BOUND on real elapsed +
  interleave order (non-deterministic clock → no exact counters): native `waiters.tks` (order
  1,2,3 + ≥12ms via run-native.sh check_timed); WASM `run-waiters.mjs` (order 1,2,3 + ≥12ms via
  process.hrtime.bigint).
- **Control-flow foundation** — ✅ done on both targets. `while (cond) { }`, `loop { }`,
  `if (cond) { }`, `break;`, `continue;` + local reassignment lower from source. Structured IL
  opcodes `OP_LOOP_BEGIN/END` (0x5B/0x5C), `OP_BREAK` (0x5D), `OP_CONTINUE` (0x5E),
  `OP_BREAK_IF_FALSE` (0x5F), `OP_IF_BEGIN/END` (0x60/0x61) — native local asm labels
  (`.Lcont_/.Lbrk_/.Lendif_`) + a loop-id/if-id stack; WASM structured `(block $brk (loop $cont))`
  + `(if … end)`. A shared block-body dispatcher (`lower_one_stmt`/`lower_block`) is reused by
  loop/if bodies AND routine bodies (so a background `fn` worker can loop). Proofs
  `runtime/native/samples/controlflow.tks` + `runtime/wasm/run-controlflow.mjs` ([10,5]).
- **14.H — Real `.tks` capstone samples** — ✅ done on both targets. One program combining named
  functions, a background routine with a LOOP inside it (14.A + control flow), main `while` loops +
  reassignment, an atomic accumulator (14.E), a delayed channel (14.C), and the `await`/`wait`
  waiters (14.G) → `[15, 6, 1, 2, 3, 42]`. Proofs `runtime/native/samples/capstone.tks` +
  `runtime/wasm/run-capstone.mjs` (reactor + in-module scheduler + host waiters). Suite 199/199.
  *(A dedicated Layer-B `--target=wasm-threads` thread sample remains covered by the Phase-10
  threads proof; the capstone targets Layer A.)*

- **14.I — Routine arguments (Go-style) + real producer/consumer** — ✅ done on both targets
  (owner follow-up). A `routines { worker(a, b, …); }` task now takes **arbitrary arguments** —
  passed by staging each into `$a0..$a(N-1)` (OP_SETARG) then `OP_SPAWN_ASYNC_ARGS argc`; the
  routine binds each `fn` param from `OP_LOAD_SPAWN_ARG i`. The channel's backing store already
  lives in the runtime (native heap / WASM reactor), so passing the **handle** (an int) is enough
  for a producer task and a consumer to share a channel — no blocking-recv/`select` needed: the
  structured **poll** + the control-flow foundation express a real consumer drain loop
  (`poll → if ready recv else stop/await`). Native: per-task arg vector in `teko_rt_sched`
  (`teko_rt_spawn_setarg`/`_args`), routine ABI passes an args pointer. WASM: args copied into the
  task spill frame, read via `$frame`. The Phase-10 `OP_SPAWN_ASYNC` (in-module-channel `arg=$cp`)
  is untouched. Proofs `runtime/native/samples/producer_consumer.tks` +
  `runtime/wasm/run-producer-consumer.mjs` (producer task given `(channel, count)` fills it; the
  consumer poll-drains → 15). Suite 200/200.

- **Real-time clock (owner pre-merge correction)** — ✅ the time base for ALL waiters/delays/
  timeouts is now a **real MONOTONIC nanosecond clock**, not a logical clock — while keeping
  cooperative NON-BLOCKING scheduling (the OS thread is never blocked in the kernel; only the time
  source changed). `teko_rt_now_ns()` = CLOCK_MONOTONIC (POSIX/macOS) / QueryPerformanceCounter
  (Windows/MSVC); WASM imports `env.teko_now_ns` (Node `process.hrtime.bigint()` — real ns;
  browser `performance.now()*1e6`, best-effort/coarsened — documented). Applied to 14.G waiters
  (real deadline spin), 14.C delayed (absolute ns deadlines; logical `advance` removed), and 14.F
  retry/circuit timeouts (real elapsed/cooldown). The runtimes stay clock-agnostic + KAT-
  deterministic (time passed in); the wrappers supply the real clock. Time tests assert LOWER
  BOUNDS on real elapsed + ordering (tolerant, fast, non-flaky), never exact counters.

**Phase 14 is COMPLETE** — all sub-blocks (14.A–14.I) + the control-flow foundation are done and
CI-green on all four gates; no dead tokens (every reserved concurrency/resilience keyword is live
with an executable `.tks` proof on native AND WASM); routines support real concurrent
producer/consumer via Go-style arguments; and all timing runs on the real monotonic clock. Ready
to leave draft.
