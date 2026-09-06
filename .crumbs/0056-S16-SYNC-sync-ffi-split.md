---
seq: 0056
crumb-id: S16-SYNC
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-SYNC-const]
sources:
  - "docs/design/plano-s16-sync-cross-plataforma.md:64-100"   # §1.1 sync API per OS
  - "docs/design/plano-s16-sync-cross-plataforma.md:162-235"  # §3 extern FFI declarations
  - "docs/design/plano-s16-sync-cross-plataforma.md:285-323"  # §4 #os split (linux verbatim)
  - "docs/design/plano-s16-sync-cross-plataforma.md:390-401"  # §5 teko.tkp link
  - "docs/design/plano-s16-sync-cross-plataforma.md:405-463"  # §6/§7 reseed analysis + crumb 2
---

# 0056 · S16-SYNC — sync FFI + split (futex / ulock / WaitOnAddress) unblocking the cross-platform seed

> Add the per-OS sync `extern`s and rewrite `futex_wait`/`futex_wake` as an `#os` split (Linux futex
> syscall VERBATIM, macOS `os_sync_wait_on_address`/`os_sync_wake_by_address_*` libSystem, Windows
> `WaitOnAddress`/`WakeByAddress*` kernel32) + `[extern.libs.windows] += synchronization` — the crumb
> that closes the red CI on every non-Linux-x86_64 leg.

## Goal

`src/runtime/sync.tks`'s `futex_wait`/`futex_wake` call `teko::sys::syscall6` with `SYS_FUTEX`/`FUTEX_*`
consts that are `#os("linux")` — so OFF-Linux those consts are PRUNED before the checker and become
"unknown type: sys" (sync.tks:20,34), failing the type build on every non-Linux leg → red CI. This crumb
splits `futex_wait`/`futex_wake` by `#os` (Linux body byte-IDENTICAL to today; macOS via the public
`os_sync_wait_on_address`/`os_sync_wake_by_address_any`/`_all`; Windows via `WaitOnAddress`/
`WakeByAddressSingle`/`WakeByAddressAll`) and adds `synchronization` to `[extern.libs.windows]`. The
consts (`OS_SYNC_*`, `WIN_INFINITE`) are landed by S16-SYNC-const. `mtx_*`/`cv_*` sit on top and DO NOT
change (only a doc-comment note that `futex_*` is `#os`-split under them). Byte-preservation posture: the
Linux target emit is byte-IDENTICAL (the `#os("macos")`/`#os("windows")` branches + new `extern`s are
pruned before the checker; the `#os("linux")` bodies are verbatim), so the Linux seed does NOT change —
`fixpoint-rebuild` reseed-class by manifest, but the seed-target `teko.c` is unchanged (SEM reseed on
Linux; macOS/Windows targets change but are not the seed). This is the crumb that turns the CI green.

## Where

- `src/runtime/sync.tks:22-…` — `futex_wait`/`futex_wake` — rewrite as `#os` splits; this branch already
  carries the `os_sync_wait_on_address` (`:4`) and `WaitOnAddress` (`:13`) externs and the split bodies
  (`:22`/`:28`/`:33` for wait, `:40`/`:46`/`:55` for wake) — the crumb formalizes the doc + link.
- `teko.tkp` — `[extern.libs.windows] += synchronization` (`-lsynchronization`; MinGW
  `libsynchronization.a`); `[extern.libs.macos]` stays EMPTY (libSystem implicit via `-lSystem`);
  `[extern.libs.linux]` empty (syscalls, no lib).

## How

1. **The per-OS `extern`s** (`§3`), each `#os`-guarded, full-Javadoc, autonomous C prototype (no OS
   header in the emitted `teko.c`):

```teko
/**
 * os_sync_wait_on_address — libSystem `int os_sync_wait_on_address(void *addr, uint64_t value,
 * size_t size, os_sync_wait_on_address_flags_t flags)`: blocks while the `size` bytes at `addr` equal
 * `value`. With `size=4` it is bit-for-bit our 32-bit futex semantics (`plano-s16-sync §1.1`). The
 * PUBLIC, documented Apple API (not the private `__ulock_*`) — the real implementation under the
 * no-shortcuts law; requires macOS 14.4+ (owner: target the newest OS).
 * @param addr   the 32-bit futex word address
 * @param value  the expected value (compared over `size` bytes)
 * @param size   bytes to compare (4)
 * @param flags  OS_SYNC_WAIT_ON_ADDRESS_NONE (0)
 * @return       remaining waiters / -1 on error — discarded
 * @since §16
 */
#os("macos")
extern fn os_sync_wait_on_address(addr: u64, value: u64, size: u64, flags: u32): i32 = "os_sync_wait_on_address" from "System"
```

Land the siblings identically: `os_sync_wake_by_address_any`/`_all` (macOS `from "System"`),
`WaitOnAddress`/`WakeByAddressSingle`/`WakeByAddressAll` (Windows `from "synchronization"`).

2. **The `#os` split bodies** (`§4`), Linux VERBATIM (so the seed-target `teko.c` is byte-identical):

```teko
/**
 * futex_wait — block while the 32-bit word at `addr` equals `expected`. `#os`-split under the same
 * signature: Linux = the futex syscall (body byte-identical to today); macOS = os_sync_wait_on_address;
 * Windows = WaitOnAddress with a `buf_ptr(8)` compare scratch (`plano-s16-sync §4`). `mtx_*`/`cv_*` sit
 * on top unchanged.
 * @param addr      the 32-bit futex word address
 * @param expected  the value to wait on
 * @return          void
 * @since §16
 */
#os("linux")
fn futex_wait(addr: u64, expected: i64)
```

- macOS body: `os_sync_wait_on_address(addr, (expected to u64) & 0xffffffff, 4,
  OS_SYNC_WAIT_ON_ADDRESS_NONE)`.
- Windows body: `store_u64(ptr_word(buf_ptr(8)) to u64, expected to u64)` then `WaitOnAddress(addr,
  scratch, 4, WIN_INFINITE)` — the `buf_ptr(8)` scratch is only on the SLOW/contended path and only
  touches the per-task arena, which does NOT use the mutex being waited on → no re-entrancy (`§9` R2).
- `futex_wake`: Linux verbatim; macOS `n==1 ? os_sync_wake_by_address_any : _all`; Windows `n==1 ?
  WakeByAddressSingle : WakeByAddressAll` — the `n==1`/`n==INT_MAX` split is exact (`§4`).

3. **The `.tkp` link** (`§5`): `[extern.libs.windows] += synchronization`; macOS empty (libSystem
   implicit); Linux empty. `mf_extern_spec("synchronization")` → `-lsynchronization`.
4. **`mtx_*`/`cv_*` unchanged.** Only a doc-comment note per fn that `futex_*` is `#os`-split underneath.
5. **Fixpoint / reseed** (`§6`): the Linux target `teko.c` is BYTE-IDENTICAL to `bootstrap/teko.c` (the
   macOS/Windows branches + externs are pruned before the checker; the Linux bodies are verbatim) → the
   seed does NOT change; `provenance_gate` + `fixpoint (tc2==tc3)` stay PASS. macOS/Windows targets emit
   the FFI prototypes+calls, but they are compiled from gen0 and do not feed the seed. If, by mistake, a
   Linux body is touched (changing the emitted C), that WOULD require a real reseed — the design AVOIDS
   it by keeping the Linux bodies verbatim.

## Rulings & laws

- **Teko-only:** `sync.tks` + `teko.tkp` (`.tks`/manifest); no `teko_rt.c` touched; `atomic_*`/`syscall*`/
  `buf_ptr`/`store_u64` are existing builtins — no new compiler builtin, no reseed via that path.
- **W15 full Javadoc** on every `extern`, every `#os` body, and the `mtx_*`/`cv_*` note; flatten; no `//`.
- **Owner "no shortcuts / works, not just compiles" (`§0` ruling):** the sync primitive must FUNCTION on
  macOS-arm64, Windows-x86_64, Linux-x86_64/arm64 (glibc+musl) — real APIs, no stubs. macOS uses the
  PUBLIC `os_sync_wait_on_address` (not the private `__ulock`), floor macOS 14.4+.
- **Linux verbatim → no seed reseed (`§6`):** the seed-target `teko.c` is byte-identical; the manifest
  reseed-class is `fixpoint-rebuild` but the Linux gate is "host emits `teko.c` identical to the seed".
- **Windows `VirtualFree`/scratch quirks (`§9` R2/R4):** the `buf_ptr(8)` compare scratch is contended
  -path-only and non-re-entrant; documented.
- **Reported, not actioned (`§1.3`):** thread SPAWN/JOIN off-Linux (`clone`/`pthread_create`/
  `CreateThread`) is a DISTINCT larger design — REPORTED up, not this crumb (this crumb makes the sync
  primitive real, unblocking the seed/CI).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` (the sync doc uses
  8388608 for cross-emit, but the standing project cap is 6.5 GiB — use the standing cap); commit the
  green step; reseed ONLY at the fixpoint; the seed-target `teko.c` is byte-identical; sweep `.tkt`/
  `.tkr` if any AST/signature changes (none expected — signatures are unchanged).

## Fixtures

The cross-emit itself is the test (the type-check + emit on the 3 OS); the runtime mutex/condvar
exercise runs only when §10-native lights up the CI (not locally, OOM). One regression pinned for that CI
leg:

| fixture | asserts | expected |
|---|---|---|
| `mtx_mutual_exclusion` | N threads increment a counter under `mtx_lock`/`mtx_unlock` + a `cv_wait`/`cv_broadcast` barrier; `eq_i64(counter, N*iters)` | 0 |

This path is NOT self-build-exercised (the compiler's own build is single-threaded; the sync primitives
fire only under a concurrent workload) — the isolated `.tkr` scenario covers mutual exclusion across all
three OS (same surface, `futex_*` `#os`-split underneath), per `§8`. The primary per-crumb signal is the
cross-emit: the `"unknown type: sys"` error DISAPPEARS on all three targets.

## Gate

`[fixpoint]` — build gen2 (`--no-verify --release`, `TEKO_BACKEND=c`, `TEKO_CC=clang`,
`ulimit -v 6815744`) + the cross-emit check on all 3 targets + `gen2==gen3` byte-identity on Linux.
"Green" = all 3 OS type-check and emit (macOS with `os_sync_wait_on_address`/…, Windows with
`WaitOnAddress`/…, `-lsynchronization` linked; Linux `teko.c` byte-identical to the seed), the
`"unknown type: sys"` symptom is gone, and the `mtx_mutual_exclusion` fixture is ready for the native CI
leg. Reseed-class: `fixpoint-rebuild` (Linux seed unchanged → no reseed harvested on the seed target).

## Deps

`S16-SYNC-const`.

## Done when

`futex_wait`/`futex_wake` are `#os`-split (Linux verbatim, macOS libSystem, Windows kernel32), the sync
`extern`s + `[extern.libs.windows] += synchronization` are in place, `mtx_*`/`cv_*` are unchanged, the
`"unknown type: sys"` failure is gone on all three targets, the Linux `teko.c` is byte-identical to the
seed, and the CI is green on every leg.
