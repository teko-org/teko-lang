---
seq: 0089
crumb-id: COL-QQuery
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q20, GATE-2]
sources:
  - "docs/design/table-collection-sql-linq-0.3.1.md:349-607"             # DELIVERABLE 2 — query surface + planner
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:746-761"   # Q-Query (LINQ-typed, GATE-2 resolved)
  - "docs/design/plano-mestre-0.3.1-implementacao.md:249"                # M2 collections row COL-QQuery
  - "src/iter/iter.tks:2"                                                # Iterator<T> pull protocol (fn next(): T | null)
---

# 0089 · COL-QQuery — `Table` query surface (LINQ-typed)

> `Table` query surface — LINQ-typed lazy `Query<Row>`/`Cursor<Row>` from typed `func<…>` delegates; a rule-based
> planner picks an index from STRUCTURED predicates. GATE-2 RESOLVED: LINQ (owner 2026-08-19). Pure `.tks`, `[dry]`.

## Goal

Deliver the typed query surface over `Table<Row>` (COL-Q20): a LAZY `Query<Row>` pipeline built from typed
`func<…>` delegates (§9E, landed), driven by a pull-based `Cursor<Row>` over the settled `Iterator<T>` protocol
(`src/iter/iter.tks:2`, `fn next(): Row | null`), plus a simple rule-based planner that picks an index from
STRUCTURED predicates (`PredEq`/`PredRange`) — never introspecting the opaque `func<Row,bool>` closure. GATE-2
is RESOLVED to LINQ-typed (owner 2026-08-19, "vamos de LINQ, melhor"): a SQL-string front-end is an OPTIONAL
later thin front-end that desugars to this LINQ chain, NEVER the primary (a string boundary needs the dynamic
`Value` model the type system exists to avoid). Pure `.tks` over the SM-R1 surface + §9E delegates — teaches
nothing, `[dry]`. The compiler core does NOT consume it → FULL fixtures. Byte-preserving: additive leaf.

## Where

- `src/collections/table_query.tks` — NEW module. Over `Table<Row>` (COL-Q20) + the landed §9E `func<…>`
  delegates + `Iterator<T>` (`iter.tks:2`).
- No EXISTING fn modified; additive leaf.

## How

Copy the W15 shapes VERBATIM from `table-collection-sql-linq-0.3.1.md:371-563` (`Query<Row>`, `Cursor<Row>`,
`Predicate<Row>`, `Aggregate<Row,A>`, `IndexHint`, and the fluent operators). Do NOT re-derive. Ordered
sub-steps:

1. **The auxiliary objects** (§3.2): `Query<Row>` (a lazy descriptor: source table + optional chosen index +
   ordered stages; immutable, composable — each combinator returns a NEW Query), `Cursor<Row>` (the pull-based
   ResultSet over `Iterator<T>`, snapshotting `epoch` and checking the `live` bit — a mid-iteration tombstone
   is skipped, never a UAF), `Predicate<Row> = PredClosure | PredEq | PredRange`, `Aggregate<Row,A>`,
   `IndexHint = UseIndex | ForceScan`.
2. **The lazy operators** (§3.3): `where`/`select`/`limit`/`skip` stream element-by-element (no
   materialization); `order_by`/`group_by`/`distinct`/`join`-build-side are PIPELINE BREAKERS that buffer into a
   chunk-chain, then resume lazy. Terminals (`to_array`/`first`/`single`/`count`/`aggregate`/`sum`/`min`/`max`/
   `avg`) pull the cursor; `to_array` materializes a FRESH `[]Row` (never a view over the store).
3. **The rule-based planner** (§3.5): index-awareness comes ONLY from the structured predicates read as DATA —
   (1) `PredEq` matching a Hash index → seek; (2) `PredRange` matching a Range index → range-scan; (3)
   `order_by` matching a Range index → iterate in order (skip the sort); (4) else full chunk-chain scan;
   (5) `use_index`/`ForceScan` hints override. A `func<Row,bool>` closure is OPAQUE — treated as a non-seekable
   filter over whatever path the structured predicates chose (full scan if nothing narrows). NO
   pretend-introspection.

```teko
/**
 * where — keep only rows satisfying `p`. A structured `PredEq`/`PredRange` lets the planner seek an index; an
 * opaque `func<Row, bool>` closure filters the driven path element-by-element. Lazy: returns a new Query,
 * executes nothing.
 *
 * @param q  the source query
 * @param p  the predicate (structured for index-awareness, or a closure escape hatch)
 * @return   a new query with the filter appended
 * @since 0.3.1
 */
exp fn where<Row>(q: Query<Row>, p: Predicate<Row>): Query<Row>
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin.
- **W15 full Javadoc** on every type/fn (copy verbatim from the Table doc); no inline `//`.
- **GATE-2 RESOLVED — LINQ-typed (owner 2026-08-19):** the LINQ chain is primary; a SQL string, if ever wanted,
  is sugar that desugars to it — never the primary. A string boundary would need a dynamic `Value` model the
  type system exists to avoid (law-first).
- **Closure opacity (§3.5):** `func<Row,bool>` is NOT introspected; index-awareness is data-driven via
  `PredEq`/`PredRange`; the closure is the full-scan escape hatch.
- **Lazy-by-default + minimal breakers (§3.4):** stream where possible; buffer only at the named pipeline
  breakers — the language-wide "no growing accumulator" law.
- **Snapshot law:** `to_array` materializes a fresh `[]Row`, never a view over the row store (the R9 to_array
  correction).
- **Teach-once (owner 2026-08-19):** no new surface — §9E delegates + Iterator landed; `[dry]`, zero reseed.
- **Rule-based planner is v1** (a cost-based planner is a documented later evolution — ADJACENT, reported, NOT
  opened as an issue).
- **Safety:** NEVER `teko test .` (each fixture ISOLATED); build in a subshell with `ulimit -v 6815744`; commit
  the green step.

## Fixtures

The compiler does NOT consume the query surface → FULL fixtures (per the Table doc §6 spec).

| fixture | asserts | expected |
|---|---|---|
| `query_where_index_seek` | `where_eq` on an indexed column; result equals the brute-force filter (planner picked the index) | `0` |
| `query_where_closure_scan` | `where(closure)`; result equals brute-force (full-scan escape hatch correct) | `0` |
| `query_order_by_index` | `order_by` on a Range-indexed column; output ascending, no sort taken | `0` |
| `query_group_by_aggregate` | `group_by` + `sum` per group; group sums match a hand tally | `0` |
| `query_join_hash` | inner `join` two tables on a hash-indexed key; matched pairs exact | `0` |
| `query_lazy_limit` | `where…limit(3)` pulls only until 3 are yielded (a counter proves early stop) | `0` |
| `query_distinct` | `distinct` de-dups by key; count correct | `0` |

## Gate

`[dry]` — compiles + the seven fixtures green + trivial fixpoint (byte-identical; additive leaf). "Green" =
index-seek vs closure-scan, ordered-index iteration, group/aggregate, hash-join, lazy early-stop, and distinct
all hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q20` (the Table core). `GATE-2` is a RESOLVED owner gate (LINQ, 2026-08-19) — it imposes no ordering
constraint; the LINQ surface is unblocked and buildable now.

## Done when

`src/collections/table_query.tks` compiles the LINQ-typed `Query`/`Cursor` + operators + rule-based planner, the
seven fixtures pass, and a `[dry]` build is byte-identical.
