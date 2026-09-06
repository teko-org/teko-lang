---
seq: 0005
crumb-id: SM-A4
milestone: M1
gate: "[RITUAL]*"
reseed-class: "(folds R1)"
deps: ["SM-A1"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:78-84"    # §1.2 arena elision
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1164-1165"# §10 Phase A — A4
---

# 0005 · SM-A4 — arena elision (`scope_touches_arena` guard)

> Arena elision (`scope_touches_arena` guard) — skip the region for alloc-free leaf scopes.

## Goal

A `scope_touches_arena(body): bool` predicate ELIDES the region for alloc-free leaf scopes, saving the
64 KiB region floor per elided leaf. It guards `open_native_region`/`open_frame_region`
(`lower.tks:1619`/`:1392`) EXACTLY as the existing `bracket_depth > 0` skip already does
(`lower.tks:1637`) — same shape, new condition. It is INDEPENDENT of the DPS core (SM-A2/A3), composes
with it, and can land in parallel. **Conservative by law:** doubt → do NOT elide (route to the enclosing
region — leak-safe, never UAF). It moves bytes deterministically (removes region new/enter/leave/drop
for elided leaves), so `gen2==gen3` holds; its seed folds into SM-R1.

## Where

- `src/lir/lower.tks:1619` — `open_native_region` — gate on `!scope_touches_arena(body)`.
- `src/lir/lower.tks:1392` — `open_frame_region` — same gate.
- `src/lir/lower.tks:1637` — the existing `bracket_depth > 0` region-skip — the exact precedent shape to
  mirror; the elision arm sits beside it.
- NEW predicate `scope_touches_arena` — a leaf-scope allocation analysis in `src/lir/lower.tks`.

## How

1. **Add the predicate.**

```teko
/**
 * scope_touches_arena — true iff a scope's body performs ANY arena allocation (an array literal, an
 * aggregate materialization, a `region_alloc`, a DPS `alloc_call_dest`, or a call whose callee may
 * allocate into the current region). When FALSE for a leaf scope, the region is elided — no
 * `open_native_region`/`open_frame_region`, saving the 64 KiB floor. CONSERVATIVE: any doubt (an
 * opaque call, an unresolved capability) returns TRUE, so the region is kept and the values route to
 * the enclosing region — leak-safe, never UAF.
 *
 * @param body  the lowered scope body under analysis
 * @return      true iff the scope allocates into the arena (⇒ keep the region); false ⇒ safe to elide
 * @since 0.3.1
 */
fn scope_touches_arena(body: LBlock): bool
```

2. **Guard the region openers.** At `open_native_region` (`:1619`) and `open_frame_region` (`:1392`),
   add the elision arm beside the existing `bracket_depth > 0` skip (`:1637`): if the scope is a leaf
   AND `!scope_touches_arena(body)`, do not open the region; route any value to the enclosing region.
3. **Keep it conservative.** The predicate defaults to TRUE (keep the region) on any uncertainty — an
   opaque call, a capability whose allocation behavior is unknown, a non-leaf scope. Only a provably
   alloc-free leaf elides. This is the "doubt → do not elide" law (§1.2).
4. **Rebuild the native ladder + fixpoint.** The elision removes region ops deterministically; `gen2 ==
   gen3` must hold. Confirm the elided-leaf fixture runs correctly (no UAF, no leak).

## Rulings & laws

- **Teko-only:** `src/lir/lower.tks`; no C twin.
- **W15 full Javadoc** on `scope_touches_arena` and helpers.
- **Conservative-elision law (§1.2):** doubt → keep the region (enclosing-region route), never elide on
  uncertainty — leak-safe, never UAF. This is the arena §0 safety principle applied to elision.
- **Safety:** NEVER `teko test .`; native ladder + fixpoint in a subshell with `ulimit -v 6815744` (the
  64 KiB-per-leaf saving must be a real drop, not a masked overrun); commit the green step; SEED at
  SM-R1, not here (`[RITUAL]*`, `(folds R1)`).

## Fixtures

The self-build has many alloc-free leaf scopes, so the fixpoint exercises the elision path AND its
byte-identity; but the correctness oracle (an elided leaf still runs correctly) is worth an isolated
`.tkr`:

| fixture | asserts | expected |
|---|---|---|
| `arena_elided_leaf_scope` | an alloc-free leaf scope runs correctly with no region opened | 0 |

## Gate

`[RITUAL]*` — full native ladder + `gen2==gen3`. "Green" = elided leaves emit no region ops, `gen2 ==
gen3` holds, the elided-leaf fixture is exit `0`, and the region-floor volume drops. Reseed-class:
`(folds R1)`.

## Deps

`SM-A1` (the profiler baseline that measures the region-floor saving elision realizes).

## Done when

`scope_touches_arena` guards `open_native_region`/`open_frame_region`, provably alloc-free leaf scopes
elide their region (conservative on doubt), the native ladder is green with `gen2==gen3`, and
`arena_elided_leaf_scope` is exit `0`.
