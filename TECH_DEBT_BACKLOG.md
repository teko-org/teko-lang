# Technical Debt Backlog — teko-lang

> Audit via the `/engineering:tech-debt` skill — revised on 2026-06-13.
> Prioritization: `Priority = (Impact + Risk) × (6 − Effort)`, each axis from 1 to 5.
> Project: the Teko language compiler in **C23** — 61 `.c` / 44 `.h`, ~10.3k LOC in `src/`; 42 test files (Unity).

## Prioritized summary

| # | Item | Category | Imp. | Risk | Eff. | Prio. | Band |
|---|------|----------|:----:|:----:|:----:|:-----:|:----:|
| 0 | ~~`teko` target missing `target_include_directories` → main binary does not build~~ | Infra (blocker) | 5 | 5 | 1 | **50** | ✅ Resolved 2026-06-13 |
| 1 | FFI / generics / AOT modules with no tests | Test debt | 4 | 5 | 3 | **27** | 🔴 High |
| 2 | No validation of codegen output per target | Test debt | 4 | 4 | 3 | **24** | 🔴 High |
| 3 | CI with no Windows runner (PE/COFF unexercised) | Infra | 3 | 3 | 2 | **24** | 🔴 High |
| 4 | WASM backend with stubbed opcodes (arena/async/channels) | Code/Arch | 4 | 5 | 4 | **18** | 🟡 Medium |
| 5 | `CMake GLOB_RECURSE` (stale builds) | Infra | 2 | 2 | 1 | **20** | 🟡 Medium |
| 6 | Scattered docs / no `ARCHITECTURE.md` | Docs | 3 | 2 | 3 | **15** | 🟡 Medium |
| 7 | 16 near-identical emitters (duplication) | Code debt | 4 | 3 | 4 | **14** | 🟢 Low |
| 8 | ~~Versioned build artifacts~~ (resolved) | — | — | — | — | — | ✅ Close |

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

## 1. FFI / generics / AOT modules with no test coverage — `27` 🔴

**Category:** Test debt

**Situation:** No test file exercises `src/parser_ffi.c` (309 LOC), `src/parser_generics.c`, `src/parser_extensions.c`, or `src/codegen_aot.c`. The suite has 42 tests, but none touch these four modules.

**Business justification:** FFI is the path that matches Teko types to native C pointers (`extern fn ... from "x.dylib"`) — a bug here is memory corruption or a wrong native call, not a visible compile error. `codegen_aot.c` is the entry of the AOT path (the product differentiator). These are exactly the areas where a regression passes CI and breaks on the real target.

**Remediation:** `tests/test_ffi.c`, `tests/test_generics.c`, and coverage of `codegen_aot` — start with FFI (largest surface and highest risk). Cases: `extern struct` resolution, complex type matching, nested generics (`map<str, mut i32>`).

**Files:** `src/parser_ffi.c`, `src/parser_generics.c`, `src/parser_extensions.c`, `src/codegen_aot.c`.

## 2. No validation of codegen output per target — `24` 🔴

**Category:** Test debt

**Situation:** There are 16 emitters (`src/codegen/{linux,apple,bsd_unix,windows,bare_metal}/emit_*.c`). There are good linker/E2E tests (`tests/codegen/test_codegen_linker_e2e.c`, `_macho`, `_pe`, `_reloc`) and opt tests (CSE/DCE/folding), but the **per-architecture instruction output** (the `fprintf` of each `emit_*`) is not asserted — only Linux/x86_64 has a genuinely covered path.

**Business justification:** Emitting a wrong instruction on an untested target (e.g., ARM32, MIPS, PPC64, RISC-V) is a silent regression: it passes CI and produces an invalid binary on the user's hardware. Binary reliability is the core product.

**Remediation:** A per-target test that compiles a minimal program and compares the emitted string against an expected golden (prologue, `OP_ADD`, epilogue). Add one target per sprint.

**Files:** `src/codegen/*/emit_*.c`, mirror `tests/codegen/test_codegen_linux.c`.

## 3. CI does not build or test on Windows — `24` 🔴

**Category:** Infra debt

**Situation:** `.github/workflows/ci.yml` runs only `build-linux` and `build-macos`. The project has Windows backends (`src/codegen/windows/emit_win_*.c`) and a PE/COFF emitter (`src/codegen/tld_pe.c`) that are **never exercised on a Windows runner**.

**Business justification:** Toolchain differences (MSVC vs Clang, paths, line endings) and the PE path can break unnoticed. Very low cost: add a `windows-latest` job.

**Remediation:** Add a `build-windows` job (`windows-latest`, CMake + Ninja/MSVC) running `teko_tests`.

**Files:** `.github/workflows/ci.yml`.

## 4. WASM backend with stubbed opcodes — `18` 🟡

**Category:** Code / Architecture debt

**Situation:** In `src/codegen/bare_metal/emit_wasm.c`, the opcodes `OP_ARENA_PUSH/POP`, `OP_SPAWN_ASYNC`, `OP_CHAN_INIT/PUT`, and `OP_AWAIT_INTENT` emit **only comments** (`;; --- [WASM Arena Push] ---`), not real WASM code. Arithmetic (`OP_ADD`…) is implemented; concurrency and arena are not.

**Business justification:** Worse than missing — it produces a WASM module that *looks* like it compiles but runs with no arena allocation and no concurrency. The O(1) arena allocator and green threads are the language's advertised differentiator.

**Remediation:** Implement real emission (linear memory for the arena, WASM threads/atomics for async) and cover it with a runtime test. Mark explicitly as "unsupported" until then, instead of silently emitting a comment.

**Files:** `src/codegen/bare_metal/emit_wasm.c:71+`.

## 5. `CMake GLOB_RECURSE` for source collection — `20` 🟡

**Category:** Infra debt

**Situation:** `CMakeLists.txt` uses `file(GLOB_RECURSE CORE_SOURCES "src/*.c")`. CMake explicitly recommends against this: adding a new `.c` does not trigger a reconfigure, leading to stale builds and "works on my machine."

**Business justification:** Near-zero cost to fix; avoids an entire class of intermittent, hard-to-diagnose build failures.

**Remediation:** List sources explicitly, or keep the glob with `CONFIGURE_DEPENDS`. Applies to the `tests/` glob too.

**Files:** `CMakeLists.txt`.

## 6. Scattered architecture documentation — `15` 🟡

**Category:** Documentation debt

**Situation:** There are five overlapping planning documents (`plan.md`, `docs/plan.md`, `docs/vm_plan.md`, `docs/BACKEND_AOT_PLAN.md`, `TEKO_COMPILER_MEMORANDUM.txt`) but no single map of the `lexer → parser → semantics → IL → VM/codegen` pipeline, nor of the boundaries between `vm_*`, `codegen/*`, `tld_*`, and `teko_il`.

**Business justification:** Five parallel docs drift and contradict each other; onboarding and IL evolution depend on tribal knowledge.

**Remediation:** Write `docs/ARCHITECTURE.md` with the pipeline and layers; consolidate/archive the redundant plans pointing to it.

**Files:** `docs/`, root (`plan.md`, `TEKO_COMPILER_MEMORANDUM.txt`).

## 7. 16 near-identical codegen emitters — `14` 🟢

**Category:** Code debt

**Situation:** Every `emit_*.c` has the same signature `void emit_X(MetalContext*, OpCode, int32_t)` and the same `switch (op)`; pairs like `emit_linux_riscv32.c` vs `riscv64.c` differ in ~40 lines (basically `sw/sd`, `lw/ld`). Every change to the IL ISA requires editing 16 files.

**Business justification:** Duplication multiplies the cost and risk of any evolution of the opcode set; a fix forgotten in 1 of the 16 becomes an architecture-specific bug.

**Remediation:** Extract the common structure (the `switch` dispatch) and parameterize only the per-ISA mnemonics — an instruction table or X-macros. Low-risk refactor if done after item 2 (golden tests provide the safety net).

**Files:** `src/codegen/{linux,apple,bsd_unix,windows}/emit_*.c`.

## 8. Versioned build artifacts — ✅ RESOLVED

`git ls-files` shows 0 files under `cmake-build-debug/` or `node_modules/`; both are already in `.gitignore`. The previous backlog item was incorrect — **close**.

---

## Phased plan (alongside features)

**Phase 1 — quick wins (effort 1-2):** item **0** (one-line CMake include fix — unblocks the main binary), item **3** (Windows CI job), and item **5** (explicit CMake sources). Hours, not days; they close infra gaps that mask regressions.

**Phase 2 — test safety net (several sprints):** item **1** (FFI/generics/AOT tests, start with FFI) and item **2** (golden test per target, one per sprint). A prerequisite for refactoring with confidence.

**Phase 3 — implementation and consolidation:** item **4** (complete real WASM emission), then item **7** (de-duplicate emitters, protected by the Phase 2 goldens) and item **6** (`ARCHITECTURE.md` + plan consolidation).
