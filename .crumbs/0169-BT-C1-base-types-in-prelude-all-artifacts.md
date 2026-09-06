---
seq: 0169
crumb-id: BT-C1
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [PRE-C1]
sources:
  - "docs/design/embed-vfs-sweep-integration-0.3.1.md:5"        # §5.2 base types in prelude
  - "DECISION_LOG.md:1173-1174"                                  # D133 consolidate base-type defs
  - "src/sys/marshall.tks:8,16"                                 # ptr/uptr newtypes (already landed)
  - "src/build/project.tks:358-366"                             # artifact_wants_runtime_prelude
---

# 0169 · BT-C1 — consolidate reserved base-type defs into ONE embedded prelude unit, inject into ALL artifacts

> The reserved base types (`str`/`[]byte`/`char`/`ptr`/`uptr`/`isize`/`usize`/`u8`…) become surface defs
> in ONE base-prelude unit (part of the embedded prelude), injected into EVERY artifact incl. Package —
> distinct from the runtime IMPL (arena/io) which stays Binary/Tool-only. This is the D133 prerequisite:
> the base defines the reserved types ONCE so provenance (PV-C1) can bar user redefinition. Byte-mover
> (type origins shift; injection widens) → RITUAL + reseed.

## Goal

Consolidate the reserved-name base-type defs into a single `Base` prelude unit (annex §5.2). `ptr`/`uptr`
already exist (`marshall.tks:8,16`); gather them + the other reserved defs into the base unit. Split the
prelude into TWO classes: (a) **base-type defs** — universal, injected into ALL `Artifact`s incl.
`Package`; (b) **runtime impl** (arena/io/sys/assert) — injected only into `Binary`/`Tool` (Package does
not need the arena). `artifact_wants_runtime_prelude` (`project.tks:358`) splits into
`wants_base_prelude` (all) + `wants_runtime_prelude` (Binary/Tool).

## Where

- NEW/consolidated base-prelude unit (a `teko::` base namespace file, embedded via PRE-C1) — the reserved-
  name defs in one place.
- `src/build/project.tks:358-366` — split `artifact_wants_runtime_prelude` into base (all artifacts) +
  runtime (Binary/Tool); `inject_runtime_prelude` injects the base unit universally, the runtime units
  conditionally.
- `src/sys/marshall.tks` — the `ptr`/`uptr` defs move to / are referenced from the base unit (no behavior
  change; same `exp global type`).

## How

1. Assemble the reserved base defs into one unit; keep the existing `ptr`/`uptr` semantics.
2. `wants_base_prelude(a): bool` = true for all five `Artifact` variants; `wants_runtime_prelude(a)` keeps
   the current Binary/Tool truth table.
3. `inject_runtime_prelude`: always inject the base unit; inject the runtime units only when
   `wants_runtime_prelude`.
4. Provenance is NOT gated here (that is PV-C1); this crumb only consolidates + widens injection.

## Rulings & laws

- **Teko-only.**
- **D133/D134:** base-type defs are universal surface (all artifacts incl. Package); runtime impl stays
  Binary/Tool; only the reserved base defs migrate to the prelude now (not the full `.tkh` retro-feed).
- **`isize→i64`/`usize→u64` implicit coercion** is landed (`same_word_lateral`) — preserve; no cast-sweep.
- **W15 full Javadoc; no `//`.**
- **Safety:** never `teko test .`; subshell `ulimit -v 4718592`; **RITUAL** — full ladder, `gen2==gen3`,
  MEM_PARANOID. Ratchet: ADDITIVE → peak NOT grown.

## Fixtures

`none — the base types are exercised by every self-build unit (they ARE the type system); Package-artifact
injection is exercised by building a Package target`.

## Gate

`[RITUAL]` — full ladder; the base unit injects into all artifacts, runtime stays conditional, `gen2==
gen3`, peak flat. "Green" = reserved base types defined once in the base prelude, injected universally,
Package builds with them. Reseed-class: `fixpoint-rebuild`.

## Deps

`PRE-C1`

## Done when

The reserved base-type defs live in one embedded base-prelude unit injected into ALL artifacts, runtime
impl stays Binary/Tool, and the RITUAL gate is green.
