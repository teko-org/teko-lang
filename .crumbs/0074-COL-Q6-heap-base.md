---
seq: 0074
crumb-id: COL-Q6
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q1]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:446-485"   # Q6 — the Heap base (full W15 shape)
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:27-30"     # §0 the settled model: three-category memory
---

# 0074 · COL-Q6 — Heap base

> `Heap<T>` — a binary min-heap over a chunk-chain, O(log n) push/pop-min; the base for PriorityQueue.

## Goal

`Heap<T>` is a binary min-heap over a `ChunkChain` indexed as an implicit tree: `push` appends then sifts up by
swapping POINTERS (no value move); `pop_min` returns the root (VALUE by DPS + bucket, CLASS/WRAPPED release),
moves the last element to the root, and sifts down. Growth is the chunk-chain's (link a chunk, never a swap).
For merge/insert-heavy loads a pairing heap over nodes (remodel §9.2) is the alternative. This crumb adds
`Heap<T>` in a NEW pure-`.tks` file over **Q1** (`ChunkChain`). It is **additive / feature-gated-inert**: the
compiler likely does not use `Heap` (PriorityQueue wraps it at Q12), so it changes no emitted byte today — a
`[dry]` leaf, reseed-class `none`, teaching nothing.

Not blocked by any open dependency (its dep COL-Q1 is `0070`, in this wave); this is executable design.

## Where

- NEW `src/collections/heap.tks` — defines `Heap<T>`; pure `.tks` over Q1's `ChunkChain`.
- NO existing collection touched — the migration of `PriorityQueue` onto `Heap` is Q12 (`0080`), NOT this crumb.
- Reuses Q1's `ChunkChain<T>` for the implicit-tree level-order storage.

## How

Create `src/collections/heap.tks` with the shape below, copied VERBATIM from `colecoes-memoria-fila…` Q6:

```teko
/**
 * Heap<T> — a binary min-heap over a ChunkChain indexed as an implicit tree. push appends then sifts up by
 * swapping POINTERS (no value move); pop-min returns the root (VALUE by DPS + bucket, CLASS/WRAPPED release),
 * moves the last to root, sifts down. Growth is the chunk-chain's (link a chunk, never a swap). For
 * merge/insert-heavy loads a pairing heap over nodes (remodel §9.2) is the alternative.
 *
 * @since 0.3.1
 */
exp type Heap<T> = class {
    /** The chunk-chain holding the implicit tree in level order. */
    intern nodes: ChunkChain<T>
    /** The comparison delegate deciding min. */
    intern less: func<T, T, bool>

    /** Build an empty heap with a comparison. */
    pub static fn make(less: func<T, T, bool>): Heap<T>
    /** Insert `x` and sift up in O(log n). */
    pub fn push(x: T)
    /** Remove and return the minimum in O(log n); the empty variant when empty. */
    pub fn pop_min(): T | null
    /** Peek the minimum without removing (O(1)). */
    pub fn peek(): T | null
}
```

Implementation notes: `make` builds an empty `ChunkChain` + captures the `less` comparison; `push` appends to
the chain then sifts up swapping pointers (parent `i` = `(child-1)/2`); `pop_min` returns the root by DPS
(value) or release (class/wrapped), moves the last live element to the root, and sifts down choosing the
smaller child by `less`; `peek` reads index 0. Growth is the chain's chunk-link, never a swap.

## Rulings & laws

- **Teko-only:** the base lands in NEW `src/collections/heap.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//` — the shape above
  is W15-conformant, copy verbatim.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface.
- **Three-category memory law (Doc-2, `colecoes-memoria-fila…` §0):** `pop_min` returns VALUE by DPS + slot
  bucket, CLASS/WRAPPED by release; sift-up/down swaps POINTERS, never moves a value.
- **Never-swap growth (`colecoes-memoria-fila…` Q6):** growth is the chunk-chain's chunk-link (Q1), never a
  whole-backing swap.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit the green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (additive) is the proof; sweep
  `.tkt`/`.tkr` after the new type is added.

## Fixtures

The compiler likely does not use `Heap`, so full fixtures apply (`colecoes-memoria-fila…` 480-481):

| fixture | asserts | expected |
|---|---|---|
| `heap_min_order` | push out of order beyond a single chunk, pop_min yields ascending order | `0` |
| `heap_empty_pop` | `pop_min` on an empty heap returns the empty variant (null) | `0` |

## Gate

`[dry]` — compile + the two fixtures + the trivial fixpoint (additive; the core does not instantiate `Heap`
until Q12, so no emitted-byte change). "Green" = `Heap<T>` compiles, `pop_min` yields ascending order across a
chunk boundary, empty-pop returns null, and no emitted byte changes. **Reseed-class:** `none` (pure `.tks`, no
teaching).

## Deps

`COL-Q1` (`0070` — the `ChunkChain` base holding the implicit tree in level order).

## Done when

`Heap<T>` compiles as a pure-`.tks` binary min-heap over a `ChunkChain` with O(log n) push/pop-min (pointer
sift, value by DPS), the two fixtures exit `0`, and a `[dry]` build is byte-identical (the core does not yet
instantiate it).
