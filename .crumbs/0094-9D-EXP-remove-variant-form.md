---
seq: 0094
crumb-id: 9D-EXP
milestone: M3
gate: "[RITUAL]"
reseed-class: "expurgo"
deps: [9D-T1]
sources:
  - "docs/design/plano-mestre-0.3.1-implementacao.md:264"              # M3 9D-EXP row
  - "docs/design/plano-mestre-0.3.1-implementacao.md:253-257"          # no-tombstone expurgo discipline
  - "src/parser/ast.tks:205"                                           # VariantBody carrier
  - "src/parser/parse_decl.tks:975"                                    # `type X = variant` production
  - "src/parser/macro_expand.tks:130"                                  # VariantBody lowering
---

# 0094 · 9D-EXP — remove `type X = variant` form + `Variant` carrier

> The E3 inline-union expurgo: with every `type X = variant` site migrated to the structural inline union
> `A | B` (9D-T1, additive, already live), DELETE the `variant` declaration form leaves→roots (`Type`
> last) and the `Variant`/`VariantBody` carrier from parser + checker. NO tombstone diagnostic.

## Goal

The delivered 9D-T1 (`0019`) taught the checker to ACCEPT a structural inline union `A | B` in field
position, additively, WHILE the legacy `type X = variant { … }` form still lived. By this seq every
inline-union site in `src/` has been rewritten to the structural form, so the `variant` form has no
producer. This crumb removes it: the lexer `Variant` token, the `parse_decl` production
(`parse_decl.tks:975`), the `VariantBody` AST carrier (`ast.tks:205`) and its macro-lowering
(`macro_expand.tks:130`), and every checker rule that only serviced the declared-`variant` shape. It is a
**byte-mover** expurgo (the self-compile enumerates residual references) driving an expurgo reseed (E3).
Removal is leaves-first, root (`Type`) last, so no intermediate build references a half-removed carrier.
No tombstone: `type X = variant { … }` becomes an unexpected-token/unknown-form parse error, exactly what
a never-existent keyword would give.

## Where

Remove in leaves→roots order (a producer is deleted only after ITS producers are gone):

- `src/parser/parse_decl.tks:975-977` — the `is_kind_at(… TokenKind::Variant)` production returning
  `VariantBody` — DELETE the production.
- `src/lexer/token.tks` + `src/lexer/lexer.tks` — the `Variant` `TokenKind` + its keyword recognition —
  DELETE (last of the lexer clean).
- `src/parser/ast.tks:205` `VariantBody` + `src/parser/ast.tks:227` `TypeBody()` macro union arm — remove
  `VariantBody` from the `TypeBody` lowering set and delete the type.
- `src/parser/macro_expand.tks:130-132` — the `VariantBody as vb` lowering arm — DELETE.
- `src/checker/borrow.tks:7,43`, `src/checker/check_modules.tks:294`, `src/checker/collect.tks`,
  `src/checker/consteval.tks:8`, `src/checker/expr.tks:18-36` — every `parser::VariantBody`/declared-
  `Variant`-form arm — DELETE the arms that only matched the DECLARATION form. The STRUCTURAL union type
  (`@Type()` `Variant`/union representation produced by 9D-T1) STAYS — distinguish the two: the removal
  targets the `VariantBody` DECLARATION carrier, not the checker's internal union `Type`.

Note the internal `Type::Variant` union representation (`consteval.tks:8`, `expr.tks:20`) is the RUNTIME
shape 9D-T1 feeds; keep it. Only the source-level `type X = variant` declarator and its `VariantBody`
carrier are removed.

## How

1. **Confirm zero producers** of the declaration form: grep `src/` for `= variant` and `VariantBody`
   construction sites; all must be migrated to `A | B` by this seq. A live producer is a 9D-T1 migration
   gap, REPORTED up.
2. **Remove leaves first:** the `parse_decl` production (`:975`) and the `macro_expand` lowering
   (`:130`) — these are the FIRST to lose a caller once the token is unrecognised. Then the `VariantBody`
   arms across the checker (`borrow`/`check_modules`/`collect`/`consteval`/`expr`).
3. **Remove the carrier** (`ast.tks:205` `VariantBody`) and drop it from the `TypeBody()` macro union
   (`ast.tks:227`) — the root of the declaration form.
4. **Remove the lexer token LAST** (`token.tks`/`lexer.tks` `Variant`) — nothing recognises the keyword
   after; `type X = variant` now lexes `variant` as a plain identifier and the parser rejects it with a
   generic unexpected-token error.
5. **Distinguish the internal union type — DO NOT remove it.** The `@Type()` union representation 9D-T1
   produces from `A | B` is load-bearing; only the `VariantBody` DECLARATION carrier goes.
6. **NO tombstone** (no-tombstone law): add NO "variant form removed / use `A | B`" message; the error is
   the generic unexpected-token/unknown-form one.
7. **Reseed ITERATIVELY** (ensinar→seed→sweep→seed) until `gen2==gen3` byte-identical.

## Rulings & laws

- **Teko-only:** parser/lexer/checker `.tks`; no C twin.
- **W15 full Javadoc** on survivors; removed decls carry no doc.
- **Removals = clean expurgo, NO tombstone:** `type X = variant` becomes an ordinary unexpected-token
  parse error (`plano-mestre:253-257`, owner ruling 3).
- **Não detectar/barrar o que não existe:** once the form is gone, every `VariantBody` checker arm is
  dead code to REMOVE, not reword.
- **Leaves→roots + build-first:** remove producers before the carrier before the token, so no
  intermediate build references a half-deleted shape; the self-compile enumerates any residual.
- **Safety:** NEVER `teko test .`; `ulimit -v 6815744` per build; commit each green sweep; reseed ONLY at
  this [RITUAL]; E3 harvest at `gen2==gen3`; sweep `.tkt`/`.tkr` after the AST/signature deletions.

## Fixtures

The self-build fixpoint exercises the surviving structural-union path (the compiler uses `A | B`
internally). The reject of the removed declaration form is never taken by the self-build:

| fixture | asserts | expected |
|---|---|---|
| `expurgo_variant_form_unexpected_not_tombstone` | `type X = variant { i32 \| str }` fails with a GENERIC unexpected-token/unknown-form parse error (no "variant removed" text) | `EXPECT_COMPILE_FAIL` |
| `inline_union_still_lives` | a struct field typed `A \| B` (structural, 9D-T1) still checks | `0` |

## Gate

`[RITUAL]` — full native ladder + a genuine expurgo reseed (E3). "Green" = the `variant` declaration form
+ `VariantBody` carrier are gone, `A | B` structural unions still check, the reject fixture compile-fails
with a GENERIC message, and `gen2==gen3` byte-identical after the E3 harvest. Reseed-class: `expurgo`.

## Deps

`9D-T1` (structural inline union accepted) + all inline-union sites migrated — verbatim from 000-INDEX.

## Done when

`type X = variant` no longer parses (generic unexpected-token error, no tombstone), the `VariantBody`
carrier and its checker arms are deleted, structural `A | B` unions still check, the fixtures pass, and
the E3 reseed lands `gen2==gen3` byte-identical.
