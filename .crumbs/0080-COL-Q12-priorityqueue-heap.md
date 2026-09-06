---
seq: 0080
crumb-id: COL-Q12
milestone: M2
gate: "[dry]/[fixpoint]"
reseed-class: "none/fixpoint"
deps: [COL-Q6, COL-Q11]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:678-692"   # Q12 PriorityQueue → Heap
  - "docs/design/plano-mestre-0.3.1-implementacao.md:240"                # M2 collections row COL-Q12
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md"             # remodel §7 ("na dúvida, RITUAL")
---

# 0080 · COL-Q12 — convert `PriorityQueue<T>` to the Heap base

> Convert `PriorityQueue<T>` → the `Heap<T>` base (COL-Q6), replacing `heap_sift_up`/`heap_pop_min` over
> `teko::list::push`. `[dry]` if not core-consumed, else `[fixpoint]`.

## Goal

Re-back `PriorityQueue<T>` on the `Heap<T>` base (COL-Q6), replacing the ad-hoc `heap_sift_up`/`heap_pop_min`
over `teko::list::push` at `src/collections/priority_queue.tks:16-17,24-28` with delegation to the chunk-chain
heap. Build-first: the `heap_sift_up`/`heap_pop_min` helpers (`collections.tks:108,122`) stay defined (removed
at COL-F2); the queue stops calling them. Gate is CONDITIONAL: `[dry]`/none if `src/` does not core-consume a
`PriorityQueue`; else `[fixpoint]`/fixpoint (remodel §7 "na dúvida, RITUAL" — prefer `[fixpoint]` on ANY doubt).
Measured default: the self-build does not appear to consume `PriorityQueue` → `[dry]` with FULL fixtures.

## Where

- `src/collections/priority_queue.tks:16-17,24-28` — replace `teko::list::push(self.heap, x)` + `heap_sift_up`
  and `heap_pop_min(&h)` with delegation to a `Heap<T>` (`enqueue`→`push`, `dequeue`→`pop_min`, `peek`→`peek`).
- `src/collections/collections.tks:108` (`heap_sift_up`), `:122` (`heap_pop_min`) — still defined until COL-F2;
  NOT removed here (build-first).

## How

1. **`PriorityQueue<T>` wraps `Heap<T>`** (COL-Q6, a binary min-heap over a ChunkChain): `enqueue`→`Heap::push`
   (append + sift up by swapping POINTERS, O(log n)), `dequeue`→`Heap::pop_min` (root out, last to root, sift
   down), `peek`→`Heap::peek`. Copy the W15 doc-comments per the Q9 pattern.
2. **Growth** is the chunk-chain's (link a chunk, never a swap) — the old `teko::list::push` copy-grow is gone.
3. **Build-first:** `heap_sift_up`/`heap_pop_min` stay defined; the queue stops calling them; COL-F2 removes the
   now-dead helpers.

```teko
/**
 * enqueue — insert `x` into the priority queue and restore the heap order in O(log n), delegated to the
 * Heap<T> base (COL-Q6): append to the chunk-chain, sift up by swapping pointers (no value move). Replaces the
 * retired `teko::list::push` + `heap_sift_up` copy-grow path.
 *
 * @param x  the element to enqueue
 * @since 0.3.1
 */
pub fn enqueue(x: T)
```

## Rulings & laws

- **Teko-only:** `.tks` only; `heap_*` helpers are `.tks`, removed cleanly at COL-F2; no C twin touched.
- **W15 full Javadoc** on every rewritten member; flatten; no inline `//`.
- **Build-first (owner):** BUILD + prove the replacement green BEFORE COL-F2 removes `heap_sift_up`/
  `heap_pop_min`.
- **Conditional gate (remodel §7 "na dúvida, RITUAL"):** `[dry]`/none if no core consumer; `[fixpoint]`/
  fixpoint-rebuild if consumed — prefer `[fixpoint]` on ANY doubt.
- **Teaches nothing:** pure `.tks` over SM-R1.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; if `[fixpoint]`, FIXPOINT
  `gen2==gen3` + sweep `.tkt`/`.tkr`; commit the green step.

## Fixtures

Under the measured `[dry]` default (no core consumer) → FULL fixtures. If escalated to `[fixpoint]`, drop to
fixpoint coverage.

| fixture | asserts | expected |
|---|---|---|
| `pq_heap_fixed` | enqueue beyond a chunk (force chunk links), dequeue ascending (min-order correct) | `0` |

## Gate

`[dry]` (measured default) — compiles + `pq_heap_fixed` green + trivial fixpoint (byte-identical). If a core
consumer IS found at dispatch: escalate to `[fixpoint]` — build gen2, scoped regression green, FIXPOINT
`gen2==gen3`. **Reseed-class: none (default) / fixpoint-rebuild (if core-consumed).**

## Deps

`COL-Q6` (the Heap base), `COL-Q11` (completes the sorted conversion before the last embedded one).

## Done when

`PriorityQueue<T>` is backed by `Heap<T>`, no longer calls `teko::list::push`/`heap_sift_up`/`heap_pop_min`,
`pq_heap_fixed` passes, and the chosen gate (`[dry]` or `[fixpoint]`) is green — the 7 embedded conversions
complete.
