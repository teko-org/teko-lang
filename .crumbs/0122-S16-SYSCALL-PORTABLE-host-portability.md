---
seq: 0122
crumb-id: S16-SYSCALL-PORTABLE
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C17]
sources:
  - "origin/feat/s16-syscall-portable"                               # recovery branch with #if __linux__ guards + portable stubs
  - "src/codegen/codegen.tks"                                        # cg_emit_syscall_helper_text_stub function
---

# 0122 · S16-SYSCALL-PORTABLE — gate syscall C helpers to `__linux__` + portable stub fallback

> Wrap raw-syscall C helpers emitted by the codegen with `#if defined(__linux__)` guards so the
> generated `teko.c` compiles on any host (Linux, macOS, Windows). For non-Linux targets, emit a
> portable `-ENOSYS` stub return value (`-38L` in C) instead of `#error`. Drain the undrained work on
> `origin/feat/s16-syscall-portable` (gates `cg_emit_syscall_helper_text_stub`, adds per-target stubs).
>
> **Scope:** C-ROUTE **HOST PORTABILITY**, NOT the parked native backend. This crumb gates the emitted C
> artifact for cross-platform C compilation, not the native backend work (which is deferred).
> **Recovery:** Work is undrained; source branch is `origin/feat/s16-syscall-portable`.

## Goal

`teko.c` is a single translation unit emitted for all targets via `#if` conditionals (no per-target
variations in the emitted file). Raw-syscall C helpers (e.g., `sys_read`, `sys_write`, FFI to
`syscall(SYS_read, ...)`) are **Linux-specific** — they don't compile on macOS (different syscall
ABI) or Windows (no syscall ABI at all). This crumb:

1. Gates the syscall-helper emission to `#if defined(__linux__)` — the helpers ONLY appear in the C
   for Linux targets.
2. For non-Linux hosts, emit a **portable stub** that returns `-ENOSYS` (error code 38, "function not
   supported") instead of `#error` — the C still compiles everywhere, and the runtime surfaces the
   error to Teko code as `error`.

This is **C-route host portability** (the compiled `teko.c` runs on any host), NOT the deferred native
backend work.

## Where

- `src/codegen/codegen.tks` — function `cg_emit_syscall_helper_text_stub` (add or modify):
  emit syscall helpers wrapped in `#if defined(__linux__) … #else <return -38L> #endif`.
  When codegen routes a syscall emission, check the target; if non-Linux, route to the stub.

## How

1. **Syscall emission gate** — when codegen sees a raw-syscall operation (e.g., `sys_read` for file I/O
   in M3 stream rewrites), route to `cg_emit_syscall_helper_text_stub(syscall_name, …)`.
2. **Linux path** — emit the real syscall helper:
   ```c
   #if defined(__linux__)
   long sys_read(int fd, void *buf, size_t count) {
     return syscall(SYS_read, fd, buf, count);
   }
   #else
   long sys_read(int fd, void *buf, size_t count) { return -38L; }  // ENOSYS
   #endif
   ```
3. **Non-Linux path** — the `#else` branch returns `-38L` (ENOSYS error code), allowing Teko code to
   handle the error (wrapped in error::join logic if needed, crumb 0121).
4. **Codegen routing** — determine the target at emission time; if not Linux, don't emit the real helper,
   only the stub. The Teko code calling the helper gets the error back.

```teko
/**
 * cg_emit_syscall_helper_text_stub — emit a portable syscall C helper, guarded for Linux and stubbed
 * with -ENOSYS on other platforms. The emitted C compiles everywhere; non-Linux gets the error.
 *
 * @param fn_name      the helper function name (e.g., "sys_read")
 * @param syscall_nr   the Linux SYS_* constant (e.g., SYS_read)
 * @param signature    the C function signature (return type + params)
 * @return             the C code as a string (ready to insert into teko.c)
 * @since 0.3.1
 */
exp fn cg_emit_syscall_helper_text_stub(fn_name: str, syscall_nr: str, signature: str): str
```

## Rulings & laws

- **C-only:** emission in `src/codegen/codegen.tks`; the product is C text.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Scope:** C-route host portability (the emitted `teko.c` compiles anywhere). NOT the native backend
  (which is deferred until memory stabilizes, M4).
- **Portability requirement:** the emitted `teko.c` MUST compile on Linux, macOS, and Windows with a
  standard C compiler (no platform-specific headers outside `#if` guards).
- **Error signaling:** syscall failure on non-Linux returns `ENOSYS` (-38); Teko code catches and
  converts via error factories (0121) or retry logic.
- **Fork protocol (owner 2026-08-19):** syscall C helpers are ratified as temporary (§16 transition).
  No fork here — just gate the emission.
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; test C compilation on
  macOS/Windows (CI validates via multi-platform build); `[fixpoint]` `gen2==gen3`.
- Rests on: the C-route § §16 I/O streaming (crumb scope). Native backend is NOT touched.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `syscall_portable_linux` | syscall helpers are emitted in full on Linux target | `0` |
| `syscall_portable_non_linux` | syscall helpers return -ENOSYS on non-Linux platforms | `0` |
| `teko_c_compiles_everywhere` | the emitted `teko.c` compiles on Linux, macOS, Windows (CI gate) | `0` |

## Gate

`[fixpoint]` — `gen2==gen3` byte-identity (C emission change; the guarded syscall stubs must stabilize).
"Green" = syscall helpers are `#if __linux__` guarded, non-Linux stubs return `-38L`, the emitted
`teko.c` compiles on all platforms (validated by CI multi-platform lane), and the rebuild is
byte-identical. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C17` (`0107`, the final C emission). Native backend (M4) is independent and deferred.

## Done when

All raw-syscall helpers emitted by codegen are guarded with `#if defined(__linux__)`, non-Linux
targets get `-ENOSYS` stubs, the `teko.c` compiles on Linux/macOS/Windows, and the fixpoint is
byte-identical.
