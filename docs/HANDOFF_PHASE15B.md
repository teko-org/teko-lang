> **SUPERSEDED (2026-06-16):** the work this hand-off described was completed in the SAME session — no new session was opened. Phase 15 (15.A–15.D) is DONE & CI-green. This doc is kept only as a record of the design rationale; the canonical status is `docs/PHASE15_OOP.md`.

# Hand-off — Phase 15.B (abstract classes + traits, static vtables)

**Branch:** `feat/phase-15-oop` (PR #8, draft). **15.A is DONE & CI-green** (concrete classes;
21/21 checks). **15.B increment 1 is DONE & green locally** (the static-vtable runtime). This doc
hands off the remaining 15.B frontend integration so a fresh session can finish it with full context.

Read first: `docs/PHASE15_OOP.md` (design + the `to_string` hook + the 15.B vtable plan), `CLAUDE.md`,
`src/CLAUDE.md`. The 15.A machinery in `src/frontend_interop.c` is the substrate you extend.

## What 15.B delivers (owner ask)
Abstract classes + `trait` composition with **compile-time static vtables**; **method-name
collision across composed traits = compile error**. Plus: confirm `to_string` resolves through the
vtable like any method (the Phase-16 casting hook) and document the convention.

## Chosen design (decided, partially built)
- **Static vtable = a runtime table populated at compile time.** `teko_vtable.c` (DONE, increment 1)
  stores `vtable[type_id][method_id] = routine_slot`. The compiler assigns each concrete class a
  dense `type_id` and each trait/abstract method a dense `method_id`, emits `teko_vtable_set` calls
  at `$main` start, and at a dynamic call site does `teko_vtable_get(type_id, method_id) -> slot`
  then dispatches via the 15.A `OP_CALL_FUNC`. Zero runtime reflection (mapping fixed at compile time).
- **Type discriminant flows via FAT trait-typed locals** — NOT stored in the object. A
  `let g: Trait = c;` local gets TWO `$v` slots: `g$handle` (the instance handle) and `g$tid` (the
  concrete class's `type_id`, a COMPILE-TIME CONSTANT known from the RHS's static type). `g = s;`
  updates both. `g.method()` loads `g$tid`, `vtable_get(tid, method_id) -> slot`, stages `g$handle`
  as arg0, `OP_CALL_FUNC`. **This keeps the object layout unchanged → 15.A stays byte-identical.**
- **Concrete-typed receivers keep 15.A static dispatch** (direct `ICONST slot; OP_CALL_FUNC`). Only
  trait/abstract-typed receivers use the vtable path.

### Increment 1 — DONE (commit on branch)
`teko_vtable.c`/`.h` + 4 KATs; `OP_VTABLE_SET=0x6F`/`OP_VTABLE_GET=0x70` wired end-to-end (codegen_li
emit_vtable + `uses_vtable`; CSE sets; native `teko_rt_vtable_*`; WASM reactor import +
`wasm_emit_vtable`; reactor SRCS/EXPORTS); IL emission test. Suite 213→218. All sanitizers + goldens green.

## Remaining increments (to build)

### Increment 2 — `trait`/`abstract` grammar + registry + type-ids + collision detection (frontend)
In `src/frontend_interop.c`, alongside the 15.A class registry (`g_class`, `collect_classes`):
- **Trait registry** `g_trait[]`: each `trait NAME { fn m(self): T; … }` → trait name + ordered
  abstract-method names. Parse trait decls (bodyless `fn` sig ending in `;`). Lexer tokens
  `TOKEN_TRAIT`/`TOKEN_ABSTRACT` already exist (Phase 12). A `skip_trait_decl` mirrors
  `skip_class_decl`.
- **Class implements clause**: parse `class C : Trait1, Trait2 { … }` (and `abstract class`). Record
  each class's trait list in `ClassInfo`. (The `:` after the class name currently isn't parsed —
  `collect_classes`/`skip_class_decl`/`emit_method_routines` skip to `{`; add a `: Trait,…` parse
  before the `{`.)
- **Dense `type_id` per concrete class**: assign in `collect_classes` (e.g. the class index).
- **Global `method_id` per trait method NAME**: a method name shared across traits/classes maps to
  ONE `method_id` (so `g.greet()` resolves the same id regardless of concrete type). Keep a
  name→method_id table.
- **Collision detection**: when a class composes traits whose method-name sets intersect (the SAME
  method name declared by ≥2 of its traits) and the class does not provide a single overriding
  impl, that's an ambiguous composition → emit a diagnostic to stderr + make `teko_compile_interop`
  signal failure (return non-zero / set an error flag the CLI surfaces). Negative test asserts it.
  (MVP: simplest rule — two composed traits declaring the same method name = compile error, since
  the trait surfaces conflict. Document the exact rule chosen.)
- Tests: a frontend test asserting the registry (trait methods, class trait list, type_ids,
  method_ids) + a collision negative test.

### Increment 3 — vtable population + fat trait-typed locals + dynamic dispatch (frontend)
- **Population at `$main` start**: before the main lowering loop, for every (concrete class C
  implementing trait method m): `teko_vtable_set(C.type_id, m.method_id, C.method_slot_for_m)`.
  Emit `teko_vtable_reset` first (stage args via SETARG + `codegen_li_emit_vtable`). The method slot
  is the routine slot `collect_classes` already assigns. (A class may inherit a trait DEFAULT method
  — if the class doesn't define `m` but a trait provides a default body, the slot is the trait
  default's routine; emit trait default bodies as routines too, like `emit_method_routines`.)
- **Fat trait-typed locals**: extend the local model so a `let g: Trait = rhs;` allocates a handle
  slot + a tid slot, records `g` in a side map `name → {trait, handle_slot, tid_slot}` (parallel to
  `localcls`). Set `handle_slot = rhs_handle`, `tid_slot = ICONST rhs_concrete_type_id`. Handle
  reassignment `g = rhs2;` (update both). The RHS's concrete type comes from `localcls`/instantiation.
- **Dynamic dispatch** at `g.method()` when `g` is trait-typed: load `tid_slot` → stage as
  vtable_get arg0; `ICONST method_id` → `$w0`; `OP_VTABLE_GET` → slot in `$w0`; stage `g$handle` as
  `$a0` + any explicit args; ICONST? No — the slot is now in `$w0`, but `OP_CALL_FUNC` wants the
  slot in `$w0` AND args staged in `$a0..`. Order: stage self(`g$handle`)→`$a0`, explicit args→`$a1..`,
  THEN compute slot via VTABLE_GET into `$w0`, THEN `OP_CALL_FUNC(argc)`. (VTABLE_GET clobbers `$w0`
  and uses `$a0` for its own arg — careful: stage the CALL_FUNC args AFTER VTABLE_GET, or spill the
  slot to a temp. Cleanest: VTABLE_GET → store slot to a temp local; stage self+args into `$a*`;
  load slot temp → `$w0`; CALL_FUNC. Mirrors the member-write temp pattern in 15.A.)
- Hook into `lower_member_call` / `lower_member_stmt` / `eval` paths: if the receiver is a fat
  trait-typed local → dynamic path; else (concrete) → existing 15.A static path. Extend
  `lower_let_stmt` to detect `: Trait` and create the fat local.

### Increment 4 — executable proofs (native + WASM) + `to_string`-via-vtable + collision
- `runtime/{native,wasm}/samples/traits.tks`: a trait with ≥2 implementors; a `Trait`-typed local
  reassigned across concrete types; the same call site dispatches to each impl (assert distinct
  outputs). Include a `to_string`-named trait method dispatched through the vtable to prove the
  Phase-16 hook. Native via `run-native.sh` (add a `check`), WASM via a new `run-traits.mjs` (mirror
  `run-class.mjs`; rebuild the reactor — `teko_vtable.c` is already in its SRCS/EXPORTS) wired into
  `wasm.yml` (build + wat2wasm + run steps, like `class.tks`).
- A negative/compile-error proof for trait-method collision (a unit test asserting the frontend
  signals the error — keep it as a Unity test, not a CLI build that must fail in CI).
- Update `docs/PHASE15_OOP.md` (mark 15.B done, record the collision rule + the fat-local design)
  and `CLAUDE.md` (Phase 15 status). Confirm the `to_string` convention section already present is
  satisfied (it is — `to_string` is resolved by name → method_id → vtable slot like any method).

## Discipline (unchanged)
1 increment/commit; build + suite; **ASan+UBSan on BOTH dispatch paths + TSan** each commit; the 16
native goldens never regress; **all 4 CI gates green** before "done" (patient ≥90s watch);
**executable `.tks` proof native + WASM, no dead token**; **no merge/force-push** — the human merges.
Watch out for the **Windows LLP64 `long` truncation** lesson (15.A): keep pointer-carrying cells
`intptr_t`. The vtable stores `long` slots (small ints) — fine.

## Quick verification commands
```sh
cmake -S . -B build >/dev/null && cmake --build build --target teko teko_rt teko_tests -j4 && ./build/teko_tests   # 218/218
# native class proof (15.A, still green): 7 then 70
./build/teko build runtime/native/samples/class.tks --target=host --rt-lib=./build/libteko_rt.a -o /tmp/c && /tmp/c
# WASM: bash runtime/wasm/crypto/build-crypto-reactor.sh ; teko build … --target=wasm ; wat2wasm ; node runtime/wasm/run-class.mjs
```
