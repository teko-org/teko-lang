---
seq: 0091
crumb-id: SM-S4
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [SM-S1]
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:174-196"     # §4 `->` return operator → `:`
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:146-171"     # §3 remove `-> ref T` (already SM-G4)
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1207-1208"   # Phase S — S4
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1276"        # fixture `arrow_token_removed`
  - "src/lexer/token.tks:78"                                             # FatArrow (Arrow's neighbour; `=>` never touched)
---

# 0091 · SM-S4 — drop `->`/`Arrow` from lexer + `token.tks`; migrate `src/` FFI to opaque `ptr`

> Drop the `->`/`Arrow` token from the lexer + `token.tks` and its dead parser branch (all returns are `:` after
> SM-S1); migrate `src/` FFI to the opaque `ptr` boundary. Clean expurgo — NO tombstone. E1 expurgo reseed.

## Goal

ALREADY EXPURGATED (re-measured 2026-08-19): The `Arrow` (`->`) token is RETIRED — `src/lexer/token.tks:85`
documents "`Arrow` (`->`) is RETIRED", and only `FatArrow` remains. The lexer does NOT scan `->`, the token
is NOT in `token.tks`, and the parser has NO `Arrow`-accepting return branch. This crumb is VERIFY-ONLY:
audit confirms the expurgo is clean (no `Arrow` token, no residual `->`), then migrate `src/` FFI to the
opaque `ptr` boundary (a foreign `ptr` is crossed with `.__wrap<T>()`, §5 — already seeded), completing the
"no raw pointer arithmetic on the surface" posture. The state-divergence flags that SM-S1's precondition
(sweep all `->` returns to `:`) ran to completion ahead of this manifest slot, so the token expurgo already
landed. This is a byte-MOVER (the lexer/parser change moved emitted bytes) from the E1 expurgo reseed
`{SM-S4, SM-S5}`. Removal was CLEAN — no tombstone, no bespoke diagnostic. `=>` (`FatArrow`, `token.tks:78`)
is NEVER touched.

## Where

- `src/lexer/lexer.tks` — remove the `->` scan arm (the two-char `-`+`>` → `Arrow` symbol). `--`/`-=`/`-` and
  `=>` (`FatArrow`) scans are untouched.
- `src/lexer/token.tks` — delete the `Arrow` `TokenKind` variant (historically `:85`; `FatArrow` at `:78`
  remains). This shifts no wire ordinal that a `.tkb` reader depends on for tokens (tokens are not serialized),
  but is a lexer-grammar change → byte-mover.
- `src/parser/parse_decl.tks` — in `parse_function`'s return parse, delete the `Arrow`-accepting branch (accept
  ONLY `Colon` after `)`). `ref` in PARAMETER position is untouched (F1 borrow direction survives).
- `src/` FFI sites — any `extern fn` still surfacing a raw/`ref`-return pointer is migrated to return the opaque
  `ptr`, crossed at the boundary with `.__wrap<T>()` (§5, seeded). `extern fn` itself is untouched.

## How

**VERIFY-ONLY audit (Arrow already expurgated):** The lexer, token, and parser are already updated. The task
is to (a) confirm clean state, (b) migrate FFI, and (c) verify the expurgo/reseed boundary.

1. **Confirm the Arrow token is gone (build-first).** Verify `src/lexer/token.tks` does NOT contain an `Arrow`
   variant — only `FatArrow` (`=>`) is present. Verify the self-compile + lint enumerates ZERO `Arrow`
   references in the codebase.
2. **Confirm the lexer scan is gone.** Verify `src/lexer/lexer.tks` does NOT contain the `->`→`Arrow` scan arm.
   `=>` (`FatArrow`) scan remains.
3. **Confirm the parser branch is gone.** In `parse_function`, verify there is NO `is_kind_at(..., TokenKind::Arrow)`
   return branch — only `Colon`-accepting remains. No other `:`-after-`)` position exists, so `:` return is
   unambiguous.
4. **Verify the sweep is complete (src state).** Confirm `src/` + `.tkt` contain ZERO `->` return operators
   (SM-S1's postcondition) and zero `-> ref T` return arms (SM-G4 removed the arm). A search for `)\s*->` should
   yield ZERO matches (comments/docstrings excepted).
5. **Migrate `src/` FFI to opaque `ptr`.** Any `extern fn` still returning a raw/`ref` pointer on the surface
   returns the opaque `ptr`; the call-site crosses it with `.__wrap<T>()` (§5). This completes the "no
   raw-pointer surface" posture for SM-S5 (the `unsafe` deletion).
6. **Confirm clean expurgo.** Verify NO tombstone: no "`->` was removed" rule, no deprecation shim, no migration
   hint anywhere. An old-form program simply fails with the generic unexpected-token error.
7. **Verify reseed state.** Confirm the new seed (E1 expurgo reseed capturing this boundary) no longer understands
   `->`. Build gen2 native (the corpus is `:`-only), FIXPOINT `gen2==gen3` (the lexer/parse change is byte-stable).

## Rulings & laws

- **Teko-only:** lexer/parser `.tks`; the C bootstrap twins are FROZEN — this is a `.tks` grammar change; no C
  twin edited.
- **W15 full Javadoc** on any touched declaration; flatten; no inline `//`.
- **Removals = CLEAN expurgo, NO tombstone (owner ruling 3 / master-plan §M3):** the lexer no longer tokenises
  `->`, the parser has no production, the checker no rule, NO special diagnostic — same generic error as a
  never-existent token.
- **Build-first:** SM-S4 CANNOT precede the SM-S1 sweep — the `Arrow` branch is removed only after every `src/`
  return is `:`.
- **`=>` untouched:** `FatArrow` (`token.tks:78`) is a distinct token for match arms — never removed.
- **Consolidated expurgo E1 `{SM-S4, SM-S5}` (master-plan §M3 note):** SM-S4 is the first half; SM-S5 follows;
  they share the "migration-complete" precondition.
- **Safety:** NEVER `teko test .` (fixtures ISOLATED); build gen2 in a subshell with `ulimit -v 6815744` (a
  blown guard is a root-cause fix, never a raised ceiling); reseed ONLY at this [RITUAL]; FIXPOINT `gen2==gen3`
  byte-identical; sweep `.tkt`/`.tkr` after the grammar change; commit each green step.

## Fixtures

The self-build no longer contains `->` (SM-S1 swept it), so it cannot assert the reject — write the one
non-self-build path (the no-tombstone guard):

| fixture | asserts | expected |
|---|---|---|
| `arrow_token_removed` | a program using `fn f() -> T` no longer parses — the GENERIC unexpected-token error, NOT a bespoke "`->` was removed" message (no tombstone); a program using `fn f(): T` still compiles | `EXPECT_COMPILE_FAIL` |
| `ffi_opaque_ptr_wrap` | an `src/`-style `extern fn` returning opaque `ptr`, crossed with `.__wrap<T>()`, round-trips without `unsafe` | `0` |

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed (state-divergence: Arrow already expurgated, FFI
migrate + fixtures + verify only). Build gen2 native (the `:`-only corpus), the two fixtures green, FIXPOINT
`gen2==gen3` byte-identical, reseed (the seed captured at this boundary no longer understands `->`). "Green"
= audit confirms `Arrow` token gone, zero `->` residual in src/+.tkt (sweep complete), `src/` FFI is
opaque-`ptr`, `arrow_token_removed` + `ffi_opaque_ptr_wrap` fixtures pass, gen2==gen3. **Reseed-class:
expurgo** (E1 boundary with SM-S5; captures the state where `->` is gone and FFI is opaque).

## Deps

`SM-S1` (all `:` swept — the precondition that the `Arrow` branch is dead before removal).

## Done when

Audit confirms `Arrow`/`->` is gone from the lexer + `token.tks` + `parse_function` (clean, no tombstone,
already expurgated), zero `->` residual in src/+.tkt (SM-S1 sweep complete), `src/` FFI is opaque-`ptr`,
`arrow_token_removed` + `ffi_opaque_ptr_wrap` fixtures pass, the fixpoint `gen2==gen3` holds, and the expurgo
reseed (E1 boundary, SM-S4+SM-S5) is captured. State-divergence handled: Arrow already gone, only FFI
migration + verify remains.

## State-divergence note (re-measured 2026-08-19)

- **Arrow is ALREADY EXPURGATED.** Confirmed on `origin/fix/retirement` @ `5790a012`:
  `src/lexer/token.tks:85` documents "`Arrow` (`->`) is RETIRED"; only `FatArrow` (`:78`) remains.
  `src/lexer/lexer.tks` scans ONLY `=>` (no `->` scan arm). `src/parser/parse_decl.tks` has NO `Arrow`
  reference. The earlier wave (SM-G1 additive `:` + the SM-S1 sweep) already landed the `Arrow`-token expurgo
  AHEAD of this manifest slot. **Residual work:** SM-S4's intent is DELIVERED via a reduced scope:
  (a) audit confirms the token/branch are absent (clean-state assertion, no edit),
  (b) the `src/` FFI→opaque-`ptr` migration (the only delta), and
  (c) authoring the `arrow_token_removed` fixture (no-tombstone guard).
  The reseed still fires as the E1 expurgo boundary (it also carries SM-S5). This is NOT a plan change — the
  crumb's INTENT (no `->` on the surface, clean, no tombstone) is delivered via the reduced path.
