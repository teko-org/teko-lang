---
seq: 0010
crumb-id: SM-G4
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: ["SM-G1"]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:146-171"  # §3 remove -> ref T
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1175"     # §10 Phase G — G4
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1323"     # §13 ritual after G4
---

# 0010 · SM-G4 — remove `-> ref T` return arm + gate cluster

> Remove `-> ref T` return arm + gate cluster (rides G1's return-parse edit).

## Goal

Return-by-reference (`-> ref T`) has ZERO production uses (verified: one probe + two rejection tests
only — assessment §4.5). DPS (SM-A2) SUBSUMES every genuine `-> ref T` case (the identity pass-down
returns caller-owned storage; under DPS the value already lands in the caller's arena). This crumb
DELETES the `Reference` return-type arm and the five-fn gate cluster that policed it. It rides SM-G1's
return-parse edit (`parse_function`'s return parse is where the arm lives). `ref` in PARAMETER position
is UNTOUCHED (the F1 borrow direction survives). Byte-preserving — vestigial-feature removal; the form
ceasing to parse IS the new rejection fixture. Its seed folds into SM-R1.

## Where

- `src/parser/parse_decl.tks` — `parse_function`'s return parse — delete the `Reference` return-type arm
  (rides the SM-G1 `:`/`Arrow` return edit — same site). `ref` in `parse_params` UNTOUCHED.
- `src/checker/typer.tks:5992`, `:6378` — the two invocation sites of the ref-return gate cluster — delete.
- The gate cluster (~5 fns, assessment §4.5): `check_ref_return_passdown`,
  `check_ref_return_passdown_stmt`, `check_ref_return_passdown_inexpr`, `ref_passdown_error`,
  `ref_value_is_passdown` — DELETE (one fewer return KIND in the type system).
- `collect_ref_param_names` — remove its return-gate use (keep its param use).

## How

1. **Delete the return-type arm.** In `parse_function` (riding SM-G1's edit), remove the `Reference`
   (`-> ref T`) arm from the return-type parse. `fn f(): ref T` now simply does not parse — the SAME
   generic "unexpected token" a never-existent form gets (no tombstone).
2. **Delete the gate cluster.** Remove the five fns and their two invocation sites (`typer.tks:5992`,
   `:6378`) and `collect_ref_param_names`'s return-gate use. This is DEAD detection once the form cannot
   parse — remove the root, do not keep a message. One fewer return KIND.
3. **Keep `ref` on params.** `ref` in parameter position (the F1 borrow direction) is UNTOUCHED — a
   separate axis. `ref []T` = position-pointer-only per the CLAUDE.md law, unchanged here.
4. **Retire the two rejection tests, retarget the one probe.** The two existing `-> ref T` rejection
   tests retire (their subject is gone); the one probe is retargeted. The form ceasing to parse IS the
   new rejection fixture.
5. **Confirm byte-neutrality.** No `src/` uses `-> ref T`, so removing it changes no emitted bytes;
   `[dry]` build byte-identical.

No new `fn`/`type` — this crumb DELETES surface.

## Rulings & laws

- **Teko-only:** parser/checker `.tks`; no C twin.
- **Removals = clean expurgo, NO tombstone (CLAUDE.md ruling 3 / owner):** a removed `-> ref T` becomes
  simply UNRECOGNISED — no lexer token, no parser production, no "was removed" diagnostic; an old-form
  program gets the SAME generic unexpected-token error a never-existent form gets.
- **NÃO DETECTAR O QUE NÃO EXISTE:** the gate cluster is dead detection once the form cannot parse —
  REMOVE the root, not the message.
- **W15:** touched declarations keep Javadoc; deletions add none.
- **Depends on SM-G1** because the `Reference` arm lives in the same `parse_function` return parse SM-G1
  edits — sequence after it to avoid a merge collision.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  ritual note: §13 lists a fixpoint after G4 (byte-mover via return-arm removal), folded into SM-R1.

## Fixtures

The self-build never uses `-> ref T`, so the fixpoint does not exercise the reject — an isolated
`EXPECT_COMPILE_FAIL` fixture is required:

| fixture | asserts | expected |
|---|---|---|
| `ref_return_form_rejected` | `fn f(): ref T` no longer parses (generic unexpected-token, no tombstone) | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + `ref_return_form_rejected` + fixpoint (byte-identical; no `src/` used the form).
"Green" = `-> ref T` does not parse (no tombstone diagnostic), the gate cluster is deleted, `ref` on
params still works, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`SM-G1` (shares the `parse_function` return-parse edit site).

## Done when

The `Reference` return arm and its five-fn gate cluster are deleted, `fn f(): ref T` is rejected with the
generic unexpected-token error (no tombstone), `ref` on params is intact, and `ref_return_form_rejected`
is `EXPECT_COMPILE_FAIL` with a byte-identical build.
