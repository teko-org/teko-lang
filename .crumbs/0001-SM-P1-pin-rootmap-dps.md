---
seq: 0001
crumb-id: SM-P1
milestone: M0
gate: "[dry]"
reseed-class: "none"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:62-77"    # §1.1 DPS keystone
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1151-1156" # §10 Phase P — P1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1232-1256" # §11 DPS↔fixpoint sequencing
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1332-1339" # §14 R1 risk
---

# 0001 · SM-P1 — pin `type_match` + `frame_sweep_inst` + `push_inst_block` (the DPS go/no-go)

> Pin `type_match`+`frame_sweep_inst`+`push_inst_block` to the return/tail-merge vs self-append facet (DPS bet).

## Goal

This is the **go/no-go PIN** for the whole DPS-as-fixpoint-fix bet (§11). Three native-fixpoint
blockers remain on `fix/retirement`: `type_match`, `frame_sweep_inst` (both the by-address
aggregate-through-merge/return family) and `push_inst_block` (the self-append slice-header boundary).
This crumb does NOT change product code — it produces a **root-map pin** (minimal-repro `.tkr` + an
objdump reading) that classifies each of the three crashes into ONE of two facets:

- **return / tail-merge facet** — a `TExpr`/`FrameSet` returned through a tail `match`/`if` merge. If
  `type_match`+`frame_sweep_inst` pin HERE, then **DPS (SM-A2/A3) closes both by construction** (the
  value is born in the caller's arena; each tail arm lowers into the shared `ret_dest`) — the "two
  birds" path where the memory track and the fixpoint track are the SAME work.
- **payload-bind offset facet** — if they pin here instead, DPS still lands for memory + return
  correctness but does NOT close these two; the fixpoint track then decouples to the root-map
  cheap-pin-and-point-fix grind, sequenced independently of the surface wave.

`push_inst_block` is separately expected to pin to the **self-append / AL3 `grow_inplace`** boundary
(NOT DPS) — confirming that routes it to SM-A5. This crumb is pure diagnosis: the deliverable is the
pin verdict that gates whether SM-A2 is built as the fixpoint fix. It is byte-preserving (touches no
`src/`), reseed-class `none`.

## Where

No `src/` product edit. Investigation targets (read + objdump only):

- `src/lir/lower.tks:7245` — `lower_return`; `:7278` — `lower_return_fat`; `:10798` — the
  `lower_block_value`/`lower_match` tail merge — the suspected return/tail-merge site.
- `src/lir/lower.tks:11715` — `own_returned_value` (the retrofitted return box the pin decides DPS retires).
- `frame_escape.tks:56` — `frame_escape_guard` (the inversion net the return facet satisfies by construction).
- `push_inst_block` (grep the codegen/lir) — the self-append site; AL3 `tk_slice_grow_inplace` boundary.
- `type_match` and `frame_sweep_inst` — the two crash sites; objdump the faulting frame to read whether
  the offending offset is the return-slot conveyance or an interior payload bind.

Artifacts produced (NOT committed to `src/`): a minimal-repro fixture per blocker under
`examples/regressions/probes/` and the objdump pin note recorded in the drain report.

## How

1. **Build the current `fix/retirement` gen2 down the native route** in a subshell with the guard
   (`ulimit -v 6815744`), reproduce each of the three crashes with the smallest input that triggers it
   (a `TExpr` returned through a tail `match`, a `FrameSet` returned through an `if` merge, a
   self-appended inst block). Keep each repro a standalone `.tkr` probe.
2. **objdump the faulting frame** for `type_match` and `frame_sweep_inst`: read the faulting effective
   address against the frame layout. Classify: is the bad offset the **return-slot / tail-merge
   conveyance** (a value moved by-address through the merge into the return), or an **interior
   payload-bind offset** (a field bind unrelated to the return channel)?
3. **Record the verdict** for each blocker as one of `{return-facet, payload-bind-facet}`; for
   `push_inst_block` confirm `{self-append/AL3-boundary}`.
4. **Decide the fork (law-first, no HALT — this is engineering sequencing, §11/§14 R1):**
   - If `type_match`+`frame_sweep_inst` = **return-facet** → SM-A2/A3 are BUILT as the fixpoint fix
     (two-birds); the memory and fixpoint tracks fuse. Note this in the SM-A2 doc's precondition.
   - If **payload-bind-facet** → SM-A2/A3 still land for memory + return-correctness, but the fixpoint
     track reverts to the independent root-map grind (C5..Cn); the surface wave (G/S) proceeds on the
     C-route reseed regardless (it depends only on the C-route reseed, never on the native fixpoint
     being green).
   - `push_inst_block` → routed to **SM-A5** (point-fix, not DPS) either way; it MUST land + prove
     green pre-R1 so the reseed does not lock in a still-crashing native chain.

There is NO new Teko surface in this crumb — it authors no `fn`/`type`. The deliverable is the pin verdict.

## Rulings & laws

- **DESIGN/DIAGNOSIS ONLY** — no `src/` product edit; byte-preserving; the probes live under
  `examples/regressions/probes/` (already an established probe home). Teko-only law is not stressed:
  nothing is implemented.
- **Safety:** NEVER `teko test .`; each native repro build runs in a subshell with `ulimit -v 6815744`
  (6.5 GiB) — a blown guard is a root-cause fix, never a raised ceiling; commit the probes + pin note.
- **No reseed** — this is a `[dry]` leaf; reseed-class `none`.
- **§14 R1** (umbrella:1332-1339): the reseed is a one-shot unbypassable hinge; **P1 is the HARD
  go/no-go BEFORE committing SM-A2.** Resolved by ordering, no HALT.
- **§11** (umbrella:1232-1256): the "fold DPS into the fixpoint grind for the two return-facet
  blockers; keep push_inst_block a separate point-fix" recommendation this crumb confirms or disconfirms.

## Fixtures

The three minimal-repro probes ARE the deliverable, but they are DIAGNOSTIC probes, not regression
oracles the wave keeps green:

| fixture | asserts | expected |
|---|---|---|
| `probes/dps_pin_type_match` | minimal `TExpr`-through-tail-match that reproduces `type_match` | (native crash pre-DPS; oracle after SM-A2) |
| `probes/dps_pin_frame_sweep` | minimal `FrameSet`-through-if-merge that reproduces `frame_sweep_inst` | (native crash pre-DPS; oracle after SM-A2) |
| `probes/dps_pin_push_inst_block` | minimal self-append inst block that reproduces `push_inst_block` | (native crash pre-A5; oracle after SM-A5) |

Each is retained so SM-A2/SM-A5 can prove they turn the crash into exit `0`.

## Gate

`[dry]` — the probes compile/run down the C route (green) and the native repros are captured; the pin
note is recorded. "Green" = the fork verdict is decided and documented; no emitted-byte change to `src/`.
Reseed-class: `none`.

## Deps

`—`

## Done when

Each of `type_match`, `frame_sweep_inst`, `push_inst_block` is pinned to `{return-facet |
payload-bind-facet | self-append-facet}` with an objdump reading, and the SM-A2-vs-decouple fork is
recorded — so SM-A2 can be dispatched knowing whether it is the fixpoint fix or memory-only.
