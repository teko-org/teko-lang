---
seq: 0075
crumb-id: COL-Q7
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-F0a]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:487-534"   # Q7 BitSet base
  - "docs/design/plano-mestre-0.3.1-implementacao.md:235"                # M2 collections row COL-Q7
  - "docs/design/colecoes-e-memoria-modelo-unificado-0.3.1.md"           # record §5 (BitSet is compiler-critical)
---

# 0075 · COL-Q7 — BitSet base (packed `[]u64` dense small-int set)

> BitSet base: a dense set of small non-negative integers packed into a fixed `[]u64`; set/clear/test O(1),
> union/intersect/difference word-parallel; pure `.tks` over `of_len` — teaches the compiler NOTHING new.

## Goal

Deliver `BitSet` — the most compact collection base and the one the compiler itself will eventually want for
liveness/reachability/visited/escape sets (record §5). It is a packed `[]u64` (bit `i` in word `i/64`, bit
`i%64`), fixed-backing: growing the universe allocates a NEW fixed word array and copies the words once (the F1
"never a resize" law). No per-element allocation, no pointers. It is **pure `.tks` over the FASE-0 `of_len`
surface already seeded at SM-R1**, so it teaches nothing and rides a `[dry]` gate; the compiler core does not
yet instantiate it (adopting BitSet to back the escape-check is a SEPARATE future core-consumes swap — out of
scope, reported). Byte-preserving: a new leaf module adds no consumer, so no emitted compiler byte changes.

## Where

- `src/collections/bitset.tks` — NEW module. Pure `.tks` over `of_len<T>(n): []T` (FASE-0, seeded at SM-R1) and
  `[]u64` word operations. No compiler-surface edit — no checker/lower/codegen touch.
- No EXISTING fn is modified; this is an additive leaf.

## How

1. **Create `src/collections/bitset.tks`.** Copy the W15 API shape verbatim from the source doc
   (`colecoes-memoria-fila-implementacao-0.3.1.md:496-525`). The backing `words: []u64` is built by
   `of_len<u64>(word_count)`; `word_count = (universe + 63) / 64`.
2. **`set`/`clear`/`test`** index by `i / 64` (word) and `i % 64` (bit): `words[i/64] |= (1 << (i%64))` etc. O(1).
3. **`union_with`/`intersect_with`** iterate word-for-word (`words.len`) OR/AND-ing in place — word-parallel
   O(words). Difference is `a &= ~b` per word. Both bitsets must share `universe`; a mismatched universe grows
   the smaller to the larger's word count by allocating a NEW fixed `[]u64` and copying (never resize).
4. **`count`** (population count) folds a per-word popcount over `words`.
5. **Growth** (if a `set(i)` with `i >= universe` is admitted) allocates `of_len<u64>(new_word_count)`, copies
   the old words once, drops the old region — the F1 fixed-backing discipline, NOT an in-place grow.

```teko
/**
 * BitSet — a dense set of small non-negative integers packed into a fixed `[]u64` (bit `i` in word `i/64`,
 * bit `i%64`). set/clear/test are O(1); union/intersect/difference are word-parallel O(words). Growing the
 * universe allocates a NEW fixed word array and copies the words once (F1: never a resize). No per-element
 * allocation, no pointers — the most compact base; serves the compiler's liveness/reachability/visited sets.
 *
 * @since 0.3.1
 */
exp type BitSet = class {
    /** The fixed packed word array; `words.len * 64` is the universe capacity. */
    intern words: []u64
    /** The highest admissible bit index + 1. */
    intern universe: u64

    /** Build a bitset sized for `[0, universe)`. */
    pub static fn make(universe: u64): BitSet
    /** Set bit `i`. */
    pub fn set(i: u64)
    /** Clear bit `i`. */
    pub fn clear(i: u64)
    /** Test bit `i`. */
    pub fn test(i: u64): bool
    /** In-place union with `other`. */
    pub fn union_with(other: BitSet)
    /** In-place intersection with `other`. */
    pub fn intersect_with(other: BitSet)
    /** The population count. */
    pub fn count(): u64
}
```

## Rulings & laws

- **Teko-only:** new leaf is `.tks` only; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on the type + every member (pub + private); flatten word-index math into small helpers;
  no inline `//`.
- **Fixed-backing (F1) law** (`colecoes-remodelagem-backing-fixo-0.3.1.md` / owner three-category model): every
  array is fixed; growth allocates a NEW fixed array and copies once; NEVER an in-place resize / growable-swap.
- **Teach-once (owner 2026-08-19):** BitSet adds NO new surface — it stands on the `of_len` taught once at
  SM-R1; a `[dry]` leaf, zero reseed.
- **Byte-preserving:** a new module with no core consumer changes no emitted compiler byte.
- **Safety:** NEVER `teko test .` (run fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit
  the green step.
- **Adjacent (reported, not opened):** adopting BitSet to back the escape-check is a future core-consumes
  `[fixpoint]` swap — out of scope here.

## Fixtures

New module, NOT self-build-exercised (the compiler does not yet instantiate BitSet) → FULL fixtures. ISOLATED
`.tkr` projects under `examples/regressions/<name>/` (native exit code = the asserted value).

| fixture | asserts | expected |
|---|---|---|
| `bitset_set_test` | set/clear/test across word boundaries (bits at 0, 63, 64, 127) round-trip | `0` |
| `bitset_ops` | union/intersect/difference vs a reference model agree | `0` |
| `bitset_popcount` | `count()` matches a hand tally after mixed set/clear | `0` |

## Gate

`[dry]` — compiles (`--no-verify --release`) + the three `.tkr` fixtures green + a trivial fixpoint (the
additive module changes no emitted compiler byte). "Green" = set/clear/test/union/intersect/count behave per the
reference model and the build is byte-identical. **Reseed-class: none.**

## Deps

`COL-F0a` (the `of_len` + fixed-backing intrinsics, seeded at SM-R1).

## Done when

`src/collections/bitset.tks` compiles, the three fixtures pass, and a `[dry]` build is byte-identical — a packed
`[]u64` BitSet with O(1) bit ops and word-parallel set algebra, teaching the compiler nothing new.
