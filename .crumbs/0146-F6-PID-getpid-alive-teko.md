---
seq: 0146
crumb-id: F6-PID
milestone: M16
gate: "[fixpoint]"
reseed-class: "expurgo"
deps: [F6-WIN-A0]
sources:
  - "docs/design/f6-process-zerolibc-windows.md:33-45"
  - "DECISION_LOG.md:1050-1058"   # D126 folds journal pid/pid_alive into F6
  - "src/runtime/teko_rt.c:5026-5046"
---

# 0146 · F6-PID — pid / pid_alive in Teko, cross-platform

> The F6 residue D126 folded out of journal: `tk_rt_pid`/`tk_rt_pid_alive` become Teko `teko::process::current_pid`/`pid_alive` over syscall/ABI; reroute journal off both externs.

## Goal

Migrate the two process-ABI externs that live in `journal.tks` (D126: they are F6 process
concerns, not journal machinery) to a Teko implementation in `teko::process`: Linux via
`SYS_GETPID`/`SYS_KILL`, macOS via `getpid`/`kill from "System"`, Windows via
`GetCurrentProcessId` + `OpenProcess`/`WaitForSingleObject`. Journal's `pid_rt`/
`pid_alive_rt` externs are removed and rerouted to the new `exp` fns. **Expurgo:** kills
`tk_rt_pid`/`tk_rt_pid_alive` (no other caller → dead C, swept 0147). Emission changes →
reseed. Linux self-build peak unaffected (tiny leaf fns).

## Where

- `src/process/process.tks` — add `#os`-dispatched `os_pid()`/`os_pid_alive(pid)` +
  two `exp` wrappers `current_pid`/`pid_alive`.
- `src/journal/journal.tks:10,12` — delete `pid_rt`/`pid_alive_rt` externs; `:62` reroute
  `pid_rt()` → `teko::process::current_pid()`; reroute the `pid_alive_rt` caller →
  `teko::process::pid_alive`.
- consts/externs from 0143: linux `SYS_GETPID`/`SYS_KILL`; mac `os_getpid_raw`/
  `os_kill_raw`; win `GetCurrentProcessId`/`OpenProcess`/`SYNCHRONIZE`/`WAIT_TIMEOUT_RC`.

## How

1. **Linux** — `getpid` takes no args (`syscall0` or `syscall1` with a dummy); `kill(pid,
   0)` probes existence: syscall returns `0` alive, `-EPERM` also means alive (exists,
   not permitted), other negative = gone. Mirror `teko_rt.c:5043`:

```teko
#os("linux")
fn os_pid(): i64 {
    teko::sys::syscall1(teko::sys::SYS_GETPID, 0)
}

#os("linux")
fn os_pid_alive(pid: i64): bool {
    if pid <= 0 { return false }
    var r = teko::sys::syscall2(teko::sys::SYS_KILL, pid, 0)
    if r == 0 { return true }
    r == (0 - teko::sys::EPERM)
}
```

   (add `EPERM: i64 = 1` to `sys.tks` if absent — the errno for "exists, not permitted".)

2. **macOS** — via `System` externs (Apple bans raw syscalls; D101/R1 sanction libSystem
   as the ABI, same rule the mac fork/wait4 arm already follows):

```teko
#os("macos")
fn os_pid(): i64 {
    teko::sys::abi::os_getpid_raw() to i64
}

#os("macos")
fn os_pid_alive(pid: i64): bool {
    if pid <= 0 { return false }
    var r = teko::sys::abi::os_kill_raw(pid to i32, 0)
    if r == 0 { return true }
    teko::sys::abi::os_errno() == teko::sys::EPERM to i32
}
```

   (mac `kill` returns -1/errno, not -errno; reuse the errno reader the mac fs/io arm
   already uses — cite it at implement time; if none exists, treat any `r == 0` as alive
   and `r != 0` as gone, which matches the C's `kill()==0` primary path and only loses the
   rare EPERM-alive edge. Prefer the errno reader.)

3. **Windows** — `OpenProcess(SYNCHRONIZE)` then a zero-timeout `WaitForSingleObject`:
   `WAIT_TIMEOUT_RC` means still running, signaled means exited (mirror `teko_rt.c:5037`):

```teko
#os("windows")
fn os_pid(): i64 {
    teko::sys::abi::GetCurrentProcessId() to i64
}

#os("windows")
fn os_pid_alive(pid: i64): bool {
    if pid <= 0 { return false }
    var h = teko::sys::abi::OpenProcess(teko::sys::SYNCHRONIZE, 0, pid to u32)
    if h == 0 { return false }
    var w = teko::sys::abi::WaitForSingleObject(h, 0)
    _ = teko::sys::abi::CloseHandle(h)
    w == teko::sys::WAIT_TIMEOUT_RC
}
```

4. **exp surface** — the only two decls here that carry doc-comments:

```teko
/**
 * teko::process::current_pid — this process's own identifier.
 *
 * @return the OS process id (getpid on POSIX, GetCurrentProcessId on Windows).
 * @since 0.3.1
 */
exp fn current_pid(): i64 {
    os_pid()
}

/**
 * teko::process::pid_alive — whether a process with id `pid` currently exists.
 *
 * @param pid  the identifier to probe; a non-positive id is never alive.
 * @return true while the process exists (including a running-but-not-signalable one),
 *         false once it is gone.
 * @since 0.3.1
 */
exp fn pid_alive(pid: i64): bool {
    os_pid_alive(pid)
}
```

5. **Reroute journal** — delete `pid_rt`/`pid_alive_rt` externs; replace the call at
   `journal.tks:62` with `teko::process::current_pid()`; replace the `pid_alive_rt` caller
   with `teko::process::pid_alive`. (journal already imports/uses `teko::process` for its
   verdict channel? verify — if not, the fully-qualified path suffices, no import needed.)

## Rulings & laws

- **Teko-only / D90:** Teko rewrite; C untouched; dead `tk_rt_pid`/`tk_rt_pid_alive` swept
  in 0147.
- **D126:** pid/pid_alive are F6 process concerns folded out of journal — this crumb is
  their home.
- **D101/R1 (mac):** libSystem `getpid`/`kill` as ABI (Apple bans raw syscalls) — same rule
  as the landed mac fork/wait4 arm.
- **exp law:** `current_pid`/`pid_alive` are user-valuable → `exp` + doc; the `os_*`
  helpers are `pub`/private, no doc, zero `//`.
- **Fork protocol:** no fork — mirrors ratified C; the mac EPERM edge is a documented
  degrade with a stated fallback, not an owner decision.

## Fixtures

`none — the fixpoint self-build exercises this` for the Linux/mac paths IF the compiler or
its journal use touches pid at build time; the Windows path is covered by the Windows CI
leg. Per owner "sem testes", no new `.tkr` is authored (pid is a leaf the platform CI legs
exercise). If a reviewer finds pid is NOT exercised by any leg, add a single
`examples/regressions/process_pid/` returning `0` on `current_pid() > 0 &&
pid_alive(current_pid())` — decided at drain, not pre-authored.

## Gate

`[fixpoint]`: Linux `gen2==gen3` byte-identical; mac + Windows CI legs green. Reseed
`bootstrap/teko.c`.

## Deps

F6-WIN-A0

## Done when

`teko::process::current_pid`/`pid_alive` exist and dispatch per-OS, journal calls them
instead of `tk_rt_*`, both journal externs are gone, and the fixpoint reseeds
byte-identical.
