---
seq: 0167
crumb-id: EMB-C5
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [EMB-C4]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:1"        # §1.3 FileStream mem-backing / §3.3 cursor
  - "src/io/file_stream.tks:332"                                # stream_read (add memory branch)
  - "src/io/file_stream.tks:40"                                 # of_handle (init mem/rpos)
  - "src/checker/resolve.tks:821,964"                           # class→interface upcast / type_conforms_to
---

# 0167 · EMB-C5 — `FileStream` memory-backing (`of_slice`) + `get`/`exists`/`list` build `RoFile`; `FileSystem` conforms `ReadableFS`

> The accessor half: extend `FileStream` with a read-only memory cursor (a rodata slice), so `get` returns
> a `RoFile` whose `content` streams the raw (still-compressed) blob bytes ≤1024 B at a time — the dev
> decompresses. Prove `FileSystem` conforms to `ReadableFS` (class up-cast, shipped). Byte-mover (real
> accessors emit) → RITUAL + seed bump (after which the compiler's own corpus may use `#embed`).

## Goal

Complete the VFS OUTPUT half (annex §1.3, §3.3): (1) extend `FileStream` with `mem: []byte` + `rpos: u64`
and a `handle < 0` sentinel; add `of_slice(data): FileStream`; add the memory branch to `stream_read`.
(2) `FileSystem.get(path)` scans `EMBED_TABLE` for the key, slices `EMBED_BLOBS[off..off+len]` (zero-copy
into rodata), builds `RoFile { name, content = of_slice(slice), compress = comp_tag }`, or the located
`no such embedded file` error. (3) `exists`/`list` over the table. (4) confirm `FileSystem` up-casts to
`ReadableFS` (a `fn f(fs: ReadableFS)` accepts `files()`). The dev reads `content` then inflates per
`compress` — NO decode in the VFS (supersedes ruling 7).

## Where

- `src/io/file_stream.tks:20` — add `mem`/`rpos` fields; `:40` `of_handle` inits them (`mem=[]`,`rpos=0`);
  new `of_slice` (annex §1.3); `:332` `stream_read` gains the `handle < 0` memory branch (annex §1.3).
- `src/embed/embed.tks` — body `get`/`exists`/`list` (replace the EMB-C0 empty answers); the table scan.
- `src/checker/resolve.tks:964` `type_conforms_to` — no change; just confirm `FileSystem`→`ReadableFS`.

## How

1. `FileStream` mem-backing (annex §1.3): fields, `of_slice`, `stream_read` branch. Byte-preserving for
   the fd path (`handle < 0` never occurs on a real fd).
2. `get`: locate the key in the packed table (O(count)); on hit compute `(blob_off, blob_len, comp_tag)`;
   `content = teko::io::of_slice(arr_slice(self.blobs, blob_off, blob_off+blob_len))`; return the `RoFile`.
   On miss: `error { message = teko::str::concat("no such embedded file: ", path) }`.
3. `exists`: table scan → bool. `list`: decode the name heap → `[]str` in emit order.
4. Conformance: add a compiler-internal use `fn f(fs: ReadableFS)` OR rely on the prelude self-build — the
   up-cast is exercised when the compiler passes `files()` as a `ReadableFS` value.

## Rulings & laws

- **Teko-only.**
- **Owner D135:** `get(path): RoFile | error`; dev decompresses (VFS returns raw + tag); FileStream
  read-only over the rodata slice; `content` never materializes the whole file (≤1024 B chunks).
- **Interface-value (interface-value-type.md §6.4):** class→interface up-cast is shipped (resolve.tks:821)
  — no new mechanism.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; **RITUAL** — full native ladder,
  `gen2==gen3`, MEM_PARANOID, native round-trip. Ratchet: ADDITIVE → peak NOT grown. After green → 🔑
  SEED-BUMP (the seed now understands `#embed`, unblocking the mascot/prelude corpus use).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `embed_roundtrip_none` | embed a blob None → `get` → `stream_read` == original | `0` |
| `embed_roundtrip_deflate` | embed Deflate → `get` → read raw → `inflate` == original | `0` |
| `embed_get_absent` | `get("nope")` on a populated VFS | `EXPECT_COMPILE_FAIL`-style runtime error asserted `0` via catch |

(Round-trips run the accessor path the compiler's own build does NOT yet drive until PRE-C1 uses `#embed`
for the prelude; kept as named oracles.)

## Gate

`[RITUAL]` — full native ladder + the round-trip oracles native, `gen2==gen3`, MEM_PARANOID 0, peak flat/
down. "Green" = `of_slice`/mem-cursor works, `get`/`exists`/`list` answer correctly, `FileSystem` up-casts
to `ReadableFS`, the dev decompresses. Reseed-class: `fixpoint-rebuild`. 🔑 SEED-BUMP after green.

## Deps

`EMB-C4`

## Done when

`FileStream` streams a rodata slice read-only, `get` returns the raw+tagged `RoFile`, `exists`/`list`
answer, `FileSystem` conforms `ReadableFS`, round-trips pass native, and the seed is bumped.
