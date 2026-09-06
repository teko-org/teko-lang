---
seq: 0043
crumb-id: IO-1
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/io-streaming-0.3.1.md:200-247"   # §3 syscalls por SO (const table)
  - "docs/design/io-streaming-0.3.1.md:348-355"   # §7 crumb 1
  - "docs/design/io-streaming-0.3.1.md:436-449"   # §10 nothing blocked
---

# 0043 · IO-1 — syscall constants (`SYS_READ/…`, `O_*`, `SEEK_*`) in `sys.tks`

> Add the file-I/O syscall numbers (`SYS_READ`/`SYS_CLOSE`/`SYS_LSEEK`/`SYS_OPENAT`/`SYS_STATX`,
> `#arch`-split), `AT_FDCWD`, the open flags (`O_RDONLY/WRONLY/CREAT/TRUNC/APPEND`), `SEEK_SET/CUR/END`,
> and the 0o644 create mode to `src/sys/sys.tks` — const-only leaf, transcribed from the kernel ABI.

## Goal

The streaming I/O layer (`file_stream.tks`, IO-3) needs the file-descriptor syscall numbers and flags.
This crumb lands them as `#os`/`#arch`-guarded literal `const`s in `src/sys/sys.tks` next to the already
-present `SYS_WRITE`/`SYS_EXIT_GROUP`/`SYS_MMAP` block, each transcribed from the kernel ABI table
(never a C header/macro). Byte-preservation posture: FEATURE-GATED-INERT — the consts are unreferenced
until IO-3 uses them, so the compiler's own emitted `teko.c` is byte-identical (a program with zero
references emits nothing new). Pure leaf, `none` reseed. This is the safest possible first step of the
io-streaming wave and unblocks IO-3/IO-4.

## Where

- `src/sys/sys.tks:14` onward — the existing `#os("linux")`/`#arch` `SYS_*` block — ADD the file-I/O
  numbers and the `#os`-guarded flag/mode consts, in the same self-contained-per-target style the
  existing `SYS_WRITE`/`SYS_MMAP` pairs use.

The macOS branch uses BSD `O_*` flag values (`#os("macos")`); Windows uses no numeric flags (it drives
the CreateFileW API), so no Windows consts here.

## How

1. **Linux syscall numbers** (`#os("linux")`, `#arch`-split x86_64 / aarch64), typed `i64` to match the
   `syscallN` `nr`/`a*` params (no cast at the call):

| const | x86_64 | aarch64 | note |
|---|---:|---:|---|
| `SYS_READ` | 0 | 63 | |
| `SYS_CLOSE` | 3 | 57 | |
| `SYS_LSEEK` | 8 | 62 | |
| `SYS_OPENAT` | 257 | 56 | arm64 has no `open`; use `openat(AT_FDCWD, …)` on both |
| `SYS_STATX` | 332 | 291 | uniform struct layout across arches — preferred over `fstat` |

2. **Flags + mode** (`#os("linux")`, arch-uniform), typed `i64`:

```teko
/**
 * O_RDONLY — the Linux `open`/`openat` flag for read-only access (value 0, arch-uniform). Part of
 * `teko::sys`, the curated per-`#os`/`#arch` syscall-constant module: every value is a literal Teko
 * `const` transcribed from the kernel ABI (never a C header/macro), `#os`-guarded so the §17 prune
 * keeps exactly the target's definition before the checker sees the others.
 * @since 0.3.1
 */
#os("linux")
pub const O_RDONLY: i64 = 0
```

Land the sibling consts in the same shape: `AT_FDCWD = -100`, `O_WRONLY = 1`, `O_CREAT = 0o100`,
`O_TRUNC = 0o1000`, `O_APPEND = 0o2000`, `SEEK_SET = 0`, `SEEK_CUR = 1`, `SEEK_END = 2`, and the create
mode `OPEN_MODE_644 = 0o644` — each a full-Javadoc `pub const`.

3. **macOS BSD flags** (`#os("macos")`, own prunable block): `O_RDONLY = 0`, `O_WRONLY = 1`,
   `O_CREAT = 0x0200`, `O_TRUNC = 0x0400`, `O_APPEND = 0x0008` — transcribed from Darwin `<fcntl.h>`,
   NEVER shared with the Linux block (the "each per-target is an autonomous unit" discipline).
4. **No Windows numeric consts.** The Windows branch (IO-3) uses `CreateFileW` dispositions via the
   kernel32 API, not numeric flags.
5. **Byte-identity check.** Because nothing references the new consts yet, the compiler's emitted
   `teko.c` is byte-identical — a `[dry]` trivial fixpoint confirms no emitted-byte change.

## Rulings & laws

- **Teko-only:** const-only edit to `src/sys/sys.tks`; no C twin.
- **W15 full Javadoc** on every `const` (the linux/mac blocks each carry the kernel-ABI value in the
  doc-comment); no inline `//`.
- **Transcribed, not included (`io-streaming §3`, `plano-s16` monolith discipline):** every value is a
  literal transcribed from the kernel ABI table; NO C header is included in the emitted `teko.c`.
- **`#os`/`#arch` prune:** off-target blocks are pruned before the checker (the existing `SYS_*`
  precedent); `#arch`-split numbers use their own prunable blocks.
- **Feature-gated-inert:** unreferenced consts change nothing that compiles today → `[dry]`, `none` reseed.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (leaf).

## Fixtures

none — the fixpoint self-build exercises this. The consts are const-only and inert; the io-streaming
runtime fixtures (roundtrip, chunk-boundary, seek) belong to the crumbs that CONSUME these consts (IO-3
`0044`, IO-5 `0046`), where the syscall paths actually fire. A const-only leaf needs no isolated `.tkr`.

## Gate

`[dry]` — compile (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) + trivial fixpoint
(no emitted-byte change). "Green" = the new consts type-check on Linux (x86_64 + aarch64) and macOS,
prune correctly off-target, and the compiler's emitted `teko.c` is byte-identical (nothing references
them yet). Reseed-class: `none`.

## Deps

`—`.

## Done when

`SYS_READ`/`SYS_CLOSE`/`SYS_LSEEK`/`SYS_OPENAT`/`SYS_STATX` (`#arch`-split), `AT_FDCWD`, the `O_*` flags
(Linux + macOS BSD), `SEEK_SET/CUR/END`, and `OPEN_MODE_644` are present as full-Javadoc `#os`/`#arch`
consts in `sys.tks`, they prune correctly per target, and the emitted `teko.c` is byte-identical.
