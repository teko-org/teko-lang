---
seq: 0090
crumb-id: COL-QFile
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [COL-Q20, IO-8]
sources:
  - "docs/design/table-collection-sql-linq-0.3.1.md:610-736"             # DELIVERABLE 3 — IO-backed FileTable
  - "docs/design/colecoes-memoria-fila-implementacao-0.3.1.md:763-779"   # Q-File FileTable
  - "docs/design/plano-mestre-0.3.1-implementacao.md:250"                # M2 collections row COL-QFile
  - "docs/design/io-streaming-0.3.1.md"                                  # the single IO surface (FileStream/stream_read/write)
---

# 0090 · COL-QFile — `FileTable<Row>` (io-streaming backed; rebuild-indices-on-load)

> `FileTable<Row>` — the persistent single-table store over io-streaming: `[header][rows]`, rebuild indices on
> load, stream (not mmap), explicit save. A load/save skin over the Table core. Pure `.tks`, `[dry]`.

## Goal

Deliver `FileTable<Row>` — the optional binary single-table store ("SQLite-lite, one table") over the
io-streaming surface: an in-memory `Table<Row>` (COL-Q20) plus a backing file path. `open` streams the rows off
disk (the io-streaming `read_stream` idiom) and REBUILDS every index by re-inserting; `save` serializes the
LIVE rows back via `stream_write` (tombstones dropped); indices are NOT persisted (derived on load — never a
persistence bug). Load/save is a skin — all queries and atomic transactions are the COL-Q20/COL-QQuery machinery
unchanged. Reuses io-streaming's SOLE IO surface (no mmap, no second surface — the sealed decree). Ships the
INJECTED-CODEC fallback (a caller-supplied `func<Row,[]byte>`/`func<[]byte,Row>` pair) that compiles TODAY
against the §9E delegates; swapping to comptime field-reflection is a later crumb blocked on serial-tags. Pure
`.tks`, `[dry]`; the compiler core does NOT consume it → FULL fixtures. NOT blocked by GATE-2. Byte-preserving.

## Where

- `src/collections/file_table.tks` — NEW module. Wraps `Table<Row>` (COL-Q20); reads the io-streaming
  `FileStream` / `open_read`/`open_write`/`stream_read`/`stream_write` surface (delivered by IO-8, the compiler
  writes migration — the full io-streaming stack is in the seed by then).
- No EXISTING fn modified; additive leaf.

## How

Copy the W15 shapes VERBATIM from `table-collection-sql-linq-0.3.1.md:666-709` (`FileTable<Row>`, `open`,
`save`) + the binary format (`:711-726`). Ordered sub-steps:

1. **`FileTable<Row>`** holds `intern mem: Table<Row>` (the real engine), `intern path: str`, `intern dirty:
   bool`. All indices/transactions/queries are the in-memory Table's.
2. **The format (v1, rebuild-on-load)**: `[header][rows…]`. Header = magic `"TEKOTBL1"` + `version:u32` +
   `ncols:u32` (≤16) + `coltags:[n]u32` + `nrows:u64`. Each row = the n columns serialized in order (fixed-width
   numeric → little-endian bytes; str/variable → u32 length prefix + bytes).
3. **`open(path, indices)`** reads the header, streams each typed row via the io-streaming CHUNK (≤1024) loop,
   `insert`s it into a fresh in-memory `Table` (which rebuilds every registered index), returns the ready table.
   A missing file yields an EMPTY table at `path` (not an error).
4. **`save(ft)`** writes the header then each LIVE row (tombstones dropped) via `stream_write` in CHUNK-bounded
   pieces; indices are NOT written (rebuilt on the next open); clears `dirty`.
5. **The serialization codec — injected fallback (ships now):** `open`/`save` take a `func<Row,[]byte>` /
   `func<[]byte,Row>` pair supplied by the caller (compiles today against §9E delegates). The comptime
   field-reflection route (zero user code) is a LATER swap, blocked on serial-tags — see Deps/blocked.

```teko
/**
 * open — load a FileTable from `path`: read the header (magic, version, column type tags, row count), stream
 * each typed row via the io-streaming CHUNK loop, `insert` it into a fresh in-memory Table (which rebuilds
 * every index), and return the ready table. A missing file yields an empty table at `path`.
 *
 * @param path     the backing file
 * @param indices  the index descriptors to (re)build over the loaded rows
 * @return         the loaded table, or an error on a corrupt header / read failure
 * @throws error   on bad magic, unsupported version, a short/garbled row, or an io-streaming read error
 * @since 0.3.1
 */
exp fn open<Row>(path: str, indices: []IndexHandle): FileTable<Row> | error
```

## Rulings & laws

- **Teko-only:** `.tks` leaf; no C twin; reads the io-streaming `.tks` surface (landed via IO-8).
- **W15 full Javadoc** on every type/fn (copy verbatim from the Table doc); no inline `//`.
- **One IO surface (io-streaming decree):** load/save uses ONLY the stream primitives; mmap-for-files is
  REJECTED on the "one IO surface" law regardless of performance (a file mmap would be a second surface).
- **Rebuild-on-load:** indices are DERIVED (never persisted) — no on-disk index consistency bug; matches the
  small-data framing.
- **Explicit save (not write-back-on-change):** the whole-table snapshot; an append-only journal is an optional
  later durability layer.
- **≤16 column ceiling** honoured (`ncols ≤ 16` in the header — the type-arg ceiling).
- **Teach-once (owner 2026-08-19):** the injected-codec route needs no new surface (§9E delegates landed);
  `[dry]`, zero reseed.
- **Byte-preserving:** additive leaf, no core consumer.
- **Safety:** NEVER `teko test .` (each fixture ISOLATED); build in a subshell with `ulimit -v 6815744`; commit
  the green step.

## Fixtures

The compiler does NOT consume FileTable → FULL fixtures (per the Table doc §6 spec).

| fixture | asserts | expected |
|---|---|---|
| `filetable_roundtrip` | build a table, `save`, `open`, re-query; every row + every index equal (rebuild-on-load correct) | `0` |
| `filetable_rebuild_index` | indices are rebuilt on load (a query hits the reconstructed index) | `0` |
| `filetable_missing_open` | `open` a nonexistent path → an empty table (not an error) | `0` |
| `filetable_corrupt_header` | `open` a file with bad magic → the error branch | `EXPECT_COMPILE_FAIL` |
| `filetable_chunk_boundary` | rows straddling the io-streaming CHUNK=1024 boundary round-trip byte-identical | `0` |

(`filetable_corrupt_header` is a runtime error branch — its `.tkr` asserts the error VARIANT via an isolated
native exit token; where the harness models a bad-magic reject as a compile/run failure marker, use the
`EXPECT_COMPILE_FAIL`/error-token form the sibling table fixtures use.)

## Gate

`[dry]` — compiles + the fixtures green + trivial fixpoint (byte-identical; additive leaf). "Green" =
save/open round-trip, index rebuild, missing→empty, corrupt→error, and the CHUNK-boundary round-trip all hold,
build byte-identical. **Reseed-class: none.**

## Deps

`COL-Q20` (the Table core), `IO-8` (the compiler-writes migration — the full io-streaming `FileStream`/
`stream_read`/`stream_write` surface is in the seed by then). **Blocked piece (design-ahead):** the comptime
field-reflection codec depends on `serial-tags-comptime-field-reflection`; until it lands the INJECTED-CODEC
fallback (compiles today) is the shipping path — the reflection swap is a separate later crumb.

## Done when

`src/collections/file_table.tks` compiles with the injected-codec fallback over io-streaming, the fixtures pass,
and a `[dry]` build is byte-identical — a persistent single-table store that rebuilds its indices on load.
