---
seq: 0073
crumb-id: COL-Q5
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:399-444"   # Q5 — the Ordered base (+Node) (full W15 shape)
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:27-30"     # §0 the settled model: three-category memory
---

# 0073 · COL-Q5 — Ordered base (+Node)

> `Ordered<K>` — a node-linked skip-list / balanced BST: sorted iterate + range, O(log n), ZERO shift; the base
> for SortedSet/SortedDictionary (and Table's range index), defining the reusable `Node`/`link_after`.

## Goal

`Ordered<K>` is the ordered base: a node-linked skip-list (or balanced BST) keyed by `K`'s comparison.
Insert/remove/lookup are O(log n) with NO backing array to shift — strictly better than an index-array sorted
set (which shifts O(n) per ordered insert). Iteration is ascending by traversal. Nodes are individually
arena-allocated; reclamation is three-category (value node bucket / class node region-drop / wrapped node
release). This crumb adds `Ordered<K>` + `OrderedNode<K>` in a NEW pure-`.tks` file over FASE 0's `class`
nodes, defining the reusable `Node<T>`/`link_after` shared with LinkedList (Q15) and the arena chunk-list
(record §4) if not already factored elsewhere. It is **additive / feature-gated-inert**: the compiler may not
use `Ordered` (SortedSet/SortedDictionary migrate at Q11), so it changes no emitted byte today — a `[dry]`
leaf, reseed-class `none`, teaching nothing.

Not blocked by any open dependency (its dep COL-F0a is `0021`, in this wave); this is executable design.

## Where

- NEW `src/collections/ordered.tks` — defines `Ordered<K>`, `OrderedNode<K>`, and the reusable `Node<T>`/
  `link_after` (if not factored elsewhere — shared with LinkedList Q15 and the arena chunk-list, record §4);
  pure `.tks` over FASE 0 `class` nodes.
- NO existing collection touched — the migrations of `SortedSet`/`SortedDictionary` onto `Ordered` are Q11
  (`0079`), NOT this crumb; LinkedList (Q15, `0083`) shares the `Node`.
- Reuses COL-F0a's `class`-node capability + region-drop.

## How

Create `src/collections/ordered.tks` with the shape below, copied VERBATIM from `colecoes-memoria-fila…` Q5:

```teko
/**
 * Ordered<K> — the ordered base: a node-linked skip-list (or balanced BST) keyed by `K`'s comparison.
 * Insert/remove/lookup are O(log n) with NO backing array to shift — strictly better than an index-array
 * sorted set (which shifts O(n) per ordered insert). Iteration is ascending by traversal. Nodes are
 * individually arena-allocated; reclamation is three-category (value node bucket / class node region-drop /
 * wrapped node release).
 *
 * @since 0.3.1
 */
exp type Ordered<K> = class {
    /** The head tower of the skip-list (or the BST root). */
    intern head: *OrderedNode<K>
    /** The live key count. */
    intern count: u64

    /** Build an empty ordered set. */
    pub static fn make(): Ordered<K>
    /** Insert `k` in order in O(log n); a no-op if already present. */
    pub fn add(k: K)
    /** True iff `k` is present (O(log n)). */
    pub fn contains(k: K): bool
    /** Remove `k` in O(log n); reclamation by class. */
    pub fn remove(k: K)
    /** The inclusive range `[lo, hi]` ascending, as a fresh snapshot. */
    pub fn range(lo: K, hi: K): []K
}
```

Implementation notes: define `OrderedNode<K>` (the key + forward links / tower); `make` builds an empty head;
`add` finds the ordered position in O(log n) and links a fresh arena-allocated node (no shift), a no-op if
present; `contains` walks the tower; `remove` unlinks in O(log n) and reclaims the node by class; `range`
snapshots the inclusive window ascending into a fresh `[]K`. Define the reusable `Node<T>`/`link_after` here so
LinkedList (Q15) shares it.

## Rulings & laws

- **Teko-only:** the base lands in NEW `src/collections/ordered.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on every declaration (pub + private), including `OrderedNode<K>`/`Node<T>`/`link_after`;
  flatten/extract; no inline `//` — the shape above is W15-conformant, copy verbatim.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface.
- **Three-category memory law (Doc-2, `colecoes-memoria-fila…` §0):** nodes reclaim by class (value node
  bucket / class node region-drop / wrapped node release); `range` returns a fresh snapshot, never aliases node
  storage.
- **Zero-shift ordering (`colecoes-memoria-fila…` Q5):** ordered insert/remove is node link/unlink, never an
  array shift — the base is strictly better than an index-array sorted set.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit the green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (additive) is the proof; sweep
  `.tkt`/`.tkr` after the new types are added.

## Fixtures

The compiler may not use `Ordered`, so full fixtures apply (`colecoes-memoria-fila…` 438-440):

| fixture | asserts | expected |
|---|---|---|
| `ordered_sorted_iter` | insert out of order, iterate ascending, order correct | `0` |
| `ordered_range` | the inclusive range window is correct and ascending | `0` |
| `ordered_remove_no_shift` | O(log n) unlink leaves the order intact, no array shift | `0` |

## Gate

`[dry]` — compile + the three fixtures + the trivial fixpoint (additive; the core does not instantiate
`Ordered` until Q11, so no emitted-byte change). "Green" = `Ordered<K>` (+`OrderedNode`/`Node`) compiles,
sorted-iterate/range/remove behave with zero shift, and no emitted byte changes. **Reseed-class:** `none` (pure
`.tks`, no teaching).

## Deps

`COL-F0a` (`0021` — the `class`-node capability + region-drop the ordered nodes are built on).

## Done when

`Ordered<K>` (with `OrderedNode<K>` and the reusable `Node`/`link_after`) compiles as a pure-`.tks` node-linked
base with O(log n) add/contains/remove/range and zero array shift, the three fixtures exit `0`, and a `[dry]`
build is byte-identical (the core does not yet instantiate it).
