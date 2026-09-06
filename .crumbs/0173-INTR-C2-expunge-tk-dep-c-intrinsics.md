---
seq: 0173
crumb-id: INTR-C2
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [INTR-C1]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:6"        # §6 nature 1
  - "DECISION_LOG.md:1155"                                       # D134 nature 1 (tk_* dep-C)
  - "src/codegen/codegen.tks"                                    # 90 recognized names
  - "src/lir/lower.tks"                                          # 32 recognized names
---

# 0173 · INTR-C2 — expunge nature-1 (`tk_*` dep-C) intrinsics → Teko/surface

> Nature 1 of the wave (D134): every backend-recognized intrinsic that lowers to a `tk_*` C call is a
> dep-C of the same class as the runtime — expunge it to Teko surface (or delete a dead one). Reduces the
> C dependency; each site is bisectable. Byte-mover (emission changes) → fixpoint; ratchet DOWN (removing
> C helpers should not grow, ideally shrink, the peak).

## Goal

For each nature-1 intrinsic (INTR-C1 census): route it to a real Teko surface fn (raw syscall / ABI /
pure Teko) or delete it if dead (D125-style presweep). The auditor found ~18 formatters already have a
dead `tk_*` overridden to Teko (`codegen.tks:3870`) — those are cleanup-only. The live nature-1 calls
migrate to surface. Staged per family (bisectable), fixpoint as the guard, ratchet each step.

## Where

- `src/codegen/codegen.tks` — the nature-1 recognized names (from INTR-C1); reroute to `teko::…` surface
  or remove dead entries.
- `src/lir/lower.tks` — the native-side nature-1 names (32 recognized) — the SAME logic, native emission.
- `src/runtime/*.tks` — the surface fns the intrinsics reroute to (transcribe from C if genuinely needed;
  NO new `from "teko_rt"` — D125).

## How

1. Per nature-1 family (INTR-C1 order), replace the `tk_*` recognition with the Teko surface call.
2. Where the `tk_*` is already dead (overridden formatters), delete the recognition (presweep).
3. Both routes: codegen-C (exercised now) AND lower.tks native (written, not run) — same logic (D134
   permanent seat = lower.tks).
4. Fixpoint per family harvest; measure peak (ratchet down/non-grow).

## Rulings & laws

- **Teko-only + NO new `from "teko_rt"` (D125):** transcribe to Teko, do not patch C.
- **D134 Opção-3:** nature-1 must not depend on libc/magic after migration; if it still does, redo.
- **Own wave, not tangled with region byte-mover.**
- **W15 full Javadoc; no `//`.** **NO tombstone** for removed dead intrinsics.
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; build gen2, `gen2==gen3`. Ratchet: DOWN
  strict (removing C → peak must not grow).

## Fixtures

`none — the intrinsics are exercised by the self-build (the compiler emits/uses them); the fixpoint proves
behavior-preservation. A dead-removal that breaks the link proves it was NOT dead (D125)`.

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` per family. "Green" = nature-1 intrinsics route to surface (or
dead ones removed), no libc/magic residue, `gen2==gen3`, peak down/flat. Reseed-class: `fixpoint-rebuild`.

## Deps

`INTR-C1`

## Done when

Every live nature-1 `tk_*` intrinsic routes to Teko surface (dead ones removed), no libc/magic residue,
`gen2==gen3` with peak not grown.
