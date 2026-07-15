# Wave D — own-backend finalization (D1 + D2)

**Status:** in progress (opened 2026-07-15, base `remodel/backend-build` @ 0.3.0.20).

This wave lands **Phase D** of `own-backend-architecture.md` §4 — the finalization crumbs that turn
the already-built own AOT backend (Phases A/B/C: lowering, isel, regalloc, encoders, object files,
the C-vs-own differential) into a first-class, user-selectable backend with a full 3-way parity gate.

## Three-level structure (debut)

This is the first wave cut under the owner's three-level PR structure (2026-07-15):

```
umbrella   remodel/backend-build → main
   └─ wave  wave/backend-d → remodel/backend-build          ← this branch / its PR
        └─ WIP  feat/<crumb> → wave/backend-d               ← the sub-packages below
```

Each crumb is a sub-package PR into `wave/backend-d`, carrying its own CI. The wave branch
accumulates the validated crumbs; the wave PR is the unit the integrator consolidates and version-
bumps before it rides up into the umbrella. WIP red stays contained at the sub-package level.

## Crumbs

### D1 · N7 — `--backend={c,native}` selection flag (issue #390)

The real CLI flag, threaded **argv → `Manifest.backend` → artifact dispatch**, making it the PRIMARY
backend selector and retiring `TEKO_BACKEND=native` to a lower-precedence fallback (the differential
gate scripts keep driving the own backend through the env seam unchanged).

- Selection precedence: `native = flag_native OR (flag_absent AND TEKO_BACKEND == "native")`.
- Default `Backend::C` → a flagless build is byte-identical (FIXPOINT preserved), purely additive.
- Design: `own-backend-architecture.md` §3.7 + §4 D1. Reference: the retired stale-base PR #569
  (re-implemented fresh here atop the 0.3.0.20 arg-threading, which diverged too far to rebase).

### D2 · N8 — the 3-way differential CI + retire/DWARF decisions (issue #225)

The VM == C-native == own-native exit/stdout parity gate across the target matrix, plus the
retire/DWARF decisions collected here. **Design-open** — the retire and DWARF calls need an owner
ruling before implementation; this crumb starts with an architected proposal, not code.

## Bootstrap note

D1 adds no new intrinsic/builtin — it parses a flag through existing `env`/`args` seams — so it is
self-hosting the moment it lands (no staged-off wall). D2's CI work is host-side (scripts/workflows),
also no bootstrap wall.
