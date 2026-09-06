---
seq: 0028
crumb-id: S16-MM-const
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/plano-s16-arena-mmap.md:189-210"   # §2.3 mmap syscall numbers + flags
  - "docs/design/plano-s16-syscall-intrinsic.md"     # ACHADO A — const-only leaf, no reseed
  - "docs/design/plano-s17-pragmas-crumbs.md"        # §17 #arch/#os pruning (banked)
---

# 0028 · S16-MM-const — mmap syscall numbers/flags in `teko::sys` (`#arch`-guarded; §17 banked)

> mmap syscall numbers/flags in `teko::sys` (`#arch`-guarded; §17 banked) — the `const` leaf the
> Teko-over-mmap arena maps `mmap`/`munmap` through.

## Goal

Declare the mmap/munmap syscall numbers and the PROT_*/MAP_* flags in `src/sys/sys.tks` as `const`-only
Teko literals so the Teko-over-mmap arena core (§16) can issue `mmap(addr, length, prot, flags, fd, offset)`
= `syscall6` and `munmap(addr, length)` = `syscall2`. The syscall NUMBERS are `#arch`-differentiated
(x86_64 vs aarch64, as the existing `SYS_*` are); the PROT_*/MAP_* FLAGS are the Linux asm-generic values,
identical on both arches, so `#os("linux")` without `#arch`. This is a LEAF per §16 ACHADO A — pure
`const` declarations, no code, no reseed of its own; the §17 pragma machinery prunes the non-target arch
blocks before the checker (banked). Purely ADDITIVE and INERT: nothing references these until the arena
switch-over (S16-MM-L1, `0053`, M2), so a `[dry]` build is byte-identical. Its seed folds into SM-R1.

## Where

- **NO CURRENT LANDING.** `src/sys/sys.tks` does NOT exist in the tree yet (grep-confirmed empty). This crumb
  will CREATE `src/sys/sys.tks` as a new module with the `#os("linux")` / `#arch`-split `const` block —
  the mmap family: `SYS_MMAP` (9 / 222), `SYS_MUNMAP` (11 / 215), `SYS_MPROTECT` (10 / 226), and the
  `#os("linux")` flag consts `PROT_NONE/READ/WRITE` (0/1/2), `MAP_PRIVATE` (2), `MAP_ANONYMOUS` (0x20),
  `MAP_FIXED` (0x10).
- The existing Linux `SYS_*` constants (not yet in `sys.tks`) are the pattern this crumb will follow verbatim
  (`#os`/`#arch` guards, W15 doc-comment per const, when it establishes the module).
- `#arch`/`#os` guard threading (the §17 pragma machinery, banked) — will prune the non-target block before
  the checker, so `SYS_MMAP` resolves to the host arch's number.

NEW: creates `src/sys/sys.tks` module with `const`-only definitions.

## How

1. **Declare the syscall numbers (`#arch`-split).** Mirror the existing `SYS_*` pattern:

```teko
/**
 * SYS_MMAP — the `mmap` syscall number for the anonymous private mapping the Teko-over-mmap arena's backing
 * store uses (`syscall6(SYS_MMAP, addr, length, prot, flags, fd, offset)`). `#arch`-split: 9 on x86_64,
 * 222 on aarch64. mmap returns the page-aligned address (>=16-aligned — it SUBSUMES the TK_ARENA_ALIGN=16
 * guarantee posix_memalign gave) or a value in the error band `[-4095, -1]` (= `-errno`) on failure.
 *
 * @since 0.3.1
 */
#os("linux") #arch("x86_64")
pub const SYS_MMAP: i64 = 9
```

   and the aarch64 twin (`222`), plus `SYS_MUNMAP` (11/215) and `SYS_MPROTECT` (10/226) the same way. Each
   carries a full W15 doc-comment (the doc-comment is the ONLY comment — no inline `//`).
2. **Declare the flags (`#os("linux")`, no `#arch`).** `PROT_NONE/READ/WRITE = 0/1/2`, `MAP_PRIVATE = 2`,
   `MAP_ANONYMOUS = 0x20`, `MAP_FIXED = 0x10` (the last for the P2 arena-control-anchor fallback). Each with
   a W15 doc-comment naming its asm-generic origin.
3. **The canonical mapping (documented, not code).** An anonymous private mapping is `flags = MAP_PRIVATE |
   MAP_ANONYMOUS = 0x22`, `prot = PROT_READ | PROT_WRITE = 3`, `fd = -1`, `offset = 0`. The arena computes
   these from the consts at the call-site (S16-MM-L1); this crumb only declares the atoms.
4. **§17 pruning (banked).** The `#arch`/`#os` guards are threaded through `parse_function`/const parsing
   and pruned BEFORE the checker (the §17 pragma machinery, already banked), so `SYS_MMAP` resolves to the
   host arch's number and the non-target block never reaches the type-checker.
5. **Stay inert.** No `src/` site references the mmap consts until the arena switch-over (S16-MM-L1,
   `0053`); a `[dry]` build is byte-identical.

## Rulings & laws

- **Teko-only:** `src/sys/sys.tks` `const`-only; no C twin, no `teko_rt.c`.
- **W15 full Javadoc** on EVERY const (pub); flatten; no inline `//` — the doc-comment carries the origin
  and value note.
- **LEAF, no reseed (§16 ACHADO A):** const-only declarations mint no reseed; the seed folds into SM-R1.
- **`#os`/`#arch` guards (§17, banked):** the pragma machinery prunes non-target blocks before the checker,
  matching the existing `SYS_*` pattern.
- **Additive/inert:** no consumer until S16-MM-L1 → byte-identical.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  reseed only at SM-R1 (`0030`).

## Fixtures

`none — the fixpoint self-build exercises this`. These are `const` literals with `#os`/`#arch` guards; the
self-build compiles `sys.tks` and the §17 pruning resolves each const to the host arch value — a value error
would fail the build directly. No behavior branch the fixpoint does not reach (the arena syscall behavior is
exercised by S16-MM-L1's own fixtures once it consumes them).

## Gate

`[dry]` — compile + fixpoint (byte-identical; consts inert until S16-MM-L1). "Green" = `sys.tks` compiles
with the mmap family + PROT_*/MAP_* consts (each W15-documented, `#os`/`#arch`-guarded), the §17 pruning
resolves `SYS_MMAP` to the host arch number, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`SYS_MMAP`/`SYS_MUNMAP`/`SYS_MPROTECT` (`#arch`-split) and `PROT_*`/`MAP_*` (`#os("linux")`) exist in
`src/sys/sys.tks` with full W15 doc-comments, the §17 guards prune to the host arch, and a `[dry]` build is
byte-identical (consts inert until S16-MM-L1 adopts them).
