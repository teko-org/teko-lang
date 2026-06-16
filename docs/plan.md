# 🗺️ Technology Tree Plan: The Teko Language

> Action plan updated on 2026-06-14. Phases 1–8 reflect work already delivered/in
> progress. Phase 9 is the Technical Debt Resolution hardening track (from
> `TECH_DEBT_BACKLOG.md`). Phase 10 is the WASM Concurrency Backend feature
> (delivered — merged via PR #3). Phase 11 is the Browser FFI / JS-DOM Interop
> feature (merged via PR #4; `docs/PHASE_BROWSER_FFI.md`). Phase 12 (Frontend Grammar)
> is current (PR #5). Phase 13 is the dedicated Native Cryptography phase (planned).
> The memorandum-derived language phases are 12 and 14–23 (`TEKO_COMPILER_MEMORANDUM.txt`).
> Phase 16 (Casting / Type Conversions) is merged; **Phase 17 (Floating-Point & Numeric Types)**
> was inserted next (owner-requested — it closes the float gap Phase 16 left gated), shifting the
> old 17–22 to 18–23. The Self-Containment (Self-Hosting) milestone is the final phase, 23.

## 📚 Documentation Map

To stop the planning docs from drifting, here is the single source of truth for each topic. **Canonical** docs are kept current; **reference/historical** docs are preserved for context but are not maintained as living plans.

| Document | Role | Status |
|----------|------|--------|
| [`README.md`](../README.md) | Project overview, features, quick start | Canonical (user-facing) |
| **`docs/plan.md`** (this file) | The roadmap — phases 1–23, current status | **Canonical (roadmap)** |
| [`docs/ARCHITECTURE.md`](./ARCHITECTURE.md) | Compiler pipeline & module/file map | **Canonical (architecture)** |
| [`docs/PHASE10_WASM_CONCURRENCY.md`](./PHASE10_WASM_CONCURRENCY.md) | Phase 10 design & implementation (delivered) | Canonical (feature design) |
| [`docs/PHASE_BROWSER_FFI.md`](./PHASE_BROWSER_FFI.md) | Phase 11 design & plan (Browser FFI / JS-DOM interop) | Canonical (feature design) |
| [`docs/PHASE16_CASTING.md`](./PHASE16_CASTING.md) | Phase 16 design (Casting / conversions / parsing / auto-`to_string`) | Canonical (feature design) |
| [`docs/PHASE17_FLOATING_POINT.md`](./PHASE17_FLOATING_POINT.md) | Phase 17 design (Floating-Point & Numeric Types) | Canonical (feature design) |
| [`TECH_DEBT_BACKLOG.md`](../TECH_DEBT_BACKLOG.md) | Prioritized maintenance backlog | Canonical (tech debt) |
| [`TEKO_COMPILER_MEMORANDUM.txt`](../TEKO_COMPILER_MEMORANDUM.txt) | Owner's checkpoint memorandum; source of phases 12 & 14–23 | Reference / historical |
| [`docs/vm_plan.md`](./vm_plan.md) | Phase 3 (VM & debugger) sprint detail | Reference / historical (phase delivered) |
| [`docs/BACKEND_AOT_PLAN.md`](./BACKEND_AOT_PLAN.md) | Phase 5 AOT backend spec (target matrix, ABIs, per-opcode requirements) | Reference (backend spec) |
| [`PITCH.md`](../PITCH.md) / [`PITCH-pt-br.md`](../PITCH-pt-br.md) | Marketing pitch (EN / PT-BR) | Reference |

When in doubt: roadmap questions → this file; "how does the compiler work / where is X" → `ARCHITECTURE.md`; "what should we fix next" → `TECH_DEBT_BACKLOG.md`.

```mermaid
graph TD
    %% Graph Style
    classDef default fill:#1f2937,stroke:#4b5563,stroke-width:1px,color:#f3f4f6;
    classDef ide fill:#1d4ed8,stroke:#3b82f6,stroke-width:2px,color:#eff6ff;
    classDef protocols fill:#111827,stroke:#6b7280,stroke-width:1px,color:#9ca3af;
    classDef teko fill:#7c2d12,stroke:#ea580c,stroke-width:2px,color:#ffedd5;

    %% Source Nodes (IDEs)
    VS[VS Code / Cursor]:::ide
    JB[CLion / IntelliJ / Fleet]:::ide
    NV[Neovim / Vim]:::ide

    %% Unified Protocols
    LSP[Language Server Protocol - JSON-RPC]:::protocols
    DAP[Debug Adapter Protocol - JSON-RPC]:::protocols

    %% Teko Internal Engines
    TKLS[Teko Language Server - tekols]:::teko
    TKVM[Teko VM / Debugger Core]:::teko

    %% Connections
    VS -->|Stdio| LSP
    JB -->|TCP| LSP
    NV -->|Stdio| LSP

    VS -->|TCP| DAP
    JB -->|TCP| DAP

    LSP --> TKLS
    DAP --> TKVM
```

```mermaid
graph TD
    %% Graph Style
    classDef default fill:#1f2937,stroke:#4b5563,stroke-width:1px,color:#f3f4f6;
    classDef phase fill:#111827,stroke:#3b82f6,stroke-width:2px,color:#eff6ff;
    classDef test fill:#064e3b,stroke:#059669,stroke-width:1px,color:#ecfdf5;

    %% Main Nodes
    F1[Phase 1: Type Checker]:::phase
    F2[Phase 2: Intermediate Language IL]:::phase
    F3[Phase 3: VM & Region Allocation]:::phase
    F4[Phase 4: Core Tooling @ Libs]:::phase
    F5[Phase 5: Backend & IR Emission]:::phase

    %% Test Nodes
    T1[tests/test_type_checker.c]:::test
    T2[tests/test_codegen_li.c]:::test
    T3[tests/test_vm.c]:::test
    T4[tests/test_tooling.c]:::test
    T5[tests/test_codegen_native.c]:::test

    %% Dependency Flow
    F1 --> F2
    F2 --> F3
    F3 --> F4
    F4 --> F5

    %% Connections to Tests
    F1 -.-> T1
    F2 -.-> T2
    F3 -.-> T3
    F4 -.-> T4
    F5 -.-> T5
```

---

## 🛠️ Phase 1: Type Checker
*The frontend must guarantee the code is 100% semantically valid before any translation attempt.*

*   **1.1 Inference and Resolution Algorithm:**
    *   Implement the type unification system for inference on `:=` operators (e.g., `wg := waiter` infers the type `teko::waiter`).
    *   Build the complex-type resolver: nested generics (`map<str, mut i32>`), nullables (`ExternalStructure?`), and function arities (`func<i32, void>`).
*   **1.2 Assignment Checking and Mutability Rules:**
    *   Enforce a barrier against invalid assignments (e.g., trying to put a `LIT_STR` literal into an `i32` type).
    *   Connect the AST to the Symbol Table to lock assignments to immutable `let` variables (e.g., fail on a write to a symbol whose `is_mutable` metadata is `false`).
*   **1.3 Async Flow and Concurrency Validation:**
    *   Validate that `await` expressions are applied strictly to returns wrapped in `intent<T>`.
    *   Verify that traditional `defer` blocks do **not** contain `await` expressions, whereas `async defer` blocks require or allow them.
*   **🧪 Associated Tests (`tests/test_type_checker.c`):**
    *   Unity assertions injecting intentional mutability and incompatible-primitive-type errors, validating that the compiler rejects them.

---

## 💾 Phase 2: Intermediate Language (IL) Architecture and Bytecode Emission
*Definition of a compact, platform-independent instruction format, ideal for portability and for feeding our VM.*

*   **2.1 IL ISA (Instruction Set Architecture) Design:**
    *   Design an instruction set based on virtual registers or a stack (e.g., `ICONST`, `STORE_MUT`, `SPAWN_ASYNC`, `CHAN_PUT`, `AWAIT_INTENT`).
    *   Structure the compiled binary file format (`.tkb` - Teko Bytecode) containing: a header with a Magic Number, a Constants Table (literals, strings), Namespace/Type metadata, and the raw instruction vector (*opcodes*).
*   **2.2 The Bytecode Emitter (IL Codegen):**
    *   Create the `src/codegen_li.c` module that walks the validated AST and translates nodes into linear IL instructions.
    *   Map the inline switch and conditionalized `when` blocks into efficient conditional bytecode branches (`JMP_IF_FALSE`).
*   **🧪 Associated Tests (`tests/test_codegen_li.c`):**
    *   Compile small syntactic snippets and inspect the bytes generated in memory to validate that the *opcodes* match the ISA specification.

---

## 🚀 Phase 3: Development Virtual Machine (VM)
*The agile, portable, cross-platform execution environment for the developer, responsible for running `.tkb` bytecode performantly.*

*   **3.1 The Core Interpreter:**
    *   Implement the main execution loop (`src/vm_core.c`) based on an optimized `switch-case` loop (or label pointers if you prefer to optimize in C23) to process IL opcodes.
    *   Build the Context and Call Stack subsystem isolated per Green Thread / coroutine to support async methods natively.
*   **3.2 The VM Concurrency Engine (Green Threads, Channels, and Semaphores):**
    *   Develop the **M:N Scheduler** (M Green Threads mapped onto N native OS threads via `pthread` or the new C23 threads).
    *   Implement the real internal control structures for sync/async channels, mutual-exclusion locks (`mutex`), and `waiter` counters.
*   **3.3 The Real Region-Based Allocator (Region-Based Memory Management):**
    *   Build the native **Memory Arena** engine (the runtime structure that serves the `ctx: arena` of your `main`). Every internal Green Thread allocation and user program data must be pushed into contiguous arena blocks, ensuring that closing the arena clears gigabytes of garbage instantly via O(1) with no Garbage Collector pausing execution.
*   **🧪 Associated Tests (`tests/test_vm.c`):**
    *   Run bytecodes that open channels, fire concurrent loops, and validate that the Scheduler distributes the load and the Arena clears memory perfectly.

---

## 🧰 Phase 4: Core Tooling & Native Framework (Support for `@`)
*Creating the language's standard library and internal tooling, written in the ecosystem and exposed transparently through the `@` syntactic sugar.*

*   **4.1 Mapping and Linking the Internal Namespaces:**
    *   Create the teko header files (e.g., `strings.tk`, `marshall.tk`, `flows.tk`, `lists.tk`, `logger.tk`).
    *   Structure the **Intrinsics / Builtins** subsystem in the compiler: when the Type Checker intercepts an identifier starting with `@` (which we expand to `teko::`), the compiler knows to link that call directly to the high-performance internal functions implemented in the VM runtime or in pure C (FFI).
*   **4.2 Development of the Mandatory `@` Sub-libraries:**
    *   `@marshall`: Pointer conversion functions (`to_ptr`, `from_ptr`) for conversions between Teko types and native C types (FFI).
    *   `@flows`: Event-driven architectural engine for CQRS (`request`, `notify`, `send`), resolving automatic Handler injection.
    *   `@lists` and `@strings`: Manipulation of mutable arrays, dynamic decimal collections, and optimized concatenations.
*   **4.3 The Compiler CLI Driver:**
    *   Create the main terminal utility (`teko compile`, `teko run`).
    *   Inject the `is_stdlib_compilation` privilege-control flag we created. If the CLI driver compiles the compiler/stdlib folder, it turns the flag on as `true` to allow the use of `teko::`; if it's a third-party project, it strictly forces the use of `@`.
*   **🧪 Associated Tests (`tests/test_tooling.c`):**
    *   Compile code containing `@strings.concat` and verify that the syntactic expansion and runtime memory routing are intact.

---

## 🎛️ Phase 5: Advanced Backend and IR Emission (LLVM / C)
*The definitive production stage. When the user needs maximum execution performance (*Ahead-of-Time*), the compiler skips the VM and generates optimized native binaries.*

*   **5.1 Intermediate Transpilation to Pure C or LLVM IR Emission:**
    *   **Transpile-to-C Approach:** Convert the validated AST or the IL instructions directly into structured C code, mapping Green Threads to the `libuv` library or native async Thread constructs, then invoking the system's local `clang` or `gcc` to produce the final binary executable.
    *   **LLVM IR Approach:** Consume the LLVM API to emit textual `.ll` instructions, leveraging industrial register optimizations and generating native code directly for **arm64** (Mac M1/M2/M3) or **x86_64** (Intel/AMD).
*   **5.2 Type Matching and Fixed FFI Generation:**
    *   Generate the exact translation of complex types described in the `extern struct` block or `extern fn ... from "my.dylib" as "GetMy"`, converting arbitrary Teko strings into traditional C `char*` pointers and binding natively via `dlopen`/`dlsym` or direct linking.
*   **🧪 Associated Tests (`tests/test_codegen_native.c`):**
    *   Generate a final binary of a complete Teko program, run it on the host operating system, and inspect whether the output result and concurrent behavior match the specification.

---

# 🗺️ Strategic Plan: The Teko Compiler — From AOT to Self-Control (Self-Hosting)

This document establishes the definitive technical roadmap for the final development phases of the **Teko** language. The central goal is to transform the ecosystem into a purely **autonomous and self-sufficient** systems-level infrastructure, completely eliminating any dependency on third-party compilers and linkers (Clang, GCC, MSVC, Link.exe), and empowering the compiler to generate bare-metal executables directly by writing the structural bytes of operating-system binary files.

---

## 🏗️ Architectural Journey Overview

```
┌──────────────────────────────┐
│  PHASE 6: GLOBAL OPTIMIZATIONS│ ➔ Constant Folding, Inlining, and Flow Analysis      [DONE]
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 7: LINKER ENGINEERING  │ ➔ Direct ELF, Mach-O, and PE/COFF generation         [IN PROGRESS]
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 8: EMBEDDED RUNTIME    │ ➔ Direct Syscalls, Virtual Arena Allocator, Threads  [VALIDATED]
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 9: TECHNICAL DEBT      │ ➔ Build hardening, test coverage, WASM MVP, de-duplication
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 10: WASM CONCURRENCY   │ ➔ Cooperative + wasm-threads backend (delivered — PR #3)
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 11: BROWSER FFI/JS-DOM │ ➔ extern→imports, string-pool data, JS/DOM interop (merged PR #4)
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 12: FRONTEND GRAMMAR   │ ➔ Keyword matrix, literal suffixes, real frontend (current — PR #5)
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 13: NATIVE CRYPTOGRAPHY│ ➔ Symmetric + asymmetric ciphers, hashes, KDFs (complete)
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASES 14–22: LANG SURFACE   │ ➔ Concurrency, OOP, Casting/Conversions, Floating-Point/Numerics,
│  (from the Memorandum roadmap)│   Optionals, Networking/Web, Parsers/Templates, Interop, Native Testing
└──────────────┬───────────────┘
               ▼
┌──────────────────────────────┐
│  PHASE 23: SELF-CONTAINMENT   │ ➔ Compiler Bootstrapping (Rewrite from C to Teko)
└──────────────────────────────┘
```

---

## 🚀 PHASE 6: Global Optimizations of the Metal Backend — *Done*
Phase 6 focuses on expanding the static analysis and reordering engine of the Intermediate Language (IL) in the central orchestrator before dispatching instructions to the CPUs.

### 1. Constant Folding
*   **Mechanics:** The optimizer runs a predictive sweep looking for arithmetic operations whose operands are literals known at compile time (e.g., `OP_ICONST 10`, `OP_ICONST 5`, `OP_ADD`).
*   **Application on Silicon:** The compiler collapses the instructions at build time, computing the result and emitting a single clean load instruction (`OP_ICONST 15`), saving clock cycles at runtime.

### 2. Automatic Static Function Inlining
*   **Mechanics:** The compiler analyzes the control graph looking for small subroutines (small-bytecode "leaf" functions that make no calls to third parties).
*   **Application on Silicon:** The physical branch opcode (`bl`, `call`, `jal`) is replaced directly with a faithful copy of the function body. This eliminates the physical cost of stack frames, Link Register preservation, and pipeline flushes, unlocking the full potential of CSE and DCE in the expanded block.

---

## 🛠️ PHASE 7: Static Native Linker Engineering (`tld`) — *In Progress*
Phase 7 eliminates the call to the host system's `system()` command. The Teko compiler will communicate with kernels by writing the executable binary formats directly to disk.

### 1. Direct Writing of Object Formats and Executable Headers
The backend will abandon textual assembly code generation (.s/.asm) and implement binary emitters to inject the structural metadata tables:
*   **Linux / FreeBSD (ELF64 formats):** Direct writing of ELF headers (`Elf64_Ehdr`), program header sections (`Elf64_Phdr`), and the ELF note marks required by BSD kernel validators.
*   **macOS (Mach-O format):** Writing of architecture headers (`mach_header_64`) and segment load commands (`segment_command_64`).
*   **Windows (PE/COFF format):** Writing of the DOS and NT Header structures (`IMAGE_DOS_HEADER`, `IMAGE_NT_HEADERS`).

### 2. Symbol Resolution and Relocation Mechanism (*The Linking Engine*)
*   **Symbol Resolution:** Map cross-global references, linking the programmer-generated code to the static data sections (`.rodata`/`.rdata`).
*   **Relocation Offsets:** Compute offsets and patch, at link time, the virtual addresses of long conditional jumps (`JMP`) and runtime calls, cementing total independence from external infrastructure.

---

## 🧵 PHASE 8: The Embedded Native Runtime (*Teko Core Runtime*) — *Validated*
To support the native features of massive M:N concurrency, blocking channels, and scope promotion via escape analysis, the language embeds its own low-level static runtime written bare-metal.

### 1. Native Cooperative M:N Concurrency Subsystem
*   **Unix-Like:** Ingest pure system calls via assembly instructions (`syscall` / `svc` / `ecall`), invoking the clone syscall (`sys_clone` on Linux) or native FreeBSD thread management (`thr_new`) to orchestrate the cooperative Green Thread scheduler without loading C's standard libc.
*   **Windows:** Clean static linkage oriented to the export pointers of the `kernel32.dll` bus APIs (such as `CreateThread` and atomic primitives).

### 2. Global Arena Allocation Bus
*   Map and request virtual memory pages directly from the operating system via `mmap`/`munmap` on Unix and `VirtualAlloc` on Windows to feed, in constant $O(1)$ time, the compiler's local arena bus, fully isolating memory scopes.

---

## 🧹 PHASE 9: Technical Debt Resolution
*Promoted to the first post-runtime phase: harden what is already built before expanding the language surface in Phases 12–22. Source: `TECH_DEBT_BACKLOG.md`. To be tackled continuously alongside the feature phases. Priority = (Impact + Risk) × (6 − Effort).*

> ✅ **Build blocker RESOLVED 2026-06-13:** the `teko` executable target in `CMakeLists.txt`
> was missing `target_include_directories(teko PRIVATE src)` (only `teko_core` and `teko_tests`
> had it), so the main compiler binary failed to build (`'teko_target.h' file not found` when
> compiling `src/main.c`). Fixed by adding the missing line; the `teko` binary now builds with
> 0 warnings and runs, and the full test suite still passes (71 tests, 0 failures).

| # | Item | Category | Priority |
|---|------|----------|:--------:|
| 0 | ~~`teko` exe missing include dir → main binary does not build~~ | Infra (build blocker) | ✅ Resolved 2026-06-13 |
| 1 | ~~FFI / generics / AOT modules with no test coverage~~ | Test debt | ✅ Resolved 2026-06-13 |
| 2 | ~~No validation of codegen output per target (16 emitters)~~ | Test debt | ✅ Resolved 2026-06-13 |
| 3 | ~~CI has no Windows runner (PE/COFF path unexercised)~~ | Infra | ✅ Resolved 2026-06-13 |
| 4 | ~~WASM stubbed opcodes — arena implemented; concurrency hooked + deferred (#9)~~ | Code/Arch | ✅ Resolved 2026-06-13 (MVP) |
| 5 | ~~`CMake GLOB_RECURSE` for source collection (stale builds)~~ | Infra | ✅ Resolved 2026-06-13 |
| 6 | ~~Scattered architecture docs / no `ARCHITECTURE.md`~~ | Docs | ✅ Resolved 2026-06-13 |
| 7 | ~~Near-identical codegen emitters (duplication)~~ | Code debt | ✅ Resolved 2026-06-13 — riscv32/64 + x86_64 SysV trio + arm64 GAS trio unified into shared parameterized cores; win_arm64 + Windows x86 kept separate by design (MASM/Intel ≠ AT&T-GAS) |
| 8 | ~~Versioned build artifacts~~ | — | ✅ Closed (was a false positive) |
| 9 | WASM concurrency backend (full spawn/channels) | Code/Arch | ✅ Delivered as **Phase 10** — merged (PR #3): cooperative Layer A + wasm-threads Layer B |
| 10 | ~~Broader emitter de-dup (x86_64 SysV / arm64 GAS)~~ | Code debt | ✅ Resolved 2026-06-13 (win_arm64 separate) |

**Phase 9 status (2026-06-13):** items **0–8 and 10 resolved**; item **7 fully resolved** (riscv32/64 + x86_64 SysV trio + arm64 GAS trio unified into shared cores, Windows MASM/Intel emitters kept separate by design); item **9 delivered as its own feature phase, Phase 10** (below), now **merged via PR #3**. CI is green across the full matrix: native Linux x86_64/arm64, Windows x86_64/arm64, macOS arm64, plus emulated Linux riscv64 (QEMU, non-blocking).

See `TECH_DEBT_BACKLOG.md` for full scoring, business justification, and file paths.

---

## 🧪 PHASE 10: WASM Concurrency Backend — *Delivered (merged via PR #3)*

*Promoted from tech-debt item #9. **Delivered:** Layer A (cooperative M:N — run queue, `call_indirect`, linear-memory channels, mid-function suspension via a `br_table` state machine) and Layer B (`--target=…-wasm-threads` — shared memory + atomics + Web Workers / `worker_threads`). Emitter output is executed end-to-end in CI (Node/wasmtime/Chromium). The design/options below are kept as historical record; see the canonical design doc for the as-built detail.*

**Already delivered (MVP, in this branch):** a real O(1) arena allocator emitted as linear-memory bump code, plus **honest host-runtime hooks** for the concurrency opcodes (`call $teko_spawn` / `$teko_chan_init` / `$teko_chan_put` / `$teko_await`, declared via `(import "teko_rt" ...)`). The emitted module is valid and self-consistent; the concurrency ops simply delegate to a host runtime that does not exist yet.

**Goal of the feature:** make Teko's concurrency model actually run on WASM.

**Design options evaluated (trade-offs):**
1. **Embedded VM / cooperative scheduler compiled to WASM** — reproduce Teko's **M:N green-thread** model on a *single* WASM thread (the same cooperative scheduler the native runtime uses, lowered to WASM). Faithful to the language's semantics; **missing only multicore parallelism**. **Likely the starting point** (no extra proposals required, runs in any WASM engine).
2. **`--target=wasm-threads`** — emit `(import "env" "memory" ... shared)` + the atomics set (`memory.atomic.wait/notify`, `i32.atomic.*`) for blocking channels, and spawn via host **Web Workers**. Adds **real multicore parallelism**, but Workers are **1:1 OS threads, not M:N** — a semantic mismatch with green threads, and it needs per-environment host glue + a threads-capable CI engine.
3. Combine: option 1 for the green-thread scheduler, option 2 as an opt-in for multicore.

**Caveat:** WASM has **no GA stack-switching** proposal, so true green-thread context switches must be synthesized by the compiled scheduler (option 1) rather than using a native primitive.

**Scope for the dedicated PR:** start with option 1 (cooperative scheduler → WASM, single thread) to honour the M:N model, then optionally layer option 2 for parallelism. Provide a minimal host `teko_rt` and an integration test under a WASM engine.

**Full design, opcode lowering, host ABI, test strategy and incremental breakdown:** see [`docs/PHASE10_WASM_CONCURRENCY.md`](./PHASE10_WASM_CONCURRENCY.md).

---

## 🌉 PHASE 11: Browser FFI / JS-DOM Interop — *Feature (current; branch `feat/browser-ffi-interop`, PR #4)*

*New dedicated phase. When targeting browser WASM, give Teko an ergonomic two-way bridge to JS/DOM. `extern fn … from "ns" as "name"` lowers to a WASM `(import "ns" "name" (func …))` via a new `OP_CALL_IMPORT`; the string constant pool is emitted as a real `(data …)` segment (today `OP_SCONST` is a placeholder); DOM/JS access goes through auto-generated host glue — string marshalling (`(ptr,len)` over linear memory), DOM-node handles (a JS handle table), and events (a Teko function-table index registered as a listener). FFI is currently **parsed but discarded** (`parser_visibility.c`); this phase builds the lowering pipeline.*

**Incremental plan** — status (detail in [`docs/PHASE_BROWSER_FFI.md`](./PHASE_BROWSER_FFI.md)):
**MVP-1a** ✅ string-pool `(data …)` + `OP_SCONST` offsets · **MVP-1b** ✅ `extern → (import)` +
`OP_CALL_IMPORT` · **MVP-2** ✅ DOM (`dom.*` multi-arg imports + auto-generated glue) ·
**MVP-3** ✅ JS→Teko events (`dom.on` + exported `teko_invoke`) · **MVP-4** ✅ real allocator
(`teko_alloc`/`teko_free`/`teko_reset` free-list + coalescing) + JS→Teko strings + ergonomic
facade (`<mod>.mjs`) + rich event payload (`dom.on_value`). **FE-A..F** ✅ wire a real
`.tks → IL → WASM` frontend (`teko build … --target=wasm`, no mock): `extern`, `@dom`/`@js`
intrinsics, strings, and `fn` event handlers compile from source. **Phase 11 complete, CI-green.**

---

# 🧬 Roadmap from the Memorandum (Phases 12 & 14–22)

These phases were lifted from the project owner's roadmap memorandum (`TEKO_COMPILER_MEMORANDUM.txt`, Sections 2–4 — the long-term conceptual requirements, the reserved keyword matrix, and the immediate next steps). **Phase 17 (Floating-Point & Numeric Types)** is an owner-requested insertion that closes the float value-model gap Phase 16 deliberately gated; it shifted the old Optionals/Networking/Parsers/Interop/Testing/Self-Hosting phases up by one (now 18–23). They expand the **language surface** that sits on top of the now-validated backend/runtime, and must land before the Self-Hosting milestone. **Phase 13 (Native Cryptography)** is interleaved here as a dedicated, owner-requested phase (not from the memorandum) — it is sequenced after Phase 12 and owns all cipher/hash/KDF work formerly bundled into the networking phase.

---

## 🔤 PHASE 12: Frontend Grammar & Lexer Extension
*The immediate next step from the memorandum: get every new token, AST node, and literal form into the frontend so the feature phases below have a grammar to compile against.*

### 1. Reserved Keyword Matrix (Lexer Tokens)
Inject the full token table the Lexer and Parser must mandatorily process:
*   Resilience: `circuit`, `fallback`, `delayed`, `retry`, `exponential`, `logarithmic`, `attempts`, `timeout`.
*   OOP & concurrency: `class`, `abstract`, `trait`, `event`, `raise`, `subscribe`, `fanout`, `fire_and_forget`, `shared`, `atomic`, `routines`, `duplex`.
*   Web: `api`, `middleware`, `get`, `post`, `put`, `delete`, `rpc`, `websocket`, `use`.
*   Tooling: `parse`, `json`, `csv`, `xml`, `html`, `bundle`, `minify`, `crypto`, `hash`, `encrypt`.
*   Core: `comptime`, `defer`, `soa`, `null`.

### 2. AST Node Mapping
*   Extend the Abstract Syntax Tree (`ast.h`) with nodes representing the new Web, OOP, and cryptographic expressions so the rest of the compiler has a representation to lower.

### 3. Native Literal Suffixes (Literal Add-ons)
*   Captured in the Lexer with zero runtime cost: Time (`ms`, `s`, `m`, `h`, `d`), Data (`b`, `kb`, `mb`, `gb`), and socket Bandwidth (`kbps`, `mbps`, `gbps`).

> **Crypto keywords are reserved here, lowered in Phase 13.** Phase 12 tokenizes
> `crypto`, `hash`, `encrypt`, `decrypt` (and will reserve `sign`/`verify` when added)
> as **reserved — lowering in Phase 13 (Native Cryptography)**. Base encoding
> (`encode`/`decode` over `base64`/`base32`/`hex`) is **not** cryptography and ships
> **functional within Phase 12** (deterministic, no external deps).

---

## 🔐 PHASE 13: Native Cryptography — *Functionally complete (PR #6, branch `feat/phase-13-native-crypto`)*
*A dedicated phase: implement the widest practical set of **symmetric** and **asymmetric**
ciphers natively — no external libraries, no OpenSSL. Pure Teko/C primitives that every
backend (the 16 native emitters + WASM) can emit, with constant-time discipline where it
matters and test-vector-driven proof (NIST/RFC KATs + round-trips). This phase owns the
cryptography moved out of the old "Networking, Web & Cryptography" phase (now Phase 19,
Networking & Web), which consumes these primitives for TLS 1.3.*

> **Status: all sub-phases landed and CI-green.** 13.1 (SHA-2/3, SHAKE, BLAKE3, HMAC,
> incl. legacy MD5/SHA-1 + UUID), 13.3a (CSPRNG, HKDF, PBKDF2), 13.2 (ChaCha20-Poly1305,
> AES-128/192/256 CTR/CBC/GCM), 13.4 (scrypt, BLAKE2b, Argon2), and 13.3b asymmetric
> (X25519, Ed25519, **P-256 ECDH/ECDSA, P-384 ECDH/ECDSA, RSA PKCS#1 v1.5 / OAEP / PSS**).
> Single source of truth = the portable C runtime (`src/runtime/teko_crypto_*.c`), KAT-tested
> (167/167) against NIST CAVP / FIPS 186 / RFC 6979 / RFC 8017 / Project Wycheproof. WASM
> lowering beyond `hash.sha256` (and `uuid.v3`/`v5`) is the documented deferred follow-up
> (compile the C runtime → wasm32 + host entropy import). See `docs/PHASE13_NATIVE_CRYPTO.md`.

**Goal:** the **maximum** practical coverage of symmetric **and** asymmetric ciphers,
all native. **Surface (Teko keywords):** `crypto`, `hash`, `encrypt`/`decrypt`,
`sign`/`verify` (reserved in Phase 12, lowered here), `encode`/`decode` interop with the
Phase 12 base codecs. Each primitive lands with grammar + functional logic + executable
KAT tests — never a dead token.

### Sub-phases (coverage + viability)
- **13.1 — Hashes & MAC.** SHA-256, SHA-512, SHA-3 (Keccak/SHAKE), BLAKE3, and HMAC over
  them. *Foundational* — KDFs, signatures, and AEAD all depend on these. Pure,
  deterministic, KAT-friendly → **most viable native; built first.**
- **13.legacy — Legacy hashes (owner add-on).** **MD5** (RFC 1321) + **SHA-1** (FIPS 180),
  native C + KATs, exposed on the `hash` surface (`hash.md5`/`hash.sha1`) with `.tks` proof.
  ⚠️ **LEGACY / INSECURE — interop/compat only, never for security** (both are collision-broken).
- **13.uuid — UUID/GUID (owner add-on).** Native primitive (token + grammar + functional):
  **v4** (CSPRNG), **v7** (time-ordered), **v5** (SHA-1), **v3** (MD5), **nil** + canonical
  parse/format. KAT the deterministic forms (RFC 4122/9562); structure/version/variant +
  uniqueness for the random ones. (Sequenced after MD5/SHA-1, which v3/v5 depend on.)
- **13.2 — Symmetric / AEAD.** AES-128/192/256 in **CBC / CTR / GCM**, and
  **ChaCha20-Poly1305** (RFC 8439). ChaCha20-Poly1305 is the easiest (no hardware dep,
  portable). AES is viable in software (constant-time/bitsliced); GCM needs GF(2^128)
  carryless multiply. *AES-NI/PCLMUL hardware acceleration is a later optimization, not a
  correctness requirement.*
- **13.3 — Asymmetric.** **RSA** (OAEP, PSS — and PKCS#1 v1.5 for interop), **Ed25519**
  (signatures), **X25519** (key exchange), and **ECDSA / ECDH P-256 / P-384**. Curve25519
  (X25519/Ed25519) is the *most viable native asymmetric* (fixed-field, no bignum). The
  NIST P-curves need modular field + point arithmetic; **RSA is the hardest** — it needs a
  full multi-precision bignum (Montgomery mul, modexp) + careful padding.
- **13.4 — KDF / utility & CSPRNG.** HKDF, PBKDF2 (cheap once HMAC exists), the memory-hard
  **scrypt / Argon2** (harder), and a platform **CSPRNG** (`getrandom` / `BCryptGenRandom` /
  `arc4random` + a WASM host import).

### Build order vs. numbering
The numbering above is the conceptual grouping. The *implementation* order follows
dependencies: **13.1 hashes → CSPRNG + HKDF/PBKDF2 (from 13.4) → 13.2 symmetric/AEAD →
13.3 asymmetric (X25519/Ed25519 first, then P-curves, RSA last)**, with scrypt/Argon2 and
RSA — the heaviest pieces — sequenced toward the end.

### Viability summary (native, no libs)
- **Most viable / early:** SHA-2/SHA-3/BLAKE3, HMAC, HKDF/PBKDF2, ChaCha20-Poly1305,
  X25519, Ed25519, CSPRNG — fixed-width, KAT-verifiable, no bignum.
- **Harder:** AES constant-time + GCM (GF(2^128) carryless mul), ECDSA/ECDH P-256/P-384
  (modular field + point arithmetic), scrypt/Argon2 (memory-hard).
- **Hardest:** RSA (OAEP/PSS) — full multi-precision bignum; gate as the final piece.

*Effort: large, multi-increment, spanning all four sub-phases. Implementation begins only
after Phase 12 is complete.*

---

## 🧵 PHASE 14: Advanced Concurrency, Signaling & Duplex Channels
*Native concurrency primitives beyond the base M:N scheduler delivered in Phase 8.*

*   `routines`: Fire pure background tasks and executions at the runtime level.
*   `duplex chan`: Full-Duplex native channels (symmetric, bidirectional) managing isolated RX and TX buses at the hardware level. Has an internal state machine able to signal a legitimate close (`.close()`) or drops/failures, unblocking and waking threads stuck during a panic and returning structured errors. Can be used as a safe alternative model for consuming isolated dependencies without export.
*   `delayed chan`: Timed channels. Messages receive timestamps and consumer threads are suspended on the Timer Queue, woken by interrupts.
*   `broadcast chan`: Non-destructive 1:N Pub-Sub channels based on registers.
*   Automated Shared Memory: `shared` block and `atomic` control. The compiler transparently injects lightweight locks (Spinlocks/Memory Fences).
*   `circuit` (Circuit Breaker) coupled with `retry` routines:
    *   Support for `exponential` and `logarithmic` backoff algorithms.
    *   Limit by `attempts`, global `timeout`, or both combined.
    *   If both limits are provided, the compiler computes the incremental relative retry time, branching straight to the `fallback` if the time limit is exceeded.

---

## 🧱 PHASE 15: Bare-Metal Object-Oriented Paradigm
*Object orientation with zero runtime reflection overhead.*

*   Support for Concrete, Generic (`<T>` via monomorphization), and Abstract classes.
*   Complete rejection of runtime object Attributes/Annotations.
*   Multiple behavior inheritance implemented via `traits`.
*   Event subsystem (`event`, `raise`, `subscribe`): behavior defined at subscription time — `fanout` (parallel Green Threads) or `fire_and_forget` (forgets).

---

## 🔁 PHASE 16: Casting / Type Conversions & Parsing
*Universal, culture-invariant conversions between types and to/from strings — the connective tissue between Phase 15's type model (primitives, complex types, user-defined classes) and every surface that serializes or displays a value.*

### 1. Conversions between types
*   Explicit and checked conversions across primitives, complex types, and user-defined classes — widening/narrowing, numeric ↔ boolean ↔ char, and type-to-type casts that fail loudly (no silent truncation/UB).

### 2. Parsing to/from strings — WITH or WITHOUT formatting
*   **Without a format → a UNIVERSAL, culture-invariant DEFAULT.** Parsing and stringification with no explicit format use ONE fixed canonical representation (e.g. `.`-decimal, ISO-8601 dates, unambiguous integer/float grammar) that **ignores the OS locale/format configuration entirely** — identical bytes on every machine, every region. (This is deliberately distinct from Phase 14's `time.format_local`, which is *explicitly* OS-locale/DST-aware; the default casting path is locale-INvariant.)
*   **With an explicit format → the developer supplies it.** Specific outputs (currency, grouped digits, custom date masks, radices, precision) come from a format string/spec the developer passes; only then does formatting deviate from the universal default.

### 3. Universal `to_string` (auto-called on concatenation / interpolation)
*   **Every type — primitive, complex, and user-defined (class) — has an automatic `to_string`** producing the culture-invariant default representation. It is **auto-invoked when a value is concatenated with or interpolated into a string** (`"x = " + p`, `"{p}"`), so any value is printable without an explicit call.
*   For **user-defined types**, `to_string` is the **built-in, overridable, inheritable convention method** defined by Phase 15's OOP model (a class may define or inherit `to_string`; a trait may provide a default). Phase 16 **discovers it by name through the OOP method table / vtable and auto-invokes it** — i.e. the auto-`to_string` machinery is Phase-16 work, but the *hook* (a conventional, dispatchable `to_string`) is established in **Phase 15** (see `docs/PHASE15_OOP.md`).
*   Lowering follows the zero-runtime-reflection ethos: the auto-call resolves the concrete `to_string` at compile time (direct call for a concrete static type; a vtable slot for an abstract/trait-typed reference) — never a reflective runtime walk.

---

## 🔢 PHASE 17: Floating-Point & Numeric Types Expansion — *In progress (branch `feat/phase-17-floating-point`, PR #10)*
*The numeric-types expansion that Phase 16 deliberately gated: float formatting needs real `f64`
values in the frontend first. Phase 16's expression evaluator is integer-only (`$w0` is i32 on
WASM / a GPR on native; literals via `atoi`), so a `convert.float_to_str` token today would have no
float value to convert — a dead token, which the discipline forbids. Phase 17 carries floating-point
values end to end, then closes the gate. See [`docs/PHASE17_FLOATING_POINT.md`](./PHASE17_FLOATING_POINT.md).*

### 1. f64 value model (the prerequisite — lands first)
*   Float literals (`3.14`, `1.0`, exponent forms), float locals, float arithmetic (`+ - * / %`),
    and float comparisons flow through the frontend value model alongside the existing integer path.
*   A **parallel float accumulator** is added to both backends so the integer path (and the 16
    native goldens) stays byte-identical: native uses an FP register (`xmm0/xmm1` on x86_64,
    `d0/d1` on arm64); WASM declares `f64` locals (`$f0/$f1`) next to `$w0/$w1`. New, additive
    opcodes (`OP_FCONST`, `OP_FADD/FSUB/FMUL/FDIV/FMOD`, `OP_F*` compares, `OP_I2F`/`OP_F2I`).
*   Checked, fail-loud int↔float casts (no silent truncation/UB), consistent with Phase 16's
    checked-conversion posture.

### 2. Float formatting + parsing (closes the Phase 16 gate)
*   `convert.float_to_str` (reserved runtime **id 50**) — shortest round-trip, culture-invariant
    `.`-decimal (Ryu/Grisu-class core in the single C runtime, KAT-tested; freestanding-safe — no
    `snprintf`/`setlocale`, so the canonical form is locale-INvariant like the rest of Phase 16).
*   `convert.parse_float` (reserved runtime **id 54**) — checked, fail-loud string→f64.
*   Auto-`to_string` for floats wired into `+` / interpolation, completing the Phase-16 core
    deliverable for the float type, on native AND WASM.

### Dependency order & discipline
The **value model lands before** the formatting/parsing surface (a float runtime id is only surfaced
once there is a float value to feed it — no dead tokens). Single C runtime as source of truth
(`teko_convert.c` + a Ryu-class formatter) → native `teko_rt_*` + the WASM compiled-C reactor; one
increment per commit; ASan+UBSan both dispatch paths + TSan; the 16 native goldens never regress;
every surface proven by an executable `.tks` on both targets; all four CI gates green (incl. Windows
MSVC) before "done".

---

## 🎯 PHASE 18: Zero-Overhead Optionals & Compile-Time Metaprogramming

*   Nullability `?T` via packed Value Types. The Elvis operator (`??`) compiles directly to hardware conditional instructions (`je`/`cbz`).
*   `comptime`: Code execution at build time. Metaprogramming happens during compilation.
*   `soa` (Structure of Arrays): An optimization that reorganizes array layouts in RAM to enable pure native SIMD vectorization invisibly.
*   `defer`: Syntactic registration of mandatory scope-closing routines.

---

## 🌐 PHASE 19: Native Networking & Web Architecture
*Comprehensive networking from OSI Layer 4 to Layer 7, plus the native web keyword surface. Cryptography is its own dedicated phase — see **Phase 13: Native Cryptography**; TLS 1.3 below consumes Phase 13's cipher/KDF/CSPRNG primitives.*

### 1. Networking Stack
*   Raw sockets, asynchronous TCP, UDP, and QUIC via io_uring/kqueue/IOCP.
*   Native TLS 1.3 embedded in the IP bus (built on the Phase 13 cryptographic primitives).
*   Integrated polyglot routing and handling spanning HTTP/1.x, HTTP/2, HTTP/3, HTTP/4, bidirectional WebSockets (`ws_chan`), and gRPC (RPC).

### 2. Native Web Architecture by Keywords
*   Expressive syntax for APIs and micro-applications (`api`, `middleware`, `get`, `post`, `put`, `delete`, `rpc`, `websocket`, `use`). Generation of static Radix trees compiled AOT.

---

## 🧩 PHASE 20: Enterprise Parsers & Embedded Template Compiler

*   Linear O(1), reflection-free execution: `parse.json`, `parse.csv`, `parse.xml`.
*   Native Template Engine integrated via rich String Literals: `html"""..."""`.
*   Integrated Bundler and Minifier at compile time: `bundle()` and `minify` commands optimize and embed static CSS/JS/Assets into the `.rodata` section.

> **Design decision — static per-type (de)serializers (Go-style, no runtime reflection).**
> Serialization/deserialization is **generated at compile time as a specialized
> (de)serializer per concrete type, emitted directly** — no runtime reflection, consistent
> with the language's zero-runtime-reflection ethos. The `serialize` / `stringify` tokens
> (and `parse.json`/`.csv`/`.xml`) lower in this phase following this model: for each type
> that crosses a (de)serialization boundary, the compiler emits a dedicated, monomorphized
> encode/decode routine (akin to Go's generated marshalers / `easyjson`), not a generic
> reflective walker. `serialize`/`stringify` are reserved in Phase 12 with this destination.

---

## 🔗 PHASE 21: Interoperability & Rich Metadata (`.teko_meta`)

*   Lookup via `include_paths`, `static_links`, and `dynamic_links` in the `.tkp`.
*   Teko modules embed rich type metadata in the `.teko_meta` section.
*   Pure C objects expose signatures via automatic header (`.h`) parsing.
*   Managed runtimes (non-AOT .NET, JVM) are isolated and handled via IPC. Native static support is guaranteed when .NET uses Native AOT compilation.

---

## 🧪 PHASE 22: Native Testing (`.tkt`) & Code Coverage

*   `.tkt` extension for co-located test files (same tree as the object under test). The release build ignores these files automatically.
*   Native Code Coverage via codegen-assisted instrumentation, injecting counters into RAM at the start of each Basic Block. The linker embeds the `.teko_cov_map` section associating counters with code lines. The runtime dumps the counters at process end in a binary format (`.tkcov`).

---

# 🔄 Final Milestone (Phase 23)

---

## 🔄 PHASE 23: Self-Containment (Self-Hosting / Bootstrapping)
*The final step that crowns the industrial maturity of a systems programming language: using the language itself to compile itself.*

### 1. Translating the Compiler Modules from C to Teko
*   The Frontend (Lexer, Parser, AST Parser) and the Backend (Type Checker, Intermediate Codegen, Metal Codegen, Linker) will be entirely rewritten using the syntax and native features of the Teko language (type safety, strict mutability control, and automatic dependency injection).

### 2. The Bootstrapping Execution Cycle
To certify bit-for-bit stability and total language independence, industrial validation occurs in a closed three-stage cross-compilation cycle:
1.  **Stage 1:** The original stable compiler (written in C) reads the new source code (written in Teko). The generated output is **Compiler Binary A**.
2.  **Stage 2:** **Compiler Binary A** takes over and reads the same Teko source code again. The generated output is **Compiler Binary B**.
3.  **Stage 3 (Validation):** **Compiler Binary B** compiles the Teko source a third time, generating **Compiler Binary C**.
4.  **Final Validation:** **Binary C** and **Binary B** must be rigorously and mathematically **bit-for-bit identical** at the hash checksum level. When this cycle closes, the Teko compiler is **100% autonomous, C-free, and self-contained**.
