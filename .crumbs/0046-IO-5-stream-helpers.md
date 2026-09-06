---
seq: 0046
crumb-id: IO-5
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [IO-3]
sources:
  - "docs/design/io-streaming-0.3.1.md:143-165"   # §2.4 writer/reader helpers
  - "docs/design/io-streaming-0.3.1.md:250-287"   # §4 zero-copy idiom + 1024 buffer
  - "docs/design/io-streaming-0.3.1.md:366-368"   # §7 crumb 5
  - "docs/design/io-streaming-0.3.1.md:388-406"   # §8 fixtures
---

# 0046 · IO-5 — helpers `write_stream` / `append_stream` / `read_stream` (open + defer-close + 1024 loop)

> Add the easy-path helpers: `write_stream(path, data)` (open truncating + defer-close + write sliced in
> CHUNK), `append_stream(path, data)` (open append + defer-close), and `read_stream(path)` (stat-size →
> ONE exact buffer → drain in CHUNK steps + defer-close).

## Goal

The crude primitives (IO-3) are the dev's manual path; this crumb delivers the RECOMMENDED helpers that
open + defer-close + loop for you. `write_stream`/`append_stream` write `data: ref []byte` sliced into
CHUNK pieces in order, closing with a defer even on error — where the output is built is where it is
written, in order, without accumulating. `read_stream` measures `path` by stat (IO-4), allocates ONE
buffer of the exact size, drains in CHUNK steps, and defer-closes: it still materializes (the lexer needs
the whole text) but WITHOUT the FFI hop and WITHOUT growing. Byte-preservation posture: FEATURE-GATED
-INERT new leaf; unreferenced by the corpus until IO-6/IO-7/IO-8 → byte-identical emit, `none` reseed.
This is the crumb whose round-trip is fixture-testable end-to-end (§8).

## Where

- `src/io/file_stream.tks` (or a sibling helper module) — the three `exp` helpers over the IO-3
  primitives (`open_read`/`open_write`/`open_append`, `stream_read`/`stream_write`, `stream_close`) and
  the IO-4 `file_size`.

## How

1. **`write_stream` / `append_stream`** (`io-streaming §2.4`):

```teko
/**
 * write_stream — the recommended write path: open `path` truncating, write `data` sliced into CHUNK
 * pieces, and close with a defer (even on error). Where the output is built is where it is written, in
 * order, without accumulating.
 * @param path  the destination path
 * @param data  the bytes to write
 * @return      null on success, or the first error
 * @throws      when open or any write fails
 * @since 0.3.1
 */
exp fn write_stream(path: str, data: ref []byte): error | null

/**
 * append_stream — as write_stream, but append to the end of `path` (create when absent).
 * @param path  the destination path
 * @param data  the bytes to append
 * @return      null on success, or the first error
 * @throws      when open or any write fails
 * @since 0.3.1
 */
exp fn append_stream(path: str, data: ref []byte): error | null
```

2. **`read_stream`** (`io-streaming §2.4`, `§4`):

```teko
/**
 * read_stream — the easy read-only path: measure `path` by stat, allocate ONE buffer of the exact size,
 * drain in CHUNK steps, and close with a defer. Materializes (the lexer needs the whole text) but
 * without the FFI hop and with exact dimensioning — never growing.
 * @param path  the path to read
 * @return      the file's bytes, or the first error
 * @throws      when stat, open, or any read fails
 * @since 0.3.1
 */
exp fn read_stream(path: str): []byte | error
```

3. **The drain loop.** `read_stream`: `var out: [file_size(path)]byte = []` (exact size, no growth), a
   reusable `scratch = teko::mem::bytes_from_ptr(teko::mem::buf_ptr(CHUNK), CHUNK)`, loop `stream_read`
   into `scratch`, copy `scratch[0..count]` into `out` at the running offset by index, until `count ==
   0`. Defer-close the stream. No `arena_pop` mid-drain (the scratch must stay stable — `§9`).
4. **Defer-close discipline.** All three helpers register the close as a `defer` immediately after open,
   so the fd is released on every exit path including the `error` arm.
5. **Leaf isolation.** No corpus site calls these yet (IO-6/IO-7/IO-8 migrate to them) → corpus emit
   unchanged. `[dry]`.

## Rulings & laws

- **Teko-only:** `.tks` over IO-3/IO-4; no `from "teko_rt"`, no C twin.
- **W15 full Javadoc** on the three helpers; flatten; no inline `//`.
- **Owner decree:** the compiler uses STRICTLY the stream form; these helpers are the easy path that
  satisfies it — the reusable 1024-B buffer, no growing accumulator, write at construction in order.
- **Scratch stability (`io-streaming §9`):** the 1024-B read buffer lives in the arena (`buf_ptr`) and
  must be stable during the drain — no `arena_pop` mid-loop; it is written in place, never reassigned,
  so purge-on-reassign does not touch it.
- **Feature-gated-inert:** unreferenced by the corpus → byte-identical emit, `[dry]`, `none` reseed.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (leaf).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `io_stream_roundtrip` | `write_stream` of N bytes + `read_stream`, compare equal | 0 |
| `io_stream_chunk_boundary` | write exactly 1024, 1025, 2048, 2049 bytes; reread; compare | 0 |
| `io_stream_last_partial` | file of size `k*1024 + r` (r≠0); assert last chunk = r bytes, no zeros | 0 |
| `io_stream_append` | `append_stream` 3×; order preserved byte-for-byte | 0 |
| `io_stream_seek_read` | `stream_seek(End,-4)` + read; check the last 4 bytes | 0 |

These paths (chunk boundaries, last-partial, append order, seek) are NOT self-build-exercised — the file
I/O with error paths and chunk fronteiras merit isolated `.tkr` oracles (standalone projects under
`examples/regressions/` mirroring `src/sys/`), per `io-streaming §8`.

## Gate

`[dry]` — compile (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) + the five stream
fixtures + trivial fixpoint (no emitted-byte change; unreferenced leaf). "Green" = round-trip equality
holds across the chunk boundaries, the last partial chunk carries exactly `r` bytes (no padding),
append order is preserved, seek+read returns the right tail, and the corpus `teko.c` is byte-identical.
Reseed-class: `none`.

## Deps

`IO-3`.

## Done when

`write_stream`/`append_stream`/`read_stream` compile as a leaf over the IO-3/IO-4 primitives, the five
stream fixtures pass (round-trip, chunk boundary, last-partial, append, seek), and the corpus emit is
byte-identical.
