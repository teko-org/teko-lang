---
seq: 0024
crumb-id: COL-F0d
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:131-143"   # FASE 0 teaching items 5-8
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:428-458"     # §2.5(4) deep_copy contract + cap 255
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md:962-999"     # §9 node-linked substrate (chunk-node)
---

# 0024 · COL-F0d — weak-ref hook + `deep_copy` + chunk-node capability + CAS helper

> Weak-ref hook + `deep_copy` + chunk-node capability + CAS helper — the last four FASE-0 surfaces the
> library needs.

## Goal

Teach the compiler the remaining four FASE-0 surfaces so the whole collection library is pure `.tks` over
them: (1) the **weak-ref hook** — a non-retaining reference against the COL-F0c wrap-refcount table (`get`
upgrades to a live reference iff count > 0), the cycle-breaker for `WeakMap`/`WeakSet` (`0086`); (2)
**`deep_copy<T>(o): T | error`** — the monomorph-driven recursive clone with a HARD `u8::MAX`=255 depth cap
that RETURNS the `error` variant at the cap (never truncates, never silently shares a reference at the
frontier), the explicit opt-in distinct from the default reference copy; (3) the **chunk-node capability** —
expose the arena's intrusive chunk-list (`ChunkNode` alloc/link over `CHUNK_NEXT/CHUNK_CAP/CHUNK_USED`) so
the pure-Teko `ChunkChain<T>` (`0070` Q1) links fixed chunks and drops regions without new teaching; (4) the
**CAS-append helper** — confirm `teko::sys::atomic_cas_*` reaches a tail-link/watermark-bump helper (the
thread-safe growth point). Purely ADDITIVE and INERT: no `src/` caller yet (FASE 1 is the first), so a
`[dry]` build is byte-identical. Its seed folds into SM-R1.

## Where

- `src/checker/scope.tks:265` — `builtin_fn` — register the weak-ref hook + `deep_copy` intrinsic
  signatures beside `byte_ptr`/`retain`.
- `src/codegen/codegen.tks:2589` — the `teko::mem` emitter block — ADD `emit_deep_copy` (walks the
  monomorphized fields with the `u8` depth counter) and the weak-read emitter (a non-retaining table read).
- `src/runtime/arena.tks:13-19` — `CHUNK_NEXT`/`CHUNK_CAP`/`CHUNK_USED` (+ `region_alloc`/`region_drop`
  `:685,696`) — expose a `ChunkNode` alloc/link surface over the existing intrusive chunk-list.
- `src/runtime/sync.tks:63` — `mtx_lock` / `teko::sys::atomic_cas_u32` (`:64`) — expose the CAS-append
  tail-link/watermark-bump helper if missing (the mutex/condvar side already exists).

NEW: no new module; four builtin/runtime surfaces registered in the existing checker/codegen/arena/sync
tables.

## How

1. **The weak-ref hook** (against COL-F0c's `addr→count` table): a read that does NOT bump the count; `get`
   upgrades to a momentarily-retained live reference iff the target's count > 0, else null.

```teko
/**
 * weak_get — a NON-retaining read of a WRAPPED target through the COL-F0c wrap-refcount table: does NOT
 * increment the count, so it never keeps the target alive. Returns a live (momentarily retained) reference
 * iff the target's count is still > 0 (the object has not been region-dropped), else null. The cycle-
 * breaker: a weak edge in a two-object cycle lets the strong side reach zero and free the whole cycle.
 *
 * @param w  the weak (raw, non-retaining) address of the target in the refcount table
 * @return   a live retained reference to the target, or null if it has been freed
 * @since 0.3.1
 */
fn weak_get<T>(w: *T): T | null
```

2. **`deep_copy`** (the explicit opt-in clone, copy the contract VERBATIM from the source doc):

```teko
/**
 * deep_copy — produce an independent exact copy of an object graph, or fail. Reference-typed fields are
 * cloned recursively (a new materialization), NOT shared. Depth is hard-capped at `u8::MAX` (255): on
 * reaching depth 255 — a graph too deep or a cycle — the function returns the `error` variant carrying
 * "profundidade máxima (255) excedida / possível ciclo"; it NEVER truncates or silently shares a
 * reference at the frontier. The copy is therefore EXACT or an explicit failure, never a silent partial
 * copy. The cap bounds both stack overflow and cyclic graphs with a cheap `u8` depth counter — NOT a
 * visited-set cycle detector. Distinct from the default reference copy (`get`/`pop` share the pointer);
 * use this only when an independent clone is required.
 *
 * @param o   the object (graph) to clone
 * @return    an independent copy, or `error` when depth 255 is exceeded
 * @since 0.3.1
 */
exp fn deep_copy<T>(o: T): T | error
```

3. **The chunk-node capability.** Expose `ChunkNode` alloc/link over the arena's intrusive chunk-list
   (`CHUNK_NEXT`/`CHUNK_CAP`/`CHUNK_USED`, `arena.tks:13-19`) so `ChunkChain<T>` (Q1) can link a NEW fixed
   chunk on grow (never a whole-backing swap — the TS substrate) and drop a chunk's region — without any new
   teaching. The same `Node`/link pattern the arena's own dynamic chunk-list already uses (§9.0: the arena
   IS a node-linked collection).

```teko
/**
 * chunk_node_link — allocate a fresh FIXED chunk from region `r` and link it after `tail` via the intrusive
 * chunk-list slots (`CHUNK_NEXT`/`CHUNK_CAP`/`CHUNK_USED`). The growth point of ChunkChain<T> (Q1): a grow
 * LINKS a new fixed chunk, it never swaps a whole backing (which would be an RMW race and value-move). No
 * existing chunk is touched or freed. Mirrors the arena's own dynamic chunk-list (§9.0 — same pattern).
 *
 * @param r    the region to allocate the new chunk in
 * @param tail the current tail chunk to link after
 * @param cap  the fixed slot count of the new chunk
 * @return     the newly linked chunk
 * @since 0.3.1
 */
fn chunk_node_link(r: ptr, tail: ptr, cap: u64): ptr
```

4. **The CAS-append helper.** Confirm `teko::sys::atomic_cas_u32` (`sync.tks:64`) reaches a
   tail-link/watermark-bump helper — the lock-free thread-safe growth point of ChunkChain. Teach ONLY if a
   helper is missing; the mutex/condvar side (`mtx_lock`/`cv_*`) already exists (`sync.tks:63,78`).
5. **Native leg = honest-stop; stay inert.** `deep_copy`/`weak_get` native lowering is Doc-2 terminal
   (honest-stop in `lower_call`'s terminal `_ =>`, addressed by NAT-*, M4); the C leg emits. No `src/`
   caller yet → `[dry]` byte-identical.

## Rulings & laws

- **Teko-only:** checker/codegen/arena/sync `.tks`; the CAS atomic is the maintained-C `teko_rt` seed
  exception (no NEW C).
- **W15 full Javadoc** on every new surface (`deep_copy` is `exp` — stdlib); flatten/extract; no inline `//`.
- **Deep-copy cap = `u8::MAX` (255) HARD, error-at-cap (SEALED `:1757`):** never truncates, never silently
  shares; the caller handles `T | error` by `match` (Teko's standard error model).
- **Weak = non-retaining (Doc-2 wrapped):** `weak_get` never bumps the count; upgrades iff count > 0.
- **Chunk-chain is the TS substrate (record §2):** grow LINKS a fixed chunk, never a whole-backing swap.
- **Additive/inert:** no corpus caller → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

The self-build FIXPOINT does not exercise weak refs, deep clones, or the cross-thread CAS growth (the
compiler is single-threaded and uses no weak/deep surface), so each needs an isolated oracle:

| fixture | asserts | expected |
|---|---|---|
| `deepcopy_depth_cap` | `deep_copy` of a graph deeper than 255 (or cyclic) RETURNS the `error` variant; `match` catches it; no silent partial copy | 0 |
| `deepcopy_exact` | `deep_copy` of a shallow graph is an independent clone; mutating the clone leaves the original; refcounts correct | 0 |
| `weak_dead` | a weak ref to a freed target returns null (does not keep it alive) | 0 |
| `weak_cycle_break` | a two-object cycle with one weak edge is fully freed (the cycle-breaker) | 0 |
| `chunk_node_link_grow` | a ChunkChain-shaped grow LINKS a new fixed chunk (no whole-backing swap, no value move); order + count correct | 0 |

## Gate

`[dry]` — compile + the five fixtures + fixpoint (byte-identical; all four surfaces inert until FASE 1
adopts them). "Green" = `weak_get` upgrades iff count > 0, `deep_copy` returns `error` at depth 255,
`chunk_node_link` links a fixed chunk, the CAS-append helper reaches the tail, `[dry]` build byte-identical.
Reseed-class: `(folds R1)`.

## Deps

`COL-F0a` (the fixed-backing substrate; the weak hook also reads COL-F0c's refcount table, but F0c lands
first in seq and is not a hard build edge for the additive registration).

## Done when

The weak-ref hook, `deep_copy` (cap 255 → `error`), the `ChunkNode` alloc/link capability, and the
CAS-append helper are all registered and emit on the C leg (native = honest-stop), the fixtures pass, and a
`[dry]` build is byte-identical (all four inert until FASE 1 adopts them).
