---
seq: 0154
crumb-id: MEM-E5
milestone: M5
gate: "[RITUAL]"
reseed-class: "fixpoint-rebuild"
deps: [MEM-E2, MEM-E3, MEM-E4]
sources:
  - "docs/design/modelo-de-memoria-por-escopo-0.3.1.md:216-251"       # §3b oracle-in-checker, both engines read it
  - "docs/design/transicao-move-on-return-e-seletor-n-niveis-0.3.1.md:253-278" # §4 unification C+native
  - "src/checker/residence.tks:49"                                    # residence_plan (now table-threaded)
  - "src/codegen/codegen.tks"                                         # C emitter ctx
  - "src/lir/lower.tks"                                               # native LowerCtx
---

# 0154 · MEM-E5 — plumb `residence_plan` into both emitters (UNCONSUMED) — RESEED-1

> The last teaching crumb: compute `residence_plan(f, table)` once per function and thread it into BOTH
> emitter contexts (C `codegen.tks`, native `lower.tks`), so the sweep flips (`MEM-W1..W6`) have the plan
> in hand — but consume NOTHING from it yet (the old 2-level heuristic still drives emission).
> Byte-identical. This crumb's gate is the RESEED-1 harvest of the whole teaching cluster (E0a..E5).

## Goal

The oracle (`residence.tks`) must be READ IDENTICALLY by both engines (the `escape.tks:405` "they must
never disagree" generalized). This crumb computes the plan per function in `lower_function`/the C
per-function emit and stores it in each context (as `fn_escaping_vars` is already consumed in native),
WITHOUT routing on it. The existing heuristic (`_tkbr`/`_tkfr`/`want_block`, 2-level) still emits, so
byte-identical. `MEM-W1` (elision), `MEM-W2` (sizing), `MEM-W3` (scope residence), `MEM-W4` (move) are
the consumers.

## Where

- `src/codegen/codegen.tks` — the per-function emit computes `checker::residence_plan(f, table)` and
  stores it in the emit ctx; no routing consumes it.
- `src/lir/lower.tks` — `LowerCtx` gains a `plan: ResidencePlan` field; `lower_function` initializes it;
  `ctx_with*` + `lower_test.tkt` literals propagate/default it.

## How

1. Add the `plan` field to both contexts; initialize per function.
2. Thread through the `ctx_with*` and the test literals (the `table`-field chore precedent).
3. Consume NOTHING — the heuristic still drives. Byte-identical.

## Rulings & laws

- **Teko-only:** `codegen.tks`/`lower.tks`.
- **One oracle, both engines (§3b):** the plan is computed once in the checker and read identically —
  the unification that makes C and native never diverge.
- **Additive/inert:** unconsumed → byte-identical.
- **W15 full Javadoc; no `//`.** **Safety:** never `teko test .`; subshell `ulimit -v 4718592`.

## Fixtures

none — unconsumed; the fixpoint proves byte-identity.

## Gate

`[RITUAL]` — **RESEED-1**, the single harvest of the whole teaching cluster (`MEM-E0a`,`E0b`,`E1`,`E2`,
`E3`,`E4`,`E5`): build gen2, `gen2==gen3` byte-identity, MEM_PARANOID 0, per-OS `nm` unchanged, RSS
ratchet flat-or-down. "Green" = the plan is in both contexts, nothing consumes it, `gen2==gen3`. This is
the ONE teaching reseed; everything before it was written without an intermediate build. Reseed-class:
`fixpoint-rebuild`.

## Deps

`MEM-E2` (the plan/sizing), `MEM-E3` (the table-threaded plan), `MEM-E4` (the region param plumbing).

## Done when

Both emitters compute and hold `residence_plan(f, table)` per function, no site routes on it, RESEED-1 is
`gen2==gen3` byte-identical with MEM_PARANOID 0, and the whole teaching cluster is in the seed.
