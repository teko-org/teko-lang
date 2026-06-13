# Phase 10 — WASM Concurrency Backend (design & implementation plan)

> Feature branch: `feat/phase-10-wasm-concurrency`. Status: **planning + scaffolding**;
> real concurrency emission is gated on plan approval. The WASM MVP (real linear-memory
> arena + honest host-runtime hooks for spawn/chan/await) is already on `main` (Phase 9).
> Roadmap context: `docs/plan.md` → Phase 10.

## 1. Goal

Make Teko's concurrency model — `routines`/spawn, channels, `await intent<T>` — actually
*run* when the target is WebAssembly, instead of delegating to non-existent host imports.

## 2. Architecture: two layers, cooperative-first

WASM has **no GA stack-switching** proposal, so we cannot do native green-thread context
switches. Two complementary layers, implemented in this order:

### Layer A — Cooperative scheduler compiled into the module (default `wasm`/`wasm-wasi`)
Reproduce the **M:N green-thread model on a single WASM thread**, reusing the linear-memory
arena already delivered. Everything lives *inside* the emitted module — **no host runtime
required**, so it runs in any standalone engine (wasmtime/node) and is trivially testable.

- A **run queue** in linear memory holds task descriptors `{ fn_index, arg_ptr, state }`.
- A top-level **scheduler loop** (emitted as `$teko_sched_run`, called from `$main`) pops
  ready tasks and invokes them via `call_indirect` against a function table.
- **Yield points** (`await`, blocking channel ops) return control to the scheduler.
- **MVP semantics:** run-to-completion tasks + cooperative yield at explicit await/channel
  boundaries. Arbitrary mid-function suspension needs a state-machine/CPS lowering of the
  green-thread body — that is the deepest sub-step (Layer A.2, see §6) and may be deferred.

### Layer B — `--target=wasm-threads` (opt-in, real multicore)
Adds genuine parallelism: emit `(import "env" "memory" (memory N N shared))`, use the
atomics set (`memory.atomic.wait32`/`notify`, `i32.atomic.*`) for blocking channels, and
spawn via host **Web Workers / `worker_threads`**. Caveat: Workers are **1:1 OS threads,
not M:N** — a semantic mismatch with green threads, so this is an *opt-in* parallelism layer
on top of A, not a replacement. Needs per-environment host glue + a threads-capable engine.

## 3. Opcode lowering: hook → real (Layer A)

| Opcode | Today (MVP, `main`) | Layer A (cooperative, in-module) |
|--------|---------------------|----------------------------------|
| `OP_SPAWN_ASYNC` | `call $teko_spawn` (host import) | append `{fn_index, arg}` to the run queue in linear memory (no host) |
| `OP_CHAN_INIT` | `call $teko_chan_init` | bump-allocate a channel struct `{head, tail, cap, buf[]}` in the arena; return ptr |
| `OP_CHAN_PUT` | `call $teko_chan_put` | store into the ring buffer; if full, mark task blocked and yield to scheduler |
| `OP_AWAIT_INTENT` | (no-op / hook) | yield to scheduler until the awaited result/intent is ready |
| (new) channel receive | — | read from ring buffer; if empty, block + yield |

The arena (`$arena_sp`) is reused for all of these allocations.

## 4. Host ABI (minimal)

- **Layer A:** *no host runtime needed.* The module exports `main`/`run` and (for tests)
  writes results to a known linear-memory offset or returns an i32. I/O for tests via WASI
  (`fd_write`) only if needed. This is what makes A testable in CI without a browser.
- **Layer B (`wasm-threads`):** host must provide a shared `memory` import and a
  `teko_rt.spawn(fn_index: i32, arg_ptr: i32)` that starts a Worker which re-instantiates the
  same module against the shared memory and calls the entry. Atomics live in the module. A
  small reference `teko_rt` (JS, ~a few hundred lines) ships under `runtime/wasm/`.

## 5. Testing strategy

1. **Golden emission tests** (extend `tests/codegen/test_codegen_embedded.c`): assert the
   emitted WAT contains the Layer-A primitives per increment (run-queue ops, channel ring
   buffer, `$teko_sched_run`, `call_indirect`).
2. **Execution under a standalone engine (CI):** job `wasm-wasmtime` builds the WASM fixture
   with `wat2wasm` and runs it under **Node** and **wasmtime** — real execution, no browser.
   Modeled on the non-blocking riscv64-QEMU job; `continue-on-error` until stable.
3. **Execution in a headless browser (CI):** job `wasm-browser` (Playwright + Chromium) loads
   the module in a page served with **COOP/COEP** (`Cross-Origin-Opener-Policy: same-origin` +
   `Cross-Origin-Embedder-Policy: require-corp`) and asserts behavioral **parity** with the
   standalone engines, plus `crossOriginIsolated === true` (the prerequisite for Layer B's
   SharedArrayBuffer + Web Workers). Browser coverage is required even when WASM ships only as
   an add-in to another host — `continue-on-error` until stable. Harness: `runtime/wasm/`.
4. **Layer B** parallel execution additionally runs under **node `worker_threads`** in a
   dedicated job (added with increment 10.4).
5. The C compiler itself keeps building/testing under **ASan + UBSan on both dispatch paths**
   every commit, as in Phase 9.

## 6. Incremental plan (one increment per commit; build+test+ASan green; CI green via PR)

- **10.0 — scaffolding (safe, no real concurrency):** recognize `--target=...-wasm-threads`
  in `teko_target.c` (parsed to a flag; emission unchanged/cooperative by default); this
  design doc; a golden test pinning the *current* WASM emission as the baseline; CI job
  skeleton (wasmtime install + run the existing WASM golden). **← first increment.**
- **10.1 — channels in linear memory** (cooperative, non-blocking first): CHAN_INIT/PUT/recv
  as arena ring buffers. Golden + wasmtime execution test.
- **10.2 — cooperative scheduler + real SPAWN/AWAIT:** run queue, `$teko_sched_run`,
  `call_indirect` task dispatch, yield at await. Run-to-completion tasks.
- **10.3 — blocking channels + mid-yield** (the hard part): state-machine/CPS lowering of
  green-thread bodies so they can suspend mid-function. May be split further or deferred.
- **10.4 — `--target=wasm-threads`** (Layer B): shared memory + atomics + Web Worker host
  glue; node execution test. Opt-in parallelism.

## 7. Risks / open decisions (need approval before the heavy increments)

1. **Mid-function suspension (10.3)** is genuine compiler work (state-machine/CPS transform).
   Recommendation: ship 10.1+10.2 (channels + run-to-completion cooperative scheduling) first;
   treat 10.3 as its own reviewed sub-project.
2. **CI engine:** wasmtime for Layer A, node/worker_threads for Layer B. Adds toolchain to CI.
3. **Faithfulness of Layer A semantics:** start with non-blocking channels, add blocking
   (yield-based) once the scheduler exists — acceptable?
4. **Reuse of native scheduler logic:** the native `teko_thread.c` is a *simulated* scheduler
   and is **not** directly reusable; the WASM scheduler is emitted WAT, a separate artifact.
5. **`wasm-threads` target spelling** and whether it is a distinct target triple or a flag on
   the existing `wasm32-wasi` target.
