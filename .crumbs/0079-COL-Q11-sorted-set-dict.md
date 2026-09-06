---
seq: 0079
crumb-id: COL-Q11
milestone: M2
gate: "[dry]/[fixpoint]"
reseed-class: "none/fixpoint"
deps: [COL-Q5, COL-Q10]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:658-676"   # Q11 SortedSet/SortedDictionary → Ordered
  - "docs/design/plano-mestre-0.3.1-implementacao.md:239"                # M2 collections row COL-Q11
  - "docs/design/colecoes-remodelagem-backing-fixo-0.3.1.md"             # remodel §7 ("na dúvida, RITUAL")
---

# 0079 · COL-Q11 — convert `SortedSet` / `SortedDictionary` to the Ordered base

> Convert `SortedSet`/`SortedDictionary` → the `Ordered<K>` base (COL-Q5), replacing the O(n) shift-insert
> (`sorted_insert`→`arr_insert_at`) with O(log n) node insert. `[dry]` if not core-consumed, else `[fixpoint]`.

## Goal

Re-back the two sorted collections on the `Ordered<K>` node-linked base (COL-Q5), replacing the O(n) shift-insert
at `src/collections/sorted_set.tks:17` (`sorted_insert(&arr, x)` → `arr_insert_at`) and the parallel
`sorted_dictionary.tks:30-31` with O(log n) node insert/remove — ZERO backing shift. Build-first: the
`sorted_insert` helper (`collections.tks:94`) stays defined (removed at COL-F2); the collections stop calling
it. The gate is CONDITIONAL: `[dry]` (no reseed) if `src/` does not core-consume a sorted collection; else
`[fixpoint]` (byte-identity) if it does. Per the remodel §7 "na dúvida, RITUAL" — prefer `[fixpoint]` on ANY
doubt. Measured default here: the compiler self-build does not appear to consume `SortedSet`/`SortedDictionary`
→ ship `[dry]` with FULL fixtures, and the coordinator escalates to `[fixpoint]` if a consumer is found at
dispatch.

## Where

- `src/collections/sorted_set.tks:17` — replace `sorted_insert(&arr, x)` (`arr_insert_at`) with delegation to an
  `Ordered<T>` node insert.
- `src/collections/sorted_dictionary.tks:30-31` — likewise over `Ordered<K>` + a parallel value store on the
  same node.
- `src/collections/collections.tks:94` (`sorted_insert`) + `:44` (`arr_insert_at`) — still CALLED elsewhere
  until COL-F2; NOT removed here (build-first).

## How

1. **`SortedSet<T>` wraps `Ordered<T>`** (COL-Q5): `add`→`Ordered::add` (O(log n), no-op if present),
   `contains`→`Ordered::contains`, `remove`→`Ordered::remove`, ascending iteration by traversal. Copy the W15
   doc-comments per the Q9 pattern.
2. **`SortedDictionary<K,V>` wraps `Ordered<K>`** plus a parallel value store aligned to the same node (the key
   node carries or indexes its value). `get` walks to the node in O(log n).
3. **No shift:** every ordered insert/remove is a node link/unlink — strictly better than the index-array
   shift (O(n) per ordered insert) the old `sorted_insert` did.
4. **Build-first:** `sorted_insert`/`arr_insert_at` stay defined; these two collections stop calling them.
   COL-F2 removes the now-dead `sorted_insert`.
5. **GATE-1 (class keys):** a class key removed early is region-dropped (conservative default, leak-safe).

```teko
/**
 * add — insert `k` in order in O(log n) via the Ordered<K> node base; a no-op if `k` is already present. No
 * backing array is shifted (the ordered base links a node), so this is strictly better than the retired
 * `sorted_insert`/`arr_insert_at` O(n) shift.
 *
 * @param k  the key to insert
 * @since 0.3.1
 */
pub fn add(k: K)
```

## Rulings & laws

- **Teko-only:** `.tks` only; `sorted_insert` is `.tks`, removed cleanly at COL-F2; no C twin touched.
- **W15 full Javadoc** on every rewritten member; flatten; no inline `//`.
- **Build-first (owner):** BUILD + prove the replacements green BEFORE COL-F2 removes `sorted_insert`.
- **Conditional gate (remodel §7 "na dúvida, RITUAL"):** `[dry]`/none if no core consumer; `[fixpoint]`/
  fixpoint-rebuild if `src/` consumes a sorted collection — prefer `[fixpoint]` on ANY doubt.
- **GATE-1 (class keys):** conservative region-drop-via-escape default; promote-to-wrapped is a later follow-up.
- **Teaches nothing:** pure `.tks` over SM-R1.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; if `[fixpoint]`, FIXPOINT
  `gen2==gen3` + sweep `.tkt`/`.tkr`; commit the green step.

## Fixtures

Under the measured `[dry]` default (no core consumer) → FULL fixtures (the self-build never touches these). If
escalated to `[fixpoint]`, drop these to fixpoint coverage per the no-redundant-fixtures rule.

| fixture | asserts | expected |
|---|---|---|
| `sortedset_shift_order` | insert out of order beyond a node-block, iterate ascending (order correct, no shift) | `0` |
| `sorteddict_shift_pair` | keys + values aligned across ordered inserts; `get` matches | `0` |

## Gate

`[dry]` (measured default: no core consumer) — compiles + the two fixtures green + trivial fixpoint
(byte-identical). If a core consumer IS found at dispatch: `[dry]` escalates to `[fixpoint]` — build gen2,
scoped regression green, FIXPOINT `gen2==gen3`. **Reseed-class: none (default) / fixpoint-rebuild (if
core-consumed).**

## Deps

`COL-Q5` (the Ordered base), `COL-Q10` (Hash conversions landed first).

## Done when

`SortedSet`/`SortedDictionary` are backed by `Ordered<K>` with O(log n) node insert/remove (no shift), no longer
call `sorted_insert`, the fixtures pass, and the chosen gate (`[dry]` or `[fixpoint]`) is green.
