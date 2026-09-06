---
seq: 0062
crumb-id: RT-L4
milestone: M2
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [RT-L3, RT-ENTRY, RT-L4-ENV]
sources:
  - "DECISION_LOG.md:963-969"                                       # D99 — the law: exec becomes Teko execve(path,argv,envp_teko); env+exec close together; POSIX first
  - "DECISION_LOG.md:976-985"                                       # D101 — zero-libc = zero C-RUNTIME; per-OS sanctioned ABI (Linux raw / macOS libSystem / Windows kernel32); per-OS nm gate
  - "DECISION_LOG.md:944-951"                                       # D97 — the env↔exec coupling (propagation was the heart of the block)
  - "DECISION_LOG.md:857-863"                                       # D86 — fork #2 dissolved: use the platform's own linker/ABI; M-linker is endgame-native only
  - "DECISION_LOG.md:884-889"                                       # D90 — method: write Teko + reflect in codegen; NEVER edit teko_rt.c
  - "docs/design/plano-s16-expurgo-libc-completo.md:137-140"        # §1.6 #23 clone/execve/pipe2 (execvp = PATH-resolve in Teko), #24 wait4, #25 ppoll, #26 io-win
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:66"         # §1.2 FFI host process family
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L4 = process/pipes/redirect, fork/exec/CreateProcess + struct-by-value FFI
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:216-229"    # §3.3 Etapa B — process half of win32_compat dies with L4 + Fase E
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:279-281"    # §4.3 STARTUPINFOA/PROCESS_INFORMATION by value blocks L4-Win32
  - "docs/design/plano-s16-expurgo-libc-completo.md:240-241"       # §16-FASE6 process/exec/pipes — Win32 half BLOQUEADO on import-lib linker (Fase E)
---

# 0062 · RT-L4 — runtime C→Teko L4: process/pipes/redirect (fork/execve/CreateProcess) — GATE §16-FASE6

> Close the L4 layer: subprocess spawn/pipes/redirect run in Teko over fork/`execve` (POSIX) and
> CreateProcess (Win32) — passing the Teko-managed `envp` to children (the D99 propagation), killing the
> process half of `win32_compat.h`; the §16-FASE6 gate. **POSIX lands first; Win32 waits for Fase E.**

## D99 / D101 refresh (2026-08-25)

D97's fork (env↔exec) is resolved BY LAW (D99): exec is no longer a passive `execvp` — it becomes a Teko
`execve(path, argv, envp_teko)` that PROPAGATES the Teko-managed environment (`0124`) to children. **D101**
refines the reach: "zero-libc" = zero **C-RUNTIME**, NOT zero-OS-ABI, and the gate is per-OS. So the exec is
per-OS via the sanctioned ABI the platform LINKER resolves (native-compatible):

- **Linux** — raw: `clone`(SIGCHLD)/`execve`/`wait4`/`pipe2`/`dup3`/`ppoll` syscalls (no ABI lib exists).
- **macOS** — the sanctioned libSystem ABI via ld64: `fork`/`execve`/`posix_spawn`/`waitpid`/`pipe`/`poll`
  as `extern fn … from "System"` (D101 — these are the OS ABI, NOT the forbidden C-runtime `system`/`popen`).
- **Windows** — `CreateProcessW`/`WaitForSingleObject`/`CreatePipe` kernel32 via link.exe import-lib (Fase E).

This crumb's POSIX half CO-LANDS with `0125` (the per-OS entry) + `0124` (the env overlay + `env_snapshot`)
as RESEED C of the RT-L4 wave. Two consequences vs the original design below: (1) exec resolves PATH in Teko
(reading `PATH` via `0124`'s zero-C-runtime `env::var`) — no C-runtime `execvp`; (2) the child's environment
is `env_snapshot()` (built from the overlay BEFORE fork), so a parent `set_var` reaches the child. The per-OS
`nm` gate (D101): Linux no process libc; macOS libSystem ABI allowed, no C-runtime `system`/`popen`; Windows
kernel32, no msvcrt. The Win32 half stays blocked on the Fase E import-lib linker. The rest of this doc's
POSIX recipe stands, amended by the "POSIX exec via execve + envp_teko" section under How.

## Goal

L4 is the hardest OS layer: **process/pipes/redirect**, reaching `fork`/`execvp`/`waitpid` (POSIX) and
`CreateProcessA` (Win32), the latter passing STRUCTS BY VALUE in the ABI. The family only in `teko_rt.c`
(`migracao…` §1.2): `tk_rt_run`/`_run_quiet`/`_spawn_redirected`/`_wait_one`/`_pipe`/`_pipe_read_fd`/
`_pipe_write_fd`/`_close_fd`/`_fd_wait_readable`/`_fd_fill`/`_fd_take_byte` + the redirect helpers
(`teko_rt.c:2969-3363`). RT-L4 migrates these to Teko: the POSIX half over `clone`/`execve`/`wait4`/`ppoll`
leaf syscalls; the Win32 half filling `STARTUPINFOA` and reading `PROCESS_INFORMATION` — structs passed/returned
BY VALUE (`migracao…` §4.3). This **kills the process half of `win32_compat.h`** (`migracao…` §3.3 Etapa B),
and since it is the LAST consumer, the file itself dies here (its fs half was already orphaned at RT-L3).
Byte-preserving for existing programs (fixpoint guards existing-case residence; `migracao…` R8); a
`fixpoint-rebuild` swap, no teaching reseed.

**BLOCKED (design-ahead, honest) — the heaviest.** Behind (1) the **native fixpoint closing**, (2) its dep
**RT-L3**, (3) the **struct-by-value FFI ABI COMPLETE (reverse-FFI included)** — the recently-resolved
`star-ref…` §4 ABI, which the `STARTUPINFOA`/`PROCESS_INFORMATION` by-value block hard-depends on, and (4) the
**own linker resolving the Win32 import library** (`kernel32`) — Fase E / camada-3 territory (`migracao…` §3.3
Etapa B; `plano-s16-expurgo…`:240-241 marks §16-FASE6 BLOQUEADO on exactly these). The POSIX half can migrate
once the fixpoint + RT-L3 close; the Win32 half waits for Fase E. This doc designs both halves, the fixtures,
and the honest split; what stays blocked is the Win32 import-lib linker (reported to the backend wagon, not a
new issue).

## Where

- `src/runtime/teko_rt.c:2969-3363` — the process/pipes/redirect family (`tk_rt_run`/`_run_quiet`/
  `_spawn_redirected`/`_wait_one`/`_pipe`/`_pipe_read_fd`/`_pipe_write_fd`/`_close_fd`/`_fd_wait_readable`/
  `_fd_fill`/`_fd_take_byte` + `_open_redirect`/`_child_bind_all`/`_next_nul_token`/…) — MIGRATE the POSIX half
  to Teko over `clone`/`execve`/`wait4`/`ppoll`; the C bodies go DEAD (deleted at M3).
- `src/win32_compat.h:119-336` — the process half (`tk_win32_quote_arg`/`tk_win32_spawnvp`/
  `tk_win32_spawn_redirected` (CreateProcessA + STARTUPINFOA/PROCESS_INFORMATION)/`tk_win32_wait_one`/
  `tk_win32_redirect_handle*`/`tk_win32_build_envblock`) — its migration is BLOCKED on struct-by-value FFI +
  the Win32 import-lib linker; when it lands, `win32_compat.h` (both halves now gone) is DELETED (the file's
  death commit, `migracao…` §3.3).
- `src/runtime/teko_rt.tks` — home of the migrated process wrappers.
- NO new user-facing surface: `teko::process` names pre-exist; migration re-homes the body. Reuses the
  same target-guarded `extern fn` mechanism (RT-L3) for per-target symbol selection.

## How

1. **Migrate the POSIX half to Teko (over `execve` + envp_teko, D99).** `run`/`run_quiet`/
   `spawn_redirected`/`wait_one`/`pipe`/`fd_*` over the leaf syscalls `clone`/`execve`/`wait4`/`ppoll`/
   `pipe2`/`dup3`/`read`/`close`. Redirect helpers wire child fds; `fd_fill`/`fd_take_byte` stream a pipe.
   The four moving parts:
   - **fork = `SYS_clone(SIGCHLD, 0, 0, 0, 0)`** (SYS_CLONE already in `sys.tks:74/77`): returns 0 in the
     child, the pid in the parent — fork semantics with a raw syscall (neither arch has `SYS_fork` in the
     table; clone-with-SIGCHLD is the portable substitute).
   - **exec = `SYS_execve(path, argv_teko, envp_teko)`.** The C used libc `execvp` (PATH search); `execve`
     does NOT search PATH, so implement the search in Teko: if `argv[0]` contains `'/'`, `execve` it
     directly; else split `env::var("PATH")` on `':'` and `execve` `dir ~ "/" ~ argv[0]` for each dir until
     one does not return (a returning `execve` means failure → try next; all-fail → `exit_group(127)`).
     `envp_teko = teko::env::env_snapshot(child_region)` (`0124`) — built BEFORE fork so the shared address
     space carries it to the child; this is the D99 PROPAGATION.
   - **wait = `SYS_wait4(pid, &status, 0, 0)`**, then decode: `WIFEXITED` → `(status >> 8) & 0xff`;
     `WIFSIGNALED` (`(status & 0x7f) != 0`) → `128 + (status & 0x7f)`; else `SPAWN_FAILED` — replicating
     `tk_rt_wait_status_code` (`teko_rt.c:3732`) exactly (128+signo convention, the M.3 fix).
   - **pipes/redirect:** `pipe2(fds, O_CLOEXEC)` for `pipe()`; in the child, `dup3(target_fd, std_fd, 0)`
     onto stdin/stdout/stderr, then `chdir` (if `dir != ""`), then `execve`; `ppoll` for
     `fd_wait_readable`; `read`/`close` for `fd_fill`/`close_fd`.
   This half unblocks with the fixpoint + RT-L3 and CO-LANDS with `0124` (env overlay + `env_snapshot`).

   **New syscall consts to add to `sys.tks` (`#os("linux")` + `#arch` split):** `SYS_EXECVE`
   (x86_64 59 / arm64 221), `SYS_WAIT4` (61 / 260), `SYS_PIPE2` (293 / 59), `SYS_DUP3` (292 / 24),
   `SYS_PPOLL` (271 / 73), plus `SIGCHLD`(17), `O_CLOEXEC`, and an `extern type Pollfd` (`#os`, R3) for
   `ppoll`. `SYS_CHDIR`/`SYS_GETCWD`/`SYS_READ`/`SYS_CLOSE`/`SYS_OPENAT`/`SYS_CLONE` already present.

```teko
/**
 * posix_exec_resolved — `execve` `argv[0]` after resolving it against `PATH` (the Teko replacement
 * for libc `execvp`, which `execve` does not do). A `'/'` in `argv[0]` bypasses the search. `envp`
 * is the Teko-managed environment block (`env_snapshot`), so a parent `set_var` is inherited by the
 * child — the D99 propagation. Only ever runs in the child; on total failure it does not return
 * (the caller `exit_group(127)`s, the POSIX no-channel convention).
 * @param argv  the resolved argument vector (NUL-terminated `char**` address)
 * @param envp  the child environment block (`env_snapshot` result)
 * @since 0.3.1
 */
fn posix_exec_resolved(argv: u64, envp: u64)
```
2. **Design the Win32 half — blocked, but shaped.** `spawn_redirected` fills a `STARTUPINFOA` and reads a
   `PROCESS_INFORMATION`, both passed/returned BY VALUE — the heaviest struct-by-value case (`migracao…` §4.3).
   The Teko wrapper declares these as `extern type=struct` and passes them by value once the resolved ABI
   (`star-ref…` §4) + reverse-FFI are available; `CreateProcessA`/`DuplicateHandle`/`WaitForSingleObject`
   resolve through the own linker's Win32 import library (Fase E). Per-target symbol selection via the same
   target-guarded `extern fn` mechanism (RT-L3).
3. **The process half of `win32_compat.h` dies — and with it the file** (`migracao…` §3.3 Etapa B): it was the
   file's last consumer (fs half orphaned at RT-L3). The `#include "win32_compat.h"` stops and the file is
   deleted in the commit the Win32 half lands (a clean expurgo, coordinated with the M3 sweep `0096` if the
   Win32 half slips past M2 — reported, not forced).
4. **The link is the normal program link** (POSIX; `migracao…` §2.2): `clone`/`execve`/`wait4` resolve as
   undefined externals. The Win32 link needs the own import-lib linker (Fase E) — the honest dependency.
5. **Fixpoint byte-identity + per-target.** `gen2==gen3` byte-identical on POSIX proves subprocess callers did
   not shift; the Win32 half compiles once its deps land (`migracao…` §5 F5: POSIX own==C, Win32 compiled).

Reused (do NOT redeclare): the target-guarded `extern fn` mechanism (RT-L3), the resolved struct-by-value
FFI ABI (`star-ref…` §4), the `{ok,value,err}` result carriers, `region_alloc` (L1) for boxed output.

## Rulings & laws

- **Teko-only:** L4 wrappers land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` process C goes DEAD; `win32_compat.h` is DELETED when
  its last (process) consumer migrates — clean expurgo.
- **W15 full Javadoc** on every touched declaration; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** `win32_compat.h` deletion (both halves gone) is clean and
  tombstone-free — no residual `#define`, no compat stub.
- **No ABI invention (`migracao…` §4.3):** the Win32 struct-by-value block consumes the resolved backend ABI
  (`star-ref…` §4); the runtime does not classify sret/by-value itself.
- **Honest blocked split (`migracao…` R4, §8):** the fs half (RT-L3) needed only per-target selection; the
  process half needs struct-by-value FFI + the Win32 import-lib linker (Fase E). Do NOT force the Win32 half
  before those close — it is REPORTED to the backend wagon, not a new issue.
- **argc/argv (`migracao…` R5):** the empty native `args()` is a backend/linker (crt0) concern, NOT runtime-C —
  L4 does not resolve it; reported to the backend wagon.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the wrapper signatures land.

## Fixtures

subprocess is OS-touching and not self-build-exercised — full isolated oracles (own==C on POSIX; Win32
compile-only until Fase E; `migracao…` §5 F5):

| fixture | asserts | expected |
|---|---|---|
| `l4_run_echo` | spawn a child that prints a known string, capture stdout, bytes match — via migrated Teko `run` over `execve` | `0` |
| `l4_run_path_resolve` | `run(["true"])` (a PATH-resolved bare name) exits `0` — the Teko `execvp`-replacement PATH search works | `0` |
| `l4_pipe_stream` | write N bytes through a pipe, read them back in order via `fd_fill`/`fd_take_byte` (`pipe2`/`read`) | `0` |
| `l4_wait_exit_code` | a child exiting `7` is reported as exit `7`; a SIGKILL-ed child as `128+9` — the `wait4` decode | `0` |
| `l4_env_propagate` | parent `set_var("TK_CHILD","5")`, `run` a teko child that reads `TK_CHILD` and exits it — the new var reaches the child via `envp_teko` (the D99 propagation; shared with `0124`) | `5` |
| `l4_exec_zero_libc_nm` | build a program using `process::run`; `nm -u` shows NO `fork`/`execvp`/`execve`/`waitpid`/`wait4`/`pipe`/`dup2` libc undefined (harness) | `0` |
| `l4_win32_process_compiles` | the Win32 process wrappers COMPILE once struct-by-value + import-lib linker are present (compile-only leg) | `0` |

## Gate

`[RITUAL]` — the POSIX exec/propagation is the THIRD reseed of the RT-L4 wave (after `0124`'s RESEED A
capture-teach and RESEED B env-overlay): build gen2 + the scoped fixtures + `gen2==gen3` byte-identity on
POSIX + MEM_PARANOID exit 0, PLUS the Win32 process leg compiling once its deps land. "Green" = the POSIX
process/pipes/redirect run in Teko over `execve` (own==C), a parent `set_var` propagates to children,
`nm -u` shows zero process libc, the process half of `win32_compat.h` is orphaned (deleted when the Win32
half also migrates, coordinated with the M3 sweep `0096` if it slips past M2), and the emitted `teko.c`
converges. This is the **§16-FASE6 (POSIX) gate**. **Reseed-class:** `fixpoint-rebuild`.

## Deps

`RT-L3` (`0061` — the target-guarded `extern fn` mechanism the process half reuses), `RT-ENTRY` (`0125` —
the per-OS entry that captures argc/argv/envp; RESEED A of the wave) and `RT-L4-ENV` (`0124` — the env
overlay + `env_snapshot` the `execve` propagation consumes; RESEED B). This crumb is RESEED C, co-landing.

## Done when

process/pipes/redirect run in Teko over fork/`execve` (POSIX, own==C) passing the Teko-managed `envp_teko`
to children (a parent `set_var` is inherited), the PATH resolver replaces libc `execvp`, `nm -u` shows no
process libc, the POSIX fixtures exit as tabled, the Win32 half is shaped against the resolved struct-by-
value ABI + own import-lib linker (compiles once Fase E lands), and a `[RITUAL]` build is `gen2==gen3`
byte-identical with MEM_PARANOID exit 0.
