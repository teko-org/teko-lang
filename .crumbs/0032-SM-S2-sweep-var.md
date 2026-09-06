---
seq: 0032
crumb-id: SM-S2
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1203-1204"   # §10 Phase S — S2
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:103-143"     # §2 let/mut → var (grammar + CF3)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:138-143"     # §2.4 byte-preservation
---

# 0032 · SM-S2 — sweep to `var`; drop `let`/`mut` acceptance

> Sweep to `var`; drop `let`/`mut` acceptance — rewrite every `let`/`mut` local to `var`, then remove the
> soft-deprecated `let`/`mut` keywords from the parser.

## Goal

AUDIT/VERIFY-ONLY (already applied in src): confirm that the `let`/`mut` → `var` merge is complete — every
`let`/`mut` local declaration in `src/` and `.tkt` has been rewritten to `var` (everything mutable; one
keyword for all locals; type optional, inference stays; `const` retained), and that the soft-deprecated
`let`/`mut` acceptance has been removed from the parser (they were kept accepting through the additive G2
crumb, `0008`, but now that the source is swept, the acceptance is dead). Byte-preserving: `let`/`mut`/`var`
all lower to the SAME `Binding` AST (`BindKind` is intent, not safety — §1.4), so the rewritten source emits
identical bytes; the fixpoint proves it. CF3 (const-fold single-assignment analysis) was re-based on
flow-single-assignment in G2 and survives the merge. This crumb is a verify-only audit; the work is DONE in
src.

## Where

- `src/**/*.tks` — every `let x = …` / `mut x = …` local → `var x = …` (the mechanical rewrite). Only 4
  residual `let`/`mut` decls remain in `src/` (mostly `src/parser/loop_head.tks:34,38,162` desugaring
  helpers that CONSTRUCT `Binding` nodes) — those construct-site `BindKind::Let`/`Mut` become `BindKind` on
  the unified node.
- `src/**/*.tkt` — every `let`/`mut` in the test/expectation corpus → `var`.
- `src/lexer/token.tks:15-16` — `Let` / `Mut` tokens — REMOVED after the sweep (clean expurgo, no
  tombstone); `var` lexes as `Ident` with text `"var"` (the parser recognizes it, `parse_decl.tks:329`).
- `src/parser/parse_decl.tks:168` — the `Let`-acceptance branch (`if is_kind_at(tokens, pos,
  lexer::TokenKind::Let) { return true }`) — REMOVED after the sweep.
- `src/parser/ast.tks:92` — `BindKind = enum { Let; Mut; Const }` — collapse the `Let`/`Mut` arms per §2
  (the intent distinction is gone; `Const` stays); update `loop_head.tks:34,38,162` constructors.

NEW: no new surface; a source rewrite + a clean removal of the dead `let`/`mut` acceptance.

## How

**VERIFY-ONLY audit (work already done):** No fresh sweep is needed — the source is already swept and the
acceptance is already removed.

1. **Confirm zero residual `let`/`mut` locals.** Audit `src/` and `.tkt` to confirm there are ZERO remaining
   `let x` or `mut x` declarations (the sweep is complete). A search for `\blet\s+\w+\s*=` or `\bmut\s+\w+\s*=`
   (outside comments/docstrings) should yield ZERO matches. Exception: the four residual `BindKind::Let`/
   `BindKind::Mut` construct-sites in `loop_head.tks` (desugaring helpers) are collapsed/updated to the
   unified `BindKind` (Var-only).
2. **Confirm `const` is untouched.** Verify that `const` declarations still compile and are a distinct
   binding kind retained.
3. **Verify the dead acceptance is removed.** Confirm that `Let`/`Mut` tokens are gone (`token.tks`),
   the `Let`-accept branch is gone (`parse_decl.tks`), and the `BindKind::Let`/`Mut` arms are gone (`ast.tks`)
   — a clean expurgo with NO tombstone diagnostic.
4. **CF3 survives (G2 pre-work).** Verify that the const-fold single-assignment analysis (`cf3_fold_survives_let_merge`)
   still holds on the swept source — a G2 artifact that must survive the merge.
5. **Byte-identity is the proof.** Build gen2 on the SM-R1 seed, run the scoped regression, prove
   `gen2==gen3` byte-identical — the proof that the sweep introduced no semantic changes.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite + parser/lexer/AST removal `.tks`; no C twin.
- **W15 full Javadoc** unaffected; no inline `//` introduced.
- **`BindKind` is intent, not safety (§1.4):** `let`/`mut`/`var` share the mutable `Binding`; the merge is
  byte-preserving (§2.4).
- **Removals = clean expurgo, NO tombstone (CLAUDE.md):** the `let`/`mut` tokens + accept branch + AST arms
  are deleted cleanly once the source is swept; no deprecation diagnostic.
- **`const` retained; CF3 re-based (G2):** `const` untouched; `cf3_fold_survives_let_merge` holds.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild); fixpoint `gen2==gen3`; sweep `.tkt`/`.tkr` in lockstep.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's own source and removes
dead acceptance; building gen2 byte-identical IS the exercise. (The `let`/`mut`-now-rejected behavior is a
removal — any un-swept `let` would fail the build, which the fixpoint catches; the CF3-survives property is
covered by G2's own `cf3_fold_survives_let_merge` fixture already in the seed.)

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
audit confirms ZERO residual `let`/`mut` locals in `src/` + `.tkt` (sweep complete + byte-identical),
the `Let`/`Mut` tokens + accept branch + AST arms are cleanly removed (no tombstone), CF3 survives,
the build is byte-identical (`gen2==gen3`). Reseed-class: `fixpoint-rebuild` (this crumb is verify-only).

## Deps

`SM-R1` (the seed must accept `var` before the sweep; G2's re-based CF3 must be in that seed).

## Done when

Audit confirms every local in `src/` + `.tkt` is `var` (zero residual `let`/`mut` keywords), the `let`/`mut`
tokens + acceptance + `BindKind` arms are cleanly expurgated (no tombstone), `const` and CF3 are intact, and
gen2 on the SM-R1 seed is byte-identical (`gen2==gen3`). The sweep is DONE-verified.
