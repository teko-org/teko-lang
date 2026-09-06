---
seq: 0071
crumb-id: COL-Q3
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:292-341"   # Q3 — the Ring base (full W15 shape)
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:27-30"     # §0 the settled model: three-category memory
---

# 0071 · COL-Q3 — Ring base

> `Ring<T>` — a fixed array with head/tail wrap, O(1) at BOTH ends, zero shift; the base for Queue/Deque and
> the bounded RingBuffer/BlockingCollection.

## Goal

`Ring<T>` is a bounded double-ended queue over ONE fixed backing with `head`/`tail`/`count`: push/pop at either
end is O(1) with wrap-around modulo `slots.len` — ZERO shift. When unbounded and full it grows by allocating a
bigger fixed ring and re-linearizing from `head` (elements copied once into the new fixed backing, the old
ring's region dropped — F1: never an in-place resize). A bounded ring is fixed-size and never grows. Mid-ring
removal is O(n) (for mid churn use LinkedList, Q15). This crumb adds `Ring<T>` in a NEW pure-`.tks` file over
FASE 0's `of_len`. It is **additive / feature-gated-inert**: the compiler does not use `Ring` (Queue/Deque wrap
it at Q14, RingBuffer at Q19), so it changes no emitted byte today — a `[dry]` leaf, reseed-class `none`,
teaching nothing.

Not blocked by any open dependency (its dep COL-F0a is in this wave, `0021`); this is executable design.

## Where

- NEW `src/collections/ring.tks` — defines `Ring<T>`; pure `.tks` over FASE 0 `of_len`.
- NO existing collection touched — Queue/Deque (Q14, `0082`) and RingBuffer/BlockingCollection (Q19, `0087`)
  wrap `Ring`, NOT this crumb.
- Reuses COL-F0a's `of_len`/`place`/`read`/`write`/`bucket` for the fixed backing.

## How

Create `src/collections/ring.tks` with the shape below, copied VERBATIM from `colecoes-memoria-fila…` Q3:

```teko
/**
 * Ring<T> — a bounded double-ended queue over ONE fixed backing with `head`, `tail`, `count`. push/pop at
 * either end is O(1) with wrap-around modulo `slots.len` — ZERO shift. When unbounded and full it grows by
 * allocating a bigger fixed ring and re-linearizing from `head` (elements copied once into the new fixed
 * backing, the old ring's region dropped — F1: never a resize). A bounded ring is fixed-size and never grows.
 * Mid-ring removal is O(n); for mid churn use LinkedList (Q15).
 *
 * @since 0.3.1
 */
exp type Ring<T> = class {
    /** The fixed backing; `slots.len` is the capacity. */
    intern slots: []T
    /** The read cursor (front). */
    intern head: u64
    /** The write cursor (back). */
    intern tail: u64
    /** The live element count (`count <= slots.len`). */
    intern count: u64

    /** Build a ring with a fixed capacity; a bounded ring never grows past `cap`. */
    pub static fn make(cap: u64): Ring<T>
    /** Append at the back in O(1) (wrap); grows if unbounded and full. */
    pub fn push_back(x: T)
    /** Prepend at the front in O(1) (wrap); grows if unbounded and full. */
    pub fn push_front(x: T)
    /** Remove and return the front in O(1); VALUE by DPS + bucket, CLASS/WRAPPED release. */
    pub fn pop_front(): T
    /** Remove and return the back in O(1). */
    pub fn pop_back(): T
}
```

Implementation notes: `make` allocates `of_len(cap)`; `push_back`/`push_front` advance `tail`/`head` modulo
`slots.len` and bump `count`; `pop_front`/`pop_back` retreat the cursor, return by DPS (value) or release
(class/wrapped), and bucket the slot; the unbounded-full grow allocates `of_len(2*cap)`, copies from `head` in
order, and drops the old ring's region.

## Rulings & laws

- **Teko-only:** the base lands in NEW `src/collections/ring.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//` — the shape above
  is W15-conformant, copy verbatim.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface.
- **Three-category memory law (Doc-2, `colecoes-memoria-fila…` §0):** `pop_front`/`pop_back` return VALUE by
  DPS + slot bucket, CLASS/WRAPPED by release; the grow copies values once and drops the old region (F1).
- **F1 never-resize (`colecoes-memoria-fila…` Q3):** the ring never resizes in place — it re-linearizes into a
  fresh fixed backing and drops the old region.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit the green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (additive) is the proof; sweep
  `.tkt`/`.tkr` after the new type is added.

## Fixtures

The compiler does not use `Ring`, so full fixtures apply (concurrency is covered later at Q19)
(`colecoes-memoria-fila…` 334-337):

| fixture | asserts | expected |
|---|---|---|
| `ring_both_ends` | interleaved front/back push+pop, order correct | `0` |
| `ring_wrap` | fill/drain/refill across the wrap boundary, order intact | `0` |
| `ring_bounded_full` | a bounded ring at its bound rejects/blocks per policy, no grow | `0` |
| `ring_grow_relinearize` | unbounded grow re-linearizes from head, old region dropped, order preserved | `0` |

## Gate

`[dry]` — compile + the four fixtures + the trivial fixpoint (additive; the core does not use `Ring`, so no
emitted-byte change). "Green" = `Ring<T>` compiles, both-ends O(1) + wrap + bounded + grow-relinearize behave,
and no emitted byte changes. **Reseed-class:** `none` (pure `.tks`, no teaching).

## Deps

`COL-F0a` (`0021` — the `of_len` fixed-backing intrinsic the ring is built over).

## Done when

`Ring<T>` compiles as a pure-`.tks` base with O(1) push/pop at both ends via wrap, zero shift, and an
unbounded grow that re-linearizes into a fresh fixed backing (old region dropped), the four fixtures exit `0`,
and a `[dry]` build is byte-identical.
