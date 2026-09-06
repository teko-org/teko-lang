---
seq: 0029
crumb-id: S16-SYNC-const
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/plano-s16-sync-cross-plataforma.md:140-158"   # §2 cross-platform ABI consts (macOS/Windows)
  - "docs/design/plano-s16-sync-cross-plataforma.md:30-51"     # infra FFI sufficient, no compiler crumb-0
  - "docs/design/plano-s16-sync-cross-plataforma.md:162-166"   # §3 (extern decls are the LATER S16-SYNC crumb)
---

# 0029 · S16-SYNC-const — cross-platform sync/arena/thread ABI consts (`#os`-guarded) in `teko::sys`

> Cross-platform sync/arena/thread ABI consts (`#os`-guarded) in `teko::sys` — the `const` leaf that unblocks
> the off-Linux seed.

## Goal

Declare, in `src/sys/sys.tks`, the macOS and Windows ABI constants the cross-platform sync + arena +
thread-stack work needs — pure `const` Teko literals transcribed from each OS's ABI, `#os`-guarded (the §17
pragma machinery prunes the non-target blocks before the checker). This is the CONST leaf of the §16 sync
wave; the matching `extern` FFI declarations (`os_sync_wait_on_address`/`WaitOnAddress`/`mmap`/…) are the
LATER, heavier `S16-SYNC` crumb (`0056`, M2). Splitting the consts out here (a leaf, no reseed) unblocks the
cross-platform seed early: the Linux consts (existing `SYS_FUTEX`/`FUTEX_*`) stay untouched; the macOS block
adds the `os_sync_wait_on_address` flag defaults + the Darwin mmap flags + the `mmap` error sentinel; the
Windows block adds the `VirtualAlloc`/`WaitOnAddress` constants. Purely ADDITIVE and INERT: on a Linux build
the §17 machinery prunes the macOS/Windows blocks, and no `src/` site references them until S16-SYNC adopts
the externs — so a `[dry]` build is byte-identical. Its seed folds into SM-R1.

## Where

- **NO CURRENT LANDING.** `src/sys/sys.tks` does NOT exist in the tree yet (grep-confirmed empty). This crumb
  will add (to the module created or will-be-created by 0028) the `#os("macos")` `const` block —
  `OS_SYNC_WAIT_ON_ADDRESS_NONE: u32 = 0`, `OS_SYNC_WAKE_BY_ADDRESS_NONE: u32 = 0`,
  `PROT_NONE/READ/WRITE: i32 = 0/1/2`, `MAP_PRIVATE: i32 = 0x0002`, `MAP_ANON: i32 = 0x1000`,
  `MAP_FAILED_WORD: u64 = 18446744073709551615` (`(void*)-1`).
- **NO CURRENT LANDING.** This crumb will also add the `#os("windows")` `const` block —
  `MEM_COMMIT: u32 = 0x1000`, `MEM_RESERVE: u32 = 0x2000`, `MEM_RELEASE: u32 = 0x8000`,
  `PAGE_READWRITE: u32 = 0x04`, `PAGE_NOACCESS: u32 = 0x01`, `WIN_INFINITE: u32 = 0xFFFFFFFF`
  (`dwMilliseconds` = infinite block).
- The existing/future Linux consts (`SYS_FUTEX`/`FUTEX_*`, `CLOCK_*`, the mmap family from 0028) — UNTOUCHED
  (each `#os` block is independent).
- `#os` guard threading (§17 pragma machinery, banked) — will prune the non-host blocks before the checker.

NEW: adds `#os("macos")` / `#os("windows")` `const` blocks to `src/sys/sys.tks`; the module itself is created
by 0028 or will be expanded by 0028.

## How

1. **Declare the macOS consts** (`#os("macos")`, full W15 doc-comment per const):

```teko
/**
 * OS_SYNC_WAIT_ON_ADDRESS_NONE — the default `flags` for macOS `os_sync_wait_on_address` (the stable
 * libSystem wait/wake-by-address ABI the cross-platform sync path uses in place of the Linux futex
 * syscall). Value 0 (no flags). Declared as a named const for W15 clarity even though it could be passed
 * as the literal `0` at the call-site.
 *
 * @since 0.3.1
 */
#os("macos")
pub const OS_SYNC_WAIT_ON_ADDRESS_NONE: u32 = 0
```

   and the siblings: `OS_SYNC_WAKE_BY_ADDRESS_NONE = 0`; the Darwin mmap flags `PROT_NONE/READ/WRITE =
   0/1/2` (same values as Linux but their OWN `#os("macos")` block), `MAP_PRIVATE = 0x0002`, `MAP_ANON =
   0x1000` (Darwin `<sys/mman.h>`); and `MAP_FAILED_WORD = 18446744073709551615` (the `(void*)-1` mmap
   error sentinel).
2. **Declare the Windows consts** (`#os("windows")`, full W15 doc-comment per const): `MEM_COMMIT =
   0x1000`, `MEM_RESERVE = 0x2000`, `MEM_RELEASE = 0x8000` (VirtualAlloc/Free); `PAGE_READWRITE = 0x04`,
   `PAGE_NOACCESS = 0x01`; `WIN_INFINITE = 0xFFFFFFFF` (the infinite `dwMilliseconds` of `WaitOnAddress`).
3. **Consts ONLY — externs are the later crumb.** The `extern fn` declarations that USE these consts
   (`os_sync_wait_on_address`, `WaitOnAddress`, the macOS `mmap`/`munmap` via FFI-of-the-ABI) are the
   heavier `S16-SYNC` crumb (`0056`, M2). The FFI infra already links libSystem (macOS) and
   kernel32/synchronization (Windows) — no compiler crumb-0 is needed (§0 of the source doc). This crumb is
   the leaf that seeds the atoms.
4. **§17 pruning (banked).** On a Linux build the `#os("macos")`/`#os("windows")` blocks are pruned before
   the checker (the §17 pragma machinery); the Linux consts resolve unchanged. So the additions are inert on
   the current (Linux CI) build.
5. **Stay inert.** No `src/` site references the macOS/Windows consts until S16-SYNC adopts the externs; a
   `[dry]` build is byte-identical.

## Rulings & laws

- **Teko-only:** `src/sys/sys.tks` `const`-only; no C twin, no `teko_rt.c`.
- **W15 full Javadoc** on EVERY const (pub); flatten; no inline `//` — the doc-comment carries the ABI
  origin and value.
- **LEAF, no reseed:** const-only declarations mint no reseed; the seed folds into SM-R1.
- **`#os` guards (§17, banked):** the pragma machinery prunes non-host blocks before the checker (each `#os`
  block independent; Linux consts untouched).
- **No new C shortcut (owner ruling, source §1.1):** macOS uses the stable public `os_sync_wait_on_address`
  ABI and libSystem `mmap` (NOT a raw undocumented syscall) — the consts reflect that decision.
- **Additive/inert:** no consumer until S16-SYNC; pruned on Linux → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

`none — the fixpoint self-build exercises this`. These are `const` literals under `#os` guards pruned on the
Linux CI build; the self-build compiles `sys.tks` and a value/type error would fail the build directly. The
off-Linux behavior that consumes them is exercised by the S16-SYNC (`0056`) fixtures on the macOS/Windows CI
legs, not here.

## Gate

`[dry]` — compile + fixpoint (byte-identical; macOS/Windows consts pruned on Linux, inert until S16-SYNC).
"Green" = `sys.tks` compiles with the macOS + Windows `#os`-guarded ABI consts (each W15-documented), the
§17 pruning drops the non-host blocks, the Linux consts are untouched, `[dry]` build byte-identical.
Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

The macOS (`OS_SYNC_*`, Darwin `PROT_*`/`MAP_*`, `MAP_FAILED_WORD`) and Windows (`MEM_*`, `PAGE_*`,
`WIN_INFINITE`) ABI consts exist in `src/sys/sys.tks` under independent `#os` blocks with full W15
doc-comments, the §17 guards prune to the host OS, and a `[dry]` build is byte-identical (consts inert until
S16-SYNC adopts the externs).
