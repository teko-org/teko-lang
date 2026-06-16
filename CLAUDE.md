# CLAUDE.md — operating guide for this repo

Teko is an **ahead-of-time (AOT) compiler written in C23**, with no LLVM: a modular
frontend (lexer → parser → type/semantic checks) feeds an **IL bytecode**, which a
polymorphic backend lowers to **16 native emitters** (ELF / Mach-O / PE-COFF) plus a
**WebAssembly** backend. This file is the operational guide; it **references** the
canonical docs rather than duplicating them.

## Canonical docs (read these for depth)
- `docs/plan.md` — the phased roadmap (Phases 1–23; WASM Concurrency = Phase 10 done, Browser
  FFI = Phase 11 merged, Frontend Grammar = Phase 12 current, Native Cryptography = Phase 13
  complete/merged, Advanced Concurrency = Phase 14 in progress —
  `docs/PHASE14_ADVANCED_CONCURRENCY.md`).
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
  PR #6); the old 13–20 shifted to 14–21. A later **Phase 16 = Casting / Type Conversions &
  Parsing** was inserted (owner-requested), shifting 16–21 → 17–22. A further owner-requested
  **Phase 17 = Floating-Point & Numeric Types** (closes the float gap Phase 16 gated) was then
  inserted, shifting 17–22 → 18–23 (Self-Hosting = **23**); see `docs/plan.md`.
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
  lower this way in **Phase 20** (Enterprise Parsers); the tokens are reserved in Phase 12 with that destination.
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

**Phase 14 (Advanced Concurrency, Signaling & Duplex Channels) is IN PROGRESS** on
`feat/phase-14-advanced-concurrency` (PR #7, draft) — `docs/PHASE14_ADVANCED_CONCURRENCY.md`.
Sub-block **14.A `routines` (background tasks) is DONE** on both targets (no dead token): a
`routines { foo(); bar(); }` block fires each enclosed call as a cooperative green thread.
- **WASM:** reuses the Phase-10 Layer-A run queue; `call $teko_sched_run` is emitted at `$main`
  close (gated by `uses_spawn`) so fired routines drain before exit. Proof
  `runtime/wasm/run-routines.mjs`.
- **Native runner:** the hosted emitter went multi-function — `OP_FUNC_BEGIN/END` emit
  `teko_routine_<slot>` functions after `$main`, `OP_SPAWN_ASYNC` → `teko_rt_spawn`, a routine
  function-pointer table is emitted, and `teko_rt_run` drains at HALT. The scheduler is a
  **separate TU** (`runtime/native/teko_rt_sched.c`) so the linker pulls it ONLY for routines
  programs — crypto binaries stay byte-identical. Proof `runtime/native/samples/routines.tks`.
- **Decision (14.A):** `routines` MVP = run-to-completion cooperative tasks, drained at program
  exit (implicit join-at-exit). Routine/handler bodies now lower plain extern + codec/crypto
  calls (not only `@dom`). Blocking/suspending rendezvous between routines is 14.B+ work.
- 167/167 → 168/168 (added `test_frontend_interop_routines_spawn`); ASan/UBSan both dispatch
  paths + TSan green; 16 native goldens + all crypto native/WASM proofs intact.
Sub-block **14.B `duplex chan` is DONE** on both targets: a symmetric full-duplex channel
(two isolated rings + OPEN/CLOSED/DROPPED state machine, structured non-blocking statuses) as
a C runtime source of truth (`src/runtime/teko_duplex.c`, 6 Unity KATs) wired to the language
via dedicated **OP_DUPLEX_*** opcodes (`duplex.open/send/recv/poll/close`, a dotted-identifier
surface — bare `duplex` stays the keyword). Native lowers to `teko_rt_duplex_*`; WASM compiles
the SAME C runtime into the reactor and imports it (pure i32 handle/value/status, shared
memory). Proofs `runtime/native/samples/duplex.tks` + `runtime/wasm/run-duplex.mjs`. Suite 174/174.
Sub-block **14.C `delayed chan` is DONE** on both targets: timed/timestamped messages on the
**REAL monotonic clock** (absolute ns deadlines; the logical `delayed.advance` was removed in the
real-time correction), released in deadline order — C runtime `src/runtime/teko_delayed.c` (4 KATs,
`now_ns` passed in) → dedicated `OP_DELAYED_*` (`delayed.*`) → native `teko_rt_delayed_*` + WASM
reactor (both read `teko_rt_now_ns`/`env.teko_now_ns`). Proofs `runtime/native/samples/delayed.tks`
+ `runtime/wasm/run-delayed.mjs` (real-deadline order + elapsed lower bound).
Sub-block **14.D `broadcast chan` is DONE** on both targets: non-destructive 1:N pub-sub (ring +
per-subscriber cursors; publish-once/read-by-all) — C runtime `src/runtime/teko_broadcast.c`
(4 KATs) → dedicated `OP_BCAST_*` (`broadcast.*` dotted-ident) → native `teko_rt_bcast_*` + WASM
reactor. Proofs `runtime/native/samples/broadcast.tks` + `runtime/wasm/run-broadcast.mjs`. Suite 184/184.
Sub-block **14.E `shared` block + `atomic` is DONE** on both targets: a `shared { }` coarse-locked
critical section (NEW block grammar → `OP_SHARED_ENTER/LEAVE`) + `atomic.*` cells (`OP_ATOMIC_*`)
— C runtime `src/runtime/teko_shared.c` (3 KATs; portable atomics without `<stdatomic.h>` —
`__atomic`/`_Interlocked`/plain-wasm, MSVC-safe + TSan-clean) called directly (register-width
ABI). Proofs `runtime/native/samples/shared.tks` + `runtime/wasm/run-shared.mjs`. Suite 188/188.
Sub-block **14.F `circuit`+`retry` is DONE** on both targets. `src/runtime/teko_retry.c` (6 KATs:
exp/log backoff, attempts+timeout→fallback rule, breaker CLOSED/OPEN/HALF_OPEN) is the source of
truth; the `retry (attempts N, [timeout T,] exponential|logarithmic, base B) { } fallback { }` and
`circuit cb { } fallback { }` BLOCK grammar drives it via the control-flow foundation (a
should_continue-gated attempt loop; circuit_allow guard + record; `circuit(threshold, cooldown)`
breaker constructor). Opcodes `OP_RETRY_*`/`OP_CIRCUIT_*` (0x62-0x67) → native
`teko_rt_retry_*`/`teko_rt_circuit_*` / WASM reactor imports (teko_retry.c added to the reactor).
All 7 reserved tokens (retry/fallback/attempts/timeout/exponential/logarithmic/circuit) are LIVE.
Proofs `runtime/native/samples/resilience.tks` + `runtime/wasm/run-resilience.mjs`.
Sub-block **control-flow foundation is DONE**: `while`/`loop`/`if`/`break`/`continue` + local
reassignment lower from source (structured `OP_LOOP_*`/`OP_IF_*`/`OP_BREAK*` 0x5B-0x61; native asm
labels + WASM `(block (loop))`/`(if end)`). A shared block-body dispatcher is reused by loop/if AND
routine bodies (loops inside a background `fn`). Proofs `controlflow.tks` native + WASM.
Sub-block **14.G `await`/`wait` timespan waiters is DONE** (on the **real monotonic clock**):
`wait <ts>;` (real-time spin, `teko_rt_wait_ns` / WASM i64 deadline loop on `env.teko_now_ns`) +
`await <ts>;` (same, plus `$teko_sched_run` drain — `teko_rt_await_ns`); both NON-BLOCKING (only
the time source is real). Opcodes `OP_WAIT` 0x59 / `OP_AWAIT_FOR` 0x5A; timespan literals normalize
to canonical ms at compile time (adopted in channel delay args). Proofs
`waiters.tks` native + WASM.
Sub-block **14.H real `.tks` capstone is DONE**: one program combining functions + a background
routine with a LOOP + main loops + atomic (14.E) + delayed channel (14.C) + await/wait (14.G) →
[15,6,1,2,3,42]. Proofs `capstone.tks` native + WASM.
Sub-block **14.I `routines` ARGUMENTS (Go-style) is DONE** (owner follow-up): a `routines {
worker(a, b, …); }` task takes arbitrary args; `fn worker(a, b)` binds each param. Opcodes
`OP_SPAWN_ASYNC_ARGS` (0x68, argc — args staged in `$a0..`) + `OP_LOAD_SPAWN_ARG` (0x69, idx).
Native: per-task arg vector in `teko_rt_sched` + an args-pointer routine ABI; WASM: args in the task
spill frame. This enables a **real concurrent producer/consumer**: a producer task and a consumer
share a channel by passing its handle (the channel's backing store lives in the runtime), and the
consumer drains with a structured **poll** loop (`poll → if ready recv else stop/await`) — no
blocking-recv/`select` needed. The Phase-10 `OP_SPAWN_ASYNC` (in-module channel `arg=$cp`) is
untouched. Proof `producer_consumer.tks` native + WASM → 15.
**Real-time clock (owner pre-merge correction) is DONE**: waiters/delays/timeouts run on a **real
MONOTONIC ns clock** (`teko_rt_now_ns` = CLOCK_MONOTONIC / QueryPerformanceCounter; WASM imports
`env.teko_now_ns`), replacing the logical clock — keeping cooperative NON-BLOCKING scheduling (only
the time source changed). Runtimes stay clock-agnostic + KAT-deterministic (time passed in); time
tests assert real-elapsed lower bounds + ordering (no exact counters).
**Wall-clock / timezone surface is DONE** (owner ask): OS-sourced CIVIL time (distinct from the
monotonic clock) — `time.now_unix()` (epoch secs as a decimal string, user can do math),
`time.now_local()`/`now_utc()`, `time.format_local(epoch)`/`format_utc(epoch)` → ISO-8601, system
local zone + DST by default. `src/runtime/teko_time.c` = portable formatter (source of truth,
KAT-able); the OS supplies the timestamp + DST-correct local offset (native `time()`+`localtime`;
WASM reactor imports `env.teko_now_unix`/`env.teko_tz_offset`). Surface = OP_CALL_RUNTIME ids 44-48
(reactor-backed on WASM). Proofs `time.tks` native + WASM (`run-time.mjs`, stubbed clock).
**PHASE 14 IS COMPLETE** — 14.A–14.I + control-flow foundation all done & CI-green on all four
gates; no dead tokens (every reserved concurrency/resilience keyword is live with an executable
`.tks` proof on native AND WASM); routines support real producer/consumer via Go-style args; all
timing on the real monotonic clock. Suite 200/200; ASan/UBSan (both dispatch paths) + TSan green;
16 native goldens intact. **Ready to leave draft** (PR #7; the human merges — no merge/force-push
from the agent). Continuation/history: `docs/HANDOFF_PHASE14.md`.

**Phase 15 (Bare-Metal Object-Oriented Paradigm) is IN PROGRESS** on `feat/phase-15-oop` (PR #8,
draft) — `docs/PHASE15_OOP.md`. Object orientation with **zero runtime reflection** (compile-time
field indices + method slots; no RTTI/attribute bags). Sub-block **15.A `class` (concrete) is DONE**
on both targets (LIVE token, executable `.tks` proofs):
- **Object model** = a handle-based register-width field-cell store (`src/runtime/teko_object.c`, 5
  KATs) — the Phase-14 "C runtime = source of truth → opcode family → native `teko_rt_*` + WASM
  reactor → `.tks` proof" pattern. Opcodes `OP_OBJ_NEW/SET/GET/FREE` (0x6A-0x6D); native
  `teko_rt_object_*`, WASM reactor import (`teko_object.c` added to the reactor + EXPORTS).
- **Methods** = function-table routines taking `self`; **static dispatch** via a NEW synchronous
  table-call primitive `OP_CALL_FUNC` (0x6E) — native `teko_rt_call` (routine fn typedef widened
  `void→long`; the body's last `$w0` survives `ret`), WASM `call_indirect (type $task)` + a
  `FUNC_END` `$w0`→`frame[0]` spill so the caller reads the result. Sets `uses_spawn`.
- **Frontend** (`frontend_interop.c`): `collect_classes` layout/method pre-pass (method slots
  continue after `nfns`, dense table); member access via the **dotted-identifier lexeme**
  (`obj.field` read/write, `obj.method(args)` static call with `self`=arg0); `ClassName()`
  instantiation → `OP_OBJ_NEW`; `emit_method_routines` (bodies as routines, `self` class-bound);
  `return <expr>;`. Decision: `routines`/methods share the routine table + scheduler TU.
- Proofs: `runtime/{native,wasm}/samples/class.tks` → `7`,`70` (native run-native.sh /
  WASM run-class.mjs `[7,70]`). Suite 213/213; ASan/UBSan (both paths) + TSan green; 16 goldens
  intact; existing routine WASM proofs still pass after the FUNC_END spill.
Sub-block **15.B `abstract` + `trait` is DONE** on both targets — dynamic dispatch via a
**compile-time static vtable**, trait composition with **collision = compile error**, `to_string`
dispatched through the vtable like any method (the Phase-16 hook). Runtime `src/runtime/teko_vtable.c`
(`vtable[type_id][method_id] → routine slot`) → `OP_VTABLE_SET/GET` (0x6F/0x70) → native
`teko_rt_vtable_*` + WASM reactor import. Frontend: `trait`/`abstract` grammar + `class C : T1,T2`
implements clause + bodyless (abstract, slot -1) methods + a global method-id space;
**fat trait-typed locals** (`let g: Trait = c` = instance-handle slot + a hidden type-id slot
holding the concrete type_id as a compile-time constant — object layout UNCHANGED, 15.A
byte-identical); vtable populated at `$main` start; `g.method()` → `vtable_get` → `OP_CALL_FUNC`.
Decision: indexed runtime vtable (not an inline branch chain); collision = two composed traits
declaring the same method without a class override. WASM needed a **nesting-safe `OP_CALL_FUNC`**
(stack-disciplined `$callfp` frame) so a method calling its own/another method works. Proofs
`runtime/{native,wasm}/samples/traits.tks` → `12,112,9,209`. Suite 200→221; ASan/UBSan (both paths)
+ TSan green; 16 goldens intact; existing class/routines/producer/capstone WASM proofs intact.
Sub-block **15.C `<T>` generics is DONE** — real per-type **monomorphization**: a generic
`class Box<T>` is specialized into one concrete `Box$Arg` per instantiation (type-param substituted
at compile time, zero runtime cost). `collect_generics` discovers templates + instantiations;
`collect_classes` clones a concrete instance per type-arg (mono slots deferred after non-generic
classes); `emit_mono_routines` re-lexes the template body per instance with `g_subst` (T→Arg) active,
so `T()` makes `Arg` + a `T`-typed local is `Arg`-typed. Proof `generics.tks` → `11,22`
(`Factory<Circle>`/`Factory<Square>`). Sub-block **15.D events is DONE** — `event`/`subscribe`/`raise`
+ `fanout`/`fire_and_forget` over the Phase-14 spawn runtime (frontend-only): static subscriptions
(`collect_events`); `raise E(args)` fan-outs by spawning each subscriber (drained at exit). Proof
`eventbus.tks` → `1,2,15,25`.
**PHASE 15 IS COMPLETE** — 15.A–15.D done & CI-green on all four gates (incl. Windows). No dead
tokens (every reserved OOP keyword LIVE with an executable `.tks` proof native AND WASM); zero
runtime reflection (compile-time field indices/method slots/static vtables/monomorphization).
Suite 223/223; ASan/UBSan (both paths) + TSan green; 16 goldens intact. **Merged via PR #8.**
Deferred (owner): full async-method `intent<>`/`await` semantics (parsing accepted; bodies lower
synchronously).

**Phase 16 (Casting / Type Conversions & Parsing) is IN PROGRESS** on `feat/phase-16-casting`
(PR #9, draft) — `docs/PHASE16_CASTING.md`. Universal, culture-invariant conversions + to/from-
string parsing, plus the auto-`to_string` on concat/interpolation that the Phase-15 `to_string`
hook was built for. Single C runtime source of truth (`src/runtime/teko_convert.c`, hand-rolled +
freestanding-safe — no snprintf/strtoll/strtod/setlocale, so the default `"C"`-locale form IS the
culture-invariant representation; distinct from Phase-14 `time.format_local`). Sub-block **16.A is
DONE** (locally green both targets): primitive `to_string` + str_concat surface —
`convert.int_to_str`/`bool_to_str`/`str_concat` (OP_CALL_RUNTIME ids 49/51/52 → native `teko_rt_*`
+ WASM reactor), proofs `runtime/{native,wasm}/samples/convert.tks` → `42/1000000/true/false/"x =
42"` (byte-identical, NO digit grouping). 6 Unity KATs (223→229). **Float** formatting/parsing is
its own later step (shortest-round-trip is large in freestanding C); the checked `parse_*` C core
landed in 16.A but its surface is deferred to **16.F**. Sub-block **16.B is DONE** (locally green,
the core deliverable): auto-`to_string` on `+` — value-type tracking in the frontend makes `"a" + n`
culture-invariant concatenation (int operand auto-converted via id 49 + `str_concat` id 52), string
locals tracked, extern call args accept expressions; proofs `concat.tks` → `x = 42/sum = 50/42
items/count: 42/n=42` (byte-identical). Sub-block **16.C is DONE** (locally green): string
INTERPOLATION — a `"…{expr}…"` literal interpolates each hole (auto-`to_string`, re-lexed through a
sub-parser sharing the ctx), `{{`/`}}` escape; proofs `interp.tks` → `x = 42/42 items, 42
total/sum = 50/count: 42/braces { } kept/[42]`. Sub-block **16.D is DONE** (locally green): a class
instance in a concat/`"{…}"` hole dispatches its (own/inherited) `to_string` via `OP_CALL_FUNC`
(Phase-15 hook), else the synthesized default `ClassName(fields)`; value-type model gains
`TEKO_VT_OBJ_BASE`; proofs `tostring.tks` → `temp is T=25/[T=25]/point = Point(3, 4)/p=Point(3, 4)`.
Sub-block **16.E is DONE**: explicit integer formats `convert.to_radix/pad/group` (ids 56/57/58).
Sub-block **16.F is DONE**: CHECKED `convert.parse_int`/`parse_bool` (ids 53/55) — fail-loud (native
aborts non-zero + stderr diag, wasm `__builtin_trap`); proofs `parse.tks` + `parse_fail.tks`.
**16.A–16.F all DONE & locally green** (suite 223→232; ASan/UBSan both paths; 16 goldens intact);
all six sub-blocks confirmed CI-green up to 16.E, 16.F pending the run. The auto-`to_string` core
deliverable (concat + interpolation + user-type dispatch + synth default) is shipped. The one
remaining owner item — **float** default/formatting — is GATED on a frontend float value model
(the accumulator model is integer-only; a `convert.float_to_str` token now would be a dead token);
documented in `docs/PHASE16_CASTING.md` as the designated next step (a numeric-types expansion, not
casting). Order: 16.A ✅ → 16.B ✅ → 16.C ✅ → 16.D ✅ → 16.E ✅ → 16.F ✅ → (float: gated, next step).
