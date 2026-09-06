---
seq: 0084
crumb-id: COL-Q16
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q10]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:707-708"   # Q16 Counter/MultiSet
  - "docs/design/plano-mestre-0.3.1-implementacao.md:244"                # M2 collections row COL-Q16
---

# 0084 · COL-Q16 — `Counter<T>` / `MultiSet<T>` (wraps Dictionary)

> `Counter<T>`/`MultiSet<T>` — tally collections wrapping `Dictionary<T,u64>` (COL-Q10). Pure `.tks`, teaches
> nothing, `[dry]`.

## Goal

Deliver `Counter<T>` / `MultiSet<T>` — multiplicity tallies that wrap `Dictionary<T, u64>` (the Hash-backed
dictionary from COL-Q10): `add` bumps the count, `count_of` reads it, `most_common` returns the top-n by tally.
Pure `.tks` composition over the SM-R1 surface — teaches nothing, `[dry]`, no reseed. The compiler core does NOT
consume these → FULL fixtures. Byte-preserving: additive leaf.

## Where

- `src/collections/counter.tks` — NEW module: `Counter<T>` / `MultiSet<T>` over `Dictionary<T, u64>` (COL-Q10).
- No EXISTING fn modified; additive leaf.

## How

1. **Create `src/collections/counter.tks`.** Hold `intern counts: Dictionary<T, u64>`. `add(x)` = `insert(x,
   get(x) + 1)`; `remove(x)` decrements (dropping the key at 0); `count_of(x)` = `get(x)` or 0. Copy W15
   doc-comments per the Q9 pattern.
2. **`most_common(n)`** collects `(key, count)` pairs, sorts by count descending, returns the top `n` (a fresh
   snapshot, never a view).
3. **`MultiSet<T>`** is `Counter<T>` framed as a set-with-multiplicity: `contains`, `total()` (sum of counts).

```teko
/**
 * Counter<T> — a multiplicity tally over a Dictionary<T, u64>: `add` increments the count for a key, `remove`
 * decrements (dropping the key at zero), `count_of` reads it. `most_common` returns the top-n keys by tally as
 * a fresh snapshot. A pure composition over the Hash-backed Dictionary (COL-Q10).
 *
 * @since 0.3.1
 */
exp type Counter<T> = class {
    /** The key -> count tally. */
    intern counts: Dictionary<T, u64>

    /** Build an empty counter. */
    pub static fn make(): Counter<T>
    /** Increment the count for `x`. */
    pub fn add(x: T)
    /** The current count for `x`, or 0 if absent. */
    pub fn count_of(x: T): u64
    /** The `n` keys with the highest counts, descending, as a fresh snapshot. */
    pub fn most_common(n: u64): []T
}
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; no checker/codegen edit.
- **W15 full Javadoc** on both types + every member; no inline `//`.
- **Teach-once (owner 2026-08-19):** no new surface — stands on Dictionary (SM-R1); `[dry]`, zero reseed.
- **Snapshot law:** `most_common` returns a fresh `[]T`, never a view over the dictionary.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build in a subshell with `ulimit -v 6815744`; commit the
  green step.

## Fixtures

The compiler does NOT consume Counter/MultiSet → FULL fixtures.

| fixture | asserts | expected |
|---|---|---|
| `counter_add` | repeated `add`; `count_of` matches the expected tally | `0` |
| `counter_most_common` | `most_common(k)` returns the top-k in descending order | `0` |

## Gate

`[dry]` — compiles + the two fixtures green + trivial fixpoint (byte-identical). "Green" = tallies + top-n order
hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q10` (the Hash-backed `Dictionary`).

## Done when

`src/collections/counter.tks` compiles, `counter_add` + `counter_most_common` pass, and a `[dry]` build is
byte-identical.
