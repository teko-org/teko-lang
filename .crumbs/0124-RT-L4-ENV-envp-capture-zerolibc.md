---
seq: 0124
crumb-id: RT-L4-ENV
milestone: M2
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS, RT-ENTRY]  # 0055 fs/time half + 0125 the `_start` that captures argc/argv/envp from the OS ABI
sources:
  - "DECISION_LOG.md:963-969"                                       # D99 — the law: build the infra, env+exec close zero-libc together, no libc crutch
  - "DECISION_LOG.md:944-951"                                       # D97 — the env↔exec coupling that D99 resolves
  - "DECISION_LOG.md:924-933"                                       # D95 — env deferred (bootstrap barrier: environ_addr → 2 reseeds)
  - "DECISION_LOG.md:850-856"                                       # D85 — zero-libc criterion (link with NO libc; `environ`/getenv/setenv libc forbidden)
  - "DECISION_LOG.md:884-889"                                       # D90 — method: write Teko + reflect in codegen; NEVER edit teko_rt.c
  - "docs/design/plano-s16-expurgo-libc-completo.md:131"           # §1.5 #22 env (supersedes the "puro sobre environ" recipe under D85/D99)
  - "docs/design/plano-s16-expurgo-libc-completo.md:235-236"       # §3 FASE4 env row
---

# 0124 · RT-L4-ENV — Teko-managed `environ` overlay + `var`/`set_var`/`cwd`/`chdir` (zero C-runtime)

> Build a program-resident, Teko-managed environment fed by `0125`'s per-OS entry capture (Linux stack /
> macOS `_NSGetEnviron` / Windows `GetEnvironmentStringsW`), and serve `var`/`set_var`/`unset`/`cwd`/`chdir`
> over it with zero C-runtime — the READ+WRITE half of the D99 co-land whose exec/propagation half is `0062`.

## Goal

`env` (`var`/`set_var`/`cwd`/`chdir`) does NOT close zero-libc alone: it is coupled to exec (D97). D99 rules
the tension by LAW — build the infra so env and exec close TOGETHER, no `getenv`/`setenv`-of-state C-runtime
crutch. The `envp` SOURCE was redesigned (owner, then D101): capture is NOT a `main` third-param (a glibc-crt
crutch that dies at native) — it is `0125`'s per-OS entry (Linux reads the initial stack; macOS reads the
sanctioned libSystem ABI `_NSGetEnviron`; Windows reads kernel32 `GetEnvironmentStringsW`). This crumb owns
what sits ON TOP of that capture: (1) `capture_envp(envp)` — the sink `0125` calls with the per-OS block
address; (2) a program-resident, Teko-managed environment (a growable array in `region_program()`) that
survives and grows on `set_var`; (3) `var`/`set_var`/`unset` over that overlay, zero C-runtime; (4)
`cwd`/`chdir` over `SYS_getcwd`/`SYS_chdir` (Linux) / the sanctioned ABI (macOS/Windows). The exec side that
consumes this overlay to PROPAGATE a new var to children is `0062` and co-lands in the same wave.
**Byte-mover** (via `0125`'s entry change) → `fixpoint-rebuild`; this is RESEED B of the wave (`0125` is A).

This crumb **supersedes** the stale env recipes: `0055`/`plano §1.5 #22` "puro Teko sobre `environ`" (needs
the C-runtime `environ` symbol) and `0061`/D-TS1 "env via `extern fn getenv/setenv`" (the C-runtime `getenv`
of-state, D101-forbidden). The mechanism is the per-OS entry capture (`0125`) + a Teko overlay, per D99/D101.

## Where

- `src/env/env.tks:7-18` — `var`/`cwd`/`set_var`/`chdir` — drop the `extern … from "teko_rt"` bindings;
  re-home the bodies onto the Teko overlay (`var`/`set_var`/`unset`) and, for `cwd`/`chdir`, `SYS_getcwd`/
  `SYS_chdir` (Linux) + per-`#os` sanctioned ABI (macOS libSystem, Windows `GetCurrentDirectoryA`/
  `SetCurrentDirectoryA` kernel32). `args`/`version`/`nproc` move to `0125` (args) / stay.
- `src/env/env.tks` (NEW private surface) — `capture_envp` (the `0125` sink), the overlay (`env_ensure_init`,
  `env_find`, `env_get`, `env_put`, `env_remove`), the raw-block scanner (per-OS: NUL-pointer-terminated
  `char**` on POSIX; double-NUL `KEY=VALUE\0…\0\0` block on Windows), `var_cstr` (the cov shim `0125`'s exit
  finalizer reads), and `env_snapshot` (the `char**` builder `0062`'s `execve` consumes).
- `src/sys/sys.tks:268-278` — `SYS_CHDIR`/`SYS_GETCWD` already present (x86_64/arm64); macOS/Windows chdir/
  cwd via per-`#os` `extern fn` (sanctioned ABI, D101).
- `src/runtime/arena.tks` — reuse the arena control block as the anchor: add a `CTRL_ENVIRON` slot (Teko-
  managed, NO teko_rt.c edit, D90) holding the overlay header pointer. `region_program()` (`arena.tks:774`)
  is the program-resident allocator for the overlay.

NOTE: the emitted-entry / `main`-shape / `tk_set_args` / cov-`getenv` changes are ALL in `0125` (RT-ENTRY);
this crumb does NOT touch codegen entry emission — it consumes `0125`'s `capture_envp` sink.

## How

The environment is captured ONCE at process start (by `0125`'s per-OS entry) and served from a program-
resident Teko structure. No C-runtime `environ`/`getenv`/`setenv`. The overlay is the single source of truth
after first touch, so a new var is coherent for both `var` (read-back) and `0062`'s `execve` (propagation) —
dissolving D97's "the captured block goes stale after setenv" incoherence: we NEVER mutate the OS block; we
copy-on-first-touch into a growable Teko array.

### 1. `capture_envp` — the sink `0125`'s per-OS entry calls

`0125` captures the environment block per-OS (Linux: decoded from the initial stack; macOS: `*_NSGetEnviron()`;
Windows: `GetEnvironmentStringsW`) and hands its ADDRESS here as a `u64`. This crumb only stashes it; the
overlay materialises lazily on first `var`/`set_var`. On Windows the block is the double-NUL `KEY=VALUE`
format, so `env_ensure_init` takes a per-`#os` decode path (walk `char**` on POSIX; split the double-NUL
block on Windows).

```teko
/**
 * capture_envp — stash the process environment block `0125`'s per-OS entry captured (its ADDRESS as a
 * u64): a NUL-pointer-terminated `char**` on POSIX, a double-NUL `KEY=VALUE\0…\0\0` block on Windows.
 * Called ONCE at process start, before any `var`/`set_var`. Stores the address in the arena control
 * block (`CTRL_ENVIRON` anchor); the overlay is materialised lazily. NOT the C-runtime `environ`
 * symbol — the sanctioned per-OS source (D101).
 *
 * @param envp  the address of the `char**` environment block (0 when the host gave none)
 * @since 0.3.1
 */
pub fn capture_envp(envp: u64)
```

### 2. The program-resident overlay (grows on `set_var`)

A tiny header in `region_program()` — `{ len: u64, cap: u64, data: u64 }` where `data` points to a
`region_program()` vector of `str` (each owned `"KEY=VALUE"`). The header pointer lives in the arena
control block (`CTRL_ENVIRON`), so it survives the whole program and is reachable from any leaf, zero-libc,
with NO teko_rt.c anchor (same pattern as `tk_arena_control_get/set`, but the slot is inside the already-
existing control block — a pure-Teko `ar_ctrl_set/get`, D90-clean).

- **Lazy init (`env_ensure_init`, per-`#os` decode):** on first `var`/`set_var`/`env_snapshot`, if the
  overlay is empty, decode the raw captured block into `region_program()`-owned `str` entries — POSIX: walk
  the `char**` until a NULL pointer, copying each C string; Windows: split the double-NUL `KEY=VALUE\0…\0\0`
  block. After this, the overlay is authoritative; the OS block is never read or mutated again.
- **`env_find(name)`:** linear scan for an entry whose bytes before `'='` equal `name`; returns the index
  or a not-found sentinel. (Env is small; linear is correct and simple.)
- **`env_get(name)`:** `env_find` → slice the value after `'='`.
- **`env_put(name, value)`:** `env_find` → replace the entry's `str` in place, else append `"name=value"`
  (grows the vector in `region_program()`; on capacity exhaustion, allocate a larger vector and copy — the
  program-resident grow).
- **`env_remove(name)`:** `env_find` → compact the entry out.

```teko
/**
 * env_ensure_init — materialise the Teko environment overlay from the captured raw `envp` the first
 * time the environment is touched. Idempotent: a non-empty overlay is left untouched. Each variable
 * is copied into `region_program()` as an owned `"KEY=VALUE"` string, so the overlay survives and
 * grows for the whole program without ever realloc-ing the kernel block (dissolving the D97 stale-
 * envp incoherence).
 * @since 0.3.1
 */
fn env_ensure_init()

/**
 * env_put — set `name` to `value` in the program-resident overlay, replacing any existing entry or
 * appending a new one; the overlay grows in `region_program()`. The new value is visible to a later
 * `var` AND to `0062`'s `execve` (which builds the child block from this overlay) — the propagation
 * D97 could not close over libc.
 * @param name   the variable name (no `'='`)
 * @param value  the value to bind
 * @since 0.3.1
 */
fn env_put(name: str, value: str)
```

### 3. Re-home the `exp` env API onto the overlay (zero-libc)

```teko
/**
 * var — the value of the environment variable `name`, from the program-resident Teko overlay
 * (captured `envp` on POSIX; `GetEnvironmentStringsA` on Windows). Zero libc — no `getenv`.
 * @param name  the variable to look up
 * @return      the value, or an error when unset
 * @throws      when `name` is not present in the environment
 * @since 0.3.1
 */
exp fn var(name: str): str | error

/**
 * set_var — bind `name` to `value` in the program-resident overlay, visible to later `var` and,
 * via `0062`'s `execve`, inherited by children. Zero libc — no `setenv`, no `putenv`.
 * @param name   the variable to set
 * @param value  the value to bind
 * @return       null on success
 * @throws       never on POSIX (the overlay always accepts); reserved for the Windows write path
 * @since 0.3.1
 */
exp fn set_var(name: str, value: str): error | null

/**
 * unset — remove `name` from the program-resident overlay (so children do not inherit it).
 * @param name  the variable to remove
 * @since 0.3.1
 */
exp fn unset(name: str)
```

### 4. `cwd`/`chdir` per-OS (zero C-runtime)

```teko
/**
 * cwd — the current working directory as an owned absolute path, over `SYS_getcwd` (Linux raw) /
 * the sanctioned OS ABI (macOS libSystem `getcwd`, Windows `GetCurrentDirectoryA` kernel32 — D101).
 * Zero C-runtime.
 * @return  the absolute path, or an error
 * @throws  when the host getcwd failed (`-errno`)
 * @since 0.3.1
 */
exp fn cwd(): str | error

/**
 * chdir — change the process working directory to `path`, over `SYS_chdir` (Linux raw) / the
 * sanctioned OS ABI (macOS libSystem `chdir`, Windows `SetCurrentDirectoryA` kernel32 — D101). The
 * child of a later fork inherits the new cwd (no propagation plumbing — cwd is process state fork
 * copies). Zero C-runtime.
 * @param path  the directory to move to
 * @return      null on success
 * @throws      when the host chdir failed (`-errno`)
 * @since 0.3.1
 */
exp fn chdir(path: str): error | null
```

### 5. `env_snapshot` — the `char**` the exec half consumes

`0062`'s POSIX `execve` needs a NUL-terminated `char**` built from the overlay. Expose the builder here (it
lives next to the overlay it reads) so `0062` calls it with no cross-crumb reach into overlay internals.

```teko
/**
 * env_snapshot — build a NUL-pointer-terminated `char**` (each entry a NUL-terminated `"KEY=VALUE"`)
 * from the program-resident overlay, allocated in the given region, for `execve`'s third argument.
 * This is the PROPAGATION vehicle: a `set_var` before a spawn reaches the child because the child's
 * block is built from the same overlay (D99). Build BEFORE fork so the shared address space carries
 * it to the child.
 * @param region  the region to allocate the vector + strings in (the caller's spawn scratch)
 * @return        the address of the `char**` block (0-terminated)
 * @since 0.3.1
 */
pub fn env_snapshot(region: ptr): u64
```

### 6. Windows env (per-`#os`, lands with the Win32 exec leg — blocked on Fase E)

`0125`'s Windows entry captures the environment from `GetEnvironmentStringsW` (kernel32 — the sanctioned OS
ABI resolved by link.exe's import lib, D101, NOT the msvcrt C-runtime) and hands it to `capture_envp`; the
overlay model is identical (the Windows decode path splits the double-NUL block, §2). `env_snapshot` emits
the Windows double-NUL environment block for `CreateProcess`. Shaped here, LANDS with `0062`'s Win32 leg
(Fase E import-lib linker) — POSIX first.

## Rulings & laws

- **D99 (owner 2026-08-25) — the law, not a decision:** build the infra; env+exec close together;
  `environ` managed in Teko, program-resident, grows on `set_var`; no C-runtime `environ`/`getenv`-of-state.
  This crumb is the env overlay half; `0125` is the entry/capture; `0062` is the exec half.
- **D101 (owner 2026-08-25) — zero-libc = zero C-RUNTIME, per-OS `nm` gate:** the env source is the
  sanctioned per-OS ABI (`0125`): Linux stack, macOS `_NSGetEnviron` (libSystem, allowed), Windows
  `GetEnvironmentStringsW` (kernel32, allowed). Proof is `nm -u` PER-OS: Linux NO `getenv`/`setenv`/
  `putenv`/`environ`/`chdir`/`getcwd` libc undefined; macOS libSystem ABI allowed but NO C-runtime
  `getenv`/`setenv`; Windows kernel32 allowed, NO msvcrt. Grep of `#include` sumido is necessary, not
  sufficient.
- **D90 (method):** the bodies go to `.tks`; codegen (in `0125`) reflects the entry. `teko_rt.c`
  `tk_rt_getenv`/`setenv`/`getcwd`/`chdir` go DEAD (unused), deleted at F9 SWEEP — NEVER patched. The
  `CTRL_ENVIRON` anchor is a pure-Teko control-block slot, not a new teko_rt.c static.
- **Bootstrap teach→use (the D95 barrier, now viable via `0125`):** the entry capture must be TAUGHT
  (seeded, `0125` RESEED A) before `var` may READ from it — else the compiler's own `gen1` (whose entry was
  emitted by the pre-change seed, so has NO capture) would read an uninitialised overlay and fail. This
  crumb is RESEED B. The per-OS-ABI capture removes the NEW-INTRINSIC obstacle D95 hit (no `environ_addr`
  intrinsic), but the teach→use ordering remains.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//`.
- **args() migrates in `0125`, not here:** `0125`'s per-OS capture also seeds `args()` off `tk_set_args`;
  this crumb owns only the env overlay.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4718592` (the measured real
  ceiling; a blown guard is a root-cause fix, never a raised ceiling); commit each green step; reseed ONLY
  at the [RITUAL] points below; fixpoint `gen2==gen3` byte-identical; MEM_PARANOID exit 0; RSS ratchet
  (this ADD is inherent C→Teko migration cost per the D95 pattern — flag the delta, reclaimed at F9); NO
  `Co-Authored-By` trailer; sweep `.tkt`/`.tkr` after the signature change.

## Fixtures

Env/cwd are OS-touching and not pinned as ASSERTIONS by the self-build — isolated `.tkr` oracles required
(the propagation fixture is the heart of what D97 blocked):

| fixture | asserts | expected |
|---|---|---|
| `env_capture_read` | a program reads a var the launcher exported (via `0125`'s captured envp, zero-C-runtime `var`) and parses it → i32 | `42` |
| `env_set_readback` | `set_var("TK","7")` then `var("TK")` parses → i32 (overlay round-trip, no child) | `7` |
| `env_unset` | `set_var("TK","1")`, `unset("TK")`, `var("TK")` is unset → exit path proves error | `0` |
| `env_propagate_child` | parent `set_var("TK_CHILD","5")` then `process::run` a child that reads `TK_CHILD` and exits it — the NEW var reaches the child by inheritance (co-lands with `0062`) | `5` |
| `cwd_chdir_roundtrip` | `chdir("/tmp")`, `cwd()` ends with `tmp` (Linux `SYS_chdir`/`SYS_getcwd`; sanctioned ABI on mac/win) | `0` |
| `env_nm_per_os` | build a program using `var`/`set_var`/`cwd`; per-OS `nm -u`: Linux no env libc; macOS libSystem-only no C-runtime `getenv`/`setenv`; Windows kernel32-only no msvcrt (harness, D101) | `0` |

`env_propagate_child` depends on `0062`'s POSIX `execve`; it lands in the same wave (co-land). The others
land with this crumb's overlay + capture.

## Gate

`[RITUAL]` — this crumb is **RESEED B** of the RT-L4 wave (`0125` is RESEED A, the teach). The teach→use
split lives across the two crumbs:

- **RESEED A = `0125`:** the per-OS entry captures + stashes argc/argv/envp, with `var`/`set_var` STILL on
  the old bindings. After that reseed, the seed emits the capturing entry.
- **RESEED B (this crumb):** switch `var`/`set_var`/`unset`/`cwd`/`chdir` onto the overlay + syscalls / the
  per-OS ABI. The seed now emits the capturing entry, so `gen1` captures its own env (`TEKO_BACKEND`/
  `TEKO_CC`/…) over the overlay → self-compile succeeds. Reseed `bootstrap/teko.c = gen2`. `gen2==gen3`,
  MEM_PARANOID 0, per-OS `nm` clean for env.

The overlay+write (steps 1–3) and cwd/chdir (step 4) fold into this one commit (they share the post-A seed,
no further teach→use). The exec/propagation half (`0062`) is RESEED C in the wave. "Green" = env reads/writes
over the Teko overlay, cwd/chdir per-OS, per-OS `nm` shows no env C-runtime, and `gen2==gen3`.
**Reseed-class:** `fixpoint-rebuild`.

## Deps

`S16-FS` (`0055` — the fs/time/rusage half; env was its deferred straggler, D95) + `RT-ENTRY` (`0125` — the
per-OS entry that captures the env block and calls `capture_envp`). Co-lands with `0062` (RT-L4 process/exec)
for `env_propagate_child`.

## Done when

`0125`'s per-OS entry hands the env block to `capture_envp`, `var`/`set_var`/`unset` serve a program-resident
Teko overlay that grows on `set_var`, `cwd`/`chdir` run over `SYS_getcwd`/`SYS_chdir` (Linux) / the sanctioned
ABI (mac/win), `env_snapshot` hands `0062` the child block, per-OS `nm` shows no env C-runtime symbol, the six
fixtures pass (the propagation one with `0062`), and the RESEED B build is `gen2==gen3` byte-identical with
MEM_PARANOID exit 0.
