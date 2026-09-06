---
seq: 0125
crumb-id: RT-ENTRY
milestone: M2
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [S16-FS]        # needs the syscall grounding (0055) + rt_exit (0051); is the foundation 0124/0062 build on
sources:
  - "DECISION_LOG.md:976-985"                                       # D101 — zero-libc = zero C-RUNTIME, not zero-OS-ABI; per-OS sanctioned ABI resolved by the LINKER
  - "DECISION_LOG.md:963-969"                                       # D99 — antecipar a infra; capture argc/argv/envp from the OS
  - "DECISION_LOG.md:850-856"                                       # D85 — the C must link with no libc C-runtime; the .o-direct native north (refined by D101)
  - "DECISION_LOG.md:857-863"                                       # D86 — each platform's OWN linker + OS ABI (kernel32/libSystem), not the C-runtime
  - "DECISION_LOG.md:884-889"                                       # D90 — write Teko + reflect in codegen; the arena-control seam has two incarnations
  - "docs/design/plano-s16-expurgo-libc-completo.md:838"           # D83 scout: atexit → explicit finalizer; exit via syscall
---

# 0125 · RT-ENTRY — process entry per-OS: argc/argv/envp from the sanctioned OS ABI (linker-resolved)

> Own the process entry WITHOUT the C-runtime, per-OS via the sanctioned OS ABI the platform LINKER resolves
> (native-compatible): Linux = our own `_start` reading the initial stack + raw syscalls; macOS = libSystem
> `_NSGetArgv`/`_NSGetEnviron` via ld64; Windows = `GetCommandLineW`/`GetEnvironmentStringsW` via kernel32.
> Feeds the Teko args/env overlays, runs vmain, exits. The foundation `0124` (env) + `0062` (exec) rest on.
> **Owner-directed 2026-08-25 (D99), refined by D101.**

## Goal

The `main(argc,argv,envp)`-param capture was rejected on LINUX (there the entry is `_start` and the third
param is a glibc-crt crutch that dies at native). But D101 draws the missing line: **"zero-libc" = zero
C-RUNTIME** (`malloc`/`printf`/`free`/`getenv`-of-state/`snprintf`), **NOT zero-OS-ABI**. What the platform
LINKER injects because the OS mandates it (raw `syscall` on Linux, `libSystem` on macOS, `kernel32`/`ntdll`
on Windows) is SANCTIONED and native-compatible — the platform linker runs at native too; only the C
COMPILER (gcc/cc/clang) leaves. So the entry is NOT one uniform `_start`; it is, per-OS, the sanctioned-ABI
mechanism that is (a) resolved by the platform linker (not the cc), and (b) not the C-runtime:

- **Linux** — raw is the ONLY way (no ABI lib exists; the ABI is the `syscall` instruction + the loader).
  Own `_start`, read the initial stack, raw syscalls, `-nostartfiles`.
- **macOS** — libSystem is MANDATORY (Apple forbids static; syscalls only via libSystem). Keep the
  libSystem/dyld entry; pull argc/argv/envp via `_NSGetArgc`/`_NSGetArgv`/`_NSGetEnviron` (`extern`→ld64) —
  the sanctioned ABI, native-compatible, NOT the forbidden C-runtime. No own stack-read, no manual TLS.
- **Windows** — kernel32/ntdll is the sanctioned ABI. Own PE entry (no msvcrt); pull argv from
  `GetCommandLineW`/`CommandLineToArgvW`, env from `GetEnvironmentStringsW` (`extern`→link.exe import-lib).
  No manual TLS (the PE loader does it).

The common invariant is **"linker-resolved + no C-runtime", not "same entry code"**. This replaces both the
C-route emitted `main`+`tk_set_args` (codegen) and the native `native_entry_stub` `main` (`lower.tks:6717`),
which today still leans on the system crt0. **Byte-mover** for every emitted program → `fixpoint-rebuild`
reseed; teach→use entry split (Gate). Also migrates `args()` off `tk_set_args`/libc (folded into the one
mechanism, per the owner's "explore deep").

## Where

- `src/codegen/codegen.tks:9762-9859` — `emit_program_main_body`/`_cov`/`emit_test_main`/`_analyze` — emit
  the vmain body as `int teko_vmain(void)` and a **per-`#os` entry**: Linux a naked `_start` (inline
  `__asm__`, the syscall-stub technique at `codegen.tks:9280-9311`); macOS keep a `main`-shaped entry that
  reads `_NSGet*`; Windows a `/entry:` stub reading kernel32. Each tail-calls the shared Teko handler.
- `src/lir/lower.tks:6684-6730` — `wrap_native_entry`/`native_entry_stub` — per-target: Linux `_start`
  LFunc (first inst = `stack_ptr` intrinsic §5); macOS/Windows an entry LFunc that calls the ABI externs.
  `NATIVE_ENTRY_VMAIN_SYMBOL` → `teko_vmain`.
- `src/backend/isel_x86_64.tks`/`isel_arm64.tks`/`minst*.tks` — lower `stack_ptr` to `mov reg,%rsp` /
  `mov reg,sp` as `_start`'s frameless first inst (Linux native only).
- `src/backend/objfile_elf.tks`/`objfile_macho.tks`/`objfile_coff.tks` — entry symbol: ELF default `_start`;
  Mach-O LC_MAIN entry; PE `AddressOfEntryPoint`/`/entry:`.
- `src/build/project.tks:614-657` (`build_cc_argv`) — Linux C route: add `-nostartfiles`; keep `-lc`
  transitionally (dead C), drop at F9. macOS: NO `-nostartfiles` (libSystem entry stays), keep `-lSystem`.
  Windows: `/entry:` + keep kernel32, drop the msvcrt default-lib.
- `src/build/project.tks:1008-1097` (native link) — ELF: DROP `Scrt1.o`/`crti.o`/`crtn.o`+`-lc`, `_start`
  is default entry. Mach-O: DROP `crt1.o`, keep `-lSystem`, LC_MAIN entry. PE: DROP the CRT default-lib,
  `/entry:`, keep kernel32.
- `src/env/env.tks` — `capture_args`/`capture_envp` fed per-OS; `args()` re-homed to captured argv.
- `src/sys/sys.tks` — Linux TLS: `SYS_ARCH_PRCTL` (x86_64 158) + `ARCH_SET_FS` (0x1002); arm64 `msr
  TPIDR_EL0` (instruction, no syscall). macOS externs: `_NSGetArgc`/`_NSGetArgv`/`_NSGetEnviron` from
  "System". Windows externs: `GetCommandLineW`/`CommandLineToArgvW`/`GetEnvironmentStringsW` from kernel32.
- `src/runtime/teko_rt.c:2019,1088,259,1860` — the `_Thread_local` anchors + `constructor` + atexit — NOT
  edited (D90); on Linux their init MOVES into `_start` (§4); macOS/Windows get it from dyld/the PE loader.

## How

### 1. The shared Teko tail (all three OSes converge here)

```teko
/**
 * start_run — the portable tail every per-OS entry converges on: seed the args + env overlays from
 * the captured (argc, argv, envp), run the renamed vmain, run the explicit exit finalizers (the
 * atexit replacement — coverage dump, arena flush), and terminate via `rt_exit`. The per-OS PROLOGUE
 * (how argc/argv/envp were captured, and any TLS/init the platform did not do) differs; this tail is
 * identical. On Linux it is reached from the raw `_start`; on macOS from the libSystem entry; on
 * Windows from the PE `/entry:` stub.
 *
 * @param argc  the argument count
 * @param argv  the `char**` argument vector address (NUL-pointer-terminated)
 * @param envp  the `char**` environment block address (NUL-pointer-terminated)
 * @since 0.3.1
 */
fn start_run(argc: i64, argv: u64, envp: u64)
```

Body: `capture_args(argc, argv)` + `capture_envp(envp)` (`0124`) → `var status = teko_vmain()` →
`run_exit_finalizers()` → `rt_exit(status)`. (On macOS, returning from vmain to dyld is also valid; `rt_exit`
is used uniformly so the three routes match.)

### 2. Linux (x86-64 + arm64) — own `_start`, raw stack, raw syscalls (raw IS the way)

No ABI lib exists on Linux; the sanctioned ABI is the `syscall` instruction + the loader. At `_start` the
kernel has placed `[argc][argv..][NULL][envp..][NULL][auxv..][AT_NULL]` at `%rsp`/`sp`. The naked stub
captures the SP and hands it to a Teko decoder, then `start_run`:

```c
/* C route, Linux x86-64 — emitted verbatim (the syscall-stub __asm__ technique) */
__attribute__((naked, used)) void _start(void) {
    __asm__ volatile(
        "xor %rbp, %rbp\n\t"
        "mov %rsp, %rdi\n\t"
        "and $-16, %rsp\n\t"
        "call teko_teko__env__start_linux\n\t"
    );
}
```

```teko
/**
 * start_linux — the Linux raw entry: bootstrap TLS (§4), walk `.init_array` (§4), decode the initial
 * stack `sp` (`argc = load_u64(sp)`, `argv = sp+8`, `envp` after argv's NULL), then `start_run`.
 * NEVER returns. On native this same logic is emitted as machine code (the `stack_ptr` intrinsic §5
 * replaces the naked `__asm__`).
 * @param sp  the initial stack pointer captured at `_start` (points at `argc`)
 * @since 0.3.1
 */
fn start_linux(sp: u64)
```

### 3. macOS (arm64) + Windows (x86-64) — the sanctioned OS ABI, no raw stack read

**macOS:** libSystem is mandatory and native-compatible (ld64 links it into the native `.o` too), so DO NOT
invent a stack-reading `_start`. Keep the libSystem/dyld entry (which already did TLS + image init) and pull
the process vectors from the libSystem ABI — sanctioned, not the C-runtime:

```teko
/**
 * ns_argc / ns_argv / ns_environ — the process argc/argv/environ from libSystem's crt-glue ABI
 * (`_NSGetArgc`/`_NSGetArgv`/`_NSGetEnviron` return the ADDRESS of the respective global). This is
 * the sanctioned macOS ABI resolved by ld64 (D101), native-compatible — NOT the forbidden C-runtime
 * `getenv`/`environ`-of-state. `capture_envp` snapshots `*ns_environ()` once into the Teko overlay.
 * @return  the argc value / the `char**` argv / the `char**` environ (dereferenced from the ABI ptr)
 * @since 0.3.1
 */
#os("macos") extern fn _NSGetEnviron(): u64 = "_NSGetEnviron" from "System"
#os("macos") extern fn _NSGetArgv(): u64 = "_NSGetArgv" from "System"
#os("macos") extern fn _NSGetArgc(): u64 = "_NSGetArgc" from "System"
```

`start_macos` = read `argc = load_u64(load_u64(ns_argc_addr))`, `argv = load_u64(ns_argv_addr)`, `envp =
load_u64(ns_environ_addr)`, then `start_run`. No `tls_bootstrap`, no `run_init_array` (dyld did them).

**Windows:** the PE loader (no msvcrt) gives no argc/argv/envp; gather them from kernel32 (sanctioned ABI via
the import-lib → link.exe / lld-link, D86/D101). The loader initialised the TEB + `.tls`, so `_Thread_local`
works with no manual bootstrap.

```teko
/**
 * start_windows — the PE `/entry:` with no msvcrt. Gathers argv from `GetCommandLineW` +
 * `CommandLineToArgvW` and the environment from `GetEnvironmentStringsW` (kernel32 — the sanctioned
 * OS ABI, D101, resolved by link.exe's import lib), decodes the double-NUL environment block into the
 * overlay, then `start_run`; the tail ends at `rt_exit` (`ExitProcess`).
 * @since 0.3.1
 */
#os("windows") fn start_windows()
```

The Windows entry LANDS with `0062`'s Win32 exec leg (both need the Fase E import-lib linker) — POSIX first.

### 4. What crt0 did that WE must do — LINUX ONLY (macOS=dyld, Windows=PE loader)

Because only Linux drops the startup files, the crt0 services move into `_start` on Linux alone:

- **TLS bootstrap** — `tk_g_arena_control`/`tk_g_current_task` are `_Thread_local` (`teko_rt.c:2019,1088`),
  the heart of allocation. Under `-nostartfiles` nothing sets `%fs`/`TPIDR_EL0`, so `_start` installs a
  minimal main-thread TCB (allocate the block sized to the module TLS image, init the TCB self-pointer,
  `arch_prctl(ARCH_SET_FS, tcb)` on x86-64 / `msr TPIDR_EL0` on arm64). REQUIRED for native Linux too. macOS
  (dyld) / Windows (PE loader) do it — nothing to add there.

```teko
/**
 * tls_bootstrap — install the main thread's TLS block so `_Thread_local` loads resolve without a crt
 * (LINUX ONLY; macOS/Windows have it from dyld / the PE loader). Sizes the block to the module TLS
 * image, sets the TCB self-pointer, and programs `%fs` (`SYS_arch_prctl`) / `TPIDR_EL0` (`msr`). The
 * F7 thread spawner reuses this block shape per child.
 * @since 0.3.1
 */
#os("linux") fn tls_bootstrap()
```

- **`.init_array`** — crt's `__libc_csu_init` walked it; `_start` walks `__init_array_start..__init_array_end`
  explicitly so the `__attribute__((constructor))` crash handler (`teko_rt.c:259`) still installs through the
  transition (fully de-C'd at F8). Linux only.
- **Exit finalizers** — `rt_exit` (exit_group) skips atexit, so `_start`'s tail calls `run_exit_finalizers()`
  before it (the cov dump `codegen.tks:9787` + the lazy finalizer `teko_rt.c:1860`), reading the covfile via
  `0124`'s zero-C-runtime `env::var`, not libc `getenv`. Applies on all three (the tail is shared), but only
  Linux LOST the crt's atexit; macOS/Windows finalizers also route here for uniformity.

### 5. The `stack_ptr` intrinsic (Linux native coherence)

For the native Linux `.o`, `_start` cannot be inline asm; add a leaf builtin `teko::sys::stack_ptr(): u64`
lowered by isel to `mov reg,%rsp` / `mov reg,sp`, ONLY the first inst of the frameless `_start`. On the C
route the same read is the naked-stub `__asm__`. This is the one place the two routes differ in EMISSION; the
value flows into the identical `start_linux`. macOS/Windows need no such intrinsic (they read the ABI
functions, which are ordinary calls on both routes).

```teko
/**
 * stack_ptr — the raw stack pointer. ONLY valid as the first operation of the naked Linux `_start`,
 * where it equals the kernel-provided initial SP (pointing at `argc`). A codegen/backend intrinsic:
 * inline asm on the C route, `mov reg,sp` on native. Meaningless anywhere else (a mid-function SP).
 * @return  the raw stack pointer address
 * @since 0.3.1
 */
#os("linux") extern fn stack_ptr(): u64
```

## Rulings & laws

- **D101 (owner 2026-08-25) — the refined line:** zero-libc = zero C-RUNTIME, not zero-OS-ABI. Use the OS
  ABI via `extern` resolved by the platform linker (native-compatible). Per-OS: Linux raw (no ABI lib);
  macOS libSystem (`_NSGet*`, mandatory); Windows kernel32/ntdll. Do NOT force raw where a sanctioned ABI
  exists. The common invariant is linker-resolved + no-C-runtime, NOT one uniform `_start`.
- **D99/owner redesign:** capture argc/argv/envp from the OS (own entry, no C-runtime crt); cover all three
  OSes, defer nothing to "the backend".
- **D86 (each platform's own linker/ABI):** ld (Linux) / ld64 + libSystem (macOS) / link.exe + kernel32
  (Windows). The M-linker is endgame-native, not this.
- **D90 (method):** entry logic in Teko (`start_*`, `decode`, `tls_bootstrap`); codegen/backend reflect
  (per-OS entry stub, `stack_ptr` intrinsic, link flags). `teko_rt.c` TLS/constructor/atexit NOT patched —
  re-driven/orphaned, deleted at F9.
- **W15 full Javadoc** on every declaration; flatten/extract; no inline `//`.
- **`nm` gate is PER-OS (D101):** Linux = zero undefined libc, entry `_start`, no `__libc_start_main`;
  macOS = libSystem ABI allowed (`_NSGet*`/`mmap`/syscall wrappers) but NO C-runtime (`malloc`/`free`/
  `printf`/`getenv`/`setenv`/`snprintf`); Windows = kernel32/ntdll allowed, NO msvcrt/ucrtbase. Native uses
  the SAME per-OS linker resolution.
- **Transition posture:** Linux `-nostartfiles` now (drop crt startup), `-lc` stays for the dead C, dropped
  at F9. macOS keeps `-lSystem`, Windows keeps kernel32 — those are ABI, permanent.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4718592`; commit each green step;
  reseed ONLY at the [RITUAL] points; fixpoint `gen2==gen3`; MEM_PARANOID 0; RSS ratchet flagged; no
  `Co-Authored-By` trailer; sweep `.tkt`/`.tkr`.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `entry_argc_argv` | a program echoes `args().len`/`args()[1]` — argv captured per-OS (Linux stack / macOS `_NSGetArgv` / Windows `CommandLineToArgvW`) | `0` |
| `entry_envp_read` | a program reads an exported var via captured envp (Linux stack / macOS `_NSGetEnviron` / Windows `GetEnvironmentStringsW`), zero C-runtime | `42` |
| `entry_nm_per_os` | `nm -u` per-OS: Linux no libc + entry `_start`; macOS libSystem-only, no C-runtime; Windows kernel32-only, no msvcrt (harness) | `0` |
| `entry_tls_arena_linux` | Linux `-nostartfiles`: heavy alloc right after start — `_start` TLS bootstrap made the `_Thread_local` arena resolve | `0` |
| `entry_init_array_linux` | Linux: a `constructor` runs before vmain under `-nostartfiles` (the `.init_array` walk) | `0` |
| `entry_exit_finalizer` | the explicit exit finalizer fires (cov-style dump) before `rt_exit`, all OSes | `0` |
| `entry_native_start_linux` | the native Linux `.o` links crt-free (`stack_ptr`→`mov reg,sp`, `_start` default entry), runs, exits 0 | `0` |

## Gate

`[RITUAL]` — the load-bearing entry change, a 2-reseed teach→use (the seed must EMIT the capturing entry
before `0124`'s `var` may READ the captured env; else `gen1` — its entry emitted by the pre-change seed — has
no capture and cannot read its own env):

- **RESEED A (teach the entry):** land the per-OS entry emission + link changes + `capture_args`/
  `capture_envp` STASHING, with `var`/`set_var` STILL on the old bindings. Pre-change seed emits `gen1` with
  the old `main` — fine (var still old). `gen2` has the new entry. Reseed = gen2. `gen2==gen3`, MEM_PARANOID
  0, per-OS `nm` clean, native `.o` links crt-free.
- **RESEED B (use the entry):** `0124` switches `var`/`args` onto the captured overlays. The seed now emits
  the capturing entry → `gen1` captures its own argc/argv/envp and self-compiles. Reseed = gen2.

"Green" = each OS starts via its sanctioned linker-resolved entry (Linux `_start`+raw; macOS libSystem
`_NSGet*`; Windows kernel32), argc/argv/envp captured with no C-runtime, per-OS `nm` clean, TLS/init/
finalizers handled, `gen2==gen3`. **Reseed-class:** `fixpoint-rebuild`. RESEED A of the RT-L4 wave (`0124`=B,
`0062` POSIX exec = C). The Windows entry lands with `0062`'s Fase E leg (POSIX first).

## Deps

`S16-FS` (`0055`) + landed `rt_exit` (`0051`/D94). Foundation for `RT-L4-ENV` (`0124`) and `RT-L4` (`0062`).

## Done when

Every emitted program starts via its per-OS sanctioned, linker-resolved, C-runtime-free entry (Linux own
`_start`+raw stack+`-nostartfiles`; macOS libSystem `_NSGet*` via ld64; Windows kernel32 via link.exe),
argc/argv/envp are captured and feed the args/env overlays, Linux `_start` bootstraps TLS + walks
`.init_array` + runs exit finalizers, per-OS `nm` shows no C-runtime, the fixtures pass, and both reseeds are
`gen2==gen3` byte-identical with MEM_PARANOID exit 0. Windows lands with `0062`'s Fase E leg.
