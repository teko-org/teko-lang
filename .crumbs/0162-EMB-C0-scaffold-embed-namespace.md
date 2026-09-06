---
seq: 0162
crumb-id: EMB-C0
milestone: M5
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:1"       # this wave's annex (surfaces)
  - "docs/design/embed-vfs.md:436-459"                          # original C0 scaffold
  - "DECISION_LOG.md:1151"                                      # D135 (owner surfaces)
  - "src/io/file_stream.tks:6,20"                               # Stream (write sink) / FileStream (fd)
  - "src/checker/resolve.tks:821"                               # class→interface upcast (shipped)
---

# 0162 · EMB-C0 — scaffold `teko::embed` (EmbedCompress / ReadableFS / FileSystem / RoFile / FILES), inert

> The design-ahead scaffold that COMPILES today and is inert: a new `src/embed/embed.tks` with the
> owner-ruled surfaces (D135), empty-default accessors, `files()` over four empty consts. Nothing calls
> it yet. Feature-gated-inert (adds an unused module) → no emitted-byte change.

## Goal

Land the `teko::embed` namespace with the exact surfaces the annex §1 fixes: `EmbedCompress`,
`ReadableFS` (the read-only interface), `FileSystem` (class conforming to it), `RoFile`, and `files():
FileSystem` over four EMPTY module-level consts (`EMBED_NAMES`/`EMBED_BLOBS`/`EMBED_TABLE`= empty `[]byte`,
`EMBED_COUNT` = 0). The accessors return the empty answers (`get` → `no such embedded file` error;
`exists` → false; `list` → empty). Inert: no directive is parsed, no pass runs, no call site exists. This
gives C1+ a home and proves the surfaces type-check before any byte-moving crumb.

## Where

- NEW `src/embed/embed.tks` (namespace `teko::embed`) — `EmbedCompress`, `ReadableFS`, `FileSystem`,
  `RoFile`, the four empty consts, `files()`. Copy the Javadoc blocks from annex §1 VERBATIM.
- NO edit to `file_stream.tks` yet (the `of_slice`/mem-backing lands in EMB-C5).
- NO parser/checker wiring yet (EMB-C1/C2).

## How

1. Create `src/embed/embed.tks`; paste `EmbedCompress` (§1.1), `ReadableFS` (§1.2), `FileSystem` (§1.2)
   with the three `override` accessors bodied to the empty answers, `RoFile` (§1.3), the four empty
   consts, and `files()` (§3.2).
2. `get` empty body: `return error { message = teko::str::concat("no such embedded file: ", path) }`.
   `exists` empty body: `false`. `list` empty body: `[]`.
3. Ensure `FileSystem` is a `class ReadableFS` (class, not struct — annex §1.2 / drift §7.3): only a class
   up-casts to an interface value today (`resolve.tks:821`).
4. Nothing references `teko::embed` — the module is inert; the full gate stays byte-identical.

## Rulings & laws

- **Teko-only:** new `.tks` only.
- **Owner D135 surfaces (verbatim):** `get(path): RoFile | error` (NOT `read(path): RoFile?`); `RoFile`
  fields `name`/`content: FileStream`/`compress`; `EmbedCompress` `exp`.
- **Fork protocol:** the Tier-A vs Tier-B question is RESOLVED (annex §3, no fork); `FILES` as
  const-computed vs `files()` accessor is a veto-open cosmetic choice — this crumb ships `files()`.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; the module is inert → full gate must be
  byte-identical (pay-per-use proof).

## Fixtures

`none — the scaffold is inert; the empty-VFS answers are exercised by EMB-C5's round-trip and by the
prelude self-build once wired`.

## Gate

`[dry]` — compiles; the added module is unused so the full self-build stays byte-identical (`gen2==gen3`
no-op). "Green" = `teko::embed` type-checks, `files()` returns the empty VFS, nothing else changes.
Reseed-class: `none`.

## Deps

`—`

## Done when

`src/embed/embed.tks` compiles with the D135 surfaces, `files()` yields an empty `FileSystem`, and the
self-build is byte-identical (inert module).
