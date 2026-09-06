---
seq: 0052
crumb-id: S16-MM-pool
milestone: M2
gate: "[dry]"
reseed-class: "none"
deps: [S16-MM-wp, S16-MM-const]
sources:
  - "docs/design/plano-s16-arena-mmap.md:214-237"   # §3 THE META-POOL
  - "docs/design/plano-s16-arena-mmap.md:57-101"    # §1 allocation-free core dialect
  - "docs/design/plano-s16-arena-mmap.md:351-403"   # §7 sub-crumb D + spec
  - "docs/design/plano-s16-arena-mmap.md:407-434"   # §8 META-POOL top risk
---

# 0052 · S16-MM-pool — arena-mmap meta-pool (mmap-specific memory-correctness keystone)

> Land the META-POOL: a small-object bump sub-allocator over mmap pages, dedicated to the fixed-size
> arena metadata (region headers = 64 B, entries arrays, the CONTROL block, free-nodes) with a 64-B
> header free-list — the one place the mmap swap is NOT a mechanical substitution of the libc arena, and
> a footprint keystone against the 6 GB cap.

## Goal

The Teko-over-mmap arena is authored in the allocation-free dialect (`load_u64`/`store_u64`/`word_ptr`/
`ptr_word`/`syscallN` — landed via S16-MM-wp/S16-MM-const). But mmap's minimum mapping is one PAGE (4096
B): one-mmap-per-64-B-header is a 64× footprint blow-up, fatal against the 6.5 GiB build cap (MEM_PARANOID
already peaks ~5.6 GB). The META-POOL fixes this: it mmaps a 64 KB slab and bump-allocates aligned 64-B
(and doubling-array) cells from it, growing by another slab when full (a slab free-list threads the
slabs), and maintains a fixed-size free-list of 64-B header cells (single-linked through the freed
header's first word) so a `region_drop` returns the header for reuse — mirroring malloc/free footprint so
the flip does NOT grow steady-state memory. Byte-preservation posture: FEATURE-GATED-INERT — the arena
core + META-POOL land as add-alongside NEW functions in `src/runtime/arena.tks`; codegen STILL emits the
C `tk_region_alloc`, so the corpus emit is unchanged and the arena is proven instead by the promoted
`arena_mmap` regression on the C leg (`[dry]`, `none` reseed). This is the correctness keystone the
switch-over (S16-MM-L1/L2) rides.

## Where

- `src/runtime/arena.tks` — the META-POOL over mmap (this branch already carries the slab/cursor control
  slots `CTRL_META_SLAB`/`CTRL_META_CURSOR`/`CTRL_META_END` at `:57`/`:59`/`:61`, `AR_META_SLAB = 65536`
  at `:101`, and the `os_mmap`/`VirtualAlloc` externs at `:130-145`); this crumb completes the header
  free-list + entries-array binning + the reuse guarantee.
- `examples/regressions/arena_mmap/` — the promoted `arena_teko` probe, RE-TARGETED to the C leg + mmap
  fundo, plus the NEW META-POOL header-reuse assert.

## How

1. **The slab sub-allocator.** `ar_meta_alloc(control, size)` bumps a 64-B-aligned cell from the current
   slab (`CTRL_META_CURSOR`..`CTRL_META_END`); when the slab is exhausted, mmap a fresh `AR_META_SLAB`
   (64 KB) slab, thread it onto the slab free-list (`AR_SLAB_LINK` at the slab head), and continue.

```teko
/**
 * ar_meta_alloc — bump a fixed-size metadata cell (region header 64 B, entries array, CONTROL block,
 * free-node) from the META-POOL slab, mmap-ing a fresh 64 KB slab when the current one is exhausted
 * (`plano-s16-arena-mmap §3`). NON-ALLOCATING through the arena itself: every field lives at a computed
 * address read/written by `load_u64`/`store_u64` — the discipline that breaks the bootstrap circularity.
 * Never one-mmap-per-header (the 64× blow-up that would OOM the 6.5 GiB cap).
 * @param control  the CONTROL-block address (holds the slab cursor/end/link)
 * @param size     the cell size in bytes (64 for a header; the array size for entries)
 * @return         the aligned cell address
 * @since §16
 */
fn ar_meta_alloc(control: u64, size: u64): u64
```

2. **The 64-B header free-list.** `region_drop` returns the header cell to a single-linked free-list
   threaded through the freed header's first word; the next `region_new` takes from the free-list before
   bumping a new cell. This is the malloc/free footprint mirror — a drop-then-new region reuses the SAME
   header address.

```teko
/**
 * ar_meta_free_header — park a 64-B region-header cell onto the META-POOL free-list for reuse, threading
 * the free link through the cell's first word (`plano-s16-arena-mmap §3`). Paired with the take path in
 * `region_new`: a drop-then-new region MUST reuse the same header address — the reuse the volume gate
 * asserts, so a header leak cannot silently bloat the self-build past the cap.
 * @param control  the CONTROL-block address (holds the header free-list head)
 * @param cell     the header cell address to park
 * @return         void
 * @since §16
 */
fn ar_meta_free_header(control: u64, cell: u64)
```

3. **Entries arrays** (rare, small): park into size-binned free slots or leak-bounded (already
   O(regions) tiny). Chunk PAYLOADS keep their own per-chunk mmap (one mapping per chunk, munmap'd on
   drop) — the META-POOL is metadata-only.
4. **The volume gate is the correctness proof.** Extend the `arena_teko` volume gate (promoted to
   `arena_mmap`) to assert META-POOL reuse: a `region_drop` then `region_new` reuses the SAME header
   address; the metadata footprint stays bounded to ~the malloc baseline. This is the single most likely
   failure (`§8` TOP RISK) — the gate MUST catch a header leak.
5. **Add-alongside, not wired.** Codegen still emits `tk_region_alloc`; the arena core + META-POOL are
   compiled but UNUSED by the corpus, proven only by the C-leg `arena_mmap` regression. Corpus emit
   unchanged → `[dry]`.

## Rulings & laws

- **Teko-only:** the arena core + META-POOL are `.tks` over mmap/load/store; the maintained-C exception
  is only the P2 CONTROL seam (`tk_arena_control_get/set`, `arena.tks:1-3`), per standing law.
- **W15 full Javadoc** on `ar_meta_alloc`/`ar_meta_free_header` and every core fn; flatten; no `//`.
- **Allocation-free dialect (`§1`):** the META-POOL allocates NOTHING through the arena — no `str`, no
  `[]T` growth, no boxing; only `u64` arithmetic + `syscallN`/`load_u64`/`store_u64`/`word_ptr`.
- **META-POOL keystone (`§3`/`§8` TOP RISK):** one-mmap-per-header = 64× blow-up → cap OOM; the
  sub-allocator + header free-list is mandatory, and the volume gate asserts reuse — a blown guard is a
  root-cause fix (a header leak), never a raised ceiling.
- **Feature-gated-inert:** add-alongside, unreferenced by the corpus → corpus emit byte-identical,
  `[dry]`, `none` reseed. (JUDGMENT CALL flagged in the report: any self-image temp-ID shift from adding
  the fns is absorbed at the S16-MM-L1 sym-table reseed, `0053`, not minted here.)
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` (and do NOT run
  MEM_PARANOID + a parallel build together); commit the green step; no reseed (leaf).

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `arena_mmap` | promoted `arena_teko` group-0..5 asserts on the C leg + mmap fundo (distinct 16-aligned ptrs; >64 KB exclusive chunk; drop idempotent; subtree drop; register/lookup; free-list ceil/floor; MEM_PARANOID poison) PLUS the META-POOL header-reuse assert (drop-then-new reuses the same header address) | 42 |

This path is NOT self-build-exercised (codegen still routes to the C arena; the Teko arena is unwired) —
the META-POOL correctness is proven ONLY by this promoted isolated regression on the C leg. It is the
add-alongside proof that the switch-over (L1/L2) then flips onto.

## Gate

`[dry]` — compile (`--no-verify --release`, `TEKO_BACKEND=c`, `ulimit -v 6815744`) + the `arena_mmap`
regression (exit 42) + trivial fixpoint (no corpus emitted-byte change; the arena is add-alongside).
"Green" = the META-POOL bump + header free-list works, the volume gate asserts header REUSE (no leak),
MEM_PARANOID poison holds, and the corpus `teko.c` is byte-identical. Reseed-class: `none`.

## Deps

`S16-MM-wp`, `S16-MM-const`.

## Done when

The META-POOL sub-allocates 64-B metadata cells over 64 KB mmap slabs with a header free-list, a
drop-then-new region reuses the same header address (bounded footprint), the `arena_mmap` regression
exits 42 with the reuse assert, and the corpus emit is byte-identical.
