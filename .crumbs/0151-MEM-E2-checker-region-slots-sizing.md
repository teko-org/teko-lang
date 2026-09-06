---
seq: 0151
crumb-id: MEM-E2
milestone: M5
gate: "[dry]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-E0a]
sources:
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:120-175"      # Idea 1 — AST-computed floor
  - ".crumbs/0108-D1-T1-arena-pre-sizing.md"                          # arena_floor (this extends it)
  - "DECISION_LOG.md:1162-1163"                                       # D130 refinements 3/4 (compile-time sizing, slots==0)
  - "src/checker/type.tks"                                            # layout_of for slot widths
---

# 0151 · MEM-E2 — checker: `region_slots(scope)` compile-time sizing + `scope_slot_count` (no consumer)

> The analysis half of D130 refinement 3 (compile-time dimensioning eliminates `#arena_size`/
> `#arena_depth`) and refinement 4 (`slots==0` ⇒ elide). Extend `arena_floor` (`0108`) into
> `region_slots(scope): usize` — the peak of simultaneously-live slots + their sizes, the number the
> compiler fixes the arena to AT INITIALIZATION — plus `scope_slot_count(scope): usize` for the elision
> predicate. Pure analysis, NO consumer yet ⇒ byte-identical.

## Goal

Under NO-PUSHES every array is bound-exact / pre-sized, so a scope's arena demand is a compile-time fact:
the SUM of the fixed-width slots live SIMULTANEOUSLY at the peak point (a union slot takes its LARGEST
alternative). `region_slots` computes that peak (the initial-chunk size the arena is born at, refinement
3); `scope_slot_count` computes whether the scope allocates ANYTHING routable (0 ⇒ elide, refinement 4).
Runtime-sized allocations (`var x: [n]T = []`, n runtime) contribute their HEADER to the floor and grow
the chunk-list beyond it (never UAF; over-floor leak-safe) — so "sizing" is the exact initial floor, not
a hard cap; stated honestly. Result type is `usize` (E0a). NO consumer here — `MEM-W1`/`MEM-W2` consume.

## Where

- `src/checker/residence.tks` (or a sibling `sizing.tks`) — ADD `region_slots`/`scope_slot_count`,
  extending the `arena_floor` design (`0108`), walking the scope's statements with `layout_of` widths.
- `src/checker/type.tks` — `layout_of` for slot byte-widths; union → largest alternative.

## How

```teko
/**
 * region_slots — the compile-time size (in bytes) an arena for `scope` is born at: the PEAK of
 * simultaneously-live routable slots and their `layout_of` widths (a union slot takes its LARGEST
 * alternative). Under NO-PUSHES every allocation is bound-exact, so this peak is a static fact — the
 * number D130 refinement 3 uses to fix the arena at INITIALIZATION, retiring the manual `#arena_size`.
 * A runtime-sized slot (`[n]T`, n runtime) contributes its header only; the chunk-list grows past the
 * floor at runtime (never a UAF — overflow is structurally impossible; over-floor is leak-safe). So the
 * value is the exact INITIAL floor, not a hard cap.
 *
 * @param body   the scope's statement block
 * @param table  the checker type table (for `layout_of` widths)
 * @return       the peak simultaneous-live byte size (0 = the scope allocates nothing → elide, W1)
 * @since 0.3.1
 */
fn region_slots(body: []checker::TStatement, table: checker::TypeTable): usize

/**
 * scope_slot_count — the count of routable allocation sites a scope makes (push/box/struct-init/
 * array-lit/str-concat/tk_alloc). 0 ⇒ the scope touches no arena ⇒ D130 refinement 4 elides its region
 * entirely (the parent's region passes straight through). The CONSERVATIVE posture (doubt ⇒ count > 0,
 * i.e. do NOT elide) mirrors `escape.tks:9-12` — a missed site routes to the enclosing region
 * (leak-safe), never UAF.
 *
 * @param body   the scope's statement block
 * @param table  the checker type table
 * @return       the number of routable allocation sites (0 = elidable)
 * @since 0.3.1
 */
fn scope_slot_count(body: []checker::TStatement, table: checker::TypeTable): usize
```

1. `region_slots`: peak simultaneous-live analysis (open/close a slot at its first/last use within the
   scope), summing `layout_of` widths at the peak; union → largest member.
2. `scope_slot_count`: count routable sites; conservative (doubt ⇒ non-zero).
3. No consumer ⇒ byte-identical `[dry]`.

## Rulings & laws

- **Teko-only:** `src/checker/*.tks`.
- **Recover/extend `0108`:** this is `arena_floor` grown to a peak + a count; folds the manual
  `#arena_size` path (removed in `MEM-W2`).
- **Honest bound:** the floor is the initial chunk, not a cap; runtime-sized allocs chunk-grow.
- **Conservative elision:** doubt ⇒ count > 0 (never wrongly elide → never UAF).
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

none — no consumer; the fixpoint proves byte-identity. The numbers are exercised by `MEM-W1`/`MEM-W2`
(their ritual gates + the `mem_*` shadow fixtures `mem_elide_leaf`/`mem_loop_per_iter`).

## Gate

`[dry]` — compile + fixpoint (byte-identical; no consumer). "Green" = `region_slots`/`scope_slot_count`
compile and are byte-neutral. Reseed-class: `fixpoint-rebuild` (folds into RESEED-1 of `MEM-E5`).

## Deps

`MEM-E0a` (the `usize` return type).

## Done when

`region_slots(scope): usize` (peak simultaneous-live + sizes, union→largest) and `scope_slot_count(scope):
usize` (routable-site count, conservative) exist, no site consumes them yet, and `[dry]` is byte-identical.
