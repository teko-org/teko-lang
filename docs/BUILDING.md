# Building Teko

Technical instructions for compiling the Teko toolchain and for building Teko projects with it.

## 1. Prerequisites

| Requirement | Notes |
|---|---|
| CMake ≥ 3.20 | |
| C23 compiler | clang (Apple clang on macOS, clang/gcc on Linux, clang + lld on Windows) |
| Host `cc` on `PATH` | `teko build` emits C and invokes the host compiler to link generated programs |

No other dependencies: the compiler links **nothing beyond libc** (no libm on macOS/Windows, no pthread, no third-party libraries). On Windows, `kernel32` is used for directory enumeration.

## 2. The execution model

Native is the sole execution engine, in the toolchain and in generated programs:

- `teko build` lowers the checked program to C, then invokes `cc` for an optimized release
  binary. Production path.
- `teko run` builds the same checked program natively under `-O0` and executes it
  immediately in-process. Development path.
- `teko test` runs the project's `#test` functions natively (see §6 "The test gate").

All three go through the same native codegen; there is no second engine to keep in agreement.

## 3. Building the bootstrap compiler

```sh
cmake -B build
cmake --build build
```

This produces:

- `build/teko` — the compiler, built from the C23 bootstrap mirror (`src/**/*.c`)
- `libteko_bootstrap.a` — the mirror archived as a static library

Notes baked into the CMake configuration:

- The `teko` executable links `src/runtime/teko_rt.c` (the same runtime source it hands to `cc` for generated programs) and gets a **64 MB stack** on all platforms — the compiler's own recursive-descent parser and checker use deep native recursion when processing the corpus during `teko test`.
- `TK_RT_DIR`/`TK_SRC_DIR` compile definitions tell the driver where the runtime lives, so generated programs can be compiled from any working directory.

## 4. Self-hosting — THE validation gate

```sh
cmake --build build --target selfhost
# equivalently: ./build/teko build .   (from the repo root)
```

This makes the bootstrap compiler compile **its own Teko source corpus** (`src/**/*.tks`) → ~1.5 MB of C → `cc` → `bin/teko`, a fully self-hosted compiler. It is the project's end-to-end proof: read → lex → parse → check → test gate → native codegen → link.

The self-hosted binary rebuilds itself to a **byte-identical fixpoint**: generation 2 == generation 3.

```sh
./bin/teko build . -o /tmp/gen2      # the self-hosted compiler rebuilding itself
```

## 5. Verification gate for changes (MANDATE)

After **any** compiler change, verify both engines and the fixpoint before considering the change done:

```sh
./build/teko build . -o bin          # C bootstrap: full build + .tkt test gate
./bin/teko  build . -o /tmp/gen2     # self-hosted: same, natively
# and: the two generated C outputs must be byte-identical after gensym normalization
```

Do **not** rely on the `selfhost` CMake target alone as evidence a change is safe — always run the test gate through both engines. Stale binaries are misleading: rebuild `build/teko` and `bin/teko` fresh before trusting their output.

## 6. Building and running Teko projects

A project is a directory containing a `*.tkp` manifest (TOML — see [../teko.tkp](../teko.tkp) for a commented reference) and a source tree.

```sh
teko build <dir>              # type-check, run tests, emit native binary
teko build <dir> -o <outdir>  # choose the output directory
teko run   <dir>              # native debug build (-O0) and immediate execution
teko test  <dir>              # run #test functions (files ending in .tkt)
teko test  <dir> --coverage   # also emit a Cobertura cobertura.xml report
```

### The test gate

`teko build` **always** runs the project's `#test` functions before codegen:

- a failing assertion bars the build (fail-fast, the failing test named);
- coverage below the manifest's `[coverage]` floors (default **80%**; function/line/branch keys) bars the build;
- `#test` functions are stripped from the emitted binary;
- projects with no `.tkt` files skip the gate cleanly.

### Artifacts

- Binaries land in the output directory together with a `<binary>.tsym` symbol map (used to resolve native panic stack traces back to Teko `name (file:line)`).
- Library artifact kinds (`static`, `shared`, `package`) and the `.tkl` package format (a ZIP of `.tkh` + `.tkb` + `.tsym`) are declared in the manifest's `[artifact]` table.

## 7. Continuous integration

Two workflows run on PRs targeting `main` (see `.github/workflows/`):

| Workflow | What it checks |
|---|---|
| `pr.yml` | Everything a PR must prove. One `artifact` lane per published asset builds the compiler natively; **every other lane consumes that artifact instead of rebuilding it**, which is why they all sit behind `needs: artifact`. On top of it: the per-asset test and CLI-surface lanes, the full regressor, `ar` validation, cross-arch determinism, seed debut, the sanitizers (TSan, ASan/UBSan, LSan, `TEKO_MEM_PARANOID`) and the clang-tidy security audit — reported through five aggregators. |
| `codeql.yml` | GitHub CodeQL analysis. Independent by design: it analyses the C runtime, has no dependency on a Teko artifact, and teaching it to read Teko is post-linker work. |

`nightly.yml` is not a PR gate — it runs on pushes to `main` and publishes the prerelease. `release.yml` promotes an already-proven build to its tag; it compiles nothing.

To reproduce the SAST gate locally on macOS, use LLVM 18 (`brew install llvm@18`) — the default `llvm` keg produces noisy `ArrayBound` false positives that CI's clang-18 does not.

## 8. Troubleshooting

- **`teko test` crashes with a stack overflow** — you are running a binary linked without the 64 MB stack option; rebuild via CMake rather than invoking `cc` by hand.
- **Self-hosted output differs from the bootstrap's** — normalize gensym temp numbers before diffing; only the one-time C→self-host transition differs cosmetically. Gen-2 vs gen-3 must be byte-identical with no normalization.
- **Linker errors about `__divti3`/`__floattidf` on Windows** — ensure `src/win32_int128_builtins.c` is part of the build (it provides the compiler-rt builtins clang does not auto-link on the MSVC ABI).
