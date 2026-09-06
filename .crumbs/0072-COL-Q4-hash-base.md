---
seq: 0072
crumb-id: COL-Q4
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q1]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:343-397"   # Q4 — the Hash base (full W15 shape)
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:27-30"     # §0 the settled model: three-category memory
---

# 0072 · COL-Q4 — Hash base

> `Hash<K,V>` — the hash-table base: parallel chunk-chains (keys / cached-hashes / values) + probe; the base
> for Map/Dictionary/HashSet/Counter/MultiMap/WeakMap.

## Goal

`Hash<K,V>` is the hash-table base: parallel `ChunkChain`s for keys, cached `u64` hashes, and values, sharing
one `count`, grown in lockstep by linking a chunk to each (never a whole-backing swap). Lookup is by hash then
probe (linear-scan today, matching the embedded impl; open-addressing with a load-factor rehash is the fase1b
follow-up — the bucket backing is a fixed index rehashed by rebuild, never a resize). Non-ordered by contract
(a hash table is a bag/set) so `remove` may swap-remove in O(1); where insertion order is contractual
(`keys()`), `remove` falls back to O(n) shift. Reclamation is three-category per key/value class. This crumb
adds `Hash<K,V>` in a NEW pure-`.tks` file over **Q1** (`ChunkChain`) + FASE 0 `place`. It is **additive /
feature-gated-inert**: the happy path is fixpoint-covered once `Map`/`Dictionary` migrate (Q10), so it changes
no emitted byte today — a `[dry]` leaf, reseed-class `none`, teaching nothing.

Not blocked by any open dependency (its dep COL-Q1 is `0070`, in this wave); this is executable design.

## Where

- NEW `src/collections/hash.tks` — defines `Hash<K,V>`; pure `.tks` over Q1's `ChunkChain` + FASE 0 `place`.
- NO existing collection touched — the migrations of `Map`/`Dictionary`/`HashSet` onto `Hash` are Q10 (`0078`),
  NOT this crumb.
- Reuses Q1's `ChunkChain<K>`/`ChunkChain<u64>`/`ChunkChain<V>` for the three parallel chains.

## How

Create `src/collections/hash.tks` with the shape below, copied VERBATIM from `colecoes-memoria-fila…` Q4:

```teko
/**
 * Hash<K,V> — the hash-table base: parallel ChunkChains for keys, cached u64 hashes, and values, sharing one
 * `count`, grown in lockstep by linking a chunk to each (never a whole-backing swap). Lookup is by hash then
 * probe (linear-scan today, matching the embedded impl; open-addressing with a load-factor rehash is the
 * fase1b follow-up — the bucket backing is a fixed index rehashed by rebuild, never a resize). Non-ordered by
 * contract (a hash table is a bag/set) so `remove` may swap-remove in O(1); where insertion order is
 * contractual (`keys()`), `remove` falls back to O(n) shift. Reclamation is three-category per key/value class.
 *
 * @since 0.3.1
 */
exp type Hash<K, V> = class {
    /** The key chain (pointers for class/wrapped keys, inline for word-sized keys). */
    intern keys: ChunkChain<K>
    /** The cached hash of each key, inline u64. */
    intern hashes: ChunkChain<u64>
    /** The value chain, aligned index-for-index with `keys`. */
    intern vals: ChunkChain<V>
    /** The shared live entry count. */
    intern count: u64

    /** Build an empty hash table. */
    pub static fn make(): Hash<K, V>
    /**
     * Insert or update `k -> v`: probe for `k`; if present, update the value in place (VALUE place + old slot
     * bucket; CLASS swap pointer; WRAPPED retain new / release old — count unchanged); else append to the
     * three chains and bump `count`.
     *
     * @param k  the key
     * @param v  the value
     */
    pub fn insert(k: K, v: V)
    /** Look up `k`; the value copy/reference, or the empty variant if absent. */
    pub fn get(k: K): V | null
    /** Remove `k`; swap-remove (O(1), unordered) or shift (O(n), insertion-order contract). */
    pub fn remove(k: K)
}
```

Implementation notes: `make` builds three empty `ChunkChain`s sharing `count`; `insert` computes the key hash
once, probes the `hashes` chain for a match then confirms the key, updating in place (value category) or
appending to all three chains; `get` probes by hash then key equality; `remove` swap-removes (unordered) or
shifts (ordered contract). The three chains grow in lockstep — a chunk linked to each.

## Rulings & laws

- **Teko-only:** the base lands in NEW `src/collections/hash.tks` (`.tks`); no C twin.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//` — the shape above
  is W15-conformant, copy verbatim.
- **Removals = clean expurgo, NO tombstone:** additive — removes no surface.
- **Three-category memory law (Doc-2, `colecoes-memoria-fila…` §0):** `insert`'s update path is VALUE place +
  old-slot bucket / CLASS pointer swap / WRAPPED retain-new-release-old; reclamation is per key/value class.
- **Never-resize (`colecoes-memoria-fila…` Q4):** growth links a chunk to each chain; a rehash rebuilds a fixed
  index, never resizes in place.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit the green step; **reseed ONLY at a [RITUAL]** — this is a
  `[dry]` leaf, reseed-class `none`, NO reseed; the trivial fixpoint (additive) is the proof; sweep
  `.tkt`/`.tkr` after the new type is added.

## Fixtures

The happy path is fixpoint-covered once `Map`/`Dictionary` migrate (Q10); write only the collision edge the
self-build may not stress (`colecoes-memoria-fila…` 391-393):

| fixture | asserts | expected |
|---|---|---|
| `hash_collision_probe` | forced hash collisions probe correctly — distinct keys with the same hash all insert, get, and remove without aliasing | `0` |

## Gate

`[dry]` — compile + the collision fixture + the trivial fixpoint (additive; the core does not instantiate
`Hash` until Q10, so no emitted-byte change). "Green" = `Hash<K,V>` compiles, the collision probe exits `0`,
and no emitted byte changes. **Reseed-class:** `none` (pure `.tks`, no teaching).

## Deps

`COL-Q1` (`0070` — the `ChunkChain` base the three parallel key/hash/value chains are built on).

## Done when

`Hash<K,V>` compiles as a pure-`.tks` base over three parallel `ChunkChain`s (keys / cached hashes / values)
with hash-then-probe lookup and three-category reclamation, the collision-probe fixture exits `0`, and a `[dry]`
build is byte-identical (the core does not yet instantiate it).
