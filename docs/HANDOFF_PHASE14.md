# Phase 14 — hand-off / continuation guide

Branch `feat/phase-14-advanced-concurrency` (PR #7, draft). This is the live continuation guide:
what's done, the **exact reusable pattern** for the channel sub-blocks, and the remaining work
(14.E–14.F). Pair with `docs/PHASE14_ADVANCED_CONCURRENCY.md` (design) and the memory file
`teko-phase14-advanced-concurrency`.

## ✚ NEW SCOPE (owner, 2026-06-16): 14.G timespan waiters, 14.H real samples
Owner added two sub-blocks (labels per owner: **14.G = await/wait waiters**, **14.H = real
samples** — samples are the capstone that showcases everything). Captured here as the canonical plan.

### Shared finding — control-flow emission is the missing foundation (a prerequisite, not a sub-block)
The interop frontend (`teko_compile_interop`) emits NO loops/branches today. Both "loops in
samples" (14.H) and the `retry { } fallback { }` block (14.F surface) need general control-flow
emission: native = `.L` labels + `jmp`/`jcc` for `OP_JMP`/`OP_JMP_IF_FALSE` in
`emit_native_hosted.c` (currently unhandled there); WASM = structured `(block $b)(loop $l … br/br_if)`
in `emit_wasm.c` (the `br $label_N` ops exist but need the enclosing block/loop scaffolding emitted
from the frontend). Build this ONCE (recommend `loop { }`+`break` and `while (cond) { }`, lowering
the condition via the existing `eval_expr_prec`); the 14.F surface and the 14.H samples both consume
it. (14.F's routine-trampoline alternative avoids loop-IL, but the 14.H samples need real loops.)

### 14.H — Real `.tks` samples (functions, threads, loops) — the capstone (build last)
- Real `.tks` combining named functions + routines/threads (`routines { … }`, and a Layer-B
  `--target=wasm-threads` thread sample) + loops (e.g. a worker loop that pushes N values to a
  channel; a fan-in loop draining a broadcast/duplex) + `await`/`wait` (14.G). Showcases 14.A–14.G.
- Needs: the control-flow foundation above; richer routine/`fn` bodies (named locals, loops, stretch:
  a return value — see the 14.F routine-trampoline note; keep 14.A's `void` routine path working).
- Executable proof native + WASM per the usual pattern; loops MUST run (assert the accumulated
  result), not just `strstr`.
- Decisions (recommend, confirm): loop keywords `loop`/`while` (+ `break`/`continue`); `for` later.

### 14.G — Timespan waiters: `await` (async) + `wait` (sync) (build first of the two)
- Literals already lex: `10ms` → LIT_INT 10 + `literal_unit=LIT_UNIT_MS` (ms/s/m/h/d). Add a frontend
  helper normalizing (value, unit) → **canonical milliseconds** (s×1000, m×60000, h×3.6e6, d×8.64e7);
  a timespan arg may be a literal, a named local (already ms), or an int expression (already ms).
- Add `await`/`wait` as keywords (TOKEN_AWAIT/TOKEN_WAIT — new; small lexer change).
- `wait <timespan>;` (SYNCHRONOUS): block the current OS thread. Native → `teko_rt_sleep_ms`
  (nanosleep / Win Sleep); WASM → host import `env.teko_sleep` (or a bounded busy-wait). New opcode
  `OP_WAIT` (1 arg = ms in $w0).
- `await <timespan>;` (ASYNCHRONOUS): cooperative timed yield — suspend the current routine on a
  timer and let the scheduler run others; resume after the delay. Build on 14.A scheduler + the
  14.C delayed/timer-queue mechanism (the logical clock / timer queue is exactly this). New opcode
  `OP_AWAIT_FOR` (1 arg = ms). On native run-to-completion, MVP can register the resume on the run
  queue after draining; on WASM Layer A, yield to `$teko_sched_run` with a deadline.
- Proofs: `wait` — assert observable ordering/elapsed ≥ delay (deterministic via a stubbed clock on
  WASM, real on native with a tight bound); `await` — two routines, one awaits while the other runs,
  asserting interleave order.

### How this changes prior approaches
- **Canonical timespan = ms integer at compile time.** 14.C `delayed.send(d,v,delay)` and 14.F
  backoff `base` should ACCEPT timespan literals (e.g. `delayed.send(d, v, 10ms)`), which normalize
  to the existing integer args — so the runtimes are unchanged; only the frontend gains unit
  normalization (apply it in `lower_codec_value` for LIT_INT args carrying a `literal_unit`). Low
  disruption, big ergonomics win, and it unifies the time model across 14.C/14.F/14.H.
- 14.C's logical clock + timer queue is the natural substrate for `await`'s timer; consider exposing
  a `teko_delayed`-style timer the `await` lowering reuses rather than a parallel mechanism.

### Recommended sequencing (labels per owner: 14.G = waiters, 14.H = samples)
1) **14.G await/wait + timespan normalization** (independent-ish; lexer already carries the unit;
   reuses 14.A scheduler + 14.C timer) → 2) **control-flow foundation** (labels/loops, native+WASM)
   → 3) **14.F surface** (retry/fallback block, now easy on the foundation) → 4) **14.H real samples**
   (capstone: functions + threads + loops + channels + await/wait, showcasing 14.A–14.G). Also adopt
   timespan literals in the 14.C/14.F delay args (compile-time-normalized → runtimes unchanged).

## ▶ RESUME POINT (read first) — for a FRESH session on this same branch/PR
- **Branch:** `feat/phase-14-advanced-concurrency` (PR #7, draft); resume from its latest commit
  (this doc's commit is the tip). Working tree clean, fully pushed to `origin`. Suite **194/194**;
  ASan+UBSan (both dispatch paths) + TSan green; 16 native goldens intact; all 4 CI gates green on
  the last code commit (`94f9cf5`, 14.F.1). **Continue ON THIS BRANCH/PR — do not open a new one.**
- **Done & CI-green:** 14.A routines · 14.B duplex · 14.C delayed · 14.D broadcast · 14.E
  shared/atomic · **14.F.1** policy runtime (`teko_retry.c` + 6 KATs) · **14.G** await/wait
  timespan waiters (commits `de41118` 14.G.1 ms-normalization + `dbc75f8` 14.G.2 waiters; all 4
  gates green). Suite **196/196**.
- **14.G DONE (decisions, for reference):** `wait <ts>;` = synchronous sleep (native
  `teko_rt_sleep_ms` real nanosleep/Win Sleep; WASM `env.teko_sleep` host import). `await <ts>;`
  = cooperative timed yield (native `teko_rt_await_ms` advances a logical clock + drains the run
  queue; WASM `env.teko_await` records ms + `$teko_sched_run` drain). Opcodes `OP_WAIT` 0x59 /
  `OP_AWAIT_FOR` 0x5A (single-byte, ms in $w0, in BOTH CSE invalidation sets). Timespan literals
  normalize to canonical ms at compile time (`literal_canonical_value` in `frontend_interop.c`),
  adopted in `lower_codec_value` so channel delay args accept `2s`/`500ms`/etc. **MVP caveat:**
  native routines are run-to-completion → `await` is a cooperative "let others run" yield, not
  real timer suspension (WASM Layer A mirrors it). Proofs `waiters.tks` native + WASM.
- **Remaining (owner-agreed order):**
  1. ~~**14.G — `await`/`wait` timespan waiters**~~ ✅ DONE.
  2. **Control-flow foundation** (loops + branches in the frontend; see "✚ NEW SCOPE → foundation").
  3. **14.F surface** — `retry { } fallback { }` / `circuit` block grammar (see "▶ 14.F STATUS";
     easy once the foundation exists, or use the routine-trampoline) — makes the remaining keyword
     tokens live; closes 14.F.
  4. **14.H — real `.tks` samples** (capstone: functions + threads + loops + channels + waiters).
  Owner recommended decisions (pre-noted): loop keywords `loop`/`while`+`break`; timespans canonical
  in ms; adopt timespan literals in 14.C/14.F delay args. Phase 14 leaves draft only when 14.F
  surface + 14.G + 14.H all land with `.tks` proofs (no dead tokens) and all 4 gates are green.
- **Before starting:** `git fetch && git checkout feat/phase-14-advanced-concurrency && git pull --ff-only`,
  rebuild (`cmake --build build`), run `./build/teko_tests` (expect 194/194), then follow the
  sub-plans below. The channel sub-blocks (14.B/C/D) are the copy-me template; 14.E/14.F/14.G are NOT
  channels — read their specifics first.
- Local quirks: Ninja isn't installed → use the pre-configured `build/` (Unix Makefiles). The
  Write tool sometimes appends a stray `</content>` line — strip it before building. `wat2wasm`
  + `node` + LLVM `clang`/`wasm-ld` are available (the last two build the reactor).

## Status
- **14.A `routines`** — DONE, CI-green.
- **14.B `duplex chan`** — DONE, CI-green.
- **14.C `delayed chan`** — DONE, CI-green.
- **14.D `broadcast chan`** — DONE, CI-green (built as `broadcast.*` dotted-ident, reusing the pattern).
- **14.E `shared` block + `atomic`** — DONE, CI-green (coarse global lock + atomic cells; new
  `shared { }` block grammar; portable atomics without <stdatomic.h>).
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

## 14.E `shared` block + `atomic` — sub-plan (largest/most novel; NOT the channel template)
Owner decision: **coarse whole-block lock** (the compiler injects ONE lock around the entire
`shared { … }` block — not per-field). `atomic` qualifies a single op to a fenced atomic RMW.
This is a frontend block-grammar + lock-injection feature, distinct from the channel runtimes.

Recommended MVP (deterministic + provable, mirrors how the channel runtimes stayed
scheduler-agnostic): a C runtime `src/runtime/teko_shared.c` (source of truth) exposing a
re-entrancy-safe coarse lock + atomic counter primitives, KAT-tested in Unity:
  `TekoLock* teko_shared_lock_new(void); teko_shared_enter(l); teko_shared_leave(l);`
  `long teko_atomic_add(long* cell, long delta); long teko_atomic_load(long* cell);` (etc.)
On native use `<stdatomic.h>` (`atomic_flag` spinlock / `atomic_fetch_add`); the lock must be a
real fence so it's correct under the TSan gate. On WASM Layer A (single-threaded cooperative)
the lock is a no-op/fence and the atomics are plain loads/stores; on `-wasm-threads` (Layer B)
lower to the atomics proposal — but the executable proof can be single-threaded (assert the
locked block's accumulated result), since Layer A is the default and is what `run-*.mjs` drives.

Steps (sub-plan; report it before coding, flag a real fork e.g. if escape analysis is needed):
1. `.1` runtime `teko_shared.{h,c}` + Unity KATs (lock enter/leave balance; atomic add/load
   single-threaded determinism). Add to CMake (CORE_SOURCES + teko_rt). 
2. `.2` native surface: opcodes `OP_SHARED_ENTER/LEAVE` + `OP_ATOMIC_*` (next free byte 0x53+);
   `uses_shared` flag; CSE invalidation for any \$w0-clobbering ones. Frontend: parse the
   `shared { … }` BLOCK (TOKEN_SHARED + braces) — emit ENTER at `{`, lower the inner statements
   (reuse the existing statement-lowering helpers), emit LEAVE at `}`. `atomic x += e` (TOKEN_ATOMIC)
   → `OP_ATOMIC_ADD`. Native wrappers `teko_rt_shared_*`/`teko_rt_atomic_*`; emitter cases; a
   `.tks` proof (a `shared { }` block with an `atomic` accumulator → emit the final value).
3. `.3` WASM: add `teko_shared.c` to the reactor + exports; emitter imports + cases; `.tks` proof.
Design note: the coarse lock + atomics are runtime calls (handle/cell as a register-width int),
so they fit the existing OP_CALL-style marshalling. The novel part is the `shared { }` BLOCK
grammar (open/close bracketing the inner statements) — design that first.

## ▶ 14.F STATUS — policy runtime DONE; block surface is the remaining hand-off
**14.F.1 DONE (CI pending):** `src/runtime/teko_retry.{h,c}` (6 Unity KATs) is the policy source of
truth — exponential/logarithmic backoff, the `attempts` + `attempts+timeout→fallback` rule
(incremental-relative-time), and the circuit breaker CLOSED/OPEN/HALF_OPEN machine. Deterministic
(time passed in), portable C, in CORE_SOURCES + teko_rt. **No language surface yet** — so the
`circuit`/`retry`/`fallback`/`exponential`/`logarithmic`/`attempts`/`timeout` keyword tokens are
still reserved-but-unused; 14.F is NOT done until the surface below lands (no-dead-token gate).

**Remaining 14.F (the large, design-heavy part — needs fresh context):** lower the
`retry(attempts N, timeout T, exponential) { body } fallback { fb }` BLOCK + `circuit` so those
keyword tokens become live, executable on native + WASM. Two viable lowerings (pick after a spike):
- **(A) Routine-trampoline (RECOMMENDED — reuses 14.A machinery, avoids new IL control-flow):**
  emit `body`/`fb` as table routines (the frontend already emits `fn` bodies as routines via
  `emit_handler_routines` + the function table), then emit a call to a C driver
  `teko_retry_run(retry_handle, body_slot, fallback_slot)` that loops: invoke the body routine
  via the table, check ok/fail, back off + retry per the policy, else invoke fallback. The retry
  LOOP lives in C — no new loop/branch emission. Sub-tasks: (1) give routines a RETURN VALUE
  (today native `teko_routine_<n>` is `void(long)`, wasm routines return their state machine
  state — extend to return an i32 ok/fail; this ripples to 14.A's `teko_rt_run`, keep it
  compatible); (2) emit ANONYMOUS inline blocks as routines (synthetic slots) + capture the
  body's result; (3) the `retry(...)`/`fallback`/`circuit` keyword block grammar in
  `frontend_interop.c` (TOKEN_RETRY/FALLBACK/CIRCUIT/EXPONENTIAL/LOGARITHMIC/ATTEMPTS/TIMEOUT —
  all reserved); (4) `teko_retry_run`/`teko_circuit_*` reactor exports + emitter wiring; (5) `.tks`
  proofs native + WASM (a flaky call that succeeds on attempt K → retried; a permanently-failing
  one → fallback ran; breaker opens after threshold).
- **(B) Emitted loop/branch IL:** lower the block to an emitted attempt-loop using OP_JMP/
  OP_JMP_IF_FALSE. Needs NEW general control-flow emission in the interop frontend AND hosted-
  emitter support for OP_JMP/labels (asm `.L` labels) + WASM structured `(block)/(loop)/br`
  wrappers — heavier and touches every backend. (A) is preferred.
Design the chosen lowering + the keyword block grammar BEFORE coding; report the sub-plan.

## 14.F `circuit` + `retry` — original notes (resilience control-flow; NOT the channel template)
`retry { … } fallback { … }` with `attempts` and/or global `timeout` limits and
`exponential`/`logarithmic` backoff between attempts; `circuit` = an open/half-open/closed
breaker wrapping a call. Tokens TOKEN_CIRCUIT/RETRY/FALLBACK/EXPONENTIAL/LOGARITHMIC/ATTEMPTS/
TIMEOUT are reserved. From `plan.md`: when BOTH `attempts` and `timeout` are given, the compiler
computes the incremental relative retry time and **branches straight to `fallback`** once the
projected next-attempt delay would exceed the time budget.

Recommended MVP — keep the POLICY in a C runtime (source of truth, deterministic, KAT-testable),
the CONTROL-FLOW in emitted IL:
1. `.1` runtime `src/runtime/teko_retry.{h,c}` + Unity KATs. A retry-policy object:
   `TekoRetry* teko_retry_new(int attempts, uint64_t timeout, int mode /*0=exp,1=log*/, uint64_t base);`
   `int teko_retry_should_continue(TekoRetry*, int attempt, uint64_t elapsed);` (0=give up→fallback)
   `uint64_t teko_retry_next_delay(TekoRetry*, int attempt);` (exp: base<<attempt; log: base*ln-ish
   integer approx) and the combined attempts+timeout branch-to-fallback rule. A circuit-breaker
   object: `teko_circuit_new(threshold, cooldown)`, `teko_circuit_allow()`, `teko_circuit_record(ok)`
   with CLOSED→OPEN→HALF_OPEN transitions. Test the backoff sequence + the fallback-on-budget rule
   + breaker transitions deterministically (pass explicit attempt/elapsed, like 14.C's logical clock).
2. `.2` native surface: this differs from channels — `retry { } fallback { }` is a control
   structure, not a call. Lowering options (pick after a quick spike): (a) the simplest provable
   MVP lowers a `retry`/`fallback` over a *single retriable extern call* to a loop in emitted IL
   driven by the policy object (`OP_JMP`/`OP_JMP_IF_FALSE` already exist) — attempt the call, ask
   the policy `should_continue`, loop or branch to the fallback block; or (b) a runtime trampoline.
   Recommend (a) for a deterministic `.tks` proof: a flaky call that fails K times then succeeds
   (assert it retried), and a permanently-failing one that lands in `fallback` (assert the
   fallback ran). Emit `OP_RETRY_*`/policy calls + the loop. `circuit.*` can be dotted-ident
   runtime calls like the channels.
3. `.3` WASM: add `teko_retry.c` to the reactor + exports; emitter cases; `.tks` proof.
Design the `retry { }`/`fallback { }` block grammar + the loop/branch lowering BEFORE coding;
this is the most control-flow-heavy sub-block. The backoff time budget reuses the 14.C
logical-clock idea (pass elapsed in) to stay deterministic.

## Closing Phase 14
After 14.F: full suite + sanitizers + 4 gates green; update `docs/PHASE14_ADVANCED_CONCURRENCY.md`
(all sub-blocks done) + CLAUDE.md + this doc; then tell the owner Phase 14 is ready to leave
draft (the owner merges — no merge/force-push from the agent).

## Gotchas
- The Write tool sometimes appends a stray `</content>` line — strip it
  (`perl -ni -e 'print unless m{^</content>$}' <file>`) before building.
- Opcodes are single-byte (no 4-byte arg) — they must NOT be added to the arg-reading list in
  `process_linear_il_bytes`; they ARE added to the two CSE-invalidation lists.
- Reactor handles are pointers into the reactor heap (> 65536); Teko stores them in an i32/i64
  local and passes them back opaquely. Pure i32-in/i32-out — no hex/shared-memory marshalling
  (unlike crypto).
