---
seq: 0088
crumb-id: COL-Q20
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q1, COL-Q4, COL-Q5]
sources:
  - "docs/design/table-collection-sql-linq-0.3.1.md:114-347"             # DELIVERABLE 1 — Table core + atomic txn
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:721-744"   # Q20 Table core (fase1c)
  - "docs/design/plano-mestre-0.3.1-implementacao.md:248"                # M2 collections row COL-Q20
  - "src/runtime/sync.tks:63"                                            # mtx_lock — the fine mutex
---

# 0088 · COL-Q20 — `Table<…>` core (chunk-chain rows + Map/SortedSet indices + atomic multi-index txn; ≤16 cols)

> `Table<…>` core: a COMPOSITION — chunk-chain row store + Map (hash index) + SortedSet (range index) + the ONE
> novel capability, the atomic multi-index transaction. ≤16 columns = the type-arg ceiling. Pure `.tks`, `[dry]`.

## Goal

Deliver the `Table<Row>` core — the multi-index in-memory table as a COMPOSITION of the settled bases: a
chunk-chain row store (COL-Q1) with stable `RowId u64` handles, a Map hash index (COL-Q4) for point lookup, a
SortedSet range index (COL-Q5) for range/`order_by`, guarded by the fine mutex (`src/runtime/sync.tks:63`). The
ONE genuinely new design is the **atomic multi-index transaction**: `insert`/`update`/`delete` mutate the row
AND every index entry as one all-or-nothing step, with a single-store commit-point watermark as the
linearization point for lock-free readers. Columns ≤16 (each column type is a type argument → the ≤16 type-arg
ceiling), realized as a comptime `Table1..Table16` family (Fork A) or a hand-written template family. Pure `.tks`
over the SM-R1 surface + the landed §9E delegates + fine mutex — teaches nothing, `[dry]`. The compiler core
does NOT consume Table → FULL fixtures (incl. the error branch + concurrency). NOT blocked by GATE-2 (that
gates only the query surface, COL-QQuery). Byte-preserving: additive leaf.

## Where

- `src/collections/table.tks` — NEW module. Composes COL-Q1 (chunk-chain rows), COL-Q4 (Map hash index), COL-Q5
  (SortedSet range index) + the fine mutex (`sync.tks:63`). Pure `.tks` (the generics machinery is landed).
- No EXISTING fn modified; additive leaf.

## How

Copy the W15 shapes VERBATIM from `table-collection-sql-linq-0.3.1.md:118-332` (`RowId`, `RowChunk<Row>`,
`RowStore<Row>`, `IndexKind`, `Uniqueness`, `Index<Row,K>`, `Table<Row>`, and `insert`/`update`/`delete`). Do
NOT re-derive. The ordered sub-steps:

1. **The row store** (§2.1): `RowId` (`chunk << 32 | slot`, a VALUE never a pointer), `RowChunk<Row>` (fixed
   `[]Row` of `CHUNK_ROWS` = 256 slots + `used` watermark + `live` tombstone bitset + `next`), `RowStore<Row>`
   (chunk-chain + a FIXED directory of chunk pointers, the §4.1 immortal-segment shape — O(1) `RowId -> &Row`).
   Append is amortized O(1); a full chunk links a NEW fixed chunk; the OLD directory is retained-immortal on
   grow (cross-thread safety), NOT dropped.
2. **The index descriptors** (§2.2): `IndexKind = Hash | Range`, `Uniqueness = Unique | Multi`, `Index<Row,K>`
   keyed by a typed projection `func<Row, K>` (§9E delegate — the ONE key-extraction serving both Fork A
   positional columns and Fork B named fields) over a `Dictionary<K,RowId>` (Hash) or `SortedDictionary<K,RowId>`
   (Range). N indices share the ONE `RowStore`; each holds `RowId`s so growth never invalidates it.
3. **The Table wrapper** (§2.3): `store` + `indices` + `mtx: u64` (fine mutex word) + `epoch: u64` (monotone
   version). Column-index heterogeneity resolved per the §9 note: Fork A's comptime family knows each `K_i`
   statically (no erasure), or an `IndexHandle` interface erases the key type (Fork B fallback).
4. **The atomic transaction** (§2.4, Fork 1 — RECOMMENDED): all writers serialize on `mtx_lock`
   (`sync.tks:63`); within the critical section stage every change, validate `Unique` constraints, then PUBLISH
   by a single commit step — bump the chunk `used` watermark (insert) or clear the `live` bit (delete) LAST.
   Lock-free readers key off that watermark, never observing a half-written row. Roll-back is trivial: an abort
   simply does not perform the final store (staged bytes sit unpublished, index puts were validated-before-
   applied) — no undo log.
5. **The column family** (Fork A): realize `Table1..Table16` from ONE template via §14 parametric-type macros,
   OR 16 hand-written types from the template (mechanical, reviewable). `Row_n = (C1,…,Cn)`, per-ordinal typed;
   ≤16 = the exact ceiling. Fork B (row-struct single-generic) is the documented alternative.

```teko
/**
 * insert — atomically add `row` to the table and to EVERY index, or abort with `error` (leaving the table
 * untouched) if any `Unique` index already holds `row`'s key. Runs under the table's fine mutex; the row slot
 * and all index entries are staged first, unique constraints validated, then a SINGLE commit step (bump the
 * chunk watermark) publishes the row to lock-free readers. All-or-nothing: on a unique-violation abort NOTHING
 * was committed, so there is nothing to roll back.
 *
 * @param t    the table to insert into
 * @param row  the row value to store (value columns are deep-copied into the row-store bucket)
 * @return     the new row's stable id, or an error on a unique-index violation
 * @throws error when a Unique index already contains `row`'s projected key
 * @since 0.3.1
 */
exp fn insert<Row>(ref t: Table<Row>, row: Row): RowId | error
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; the fine mutex is `sync.tks` (`.tks`).
- **W15 full Javadoc** on every type/fn/member (copy verbatim from the Table doc); no inline `//`.
- **≤16 type-arg ceiling (`resolve.tks:659` `max_generic_arity`):** each column is a type argument →
  `Table1..Table16`; honoured by the comptime family, NOT relaxed.
- **Fixed-backing (F1) + banned growable-swap:** every chunk backing + directory is a fixed array; growth
  allocates a NEW fixed array; the old directory is retained-immortal ONLY where cross-thread safety demands —
  a stable-`RowId` requirement, not a fixed-array relaxation. A backing swap would dangle every index — the
  composition is FORCED onto chunk-chain, not incidental.
- **TS model (record):** writers serialize on the fine mutex; the single-store watermark is the linearization
  point for lock-free readers; tombstones epoch-retained then swept (deferred reclaim). No separate Concurrent
  family.
- **GATE-2 does NOT block this crumb** — it gates only the query surface (COL-QQuery). Table core proceeds.
- **Teach-once (owner 2026-08-19):** no new surface — the generics/delegates/mutex are landed; `[dry]`, zero
  reseed. Note (`mudancas-superficie-…:1630-1631`): run when build capacity is free to avoid heavy parallel
  builds with §16.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (each fixture ISOLATED — a monomorph leak in a combined run crashes the
  container); build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The compiler does NOT consume Table → FULL fixtures, including the error branch + concurrency (per the Table doc
§6 spec).

| fixture | asserts | expected |
|---|---|---|
| `table_insert_lookup` | insert N rows, `get` each by the primary hash index; all match | `0` |
| `table_range_index` | a Range index range-query returns the correct ordered window | `0` |
| `table_atomic_txn` | a FAILING update (Unique violation) rolls back ALL index entries; table unchanged (the error branch) | `0` |
| `table_rowid_stable` | insert past `CHUNK_ROWS` (force new chunks + directory grow); old RowIds still resolve | `0` |
| `table_16_cols` | a 16-column table type-checks and round-trips (the ceiling exactly) | `0` |

## Gate

`[dry]` — compiles + the five fixtures green + trivial fixpoint (byte-identical; additive leaf off the critical
path). "Green" = point/range lookup, atomic rollback on Unique violation, stable RowId across growth, and the
16-column ceiling all hold, build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q1` (chunk-chain rows), `COL-Q4` (Map hash index), `COL-Q5` (SortedSet range index).

## Done when

`src/collections/table.tks` compiles as a composition of the three bases + the atomic multi-index transaction,
the five fixtures pass, and a `[dry]` build is byte-identical — the multi-index in-memory Table core at the ≤16
column ceiling.
