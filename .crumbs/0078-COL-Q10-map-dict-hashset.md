---
seq: 0078
crumb-id: COL-Q10
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [COL-Q4, COL-Q9]
sources:
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:636-656"   # Q10 Map/Dictionary/HashSet → Hash
  - "docs/design/plano-mestre-0.3.1-implementacao.md:238"                # M2 collections row COL-Q10
  - "docs/design/plano-collections-genericas-e-concorrentes-0.3.1.md:762"# Map is core-consumed by teko::env
---

# 0078 · COL-Q10 — convert `Map` / `Dictionary` / `HashSet` to the Hash base

> Convert `Map`/`Dictionary`/`HashSet` → the `Hash<K,V>` base (COL-Q4), replacing the parallel-array
> `teko::list::push`/`arr_drop_at`. `Map` is core-consumed by `teko::env` → the swap rides a `[fixpoint]`.

## Goal

Re-back the three hash collections on the `Hash<K,V>` base (COL-Q4), replacing their parallel-array
`teko::list::push` + `arr_drop_at` bodies. `Map` is core-consumed by `teko::env`
(`plano-collections-…:762`), so this swap rides a `[fixpoint]` rebuild — the compiler is rebuilt on the new
`Map`/`Dictionary`/`HashSet` bodies but the language is UNCHANGED (pure `.tks` over the SM-R1 surface): teaches
nothing, `gen2==gen3` byte-identity is the gate. Build-first: the old `arr_*`/`push` helpers stay defined (their
removal is COL-F2); the collections just stop calling them. `Dictionary.keys()` keeps its insertion-order
contract, so its `remove` uses the O(n) shift path, NOT swap-remove.

## Where

- `src/collections/map.tks:31-33,47-49` — replace the `teko::list::push`(keys/hashes/vals) + `arr_drop_at`
  triplet with delegation to a `Hash<str, V>`.
- `src/collections/dictionary.tks:31-33,47-49` — likewise over `Hash<K, V>`; `keys()` (`dictionary.tks:54`)
  keeps insertion order → `remove` = O(n) shift (remodel T-5), not swap-remove.
- `src/collections/hashset.tks:21-22,35-36` — over `Hash<T, unit>` (the value is `unit`).
- `src/collections/collections.tks` `arr_drop_at`/`arr_drop_u64_at` (`:14,:26`) — still CALLED until COL-F2;
  NOT removed here (build-first).

## How

1. **Each collection wraps a `Hash<K,V>`** (COL-Q4) and delegates `insert`/`get`/`remove`/`len`. HashSet uses
   `Hash<T, unit>`. Copy the W15 doc-comments per the Q9 pattern (`colecoes-memoria-fila-implementacao-…:644`).
2. **`Map`** (string-keyed) → `Hash<str, V>`; `insert`/`get`/`remove` delegate. The compiler's `teko::env` uses
   `Map`, so the self-build exercises this happy path once rebuilt.
3. **`Dictionary`** → `Hash<K, V>`, but `keys()` must remain insertion-ordered (`dictionary.tks:54` contract),
   so `remove` selects the O(n) shift path in the Hash base (never the unordered swap-remove) — the remodel T-5
   distinction.
4. **`HashSet`** → `Hash<T, unit>`; `add` = `insert(x, unit)`, `contains` = `get(x) != null`, duplicate add is a
   no-op (probe finds the key, updates nothing).
5. **Build-first:** the `arr_drop_at`/`push` roots stay defined; these three collections stop calling them.
   COL-F2 removes the now-dead helpers once EVERY caller is migrated.

```teko
/**
 * insert — probe for `k`; if present, update the value in place (VALUE place + old slot bucket; CLASS swap
 * pointer; WRAPPED retain new / release old — count unchanged); else append to the three parallel chains and
 * bump `count`. Delegated to the shared `Hash<K,V>` base (COL-Q4).
 *
 * @param k  the key
 * @param v  the value
 * @since 0.3.1
 */
pub fn insert(k: K, v: V)
```

## Rulings & laws

- **Teko-only:** `.tks` only; the old `arr_*`/`teko::list::push` roots are `.tks`, removed cleanly at COL-F2; no
  C twin touched.
- **W15 full Javadoc** on every rewritten member; flatten; no inline `//`.
- **Build-first (owner):** BUILD + prove the three replacements green BEFORE COL-F2 removes the old helpers; the
  roots stay callable meanwhile.
- **Insertion-order contract (remodel T-5):** `Dictionary.keys()` is ordered → `remove` = O(n) shift, never
  swap-remove; `Map`/`HashSet` (unordered) may swap-remove in the Hash base.
- **GATE-1 (class values):** class values held by the map are region-dropped on removal (conservative default,
  leak-safe); promote-to-wrapped is an additive follow-up when GATE-1 closes.
- **Teaches nothing:** pure `.tks` over SM-R1; the `[fixpoint]` advances the blessed binary only to carry the
  new bodies.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744` (a Hash monomorph inflating
  the build is a root-cause fix, never a raised ceiling); FIXPOINT `gen2==gen3`; sweep `.tkt`/`.tkr`; commit the
  green step.

## Fixtures

`Map` is core-consumed → the self-build covers its happy path. Write ONLY the non-self-build paths:

| fixture | asserts | expected |
|---|---|---|
| `dict_class_value_ref` | a `class` value: `get` returns the reference (mutation reflects); removal region-drops it (a class path `teko::env`'s string-keyed map may not exercise) | `0` |
| `hashset_add_dup` | a duplicate `add` is a no-op (a boundary the self-build may not stress) | `0` |

## Gate

`[fixpoint]` — build gen2 (`TEKO_BACKEND=native`), the two fixtures green, FIXPOINT `gen2==gen3` byte-identical.
"Green" = the compiler rebuilds on the Hash-backed `Map`/`Dictionary`/`HashSet`, the class-value + dup-add
boundaries hold, and gen2==gen3. Teaches nothing. **Reseed-class: fixpoint-rebuild.**

## Deps

`COL-Q4` (the Hash base), `COL-Q9` (List hardened the substrate first).

## Done when

`Map`/`Dictionary`/`HashSet` are backed by `Hash<K,V>`, no longer call `teko::list::push`/`arr_drop_at`,
`Dictionary.keys()` stays insertion-ordered, the two fixtures pass, and the self-build fixpoint `gen2==gen3`
holds.
