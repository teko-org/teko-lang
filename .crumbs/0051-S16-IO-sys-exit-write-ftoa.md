---
seq: 0051
crumb-id: S16-IO
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-EMIT]
sources:
  - "docs/design/plano-s16-expurgo-libc-completo.md:106-112" # §1.3 I/O + exit (the 11 live symbols)
  - "docs/design/plano-s16-expurgo-libc-completo.md:229-230" # §3 FASE2
  - "docs/design/plano-s16-syscall-intrinsic.md:284-315"     # §4 SYS_write/SYS_exit_group numbers
  - "docs/design/plano-s16-syscall-intrinsic.md:404-431"     # §7 ordered crumbs (exit→write)
  - "docs/design/plano-s16-expurgo-libc-completo.md:104-104" # §1.2 float-bottom %.17g
---

# 0051 · S16-IO — FASE2 `SYS_exit_group` + `SYS_write` + float-bottom ftoa/`%.17g` pure-Teko

> Route process exit through `SYS_exit_group` and stdout/stderr through `SYS_write` (no libc `exit`/
> `fwrite`/`fputs`/`snprintf`), and realize the float-bottom `ftoa`/`%.17g` in pure Teko — closing the
> I/O + exit + float-format leg of the emitted image with zero libc `<stdio.h>`/`<stdlib.h>`-exit.

## Goal

FASE2 migrates the I/O + exit path (`teko_rt`'s `write`/`print`/`exit`, `<stdio.h>` `fwrite`/`fputc`/
`fputs`/`snprintf`, `<stdlib.h>` `exit`/`_Exit`/`abort`) onto the landed raw-syscall intrinsic: process
exit = `SYS_exit_group` (terminates ALL threads, the correct process-exit primitive), stdout/stderr =
`SYS_write` (fd 1/2, NO buffering — direct write). The syscall intrinsic (`syscall0..6`, `ptr_word`) and
the `SYS_WRITE`/`SYS_EXIT_GROUP` numbers are already landed (`sys.tks:14-23`); this crumb consumes them
in the runtime callers. The remaining piece is the FLOAT BOTTOM: `snprintf("%.17g")` is the only
`<stdio.h>` use with real formatting, and the `ftoa`/`fmt_g`/`f64_g17` twins in `teko_rt.tks` currently
defer to it — this crumb realizes the precise `%.17g→str` in pure Teko (the shortest-round-trip float
formatter), retiring the variadic `snprintf`/`vsnprintf` and its `<stdarg.h>`. Byte-mover for the
emitted `teko.c`? YES — the runtime callers emit `tk_syscallN(...)` instead of the libc calls, and the
float path emits the Teko ftoa → a real emit delta → `fixpoint-rebuild` reseed.

## Where

- `src/runtime/teko_rt.tks` — the io/panic/exit callers (`write`/`print`/`ewrite`/`exit`/`panic_str`)
  reroute to `SYS_write`/`SYS_exit_group` via the syscall intrinsic; the float twins
  (`ftoa`/`fmt_g`/`f64_g17`) realize the pure-Teko `%.17g` instead of deferring to the host.
- `src/sys/sys.tks:14-23` — `SYS_EXIT_GROUP`/`SYS_WRITE` (already present) — consumed, not added.
- `src/io/io.tks:39-…` — the `print`/`write`/`ewrite` externs may become Teko bodies over `SYS_write`
  (the 11-live-symbol path, `plano-s16-expurgo §1.3`); the `str`→(ptr,len) seam passes the address as a
  `u64` (`ptr_word(s.ptr)` + `s.len`).

## How

1. **Exit via `SYS_exit_group`** (`syscall-intrinsic §5`, the FIRST-proof shape): the exit path lowers
   to `teko::sys::syscall1(SYS_EXIT_GROUP, code)`, which terminates the process at exactly `code`,
   bypassing libc `exit`. The `sys_exit_group` fixture (already landed) is the keystone proof.
2. **stdout/stderr via `SYS_write`** (`§7` crumb 2): `write(fd, buf, count)` lowers to
   `teko::sys::syscall3(SYS_WRITE, fd, ptr_word(<buf base>), count)`, fd 1 for stdout / 2 for stderr, NO
   buffering (write direct). This is the first heavy-traffic `ptr_word` user.

```teko
/**
 * rt_write — write `s` to fd `fd` (1=stdout, 2=stderr) via the raw `SYS_write` syscall, no libc
 * `fwrite`/buffering (FASE2, `plano-s16-expurgo §1.3`). The `str` fat-pointer is decomposed at the seam:
 * the base address crosses as `teko::sys::ptr_word(s.ptr)` (the sanctioned reinterpret intrinsic) and
 * the length as `s.len`. A short write loops on the remainder; a negative return (`-errno`) is the
 * error.
 * @param fd  the file descriptor (1 stdout, 2 stderr)
 * @param s   the text to write
 * @return    void (errors surfaced by the caller's own path)
 * @since §16
 */
fn rt_write(fd: i64, s: str)
```

3. **Float bottom — pure-Teko `%.17g`** (`§1.2`/`§1.3` item 11, the NEW piece): implement the precise
   shortest-round-trip `f64 → str` (`ftoa`/`f64_g17`) in Teko over `[]byte`, feeding the existing
   `fmt_g`/`fmt_dyn_f64` twins so they stop deferring to the host `snprintf("%.17g")`. This retires the
   variadic `snprintf`/`vsnprintf` + `<stdarg.h>` (the fmt Teko is monomorphized-by-arity, non-variadic).

```teko
/**
 * f64_g17 — format `x` as the shortest decimal string that round-trips to the same `f64` (the `%.17g`
 * contract), in PURE Teko over `[]byte` — no libc `snprintf`/`<stdio.h>`/`<stdarg.h>` (FASE2 float
 * bottom, `plano-s16-expurgo §1.2`). Handles the sign, the exponent form, and the 17-significant-digit
 * ceiling; NaN/±Inf spelled explicitly. Allocates its result in the current scope region (residency).
 * @param x  the double to format
 * @return   the shortest round-tripping decimal string
 * @since §16
 */
pub fn f64_g17(x: f64): str
```

4. **The `str`→(ptr,len) seam.** The migrated io path passes addresses as `u64` (`ptr_word` + `.len`),
   the marshalling discipline already in flight — no new `extern` that takes a `str` fat-pointer.
5. **Fixpoint.** The runtime callers emit `tk_syscallN(...)` + the Teko ftoa instead of libc → `gen1 ≠
   gen0`; converge `gen2 == gen3`. The float format MUST match libc `%.17g` bit-exactly for every double
   (round-trip identity) — a divergence corrupts every emitted float literal, HALT and fix.

## Rulings & laws

- **Teko-only:** runtime callers + float bottom in `.tks` over the syscall intrinsic; C twins frozen
  (deleted at S16-SWEEP, not here).
- **W15 full Javadoc** on `rt_write`/`f64_g17` and every migrated caller; flatten; no inline `//`.
- **Owner "no shortcuts" (`§5`):** the float `%.17g` gets a REAL pure-Teko implementation, not a
  degraded formatter; exit/write are real syscalls, not stubs.
- **Errno raw (`syscall-intrinsic §8`):** the syscall returns raw `i64` (`< 0` = `-errno`); the caller
  checks the sign — a forgotten check treats `-errno` as a huge length.
- **`"memory"` clobber correctness:** the `SYS_write` buffer store must be ordered before the syscall —
  guaranteed by the landed helper's `"memory"` clobber; the stdout-content fixture catches a regression.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit each migrated
  caller as its own green step; reseed ONLY at the fixpoint; `gen2==gen3` byte-identical; sweep
  `.tkt`/`.tkr` after the emit change.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `sys_exit_group` | `syscall1(SYS_EXIT_GROUP, 42)` sets the process exit code (the landed keystone) | 42 |
| `sys_write_stdout` | `syscall3(SYS_WRITE, 1, ptr_word(as_cstr("hi\n")), 3)` writes `hi` to stdout | 0 |
| `f64_g17_roundtrip` | `f64_g17(x)` parsed back equals `x` for a battery of doubles (incl. subnormals, 0.1, 1e308) | 0 |

The exit/write paths and the float round-trip are NOT self-build-exercised as ASSERTIONS (the compiler
prints little during a build and does not verify `%.17g` on itself) — isolated oracles under
`examples/regressions/` required; `sys_exit_group`/`sys_write_stdout` are the §7 provers, `f64_g17` is
the FASE2 float-bottom oracle.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + the three fixtures + `gen2==gen3` byte-identity. "Green" = process exit is
`SYS_exit_group`, stdout/stderr are `SYS_write` (no libc buffering), `f64_g17` round-trips every double,
the emitted `teko.c` loses the `<stdio.h>` I/O + `exit` calls, and the ladder converges. Reseed-class:
`fixpoint-rebuild`.

## Deps

`S16-EMIT`.

## Done when

Exit routes through `SYS_exit_group`, stdout/stderr through `SYS_write`, the float bottom `%.17g` is
pure-Teko (`snprintf`/`<stdarg.h>` retired from the path), the three fixtures pass, and `gen2==gen3` holds.
