---
seq: 0055
crumb-id: S16-FS
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-MM-L2]
sources:
  - "docs/design/plano-s16-expurgo-libc-completo.md:120-131" # §1.5 fs/env/time/random table
  - "docs/design/plano-s16-expurgo-libc-completo.md:235-236" # §3 FASE4
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:304-321" # §5 F4 roadmap
  - "docs/design/plano-s16-expurgo-libc-completo.md:373-393" # §7 fixtures S7-S14
  - "docs/design/plano-s16-expurgo-libc-completo.md:326-339" # §5 R3/R4 (struct layout per-OS, per-target symbol)
---

# 0055 · S16-FS — FASE4 fs + env + time + random (`open`/`stat`/`mkdir`/`getdents`/`clock`/`localtime`/`getenv`/`getrandom`/`getrusage`)

> Migrate the host FFI leaf — file open/stat/mkdir/directory-list, clock + civil-calendar `localtime`,
> env over `environ`, `getrandom`, `getrusage` — from `teko_rt.c` to raw syscalls (Linux) / `extern fn`
> (macOS libSystem, Windows kernel32), killing the fs-half of `win32_compat.h`.

## Goal

FASE4 migrates the fs + env + time + random subsystem onto the syscall intrinsic / per-target `extern
fn`, the layer that (with the arena, S16-MM-L2, now done) mata the fs-half of `win32_compat.h`. It
covers: `open`/`openat` (+ flags, IO-1), `stat`/`mkdir` (per-`#os` `extern type` structs whose layout
DIVERGES Darwin×Linux), directory listing via `SYS_getdents64` (buffer + parse in Teko), `chdir`/
`getcwd`/`dup2`, `clock_gettime` (already landed) + `localtime` as a PURE-Teko civil-calendar algorithm
(Howard Hinnant), env read/write over `environ` (owner: env is memory), `getrandom` (already landed), and
`getrusage` (peak RSS). Byte-mover for the emitted `teko.c`? YES — the migrated callers emit
`tk_syscallN(...)` / per-target `extern` calls instead of the `tk_rt_*` FFI → a real emit delta →
`fixpoint-rebuild` reseed. R3/R4 rulings apply: struct layout is per-`#os` (`extern type`), and
per-target symbol binding uses the existing pragmas + `.tkp` (NOT a new declaration form).

## Where

- `src/fs/fs.tks` — `list_dir` (`:3`) and `mkdir` (`:7`) migrate off `extern … from "teko_rt"` to
  `SYS_getdents64` / `SYS_mkdirat` (Linux) + per-`#os` `extern fn` (macOS/Windows); `stat`/`file_size`
  (`:31`/`:41`) already stream-sized (IO-4) — the `is_dir` path moves to `SYS_statx`/`fstat`.
- `src/sys/sys.tks` — the fs syscall numbers (`SYS_OPENAT`/`SYS_STATX`/`SYS_GETDENTS64`/`SYS_MKDIRAT`/
  `SYS_CHDIR`/`SYS_GETCWD`/`SYS_DUP2`, `#arch`-split) + `O_*`/`AT_FDCWD` (IO-1) + per-`#os` `extern
  type` struct layouts (`Stat`, `linux_dirent64`, `Rusage`, `Timespec`).
- `src/runtime/teko_rt.tks` — `getenv`/`setenv` over `environ` (pure Teko), `localtime` civil-calendar,
  `getrusage` peak RSS, `getrandom` (landed) — the leaf callers reroute.

## How

1. **fs syscalls** (`§1.5` #15-#18): `open`/`openat` over `SYS_OPENAT(AT_FDCWD, …)`, `mkdir` over
   `SYS_mkdirat`, directory listing over `SYS_getdents64` (a buffer + parse the `linux_dirent64` records
   in Teko), `chdir`/`getcwd`/`dup2` over their syscalls. Each is a per-`#os` fn: Linux syscall, macOS
   `extern fn … from "System"`, Windows `extern fn … from "kernel32"` (CreateFile/GetFileAttributes/
   FindFirstFile via the `.tkp` import-lib).

```teko
/**
 * list_dir — list the entries of `path`. Linux: `SYS_getdents64` into a Teko buffer, parsing the
 * `linux_dirent64` records by field offset (an `extern type = struct` per-`#os`, `plano-s16-expurgo`
 * §1.5 #18, R3 layout). macOS: `getdirentries`/`readdir` libSystem; Windows: `FindFirstFile`/
 * `FindNextFile` kernel32 — the per-target binding via the existing pragmas + `.tkp` (R4), not a new
 * declaration form. Replaces the `tk_rt_list_dir` FFI, killing the fs-half of `win32_compat.h`.
 * @param path  the directory to list
 * @return      the entry names, or an error
 * @throws      when the OS open/getdents fails (`-errno`)
 * @since §16
 */
exp fn list_dir(path: str): []str | error
```

2. **`stat`/`mkdir` structs per-`#os`** (R3): `Stat`/`linux_dirent64` are `extern type = struct` guarded
   by `#os` (Darwin `stat` ≠ Linux `stat`), materialized in the monolith cc-emit via `#if`; the address
   crosses via `ref_word(local)`. A fixture pins each layout (S9 via a real stat).
3. **`localtime` — pure-Teko civil calendar** (`§1.5` #21, `§2` NEW): implement `civil_from_days` /
   `localtime` as the Howard Hinnant algorithm over integers — no libc `localtime_r`. `clock_gettime`
   (landed) feeds it.

```teko
/**
 * civil_from_days — convert a day count since the Unix epoch to `(year, month, day)` by the Howard
 * Hinnant civil-calendar algorithm, PURE Teko integer arithmetic — no libc `localtime_r`
 * (`plano-s16-expurgo §1.5` #21). Correct across the proleptic Gregorian range incl. leap years.
 * @param z  days since 1970-01-01 (may be negative for pre-epoch)
 * @return   the civil date as `(year, month, day)`
 * @since §16
 */
pub fn civil_from_days(z: i64): (i64, i64, i64)
```

4. **env over `environ`** (`§1.5` #22, owner: env is memory): `getenv`/`setenv` in pure Teko over the
   `environ` block (or `extern fn getenv/setenv` on targets where `environ` is not directly reachable —
   Windows `GetEnvironmentVariable`).
5. **`getrusage` peak RSS** (`§1.5` #20): `SYS_getrusage` + `extern type Rusage` (per-`#os`), a small
   leaf (`teko::mem::peak_rss`).
6. **Fixpoint.** The migrated callers emit syscalls / per-target externs → `gen1 ≠ gen0`; converge
   `gen2 == gen3`. Per-target: POSIX green (runs), Win32 compiles (the import-lib binding via `.tkp`).
   A struct-layout error (R3) corrupts stat results — the layout fixtures (S9) are the detectors.

## Rulings & laws

- **Teko-only:** all callers + `extern type` structs in `.tks` over syscalls / per-target `extern fn`;
  `teko_rt.c` fs symbols DELETED later (S16-SWEEP / RM-C9), not here.
- **W15 full Javadoc** on every migrated fn, `extern type` struct, and the civil-calendar helper; no `//`.
- **Owner "no shortcuts" (`§5`):** every fs/env/time/random fn is a REAL syscall/FFI implementation, no
  no-op, no degrade; `localtime` is a real civil-calendar algorithm.
- **R3 struct layout per-`#os`:** `stat`/`dirent`/`Rusage`/`Timespec` get dedicated `extern type`
  layouts per `#os` (and possibly per `#arch`), materialized via `#if`; a fixture pins each.
- **R4 per-target symbol binding:** use the EXISTING pragmas + `.tkp` (which carry the FFI helpers) — do
  NOT invent a new declaration form; each `extern fn` that diverges per target redeclares under `#os`.
- **Reported, not actioned:** the process-half of `win32_compat.h` (fork/exec/CreateProcess) is FASE6
  (blocked on struct-by-value FFI + import-lib linker) — REPORTED up, not this crumb.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each subsystem
  (fs, env, time, random) as its own green step; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical;
  sweep `.tkt`/`.tkr` after the emit change.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `sys_openat_read` | create `/tmp/tk_fx` (openat+write), reopen, read, compare bytes | 0 |
| `sys_getdents` | list `/` via getdents64, assert it contains `usr` | 0 |
| `sys_stat_size` | stat a 42-byte file, assert `st_size == 42` (`extern type Stat` per-`#os`) | 0 |
| `localtime_civil` | `civil_from_days(0) == (1970,1,1)` and a known leap day | 0 |
| `sys_getrusage_rss` | `peak_rss() > 0` after allocating 8 MB | 0 |
| `env_environ_teko` | `set("TK","42")`; `get("TK")` parsed → i32 | 42 |

These fs/env/time paths are NOT self-build-exercised as ASSERTIONS (the compiler opens/reads files but
does not `getdents`/`stat`-size/`localtime`/`getrusage` on itself in a way that pins the syscall
correctness), and S9/S11 are the NEW highest-risk oracles (per-OS struct layout / civil-calendar) —
isolated regressions under `examples/regressions/` (mirroring `src/sys/`) required, per `§7`.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + the six fixtures + `gen2==gen3` byte-identity. This is a RITUAL point (`§8`
ritual 3 after F4): POSIX green, Win32 compiles (import-lib via `.tkp`), the fs-half of `win32_compat.h`
is orphaned. "Green" = the fs/env/time/random callers run over syscalls / per-target externs, the
struct-layout fixtures pin each OS, and the ladder converges. Reseed-class: `fixpoint-rebuild`.

## Deps

`S16-MM-L2`.

## Done when

`open`/`stat`/`mkdir`/`getdents`/`chdir`/`getcwd`/`dup2`, `clock`+pure-Teko `localtime`, env over
`environ`, `getrandom`, and `getrusage` run over syscalls / per-target `extern fn` (no `tk_rt_*` fs FFI),
the six fixtures pass, `gen2==gen3` holds, and the fs-half of `win32_compat.h` is orphaned.
