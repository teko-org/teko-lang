> **SUPERSEDED (2026-06-16):** the work this hand-off described was completed in the SAME session — no new session was opened. Phase 15 (15.A–15.D) is DONE & CI-green. This doc is kept only as a record of the design rationale; the canonical status is `docs/PHASE15_OOP.md`.

# Hand-off — Phase 15.C (generics/monomorphization) + 15.D (events)

**Branch:** `feat/phase-15-oop` (PR #8, draft). **15.A (concrete classes) + 15.B (abstract + traits,
static vtables) are DONE & CI-green** (21/21). This hands off the last two OOP sub-blocks so a fresh
session finishes Phase 15 with full context. Owner has pre-approved autonomous work through
15.C → 15.D (decide + document design points; flag only phase-scope forks). When all of Phase 15 is
green, tell the owner it's ready to leave draft.

Read first: `docs/PHASE15_OOP.md` (design + decisions + the `to_string` Phase-16 hook), `CLAUDE.md`
(Phase 15 status), `src/CLAUDE.md`. The substrate is `src/frontend_interop.c` (class/trait registry,
`collect_classes`/`collect_traits`, `emit_method_routines`, `lower_member_call`/`lower_trait_dispatch`,
fat trait-typed locals) + the runtimes `src/runtime/teko_object.c` / `teko_vtable.c`.

## Discipline (unchanged, non-negotiable)
1 increment/commit; build + suite; **ASan+UBSan on BOTH dispatch paths + TSan** each commit; the 16
native goldens never regress; **all 4 CI gates green incl. Windows** before a sub-block is "done"
(patient ≥90s watch); **executable `.tks` proof native + WASM, no dead token**; **no merge/force-push**.
Watch the **Windows LLP64** lesson (pointer-carrying cells = `intptr_t`, not `long`). When adding a
runtime-backed opcode family, follow the established chain: C runtime + KATs → `OP_*` in
`codegen_li.h` + emit helper + `uses_*` flag → CSE sets in `codegen_metal.c` → native symbol/case in
`emit_native_hosted.c` + `teko_rt_*` wrapper in `runtime/native/teko_rt.{c,h}` → WASM import + case +
`wasm_emit_*` flag in `emit_wasm.c` + `codegen_li_wasm.c` + reactor `SRCS`/`EXPORTS` in
`runtime/wasm/crypto/build-crypto-reactor.sh` → IL emission test + native + WASM `.tks` proof
(`run-native.sh` / a new `run-*.mjs` wired into `wasm.yml`). The object/vtable families are worked
examples to copy.

## Current generics/async state (what already works)
- Generic methods PARSE: `fn m<T>(self, k): T { … }` — the `<T>` clause is skipped
  (`skip_generic_clause`) and the body lowers with the **uniform-i32 value model** (a generic method
  binds params + lowers exactly like a concrete one). Verified: `fn scale<T>(self,k): i32` runs.
- `async fn … : intent<>` PARSE (async consumed; body lowers synchronously). Full `intent<>`/`await`
  async-method semantics are a SEPARATE later sub-block (owner deferred — do NOT pull forward).

## 15.C — Generics / real per-type monomorphization (the deep one)
**Goal:** for each concrete instantiation `Box<int>`, `Box<Circle>`, …, generate a SPECIALIZED copy
of the generic class/method (Go-generics / C++-template style) — never a runtime type parameter.

**Why it's deep here:** the frontend is single-pass direct-lowering with no AST. Monomorphization
means capturing the generic body's token span and RE-LOWERING it per concrete type argument with the
type parameter substituted. The uniform-i32 model means `T` mostly selects *which methods of T* the
body calls and *which concrete class* a `T`-typed field/local resolves to — so the substitution is
"T → a concrete class name" at instantiation.

**Suggested approach (increments):**
1. **Template capture.** In `collect_classes`, detect a generic class header `class C<T> [: traits]`
   (a `<…>` clause after the class name — add a `skip`/capture analogous to `skip_generic_clause`,
   but RECORD the type-param names). Mark it a TEMPLATE: do NOT assign a type-id / method slots yet;
   store its source token range (or just its name + that it's generic, then re-parse on demand).
2. **Instantiation discovery.** Scan for instantiation/annotation sites that name a concrete arg:
   `Box<int>(...)`, `let b: Box<Circle> = …`. Collect the SET of distinct `(template, typearg)` pairs.
   (A pre-pass like `collect_classes`.)
3. **Monomorphize.** For each `(C, A)` pair, synthesize a concrete class `C$A` in `g_class`: copy
   C's field/method tables, assign a fresh dense type-id + method slots (continuing the global
   counter), substituting the type-param `T` with `A` wherever it appears (field types, return
   types, and — the real work — `T`-typed locals/`self`-fields whose methods are called). Emit the
   monomorphized method bodies as routines (extend `emit_method_routines` to iterate the synthetic
   instantiations, re-lexing the template body with `T`→`A`).
4. **Resolve uses.** `Box<int>(...)` instantiation → `C$int` (its nfields/type-id). `b.method()` →
   `C$A`'s method slot (static) or, if `b` is trait-typed, the vtable (15.B path already works once
   `C$A` is a normal concrete class with vtable entries).
5. **Proofs:** a generic container/box instantiated at ≥2 concrete types, each specialization
   behaving correctly (native + WASM `.tks`). Negative/either: document any bounded subset.

**Scope guard / possible fork:** full monomorphization with `T`-method dispatch + nested generics is
large. A defensible MVP: generic classes/methods over an opaque `T` (value pass-through, the uniform
model — already works) PLUS monomorphization that specializes a `T`-typed field so `field.method()`
resolves to the concrete `A`'s method per instantiation. If the full template re-lexing proves
larger than a sub-block, REPORT it as a scope fork before committing to the deep version; an interim
"generics = uniform-i32 pass-through, monomorphization of behavior is staged" is already partly true.
Document whatever subset you land precisely (no dead token: `<T>` must remain live + proven).

## 15.D — Event subsystem (`event` / `raise` / `subscribe`, `fanout` / `fire_and_forget`)
**Goal:** `event E;` declares an event; `subscribe E with handler [fanout|fire_and_forget];`
registers a handler + delivery mode (chosen AT SUBSCRIPTION time); `raise E(args);` dispatches to
all subscribers — **`fanout`** spawns each handler as a parallel green thread, **`fire_and_forget`**
spawns without any join. Tokens `event`/`raise`/`subscribe`/`fanout`/`fire_and_forget` are already
reserved (Phase 12). Builds on the **Phase-14 spawn/scheduler runtime** (already present:
`OP_SPAWN_ASYNC`/`OP_SPAWN_ASYNC_ARGS`, `teko_rt_spawn*`/`teko_rt_run`, WASM run queue).

**This is the BOUNDED one — likely FRONTEND-ONLY** (no new C runtime): subscriptions are STATIC
(compile-time `subscribe` statements), so the subscriber list per event is known at compile time.
`raise E(args)` then lowers to a spawn of each registered handler (handlers are `fn`s → routine
slots, exactly like `routines { … }`). `fanout` vs `fire_and_forget` both lower to a spawn drained
at program exit in the cooperative MVP; document the semantic distinction (fanout = parallel green
threads — real parallelism on Layer B / wasm-threads; fire_and_forget = enqueue, no result tracked).

**Suggested approach (increments):**
1. **Event + subscription registry (frontend, file-static like the class registry).** A pre-pass
   collects `event E;` declarations and `subscribe E with H [mode];` registrations → per-event list
   of `(handler_routine_slot, mode)`. `H` resolves to a top-level `fn`'s slot (reuse
   `collect_functions`/`bind_lookup(fns,…)`). Handlers may take the raised args (bind like 14.I
   spawn args).
2. **`raise E(args);` lowering.** For each subscriber of `E`: stage `args` into `$a*`, `ICONST
   handler_slot`, `OP_SPAWN_ASYNC_ARGS argc` (or `OP_SPAWN_ASYNC` for arg-less). Sets `uses_spawn`
   → the scheduler drains at exit (handlers run after `$main` body, proving deferred fan-out). Hook
   into the top-level loop + `lower_one_stmt`.
3. **`event`/`subscribe` statements emit no code** (compile-time registrations) — skip in the main
   lowering pass (like class decls).
4. **Proofs (native + WASM):** an event with 2 subscribers; `raise` fires both (assert both handler
   effects appear, after the main body — deferred). Show a `fanout` subscription and a
   `fire_and_forget` subscription. `runtime/{native,wasm}/samples/events.tks` + a `run-events.mjs`
   wired into `wasm.yml` + a `check` in `run-native.sh`.
5. Update `docs/PHASE15_OOP.md` + `CLAUDE.md`; when Phase 15 is fully green, tell the owner it's
   ready to leave draft (the human merges — no merge/force-push).

## Quick verification
```sh
cmake -S . -B build >/dev/null && cmake --build build --target teko teko_rt teko_tests -j4 && ./build/teko_tests   # 221/221
./build/teko build runtime/native/samples/traits.tks --target=host --rt-lib=./build/libteko_rt.a -o /tmp/t && /tmp/t  # 12 112 9 209
# WASM: bash runtime/wasm/crypto/build-crypto-reactor.sh ; teko build … --target=wasm ; wat2wasm ; node runtime/wasm/run-traits.mjs
```
