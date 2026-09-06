---
seq: 0059
crumb-id: RT-L1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [S16-MM-L2]
sources:
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:58-61"      # §1.2 S0 alloc / S1-S2 arena / slice-box (5th gap)
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:112-118"    # §2.1 L1 = alloc+arena+slice/box, one mem syscall via extern
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:155-170"    # §2.3 the L1 bottleneck — P1/P2 compiler seams
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:283-300"    # §4.4 boxing honors the SAME ResidencePlan
  - "docs/design/migracao-runtime-c-para-teko-0.3.1.md:355-378"    # §6 store_u64/load_u64 W15 signatures
---

# 0059 · RT-L1 — runtime C→Teko L1: alloc + arena/regions + slice/box (the two compiler seams)

> Close the L1 layer: allocation, the arena/region tree, and aggregate slice/box growth run in Teko over the
> Teko-over-mmap arena (from S16-MM-L2), reaching the OS with ONE memory syscall via `extern fn`.

## Goal

L1 is the allocation seam every higher layer bumps through: **alloc + arena/regions + slice/box**. The heavy
lift — the compiler's own runtime IS the Teko-over-mmap arena — landed in **S16-MM-L2** (`0054`): `arena.tks`
already realises `region_alloc`/`region_new`/`region_drop`/`region_drop_subtree`/`alloc` (`arena.tks:685-720`)
over `os_mmap` (`arena.tks:130`), with the P1 word load/store seam declared (`scope.tks:289,294`
`load_u64_signature`/`store_u64_signature`) and the P2 `ARENA_CONTROL` slot reached by `ar_control()`
(`arena.tks:246`). What RT-L1 finishes is the **5th gap — slice/box boxing** (`migracao…` §1.2): the aggregate
growth/boxing symbols still only in `teko_rt.c` — `tk_slice_push`/`_push_r`/`_push_fo`/`tk_slice_elem_box`/
`tk_append_bytes_fo`/`tk_slice_with_cap`(`_r`)/`tk_mem_copy`/`tk_push_slot`/`tk_push_cache_purge`
(`teko_rt.c:3762-3995`) — migrate to Teko bumping into the CURRENT region (`_r`), honoring the same
`ResidencePlan` (`modelo…` §14) user code obeys: the runtime is the first client to prove the residence rule
closes over itself. Byte-preserving for existing programs: the emitted `teko.c` is guarded byte-identical by
the fixpoint (existing-case residence unchanged; `migracao…` R8). It does NOT reseed teaching — a
`fixpoint-rebuild` swap the core consumes.

**BLOCKED (design-ahead, honest).** Behind the **native fixpoint closing** (`migracao…` banner) AND its dep
**S16-MM-L2** (the load-bearing arena switch-over). This is the design; the implementer resumes in minutes when
both close. L1 is the bottleneck ALL higher layers allocate through, so it precedes L2/L5 — but the arena
reference implementation already exists (`examples/probes/arena_teko/`, `migracao…` §2.3), so the remaining
work is the slice/box migration plus the mutation-gate oracles, not a fresh arena.

## Where

- `src/runtime/arena.tks:685-720` — `region_alloc`/`region_new`/`region_drop`/`region_drop_subtree`/`alloc`
  (already Teko, from S16-MM-L2) — confirm they are the sole allocation seam; no `tk_alloc` C on the path.
- `src/runtime/arena.tks:246` `ar_control` + `:130` `os_mmap` — the P2 `ARENA_CONTROL` control word and the ONE
  memory syscall reached by `extern fn` (`mmap`/`VirtualAlloc`); confirm intact.
- `src/checker/scope.tks:289,294` — `load_u64_signature`/`store_u64_signature` (the P1 seam builtins) — confirm
  they lower to a single `LLoad`/`LStore` (`lir.tks`), the NON-allocating primitive that breaks the arena's
  self-reference circularity (`migracao…` §2.3 P1).
- `src/runtime/teko_rt.c:3762-3995` — the slice/box family (`tk_slice_push`/`_push_r`/`_push_fo`/
  `tk_slice_elem_box`/`tk_append_bytes_fo`/`tk_slice_with_cap`(`_r`)/`tk_mem_copy`/`tk_push_slot`/
  `tk_push_cache_purge`) — MIGRATE each to a Teko body in `teko_rt.tks` bumping into the current region; the C
  bodies go DEAD (deleted at M3, not here).
- `src/runtime/teko_rt.tks` — home of the migrated slice/box bodies (new fns beside the existing L0 bodies).
- NO new PUBLIC decl surface: the slice/box symbols already have their contract; migration re-homes the body.

## How

1. **Confirm alloc + arena/regions are the Teko arena** (consumed from S16-MM-L2): `alloc`/`region_*` bump
   through the Teko-over-mmap control block; `tk_alloc`/`tk_region_*` C are dead on the path.
2. **Migrate slice/box boxing to Teko honoring residence** (`migracao…` §4.4). Each `tk_slice_push_r` bumps a
   fresh box into the caller's CURRENT region `r` (not root), applying the ceil16-on-take / floor16-on-park
   asymmetry that guards the aliasing decree (`arena-em-teko.md` §4.1). A boxing that allocates in root
   REOPENS the leak `modelo…` closes — the fixtures assert residence, not just absence of crash.
3. **`tk_mem_copy` uses the P1 load/store seam** (`load_u64`/`store_u64`, `scope.tks:289,294`) for the raw
   index-join copy (the RM-C2 idiom); no C `memcpy` on the path (S16-EMIT already grounded the emit-side
   memcpy).
4. **The link is the normal program link** (`migracao…` §2.2): the L1 bodies are `exp fn` Teko compiled INTO
   the program's object; `mmap`/`VirtualAlloc` resolves as an undefined external, like any libc symbol
   (proven: `U aligned_alloc@GLIBC_2.16` in `examples/probes/arena_bottom`, `migracao…` §2.2.2).
5. **The runtime is subject to its own ResidencePlan** (`migracao…` §4.1): the arena control block is the
   legitimate `#singleton`-like root (one process word); everything else resides in scope. No leak-to-root.
6. **Fixpoint byte-identity + the mutation gates.** `gen2==gen3` byte-identical proves existing allocation
   residence did not shift; the mutation gates (`arena-em-teko.md` §5) + `TEKO_MEM_PARANOID` oracle assert the
   arena BEHAVES (a subtly-wrong arena passes tests and corrupts every emitted program — `migracao…` R7).

The W15 signatures the implementer copies verbatim for the P1 seam (already declared; restated for the box
migration's call sites):

```teko
/**
 * store_u64 — write the word `v` at the raw address `addr` (P1). It does NOT ALLOCATE — the property that
 * breaks the arena's self-reference circularity (`arena-em-teko.md` §2 P1): the arena's own bookkeeping fits
 * in the memory it administers. Lowers to a single `LStore`, the same instruction every struct-field write
 * emits. The slice/box migration uses it to write boxed elements into the current region.
 *
 * @param addr  the destination address, a `u64` ABI-identical to `void*` on a 64-bit target
 * @param v     the 64-bit word to write
 * @return      void
 * @since 0.3.1
 */
pub fn store_u64(addr: u64, v: u64)

/**
 * load_u64 — read the 64-bit word at the raw address `addr` (P1). It does NOT ALLOCATE. Lowers to a single
 * `LLoad`. The pair of `store_u64`; together the two are the ONLY compiler entries L1 (the arena) needs beyond
 * the `ARENA_CONTROL` slot. `tk_mem_copy`'s index-join copy walks with this pair.
 *
 * @param addr  the source address
 * @return      the 64-bit word at `addr`
 * @since 0.3.1
 */
pub fn load_u64(addr: u64): u64
```

Reused (do NOT redeclare): `region_enter(child)`/`region_leave()` (`arena.tks:712,716` — the ONE shared
primitive, `modelo…` §14), `region_alloc`/`region_drop_subtree` (`arena.tks:685,700`), and the `ResidencePlan`
the boxing consumes as any user code (`migracao…` §4.4).

## Rulings & laws

- **Teko-only:** L1 bodies land in `src/runtime/{arena,teko_rt}.tks`; the maintained-C exception is the BRIDGE
  the campaign retires (`migracao…` §1.4/R1). The `teko_rt.c` slice/box + `tk_alloc` C goes DEAD; deletion is
  `0095` RM-C9 (M3).
- **W15 full Javadoc** on every touched declaration; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** removes no surface (slice/box names persist); kills the C bodies'
  role. Physical `teko_rt.{c,h}` deletion is `0095` RM-C9, clean and tombstone-free.
- **Residence law (`modelo…` §0/§4/§14, `migracao…` §4.1/§4.4):** the boxing routes to the current region via
  `_r`, NEVER root; the arena control block is the sole legitimate root residence (`#singleton`-like). A
  boxing that allocates in root reopens the closed leak — REJECTED.
- **Arena-correctness (`arena-em-teko.md` §6, `migracao…` R7):** a subtly-wrong arena is WORSE than none — it
  passes tests and corrupts every emitted program including the compiler. Mutation gates + `TEKO_MEM_PARANOID`
  (survives ASan's death) are mandatory.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after any signature change.

## Fixtures

L1 is core-consumed by every allocation, so the happy path is fixpoint-covered; the isolated oracles are the
mutation gates + the residence boundary the guard alone would not localise:

| fixture | asserts | expected |
|---|---|---|
| `l1_box_residence` | a `[]byte` built in a runtime scope dies WITH the scope (bumped into the current region, not root) — probe reads the region high-water | `0` |
| `l1_slice_push_grow` | repeated `slice_push_r` grows by boxing into `r`, no whole-backing swap, `TEKO_ARENA_OBS` scoped>0 | `0` |
| `l1_mem_copy_index_join` | `mem_copy(dst, at, src)` over the P1 load/store seam copies 16 bytes correctly, no C `memcpy` symbol | `0` |
| `l1_paranoid_no_uaf` | the mutation gate under `TEKO_MEM_PARANOID`: a dropped region's addresses are poisoned, a stale read faults | `0` |

## Gate

`[fixpoint]` — build gen2 + the scoped fixtures + `gen2==gen3` byte-identity. "Green" = alloc/arena/regions are
the Teko arena, slice/box boxing routes to the current region (residence honored), the mutation gates pass, no
`tk_alloc`/`tk_slice_*` C symbol is on the path, and the emitted `teko.c` is byte-identical to before the swap.
**Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`S16-MM-L2` (`0054` — the compiler's own runtime IS the Teko-over-mmap arena, load-bearing: the alloc/region
seam L1's slice/box bumps through).

## Done when

Allocation, the arena/region tree, and slice/box boxing run in Teko over the Teko-over-mmap arena honoring the
ResidencePlan (mutation gates + paranoid oracle green), no C alloc/slice symbol is on the path, the fixtures
exit `0`, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
