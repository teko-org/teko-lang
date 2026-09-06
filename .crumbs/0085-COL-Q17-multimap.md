---
seq: 0085
crumb-id: COL-Q17
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q10, COL-Q9]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:709-710"   # Q17 MultiMap
  - "docs/design/plano-mestre-0.3.1-implementacao.md:245"                # M2 collections row COL-Q17
---

# 0085 · COL-Q17 — `MultiMap<K,V>` (wraps Dictionary<K, List<V>>)

> `MultiMap<K,V>` — a key-to-many map wrapping `Dictionary<K, List<V>>` (COL-Q10 + COL-Q9). Pure `.tks`, teaches
> nothing, `[dry]`.

## Goal

Deliver `MultiMap<K,V>` — a map from a key to MANY values, wrapping `Dictionary<K, List<V>>` (the Hash-backed
dictionary from COL-Q10 whose value is the chunk-chain `List<V>` from COL-Q9). `put(k, v)` appends to the key's
list; `get(k)` returns all values; `remove_one(k, v)` removes a single value leaving the rest. Pure `.tks`
composition over the SM-R1 surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT consume it
→ FULL fixtures. Byte-preserving: additive leaf.

## Where

- `src/collections/multimap.tks` — NEW module wrapping `Dictionary<K, List<V>>` (COL-Q10 + COL-Q9).
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/multimap.tks`.** Hold `intern buckets: Dictionary<K, List<V>>`. `put(k, v)`: get or
   create the key's `List<V>`, `push(v)`. `get(k)`: return the key's list (or an empty snapshot). Copy W15
   doc-comments per the Q9 pattern.
2. **`remove_one(k, v)`** removes ONE matching value from the key's list (leaving other values for that key);
   drops the key entirely when its list empties.
3. **`keys()`/`len()`** delegate to the dictionary; value snapshots are fresh, never views over a bucket list.

```teko
/**
 * MultiMap<K,V> — a key-to-many map over a Dictionary<K, List<V>>: `put` appends a value to the key's list,
 * `get` returns all values for a key, `remove_one` removes a single value leaving the rest (dropping the key
 * when its list empties). A pure composition over the Hash-backed Dictionary (COL-Q10) + chunk-chain List
 * (COL-Q9).
 *
 * @since 0.3.1
 */
exp type MultiMap<K, V> = class {
    /** The key -> list-of-values buckets. */
    intern buckets: Dictionary<K, List<V>>

    /** Build an empty multimap. */
    pub static fn make(): MultiMap<K, V>
    /** Append `v` under `k` (creating the key's list on first use). */
    pub fn put(k: K, v: V)
    /** All values under `k` as a fresh snapshot (empty if absent). */
    pub fn get(k: K): []V
    /** Remove a single value `v` under `k`, leaving the rest; drops `k` when its list empties. */
    pub fn remove_one(k: K, v: V)
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on the type + every member; no inline `//`.
- **Teach-once (owner 2026-08-19):** no new surface — stands on Dictionary + List (SM-R1); `[dry]`, zero reseed.
- **Snapshot law:** `get` returns a fresh `[]V`, never a view over a bucket list.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume MultiMap → FULL fixtures.

| fixture | asserts | expected |
|---|---|---|
| `multimap_multi` | multiple `put`s under one key; `get` returns ALL values | `0` |
| `multimap_remove_one` | `remove_one` drops exactly one value, leaving the rest under the key | `0` |

## Gate

`[dry]` — compiles + the two fixtures green + trivial fixpoint (byte-identical). "Green" = multi-value get +
single-value remove hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q10` (the Hash-backed `Dictionary`), `COL-Q9` (the chunk-chain `List`).

## Done when

`src/collections/multimap.tks` compiles, `multimap_multi` + `multimap_remove_one` pass, and a `[dry]` build is
byte-identical.
