---
seq: 0044
crumb-id: IO-3
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [IO-1, IO-2]
sources:
  - "docs/design/io-streaming-0.3.1.md:82-138"    # §2.3 primitives
  - "docs/design/io-streaming-0.3.1.md:200-247"   # §3 per-OS wrappers
  - "docs/design/io-streaming-0.3.1.md:250-287"   # §4 zero-copy idiom
  - "docs/design/io-streaming-0.3.1.md:361-364"   # §7 crumb 3
---

# 0044 · IO-3 — `file_stream.tks` (`os_*` wrappers + `FileStream` + open/read/write/seek/close)

> Write `src/io/file_stream.tks`: the per-`#os` `os_open`/`os_read`/`os_write`/`os_lseek`/`os_close`/
> `os_size` wrappers (Linux raw syscall, macOS/Windows `extern fn`) plus `FileStream` and
> `open_read`/`open_write`/`open_append` and `stream_read`/`stream_write`/`stream_seek`/`stream_close` —
> the crude streaming primitives the dev drives directly.

## Goal

Deliver the crude I/O primitive layer: a `FileStream` (an OS fd carried as `i64`) plus open/read/write/
seek/close over a per-OS wrapper set. Reads write INTO a caller-supplied reusable buffer (≤ CHUNK = 1024
B, R of `ref []T`, zero-copy) and return the exact count, so the last partial chunk never carries
padding; writes slice `data: ref []byte` by index (via `teko::mem::byte_ptr`, IO-2) without copying the
payload. Byte-preservation posture: this is a NEW LEAF module — not referenced by the compiler yet — so
it compiles isolated and the emitted `teko.c` for the corpus is byte-identical (`none` reseed). It sits
on the IO-1 consts and the IO-2 `byte_ptr` builtin, plus the already-landed `syscallN`/`ptr_word`/
`buf_ptr`/`bytes_from_ptr` seams.

## Where

- `src/io/file_stream.tks` — the module. `FileStream` type; the `exp` primitives; the private per-`#os`
  `os_*` wrappers (Linux `syscallN`, macOS `extern fn … from "System"`, Windows `extern fn … from
  "kernel32"`). `Whence` is reused from `src/io/stream.tks:2`.

## How

1. **`FileStream` + open primitives** (`io-streaming §2.3`):

```teko
/**
 * FileStream — an open OS file handle: a POSIX fd (Linux/macOS) or a Windows HANDLE carried as an
 * `i64`. The dev who wants to control opening, closing, and the buffer used drives these primitives
 * directly; the easy path is the §2.4 helpers (IO-5).
 * @since 0.3.1
 */
exp type FileStream = struct {
    /** The POSIX fd or Windows HANDLE, carried as a machine word. */
    handle: i64
}

/**
 * open_read — open `path` read-only, cursor at the start.
 * @param path  the path to read
 * @return      the opened stream, or an error when the file cannot be opened
 * @throws      when the OS open fails (fd in the error band `[-4095,-1]`)
 * @since 0.3.1
 */
exp fn open_read(path: str): FileStream | error
```

Land `open_write` (truncating, `O_WRONLY|O_CREAT|O_TRUNC`, mode 0o644) and `open_append`
(`O_APPEND|O_CREAT`, writes forced to end by the OS) in the same shape.

2. **The read/write/seek/close primitives** (`io-streaming §2.3`, `§4`):

```teko
/**
 * stream_read — read up to `into.len` bytes (at most CHUNK) from the cursor into `into`, advancing the
 * cursor; return the count read (0 = EOF). The caller consumes `into[0..count]` — the return is the
 * exact boundary, so the last partial chunk never carries padding zeros.
 * @param s     the stream to read (cursor advances)
 * @param into  the reusable destination buffer, written in place (R of `ref []T`)
 * @return      the bytes read (0 at EOF), or a read error
 * @throws      when the OS read fails
 * @since 0.3.1
 */
exp fn stream_read(ref s: FileStream, into: ref []byte): u64 | error

/**
 * stream_write — write `data`, slicing internally into pieces of at most CHUNK bytes and appending each
 * in order. Nothing accumulates; operates the base pointer of `data` via `teko::mem::byte_ptr` (R of
 * `ref []T`: no copy of the payload).
 * @param s     the stream to write (cursor advances)
 * @param data  the bytes to write, in constructed order
 * @return      the total bytes written, or a write error
 * @throws      when the OS write fails
 * @since 0.3.1
 */
exp fn stream_write(ref s: FileStream, data: ref []byte): u64 | error
```

Land `stream_seek(ref s, off: i64, whence: Whence): u64 | error` (append-only streams ignore the write
cursor per OS contract but seek+read still position reads — documented on `open_append`) and
`stream_close(ref s): error | null`.

3. **The zero-copy write loop** (`io-streaming §4`): slice `data` by index without copying — a running
   `off`, `take = min(data.len - off, CHUNK)`, `base = teko::sys::ptr_word(teko::mem::byte_ptr(data,
   off))`, `os_write(s.handle, base to u64, take)`, `off += n`.
4. **Per-`#os` wrappers**, each a private fn declared once per `#os` (prune keeps exactly one):
   - `#os("linux")`: `os_open` = `syscall4(SYS_OPENAT, AT_FDCWD, ptr_word(as_cstr(path)), flags, mode)`;
     `os_read`/`os_write`/`os_lseek`/`os_close` over `SYS_READ`/`SYS_WRITE`/`SYS_LSEEK`/`SYS_CLOSE`;
     error = a return in `[-4095, -1]` (same test as `thr_mmap`).
   - `#os("macos")`: `extern fn open/read/write/lseek/close from "System"` with BSD `O_*` (IO-1).
   - `#os("windows")`: `extern fn CreateFileW/ReadFile/WriteFile/SetFilePointerEx/CloseHandle from
     "kernel32"`; args by block via `store_u64`/`buf_ptr`; `CreateFileW` needs UTF-16 widening (§11 R3:
     `MultiByteToWideChar` isolated in the `#os("windows")` `os_open` — does not contaminate POSIX).
5. **File size** (`os_size`, `io-streaming §3.4`): portable `lseek(fd,0,SEEK_END)` + `lseek(fd,0,
   SEEK_SET)` (POSIX) / `GetFileSizeEx` (Windows) — no `struct stat` layout dependency. (Consumed by
   IO-4.)
6. **Leaf isolation.** No compiler site calls this module yet → the corpus emit is unchanged. `[dry]`.

## Rulings & laws

- **Teko-only:** the whole module is `.tks` over `syscallN`/`extern fn`; no `from "teko_rt"`, no C twin.
- **W15 full Javadoc** on `FileStream`, every `exp` fn, and every private `os_*` wrapper; flatten; no `//`.
- **Owner decree (`io-streaming` header):** the compiler moves to STRICTLY the stream form; this crumb
  delivers the crude sink. Chunk ceiling = CHUNK (1024 B), reusable buffer, no growing accumulator.
- **Seek in append-only (§11 R4):** `stream_seek` on an append-only stream repositions the READ only;
  writes stay at end by OS contract — documented on `open_append`.
- **Windows widening (§11 R3):** UTF-8→UTF-16 via `MultiByteToWideChar`, isolated in the Windows
  `os_open` — platform work, not a Law tension.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (new leaf).

## Fixtures

The stream primitives (chunk boundaries, error branches) are NOT self-build-exercised — but the ergonomic
round-trip fixtures ride on the IO-5 helpers (`0046`) that wrap these primitives. IO-3 in isolation ships
the primitives; add the open-error oracles here (they exercise the raw fd path directly):

| fixture | asserts | expected |
|---|---|---|
| `io_open_missing` | `open_read` of a non-existent path → the `error` arm | own exit (e.g. 40) |
| `io_write_readonly_dir` | `open_write` in a no-permission dir → `error` | own exit (e.g. 41) |

(These reject-path fixtures are standalone projects under `examples/regressions/` mirroring `src/sys/`;
the round-trip / chunk-boundary / seek fixtures land with IO-5 where the loops are exercised end-to-end.)

## Gate

`[dry]` — compile (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) + the two open-error
fixtures + trivial fixpoint (no emitted-byte change; the module is an unreferenced leaf). "Green" = the
module type-checks and emits on Linux/macOS/Windows targets, the open-error fixtures hit the `error` arm
with their own exit codes, and the corpus `teko.c` is byte-identical. Reseed-class: `none`.

## Deps

`IO-1`, `IO-2`.

## Done when

`src/io/file_stream.tks` compiles as a leaf with `FileStream`, the `open_*`/`stream_*` primitives, and
the per-`#os` `os_*` wrappers (Linux syscall / macOS libSystem / Windows kernel32), the open-error
fixtures pass, and the corpus emit is byte-identical.
