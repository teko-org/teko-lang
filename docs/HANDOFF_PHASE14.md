# Phase 14 — hand-off / continuation guide

Branch `feat/phase-14-advanced-concurrency` (PR #7, draft). This is the live continuation guide:
what's done, the **exact reusable pattern** for the channel sub-blocks, and the remaining work
(14.D–14.F). Pair with `docs/PHASE14_ADVANCED_CONCURRENCY.md` (design) and the memory file
`teko-phase14-advanced-concurrency`.

## Status
- **14.A `routines`** — DONE, CI-green.
- **14.B `duplex chan`** — DONE, CI-green.
- **14.C `delayed chan`** — DONE, CI-green.
- **14.D `broadcast chan`** — TODO. Owner decision: build on `pub`/`subscribe` (no `broadcast` keyword).
- **14.E `shared` block + `atomic`** — TODO. Owner decision: coarse whole-block lock. *Largest/most
  novel; do its own scoped sub-plan; a genuine scope-fork would be reported, not decided solo.*
- **14.F `circuit` + `retry`** — TODO (exp/log backoff, attempts/timeout, fallback).

Owner granted standing autonomy across all sub-blocks with the non-negotiable bars (1
increment/commit; ASan+UBSan both dispatch paths + TSan; 16 native goldens; 4 CI gates green;
no dead token — executable `.tks` proof on native AND WASM; patient CI watcher; docs/CLAUDE.md
updated; no merge/force-push; report at each sub-block close).

## The reusable channel-sub-block pattern (followed by 14.B and 14.C — copy it for 14.D)
Each channel sub-block is 3 commits:

**.1 — C runtime (source of truth) + Unity KATs.** New `src/runtime/teko_<name>.{h,c}`,
scheduler-agnostic, non-blocking, returns a structured status enum. Add the `.c` to BOTH
`CORE_SOURCES` and the `teko_rt` target in `CMakeLists.txt`. New `tests/runtime/test_<name>.c`
(include `"../../src/runtime/teko_<name>.h"`); add to `TEST_SOURCES`; register externs +
`RUN_TEST` in `tests/test_main.c`.

**.2 — native surface.** (a) Opcodes `OP_<NAME>_*` in `src/codegen_li.h` (next free byte ≥ 0x4D)
+ a `uses_<name>` flag (init in `codegen_li.c` create-context) + `codegen_li_emit_<name>(buffer,op)`
helper (sets the flag, emits the byte). (b) **CRITICAL:** add every new opcode to BOTH IL-CSE
invalidation sets in `process_linear_il_bytes` (`src/codegen/codegen_metal.c`) — they are
`$w0`-clobbering runtime calls (see the memory file; this caused a Linux-x86_64-only CI failure
in 14.A). (c) Frontend `src/frontend_interop.c`: `<name>_op_for`/`is_<name>_head`/
`lower_<name>_call` (copy `lower_duplex_call`; the lexer folds `name.method` into one IDENTIFIER,
so it reuses the dotted-identifier path — but `pub`/`subscribe` are KEYWORDS, see 14.D note).
Hook `is_<name>_head` into the let-initializer branch, the top-level-statement branch, and the
routine-body loop. (d) Native emitter `src/codegen/emit_native_hosted.c`: `teko_native_<name>_symbol`
mapping each opcode → `teko_rt_<name>_*` + arity; add the `case`s routing through `emit_call`.
(e) Wrappers in `runtime/native/teko_rt.c` (+ decls in `teko_rt.h`): `long teko_rt_<name>_*(long…)`
over the C runtime (handle = register-width int). (f) Proof `runtime/native/samples/<name>.tks`
(use `extern fn emit_int … as "teko_rt_emit_int"`) + a `check` case in `runtime/native/run-native.sh`.
(g) A `test_frontend_interop_<name>_lowering` in `tests/test_codegen_li.c` pinning the IL.

**.3 — WASM surface.** (a) `runtime/wasm/crypto/build-crypto-reactor.sh`: add the `.c` to `SRCS`
and the wrappers to `EXPORTS`. (b) WASM emitter: `wasm_emit_<name>` ctx flag (+ setter in
`codegen_metal.c`, decl in `.h`, init, transfer in `codegen_li_wasm.c`); import block (copy the
duplex one, namespace `"crypto"`); OR the flag into the shared-memory gate; OP_<NAME>_* `case`s
(copy duplex — `local.get $a0..` staging + `call $<name>_op`). (c) Proof
`runtime/wasm/samples/<name>.tks` (use `extern fn log_int … from "env" as "log_int"`) +
`runtime/wasm/run-<name>.mjs` (copy run-duplex.mjs; instantiate reactor + sample over one shared
`WebAssembly.Memory`, env provides `teko_random` stub + `log_int`). (d) Wire into `wasm.yml`
(compile + assemble + run step) and `.gitignore` (`.wat` + `.glue.mjs`). (e) Extend the
`_lowering` test with WASM strstr assertions. (f) Regression: re-run run-duplex/run-delayed/
run-crypto (the reactor changed).

Every commit: `cmake --build build` (Unix Makefiles — Ninja isn't installed locally), run
`./build/teko_tests`, then ASan/UBSan (`build-asan`, `build-asan-portable` with
`-DTEKO_VM_PORTABLE_DISPATCH`) + TSan (`build-tsan`) — all green. Push, watch all 4 CI gates.

## 14.D `broadcast chan` — specifics
Non-destructive 1:N pub-sub. `pub`/`subscribe` are reserved KEYWORDS (TOKEN_PUB/TOKEN_SUBSCRIBE),
so `pub.x` does NOT fold to a dotted identifier — handle them via the keyword tokens in the
frontend (a small departure from the dotted-ident path), or expose the surface as
`broadcast.publish/subscribe/recv` dotted-idents (NOTE: `broadcast` is NOT a keyword, so
`broadcast.publish` DOES fold to one IDENTIFIER — simplest, and consistent with duplex/delayed).
Recommend the `broadcast.*` dotted-ident surface to reuse the pattern verbatim, and treat the
`pub`/`subscribe` keywords as alternative spellings only if the owner insists. Runtime
`teko_broadcast.c`: a slot buffer + one read cursor per subscriber; `publish` writes once,
each subscriber reads non-destructively from its own cursor (so all N see every value).

## 14.E `shared` block + `atomic` — specifics (largest; sub-plan before coding)
`shared { … }` block whose state the compiler treats as concurrently accessed, injecting a
coarse whole-block lock (owner decision); `atomic` qualifies one op to a fenced RMW. Native:
`<stdatomic.h>` / a spinlock in teko_rt. WASM: Layer B atomics for the threaded target; Layer A
is single-threaded so the lock is a fence/no-op. This needs frontend block-grammar work
(`shared` is TOKEN_SHARED, `atomic` is TOKEN_ATOMIC). Report a scoped sub-plan first; flag any
real scope-fork (e.g. escape analysis depth) to the owner.

## 14.F `circuit` + `retry` — specifics
Resilience control flow, mostly independent of channels. `retry { } fallback { }` with
`attempts` / global `timeout` limits and `exponential`/`logarithmic` backoff; `circuit`
open/half-open/closed breaker. The backoff schedule is computable in a C runtime helper
(`teko_circuit.c`?) returning the next delay / a branch-to-fallback decision; reuses the 14.C
logical-clock idea for the time budget. Tokens TOKEN_CIRCUIT/RETRY/FALLBACK/EXPONENTIAL/
LOGARITHMIC/ATTEMPTS/TIMEOUT are reserved. This is more frontend-control-flow than the channel
sub-blocks — design the lowering (loop + branch IL, or a runtime-driven trampoline) before coding.

## Gotchas
- The Write tool sometimes appends a stray `</content>` line — strip it
  (`perl -ni -e 'print unless m{^</content>$}' <file>`) before building.
- Opcodes are single-byte (no 4-byte arg) — they must NOT be added to the arg-reading list in
  `process_linear_il_bytes`; they ARE added to the two CSE-invalidation lists.
- Reactor handles are pointers into the reactor heap (> 65536); Teko stores them in an i32/i64
  local and passes them back opaquely. Pure i32-in/i32-out — no hex/shared-memory marshalling
  (unlike crypto).
