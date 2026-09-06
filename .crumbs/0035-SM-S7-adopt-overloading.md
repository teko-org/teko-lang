---
seq: 0035
crumb-id: SM-S7
milestone: M2
gate: "[dry]/[fixpoint]"
reseed-class: "none/fixpoint"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1217-1219"   # §10 Phase S — S7 (optional)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1085-1095"   # §7c.3 S7 adopt-in-src
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1221-1228"   # §10 independence map (S7 optional/empty)
---

# 0035 · SM-S7 — (optional) adopt overloading in `src/`

> (Optional) adopt overloading in `src/` (empty if `src/` adopts nothing) — a conditional sweep that
> byte-moves only where the compiler's own source chooses an overload set or an operator dunder.

## Goal

The OPTIONAL, conditional tail of Phase S: IF the compiler's own `src/` chooses to USE a method overload set
or an operator dunder (e.g. a `bigint`/`dec` `__add`) now that G10/G11 (`0016`/`0017`) shipped method +
operator overloading as user-facing capability, that adoption is a Phase-S sweep — byte-moving ONLY where
adopted, fixpoint-gated. If `src/` adopts NOTHING (the likely case — the features ship as user capability
regardless of whether the compiler dogfoods them), SM-S7 is EMPTY: no source change, `[dry]`, reseed-class
`none`. Its gate/reseed-class is therefore the master-plan's DUAL `[dry]/[fixpoint]` · `none/fixpoint`,
resolving to the heavier arm ONLY if `src/` actually adopts an overload set or dunder. This crumb makes the
resolution: audit `src/` for an adoption opportunity, adopt where it genuinely reads better, and gate the
result.

## Where

- `src/**/*.tks` — ONLY the sites that genuinely benefit from an overload set / operator dunder (if any):
  e.g. a numeric type (`bigint`/`dec`) gaining `__add`/`__eq`, or a same-name method family distinguished by
  param signature. If none reads better, NOTHING is touched.
- `src/checker/typer.tks` — `select_overload` (the call-path overload resolution G10 added) — the machinery
  the adoption uses; UNCHANGED (already in the SM-R1 seed).
- `src/checker/*` — the dunder-lookup + op→dunder map + derived-comparison machinery (G11) — UNCHANGED;
  the adoption calls into it.
- `src/**/*.tkt` — the expectation corpus for any adopted site — swept in lockstep IF an adoption lands.

NEW: no new surface; a conditional adoption sweep that may be EMPTY.

## How

1. **Audit `src/` for a genuine adoption.** Grep for same-name method families that overloading would now
   express cleanly, and for numeric/value types where an operator dunder (`__add`/`__eq`/…) reads better
   than a named method. Confirm no ACCIDENTAL same-name defs (the pre-sweep check §7c.3): overloading now
   makes two same-name defs a valid overload set rather than a reject, so verify each is intentional.
2. **Adopt ONLY where it genuinely improves the source.** Overloading + operator dunders are a user-facing
   capability that ships regardless; the compiler adopts them only where the source is clearer for it — not
   as a mechanical mass rewrite. Restraint is correct here (§14 austerity): an adoption that does not read
   better is not made.
3. **Resolve the gate by the outcome.**
   - **If `src/` adopts NOTHING:** SM-S7 is EMPTY — no source change, `[dry]`, reseed-class `none`. The
     features ship as user capability from G10/G11 already in the seed.
   - **If `src/` adopts a set/dunder:** it is byte-MOVING where adopted (the overload suffix / dunder
     dispatch changes the emitted symbol/call) → `[fixpoint]`, reseed-class `fixpoint-rebuild`; build gen2
     on the SM-R1 seed, prove `gen2==gen3` byte-identical for the adopted emit.
4. **Sweep the corpus if adopted.** Any adopted site's `.tkt`/`.tkr` expectations are swept in lockstep.
5. **State the resolution in the crumb outcome.** The implementer records which arm resolved (empty vs.
   adopted) and, if adopted, the exact sites — so the manifest's dual gate is pinned to the real outcome.

## Rulings & laws

- **Teko-only:** any adoption is source `.tks`; the overload/dunder machinery (G10/G11) is already in the
  seed; no C twin.
- **W15 full Javadoc** on any newly-authored overload/dunder method; no inline `//`.
- **Restraint / austerity (§14):** overloading is a user capability; the compiler adopts it ONLY where the
  source genuinely reads better — an unnecessary adoption is not made.
- **Dual gate resolves to the outcome (master-plan):** `[dry]`/`none` if empty; `[fixpoint]`/
  `fixpoint-rebuild` if `src/` core-consumes an adopted set/dunder.
- **Confirm no accidental same-name defs (§7c.3):** overloading turns a former reject into a valid set —
  verify each same-name pair is intentional before the sweep.
- **Safety:** NEVER `teko test .`; if adopted, build gen2 in a subshell with `ulimit -v 6815744`; commit the
  green step; NO teaching reseed (fixpoint-rebuild at most); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in
  lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. If SM-S7 adopts nothing there is no behavior to test (the
G10/G11 acceptance fixtures already sit in the seed). If it adopts a set/dunder, the compiler now
core-consumes that emit and the fixpoint `gen2==gen3` exercises it directly — a scoped `.tkr` would be
redundant.

## Gate

`[dry]/[fixpoint]` — resolves to `[dry]` (byte-identical, no source change, reseed-class `none`) if `src/`
adopts nothing, or `[fixpoint]` (build gen2 on the SM-R1 seed + `gen2==gen3` for the adopted emit,
reseed-class `fixpoint-rebuild`) if `src/` adopts an overload set or operator dunder. "Green" = the
resolution is made and recorded, and (if adopted) the emit is byte-identical to the intended overloaded
lowering.

## Deps

`SM-R1` (the overload/dunder machinery from G10/G11 must be in the seed before `src/` may adopt it).

## Done when

The `src/` overloading-adoption audit is done and its outcome recorded: either SM-S7 is empty (`[dry]`,
`none`, no source change) or the adopted sites are swept and gen2 on the SM-R1 seed is byte-identical
(`[fixpoint]`, `fixpoint-rebuild`) — the manifest's dual gate pinned to the real outcome.
