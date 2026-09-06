---
seq: 0165
crumb-id: EMB-C3
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [EMB-C2]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:4"        # §4 where the pass plugs in
  - "docs/design/embed-vfs.md:359-414"                          # read seam + compress
  - "src/compress/deflate.tks:38"                               # deflate(data): []byte
  - "src/compress/gzip.tks:22"                                  # gzip_compress(data): []byte
  - "src/io/io.tks:46"                                          # write_file_bytes (no read_file_bytes yet)
---

# 0165 · EMB-C3 — compile-time read seam `read_file_bytes` + per-directive build-time compress + level-range validation

> The embed pass reads each resolved file's BYTES at compile time (binary assets need bytes, not `str`),
> compresses per the directive's codec, and validates the level against the codec range. Adds ONE
> maintained-C seam `tk_rt_read_file_bytes` (inside the frozen-C exception, compile-time-only, no trace in
> the emitted program). Still no rodata emission (that is EMB-C4) → the accumulated heaps live only in the
> pass; feature-gated-inert on emitted bytes, but the seam changes the compiler → fixpoint.

## Goal

Complete the embed pass's INPUT half: for each `EmbedDecl`, `resolve_embed_path` (EMB-C2) → key; read the
file bytes via a new `read_file_bytes` seam; validate LEVEL against the codec's declared range (Deflate
0..9, Gzip 0..9; None ignores level); compress build-time (`deflate`/`gzip_compress`, or verbatim for
None); accumulate `(key, comp_bytes, orig_len, comp_tag)` into the pass's ordered set (conflict PANIC from
EMB-C2). The heaps are built in-memory here; EMB-C4 emits them to rodata.

## Where

- `src/runtime/teko_rt.{c,h}` (MAINTAINED-C exception) — add
  `pub extern fn read_file_bytes(path: str): []byte | error = "tk_rt_read_file_bytes" from "teko_rt"`;
  the C body opens+reads the file to a byte buffer. Compile-time-only (called only by the embed pass).
- `src/io/io.tks:46` — expose `read_file_bytes` alongside `write_file_bytes`.
- The embed pass module — the resolve→read→validate→compress→accumulate loop; `level_in_range(comp,
  level)`.

## How

1. Add the maintained-C `tk_rt_read_file_bytes` (D90 exception: `teko_rt.c/.h` editable; this is compile-
   time infra, NOT the array-expunge and NOT a new emitted-program dependency).
2. `level_in_range(comp: EmbedCompress, level: i64): bool` — Deflate/Gzip → `0 <= level <= 9`; None → true.
   Out-of-range → `#embed compression level <n> out of range for <codec> (0..9)`.
3. Per directive: read bytes; `comp_bytes = match comp { None => bytes; Deflate => deflate(bytes); Gzip =>
   gzip_compress(bytes) }`; record `orig_len = bytes.len`, `comp_tag = comp`.
4. A missing file → `#embed: no such file in the project: "<path>"`.
5. Accumulate into the ordered key set (the conflict PANIC is EMB-C2's).

## Rulings & laws

- **Teko-only + maintained-C exception (D90):** the ONE non-`.tks` addition is `tk_rt_read_file_bytes`,
  compile-time-only, leaving no runtime trace (M.0). Not the array-expunge path.
- **M.1 fail-loud:** out-of-range level = located error, never a clamp.
- **Compress reuse:** `deflate`/`gzip_compress` already landed (`src/compress/*`) — call, don't reimpl.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; the seam changes the compiler → build
  gen2, `gen2==gen3`. Ratchet: ADDITIVE build path → peak must NOT grow.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `embed_level_oob` | `#embed("f", Deflate, 99)` | `EXPECT_COMPILE_FAIL` (out of range) |
| `embed_missing_file` | `#embed("nope.txt")` | `EXPECT_COMPILE_FAIL` (no such file) |

## Gate

`[fixpoint]` — build gen2 + the reject fixtures + `gen2==gen3` byte-identity (the seam is deterministic;
no VFS emitted yet since the corpus has no `#embed` → emitted bytes unchanged, but the compiler binary
changed → reseed). "Green" = bytes read, level validated, codecs applied, `gen2==gen3`, peak flat/down.
Reseed-class: `fixpoint-rebuild`.

## Deps

`EMB-C2`

## Done when

The embed pass reads+validates+compresses each directive into the in-memory heaps, the maintained-C read
seam links, and `gen2==gen3` with peak not grown.
