---
seq: 0023
crumb-id: COL-F0c
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:129-130"   # FASE 0 teaching item 4
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:419-427"     # §2.5(3) wrap-refcount law
  - "docs/design/mudancas-superficie-0.3.1.md:1614-1637"                 # Doc-2 three-category; wrapped kind
---

# 0023 · COL-F0c — wrapped retain/release (refcount-wrap: root-arena `addr→count` dict)

> Wrapped retain/release (refcount-wrap: root-arena `addr→count` dict) — the `wrapped` kind's precise
> lifetime mechanism.

## Goal

Teach the compiler the WRAPPED-refcount intrinsics `retain(obj)` / `release(obj)` for the `wrapped`
memory category ONLY (service / opaque-ref / FFI, opt-in — Doc-2 three-category law
`mudancas-superficie-0.3.1.md:1614-1637`; the escape-hatch sealed at `:1757`). A `wrapped` object carries a
reference count in an `addr→count` dict held in the ROOT arena: a new reference (`push`/`get`/`pop` copying
the pointer) INCREMENTS; releasing a reference (`remove`/`set`/scope-exit) DECREMENTS; **at zero →
`region_drop` of the object's own region** (F2, O(1)). It is ATOMIC when the object is shared cross-thread.
This is the PRECISE mechanism (frees the moment the last reference drops) for a `wrapped` object that
outlives its birth scope — where the conservative LUB/region-drop of COL-F0b could only free at the outer
scope's end. `class` stays region-drop (COL-F0b); refcount is `wrapped` ONLY. Purely ADDITIVE and INERT:
the compiler itself uses NO `wrapped` objects (its collections hold VALUE and `class`), so a `[dry]` build
is byte-identical. Its seed folds into SM-R1.

## Where

- `src/checker/scope.tks:265` — `builtin_fn` (the `byte_ptr`/`word_ptr` neighbourhood) — register the
  `retain`/`release` intrinsic signatures.
- `src/codegen/codegen.tks:2589` — `emit_byte_ptr` neighbourhood (the `teko::mem` emitter block) — ADD
  `emit_retain`/`emit_release` special emitters that inc/dec the root-arena count word and, at zero, emit
  the `region_drop`.
- `src/runtime/arena.tks:704,696` — `region_root`/`region_drop` — the `addr→count` dict lives in the ROOT
  arena (`region_root`); zero triggers `region_drop` of the object's region.
- `src/runtime/sync.tks:64` — `teko::sys::atomic_cas_u32` (used by `mtx_lock`) — the atomic inc/dec path for
  a cross-thread-shared `wrapped` object; the maintained-C atomic seed already exists (`teko_rt` exception).

NEW: no new module; two builtin-call intrinsics + a root-arena `addr→count` dict helper in `arena.tks`.

## How

1. **The refcount dict in the ROOT arena.** A `wrapped` object's header address keys a count in a dict held
   in the root arena (survives the object's own region churn). `retain` inc, `release` dec; at zero the
   object's region is dropped. The dict itself is pure-Teko over `of_len`+`[]u64` (COL-F0a), NO `teko_rt.c`.

```teko
/**
 * retain — register a new reference to a WRAPPED object: increment its count in the root-arena `addr→count`
 * dict (ATOMIC when the object is cross-thread-shared). Called by `push`/`get`/`pop` when they copy a
 * `wrapped` pointer. Applies to the `wrapped` kind ONLY — a plain `class` is region-drop-via-escape
 * (COL-F0b), never refcounted; a VALUE is bump + bucket (COL-F0a).
 *
 * @param obj  the wrapped object whose reference count is incremented
 * @since 0.3.1
 */
fn retain<T>(obj: T)

/**
 * release — drop a reference to a WRAPPED object: decrement its count in the root-arena `addr→count` dict
 * (ATOMIC when cross-thread-shared); AT ZERO → `region_drop` of the object's own region (F2, O(1)). Called
 * by `remove`/`set`/scope-exit. This is the PRECISE lifetime (frees at the last drop) the conservative
 * class-holder LUB could not give. `wrapped` ONLY.
 *
 * @param obj  the wrapped object whose reference count is decremented (freed at zero)
 * @since 0.3.1
 */
fn release<T>(obj: T)
```

2. **Zero → region_drop.** `release` decrements; when the count reaches zero it emits `region_drop` of the
   object's region (`arena.tks:696`) — O(1), the object's own per-object region (F2). Multiple references
   keep the object alive while any count > 0; releasing one never dangles the others.
3. **Atomic when cross-thread.** A `wrapped` object shared across threads uses the atomic inc/dec
   (`teko::sys::atomic_cas_u32`, the same primitive `mtx_lock` uses, `sync.tks:64`); the sequential case may
   use a plain word. No new atomic in `teko_rt.c` (the maintained-C seed already exposes CAS).
4. **`class` stays region-drop.** This crumb does NOT touch the `class` path (COL-F0b). The
   promote-`class`-to-`wrapped` optimization (GATE-1) is an additive follow-up that REUSES these intrinsics
   when the owner closes GATE-1 — but COL-F0c itself only teaches the `wrapped` mechanism.
5. **Stay inert.** The compiler holds no `wrapped` objects; the intrinsics have no `src/` caller → `[dry]`
   byte-identical.

## Rulings & laws

- **Teko-only:** checker/codegen `.tks` + the pure-Teko root-arena dict; the CAS atomic is the maintained-C
  `teko_rt` seed exception (no NEW C).
- **W15 full Javadoc** on `retain`/`release` and helpers; flatten/extract; no inline `//`.
- **Doc-2 three-category (SEALED `:1614-1637` + `:1757`):** refcount is the **wrapped** kind ONLY; `class`
  is region-drop-via-escape (COL-F0b); value is bump + bucket (COL-F0a).
- **Atomicity:** cross-thread-shared `wrapped` inc/dec is atomic; the sequential case may be a plain word.
- **Additive/inert:** no corpus `wrapped` object → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

The compiler uses NO `wrapped` objects, so this whole path is UN-exercised by the self-build — it needs its
own isolated oracles (the reference-count lifecycle the fixpoint never walks):

| fixture | asserts | expected |
|---|---|---|
| `wrapped_retain_release` | a wrapped object retained twice, released twice: freed EXACTLY at zero, not before; no UAF, no leak | 0 |
| `wrapped_shared_no_early_free` | a wrapped object referenced by a collection AND an external binding: `remove` from the collection decrements but does NOT free (count > 0); the external binding stays valid | 0 |
| `wrapped_zero_region_drop` | the last `release` drives the count to zero and drops the object's region ON THE SPOT (not at outer-scope end); token == objects freed | 0 |

## Gate

`[dry]` — compile + the three fixtures + fixpoint (byte-identical; intrinsics inert — no corpus `wrapped`).
"Green" = `retain`/`release` inc/dec the root-arena `addr→count` dict, zero triggers `region_drop` of the
object's region, cross-thread inc/dec is atomic, `class` stays region-drop, `[dry]` build byte-identical.
Reseed-class: `(folds R1)`.

## Deps

`COL-F0a` (the `of_len`+`[]u64` substrate the `addr→count` dict is built over).

## Done when

`retain`/`release` maintain a root-arena `addr→count` dict for the `wrapped` kind (atomic when
cross-thread; zero → `region_drop` of the object's region), `class` stays region-drop-via-escape, the
fixtures pass, and a `[dry]` build is byte-identical (intrinsics inert on the corpus).
