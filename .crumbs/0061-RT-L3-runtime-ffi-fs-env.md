---
seq: 0061
crumb-id: RT-L3
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:65,67-68"   # §1.2 FFI host fs/env + time/date families
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L3 = FFI host fs/env + time/date, POSIX/Win32 leaf syscalls
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:203-214"    # §3.3 Etapa A — the fs half of win32_compat dies with L3
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:271-278"    # §4.3 struct-by-value FFI (SRES/URES) the host results cross
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:336-353"    # §6 TargetSymbol — per-target symbol selection
---

# 0061 · RT-L3 — runtime C→Teko L3: FFI host fs/env + time/date (POSIX/Win32 leaf syscalls)

> Close the L3 layer: file-system, environment, and time/date reach the host via leaf `extern fn` syscalls
> per-target — killing the fs half of `win32_compat.h`.

> **ENV SUPERSEDED (D99, 2026-08-25):** this crumb's env recipe (`_getenv`/`_setenv` via per-target `extern
> fn getenv/setenv`, D-TS1) ACCEPTED libc and is REJECTED by D85/D99. Env now migrates via the entry-point
> `envp` capture + a program-resident Teko overlay in **`0124` (RT-L4-ENV)**, co-landing with `0062`'s
> `execve`. Ignore the `getenv`/`setenv` items below; take the fs (`chdir`/`getcwd`/`opendir`) and time/date
> items only. `cwd`/`chdir` land with `0124` under D99.

## Goal

L3 is the first OS-touching layer: **fs/env + time/date**, reached by leaf syscalls (`migracao…` §2.1). The
families still only in `teko_rt.c` (`migracao…` §1.2): fs/env — `tk_rt_read_file`/`_read_line`/`_read_stdin`/
`_stdin_eof`/`_getenv`/`_setenv`/`_write_file`(`_bytes`)/`_append_file`/`_chdir`/`_mkdir`/`_remove_file`/
`_getcwd`/`_list_dir`/`_last_index_of`/`tk_sort_names` (`teko_rt.c:2665-2960`); and time/date — `tk_wall_now_ns`/
`tk_jdn_to_ymd`/`tk_rt_date_*`/`_wall_days`/`_wall_ns_of_day`/`_wall_offset_minutes`/`_monotonic_ns`
(`teko_rt.c:4053-4149`). The syscall grounding for these — `open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/
`getenv`/`getrandom`/`getrusage` — landed in **S16-FS** (FASE4, `0055`). RT-L3 migrates the runtime WRAPPERS to
Teko `extern fn` that select the host symbol PER TARGET (POSIX `chdir`/`getcwd`/`opendir` vs Win32 `_chdir`/
`_getcwd`/`FindFirstFileA`) via the `TargetSymbol` surface (`migracao…` §6), and lifts their `{ok,value,err}`
results across the FFI boundary as struct-by-value (`migracao…` §4.3). This **kills the fs half of
`win32_compat.h`** (`migracao…` §3.3 Etapa A): once the wrappers are per-target `extern`, the `#define chdir
_chdir` shims are orphaned. Byte-preserving for existing programs (fixpoint guards existing-case residence;
`migracao…` R8); a `fixpoint-rebuild` swap, no teaching reseed.

**RESOLVED / RATIFIED (owner 2026-08-19, D-TS1).** The per-target `extern`-symbol-selection surface is
**NOT a new `TargetSymbol` struct** (withdrawn, D-TS1) — it IS the **existing target-guarded `extern fn`**
mechanism (`#os("linux"|"macos"|"windows")` / `#arch(…)` guards, pruned by `prune_cc` before the checker,
using NAT-XL native target) that `src/io/file_stream.tks` already ships. The `TargetSymbol` struct proposal
is a **phantom** — zero real-code consumers in `src/**` — and is WITHDRAWN from 0061, 0062, and the source
docs (see `docs/design/target-symbol-extern-selection-0.3.1.md` for the full ratification). This doc designs
the wrappers using the existing guard-on-extern surface; the fs half is now **UNBLOCKED with zero compiler
change** (only the S16-FS dep and native-fixpoint remain).

## Where

- `src/runtime/teko_rt.c:2665-2960` — the fs/env family (`tk_rt_read_file`/…/`tk_sort_names`) — MIGRATE each to
  a Teko wrapper over the S16-FS syscall grounding, selecting the host symbol per target; C bodies go DEAD.
- `src/runtime/teko_rt.c:4053-4149` — the time/date family — MIGRATE `monotonic_ns`/`wall_now_ns` over
  `clock_gettime` (S16-FS), `jdn_to_ymd`/`civil_from_days`/offset as pure-Teko calendar arithmetic + a
  `localtime` leaf for the offset.
- `src/win32_compat.h:42-100` — the fs half (`chdir→_chdir`, `mkdir→_mkdir`, `getcwd→_getcwd`, `setenv→
  _putenv_s`, the dirent shim over `FindFirstFileA`/`FindNextFileA`) — goes ORPHANED once the wrappers are
  per-target `extern`; the file is NOT deleted here (it dies with its LAST consumer, the process half, at
  `0062` RT-L4 / M3).
- `src/runtime/teko_rt.tks` — home of the migrated fs/env/time wrappers.
- Per-target symbol selection via target-guarded `extern fn` (existing mechanism, no new declarative surface).
- NO new user-facing fs/env/time surface: `teko::fs`/`teko::env`/`teko::time` names pre-exist; migration
  re-homes the body.

## How

1. **Migrate the fs/env wrappers to Teko over S16-FS.** `read_file`/`write_file`/`getenv`/`getcwd`/`chdir`/
   `mkdir`/`list_dir` call the grounded `open`/`stat`/`getdents`/`getenv` syscalls; results lift as struct-by-
   value `{ok,value,err}` honoring the resolved sret/register-pair classification (`migracao…` §4.3,
   `c-types-and-marshalling…` §5) — the runtime does not invent ABI, it consumes the backend's.
2. **Per-target symbol selection — target-guarded `extern fn`** (not `TargetSymbol` struct, D-TS1).
   An `extern fn` guarded by `#os("linux"|"macos"|"windows")` / `#arch(…)` resolves the target-specific
   symbol at lowering by the build target (e.g., `chdir` on POSIX, `_chdir` on Windows). Precedent: `src/io/file_stream.tks` already uses this mechanism for `os_open`/`os_read`/`os_write`/`os_close`. No new
   declarative surface needed — the existing guards + `prune_cc` pruning (before the checker) closes the
   selection. This removes the dependency on a new `TargetSymbol` struct.

3. **Migrate time/date.** `monotonic_ns`/`wall_now_ns` over `clock_gettime` (S16-FS); `civil_from_days`/
   `jdn_to_ymd` as pure-Teko calendar arithmetic (leap-year correct — the S11 `localtime_civil` fixture);
   `wall_offset_minutes` via a `localtime` leaf.
4. **The fs half of `win32_compat.h` is orphaned** (`migracao…` §3.3 Etapa A): with the wrappers per-target
   `extern`, the `#define chdir _chdir` shims have no caller. The file survives only for its process-half
   consumer (L4).
5. **The link is the normal program link** (`migracao…` §2.2): the leaf syscalls resolve as undefined externals
   (`open`/`stat`/`getenv`/`clock_gettime`), like any libc symbol.
6. **Fixpoint byte-identity + per-target build.** `gen2==gen3` byte-identical on the host target proves fs/env/
   time callers did not shift; the Win32 leg must COMPILE (per-target: POSIX green, Win32 compiles) —
   `migracao…` §5 F4.

Reused (do NOT redeclare): the SRES/URES/SLRES result carriers (`{ok,value,err}`/`{ok,ptr,len}`, lifted as
struct-by-value), the S16-FS syscall grounding, `region_alloc` (L1) for boxed result strings.

## Rulings & laws

- **Teko-only:** L3 wrappers land in `src/runtime/teko_rt.tks`; the maintained-C exception is the BRIDGE the
  campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` fs/env/time C goes DEAD; deletion is `0095` RM-C9 (M3).
- **W15 full Javadoc** on every touched declaration (guarded `extern fn` / each member); flatten/extract;
  no inline `//`.
- **Removals = clean expurgo, NO tombstone:** the fs half of `win32_compat.h` is ORPHANED here, not deleted;
  the file dies with its last consumer (process half, L4/M3). Clean, tombstone-free.
- **No ABI invention (`migracao…` §4.3):** the runtime consumes the backend's resolved struct-by-value ABI for
  `{ok,value,err}`; it does not classify sret itself.
- **Honest per-target (`migracao…` §5 F4):** the fs half must compile on Win32; a POSIX-only migration that
  leaves Win32 uncompilable is not done — the target-guarded `extern fn` mechanism is the solution.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the wrapper signatures land.

## Fixtures

fs/env/time are OS-touching and not fully self-build-exercised — full isolated oracles (own==C during the
transition; `migracao…` §5 F4):

| fixture | asserts | expected |
|---|---|---|
| `l3_read_write_roundtrip` | write a file, read it back, bytes equal — via the migrated Teko wrappers, no `tk_rt_read_file` C symbol | `0` |
| `l3_list_dir_contains` | `list_dir("/")` (or a temp dir) contains a known entry; per-target symbol selected | `0` |
| `l3_env_roundtrip` | `set("TK","42")`; `get("TK")` parses to `42` | `42` |
| `l3_time_civil_leap` | `civil_from_days(0) == (1970,1,1)` and a known leap day decode correctly | `0` |
| `l3_win32_compiles` | the fs wrappers COMPILE for the Win32 target via `TargetSymbol` (compile-only leg) | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity, PLUS the per-target compile of
the Win32 fs leg. "Green" = fs/env/time run in Teko over the S16-FS syscalls, results lift as struct-by-value,
the fs half of `win32_compat.h` is orphaned, the Win32 leg compiles, and the emitted `teko.c` is byte-identical
to before the swap. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`S16-FS` (`0055` — FASE4 fs+env+time+random: the `open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/`getenv`/
`getrandom`/`getrusage` syscall grounding L3's wrappers stand on).

## Done when

fs/env + time/date run in Teko as per-target `extern` wrappers over the S16-FS syscalls with `{ok,value,err}`
struct-by-value results, the fs half of `win32_compat.h` is orphaned, the Win32 fs leg compiles, the fixtures
exit `0`, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
