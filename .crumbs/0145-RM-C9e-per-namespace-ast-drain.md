---
seq: 0145
crumb-id: RM-C9e
milestone: M3
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C9d]
sources:
  - "docs/design/arena-terminal-zero-libc-0.3.1.md:34-38,92-105"
  - "DECISION_LOG.md:1171-1173"
  - "src/build/project.tks:1911-1939,2043-2054"
---

# 0145 · RM-C9e — per-namespace AST drain (the reclaim: peak DROP)

> The migration campaign floated the self-compile peak up because the typed AST stays all-resident;
> drain each namespace's AST into its own region and drop it after that unit emits — the peak falls.

## Goal

Per D118-ctx (`DECISION_LOG.md:1171`) moving runtime C into Teko makes `teko` compile it, and the
typed AST is held all-resident "sem drenar por-unidade até a arena RM-C9" — so each migration adds
~0,12%/lote to the peak (D118, accepted, reclaimed HERE). The native fused-emit path ALREADY drains
per-LFunc (`project.tks:1911/1930/2043`, region_new→enter→…→region_drop). This crumb applies the SAME
discipline to the C-leg self-compile driver: wrap each namespace/compilation-unit's check→emit in an
arena checkpoint (or a child region) dropped after that unit's C text is emitted, so a namespace's
typed-AST memory is reclaimed before the next unit is built instead of accumulating. Uses ONLY landed
machinery (`arena_push`/`arena_pop`, or `region_new`/`region_enter`/`region_leave`/`region_drop`). This
is THE reclaim crumb — the measured peak DROP that realizes RM-C9's "também reclama memória".

## Where

- `src/build/project.tks` — the C-leg emit driver (`ffi_emit_c_file` path, `:1241` and the codegen
  loop it calls) — wrap the per-namespace check→emit in an `arena_push()` / `arena_pop()` bracket (or a
  `region_new(region_root())` child dropped per unit), so the unit's typed AST + scratch is released
  after its bytes are emitted.
- `src/codegen/codegen.tks` — the top-level program emit loop — if emit is currently one pass over the
  whole `TProgram`, restructure to emit namespace-by-namespace so a checkpoint can bracket each unit.
- `src/runtime/arena.tks:949-959` — `arena_push`/`arena_pop`/`arena_commit` (landed) — the drain
  primitive; no change, just the new caller.

## How

1. Confirm the emit order groups items by namespace (or sort a working index by namespace) so a unit's
   AST/scratch has a clean lifetime boundary.
2. Bracket each unit. Preferred (checkpoint, cheapest — rewinds the root chunk watermark):

```teko
teko::runtime::arena_push()
emit_namespace_unit(prog, ns, out)
teko::runtime::arena_pop()
```

   Anything that must OUTLIVE the unit (the accumulated output text, cross-unit tables) is written to
   the caller's region / the program region BEFORE `arena_pop` (the arena's move-on-return / DPS
   discipline), so the pop only reclaims the unit's transient AST + scratch, never live output.
3. Verify no live pointer into a dropped unit is read afterward (UAF is the dev's design
   responsibility, `DECISION_LOG` NO-PUSHES arena law; the self-compiling compiler knows its own
   lifetimes → never dereferences a dropped unit).
4. **MEASURE:** dry-build `teko: memory: peak <N> MB` before and after. Expect a STRICT fall (D68) —
   this recovers the migration float-up. Report the delta at the reseed.

## Rulings & laws

- **Arena reclaim (D118-ctx):** RM-C9 is where the peak falls; the ratchet D68 applies to the FINAL
  post-RM-C9 state — this crumb must show a strict drop.
- **D68 RATCHET:** only a measured fall lands; flat/rise = regression → root-cause before landing. This
  is a REDUÇÃO crumb, not an expurgo-floor crumb → strict fall required.
- **NO PUSHES / purge-on-reassign:** the output buffer is the pre-sized / DPS-returned form, never a
  growing accumulator inside a dropped unit.
- **Teko-only; W15:** driver fns are `pub`/private → no doc.
- **Safety:** NEVER `teko test .`; subshell `ulimit -v 4718592`; reseed ONLY at this [RITUAL];
  `gen2==gen3` byte-identical (the drain is behavior-transparent — same emitted text, less resident
  memory); leave gen2/gen3 in scratchpad; measure and REPORT the peak delta.

## Fixtures

none — the fixpoint self-build IS the exercise (it emits every namespace); correctness = `gen2==gen3`
byte-identical AND a lower measured peak. A divergence surfaces as a fixpoint break or a UAF panic, not
a missed fixture.

## Gate

`[RITUAL]` — full native ladder + a fixpoint-rebuild reseed. "Green" = per-namespace drain lands, the
emitted `teko.c` is byte-identical (`gen2==gen3`), and the dry-build `peak` falls strictly vs the 0144
baseline. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C9d` (the arena path fully teko_rt-free, so the drain runs entirely on the Teko arena).

## Done when

The C-leg driver drains each namespace's AST per unit, `gen2==gen3` byte-identical, and the measured
dry-build peak is strictly lower than before (the migration float-up reclaimed).
