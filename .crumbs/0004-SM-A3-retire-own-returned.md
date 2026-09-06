---
seq: 0004
crumb-id: SM-A3
milestone: M1
gate: "[RITUAL]*"
reseed-class: "(folds R1)"
deps: ["SM-A2"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:62-77"    # §1.1 retire own_returned_value
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1162-1163"# §10 Phase A — A3
---

# 0004 · SM-A3 — retire `own_returned_value` on the DPS path

> Retire `own_returned_value` on the DPS path; keep `frame_escape_guard` as the inversion net.

## Goal

With DPS landed (SM-A2), the retrofitted copy-out return box `own_returned_value` (`lower.tks:11715`)
is DEAD on the DPS path — the value is already born in the caller's arena, so there is nothing to own
and copy out. This crumb RETIRES that box on the DPS path (removes the now-dead emission), while KEEPING
`frame_escape_guard` (`frame_escape.tks:56`) as the inversion net that catches any escape DPS did not
cover. It is a byte-mover only where the box was still being emitted; after retirement the DPS emit is
leaner. Its seed folds into SM-R1 (`(folds R1)`).

## Where

- `src/lir/lower.tks:11715` — `own_returned_value` — the retrofit box; retire its emission on the DPS
  path (`ret_dest != null`). Do NOT delete the symbol wholesale if any legacy `ret_dest == null` path
  still legitimately needs it — retire it exactly where DPS made it dead (guarded by `ret_dest`).
- `frame_escape.tks:56` — `frame_escape_guard` — KEPT; it remains the inversion net.
- `src/lir/lower.tks:7245`/`:7278` — `lower_return`/`lower_return_fat` — confirm the DPS branch no longer
  references the box.

## How

1. **Retire the box on the DPS path.** In `lower_return`/`lower_return_fat`, on the `ret_dest != null`
   branch, remove the `own_returned_value` box construction/copy-out entirely — the write went straight
   through `ret_dest` in SM-A2. Follow the CLAUDE.md law "do not detect/emit for what no longer flows":
   the box is DEAD CODE to REMOVE on this path, not to keep behind a flag.
2. **Keep `frame_escape_guard` as the net.** Leave `frame_escape.tks:56` in place; DPS satisfies it BY
   CONSTRUCTION (the returned value lives in the caller's arena), but the guard stays as the inversion
   backstop that fails a fixture if an escape slips through. Prove it stays satisfied: `dps_no_frame_escape`.
3. **Confirm SM-P1's two blockers.** If SM-P1 pinned `type_match`+`frame_sweep_inst` to the return
   facet, this crumb's clean DPS return (no box) is where they finish green — the ritual verifies both
   native crashes are exit `0` and `frame_escape_guard` is clean.
4. **Rebuild the native ladder + fixpoint.** `gen2 == gen3` must hold; the retirement is a deterministic
   leaner emit.

There is NO new Teko surface — this crumb REMOVES a dead emission and adds no `fn`/`type`.

## Rulings & laws

- **Teko-only:** `src/lir/lower.tks` edit; no C twin.
- **CLAUDE.md "NÃO DETECTAR/BARRAR/EMITIR O QUE NÃO EXISTE":** the box is dead on the DPS path — REMOVE
  it (clean expurgo of the dead emission), do not keep it behind a condition. No tombstone.
- **W15:** no doc-comment added for a removal; any touched declaration keeps its Javadoc.
- **Safety:** NEVER `teko test .`; native ladder + fixpoint in a subshell with `ulimit -v 6815744`;
  commit the green step; SEED harvested at SM-R1, not here (`[RITUAL]*`, `(folds R1)`).

## Fixtures

`none — the fixpoint self-build exercises this` for the box retirement itself (the compiler returns
aggregates everywhere; the leaner DPS emit is exercised by the self-build and gated by `gen2==gen3`).
The escape/inversion oracle is carried by SM-A2's `dps_no_frame_escape` (0) and
`dps_caller_dest_not_dropped` (inversion fails) — no new fixture is minted here.

## Gate

`[RITUAL]*` — full native ladder + `gen2==gen3` + `frame_escape_guard` clean + (if SM-P1 confirmed)
`type_match`/`frame_sweep_inst` native crashes = exit `0`. "Green" = the box no longer emits on the DPS
path, the fixpoint holds byte-identical, and the guard is satisfied. Reseed-class: `(folds R1)`.

## Deps

`SM-A2` (DPS must be landed for the box to be dead on the DPS path).

## Done when

`own_returned_value` no longer emits on the `ret_dest != null` path, `frame_escape_guard` stays
satisfied, the native ladder is green with `gen2==gen3`, and SM-P1's return-facet blockers (if
confirmed) are exit `0`.
