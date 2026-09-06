---
seq: 0157
crumb-id: MEM-W2
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-W1]
sources:
  - "DECISION_LOG.md:1162"                                            # D130 refinement 3 (compile-time sizing removes #arena_*)
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:120-175"     # Idea 1 (the floor)
  - "src/checker/residence.tks"                                       # region_slots (MEM-E2)
  - "src/codegen/codegen.tks:9832"                                    # #arena_size presize (to REMOVE)
  - "src/parser/parse_decl.tks"                                       # #arena_size/#arena_depth attribute parse (to REMOVE)
---

# 0157 · MEM-W2 — pre-sizing from `region_slots`; REMOVE `#arena_size`/`#arena_depth` surface

> Consume `region_slots` (MEM-E2): each opened region is born at its COMPILE-TIME size, not a fixed
> 64 KiB default. D130 refinement 3: because the compiler now computes the peak simultaneous-live slots +
> sizes, the MANUAL `#arena_size` (floor) and `#arena_depth` (flattening) attributes are REDUNDANT and
> REMOVED from the language. Byte-mover (presize constants change) → fixpoint.

## Goal

Refinement 3: the AST number IS the arena size, set at initialization. A region opened by `MEM-W1` (when
`slots>0`) is sized via `region_slots(scope, table)` instead of pulling a default 64 KiB. Since the
compiler computes the size, the two manual knobs lose their reason to exist: `#arena_size(N)` (manual
floor) and `#arena_depth(N)` (manual flattening; the default depth-1 + elision already handle
granularity) are DELETED from the surface (parse + checker + codegen). A runtime-sized slot still
chunk-grows past the floor (never UAF; over-floor leak-safe) — "sizing" is the exact initial floor.

## Where

- `src/codegen/codegen.tks` (region-open, ~`3167`/`4758`/`6230`) + `src/lir/lower.tks` (native
  region-open) — pass `region_slots(scope, table)` (as `usize`) to the sized region-new instead of the
  64 KiB default.
- `src/codegen/codegen.tks:9832` — DELETE the `#arena_size` presize path.
- `src/parser/parse_decl.tks` — REMOVE the `#arena_size`/`#arena_depth` attribute parse.
- `src/checker/*` — REMOVE any `#arena_size`/`#arena_depth` checking / AST fields.
- Fold `0108 D1-T1` (`arena_floor`) — it is SUBSUMED by `region_slots` (the peak, not just the floor).

## How

1. Route the sized region-new through `region_slots` (the computed peak) on both engines.
2. Delete `#arena_size`/`#arena_depth`: parse, AST field, checker, codegen presize. A program using them
   now fails to parse (the new rejection — `EXPECT_COMPILE_FAIL` oracle).
3. Confirm the chunk-list still grows a runtime-sized alloc beyond the floor (unchanged).

## Rulings & laws

- **Teko-only:** parser/checker/codegen/lir `.tks`.
- **D130 refinement 3:** compile-time sizing is the SOLE sizing; `#arena_size`/`#arena_depth` REMOVED
  (surface removal, like `-> ref T`).
- **Floor is the initial chunk, not a cap (§3.2):** runtime-sized allocs chunk-grow; sub-floor never
  UAF; over-floor leak-safe.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[RITUAL]` gen2==gen3 + MEM_PARANOID + the RSS ratchet (this is a REDUCTION — expect peak DOWN, D68).

## Fixtures

- Shadow (scratchpad): the `mem_*` peak/tail-waste drop is measured via `TEKO_ARENA_OBS`.
- Rejection oracle (allowed): `arena_size_attr_removed` — `#arena_size(N)` / `#arena_depth(N)` no longer
  parse → `EXPECT_COMPILE_FAIL` (the surface-removal proof).

## Gate

`[RITUAL]` — gen2==gen3 (presize constants deterministic) + MEM_PARANOID 0 + RSS peak DOWN (ratchet).
"Green" = regions are born at `region_slots`, `#arena_size`/`#arena_depth` are gone (reject), tail-waste
drops, `gen2==gen3`. Reseed-class: `fixpoint-rebuild` (sweep; harvested at RESEED-FINAL `MEM-W6`).

## Deps

`MEM-W1` (the region-open decision it sizes).

## Done when

Each opened region is born at its `region_slots` compile-time size, `#arena_size`/`#arena_depth` are
removed from the language (reject), runtime-sized allocs still chunk-grow, peak/tail-waste drop, and the
ritual gate is green under MEM_PARANOID with the RSS ratchet satisfied.

## Region-type reconciliation (D149 / crumbs 0183-0184)

The "sized region-new" this crumb routes `region_slots` into **IS `Region.child_sized(floor: usize)`**, the
method born in `0183 MEM-ARENA-TYPE` — NOT a loose `region_new_sized(parent_u64, floor)` over `u64`. This is
the D148 "W2 refaz" resolution: W2 was stopped for reaching into `teko_rt.c` to build a sized region-new;
the correct form is a METHOD on the `Region` type (zero C, `arena.tks` only), which requires the type to land
first. **Reordered dep: `0183 MEM-ARENA-TYPE` lands before this crumb** (it carries `child_sized`). The
`#arena_size`/`#arena_depth` surface removal (parse/checker/codegen) is unchanged. See
`docs/design/arena-region-tipo-com-metodos-0.3.1.md §4`.
