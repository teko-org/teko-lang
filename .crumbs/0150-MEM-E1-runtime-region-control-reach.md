---
seq: 0150
crumb-id: MEM-E1
milestone: M5
gate: "[dry]"
reseed-class: "fixpoint-rebuild"
deps: []
sources:
  - "docs/design/plano-fiacao-modelo-memoria-por-escopo-0.3.1.md:§3"     # control reachability via region
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:454-475"          # §9 tk_str_concat_r precedent
  - "DECISION_LOG.md:1155-1164"                                          # D130 refinements 1/6 (param, root in _start)
  - "src/runtime/arena.tks:291"                                          # ar_control() thread-local veneer
  - "src/runtime/arena.tks:769-947"                                      # region_enter/leave/ret_dest ambient
---

# 0150 · MEM-E1 — runtime: `region_control(r)` reachability + region-aware entries (ambient intact)

> Add the Teko-side runtime pieces the param-threaded model needs, ADDITIVELY, WITHOUT touching the
> ambient path yet: `region_control(r)` (reach the control-block from any region by walking to the root),
> so the SWEEP (`MEM-W6`) can replace `ar_control()` with `region_control(<region-param>)`; and any
> region-explicit alloc/return entry points the consumers call. Ambient (`region_enter`/`leave`/
> `ret_dest`/`ar_control`) STAYS live — it dies in the sweep. No caller yet ⇒ non-emitted ⇒ byte-identical.

## Goal

D130 refinement 1 makes the region an implicit PARAMETER (not the ambient `_Thread_local`); refinements
2+6 make the region carry/reach the control-block (root born in `_start` owns it; children reach via the
parent chain). This crumb lands the ONE runtime primitive that makes the ambient replaceable —
`region_control(r)` — plus confirms the region-explicit alloc entry (`ar_region_alloc_w` already takes
the region; a bare `region_alloc(r, n)` wrapper if the consumers need it). The 40 `ar_control()` sites
are a thin veneer over `ar_*_w(control, …)` that already takes control explicitly, so the sweep is
mechanical. This crumb adds; it removes nothing. **Verified:** all 40 `ar_control()` live in `arena.tks`.

## Where

- `src/runtime/arena.tks` — ADD `region_control(r: ptr): uptr` (walk the region's parent chain to the
  root, read its control word). The region already links to its parent (`ar_region_new_w(parent)`); the
  root holds the control. ADD a bare `region_alloc(r: ptr, n: usize): ptr` wrapper over `ar_region_alloc_w`
  if a consumer wants a region-explicit alloc without going through `ar_control()`.
- `src/runtime/arena.tks:769-947` — `region_enter`/`region_leave`/`set_ret_dest`/`ret_dest` — NOT edited
  here (ambient stays; retired in `MEM-W4`/`MEM-W6`).

## How

```teko
/**
 * region_control — the control-block reachable from any region by walking its parent chain to the root
 * (the root, born in `_start`, OWNS the control). Replaces the ambient `ar_control()` `_Thread_local`
 * first-touch (D130 refinement 6): once the region is threaded as a parameter, every task-global
 * accessor (program-region, intern, names, panic, environ) reads its control via
 * `region_control(<region-param>)` instead of a thread-local read. Added here with no caller (the
 * sweep `MEM-W6` reroutes the accessors); non-emitted, so byte-identical.
 *
 * @param r  any region in the task's region tree (a `ptr`/`uptr` handle)
 * @return   the control-block word for the task that owns `r`
 * @since 0.3.1
 */
fn region_control(r: ptr): uptr
```

1. Implement `region_control` by walking `ar_region_parent_w` to the root, reading the root's control
   slot. (If the root's control is stored at region-tree construction — `MEM-W6` sets it in `_start` —
   this is a pure read; until then it can delegate to `ar_control()` so the teaching build stays inert.)
2. Add `region_alloc(r, n)` wrapper if consumers need it (else they call `ar_region_alloc_w`).
3. Confirm no caller ⇒ non-emitted ⇒ byte-identical `[dry]`.

## Rulings & laws

- **Teko-only + arena-is-Teko (D128):** `arena.tks`; NO `teko_rt.c` patch. The region primitives stay
  the maintained-C exception (D90) but `region_control` is Teko over the Teko arena.
- **No ambient removal here:** ambient coexists through teaching; dies in the sweep (`MEM-W4`/`W6`).
- **Additive/inert:** no caller → non-emitted → gen2==gen3 byte-identical.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

none — additive with no caller; the fixpoint self-build proves byte-identity. `region_control`'s behavior
is exercised only once the sweep reroutes accessors (`MEM-W6`), whose ritual gate covers it.

## Gate

`[dry]` — compile + fixpoint (byte-identical; no caller). "Green" = `region_control`/`region_alloc`
compile, nothing is emitted for them, `[dry]` byte-identical. Reseed-class: `fixpoint-rebuild` (folds into
RESEED-1 of `MEM-E5`).

## Deps

`—` (batches with E0a/E0b/E2/E3).

## Done when

`region_control(r)` (and a bare `region_alloc` if needed) exist in `arena.tks` (Teko), the ambient path is
untouched, no caller references them yet, and a `[dry]` build is byte-identical.
