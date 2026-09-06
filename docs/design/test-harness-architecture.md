---
section: design
created: 2026-08-20
status: CANONICAL — consolidates design fragments from D58–D60 (DECISION_LOG), harness-de-testes-gerado.md, tkr-regression-format.md, and S10 concurrency crumbs
gating: memory milestone (build seco ≤1.5 GB / fixpoint / tests green per D52 triad)
---

# Test Harness & Concurrency Architecture — Canonical Design

**Purpose:** Single authoritative source for parallel test execution (tkt/tkr models), concurrency primitives (spawn/memchan/oschan), dependencies (DI), and roadmap. Supersedes the fragmented map in `test-concurrency-design-map.md`.

---

## 1. Executive Overview — Two Models, One Law

The Teko compiler runs tests in **two execution models**, each with distinct isolation boundaries, channel types, and parallelism strategy. Both are gated on the **memory stability milestone** (D52 triad: build seco ≤1.5 GB, fixpoint gen2==gen3 byte-identical, tests green); neither is blocked by the memory elephant (COL-F0), which is independent infrastructure.

| Model | Scope | Isolation | Concurrency | Channel | Primitives | Status |
|---|---|---|---|---|---|---|
| **tkt** (unit tests) | `#test` functions in `.tkt` files | THREAD-BASED, shared memory | Parallel via `spawn` + threads | `memchan<T>` (in-process) | spawn, memchan, TestVerdict struct | Design ~70–85% landed (S10-SURF/S10-RT); pending harness integration (RT-L6 crumb 0064) |
| **tkr** (regression tests) | Gherkin `.tkr` files, one `.tkp` regressor per N fixtures | PROCESS-ISOLATED, separate address spaces | Parallel sliding-window pool | `oschan<T>` (IPC/AF_UNIX) or archivo-based | spawn_redirected, wait_one, ProcPool, run_pool | Format ratified (owner 2026-07-23); runner sequential today, awaiting SW11.4 (#472) |

**The law (R0–R14, harness-de-testes-gerado.md §0):** Verdict assignment is by **HOUSE, not FLOW**. Executors never print; reporters never execute. The house is identified by **test INDEX** (in `prog.items` for tkt, scenario line for tkr), and the report is assembled by the parent after a barrier, in order. This law materializes identically in both models via different transports (channel for threads, file I/O or `oschan` for processes).

---

## 2. tkt: Unit Tests — Threads & `memchan<T>`

### 2.1 Execution Model

Each `.tkt` file is compiled to a **unique test binary**. At runtime, a `main` is synthesized (post-check, pre-lowering in `src/build/gate.tks`) that:

1. Discovers all `#test` functions (by `checker::TFunction.is_test` flag).
2. **Spawns each as an OS thread** (R1 ruling, D58; NOT process-based).
3. Each thread:
   - Runs its test body in isolation (arena per thread, task-local storage via `tk_task_begin/end`).
   - Captures the result in a `TestVerdict` struct (outcome, timing, stdout/stderr, panic/exit code).
   - **Sends the verdict to the parent** via `memchan<T>` (in-process memory channel).
4. **Parent collects all verdicts** after joining all threads, in **test INDEX order** (not thread-finish order), assembles the report.

Threads share the process address space (**not memory-isolated**); verdicts cross the thread boundary by **copying** to the parent's arena (§2.3).

### 2.2 Design Ruling: Copy, Not Move or Reference

Three forms were evaluated (harness-de-testes-gerado.md §6.3):

1. **Store pointer in parent's arena** — REJECTED: child arena closes at thread death; parent reads freed memory.
2. **Move value** — REJECTED: requires linear-type analysis (not in scope); doesn't survive panic (incomplete move leaves ambiguous state).
3. **Copy to parent's arena** — **CHOSEN**: parent owns channel slots; `send` copies `TestVerdict` entirely to parent memory before publishing. Survives panic (publish is atomic after copy), respects R6 (no `ref` across boundary), bounds memory (capacity × sizeof(T)).

**Trade-off:** `T` (here `TestVerdict`) must be **value-copyable** — scalars, `str`, flat structs. Closures, slices with interior pointers, and interfaces are compile errors.

### 2.3 TestVerdict Structure

```teko
pub type TestState = enum {
    Ok, Failed, Panicked, Exited, Vanished, NotRun
}

pub type TestVerdict = struct {
    index: u64              // prog.items index — the HOUSE
    name: str               // qualified test name
    state: TestState        // outcome
    code: i32               // exit code (if Exited)
    message: str            // assertion/panic message
    site: str               // file:line:column origin
    elapsed_ns: i64         // runtime in nanoseconds
    out_text: str           // captured stdout
    err_text: str           // captured stderr
}
```

All fields are **self-contained** (scalars or `str`); no nesting, no references. Text fields (`message`, `site`, `out_text`, `err_text`) are **truncated to `TEST_TEXT_CAP`** with a visible overflow mark, preventing unbounded channel growth during failure reporting.

### 2.4 Panic & Exit Capture — Two New Primitives

**Current state (measured):** `tk_panic_str` and `tk_exit` are `_Noreturn` and kill the PROCESS. When running tests in threads, both must be **thread-local bifurcations** (R10, R14):

- **Panic capture:** A thread-local handler (compiler-emitted at `#test` discovery time) catches panic and deposits a `TestVerdict` with `state=Panicked` instead of aborting the process.
- **Exit capture:** Similarly, `exit(code)` is intercepted; the thread stores `state=Exited, code=<code>` and returns normally.

**Mark is compile-time, not runtime** (R14 answer: "Compilação"). The compiler knows a function is a `#test` during checking; it emits **bifurcated** `panic` / `exit` calls (`tk_panic_str_thread` vs. `tk_panic_str`, `tk_exit_thread` vs. `tk_exit`) in the lowering, gated by an internal flag.

These are **internal to the test harness**, not public API (R7 note: "maintained only for testes").

### 2.5 Main Synthesized (Serial, then Threaded)

The harness generates a `main` in two **forms** (GateShape enum in gate.tks), configurable by policy:

- **Serial** (v0.3.1.1): Each `#test` runs sequentially in the main thread (for validation before threading works).
- **Threaded** (v0.3.1.2+): Each spawns a thread; parent drains `memchan<T>` (collects N verdicts by blocking on empty channel until thread count matches).

**Both forms produce the same AST** (from `gate_program` in `src/build/gate.tks`); the difference is `GateShape::Serial` vs. `GateShape::Threaded`, a policy decision, not two separate synthesizers. The C emitter consumes the same `main` (no drift between routes).

### 2.6 Capacity & Blocking

- **Channel capacity:** `cap = thread_count + 1` — one slot per thread in flight, plus one, so no thread blocks on `send`.
- **Sender blocks:** `send` blocks when channel is full (bounded backpressure).
- **Receiver blocks:** `recv` blocks when channel is empty and open; returns `null` when closed and empty.
- **Closure:** Only the **parent (receiver) closes** the channel after joining all threads. A sender closing the channel is a classic race condition; here it's structurally impossible.

### 2.7 Ref Guard (R6)

No `ref` to the thread's arena crosses the channel boundary. The thread receives only the `chan<T>` handle (which it never names the interior of); `send` is the only access, and it copies.

---

## 3. tkr: Regression Tests — Processes & Isolated Binaries

### 3.1 Hierarchy & Build-Per-Regressor (D60 — Critical Correction)

**Error in prior understanding:** Agents assumed **build-per-fixture** or **build-per-`.tkr`**. This is **catastrophically wrong** (D60 literally: "TODOS OS AGENTES ANTERIORES ERRARAM").

**Correct hierarchy:**
```
1 tkp (product) 
  → N tkp (regressors, directories with .tkr files) 
    → M .tkr files per regressor 
      → K Scenario / Scenario Outline per .tkr 
        → L fixture (row/single test case)
```

**Build counting:**
- **N builds**, one per REGRESSOR (per `.tkp` regressor directory).
- **M × K × L executions** (one per fixture, in isolated process).
- **Never N × M × K × L builds.**

The same regressor binary runs every fixture in a separate process (via `oschan` or archivo).

### 3.2 Execution Model

1. **Build once per regressor:** `src/build/regression.tks` compiles the regressor `.tkp` to a single binary (same invocation regardless of how many `.tkr` files or fixtures it holds).
2. **Discover all `.tkr` files:** `dir_tkr_files(dir)` scans the regressor directory for every `.tkr` (not just the first).
3. **Parse and run:** For each `.tkr`, parse Gherkin to `[]TkrFeature`, for each Feature's each Scenario, construct a fixture and **spawn an isolated process** running the regressor binary with **selector arguments** (or env var marking which fixture to run).
4. **Isolate by process:** Each fixture runs in its own address space (no shared memory). Verdicts cross the boundary via `oschan` (IPC AF_UNIX DGRAM) or through files (currentimplementation uses `.chan` file with JSON).
5. **Collect in order:** A `ProcPool` (sliding window) launches up to `jobs` children, collects by INDEX (not finish order), assembles report.

### 3.3 `.tkr` Format (Gherkin) — Ratified

The `.tkr` file is Gherkin BDD syntax (Feature / Scenario / Background / Examples / Given/When/Then/And/But keywords) with **TOML-value RHS** using existing manifest lexers. Ratified owner 2026-07-23 (tkr-regression-format.md).

**Example fixture:**
```gherkin
Feature: arithmetic in Teko

Background:
  Given backend = "own"
  Given targets = "all"

Scenario: addition
  Given args = ["10", "20"]
  When compiled
  Then exit = 0
  And stdout = "30"
```

**Parsing:** `src/build/tkr.tks` lowers Gherkin lines to in-memory `Tkr` structs (not a secondary TOML file on disk). The model and verdict layer (`RegrOutcome`, `check_run`, `match_stream`) are **frozen** (owner 2026-07-23).

### 3.4 Rediection & Descriptors

**ProcHandle & spawn_redirected** (harness-de-testes-gerado.md §5.2):

```teko
pub type ProcHandle = struct {
    raw: i64  // opaque host value (HANDLE on Windows, pid on POSIX)
}

pub fn spawn_redirected(
    argv: []str, dir: str, env: []str,
    in_path: str, out_path: str, err_path: str
): ProcHandle

pub fn wait_one(h: ProcHandle): i32
```

Parent creates three (or four) file paths before spawning:
```
<prefix>.in   → child stdin (written by parent before spawn)
<prefix>.out  → child stdout
<prefix>.err  → child stderr
<prefix>.chan → verdict channel (if TEKO_VERDICT_CHANNEL env var set)
```

Parent opens, duplicates descriptors into child, closes parent copies immediately after fork. Child inherits the fds. Parent reads files **after `wait_one`** (no concurrent drain, no `select`/`poll` needed).

### 3.5 The Third Channel & Verdict Transport

**R0/R3 ruling:** A third channel carries what exit codes cannot (panic line, coverage snapshot). On POSIX and Windows, there is no fourth standard stream, so the **channel is a PATH**:

- `TEKO_VERDICT_CHANNEL=<prefix>.chan` env var.
- Child writes JSON/binary verdict to that path.
- Fallback (no env var): verdict goes to stderr (human-readable; can run test binary by hand).

Current implementation uses **archivo-based DIY** (JSON in `.chan` file); future may use `tk_oschan_*` (AF_UNIX DGRAM) directly.

### 3.6 ProcPool — Sliding Window

```teko
pub type ProcPool = struct {
    jobs: u64                      // max children in flight
    inflight: []ProcHandle         // indexed by spec
}

pub fn run_pool(specs: []ProcSpec, jobs: u64): []CapResult
```

Maintains a deslizante window: launch 1, wait for 1, launch next. No barrier at each batch of `jobs`. Collects by SPEC INDEX (not finish order) — output is stable regardless of which child finishes first.

`CapResult` contains exit, stdout, stderr, harness_ns, child_ns, cmd. With `spawn_redirected` + `wait_one`, `child_ns` becomes a **real measurement** per child (not the shell-batched approximation of today).

### 3.7 Death of Shell Scaffolding

The entire shell layer (`sh_squote`, `sh_join`, `to_sh_path`, `cd_prefix_cmd`, `env_prefix_cmd`, `regr_batch_script`, `.rc` files) **is removed**. Reason: `spawn_redirected` is the escalonador; there is no shell to escape-quote for.

The `.rc` file dies specifically because **regressors have an explicit exit code contract in the `.tkr`** (unlike unitários, where exit is ambiguous). The exit code IS the verdict; no auxiliary channel is needed to disambiguate.

The `.chan` file (verdict channel) **survives**, but now **optional** (falls back to stderr).

---

## 4. Concurrency Primitives — Surface & Runtime

All three built-in transports and spawn land on the **maintained-C runtime** (not §16 FFI boundary). Only user-pluggable transports (Kafka, RabbitMQ, WebSocket) require dynamic FFI (§16–§17 future work).

### 4.1 `spawn` — Thread-Only (D58)

**Mandatory constraint (D1, not owner choice):** `spawn` creates **OS threads**, NOT processes. Arena discipline requires `tk_task_begin()` as the thread's first instruction and `tk_task_end()` as its last — a bracket Teko cannot express, so it lives in the C trampoline (`tk_thread_spawn` in `teko_rt.{c,h}`).

**Ruled out:**
- `extern fn` to `pthread_create` — `cabi fn(T…): R` is not a token the lexer mints.
- Direct `clone(2)` — platform-specific; correctly deferred to §16.

**Current landing (S1–S3 spine):**
- `tk_thread_spawn(fn, ctx, flags)` — **[C-rt]** (teko_rt.c:2554)
- Parser: `Spawn` AST node, contextual recognition — **[C]**
- Checker: ref-guard (R6, no `ref` across), arg-copy — **[C]**
- Codegen: ctx-blob + `cabi` trampoline — **[C]** (S3, hardest crumb)

**Joined by:** `tk_thread_join` (teko_rt.c:2567, joinable sibling).

### 4.2 `memchan<T>` — In-Process Memory Channel

Owned by the RECEIVER. Slots live in the receiver's arena. `send` copies to those slots before publishing (§2.3).

**Runtime (C-rt):**
```c
tk_memchan_init(cap, slot_size)        // teko_rt.c:2605+
tk_memchan_send(chan, value_bytes)     // copy + publish
tk_memchan_recv(chan): void* | null    // FIFO pop
tk_memchan_close(chan)                 // receiver only
```

**Surface (Teko, `src/threads/threads.tks`, 70–85% landed):**
```teko
pub type chan<T>  // created by chan_new<T>(cap)

pub fn send<T>(c: chan<T>, value: T): null | error
pub fn recv<T>(c: chan<T>): T | null
pub fn chan_close<T>(c: chan<T>)
```

**Blocking:** `send` blocks when full; `recv` blocks when empty & open; returns `null` when closed & empty.

**Use cases:**
- tkt: parent drains test verdicts from worker threads.
- Future: work queue (`Queue<T>` on top of `Ring<T>`, COL-Q14, M2).

### 4.3 `oschan<T>` — IPC Channel (AF_UNIX DGRAM)

Between **isolated processes** (no shared memory). Uses POSIX AF_UNIX datagram sockets.

**Runtime (C-rt, teko_rt.c:2840+):**
```c
tk_oschan_send(path, data, len)        // send to named socket
tk_oschan_recv(path, buf, cap): i32    // receive from named socket
```

**Proven by probe:** `examples/probes/chan_dgram/` validates AF_UNIX DGRAM transport on Linux/macOS/Windows.

**Use cases:**
- tkr: fixture verdicts cross process boundary (alternative to archivo).
- Future: distributed tracing, external IPC.

**Current regressor implementation:** Uses **archivo-based DIY** (JSON in `.chan` file) instead of real `oschan`. Future #472 will decide: upgrade to real `oschan`, or keep archivo?

---

## 5. Dependencies: Channels Require DI (D58.1)

**Ruling:** `memchan<T>` and `oschan<T>` are **service singletons** (one instance per injection scope). This creates a **dependency chain:**

```
DI (Part A landed: 0012 SM-G6; Part B deferred: 0117 D1-DI)
  ↓
canais (memchan/oschan, S10-SURF/S10-RT: 0115/0116)
  ↓
{await (opção-c D2, deferred), runner paralelo (#472)}
```

**Impact on roadmap:**
- tkt and tkr harness do NOT use DI directly (they are compiler-internal, created at compile time).
- DI becomes load-bearing when user code spawns tasks and shares channels across them (future patterns).
- **15–30% of canais still landing** = likely the DI-dependent lifetime/scope bindings (Part B).

**Confirmation:** Reconcile the channel map (test-concurrency-design-map.md) after Part B closes.

---

## 6. Roadmap — Planned, Not Yet Implemented

None of these are lacunas; all are explicitly scheduled crumbs or issues, gated on the memory milestone.

### 6.1 Harness Runtime Integration (RT-L6 crumb 0064) — M2

**What's missing:** Wiring tkt threads into the synthesized `main` (serial form in 0.3.1.1 exists; threaded form 0.3.1.2+).

- Emit `chan<TestVerdict>` creation in preamble.
- Spawn each test, `send` verdict.
- Parent drains by index, prints report.
- Thread-local panic/exit capture.

**Blocking:** Nothing; purely implementation of the existing design.

### 6.2 Parallel tkr Runner (#472 SW11.4) — M

**What's missing:** Specify and wire #472 runner-pool protocol.

- Decision needed: `memchan` (in-process, for unit-test-like suite), `oschan` (true IPC), or archivo?
- `Queue<T>` (COL-Q14 M2) as work-queue abstraction?
- Fixtures owed (wave-0.3.1-plan.md:442):
  - `test_selector_glob` (subset by pattern).
  - `test_panic_asserted` / `test_exit_asserted` (test framework APIs for diverging behavior).
  - `parallel_tests_serial_group` (`#serial_group` attribute skips parallelism for a subset).

**Blocking:** D52 memory milestone; owner decision on protocol.

### 6.3 Graceful Channel Shutdown

**What's missing:** Formal close protocol for `memchan<T>` under cancellation.

- Current: receiver closes after join (safe by construction).
- Future: graceful drain when a sender signals "last message".
- Out of scope for v1 (threaded harness does not rely on this).

### 6.4 `cancel()` Under `await` (CN1, deferred post-S10)

Part of the async/await stack (D2 ruling on suspension model still open). The public `cancel(error | null)` API is designed (harness-de-testes-gerado.md R11) but not implemented:

```teko
pub fn cancel(reason: str | null)
  // In thread: cancel thread, return with error.
  // In process: panic.
  // reason=null: thread cancels silently; process exits(1).
```

---

## 7. Gating: Memory Milestone (D52 Triad)

All concurrency work — tkt/tkr harness, spawn, canais, DI Part B, runner #472 — is **conditional on stable memory**.

**Gate condition (D52 law-final, refines 2026-08-18):**
1. **Build seco ≤ 1.5 GB** (heap peak).
2. **Fixpoint gen2 == gen3 byte-identical**.
3. **Tests green** (fixture pass).

**Until gate:** No `teko test .` full suite (OOM risk). Validation = compile + fixpoint + offline cross-check.

**After gate:** Full `teko build .` (with tests) becomes safe. Test-harness implementation and runner parallelization proceed in parallel (independent axes).

**Independent of COL-F0 (memory elephant):** The 6+ GB current consumption is the array-push accumulation (expurgo scope); concurrency design is orthogonal. Both must close before the test harness can run freely.

---

## Appendix A. Source Locator

| Component | Piece | File(s) | Line(s) | Type | Status |
|---|---|---|---|---|---|
| **Architecture (Decisions)** | D58–D60, D58.1 | DECISION_LOG.md | 601–632 | DECISION | AUTHORITATIVE (owner 2026-08-20) |
| **tkt harness (design)** | Rulings R1–R14 | harness-de-testes-gerado.md | 12–150 | DOC (rulings literal) | RATIFIED |
| | Synth `main` (Serial/Threaded) | harness-de-testes-gerado.md | 227–303 | DOC (gate.tks shape) | DESIGN COMPLETE |
| | Panic/exit capture (two primitives) | harness-de-testes-gerado.md | 714–815 | DOC (R5, R7, R10, R14) | DESIGN COMPLETE |
| | `TestVerdict` struct | harness-de-testes-gerado.md | 605–661 | DOC (type spec) | DESIGN COMPLETE |
| | `chan<T>` surface | harness-de-testes-gerado.md | 533–601 | DOC (Teko API) | DESIGN COMPLETE |
| **tkr format (Gherkin)** | `.tkr` Gherkin grammar | tkr-regression-format.md | 68–213 | DOC (spec) | RATIFIED (owner 2026-07-23) |
| | Hierarchy (build-per-regressor) | tkr-regression-format.md + DECISION_LOG.md D60 | various | DOC + DECISION | RATIFIED |
| | Discovery (all `.tkr` per regressor) | tkr-regression-format.md | 233–250 | DOC | RATIFIED |
| **Concurrency spine (S10)** | D1 (spawn constraint) | plano-s10-concorrencia-crumbs.md | 16–26 | DOC (design-ahead) | CONSTRAINT-FORCED |
| | D2 (await: thread-per-await option) | plano-s10-concorrencia-crumbs.md | 27–40 | DOC (fork, open) | PENDING OWNER RULING |
| | Crumb spine C0a–CN1 | plano-s10-concorrencia-crumbs.md | 44–89 | DOC (implementer sequence) | DESIGN-AHEAD |
| | `tk_thread_spawn` + join | teko_rt.c | 2554, 2567 | C [maintained] | LANDED (S1–S3) |
| | `tk_memchan_*` | teko_rt.c | 2605+ | C [maintained] | LANDED ~70–85% |
| | `tk_oschan_*` | teko_rt.c | 2840+ | C [maintained] | LANDED ~70–85% |
| | Surface (`IChannelKind`, `Rx/Tx`) | src/threads/threads.tks | — | Teko [leaf] | LANDED ~70–85% |
| **DI** | Part A (SM-G6 surface, service taint) | crumb 0012-SM-G6-di-service-taint.md | — | CRUMB | LANDED (M1) |
| | Part B (D1-DI, arena-lifetime binding) | crumb 0117-D1-DI-arena-lifetime-binding.md | — | CRUMB | DEFERRED (M5) |
| | Design (both parts) | plano-secao7-di-service-svc.md + mudancas-superficie-0.3.1.md §7–8 | — | DOC (sealed) | DESIGN COMPLETE |
| **Queue<T> (COL-Q14)** | FIFO on Ring<T> | colecoes-memoria-fila-implementacao-0.3.1.md | — | DOC | DESIGN COMPLETE |
| | Crumb 0082 | .crumbs/0082-COL-Q14-queue-deque.md | — | CRUMB | PLANNED (M2) |
| **Runner parallel (#472)** | Spec / protocol | wave-0.3.1-plan.md | 426–443 | DOC (plan) | PLANNED (M) |
| | Fixtures owed | wave-0.3.1-plan.md | 442–443 | DOC (checklist) | PLANNED |
| **Recon (Aug 20)** | Full audit of readiness | parallel-test-channels-recon.md | — | RECON | COMPLETE |

---

**Document Status:** CANONICAL (supersedes `test-concurrency-design-map.md`). Owned by the coordinator; updated when crumbs land or decisions change. All design fragments are cited by source; no design is reinvented here.
