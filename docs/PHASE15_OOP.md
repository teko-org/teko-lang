# Phase 15 — Bare-Metal Object-Oriented Paradigm

*Object orientation with **zero runtime reflection overhead**.* Branch
`feat/phase-15-oop` (PR #8, draft). Builds on the Phase 12 grammar/locals/expression
substrate and the Phase 14 control-flow + concurrency runtime. Every reserved token
becomes **live with an executable `.tks` proof on native AND WASM** — no dead tokens.

## Scope (from `docs/plan.md` §Phase 15)
- Concrete, **Generic** (`<T>` via monomorphization), and **Abstract** classes.
- **Complete rejection of runtime object Attributes/Annotations** — no RTTI, no dynamic
  attribute bags, no runtime type tags beyond a compile-time-assigned vtable id. All
  dispatch resolved at compile time (direct call for concrete; static, compile-time-built
  vtable index for abstract/trait).
- Multiple behavior inheritance via **`traits`**.
- **Event subsystem** (`event`, `raise`, `subscribe`): delivery behavior chosen at
  subscription time — `fanout` (parallel green threads) or `fire_and_forget`. Builds on
  the Phase 14 spawn/scheduler runtime.

Reserved-but-dead tokens to make live (already lexed since Phase 12): `class`, `abstract`,
`trait`, `event`, `raise`, `subscribe`, `fanout`, `fire_and_forget`.

## How OOP lowers with zero runtime reflection

### Object representation — runtime field-cell store (the Phase-14 pattern)
The IL is an **accumulator machine** (`$w0/$w1/$cp`) with uniform i32 values (ints /
handles) and **no generic pointer load/store opcode**. Rather than invent pointer
arithmetic, an object is a **runtime record keyed by an i32 handle** — the SAME
"C runtime is the single source of truth → opcode family → native `teko_rt_*` + WASM
reactor import → `.tks` proof" pattern every Phase 14 sub-block used:

- `src/runtime/teko_object.c` (KAT-tested): `obj_new(nfields) -> handle`,
  `obj_set(handle, idx, val)`, `obj_get(handle, idx) -> val`, `obj_free(handle)`.
  Field cells are i32; objects holding strings/sub-objects store their handle/pointer as
  the i32 cell value. This is **not** reflection — there is no name→field lookup at
  runtime; field **indices are resolved at compile time** from the class declaration.
- Opcode family `OP_OBJ_NEW / OP_OBJ_SET / OP_OBJ_GET` (+ optional free), staged like
  every other runtime family (`OP_SETARG` 0..n-2, last in `$w0`, result in `$w0`).

### Methods — static dispatch via a synchronous table call
Methods are emitted as **function-table routines** (reusing `OP_FUNC_BEGIN/END` and the
existing routine table) that take the object handle as their first argument (via the
Phase-14.I spawn-arg ABI, but **called synchronously**, not spawned). Concrete-class calls
are **direct**: the compiler knows the static type of the receiver, so it calls the exact
method routine — no vtable, no lookup.

This needs ONE genuinely new IL primitive: a **synchronous table call** that invokes a
routine slot and returns to the caller (today routines are spawn-only — dispatched by the
scheduler, never called inline). Both backends already have the table + `call_indirect`
(WASM) / `call` (native asm) machinery from spawn; we add the synchronous variant:
`OP_CALL_FUNC <slot>` (args staged in `$a0..`, result in `$w0`).

### Abstract classes + traits — compile-time static vtables
"Zero runtime reflection" forbids RTTI/attribute bags — it does **not** forbid vtables, as
long as they are **built at compile time** (the Go/Rust/C++ static-dispatch model). A
trait/abstract-typed reference is a `(handle, vtable_id)` pair; calling a trait method
indexes a **compile-time-emitted** dispatch table → `call_indirect`/indirect `call`. Trait
composition (multiple behavior inheritance) = the **union of the traits' method tables**,
with a name collision being a **compile-time error**. Requires a light compile-time symbol
table mapping each local → its class/trait type (the frontend currently tracks locals only
as untyped i32 slots — this is the main new frontend machinery here).

### Generics `<T>` — monomorphization (the deep one)
For each concrete instantiation `Box<int>`, `Box<Account>`, … emit a **specialized copy**
of the class body — Go-generics / C++-template style, never a runtime type parameter.
Because values are uniform i32, `T` mostly selects *which methods of `T`* the body calls;
the deep work is **capturing the generic class/method token spans and re-lowering them per
instantiation** in a single-pass, direct-lowering frontend (the same pre-pass trick
`emit_handler_routines` uses to re-parse `fn` spans, generalized to substitute `T`). MVP is
bounded by supporting monomorphization over the instantiations a program actually uses.

### Event subsystem — on the Phase 14 spawn/scheduler runtime
- `event E;` declares a named signal (a compile-time id + a runtime subscriber list).
- `subscribe E with handler [fanout|fire_and_forget];` registers a handler routine slot +
  a delivery mode in the event registry.
- `raise E(args);` dispatches to every subscriber: **`fanout`** spawns each handler as a
  green thread (`OP_SPAWN_ASYNC_ARGS`, drained at exit — true parallel on Layer B / WASM
  threads, cooperative on Layer A / native); **`fire_and_forget`** enqueues without any
  join semantics (same spawn, no result awaited). Source of truth
  `src/runtime/teko_event.c` (subscriber registry, KAT-tested) → `OP_EVENT_*` family →
  native `teko_rt_event_*` + WASM reactor import. Handlers are plain `fn` routines, so this
  is **independent of the class model** and can ship without classes.

## Dependency order & sizing

| Sub-block | Content | Size | Depends on |
|-----------|---------|------|-----------|
| **15.A** | Concrete classes: `teko_object` runtime, `OP_OBJ_*`, `OP_CALL_FUNC` sync call, fields + methods + static dispatch, `new`/instantiation grammar | **Medium** (1 new IL call primitive) | substrate |
| **15.B** | Abstract classes + `trait` composition: compile-time static vtables, local→type symbol table, dynamic dispatch, collision = compile error | **Medium-large** | 15.A |
| **15.C** | Generics `<T>` via monomorphization: capture + re-lower class body per instantiation | **Large (deepest)** | 15.A (+15.B for bounded generic traits) |
| **15.D** | Event subsystem: `teko_event` runtime, `OP_EVENT_*`, `event`/`raise`/`subscribe` + `fanout`/`fire_and_forget` over Phase 14 spawn | **Bounded** (substrate exists) | Phase 14 spawn only |

**Start point: 15.A (concrete classes)** — it is the spine the abstract/trait/generic
blocks all build on, and it establishes the object runtime + the synchronous-call primitive.
15.D (events) is independent and could be pulled forward as an early win if a concurrency
proof is wanted first; default order is A → B → C → D.

## Status

**15.A (concrete classes) is DONE** on both targets — the `class` token is LIVE with executable
`.tks` proofs (native + WASM), zero runtime reflection. Delivered in four increments:
1. `teko_object.c` runtime (handle-based register-width field-cell store) + 5 KATs.
2. `OP_OBJ_NEW/SET/GET/FREE` family — native `teko_rt_object_*` + WASM reactor import, both
   backends, CSE-safe, flag-gated (object-free programs byte-identical).
3. `OP_CALL_FUNC` — the synchronous table-call primitive (method dispatch): native `teko_rt_call`
   (routine fn typedef widened to return `long`); WASM `call_indirect` + a `FUNC_END` `$w0`→`frame[0]`
   spill so a sync caller reads the result.
4. Frontend `class` grammar — `collect_classes` layout/method pre-pass, member access via the
   dotted-identifier lexeme (`obj.field` read/write → `OP_OBJ_GET/SET`, `obj.method(args)` →
   `OP_CALL_FUNC` static dispatch with `self`=arg0), `ClassName()` instantiation → `OP_OBJ_NEW`,
   `emit_method_routines` (method bodies as table routines, `self` bound + class-scoped), `return`.

Proofs: `runtime/native/samples/class.tks` → `7`, `70`; `runtime/wasm/samples/class.tks`
(`run-class.mjs`) → `[7,70]`. Suite 213/213; all four gates' local equivalents green.

**Method signatures mirror function signatures (owner decision).** A method carries a COMPLETE
signature with a return type (`fn sum(self): i32`), may be GENERIC (`fn scale<T>(self, k): i32`),
and may be `async` (returns `intent<>`). The frontend parses all three forms (a leading `async` is
consumed; a balanced `<…>` generic clause is skipped before the param list; the `: ReturnType` is
skipped to the body). In this MVP a generic method binds + lowers exactly like a concrete one (the
value model is uniform i32) and an `async` method lowers synchronously — **per-type
monomorphization is 15.C; full `intent<>`/`await` async-method semantics are a dedicated later
sub-block.** The proofs use the complete-signature convention (incl. a generic method).

**15.B (abstract classes + traits) is DONE** on both targets — dynamic dispatch through a
**compile-time static vtable**, trait composition with **collision = compile error**, `to_string`
dispatched through the vtable like any method. Four increments:
1. `teko_vtable.c` runtime (`vtable[type_id][method_id] → routine slot`, -1 unset sentinel) +
   `OP_VTABLE_SET/GET` (0x6F/0x70), native `teko_rt_vtable_*` + WASM reactor import. 4 KATs.
2. `trait NAME { fn m(self): T; }` registry + `class C : T1, T2` implements clause + `abstract`
   modifier + bodyless (abstract, slot -1) methods + a global method-id space. **Collision =
   compile error** (`check_oop_collisions` → `teko_compile_interop` returns non-zero, emits nothing;
   a class override resolves it).
3. **Fat trait-typed locals** (`let g: Trait = c;` → instance-handle slot + a hidden type-id slot
   holding the concrete type_id as a compile-time constant; object layout unchanged, 15.A
   byte-identical). Vtable populated at `$main` start (`vtable_set` per bodied method). Dynamic
   dispatch `g.method()` → `vtable_get(type_id, method_id)` → `OP_CALL_FUNC`. Reassigning `g`
   updates its type-id slot.
4. WASM proof + a **nesting-safe `OP_CALL_FUNC`** (stack-disciplined `$callfp` frame: reserve
   before the call, restore after — so a method calling its own/another method works on WASM).

Proofs: `runtime/{native,wasm}/samples/traits.tks` → `12, 112, 9, 209` (a `Shape`-typed reference
dispatches `area()`/`to_string()` to `Circle` then, after reassignment, `Square`; `to_string` via
the same vtable). Suite 221/221.

**Decision (15.B):** dynamic dispatch = a runtime-stored static vtable (compile-time-populated,
C++ vptr / Go itab model) + fat trait-typed locals carrying the type-id as a compile-time constant.
This keeps the object layout untouched (no per-object vptr), so concrete-class programs stay
byte-identical to 15.A. Collision rule: a class composing two traits that both declare a method name
without overriding it is a hard compile error. An inline branch-chain "vtable" was rejected in favor
of the indexed runtime table (O(1), faithful to "vtable").

**15.C (generics — real per-type monomorphization) is DONE** on both targets. A generic
`class Box<T>` is specialized into one concrete `Box$Arg` per instantiation (the type parameter
substituted at compile time — zero runtime cost, no runtime type parameter). `collect_generics`
discovers templates + instantiations; `collect_classes` clones a concrete instance per type-arg
(method-slots deferred so mono instances slot after all non-generic classes — dense table);
`emit_mono_routines` re-lexes the template body per instance with the type-param substitution active
(`g_subst`: `T` → `Arg`), so `T()` instantiates `Arg` and a `T`-typed local is `Arg`-typed
(`resolve_type_name`). Instantiation lowering handles `Class()`, `T()`, and `Box<Arg>()` (→
`Box$Arg`). Proof `runtime/{native,wasm}/samples/generics.tks` → `11, 22` (`Factory<Circle>` /
`Factory<Square>` each specialize `make()` so `T()` makes the right type, `t.tag()` static-dispatches).
Suite 222/222.

**Decision (15.C):** monomorphization re-lexes the generic template body per concrete type-arg with
a name-substitution (`T`→`Arg`) — never a runtime type parameter. The uniform-i32 placeholder of
15.A is replaced for instantiated generics; an un-instantiated generic template emits no code.

**15.D (event subsystem) is DONE** on both targets — a static-subscription event system over the
Phase-14 cooperative runtime (frontend-only, no new C runtime). `event E;` declares; `subscribe E
with H [fanout|fire_and_forget];` registers a handler (top-level `fn` → routine slot) + a delivery
mode at subscription time (compile-time-static subscriber set, `collect_events`); `raise E(args);`
fan-outs to every subscriber by spawning each handler over the scheduler (drained at exit — deferred
fan-out). `fanout` = parallel green threads (real parallelism on Layer B), `fire_and_forget` =
enqueue-no-join; both spawn in the cooperative MVP. Proof `runtime/{native,wasm}/samples/eventbus.tks`
→ `1, 2, 15, 25` (main body, then the two handlers at exit). Suite 223/223.

## PHASE 15 IS COMPLETE
All four sub-blocks done & CI-green on all four gates (incl. Windows MSVC): 15.A concrete classes,
15.B abstract + traits (static vtables, collision = compile error, `to_string`-via-vtable hook for
Phase 16), 15.C generics (real per-type monomorphization), 15.D events. **No dead tokens** — every
reserved OOP token (`class`/`abstract`/`trait`/`event`/`raise`/`subscribe`/`fanout`/
`fire_and_forget`) is LIVE with an executable `.tks` proof on **native AND WASM**. Zero runtime
reflection throughout (compile-time field indices, method slots, static vtables, monomorphization).
Suite 223/223; ASan/UBSan (both dispatch paths) + TSan green; 16 native goldens intact. **Ready to
leave draft** (PR #8; the human merges — no merge/force-push from the agent). Deferred to a dedicated
later sub-block (owner): full `async`-method `intent<>`/`await` semantics on the Phase-14 engine
(parsing is accepted today; bodies lower synchronously).

## The `to_string` convention hook (for Phase 16's auto-conversion)

**Owner directive (cross-phase):** every type — primitive, complex, and user-defined — has an
automatic `to_string` producing a **culture-invariant default** representation, **auto-invoked when
a value is concatenated with / interpolated into a string**. The auto-call *machinery* is **Phase 16
(Casting / Type Conversions & Parsing)**; this OOP design must leave it viable and never paint it
into a corner. The contract Phase 15 commits to:

- **`to_string` is an ordinary, conventionally-named method.** A user-defined type exposes its
  string form by defining `fn to_string(self): str { … }`. It is **overridable** (a class redefines
  it) and **inheritable** (15.B: an abstract base or `trait` may supply a default `to_string` that
  concrete classes inherit unless they override). No special grammar — it rides the exact same
  method machinery as any other method.
- **Discoverable by name at compile time.** The class registry already resolves methods by name
  (`class_method_idx(ci, "to_string")`), so Phase 16 can ask "does this type define/inherit
  `to_string`?" and get a definite compile-time answer (slot, or absent). **15.B's vtable layout
  MUST keep methods name-resolvable** (no name-mangling/numbering scheme that hides `to_string`) and
  **MUST place an inherited/overridden `to_string` in a stable, dispatchable slot** — so a
  trait/abstract-typed reference can `to_string` through the vtable while a concrete static type
  dispatches it directly.
- **Dispatch reuses the existing primitives, zero runtime reflection.** A concrete-static receiver
  lowers `to_string` to a direct `OP_CALL_FUNC` (static dispatch, exactly like `p.sum()` today); an
  abstract/trait-typed receiver lowers it to a vtable-slot `call_indirect` (15.B). Phase 16 resolves
  the concrete `to_string` at compile time — never a reflective runtime walk.
- **Default when absent.** If a user-defined type neither defines nor inherits `to_string`, Phase 16
  synthesizes the culture-invariant default (e.g. a generated per-type stringifier over the fields,
  in the Go-style monomorphized-generator spirit of Phase 19) — the registry's "absent" answer is
  the trigger. Phase 15 owns only the *hook* (the conventional, dispatchable method + name
  resolution); Phase 16 owns the synthesis + the auto-call at concat/interpolation sites.

**Status in 15.A:** the hook is already viable — methods are resolved by name and dispatched via
`OP_CALL_FUNC`, so a class that defines `fn to_string(self): str` works today with no new machinery
(it is just a method). 15.B will extend this to inherited/trait-default `to_string` via the static
vtable, explicitly preserving name-resolvability. No design choice in 15.A blocks the auto-call.

## Discipline (unchanged, non-negotiable)
One increment per commit; build + suite; **ASan + UBSan on BOTH dispatch paths + TSan**
clean each commit; the **16 native emitter goldens never regress**; all four CI gates
(native / wasm / wasm-threads / sanitizers) green before any sub-block is "done"; patient
CI watch (≥90s); **no dead tokens** (executable `.tks` proof per surface, native + WASM);
**no merge / force-push** — the human merges. New C runtimes are the single source of
truth, KAT-tested in the Unity suite, compiled to the wasm32 reactor for WASM and linked
via `teko_rt` for native.
