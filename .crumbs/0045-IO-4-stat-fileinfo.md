---
seq: 0045
crumb-id: IO-4
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [IO-1]
sources:
  - "docs/design/io-streaming-0.3.1.md:59-80"     # §2.2 FileInfo/stat/file_size
  - "docs/design/io-streaming-0.3.1.md:240-247"   # §3.4 size without struct stat
  - "docs/design/io-streaming-0.3.1.md:365"       # §7 crumb 4
  - "docs/design/io-streaming-0.3.1.md:452-459"   # §11 R1 lseek-to-end vs statx
---

# 0045 · IO-4 — `stat` / `FileInfo` / `file_size`

> Add `teko::fs::FileInfo` (the exact byte size + is-dir), `stat(path)`, and `file_size(path)` — sized
> via portable `lseek`-to-end (POSIX) / `GetFileSizeEx` (Windows), with `is_dir` via `statx`/
> `GetFileAttributesW` isolated off the hot path.

## Goal

The reader (IO-5 `read_stream`) needs the exact file size to allocate ONE buffer of the exact length and
drain in CHUNK steps — never a growing accumulator. This crumb delivers `FileInfo`/`stat`/`file_size` in
`teko::fs`. The size is measured WITHOUT parsing a `struct stat` (whose layout diverges Linux×macOS and
x86_64×arm64): `lseek(fd,0,SEEK_END)` + `lseek(fd,0,SEEK_SET)` on POSIX, `GetFileSizeEx` on Windows —
the law-first choice (`§11 R1`: least struct surface, portable across all three). `is_dir` (rare, only
for directory info) uses `statx`/`GetFileAttributesW`, isolated off the hot path. Byte-preservation
posture: FEATURE-GATED-INERT new leaf; unreferenced by the corpus → byte-identical emit, `none` reseed.

## Where

- `src/fs/fs.tks` — the `FileInfo` type and `stat`/`file_size` (this branch already carries a
  `FileInfo`/`stat`/`file_size` scaffold at `:12`/`:31`/`:41` alongside the `list_dir`/`mkdir` externs;
  this crumb lands the streaming-backed size path over IO-1's consts + IO-3's `os_size`).

## How

1. **`FileInfo` + `stat` + `file_size`** (`io-streaming §2.2`):

```teko
/**
 * FileInfo — the essentials of a path for planning reads without overrun: the exact byte size and
 * whether it is a directory. `size` is the total; the reader splits into `ceil(size / CHUNK)` reads and
 * the last brings `size % CHUNK` bytes — never padding zeros.
 * @since 0.3.1
 */
exp type FileInfo = struct {
    /** The total file size in bytes. */
    size: u64
    /** True when the path is a directory. */
    is_dir: bool
}

/**
 * file_size — the exact size of `path` in bytes, to dimension the reader. Measured by
 * `lseek(fd,0,SEEK_END)` + `lseek(fd,0,SEEK_SET)` (POSIX) / `GetFileSizeEx` (Windows) — no `struct stat`
 * layout dependency (`io-streaming §11 R1`).
 * @param path  the path to measure
 * @return      the total in bytes, or an error when the path cannot be consulted
 * @throws      when the OS open/seek fails
 * @since 0.3.1
 */
exp fn file_size(path: str): u64 | error

/**
 * stat — the info for `path` without opening a data stream. `size` via `file_size`; `is_dir` via
 * `statx`/`GetFileAttributesW`, isolated off the hot path.
 * @param path  the path to inspect
 * @return      the info, or an error when the path cannot be consulted
 * @throws      when the OS stat/open fails
 * @since 0.3.1
 */
exp fn stat(path: str): FileInfo | error
```

2. **Size via `os_size`.** `file_size` opens `path` read-only (IO-3 `open_read`), calls `os_size`
   (`lseek`-to-end / `GetFileSizeEx`), and closes — the portable path, no struct layout.
3. **`is_dir` isolated.** Only when directory info is requested: Linux/macOS `statx`/`fstat`, Windows
   `GetFileAttributesW` — a small `#os`-guarded helper OFF the size hot path (`§11 R1`).
4. **Leaf isolation.** No compiler site consults `teko::fs::stat`/`file_size` yet (the migration crumbs
   IO-7/IO-8 do) → corpus emit unchanged. `[dry]`.

## Rulings & laws

- **Teko-only:** `.tks` over IO-3's `os_size`/`syscallN`/`extern fn`; no `from "teko_rt"`, no C twin.
- **W15 full Javadoc** on `FileInfo`, `stat`, `file_size`, and the `is_dir` helper; no inline `//`.
- **Owner R1 (`io-streaming §11`):** `lseek`-to-end for size (least struct surface, portable across all
  three platforms); `is_dir` by `statx`/`GetFileAttributesW`, isolated — ratifiable as written.
- **Feature-gated-inert:** unreferenced by the corpus → byte-identical emit, `[dry]`, `none` reseed.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (leaf).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `io_file_size` | `file_size` of a known file == the expected byte total | 0 |

This path is NOT self-build-exercised (the compiler does not `stat` files until IO-7/IO-8), and the
`lseek`-to-end size measurement has no self-build coverage — one isolated oracle (a standalone project
mirroring `src/sys/`) suffices; `is_dir`/`stat` are exercised transitively where directory info is later
consumed.

## Gate

`[dry]` — compile (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) + `io_file_size` +
trivial fixpoint (no emitted-byte change; unreferenced leaf). "Green" = `file_size` returns the exact
byte total via `lseek`-to-end (`GetFileSizeEx` on Windows), `stat` fills `FileInfo`, the fixture exits 0,
and the corpus `teko.c` is byte-identical. Reseed-class: `none`.

## Deps

`IO-1`.

## Done when

`teko::fs::FileInfo`/`stat`/`file_size` compile as a leaf, `file_size` measures via portable
`lseek`-to-end (no `struct stat`), the `io_file_size` fixture passes, and the corpus emit is byte-identical.
