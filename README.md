# 🌀 Teko Programming Language

[![Native (hosts build & test)](https://github.com/schivei/teko-lang/actions/workflows/native.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/native.yml)
[![WASM Layer A (cooperative)](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml)
[![WASM Layer B (wasm-threads)](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml)
[![Sanitizers & stress guards](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml)

| Area | Workflow | Status |
|------|----------|--------|
| Native hosts (build & test, 5 runners + riscv64-QEMU) | `native.yml` | [![Native](https://github.com/schivei/teko-lang/actions/workflows/native.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/native.yml) |
| WASM Layer A — cooperative (wasmtime + headless Chromium) | `wasm.yml` | [![WASM A](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml) |
| WASM Layer B — multicore (worker_threads + Web Workers) | `wasm-threads.yml` | [![WASM B](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml) |
| Sanitizers (ASan/UBSan both dispatch paths, TSan) + Windows stress | `sanitizers.yml` | [![Sanitizers](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml) |

![teko.jpeg](docs/teko.jpeg)

Teko is a high-performance, ahead-of-time (AOT) compiled, general-purpose programming language tailored for modern systems programming. It combines the raw execution speed of C, the type safety and clean architecture of modern compilers, and a zero-overhead **Region-Based Memory Management (O(1) Arenas)** to eliminate both manual `free()` errors and garbage collection pauses.

---

## ✨ Key Features

*   **⚡ Bare-Metal Execution:** Fully independent compiler infrastructure that skips heavy third-party backends (like LLVM) to emit optimized machine code directly to target architectures.
*   **🧠 Zero-Cost Region Memory:** Automatic compile-time Escape Analysis triggers stack allocation or promotes data to deterministic O(1) stack-allocated sub-arenas. Megabytes of local memory are recycled instantly in a single clock cycle.
*   **🧵 Native M:N Concurrency:** Lightweight cooperative green threads (`using` blocks and channels) baked directly into the instruction set and routed dynamically to native OS architectures.
*   **🗺️ Clean Platform Isolation:** Highly decoupled backend structure strictly segregating architecture and system conventions (Apple Silicon, macOS Intel x86_64, Linux ELF, Windows PE/COFF, FreeBSD, and WebAssembly).
*   **🛠️ Multi-Mode Tooling:** Robust CLI compiler driver providing precise compilation interrupts via standard industry flags (`-S` for pure text assembly, `-c` for unlinked binary object files).

---

## 🎯 Supported Targets

Teko's backend is a polymorphic router (`src/codegen/codegen_metal.c`) that dispatches
each `(OS, architecture)` pair to a dedicated emitter. There are **16 emitters** — 15
native families plus WebAssembly. Targets are selected by a triple passed to
`--target` (matched by substring in `src/teko_target.c`).

**Honest status — three real tiers** (the *emitted native code* is never executed;
only the compiler itself, and the WASM modules, run):

- 🟢 **CI host** — the compiler is **built and the whole test suite is run** on this
  OS/architecture in CI (`native.yml`). Every emitter's emission goldens run here too.
- 🟡 **Emission only** — there is **no CI runner** for this OS/arch; the emitter is
  validated by **golden tests** (prologue / arithmetic / syscall bytes asserted) that
  execute on the CI hosts. The compiler is not built or run here, and the emitted
  assembly is not assembled, linked, or executed anywhere.
- 🔵 **Executed** — the emitted module is **built and run end-to-end** in CI with its
  result asserted (WebAssembly only).

| OS | Architecture | Emitter (`src/codegen/…`) | Backend | Tier | What is actually verified |
|----|--------------|---------------------------|---------|------|---------------------------|
| Linux | x86_64 | `linux/emit_linux_x86_64.c` | ELF (SysV) | 🟢 CI host | Compiler built + suite run (Clang/Ninja, `ubuntu-latest`); shares the unified x86_64-SysV emitter core |
| Linux | arm64 / aarch64 | `linux/emit_linux_arm64.c` | ELF | 🟢 CI host | Compiler built + suite run on `ubuntu-24.04-arm`; AArch64 GAS core |
| Linux | riscv64 | `linux/emit_linux_riscv64.c` | ELF | 🟢 CI host (QEMU) | Cross-compiled static + suite run under QEMU user-mode (non-blocking); shared riscv core |
| macOS (Darwin) | arm64 / Apple Silicon | `apple/emit_darwin_arm64.c` | Mach-O | 🟢 CI host | Compiler built + suite run on `macos-latest` (AppleClang); Mach-O writer asserted |
| Windows | x86_64 | `windows/emit_win_x86_64.c` | PE/COFF | 🟢 CI host | Compiler built + suite run (MSVC), **plus 20× bare-Release stress** (`sanitizers.yml`) |
| Windows | arm64 | `windows/emit_win_arm64.c` | PE/COFF | 🟢 CI host | Compiler built + suite run on `windows-11-arm` (MSVC) |
| macOS (Darwin) | x86_64 (Intel) | `apple/emit_darwin_x86_64.c` | Mach-O | 🟡 Emission only | Mach-O emission golden — GitHub retired the Intel macOS runners, so not built/run here |
| Linux | x86 / i386 | `linux/emit_linux_x86.c` | ELF | 🟡 Emission only | 32-bit SysV ELF prologue/arithmetic golden; no 32-bit runner |
| Linux | arm32 / armv7 | `linux/emit_linux_arm32.c` | ELF | 🟡 Emission only | ARM EABI emission golden; no runner |
| Linux | riscv32 | `linux/emit_linux_riscv32.c` | ELF | 🟡 Emission only | RV32 emission golden (shared riscv core, byte-diff-verified vs RV64); no runner |
| Linux | mips | `linux/emit_linux_mips.c` | ELF | 🟡 Emission only | MIPS emission golden; no runner |
| Linux | ppc64 / powerpc64 | `linux/emit_linux_ppc64.c` | ELF | 🟡 Emission only | PowerPC64 emission golden; no runner |
| Windows | x86 / i386 | `windows/emit_win_x86.c` | PE/COFF | 🟡 Emission only | 32-bit PE/COFF emission golden; no 32-bit Windows runner |
| FreeBSD | x86_64 | `bsd_unix/emit_freebsd_x86_64.c` | ELF | 🟡 Emission only | ELF emission golden incl. the FreeBSD `PT_NOTE` ABI tag; no FreeBSD runner |
| FreeBSD | arm64 | `bsd_unix/emit_freebsd_arm64.c` | ELF | 🟡 Emission only | AArch64 ELF emission golden (FreeBSD ABI); no runner |
| WebAssembly | wasm32 / wasm64 | `bare_metal/emit_wasm.c` | WASM (WAT) | 🔵 Executed | Built **and run** end-to-end — see below (`wasm.yml`, `wasm-threads.yml`) |

> Note: the in-memory ELF / Mach-O / PE-COFF object writers (`src/codegen/tld_*.c`) are
> exercised by assertions on the produced bytes (e.g. the ELF e2e test verifies the
> header + injected relocation), but the resulting binaries are not run as processes.

### WebAssembly targets — the only family executed end-to-end in CI

[![WASM Layer A](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm.yml)
[![WASM Layer B](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/wasm-threads.yml)

- **Layer A — cooperative (default `wasm`/`wasm32-wasi`).** Single WebAssembly thread
  reproducing the M:N green-thread model entirely *inside* the module: an in-memory run
  queue + function table, `call_indirect` dispatch, linear-memory channels, and
  mid-function suspension via a state machine (`OP_FUNC_BEGIN`/`END`, `OP_CHAN_GET`).
  No host runtime required. Executed in CI under **Node + wasmtime + headless Chromium**
  (`channels=42`, `scheduler=15`, real backend output `emitted=7`, suspension `=30`,
  multi-spawn contention `=15`).
- **Layer B — real multicore (`--target=…-wasm-threads`).** Opt-in parallelism: imports
  a `shared` memory, lowers channels to the **atomics** proposal, and delegates `spawn`
  to a host `teko_rt.spawn` that starts a real OS thread (Web Worker / `worker_threads`)
  which re-instantiates the module against the shared memory. Executed in CI under
  **node `worker_threads`** and **headless Chromium Web Workers** with COOP/COEP
  (`threads=777`, real backend output `emitted_threads=99`). Layered on top of A, not a
  replacement (Workers are 1:1 OS threads, not M:N).

### What CI actually exercises

[![Native](https://github.com/schivei/teko-lang/actions/workflows/native.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/native.yml)
[![Sanitizers & stress](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml)

- **Compiler built & run (host) — gating** (`native.yml`): Linux x86_64, Linux arm64,
  macOS arm64, Windows x86_64, Windows arm64. Plus **Linux riscv64 under QEMU**
  (non-blocking). Every emitter's emission golden runs on these hosts.
- **WASM executed — gating** (`wasm.yml`, `wasm-threads.yml`): `wasm-wasmtime`
  (Node + wasmtime), `wasm-browser` (Chromium, COOP/COEP), `wasm-threads-node`
  (`worker_threads`), `wasm-threads-browser` (Web Workers).
- **Sanitizers & stress — gating** (`sanitizers.yml`): `asan-ubsan` (Release + ASan +
  UBSan on **both** VM dispatch paths — computed-goto and portable `switch`),
  `tsan` (ThreadSanitizer regression guard — it caught the FFI wild-free that ASan
  could not), and `windows-stress` (the bare Release suite run **20×** on Windows).
- **Not in CI today:** macOS x86_64 (Intel runners retired), 32-bit (x86 / arm32 /
  riscv32), mips, ppc64, and FreeBSD have **no host runner**, so their emitters are
  validated by emission goldens only — not by building/executing the compiler on that
  platform, nor by running the emitted native code anywhere.

## 📂 Project Directory Structure

```text
teko/
├── src/
│   ├── main.c                          # CLI compiler driver & option parsing
│   ├── codegen/                        # Native Bare-Metal Backend Core
│   │   ├── codegen_metal.h             # Orchestrator central definitions
│   │   ├── codegen_metal.c             # Polymorphic OS/CPU instruction router
│   │   ├── codegen_opt.h / .c          # Escape Analysis & optimization pipeline
│   │   ├── apple/                      # macOS Darwin Ecosystem (Mach-O)
│   │   │   ├── emit_darwin_arm64.c     # Apple Silicon M-Series
│   │   │   └── emit_darwin_x86_64.c    # Intel Mac legacy support
│   │   ├── linux/                      # Linux Ecosystem (ELF ABI)
│   │   │   ├── emit_linux_x86_64.c     # 64-bit Intel/AMD Linux
│   │   │   └── (other Linux architectures...)
│   │   ├── windows/                    # Windows Ecosystem (PE/COFF)
│   │   └── bare_metal/                 # Virtual and Embedded Targets (WASM WAT)
│   └── (frontend/lexer/parser...)
└── tests/
    ├── test_main.c                     # Unity framework automated executor
    └── codegen/                        # Isolated backend sanity smoke tests
        ├── test_codegen_apple.c        # Apple Silicon and Intel validations
        ├── test_codegen_linux.c        # Multi-architecture ELF validations
        ├── test_codegen_windows.c      # Win32 & Win64 structural assertions
        └── test_codegen_unix.c         # FreeBSD system trap validations
```

---

## 🛠️ Building and Testing

### Prerequisites
Make sure you have a standard C compiler (GCC, Clang, or MSVC) and CMake installed on your machine.

### Compilation
To build the CLI driver compiler and the test engine, use the following native instructions:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j\$(nproc)
```

### Running Automated Test Suite
To run all cross-platform compiler smoke tests via the Unity framework:

```bash
./teko_tests
```

### Compiling a Teko Project
You can build your native entry-point manifests using the CLI driver options:

```bash
# Generate pure text assembly for audit (.s / .asm)
./teko build path/to/project.tkp -S --target=aarch64-apple-darwin

# Generate object binary files without final linking (.o / .obj)
./teko build path/to/project.tkp -c --target=x86_64-unknown-linux-gnu

# Generate a clean production bare-metal native executable
./teko build path/to/project.tkp -o my_app
```

---

## 📜 License
Teko is released under the MIT License. Feel free to use, modify, and distribute.
