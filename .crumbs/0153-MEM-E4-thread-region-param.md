---
seq: 0153
crumb-id: MEM-E4
milestone: M5
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-E0b, MEM-E1]
sources:
  - "docs/design/ast-computed-arena-assessment-0.3.1.md:234-323"      # Idea 3 DPS — hidden dest arg
  - "DECISION_LOG.md:1155-1156"                                       # D130 refinement 1/2 (region = param, not ambient)
  - "src/codegen/codegen.tks:134-135"                                 # set_ret_dest/ret_dest CgArenaSym
  - "src/runtime/arena.tks:941-947"                                   # the ambient ret_dest to be superseded
  - "src/lir/lower.tks"                                               # LowerCtx / call ABI
---

# 0153 · MEM-E4 — thread the implicit region PARAMETER through both backends (null-defaulted)

> The mechanism switch D130 refinement 1 mandates: the region comes as an IMPLICIT PARAMETER (DPS-style
> hidden arg), NOT the ambient `_Thread_local` current-stack. Thread a single implicit region param
> (a `ptr`) through EVERY emitted function on both routes, **null-defaulted** — `null` selects today's
> ambient path, so the teaching build is byte-identical. The FLIP that makes callers pass a real region
> and callees use it is the sweep (`MEM-W4`). Byte-mover (signatures change) but deterministic → fixpoint.

## Goal

Refinement 1 rejects the ambient current-stack (`region_enter`/`leave`/`ret_dest` over `ar_control()`):
the caller's region is threaded as a hidden parameter — the SAME parameter serves both as the region the
callee opens its child from AND as the destination the return MOVES into (unifying the assessment's
`ret_dest` with the caller-region: ONE param, not two). This crumb ADDS the param to every emitted
function signature (C route: a hidden leading arg; native: a `LowerCtx` field + a call-ABI arg),
DEFAULTED to `null`. When `null`, emission is byte-identical to today (the ambient path still runs). No
routing decision consumes the param yet — the flips are `MEM-W1..W6`. The param type is the opaque `ptr`
(E0b), not a raw `u64`.

## Where

- `src/codegen/codegen.tks` — emitted C function signatures gain a hidden leading `tk_region *` param;
  callers pass `NULL` (byte-identical: the callee still reads the ambient current when the param is null).
  `region_from_param(ctx)` returns the param or falls back to the ambient current.
- `src/lir/lower.tks` — `LowerCtx` gains a `region_param` VReg field (twin of the `ret_dest` plumbing);
  the call ABI gains the hidden arg; `ctx_with*` propagate it; `lower_function` literal + `lower_test.tkt`
  literals default it.
- `src/runtime/arena.tks:941-947` — `set_ret_dest`/`ret_dest` STAY (ambient coexists); retired in
  `MEM-W4`.

## How

```teko
/**
 * region_from_param — the region a function allocates its child from and moves its return into: the
 * implicit region PARAMETER (D130 refinement 1) when the caller passed one, else the ambient current
 * (today's path). During teaching the param is `null` at every call site, so this returns the ambient
 * current and emission is byte-identical; the sweep (`MEM-W1..W6`) makes callers pass a real region and
 * this returns it. The param is the opaque `ptr` (E0b), NOT a raw `u64` — region handles are opaque.
 *
 * @param ctx  the emit/lowering context (its region param slot)
 * @return     the caller-provided region param, or the ambient current when the param is null
 * @since 0.3.1
 */
fn region_from_param(ctx: EmitCtx): ptr
```

1. Add the hidden param to both routes' function signatures + call sites, default `null`/`NULL`.
2. `region_from_param` returns the param or the ambient current (null → ambient → byte-identical).
3. Thread the field through the five `ctx_with*` + the `LowerCtx`/`lower_test.tkt` literals (the same
   chore the `table` field took, `git 55c2c890`).
4. Confirm gen1≠gen2 (signatures changed) but gen2==gen3 (deterministic) and byte-identical RUNTIME
   behavior (null → ambient).

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`; the runtime ambient is untouched (dies in the sweep).
- **D130 refinement 1:** region = implicit PARAMETER; NO thread-local, NO `global var`, NO tid-table.
  This crumb THREADS it; the ambient stays only as the null-default fallback until the sweep.
- **Byte-preserving RUNTIME (null → ambient):** the fixpoint is self-consistency (gen2==gen3); the
  signature change is deterministic. Opaque `ptr` param (E0b), never a raw word.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`;
  `[fixpoint]` gen2==gen3; MEM_PARANOID 0; sweep `.tkt` after the `LowerCtx` field.

## Fixtures

none — the param is null-defaulted (no behavior change); the fixpoint self-build is the proof (thousands
of emitted functions gain the param and still self-compile byte-identically). The param's USE is
exercised by `MEM-W4`'s ritual.

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-identity (native-object where the native route builds; the
C route validates fully) + MEM_PARANOID 0. "Green" = every emitted function carries the null-defaulted
region param, `region_from_param` falls back to ambient, RUNTIME byte-identical, `gen2==gen3`.
Reseed-class: `fixpoint-rebuild` (harvested together at RESEED-1 with `MEM-E5`).

## Deps

`MEM-E0b` (the opaque `ptr` param type), `MEM-E1` (`region_control` for the sweep to build on).

## Done when

Both backends thread a single implicit region `ptr` param through every emitted function, null-defaulted
to the ambient path, `region_from_param` returns the param-or-ambient, no routing consumes it yet, and
the `[fixpoint]` build is `gen2==gen3` with MEM_PARANOID 0.
