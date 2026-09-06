---
seq: 0068
crumb-id: RM-C13
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C12]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:299-303"          # C13 — typed .tkb per-unit disk dump
  - "docs/design/reducao-memoria-arrays-0.3.1.md:462-495"          # §6bis the two dump forms; reuse .tkb, not invent
  - "docs/design/reducao-memoria-arrays-0.3.1.md:641-646"          # R6 — determinism of the serialized inter-stage artifact
---

# 0068 · RM-C13 — typed `.tkb` per-unit disk dump (serialize/deserialize one namespace; deterministic frame)

> Extend `serialize_program`/`deserialize_program` to serialize/deserialize a SINGLE namespace, so the stage
> whose working set does not fuse (the LINK barrier) dumps each unit's typed AST to disk and re-reads one at a
> time — with a deterministic frame.

## Goal

C12 fuses check→lower→emit per unit (a memory dump = arena region-drop). But one boundary does NOT fuse: the
LINK barrier precedes ALL bodies (the monolith's mutual recursion needs every export before checking any body,
`reducao…` 428-435), so the typed working set cannot all stay resident. C13 adds the **disk dump** form of the
unifying principle (`reducao…` 462-495): where a stage does not fuse, dump each unit's typed artifact to disk
and re-read it one at a time — the working set leaves the heap and returns on demand. It **reuses `.tkb`, not
invents**: `src/emit/tkb_frame.tks:369` `serialize_program(prog): []byte` and `src/emit/tkb_read.tks:930`
`deserialize_program(data): TProgram` already serialize the whole typed-AST (in production on the package
path). C13 extends them to a SINGLE namespace (serialize/deserialize one unit, not the whole program). The frame
MUST be deterministic (`reducao…` R6): same input → same `.tkb` bytes → same `TProgram` → same `teko.c`. It
rests on C12 and drives no teaching reseed; a `fixpoint-rebuild` swap.

Not blocked by any open dependency (its dep RM-C12 is in this wave); this is executable design.

## Where

- `src/emit/tkb_frame.tks:369` — `serialize_program(prog: checker::TProgram): []byte | error` — add a per-unit
  companion `serialize_unit(unit)` that serializes ONE namespace's typed decls+bodies with a deterministic
  frame; the whole-program form stays for the package path.
- `src/emit/tkb_read.tks:930` — `deserialize_program(data: []byte): checker::TProgram | error` — add
  `deserialize_unit(data)` reading one namespace back.
- `src/build/project.tks:352,387` — `frontend_parse`/`frontend_check` — the LINK barrier (C11) writes each
  unit's typed dump; the streaming stage (C12) re-reads one unit at a time from disk.
- NO new user surface: the `.tkb` format is internal; the per-unit forms are additive companions.

## How

1. **Add `serialize_unit`/`deserialize_unit`** (`reducao…` 473-479): extend the existing `.tkb` framing to a
   single namespace — the typed decls + bodies of one unit, framed deterministically. The whole-program
   `serialize_program`/`deserialize_program` remain unchanged for the package (`.tkl`) path. The W15 surface:

```teko
/**
 * serialize_unit — serialize ONE namespace's typed AST (decls + bodies) to a deterministic `.tkb` frame,
 * the disk-dump form of the unifying principle for the stage that does NOT fuse (the LINK barrier precedes
 * all bodies, `reducao…` 468-471). Reuses the `.tkb` framing of `serialize_program` (`tkb_frame.tks:369`),
 * narrowed to a unit. The frame MUST be deterministic — same input → same bytes → same `teko.c` (`reducao…`
 * R6): no `map`/`hashset` iteration order, no implicit global state, stable item order matching whole-program.
 *
 * @param unit  the namespace whose typed AST to serialize
 * @return      the deterministic `.tkb` bytes for the unit, or a serialization error
 * @throws      when the unit's typed AST cannot be framed
 * @since 0.3.1
 */
fn serialize_unit(unit: checker::TUnit): []byte | error

/**
 * deserialize_unit — read one namespace's typed AST back from its `.tkb` frame (the inverse of
 * `serialize_unit`), re-materializing the unit's typed decls+bodies for the streaming check+lower+emit stage
 * (C12) one unit at a time. Reuses `deserialize_program`'s reader (`tkb_read.tks:930`), narrowed to a unit.
 *
 * @param data  the unit's `.tkb` bytes
 * @return      the re-materialized typed unit, or a deserialization error
 * @throws      when the bytes are not a valid unit frame
 * @since 0.3.1
 */
fn deserialize_unit(data: []byte): checker::TUnit | error
```

2. **Wire the disk dump at the LINK boundary** (`reducao…` 484-495): the parse→link boundary is the one that
   warrants a disk dump (link is a global barrier; the bodies do not all fit). The typed unit is dumped after
   the link; the streaming stage re-reads one unit at a time. check→lower and lower→codegen still FUSE in
   memory (C12) — only the non-fusing boundary hits disk.
3. **Deterministic frame is mandatory** (`reducao…` R6, 641-646): the per-unit `.tkb` must preserve the SAME
   item order as the whole-program frame — same `.tkb` → same `teko.c`. Audit any `map`/`hashset` ordering or
   implicit global state in the framing (rests transitively on C10).
4. **Fixpoint proves it.** `gen2==gen3` byte-identical, with the typed working set dumped/re-read at the LINK
   boundary — the emitted `teko.c` unchanged.

Reused (do NOT redeclare): `serialize_program`/`deserialize_program` (the framing this narrows),
`checker::TProgram`/`TUnit`, the internal FFI (C11), `region_drop_subtree` (C6/C12).

## Rulings & laws

- **Teko-only:** the extension lands in `src/emit/{tkb_frame,tkb_read}.tks` + `src/build/project.tks` (`.tks`);
  no C twin.
- **W15 full Javadoc** on `serialize_unit`/`deserialize_unit`; flatten/extract; no inline `//`.
- **Removals = clean expurgo, NO tombstone:** additive companions — removes no surface.
- **Determinism law (`reducao…` R6, 641-646):** the serialized inter-stage artifact must be reproducible —
  same `.tkb` → same `teko.c`; the per-unit frame preserves whole-program item order.
- **Reuse, not invent (`reducao…` 473-479):** extend the existing `.tkb` framing to a unit; do NOT invent a new
  serialization format (LIR needs no disk artifact if lower→emit fuse — defer any `.tkb`-of-LIR until measured).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744` cap — a blown guard is a
  root-cause fix, never a raised ceiling; commit each green step; **reseed ONLY at a [RITUAL]** — this is
  `fixpoint-rebuild`, no teaching seed harvested; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr`
  after the `serialize_unit`/`deserialize_unit` signatures land.

## Fixtures

none — the fixpoint self-build exercises this. (The compiler's own namespaces are serialized/deserialized per
unit at the LINK boundary; a determinism defect surfaces as a `teko.c` byte diff — `gen2==gen3` + the
whole-program byte-identity IS the regression. A round-trip `serialize_unit`∘`deserialize_unit` identity on the
compiler's own units is exercised by the self-build.)

## Gate

`[fixpoint]` — build gen2 + `gen2==gen3` byte-identity + the `ulimit -v 6815744` peak cap. "Green" = one
namespace serializes/deserializes through a deterministic `.tkb` frame at the LINK boundary, the typed working
set leaves the heap and returns on demand, and the emitted `teko.c` is byte-identical. **Reseed-class:**
`fixpoint-rebuild` (core-consumes; teaches nothing; no reseed harvested).

## Deps

`RM-C12` (`0067` — the fused per-unit check+lower+emit; C13 dumps the one stage that does not fuse, the LINK
barrier).

## Done when

`serialize_unit`/`deserialize_unit` frame a single namespace deterministically, the LINK-boundary typed working
set dumps to disk and re-reads one unit at a time, the emitted `teko.c` is byte-identical, the memory peak stays
under the `ulimit -v` cap, and a `[fixpoint]` build is `gen2==gen3` byte-identical.
