---
seq: 0067
crumb-id: RM-C12
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C11, RM-C6]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:290-297"          # C12 — fused check+lower+emit per unit (memory dump = C6)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:383-386"          # §6bis step 3 — check+lower+emit per unit, drop the unit region
  - "docs/design/reducao-memoria-arrays-0.3.1.md:437-502"          # what the linker retains × discards; the unifying dump principle
  - "docs/design/reducao-memoria-arrays-0.3.1.md:497-502"          # fixpoint byte-identity: the concatenation must equal whole-program
---

# 0067 · RM-C12 — fused check+lower+emit per unit (abstract unit-output; drop unit region)

> Restructure the backend to iterate namespaces in deterministic order — re-check bodies against the internal
> FFI, lower, emit the abstract unit-output, then DROP the unit's region — with `teko.c` byte-identical.

## Goal

C12 is the memory-dump heart of Eixo C (`reducao…` §6bis, the unifying principle): the streaming stage where
only ONE namespace's bodies live at a time. It restructures `backend`/`codegen_and_report` to iterate
namespaces in a deterministic order and, per unit: (re)load the unit's bodies, CHECK them against the internal
FFI (from C11), LOWER, emit the **unit-output** (abstract: on the C route a concatenated text chunk; on the
native route a `.o`, C15), append/write it, and **DROP the unit's region** (C6, `region_drop_subtree`) before
the next. The global prologue (type decls + forward fn decls) comes from the internal FFI (the link has it);
bodies stream. The **hard guard**: the concatenated per-unit output must EQUAL today's whole-program emission
byte-for-byte — `teko.c` byte-identical (`reducao…` 295-297, 497-502) — which requires the SAME global item
ordering and the SAME sectioning. It rests on C11 (the internal FFI feeding the per-unit checker) and C6 (the
arena-per-scope whose region-drop IS the per-unit dump). It drives no teaching reseed; a `fixpoint-rebuild`
swap. It is the "abstract unit-output" whose native realisation (`.o`) is C15.

Not blocked by any open dependency (its deps RM-C11 and RM-C6 are in this wave); this is executable design.

## Where

- `src/build/project.tks:2426` — `codegen_and_report(dir, out_dir, prog, m, opt, tty, start, debug): i32` — the
  emit entry; restructure to iterate namespaces per-unit (via `backend`).
- `src/build/project.tks:1165` — `backend(dir, stem, out_dir, prog, m, opt, tty, debug): i32` — the per-unit
  loop lives here: re-check → lower → emit unit-output → append/write → drop unit region.
- `src/build/project.tks:387` — `frontend_check` — its per-unit body re-parse + check against the internal FFI
  (C11) is invoked in the streaming loop, not once whole-program.
- `src/runtime/arena.tks:700` — `region_drop_subtree(r)` — the per-unit region drop (C6) that IS the memory
  dump on the fused path.
- `src/codegen/codegen.tks` — the emit must produce a concatenable UNIT-output; the global prologue (type/fwd
  decls) sources from the internal FFI, not a whole-program pass.
- NEW abstraction: a `UnitOutput` seam (C text chunk today; `.o` at C15) — see How §2.
- Existing types touched: `checker::TProgram` (now per-unit), the internal FFI (`InternalFfi`, C11).

## How

1. **Iterate namespaces deterministically** (`reducao…` §6bis step 3). For each unit in a fixed order:
   (re)load the unit's bodies, check against the internal FFI (C11), lower, emit the unit-output, append/write,
   drop the unit's region (`region_drop_subtree`). Only one unit's bodies are resident at a time — the peak
   drops from the SUM to the MAX of a stage.
2. **Abstract the unit-output** so the native terminal (C15) drops in without re-restructuring. The W15 seam
   the implementer copies verbatim:

```teko
/**
 * UnitOutput — the abstract per-unit emission the streaming backend appends and then drops the unit region
 * for. On the C route it is a concatenated text chunk of the unit's emitted C; on the native route (C15) it
 * is the unit's `.o`. Abstracting it here lets C15 replace the realisation without re-restructuring the loop —
 * "the abstract output of C12 = the `.o` of C15" (`reducao…` 315-320).
 *
 * @since 0.3.1
 */
type UnitOutput = struct {
    /** the emitted bytes for this unit (C text today; `.o` bytes at C15). */
    bytes: []byte
}

/**
 * emit_unit — check (against the internal FFI), lower, and emit ONE namespace's bodies to a `UnitOutput`, then
 * the caller drops the unit's arena region (C6). The global prologue is NOT re-emitted here — it comes from
 * the internal FFI once. The per-unit concatenation MUST equal today's whole-program emission byte-for-byte
 * (`reducao…` 497-502) — same global item ordering, same sectioning.
 *
 * @param unit  the namespace whose bodies to check+lower+emit
 * @param ffi   the internal-FFI table (C11) the checker types cross-namespace calls against
 * @return      the unit's emitted output, or a check/lower/emit error
 * @throws      when the unit fails checking, lowering, or emission
 * @since 0.3.1
 */
fn emit_unit(unit: Unit, ffi: InternalFfi): UnitOutput | error
```

3. **The global prologue sources from the internal FFI** (`reducao…` 499-502): type decls + forward fn decls
   emit once from the linked table; the per-unit stream emits only bodies. The concatenation order must match
   the whole-program section order EXACTLY.
4. **Drop the unit region after emit** (C6): `region_drop_subtree` on the unit's arena frame — the memory dump
   is the mass region-drop, no block-by-block free (`reducao…` 446-447).
5. **HARD GUARD — `teko.c` byte-identical.** Fixpoint per namespace migrated: after each namespace moves to the
   streaming path, `teko.c` must stay byte-for-byte the whole-program output. If a byte shifts, the ordering or
   sectioning diverged — stop and re-examine (this is why C10's determinized gensym is a transitive
   pre-condition).
6. **Retain × discard per the table** (`reducao…` 437-447): retain the internal FFI (compact) + the output
   accumulator (ideally streamed to file); discard per unit the parsed bodies, typed statement trees, and
   lowered LIR of that namespace.

Reused (do NOT redeclare): `InternalFfi`/`link_units` (C11), `region_drop_subtree`/`region_enter`/
`region_leave` (arena.tks, C6), `checker::TProgram`, the codegen emit fns.

## Rulings & laws

- **Teko-only:** the restructure lands in `src/build/project.tks` + `src/codegen/codegen.tks` (`.tks`); no C
  twin.
- **W15 full Javadoc** on `UnitOutput`/`emit_unit` + every member; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** the whole-program emit path is REPLACED by the streaming path
  under a byte-identical guard; when the whole-program pass is removed it is a clean expurgo, no tombstone.
- **Hard byte-identity guard (`reducao…` 295-297, 497-502):** `teko.c` byte-identical to whole-program is a
  RELEASE gate, not a nicety — the per-unit concatenation must equal today's output.
- **Memory-dump principle (`reducao…` §6bis):** the peak is the MAX of a stage; the per-unit region-drop (C6)
  IS the dump; retain only the compact internal FFI + the streamed output.
- **Determinism (`reducao…` R6, transitively C10):** deterministic namespace order → deterministic output.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step per namespace migrated; **reseed ONLY at a
  [RITUAL]** — this is `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical;
  sweep `.tkt`/`.tkr` after the `UnitOutput`/`emit_unit` signature changes.

## Fixtures

none — the fixpoint self-build exercises this. (The compiler's own multi-namespace source is the streaming
input; `teko.c` byte-identical (per namespace migrated) + `gen2==gen3` IS the regression. The 6.5 GiB
`ulimit -v` cap is the memory-peak regression — the whole point of Eixo C is that the peak drops, and a blown
cap is a root-cause fix.)

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-identity + the `ulimit -v 6815744` peak cap, fixpoint per
namespace migrated. "Green" = the backend streams check+lower+emit per unit, drops each unit's region, sources
the prologue from the internal FFI, and the concatenated `teko.c` is byte-identical to the whole-program
output. **Reseed-class:** `fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`RM-C11` (`0066` — the internal-FFI table the per-unit checker types against), `RM-C6` (`0040` — the
arena-per-scope whose `region_drop_subtree` IS the per-unit memory dump).

## Done when

the backend iterates namespaces in deterministic order (re-check → lower → emit unit-output → drop unit
region), the global prologue sources from the internal FFI, the concatenated `teko.c` is byte-identical to the
whole-program output, the memory peak stays under the `ulimit -v` cap, C15's abstract unit-output seam exists,
and a `[fixpoint]` build is `gen2==gen3` byte-identical.
