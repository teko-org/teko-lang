# CLAUDE.md — operating guide for this repo

Teko is an **ahead-of-time (AOT) compiler written in C23**, with no LLVM: a modular
frontend (lexer → parser → type/semantic checks) feeds an **IL bytecode**, which a
polymorphic backend lowers to **16 native emitters** (ELF / Mach-O / PE-COFF) plus a
**WebAssembly** backend. This file is the operational guide; it **references** the
canonical docs rather than duplicating them.

## Canonical docs (read these for depth)
- `docs/plan.md` — the phased roadmap (Phases 1–19; WASM Concurrency = Phase 10, done).
- `docs/ARCHITECTURE.md` — compiler architecture; `docs/BACKEND_AOT_PLAN.md`, `docs/vm_plan.md`.
- `README.md` → **Supported Targets** — the 16 emitters + WASM A/B with honest CI status.
- `docs/PHASE10_WASM_CONCURRENCY.md` — the WASM concurrency backend design.
- `docs/PHASE_BROWSER_FFI.md` — the current phase (Browser FFI / JS-DOM interop) + plan.
- `TECH_DEBT_BACKLOG.md` — tech-debt items and their resolution status.

## Essential commands
```sh
# Build (Linux: Clang+Ninja; macOS/Windows: default generator)
cmake -S . -B build -G "Ninja" -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4            # targets: teko_core (lib), teko (cli), teko_tests
./build/teko_tests                          # Unity suite (the per-emitter goldens live here)

# Sanitizers — run BOTH VM dispatch paths every commit (see Discipline):
cmake -S . -B build-asan -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
cmake --build build-asan --target teko_tests -j4 && \
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 ./build-asan/teko_tests
# Portable switch dispatch: add -DTEKO_VM_PORTABLE_DISPATCH to CMAKE_C_FLAGS, repeat.
# ThreadSanitizer (caught the FFI wild-free; run for uninit/race regressions):
#   -DCMAKE_C_FLAGS="-fsanitize=thread -g -O1"  then  TSAN_OPTIONS=halt_on_error=1 ./build-tsan/teko_tests

# WASM harnesses (no real .tks→.wat pipeline yet — emit-demos drive the emitter directly):
#   runtime/wasm/emit-demo/*.c  link build/libteko_core.a, emit .wat; wat2wasm assembles.
node runtime/wasm/run-node.mjs              # cooperative fixtures (Node) — channels=42, etc.
node runtime/wasm/threads/run-node-threads.mjs   # Layer B via worker_threads (777/99)
# Browser: runtime/wasm/run-browser.mjs / run-threads-browser.mjs (Playwright, COOP/COEP).
```
No local `wat2wasm`? `npm i wabt` gives a JS `parseWat`/`toBinary` — used to validate emitter
output locally (the goldens only `strstr`; always *assemble + run*, never trust strstr alone).

## CI (4 workflows, one badge each; see README → Branch protection)
- `native.yml` — Build & Test matrix: Linux x86_64/arm64, macOS arm64, Windows x86_64/arm64,
  + Linux riscv64 under **QEMU** (non-blocking).
- `wasm.yml` — Layer A (wasmtime + headless Chromium). `wasm-threads.yml` — Layer B
  (node `worker_threads` + Web Workers).
- `sanitizers.yml` — `asan-ubsan` (both dispatch paths), `tsan`, `windows-stress` (suite ×20).
- **Path filters + gate:** each workflow always runs; a `changes` job skips the heavy jobs on
  irrelevant (e.g. docs-only) changes; an always-running **`gate`** job aggregates and is the
  ONLY required check. Never `on: paths:` a required workflow (phantom "waiting" blocks merge).
  Branch protection requires only the four `<workflow> / gate` checks.

## Engineering discipline (non-negotiable)
- **One increment per commit.** Build + ASan + UBSan on **both** dispatch paths each commit.
- **Per-architecture emission goldens** pin the 16 native emitters — never regress them.
- **Executable validation**, not just `strstr`: WASM output is assembled and run (node/wasmtime/browser).
- **Nothing merges without green CI.** A ~50%-flaky gating job does NOT count as green — fix the
  root cause, don't mask with timeouts or non-blocking.
- No `git merge`/force-push from the agent; the human merges. New phase ⇒ branch + **draft PR** up front.

## MSVC / Windows portability rules (hard-won)
- **No computed-goto** in the VM — guarded portable `switch` fallback (`TEKO_VM_PORTABLE_DISPATCH`,
  the path MSVC uses). No C23 `auto`/`nullptr` in shared code or tests.
- **Portable struct packing** via `TEKO_PACKED` + `__pragma(pack)` (see `src/teko_il.h`), not
  `__attribute__((packed))` alone.
- Guard POSIX headers (`<unistd.h>`, `access`) behind `#if !defined(_WIN32)`; provide Win32 paths.
- The **WASM emitter is an accumulator machine**: `$w0` accumulator, `$w1` scratch, `$cp` channel
  ptr; every op is stack-neutral so each function ends with exactly one value (a stack-imbalanced
  module won't even instantiate — the old golden-only tests hid this).

## WASM concurrency backend (Phase 10)
- **Layer A — cooperative (default `wasm`/`wasm32-wasi`):** M:N green threads on one thread,
  entirely in-module: a linear-memory run queue + function table, `call_indirect` dispatch,
  ring-buffer channels, and **mid-function suspension** via a `br_table` state machine + per-task
  spill frame (`OP_FUNC_BEGIN/END`, `OP_CHAN_GET`). No host runtime.
- **Layer B — `--target=…-wasm-threads`:** real multicore. Imports a `shared` memory, channels
  use the **atomics** proposal, `spawn` delegates to a host that starts a Web Worker /
  `worker_threads` thread re-instantiating the module against the shared memory. The channel
  **receive busy-polls** (bounded, with an `unreachable` cap) instead of `memory.atomic.wait32`,
  because **cross-instance `notify` is unreliable on the GitHub runner** — see Decisions.

## Decisions / Memories (pitfalls already resolved)
- **Windows intermittent crash (~50%) = uninitialized-pointer wild-free in the FFI AST**
  (`src/parser_ffi.c`): nodes were `malloc`'d and free walked unset pointer fields. Fixed with
  `calloc` + defensive NULL-init of `params/param_type/field_type/fn_name`. ASan/UBSan/heap+stack
  poisoning were all clean; **ThreadSanitizer** reproduced it deterministically → keep the TSan gate.
- **CI `apt-get update` 403** = the hosted image's third-party repos (`packages.microsoft.com`,
  `azure-cli`) are unsigned; **remove those source lists** before `apt-get update` (a retry just
  re-hits the 403). Done in every Linux job.
- **Required-check phantom-block** with path filters → solved with the always-running `gate` job
  (above). Require only the gates.
- **Roadmap:** the "WASM Concurrency Backend" was reclassified from a tech-debt item to its own
  **Phase 10** (merged via PR #3). Browser FFI / JS-DOM interop is the **current phase**
  (Phase 11; `docs/PHASE_BROWSER_FFI.md`).
- **Browser FFI backend AND a real `.tks` frontend are built.** The WASM backend lowers the
  full Browser FFI surface (imports, DOM, events, allocator, facade), and the frontend now
  compiles real source for it: `teko build <f>.tks --target=wasm` lexes/parses/lowers the
  interop subset (`extern`, `@dom`/`@js`, strings, `fn` event handlers) to IL → `.wat` with
  **no mock bytecode** (`frontend_interop.c` + `codegen_li_wasm.c`; reuses the real lexer +
  `parse_extern_declaration`). General expressions / named locals are future work; backend
  features beyond the source subset remain exercised via the `emit-demo/*.c` drivers.

## Current state / next
Phase 10 (WASM concurrency, Layers A & B) is **merged and CI-green**. **Phase 11 (Browser FFI /
JS-DOM interop) is complete and CI-green** on branch `feat/browser-ffi-interop` (PR #4, awaiting
human merge): MVP-1a string pool `(data …)`, MVP-1b `extern → (import …)` + `OP_CALL_IMPORT`,
MVP-2 DOM (`dom.*` multi-arg imports + `OP_SETARG` + auto-generated `.glue.mjs`), MVP-3 JS→Teko
events (`dom.on` + exported `teko_invoke`), MVP-4 real freeing allocator
(`teko_alloc`/`teko_free`/`teko_reset`, free-list + coalescing) + `teko_invoke2` + JS→Teko
strings + ergonomic facade (`<mod>.mjs`) + rich event payload (`dom.on_value`). All proven via
emit-demos under Node + headless Chromium (Playwright). See `docs/PHASE_BROWSER_FFI.md`.
