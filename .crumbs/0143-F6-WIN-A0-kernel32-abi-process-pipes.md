---
seq: 0143
crumb-id: F6-WIN-A0
milestone: M16
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/f6-process-zerolibc-windows.md:1-90"
  - "DECISION_LOG.md:1020-1031"   # D106/D107 antecipate Windows
  - "DECISION_LOG.md:1140-1170"   # D125 pre-sweep, orphan externs
---

# 0143 · F6-WIN-A0 — Windows kernel32 ABI surface for zero-libc process/pipes

> Add the kernel32 externs + constants the Teko-native Windows spawn/pipe/fd arm needs, so 0144-0146 compile with zero new `from "teko_rt"`.

## Goal

Additive scaffolding only: extend `teko::sys::abi::windows` and `teko::sys` with the
kernel32 entry points and constants that Cluster A (pipes/fd/spawn) and Cluster B (pid)
consume. **Byte-preserving / feature-gated-inert:** an `extern fn` with no caller emits
no table entry, so this crumb changes no emitted C and drives no reseed. It exists as a
separate step purely so the consuming crumbs land against a stable, reviewed ABI and never
carry an orphan extern (D125).

## Where

- `src/sys/abi/windows.tks` — append 8 `pub extern fn` (each `from "kernel32"`):
  - `CreatePipe(read_out: u64, write_out: u64, sa: u64, size: u32): i32`
  - `PeekNamedPipe(h: u64, buf: u64, n: u32, read_out: u64, avail_out: u64, left_out: u64): i32`
  - `DuplicateHandle(src_proc: u64, src: u64, dst_proc: u64, dst_out: u64, access: u32, inherit: i32, options: u32): i32`
  - `GetCurrentProcess(): u64`
  - `Sleep(ms: u32)`
  - `GetLastError(): u32`
  - `GetCurrentProcessId(): u32`
  - `OpenProcess(access: u32, inherit: i32, pid: u32): u64`
- `src/sys/sys.tks` — append the constants (Windows-scoped where they are Win32-only):
  - `DUPLICATE_SAME_ACCESS: u32 = 0x00000002`
  - `ERROR_BROKEN_PIPE: u32 = 109`
  - `WAIT_TIMEOUT_RC: u32 = 0x00000102`
  - `SYNCHRONIZE: u32 = 0x00100000`
  - `WIN_PIPE_POLL_MS: u32 = 5`
  - `SYS_GETPID` / `SYS_KILL` under the existing `#os("linux")` arch `#if` block
    (x86-64: `SYS_GETPID = 39`, `SYS_KILL = 62`; aarch64: `SYS_GETPID = 172`,
    `SYS_KILL = 129`) — mirror the twin-const pattern already used for `SYS_CLONE` etc.
- `src/sys/abi/mac.tks` — append 2 `pub extern fn ... from "System"`:
  - `os_getpid_raw(): i32 = "getpid"`
  - `os_kill_raw(pid: i32, sig: i32): i32 = "kill"`

No existing fn changes. No removals.

## How

1. Append the externs verbatim. `exp`-vs-`pub`: these are **compiler/runtime plumbing the
   user never constructs** → `pub` (per the D111/exp test: no stdlib consumer calls
   `DuplicateHandle`). No doc-comment on `pub` (style law). Match the existing file's bare
   `pub extern fn` lines exactly (no blank-line-per-decl churn beyond the file's style).

2. `size: u32` on `CreatePipe` is the suggested pipe buffer size; pass `PIPE_CAPACITY`
   (65536, already a const in `process.tks`) at the call site — this crumb only declares.

3. `Sleep`/`GetCurrentProcess` return-less-or-scalar; keep signatures byte-exact to the
   Win32 ABI so `link.exe`/`lld-link` resolves them against `kernel32.lib` (D106 —
   `synchronization`/`kernel32` externs already prove green on CI Windows).

4. The linux `SYS_GETPID`/`SYS_KILL` go inside the SAME `#os("linux")` + arch guard that
   holds `SYS_CLONE`/`SYS_WAIT4` (`sys.tks:74-77`, `:356-377`) — one pair per arch.

5. No `[extern.libs.windows]` change needed: `kernel32` is already linked (used by the
   landed `run` arm).

## Rulings & laws

- **Teko-only / D90:** `.tks` only; `teko_rt.{c,h}` untouched; **no new `from "teko_rt"`**
  (these are `from "kernel32"`/`from "System"`, the OS ABI — D106).
- **D106/D107:** Windows is antecipated via the platform linker against the OS ABI, never
  deferred; kernel32 externs resolve today.
- **exp law (D111):** ABI plumbing = `pub`, no doc-comment, zero `//`.
- **D125:** ship the ABI in the same wave as its consumers so no orphan extern lingers;
  this crumb is `[dry]` (no emission change) and folds into 0144's fixpoint.
- **Fork protocol:** no fork — pure additive ABI on already-ratified surface.

## Fixtures

`none — the fixpoint self-build exercises this` (unreferenced externs; nothing to run
until 0144-0146 call them, where the Windows CI leg is the oracle).

## Gate

`[dry]`: compiles clean (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
subshell `ulimit -v 4718592`); emitted `teko.c` byte-identical to base (no caller →
no table entry). reseed-class none.

## Deps

—

## Done when

`src/sys/abi/windows.tks`, `src/sys/abi/mac.tks`, `src/sys/sys.tks` carry the new
externs/consts, the tree compiles, and the emitted C is unchanged from base.
