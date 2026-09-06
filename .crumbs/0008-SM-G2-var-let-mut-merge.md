---
seq: 0008
crumb-id: SM-G2
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:103-143"  # §2 let/mut → var
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1171-1172"# §10 Phase G — G2
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:92-99"    # §1.4 BindKind is intent, not safety
---

# 0008 · SM-G2 — merge `Let`/`Mut` → `var` (accept `var`/`let`/`mut`); re-base CF3

> Merge `Let`/`Mut` → `var` (accept `var`/`let`/`mut`); re-base CF3 on flow-single-assign.

## Goal

Everything mutable; ONE keyword `var` for all locals (type optional, inference stays). `const` retained.
Params stay immutable (B.21 — "everything mutable" is LOCALS ONLY). This crumb ADDS `var` and collapses
`Let`+`Mut` into ONE local `BindKind`, while KEEPING `let`/`mut`/`const` classified so the current seed's
own source keeps parsing (`let`/`mut` become soft-deprecated no-op spellings of `var`, a lint not a parse
error). The one byte-mover is the **CF3 const-fold re-base**: today `lp_is_const_binding` folds on
`k == Let || k == Const`; with `Let` gone it re-bases on FLOW-single-assignment (a local written exactly
once is effectively immutable and derivable), so the folds and emitted bytes SURVIVE. Byte-preserving,
gated on the CF3 re-base holding the folds; its seed folds into SM-R1.

## Where

- `src/lexer/lexer.tks:339-341` — add `if text == "var" { return TokenKind::Var }` (new `TokenKind::Var`
  in `token.tks`). KEEP `let`/`mut`/`const` classified.
- `src/parser/parse_stmt.tks:55/194/227/256-258` and `src/parser/loop_head.tks:84/98/407` — the
  keyword→`BindKind` map sends `Var`, `Let`, `Mut` ALL to the merged local kind; `Const` stays separate.
- `src/parser/parse_stmt.tks:311` — the `ref`→Mut desugar targets the merged kind.
- `src/parser/ast.tks:259` — `BindKind = enum { Let; Mut; Const }` → collapse `Let`+`Mut` into ONE local
  kind (name it `Var`; keep `Const`).
- `src/checker/typer.tks:949` (free), `:1769`/`:3276` (`&`), `:4212` (`ref` source) — the three ergonomic
  gates become always-pass for locals; delete the now-dead "declare it `mut`" messages.
- `src/checker/comptime_fold.tks:2918` — `lp_is_const_binding` — re-base on flow-single-assignment.
- Params: `scope.tks:186/249`, `match.tks:216/253` — B.21-immutable — UNTOUCHED (a SEPARATE axis).
- Codegen: `codegen.tks:8583` (const rodata), `:1447` (frame-route), `lower.tks:6704` (`Mut` fat-rebind
  → merged kind) — NO storage difference; both `let`/`mut` already lower to the same writable slot.

## How

1. **Lexer:** add `TokenKind::Var` (`token.tks`) and classify `"var"` (`lexer.tks:339-341`). Keep
   `let`/`mut`/`const` classified — all four accepted during the window; `const` unchanged.
2. **AST:** collapse `BindKind` to `enum { Var; Const }` (per CLAUDE.md: `Mut`→`Var`, `Let`→`Var`, no
   re-thinking `Let`; discard/loop-var idem). Every `is_mut` read for a LOCAL becomes always-true.
3. **Parser map:** `Var`/`Let`/`Mut` → `Var`; `Const` → `Const` (`parse_stmt.tks`, `loop_head.tks`). The
   `ref`→Mut desugar (`parse_stmt.tks:311`) targets `Var`.
4. **Checker gates:** the three ergonomic gates (free `:949`, `&` `:1769`/`:3276`, `ref` `:4212`) become
   always-pass for locals; DELETE the dead "declare it `mut`" messages (CLAUDE.md: dead-code removal, no
   message referencing the removed construct — the user must not even know `let`/`mut` existed).
5. **Params stay B.21-immutable** — do NOT make params mutable on this crumb (`scope.tks:186/249`,
   `match.tks:216/253` untouched). This is the separate axis.
6. **CF3 re-base (the byte-mover).** In `lp_is_const_binding` (`comptime_fold.tks:2918`), replace
   `k == Let || k == Const` with: `k == Const` OR the local is FLOW-single-assigned (written exactly
   once). The single-assignment fact is derivable from the existing flow analysis; a once-written local
   is effectively immutable, so every fold that fired under `Let` still fires — emitted bytes survive.

```teko
/**
 * lp_is_const_binding — true iff a local's value is a compile-time constant the folder may propagate:
 * a `const` binding, OR a `var` local written EXACTLY ONCE on every flow path (flow-single-assignment),
 * which is effectively immutable. Re-bases the pre-merge `Let || Const` rule onto single-assignment so
 * the const-folds — and thus the emitted bytes — survive the `Let`→`Var` collapse. This is the one
 * byte-mover of the merge; the fixpoint byte-identity is its proof.
 *
 * @param b    the local binding under analysis
 * @param env  the checker environment (for the flow single-assignment fact)
 * @return     true iff `b` is a const or a once-assigned `var` (foldable)
 * @since 0.3.1
 */
fn lp_is_const_binding(b: Binding, env: Env): bool
```

7. **Confirm byte-neutrality.** The compiler's own `src/` never reassigns a `let`, so removing the
   rejection cannot change how `src/` lowers; both `let` and `mut` already lower to the same writable
   slot — no storage difference. `gen2==gen3` unaffected, gated on CF3 holding the folds.

## Rulings & laws

- **Teko-only:** parser/lexer/checker `.tks`; no C twin.
- **CLAUDE.md "Só existe VAR e CONST":** `BindKind = enum { Var; Const }`; `Mut`/`Let`→`Var`, no
  re-thinking; params immutable by B.21 (separate axis).
- **CLAUDE.md "NÃO DETECTAR/BARRAR O QUE NÃO EXISTE":** the "declare it `mut`" messages and any
  `BindKind::Mut`/`Let` branch are DEAD CODE to REMOVE; a re-worded message referencing the inexistent is
  a band-aid — the root cause is the dead detection. No error message may reference `let`/`mut`.
- **W15:** full Javadoc on `lp_is_const_binding`; no `//`.
- **Additive window:** `let`/`mut` still PARSE (soft-deprecated no-op) so the current seed builds; the
  DROP of `let`/`mut` acceptance is SM-S2 (`0032`, M2).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The self-build uses `var`-equivalent locals throughout, and the CF3 fold path is exercised by the
fixpoint — but the ACCEPT of the new `var` keyword and the const-reassign REJECT are not both self-
exercised as oracles; and `cf3_fold_survives_let_merge` is the load-bearing byte-move proof:

| fixture | asserts | expected |
|---|---|---|
| `var_all_locals_mutable` | `var a = 0; a = 1` compiles; `const` still rejects reassign | 0 |
| `mut_accepted_as_var_softdep` | `mut a = 0` still parses during the additive window | 0 |
| `cf3_fold_survives_let_merge` | a fold that fired under `Let` still fires under flow-single-assign | 0 |

## Gate

`[dry]` — compile + the three fixtures + fixpoint (byte-identical, gated on CF3 holding the folds).
"Green" = `var`/`let`/`mut` all parse to the merged kind, `const` still rejects reassign, the CF3 folds
survive, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`var` is a keyword, `BindKind` is `{ Var; Const }` (`Let`/`Mut` merged, still parsing as no-op spellings),
the three ergonomic gates always-pass for locals with the dead `mut` messages removed, CF3 re-based on
flow-single-assignment holds every fold, and the three fixtures are exit `0` with a byte-identical build.
