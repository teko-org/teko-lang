# Technical Debt Backlog — teko-lang

> Audit via the `/engineering:tech-debt` skill — revised on 2026-06-13.
> Prioritization: `Priority = (Impact + Risk) × (6 − Effort)`, each axis from 1 to 5.
> Project: the Teko language compiler in **C23** — 61 `.c` / 44 `.h`, ~10.3k LOC in `src/`; 42 test files (Unity).

## Prioritized summary

| # | Item | Category | Imp. | Risk | Eff. | Prio. | Band |
|---|------|----------|:----:|:----:|:----:|:-----:|:----:|
| 0 | ~~`teko` target missing `target_include_directories` → main binary does not build~~ | Infra (blocker) | 5 | 5 | 1 | **50** | ✅ Resolved 2026-06-13 |
| 1 | ~~FFI / generics / AOT modules with no tests~~ | Test debt | 4 | 5 | 3 | **27** | ✅ Resolved 2026-06-13 |
| 2 | ~~No validation of codegen output per target~~ | Test debt | 4 | 4 | 3 | **24** | ✅ Resolved 2026-06-13 |
| 3 | ~~CI with no Windows runner (PE/COFF unexercised)~~ | Infra | 3 | 3 | 2 | **24** | ✅ Resolved 2026-06-13 |
| 4 | ~~WASM stubbed opcodes (arena/async/channels)~~ | Code/Arch | 4 | 5 | 4 | **18** | ✅ Resolved 2026-06-13 (MVP; real concurrency → #9) |
| 5 | ~~`CMake GLOB_RECURSE` (stale builds)~~ | Infra | 2 | 2 | 1 | **20** | ✅ Resolved 2026-06-13 |
| 6 | ~~Scattered docs / no `ARCHITECTURE.md`~~ | Docs | 3 | 2 | 3 | **15** | ✅ Resolved 2026-06-13 |
| 7 | ~~Near-identical emitters (duplication)~~ | Code debt | 4 | 3 | 4 | **14** | ✅ Resolved 2026-06-13 (riscv + x86_64/arm64 GAS via #10; win/x86 separate) |
| 8 | ~~Versioned build artifacts~~ (resolved) | — | — | — | — | — | ✅ Close |
| 9 | WASM real concurrency (deferred → WASM threads proposal) | Code/Arch | 3 | 3 | 5 | **24** | ⏸️ Deferred (tracked) |
| 10 | Broader emitter de-dup (x86_64 SysV trio / arm64 GAS trio) | Code debt | 3 | 2 | 4 | **10** | ✅ Resolved 2026-06-13 (win_arm64 kept separate) |

---

## 0. `teko` target did not build — missing `target_include_directories` — `50` ✅ RESOLVED 2026-06-13

**Category:** Infra debt (build blocker)

**Situation:** In `CMakeLists.txt`, the production executable `add_executable(teko src/main.c)` did **not** receive `target_include_directories(teko PRIVATE src)` — only `teko_core` and `teko_tests` did. Because the include is `PRIVATE`, it is not propagated to the consumer. Result: compiling `src/main.c` failed with `fatal error: 'teko_target.h' file not found` (include chain via `src/codegen/tld_elf_arch.h`). The previously existing `cmake-build-debug/` contained only `libteko_core.a` and `teko_tests` — the `teko` binary had never been produced.

**Business justification:** This is the product's main binary (the compiler). Without it, only the tests run. `teko_core` and `teko_tests` build and pass (71 tests, 0 failures) because they have the include dir; the `teko` target was the only broken one.

**Resolution (2026-06-13):** Added the missing line to `CMakeLists.txt`:
```cmake
target_include_directories(teko PRIVATE src)
```
Verified: the `teko` binary now builds with 0 warnings and runs (prints the AOT compiler driver banner); full test suite still passes (71 tests, 0 failures).

**Files:** `CMakeLists.txt`.

## 1. FFI / generics / AOT modules with no test coverage — `27` ✅ RESOLVED 2026-06-13

**Category:** Test debt

**Situation:** No test file exercised `src/parser_ffi.c` (309 LOC), `src/parser_generics.c`, `src/parser_extensions.c`, or `src/codegen_aot.c`. The suite had 42 tests, but none touched these four modules.

**Business justification:** FFI is the path that matches Teko types to native C pointers (`extern fn ... from "x.dylib"`) — a bug here is memory corruption or a wrong native call, not a visible compile error. `codegen_aot.c` is the entry of the AOT path (the product differentiator). These are exactly the areas where a regression passes CI and breaks on the real target.

**Resolution (2026-06-13):** Added four test files (now 75 tests total, 0 failures):
- `tests/test_ffi.c` — `parse_extern_declaration` across all three shapes (extern struct with typed fields, extern fn with `as` alias, extern block), plus `parse_complex_type` (`ptr<ExternalStructure>`) and the `from`/`as` suffixes.
- `tests/test_generics.c` — `parse_generic_parameters_decl` (`<T, U>`) and `parse_generic_constraints_where` (`where T : struct`). Also implemented `free_generic_function_signature_members`, which was declared in the header but never defined (latent link error once referenced).
- `tests/test_extensions.c` — `parse_type_extension`: receiver binding, a normal method, and an inline operator overload.
- `tests/test_codegen_aot.c` — the AOT C-transpilation backend: header/runtime emission and a `NODE_VAR_DECL` (`i32` → `int32_t`) statement, read back from the emitted file.

**Files:** `src/parser_ffi.c`, `src/parser_generics.c`, `src/parser_extensions.c`, `src/codegen_aot.c`; new tests under `tests/`.

## 2. No validation of codegen output per target — `24` ✅ RESOLVED 2026-06-13

**Category:** Test debt

**Situation:** There are 16 emitters (`src/codegen/{linux,apple,bsd_unix,windows,bare_metal}/emit_*.c`). On re-audit, all 16 already had per-target emission tests with conservative per-architecture substring assertions — `tests/codegen/test_codegen_{linux (8), apple (2), unix/freebsd (2), windows (3), embedded/wasm (1)}.c` — covering the prologue and the syscall/halt path. The real gap was that the **core arithmetic opcodes** (`OP_ADD/SUB/MUL/DIV`) were unasserted for almost every target, which is exactly the surface most at risk during the emitter de-duplication refactor (item 7).

**Business justification:** Emitting a wrong instruction on an untested target (e.g., ARM32, MIPS, PPC64, RISC-V) is a silent regression: it passes CI and produces an invalid binary on the user's hardware. Binary reliability is the core product.

**Resolution (2026-06-13):** Verified the existing 16 per-target emission tests, then added `tests/codegen/test_codegen_emitters_arithmetic.c`: a single table-driven test that drives `OP_ADD` through every one of the 16 emitters and asserts the architecture-specific add mnemonic (e.g. `addl %ebx, %eax`, `add w0, w0, w1`, `addu $v0, $v0, $v1`, `add 3, 3, 4`, `i32.add`). This is the conservative golden safety-net required before item 7.

**Files:** `src/codegen/*/emit_*.c`; `tests/codegen/test_codegen_emitters_arithmetic.c` plus the existing `test_codegen_{linux,apple,unix,windows,embedded}.c`.

## 3. CI does not build or test on Windows — `24` ✅ RESOLVED 2026-06-13

**Category:** Infra debt

**Situation:** `.github/workflows/ci.yml` ran only `build-linux` and `build-macos`. The project has Windows backends (`src/codegen/windows/emit_win_*.c`) and a PE/COFF emitter (`src/codegen/tld_pe.c`) that were **never exercised on a Windows runner**.

**Business justification:** Toolchain differences (MSVC vs Clang, paths, line endings) and the PE path can break unnoticed.

**Resolution (2026-06-13):** Rewrote `ci.yml` into a `fail-fast: false` matrix over `ubuntu-latest`, `macos-latest`, and `windows-latest` (Windows via MSVC), which also drove the MSVC portability fixes (computed-goto fallback, `auto`/`nullptr` removal, portable packing, `<unistd.h>` guards). Merged via PR #1; all three OS jobs are green.

**Files:** `.github/workflows/ci.yml`.

## 4. WASM backend with stubbed opcodes — `18` ✅ RESOLVED 2026-06-13 (MVP)

**Category:** Code / Architecture debt

**Situation:** In `src/codegen/bare_metal/emit_wasm.c`, the opcodes `OP_ARENA_PUSH/POP`, `OP_SPAWN_ASYNC`, `OP_CHAN_INIT/PUT`, and `OP_AWAIT_INTENT` emitted **only comments**, not real WASM code. Arithmetic was implemented; arena and concurrency were not.

**Business justification:** Worse than missing — it produced a WASM module that *looked* like it compiled but ran with no arena allocation and no concurrency. The O(1) arena allocator is the language's advertised differentiator.

**Resolution (2026-06-13) — MVP:**
- **Arena allocator: implemented for real.** The prologue declares a mutable global `(global $arena_sp (mut i32) (i32.const 2048))` (based above the `.data` region at offset 1024); `OP_ARENA_PUSH` does an O(1) bump (`global.get`/`i32.const 1024`/`i32.add`/`global.set`) and `OP_ARENA_POP` rewinds it. Covered by `tests/codegen/test_codegen_embedded.c::test_teko_aot_wasm_arena_and_concurrency_hooks`.
- **Concurrency ops: honest host-runtime hooks instead of silent comments.** `OP_SPAWN_ASYNC`/`OP_CHAN_INIT`/`OP_CHAN_PUT`/`OP_AWAIT_INTENT` now emit `call $teko_spawn` / `$teko_chan_init` / `$teko_chan_put` / `$teko_await`, backed by `(import "teko_rt" ...)` declarations in the prologue. Real semantics are deferred — see item 9. Also fixed a residual Portuguese comment (`Label Marcacao` → `Label marker`).

**Post-fix (`5b65436`):** the item-4 commit's new WASM test perturbed the heap layout enough to expose a pre-existing latent heap-buffer-overflow in `test_codegen_linker_e2e.c` (a 19-byte `memcmp` under a `binary_size - 10` loop bound), which crashed the Windows runner (no Unity output — a buffered-stdout-lost-on-crash signature). Found via ASan/UBSan locally and fixed by bounding each `memcmp` by its own length. After the fix, PR #2 CI is **green on all three OSes** (Linux/macOS/Windows).

**Files:** `src/codegen/bare_metal/emit_wasm.c`; `tests/codegen/test_codegen_embedded.c`; `tests/codegen/test_codegen_linker_e2e.c`.

## 5. `CMake GLOB_RECURSE` for source collection — `20` ✅ RESOLVED 2026-06-13

**Category:** Infra debt

**Situation:** `CMakeLists.txt` used `file(GLOB_RECURSE CORE_SOURCES "src/*.c")` (and the same for `tests/`). CMake explicitly recommends against this: adding a new `.c` does not trigger a reconfigure, leading to stale builds and "works on my machine."

**Business justification:** Near-zero cost to fix; avoids an entire class of intermittent, hard-to-diagnose build failures.

**Resolution (2026-06-13):** Replaced both globs with explicit `CORE_SOURCES` (60 files) and `TEST_SOURCES` (42 files) lists, grouped by subsystem with a note to add new files there. Verified the lists match the filesystem exactly and that build + tests pass on both VM dispatch paths.

**Files:** `CMakeLists.txt`.

## 6. Scattered architecture documentation — `15` ✅ RESOLVED 2026-06-13

**Category:** Documentation debt

**Situation:** There were several overlapping planning documents (`docs/plan.md`, `docs/vm_plan.md`, `docs/BACKEND_AOT_PLAN.md`, `TEKO_COMPILER_MEMORANDUM.txt`) but no single map of the `lexer → parser → semantics → IL → VM/codegen` pipeline, nor of the boundaries between `vm_*`, `codegen/*`, `tld_*`, and `teko_il`. (The duplicate root `plan.md` was already removed earlier.)

**Business justification:** Parallel docs drift and contradict each other; onboarding and IL evolution depend on tribal knowledge.

**Resolution (2026-06-13):**
- Added `docs/ARCHITECTURE.md`: the canonical compiler-internals reference — full pipeline with a textual flow diagram and a stage-by-stage map of the key `src/` directories/files (frontend, semantics, IL, VM, AOT backend, the 16 emitters, the `tld` linker, tooling).
- Added a **Documentation Map** table at the top of `docs/plan.md` declaring the single source of truth per topic (roadmap → `plan.md`; architecture → `ARCHITECTURE.md`; tech debt → this file; overview → `README.md`) and labelling `TEKO_COMPILER_MEMORANDUM.txt`, `docs/vm_plan.md`, `docs/BACKEND_AOT_PLAN.md` as reference/historical.
- Added "reference / historical" banners to those three docs (content preserved, not deleted) and fixed a stray brand typo (`Leko` → `Teko`) in `BACKEND_AOT_PLAN.md`.

**Files:** `docs/ARCHITECTURE.md` (new), `docs/plan.md`, `docs/vm_plan.md`, `docs/BACKEND_AOT_PLAN.md`, `TEKO_COMPILER_MEMORANDUM.txt`.

## 7. Near-identical codegen emitters — `14` ✅ RESOLVED 2026-06-13

**Category:** Code debt

**Situation:** Every `emit_*.c` has the same signature `void emit_X(MetalContext*, OpCode, int32_t)` and the same `switch (op)`. The clearest duplicate was `emit_linux_riscv32.c` vs `riscv64.c`, which differed only in load/store width (`sw/lw` vs `sd/ld`), frame/offset sizes, the arena frame size, the banner bit-width, and the local-label tag — i.e. *purely per-ISA mnemonics/widths*.

**Business justification:** Duplication multiplies the cost and risk of any evolution of the opcode set; a fix forgotten in 1 of the N files becomes an architecture-specific bug.

**Resolution (2026-06-13):** First built the safety net — extended the per-target golden tests to assert ADD/SUB/MUL/DIV mnemonics for all 16 emitters — then extracted `emit_linux_riscv_common.{c,h}` (a single parameterized core) so riscv32/riscv64 are now ~10-line wrappers. Verified **byte-for-byte identical** emitted output for all 16 targets via a full-output capture/diff harness. ASan/UBSan clean on both dispatch paths.

**Broader families (completed in item 10):** the x86_64 SysV trio (linux/darwin/freebsd) and the arm64 GAS trio (linux/darwin/freebsd) were subsequently unified too, via `emit_x86_64_sysv_common` and `emit_arm64_gas_common` — see item 10. **Kept separate by design:** `emit_win_arm64` (MASM `AREA`/`PROC` directives) and the Windows x86 emitters (Intel/MASM syntax) are structurally unlike the AT&T/GAS families; unifying them would worsen the abstraction. The arithmetic goldens guard all 16 emitters against drift. Net: every emitter that is a true near-duplicate is now de-duplicated; what remains separate is separate on purpose.

**Files:** `src/codegen/linux/emit_linux_riscv{32,64,_common}.{c,h}`; `tests/codegen/test_codegen_emitters_arithmetic.c`.

## 8. Versioned build artifacts — ✅ RESOLVED

`git ls-files` shows 0 files under `cmake-build-debug/` or `node_modules/`; both are already in `.gitignore`. The previous backlog item was incorrect — **close**.

## 9. WASM real concurrency — ⏸️ DEFERRED (depends on the WASM threads proposal)

**Category:** Code / Architecture debt

**Situation:** The WASM MVP (item 4) routes `OP_SPAWN_ASYNC`, `OP_CHAN_INIT`, `OP_CHAN_PUT`, and `OP_AWAIT_INTENT` to imported host-runtime functions (`$teko_spawn`, `$teko_chan_init`, `$teko_chan_put`, `$teko_await`). These are honest placeholders, not a real green-thread/channel runtime.

**Why deferred (technical rationale):** Genuine M:N concurrency and blocking channels cannot be expressed in standalone WAT. They require the **WASM threads proposal**: a `shared` linear memory, the atomic instruction set (`memory.atomic.wait` / `memory.atomic.notify`, `i32.atomic.*`), and a host-side spawn mechanism (e.g. a JS `Worker` that instantiates the same module against the shared memory). That is an environment/runtime concern beyond the code emitter, so it is tracked separately rather than rushed.

**Remediation (future):** (1) Emit `(import "env" "memory" (memory $mem 1 1 shared))` when a `--target=wasm-threads` flag is set; (2) implement channel buffers as atomic ring buffers in shared memory with `wait`/`notify`; (3) provide the host `teko_rt` glue (Worker spawn + module re-instantiation); (4) add a runtime/integration test under a WASM engine that supports threads.

**Files:** `src/codegen/bare_metal/emit_wasm.c`; future host runtime glue.

## 10. Broader emitter de-duplication — `10` ✅ RESOLVED 2026-06-13 (win_arm64 kept separate)

**Category:** Code debt

**Situation:** After unifying the riscv32/64 pair (item 7), the remaining duplication was the x86_64 SysV trio (linux/darwin/freebsd) and the arm64 quad (linux/darwin/freebsd/windows). These diverge in syscall sequences, symbol decoration (`_main`/`_pthread_create`), string-label relocation syntax (ELF `:lo12:` vs Mach-O `@PAGE`/`@PAGEOFF`), local-label tags, and comment text — beyond pure mnemonics.

**Business justification:** Duplication multiplies the cost/risk of evolving the opcode set across architectures.

**Resolution (2026-06-13):** With the arithmetic goldens + full-output byte-diff harness as the safety net, the structural differences turned out to be cleanly parameterizable after all:
- **10a — `emit_x86_64_sysv_common.{c,h}`**: unifies `emit_{linux,freebsd,darwin}_x86_64` (→ ~16-line wrappers), parameterizing banner, symbol prefix, string label, label tag, halt/exit (syscall 60 / int $0x80 exit 1 / comment-only on macOS), and two comment lines.
- **10b — `emit_arm64_gas_common.{c,h}`**: unifies `emit_{linux,freebsd,darwin}_arm64`, additionally parameterizing the SCONST relocation style (ELF vs Mach-O).
- Each step verified **byte-for-byte identical output for all 16 targets** + goldens + ASan/UBSan on both dispatch paths.

**Intentionally kept separate:** `emit_win_arm64` (Windows on ARM) uses MASM-style directives (`AREA |.text|, CODE`, `PROC`), structurally unlike the GAS trio — unifying it would genuinely worsen the abstraction. Likewise the Windows x86 emitters (`emit_win_x86`, `emit_win_x86_64`) use Intel/MASM syntax distinct from the AT&T SysV trio. These remain explicit by design; the goldens guard them.

**Files:** `src/codegen/emit_x86_64_sysv_common.{c,h}`, `src/codegen/emit_arm64_gas_common.{c,h}`, and the six thin wrappers under `linux/`, `apple/`, `bsd_unix/`.

---

## Phased plan (alongside features)

**Phase 1 — quick wins (effort 1-2):** item **0** (one-line CMake include fix — unblocks the main binary), item **3** (Windows CI job), and item **5** (explicit CMake sources). Hours, not days; they close infra gaps that mask regressions.

**Phase 2 — test safety net (several sprints):** item **1** (FFI/generics/AOT tests, start with FFI) and item **2** (golden test per target, one per sprint). A prerequisite for refactoring with confidence.

**Phase 3 — implementation and consolidation:** item **4** (complete real WASM emission), then item **7** (de-duplicate emitters, protected by the Phase 2 goldens) and item **6** (`ARCHITECTURE.md` + plan consolidation).
