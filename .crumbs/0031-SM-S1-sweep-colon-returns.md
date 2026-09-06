---
seq: 0031
crumb-id: SM-S1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [SM-R1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1201-1202"   # §10 Phase S — S1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:191-196"     # §4.2 byte-preserving post-sweep
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1325"        # §12 fixpoint is the load-bearing gate
---

# 0031 · SM-S1 — sweep `src/` + `.tkt` to `:` returns

> Sweep `src/` + `.tkt` to `:` returns — the mechanical source rewrite of every `-> T` return to `: T`,
> against a seed that already accepts both.

## Goal

AUDIT/VERIFY-ONLY (already applied in src): confirm that EVERY return-type annotation in `src/` and the
`.tkt` corpus has been rewritten from the old `-> T` form to the unified `: T` form (`fn f(params): T { }`).
The sweep was done after SM-R1 (`0030`) captured a seed accepting both `Arrow` and `Colon` (the additive G1
crumb, `0007`). The source is byte-identical after the sweep: `:` and `->` lex to distinct tokens but parse
to the IDENTICAL return-type AST node, so the rewritten source emits the same bytes — the fixpoint
`gen2==gen3` byte-identity is the proof. The `Arrow`/`->` acceptance STAYS in the lexer/parser through this
crumb (its removal from the lexer + `token.tks`, plus the FFI migration to opaque `ptr`, is the SEPARATE M3
expurgo crumb SM-S4, `0091`). This crumb is a verify-only audit; the work is DONE in src.

## Where

- `src/**/*.tks` — every return annotation `) -> T` → `): T` (the mechanical rewrite; the whole compiler
  source).
- `src/**/*.tkt` + the `.tkt` corpus — every `-> T` return in the test/expectation files → `: T`.
- `src/parser/parse_decl.tks:359` — `parse_function` return parse (accepts `Arrow` OR `Colon` since G1) —
  UNCHANGED here; still accepts both (the `Arrow` branch is deleted in SM-S4, M3).
- `src/lexer/token.tks:85` — `Arrow // ->` — UNCHANGED (still lexes; removed in SM-S4). `:86` `FatArrow`
  (`=>`) NEVER touched.

NEW: no new surface; this is a pure mechanical source rewrite gated by byte-identity.

## How

**VERIFY-ONLY audit (work already done):** No fresh sweep is needed — the source is already swept.

1. **Confirm zero residual `->` return operators.** Audit `src/` and `.tkt` to confirm there are ZERO remaining
   `-> T` return annotations (the sweep is complete). A search for `)\s*->` should yield ZERO matches in
   real source (comments/docstrings excepted).
2. **Confirm `=>` (`FatArrow`) is untouched.** Verify that match arms and lambda expressions still use `=>`,
   confirming the old token was swept only in return position.
3. **Verify parser acceptance.** Confirm that the parser still accepts `Arrow` OR `Colon` (`parse_decl.tks:359`,
   from G1); a stray un-swept `->` should still compile without error. (This acceptance is deleted in SM-S4,
   `0091`, M3, after all `->` is gone and FFI is migrated.)
4. **Byte-identity is the proof.** Build gen2 on the SM-R1 seed, run the scoped regression, and prove
   `gen2==gen3` byte-identical — the proof that the sweep introduced no semantic changes.
5. **Mark clean.** Once audit confirms zero residual `->`, the sweep is DONE-verified.

## Rulings & laws

- **Teko-only:** source `.tks`/`.tkt` rewrite; the parser/lexer are unchanged (no C twin touched).
- **W15 full Javadoc:** unaffected — the rewrite touches return operators, not doc-comments; no `//`
  introduced.
- **Byte-preserving after the sweep (§4.2):** `:` and `->` parse to the IDENTICAL AST node → same bytes; the
  fixpoint proves it.
- **`Arrow` removal is SEPARATE (SM-S4, M3):** this crumb keeps `->` acceptance; it is NOT a removal/expurgo.
- **Sweep `.tkt`/`.tkr` after the surface change (safety law):** the corpus is rewritten in lockstep.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744`; commit each green
  batch; NO reseed (fixpoint-rebuild core-consumes the SM-R1 seed); fixpoint `gen2==gen3` byte-identical.

## Fixtures

`none — the fixpoint self-build exercises this`. The sweep rewrites the compiler's OWN source; building gen2
on the swept source and proving `gen2==gen3` byte-identical is exactly the exercise — a scoped `.tkr` would
be redundant (any parse regression fails the build directly).

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity. "Green" =
audit confirms ZERO residual `->` return operators in `src/` + `.tkt` (sweep complete + byte-identical),
the parser still accepts both `Arrow` and `Colon` (removal is SM-S4), the build is byte-identical
to pre-sweep (`gen2==gen3`). Reseed-class: `fixpoint-rebuild` (core-consumes the SM-R1 seed; teaches nothing;
this crumb is verify-only).

## Deps

`SM-R1` (the seed must ACCEPT `:` returns before the source is swept to them).

## Done when

Audit confirms every return annotation in `src/` and the `.tkt` corpus is `: T` (zero residual `->` return
operators), `=>` is untouched, the `Arrow` acceptance remains (its removal is SM-S4), and gen2 built on the
SM-R1 seed is byte-identical (`gen2==gen3`). The sweep is DONE-verified.
