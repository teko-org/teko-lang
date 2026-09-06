---
seq: 0146
crumb-id: RM-C9f
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [RM-C9e]
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:107-140"
  - ".crumbs/0095-RM-C9-terminal-c-root-removal.md:45-47,80-84"
  - "src/runtime/teko_rt.c:2019-2045,2236-2262"
---

# 0146 · RM-C9f — dead arena C-root removal (C7 two-legs)

> After the flips + fixpoint prove the arena path is Teko, delete the enumerated dead arena C roots —
> a separate reseed that proves they are genuinely unreferenced. NO tombstone.

## Goal

Once 0142-0144 flip the CONTROL word, the paranoid probe, and the u64 twins to Teko and the fixpoint is
green, the arena's C roots have no caller. This crumb REMOVES them (C7 "symbol deletion after reseed"
discipline — a separate reseed proves deadness, never folded into the migration crumb). What is
REMOVED: `tk_g_arena_control` + `tk_arena_control_get/set` (`teko_rt.c:2019-2032`), `tk_arena_paranoid`
(`:2038-2045`), and the L2-orphaned `tk_region_alloc`/`tk_region_new`/`_on`/`tk_region_drop`/`_subtree`/
`tk_region_root`/`_current`/`_program`/`tk_region_enter`/`_leave`/`tk_arena_push`/`_pop`/`_commit`/
`tk_region_register`/`_lookup`/`tk_regions_free_all`/`tk_chunk_*` + the u64 twins `tk_region_*_u`, plus
the now-dead free-bin/mark/canary machinery those used. What STAYS (maintained-C, other legs):
`struct tk_task`/`tk_g_current_task`/`tk_task_current`/`tk_task_begin/end/reset` (still back cov/test/
chan/fd + user-program spawn), `tk_panic`. The build-first method lets the self-compile ENUMERATE the
dead roots: remove → seed → the compile errs at any residual reference → each error is a removal site.

## Where

- `src/runtime/teko_rt.c:2019-2045` — `tk_g_arena_control`, `tk_arena_control_get/set`,
  `tk_arena_paranoid` — REMOVE.
- `src/runtime/teko_rt.c` — the arena block (`tk_region_*`, `tk_chunk_*`, `tk_arena_push/pop/commit`,
  `tk_region_*_u`, `tk_regions_free_all`, free-bin/mark/canary) with no remaining caller — REMOVE as
  the self-compile enumerates them.
- `src/runtime/teko_rt.h` — matching prototypes — REMOVE.
- KEEP: `struct tk_task` and the `tk_task_*` seam (`:1048/1088/1093/2236/2247/2250`) — still used by
  the cov/test/chan/fd `#define tk_g_*` block and user-program spawn; their removal folds into those
  legs + the concurrency expurgo, NOT here.

## How

1. Delete `tk_g_arena_control`/`tk_arena_control_get/set`/`tk_arena_paranoid` (their Teko replacements
   landed in 0142/0143).
2. Delete the arena bodies the L2 switch-over already routed past (codegen emits `TK_ARENA_*` →
   `teko_teko__runtime__*`, so no emitted C names them) and the u64 twins 0144 repointed.
3. **Seed; let the self-compile enumerate residuals.** Any remaining C reference errs raw at build —
   each error names a removal site (arena-em-teko method). Remove → seed → repeat until clean.
4. Keep the `tk_task` seam and `tk_panic`. Do NOT remove `tk_task_current`: the cov/test/chan/fd slots
   still deref it. A separate leg retires it when those close.
5. **NO tombstone:** removed C symbols cease to exist; no diagnostic references them (the user never
   knew `tk_g_arena_control`/`tk_region_alloc` existed).
6. Reseed to `gen2==gen3`.

## Rulings & laws

- **C7 two-legs (`plano-s16-arena-mmap.md:336-347`):** deletion is its OWN reseed after the migration
  is proven — the C arena is the fallback until 0142-0145 are green; delete only after.
- **Pré-sweep deadness proof (D125):** removing a dead body → the build still links/runs → PROVES it
  was dead; if it breaks, it was not dead (restore + find the caller).
- **Removals = clean expurgo, NO tombstone** (`plano-mestre:253-257`).
- **Maintained-C exception:** editing `teko_rt.c` to DELETE dead arena roots is allowed (D90's
  "editar teko_rt.c" ban is for MIGRATION-by-C-edit; cadaver deletion post-migration is the sanctioned
  pré-sweep/F9, D125).
- **Safety:** NEVER `teko test .`; subshell `ulimit -v 4718592`; reseed ONLY at this [RITUAL];
  `gen2==gen3`; leave gen2/gen3 in scratchpad.

## Fixtures

none — the fixpoint self-build + the link prove deadness (a live reference errs at compile/link, not a
missed fixture).

## Gate

`[RITUAL]` — full native ladder + expurgo reseed. "Green" = the enumerated dead arena C roots are gone,
`teko_rt.c` still compiles/links cross-OS, the `tk_task` seam + `tk_panic` remain, and `gen2==gen3`
byte-identical. Peak unchanged (removed C was never in the peak, D118-ctx). Reseed-class: `expurgo`.

## Deps

`RM-C9e`.

## Done when

The arena's dead C roots (control word, paranoid, `tk_region_*`, `tk_chunk_*`, `tk_arena_push/pop/
commit`, u64 twins, free-bin/mark machinery) are removed with no tombstone, the `tk_task` seam stays,
and `gen2==gen3`.
