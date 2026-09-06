# F6 process/exec/pipes — the zero-libc residue (Windows + pid)

## Status verified 2026-08-26 (against `src/`, not presumed)

The POSIX half of F6 is **fully landed in Teko** and calls **zero C**:

- Linux (`src/process/process.tks:148-383`): `clone`/`execve`/`wait4`/`pipe2`/`ppoll`/
  `dup3`/`openat`/`fcntl`/`close`/`read` all through `teko::sys::syscallN` — no
  `from "teko_rt"`.
- macOS (`:385-596`): the same shapes through `teko::sys::abi::os_*` (`fork`/`execve`/
  `wait4`/`pipe`/`poll`/`dup2`/`open`/`fcntl`/`close`/`read` `from "System"`).
- Windows `run`/`run_quiet` (`:837-857`): already Teko-native (D107) —
  `CreateProcessA`+`WaitForSingleObject`+`GetExitCodeProcess`+`CloseHandle`, STARTUPINFOA
  built as a **raw offset buffer** (`win_startup_info`, `:813`), no struct-FFI.

The residue — the only C that is still **CALLED** — is two clusters:

### Cluster A — Windows `spawn_redirected` / pipes / fd-staging (7 externs, all `#os("windows")`)

`src/process/process.tks:665-683`:

| Teko extern | C symbol | zero-libc replacement |
|---|---|---|
| `win_pipe` | `tk_rt_pipe` | `CreatePipe` (raw HANDLE ends, non-inheritable) |
| `win_close_fd` | `tk_rt_close_fd` | `CloseHandle` |
| `win_fd_wait_readable` | `tk_rt_fd_wait_readable` | `PeekNamedPipe` poll loop + `Sleep` |
| `win_fd_fill` | `tk_rt_fd_fill` | `ReadFile` into the shared Teko stage buffer |
| `win_fd_take_byte` | `tk_rt_fd_take_byte` | shared Teko `stage_take_byte()` (already exists) |
| `win_spawn_redirected` | `tk_rt_spawn_redirected` → `tk_win32_spawn_redirected` | `CreateProcessA` + inheritable `DuplicateHandle` per stream |
| `win_wait_one` | `tk_rt_wait_one` → `tk_win32_wait_one` | `WaitForSingleObject`+`GetExitCodeProcess`+`CloseHandle` |

### Cluster B — `pid` / `pid_alive` (2 externs, cross-platform, consumed by journal)

`src/journal/journal.tks:10,12` — folded into F6 by **D126** (`tk_rt_pid`/`tk_rt_pid_alive`
are process ABI, not journal machinery):

| Teko extern | C (`teko_rt.c:5026,5034`) | zero-libc replacement |
|---|---|---|
| `pid_rt` | `getpid` / `GetCurrentProcessId` | linux `SYS_GETPID` · mac `getpid from "System"` · win `GetCurrentProcessId` |
| `pid_alive_rt` | `kill(pid,0)` / `OpenProcess`+`WaitForSingleObject` | linux `SYS_KILL` · mac `kill from "System"` · win `OpenProcess`+`WaitForSingleObject` |

## Design decisions (law-first, no fork)

1. **Windows fd currency becomes the raw HANDLE, not the CRT descriptor.** The C arm
   used `_pipe`/`_get_osfhandle`/`_open_osfhandle`/`_read`/`_close` precisely so every
   Teko `fd: i64` spoke a CRT `int`. Zero-libc forbids the CRT, so the Windows pipe ends
   are raw kernel HANDLEs cast to `i64` — exactly what `ProcHandle.raw`'s doc already
   promises ("a HANDLE cast to i64 on Windows"), and what the already-landed `run`/
   `run_quiet` arm speaks. This is forced by D90/D106 (zero-libc, antecipate), not a
   choice — no owner decision.

2. **Two 64-bit HANDLEs still pack into one `i64` via the Win32 32-bit-significance
   guarantee.** `os_pipe` returns one packed `i64` (existing `pack_pipe`, low-32/high-32).
   Win32 documents every HANDLE as 32-significant-bit (`HandleToLong`/`LongToHandle`), so
   the low 32 bits round-trip a pipe HANDLE losslessly; unpack zero-extends back to a valid
   `u64` HANDLE for `ReadFile`/`PeekNamedPipe`/`CloseHandle`. Documented platform
   guarantee resolves this law-first — not a fork. (Fallback if ever violated: stash the
   two full HANDLEs in the process-wide fd-stage control block — noted, not needed.)

3. **No struct-FFI surface is required.** D107 already settled that `from "tag"` cannot
   express `STARTUPINFOA`/`PROCESS_INFORMATION`/`SECURITY_ATTRIBUTES` (the defining
   `<windows.h>` also declares the kernel32 functions we extern → TU collision). The idiom
   is the **raw offset buffer** (`teko::mem::buf_ptr` + `store_u64`/`store_u32_at` by
   offset), already in production in `win_startup_info`/`win_open_nul`. `spawn_redirected`
   only extends that buffer with three `hStdInput/hStdOutput/hStdError` slots + the
   `STARTF_USESTDHANDLES` flag. **The D102 follow-up #1 stays closed; no new surface.**

4. **The staging buffer is the shared Teko one.** `fd_stage_base()`/`stage_take_byte()`
   (`process.tks:345-383`) already back Linux+macOS from `region_program`; the Windows
   `os_fd_fill`/`os_fd_take_byte` join them — `ReadFile` writes into `stg + STAGE_HDR_BUF`,
   `take_byte` is the shared drain. Kills the per-task C `rt_fd_stage[]`.

5. **Inheritance mirrors the C exactly.** `CreatePipe` with `sa = 0` → both ends
   non-inheritable (the `_O_NOINHERIT` equivalent). Per launch, `DuplicateHandle(self,
   end, self, &dup, 0, /*inherit*/1, DUPLICATE_SAME_ACCESS)` makes the one inheritable dup
   the child gets; `CreateProcessA(..., bInheritHandles=1, ...)`; `CloseHandle(dup)` right
   after launch. The caller's own end is never touched — the POSIX ownership rule.

## No genuine fork

Every open point above is resolved by an existing ruling or a documented platform
guarantee. There is **no HALT**. The only owner-facing item is the standing D104-T5
carve-out (a Windows CI fixture is the SOLE oracle for the Windows arm — the Linux
self-build fixpoint never drives `#os("windows")` at runtime); F6's process-half harness
already exists, so no new test is authored.

## Sequence

0143 (ABI surface, `[dry]`) → 0144 (Windows pipes/fd, `[fixpoint]`+win CI) → 0145
(Windows spawn/wait, `[fixpoint]`+win CI) → 0146 (pid/pid_alive cross-platform, journal
reroute, `[fixpoint]`) → 0147 (pre-sweep the dead C, D125, `[RITUAL]`). `tk_rt_close_fd`
is **excluded from the sweep** — journal (`close_fd_rt`, F4/F8 ficha) still calls it.
