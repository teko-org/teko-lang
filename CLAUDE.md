# CLAUDE.md — operating guide for this repo

Teko is an **ahead-of-time (AOT) compiler written in C23**, with no LLVM: a modular
frontend (lexer → parser → type/semantic checks) feeds an **IL bytecode**, which a
polymorphic backend lowers to **16 native emitters** (ELF / Mach-O / PE-COFF) plus a
**WebAssembly** backend. This file is the operational guide; it **references** the
canonical docs rather than duplicating them.

## Canonical docs (read these for depth)
- `docs/plan.md` — the phased roadmap (Phases 1–21; WASM Concurrency = Phase 10 done, Browser
  FFI = Phase 11 merged, Frontend Grammar = Phase 12 current, Native Cryptography = Phase 13 complete).
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
  **Phase 10** (merged via PR #3). Browser FFI / JS-DOM interop = **Phase 11** (merged via PR #4).
  **Phase 12** (Frontend Grammar & Lexer Extension) is the **current phase** (PR #5;
  `docs/PHASE12_FRONTEND_GRAMMAR.md`). **Phase 13 = Native Cryptography** (dedicated,
  owner-requested, **functionally complete & CI-green** on `feat/phase-13-native-crypto`,
  PR #6); the old 13–20 shifted to 14–21 (Self-Hosting = 21).
- **Phase 13 (Native Cryptography) decisions.** Every primitive is **portable C23 in the
  embedded native runtime** (`src/runtime/teko_crypto_*.c`) — the single source of truth,
  KAT-tested in the Unity suite against NIST/RFC vectors; it is *intended* to be linked into
  native targets and is the reference for WASM lowering. **Native runner — Sub-phase A step 1
  landed:** `teko build <f>.tks --target=<native>` now compiles real source through a
  **libc-hosted, assemble-able** emitter (`src/codegen/emit_native_hosted.c`, gated by
  `MetalContext.hosted` so the 16 freestanding goldens are untouched) and drives the system
  `cc` to assemble + link the **`teko_rt`** archive (`runtime/native/teko_rt.c`) into a
  **runnable** executable — proven by `runtime/native/samples/hello.tks` (`emit(...)` →
  `OP_CALL_IMPORT` → `teko_rt_emit`; runs on macOS arm64 + Linux x86_64/arm64; harness
  `runtime/native/run-native.sh` in `native.yml`). The legacy freestanding `--target` path that
  encoded **mock bytecode** still backs non-host/bare-metal arches; the Windows (PE-COFF)
  native runner is future work. The crypto *language surface* now builds on this runner
  (native `OP_CALL_RUNTIME` lowering + `teko_rt_*` wrappers). See
  `docs/HANDOFF_NATIVE_RUNNER_AND_CRYPTO_SURFACE.md`.
  **Constant-time AES = table-free GF(2⁸) arithmetic
  S-box** (field-inverse-by-exponentiation `x^254` over a branchless `gmul` + affine bitwise
  map); **no lookup tables, no secret-dependent branches/indexing** (cache-timing-immune).
  AES-NI/hardware accel is a future optimization, **not** a correctness gate. **WASM crypto
  lowering is DONE (Sub-phase C "big step", 2026-06-15):** the single C runtime is compiled to a
  freestanding **wasm32 "reactor" module** (`runtime/wasm/crypto/build-crypto-reactor.sh`:
  wasm32 clang + `wasm-ld` + a tiny libc shim, **no wasi-sdk**) the emitted Teko module imports
  (namespace `crypto`) and shares ONE linear memory with — NOT hand-emitted WAT per primitive,
  the SAME source of truth as native. Reactor image+heap are linked above Teko's `[0..65536)`
  window (`--global-base=65536 --no-stack-first`), so the allocators never alias.
  `OP_CALL_RUNTIME` ids 5,10-40 lower to `call $crypto_<id>` (gated by a `uses_crypto_ext` flag;
  non-crypto modules stay byte-identical). Entropy = the `env.teko_random` host import
  (`teko_crypto_random.c` `__wasm__` branch). In-module ids 4/6/7/8/9 (sha256/md5/sha1/uuid
  v3/v5) + 41/42/43 (random/uuid v4/v7) remain in-module. Proofs: `runtime/wasm/run-crypto.mjs`
  over `crypto_{hash,hmac,aead,sign,kdf,rsa}.tks` (same KAT vectors as native) in `wasm.yml`.
- **Phase 13 asymmetric block (P-curves + RSA) — DONE (decisions).** Shared **Montgomery
  bignum** (`teko_crypto_bn.c`, CIOS, runtime R²/n′, MSVC-safe 32-bit limbs) exposes a
  limb-level field API; the **NIST P-curves** (`teko_crypto_ec.c` group law +
  `teko_crypto_ecc.c` curve-generic ECDH/ECDSA, instantiated by `teko_crypto_p256.c` /
  `teko_crypto_p384.c`) use the **Renes–Costello–Batina complete (exception-free) formulas**
  (Algorithm 4/6, eprint 2015/1060, transcribed line-for-line) with a **constant-time
  double-and-add ladder** and **RFC 6979** deterministic nonces (HMAC-SHA-256/384). **RSA**
  (`teko_crypto_rsa.c`) sits on `teko_bn_modexp`: PKCS#1 v1.5 sign+encrypt, OAEP (MGF1), PSS.
  **Constant-time posture:** P-curve scalarmult is CT (branchless select; Fermat inverse with
  public p-2 exponent); **RSA private ops are NOT CT** (square-and-multiply modexp) and
  **PKCS#1 v1.5 decryption is padding-oracle-prone** — blinding/CRT and CT decryption are
  documented future work, not a correctness gate (same posture as AES-NI). KATs: NIST CAVP
  CDH, RFC 6979 A.2.5/A.2.6, FIPS 186 SigGen, Project Wycheproof OAEP/PSS. No language
  surface yet (C-runtime + KATs only) — `crypto`/`sign`/`verify`/`encrypt`/`decrypt` token
  lowering is later frontend work; the asymmetric runtime is the source of truth for it.
- **Legacy hashes MD5 + SHA-1 are LEGACY/INSECURE (decision).** Provided as native C
  runtimes (`teko_crypto_md5.c`/`teko_crypto_sha1.c`) + KATs and on the `hash` surface
  (`hash.md5`/`hash.sha1`, in-module WASM, `.tks`-proven) **only** for backward-compat/interop
  (UUID v3/v5, legacy checksums/protocols) — **never** for security (both are collision-broken).
  Documented in the headers + design doc. Security uses → SHA-256/SHA-3/BLAKE3. UUID/GUID
  (nil/v3/v4/v5/v7 + parse/format) is a native Phase 13 primitive built on these + the CSPRNG.
- **Serialization = static per-type generators, no runtime reflection (decision).** (De)serialization
  is generated at compile time as a specialized, monomorphized (de)serializer **per concrete type**,
  emitted directly — Go-style generated marshalers, never a runtime reflective walker — consistent with
  the language's zero-runtime-reflection ethos. `serialize`/`stringify` (and `parse.json`/`.csv`/`.xml`)
  lower this way in **Phase 18** (Enterprise Parsers); the tokens are reserved in Phase 12 with that destination.
- **Browser FFI backend AND a real `.tks` frontend are built.** The WASM backend lowers the
  full Browser FFI surface (imports, DOM, events, allocator, facade), and the frontend now
  compiles real source for it: `teko build <f>.tks --target=wasm` lexes/parses/lowers the
  interop subset (`extern`, `@dom`/`@js`, strings, `fn` event handlers) to IL → `.wat` with
  **no mock bytecode** (`frontend_interop.c` + `codegen_li_wasm.c`; reuses the real lexer +
  `parse_extern_declaration`). General expressions / named locals are future work; backend
  features beyond the source subset remain exercised via the `emit-demo/*.c` drivers.

## Current state / next
Phases 10 (WASM concurrency) and 11 (Browser FFI / JS-DOM interop) are **merged and CI-green**
(PR #3, PR #4). **Phase 13 (Native Cryptography) is FULLY COMPLETE and CI-green** on
`feat/phase-13-native-crypto` (PR #6, ready to leave draft) — runtime + native surface + WASM
surface, all three:
- **Runtime:** the full hash/MAC, KDF, symmetric/AEAD, memory-hard, and **asymmetric** (X25519,
  Ed25519, P-256 & P-384 ECDH/ECDSA, RSA PKCS#1 v1.5 / OAEP / PSS) primitives as KAT-tested
  portable C runtimes (Unity suite 167/167).
- **Native surface:** `crypto`/`hash`/`hmac`/`kdf`/`uuid`/`random` tokens lower (via
  `OP_CALL_RUNTIME` → `teko_rt_*` over the linked C runtime) to runnable binaries, each with an
  executable `.tks` KAT (`runtime/native/run-native.sh`, `native.yml`).
- **WASM surface (Sub-phase C "big step"):** the SAME C runtime is compiled to a wasm32 reactor
  (`runtime/wasm/crypto/`) the emitted module imports + shares memory with; ids 5,10-40 lower to
  `call $crypto_<id>`, entropy via `env.teko_random`. Proven by `runtime/wasm/run-crypto.mjs`
  (32 KAT vectors, `wasm.yml`). **No dead crypto tokens on any target.**

All four gates green (167/167; ASan/UBSan both dispatch paths + TSan; 16 native goldens intact).
Phase 12 (Frontend Grammar & Lexer Extension) work continues on its own branch (PR #5). See
`docs/PHASE13_NATIVE_CRYPTO.md`, `docs/HANDOFF_NATIVE_RUNNER_AND_CRYPTO_SURFACE.md`, and
`docs/PHASE12_FRONTEND_GRAMMAR.md`.
