---
seq: 0086
crumb-id: COL-Q18
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q4, COL-Q8]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:711-713"   # Q18 WeakMap/WeakSet
  - "docs/design/plano-mestre-0.3.1-implementacao.md:246"                # M2 collections row COL-Q18
---

# 0086 · COL-Q18 — `WeakMap<K,V>` / `WeakSet<T>` (Hash<Weak,·> + get-time prune)

> `WeakMap`/`WeakSet` — a `Hash<Weak<K>, V>` (COL-Q4 + COL-Q8) with get-time liveness prune. Pure `.tks`,
> teaches nothing, `[dry]`.

## Goal

Deliver `WeakMap<K,V>` / `WeakSet<T>` — hash collections keyed by a `Weak<K>` (COL-Q8) over the `Hash<K,V>` base
(COL-Q4), with a get-time liveness prune: a lookup whose weak key's target has been freed is dropped (no UAF).
The keys do not keep their targets alive; entries for collected keys are removed lazily on access. Pure `.tks`
composition over the SM-R1 surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT consume
these → FULL fixtures. Byte-preserving: additive leaf.

## Where

- `src/collections/weakmap.tks` — NEW module: `WeakMap<K,V>` = `Hash<Weak<K>, V>` + prune; `WeakSet<T>` =
  `Hash<Weak<T>, unit>` + prune. Over COL-Q4 (Hash) + COL-Q8 (Weak).
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/weakmap.tks`.** `WeakMap<K,V>` holds `intern entries: Hash<Weak<K>, V>`. `set(k, v)`
   stores `Weak::of(k) -> v` (the key does NOT retain `k`). Copy W15 doc-comments per the Q9 pattern.
2. **Get-time prune:** `get(k)` probes; for each visited entry whose `Weak` key `.get()` returns null (target
   freed), REMOVE the entry (lazy prune) and skip it; return the value only for a live matching key.
3. **`WeakSet<T>`** is the `Hash<Weak<T>, unit>` framing: `add`/`contains` with the same get-time prune.
4. **No UAF:** a dead weak key never dereferences its target; the entry is pruned, not read.

```teko
/**
 * WeakMap<K,V> — a hash map keyed by a non-retaining Weak<K> (COL-Q8) over the Hash base (COL-Q4). A key does
 * NOT keep its target alive; on lookup, entries whose weak key's target has been freed are pruned lazily
 * (get-time prune) — no UAF, no unbounded growth from dead keys. A pure composition; meaningful for `wrapped`
 * keys (the refcount kind Weak tracks).
 *
 * @since 0.3.1
 */
exp type WeakMap<K, V> = class {
    /** The Weak<K> -> V entries; dead keys pruned on access. */
    intern entries: Hash<Weak<K>, V>

    /** Build an empty weak map. */
    pub static fn make(): WeakMap<K, V>
    /** Associate `v` with a weak reference to `k` (does NOT retain `k`). */
    pub fn set(k: K, v: V)
    /** Look up `k`; pruning any dead-key entries seen; the value or null. */
    pub fn get(k: K): V | null
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on both types + every member; no inline `//`.
- **Weak semantics (COL-Q8):** the key does NOT retain; a dead weak key is pruned, never dereferenced (no UAF).
- **Teach-once (owner 2026-08-19):** no new surface — stands on Hash + Weak (SM-R1); `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume WeakMap/WeakSet → FULL fixtures.

| fixture | asserts | expected |
|---|---|---|
| `weakmap_live_key` | a strongly-held key: `get` returns its value (weak key still alive) | `0` |
| `weakmap_dead_key` | a collected key: the entry is pruned on access, no UAF, `get` returns null | `0` |

## Gate

`[dry]` — compiles + the two fixtures green + trivial fixpoint (byte-identical). "Green" = live-key lookup +
dead-key prune (no UAF) hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q4` (the Hash base), `COL-Q8` (the Weak wrappers).

## Done when

`src/collections/weakmap.tks` compiles, `weakmap_live_key` + `weakmap_dead_key` pass, and a `[dry]` build is
byte-identical.
