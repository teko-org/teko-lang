---
seq: 0019
crumb-id: 9D-T1
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/plano-9d-migracao-variant.md:334-352"                 # 9D §5.1(1) additive accept
  - "docs/design/plano-9d-capabilities-inline-uniao-v1.md:93-145"      # capability (a) parser patch
  - "docs/design/mudancas-superficie-0.3.1.md:402-449"                 # Doc-2 §9.2b/§9.D union positions
---

# 0019 · 9D-T1 — inline-union: accept `A | B` structural union in field position

> Inline-union: accept `A | B` structural union in field position (additive; `variant` still lives).

## Goal

Extend the structural `|` union — already valid in var/param/return position — to STRUCT FIELD position,
so `struct { x: A | B | C }` parses and checks. This is the ADDITIVE accept that will let the ~29 named
`variant` ADTs be replaced by inline-por-extenso unions later (§9.D); `type X = variant` STILL LIVES in
this crumb (its removal is 9D-EXP, `0094`, M3, after every inline site is migrated). The load-bearing
parser piece is the READY, BUILD-VERIFIED `parse_grouped_type` patch (capability (a) — a parenthesized
type group `(A | B)`, the only spelling that puts a `|` union in a type-PRIMARY position such as a slice
element `[](A | B)` or a generic argument). Precedence `[]` > `|`: `[]A | B` = `([]A) | B`; a slice of
the union needs `[](A | B)`. Additive and inert on the corpus (a `(` in a type position is presently
always an error). Its seed folds into SM-R1.

## Where

- `src/parser/parse_type.tks:148` — `parse_type_primary` — insert ONE `else-if` arm for `LParen` between
  the `LBracket` (slice) branch and the `is_delegate_kw_at` branch, delegating to `parse_grouped_type`.
- `src/parser/parse_type.tks` — NEW `parse_grouped_type` beside `parse_slice` (the BUILD-VERIFIED patch,
  reproduced verbatim below — self-hosts, 3572 MB).
- `src/checker/*` — field-position union: a field typed `A | B | C` is a structural union (the value IS
  one of the branches); reuse the existing var/param/return union checking for the field slot.
- `type X = variant` — UNTOUCHED (still lives); its removal is 9D-EXP (`0094`).

## How

1. **Add the grouped-type parser** (capability (a), copy VERBATIM):

```teko
/**
 * parse_grouped_type — a PARENTHESIZED type group `(A | B | …)`, the only spelling that puts a `|`
 * union in a type-PRIMARY position (a slice element `[](A | B)`, a generic argument `List<(A | B)>`,
 * or a member of a wider union `(A | B) | C`). `pos` is at the `(` (the caller checked `LParen`). The
 * body is a FULL union-level type (`parse_type`), so an inner `|` binds inside the parens exactly as a
 * top-level union does; a single-member group `(A)` collapses to `A` (no needless `UnionType`, mirroring
 * `parse_type`'s M.5 rule). Separators may lead the closing `)` so a wide group may wrap across lines,
 * like a multi-line `variant` used to. A pending compound-`>` close (a nested generic mid-close) cannot
 * be followed by `)`, so it is a parse error here rather than a silently swallowed token.
 *
 * @param tokens  the token stream
 * @param pos     the index of the opening `(`
 * @return        the parsed inner union (or its sole member) as a primary, consuming through the `)`
 * @throws        when the group is not closed by `)`, or a nested generic `>>` close reaches the `)`
 * @since         §9.D capability (a)
 */
fn parse_grouped_type(tokens: []lexer::Token, pos: u64): ParsedType | error {
    var inner = match parse_type(tokens, pos + 1) { ParsedType as x => x; error as e => return e }
    if inner.pending_gt > 0 {
        return err_at(tokens, inner.next, "a grouped type '(A | B)' cannot close inside a generic's '>>' — parenthesize the inner type separately")
    }
    var q = skip_seps(tokens, inner.next)
    if !is_kind_at(tokens, q, lexer::TokenKind::RParen) {
        return err_at(tokens, q, "expected ')' to close a grouped type '(A | B)'")
    }
    ParsedType { node = inner.node; next = q + 1; pending_gt = 0 }
}
```

2. **Add the one-line branch** to `parse_type_primary`'s head chain (after the `LBracket` arm):

```teko
    } else if is_kind_at(tokens, pos, lexer::TokenKind::LParen) {
        match parse_grouped_type(tokens, pos) { ParsedType as x => x; error as e => return e }
```

   Existing fns touched: `parse_type_primary` (`parse_type.tks:148`, one new `else-if`) — nothing else.
   `parse_type`/`parse_slice`/`skip_seps`/`err_at` are reused unchanged. A `(` in a type position is
   presently always an error, so the arm is INERT on the corpus.
3. **Field-position union checking.** A struct field typed `A | B | C` is a structural union — reuse the
   existing var/param/return union type checking for the field slot (the value IS one of the branches; the
   union `|` already emits its box descriptor `{tag; ptr; len}`, so recursion closes by that box).
4. **Precedence `[]` > `|` (Doc-2 ruling):** `[]A | B` = `([]A) | B`; a slice whose element is the union
   is `[](A | B)`. The grouped-type patch is exactly what makes `[](A | B)` express that.
5. **`variant` still lives.** Do NOT touch `type X = variant` or the `Variant` carrier — they are removed
   in 9D-EXP (`0094`) only after every inline site is migrated (M2). This crumb is purely additive accept.
6. **Confirm byte-neutrality.** The corpus has no `(` in a type position and no field union yet, so the
   arm is inert — `[dry]` build byte-identical.

## Rulings & laws

- **Teko-only:** parser/checker `.tks`; no C twin.
- **W15 full Javadoc** on `parse_grouped_type` (verbatim above) and any helper; no `//`.
- **Doc-2 §9.2b / §9.D (owner ruling):** the union `|` survives ONLY in var/param/return/field/constraint
  position, NEVER as a named `type` (`type X = A | B` stays rejected; `type X = variant` is removed in
  9D-EXP). This crumb adds the FIELD position; the named-form removal is separate.
- **Additive/inert:** a `(` in type position was always an error, so the patch is inert on the corpus;
  `variant` still lives.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

The corpus has no field union yet (migration is M2), so the accept path is NOT self-exercised — isolated
oracles required:

| fixture | asserts | expected |
|---|---|---|
| `field_inline_union_accept` | `struct { x: A \| B \| C }` parses, checks, and a value is one of the branches | 0 |
| `slice_of_union_grouped` | `[](A \| B)` parses (grouped) while `[]A \| B` = `([]A) \| B` (precedence) | 0 |
| `grouped_type_unclosed_reject` | `(A \| B` without `)` is a parse error | EXPECT_COMPILE_FAIL |
| `variant_still_lives` | `type X = variant { … }` still parses (removal is 9D-EXP, not here) | 0 |

## Gate

`[dry]` — compile + the fixtures + fixpoint (byte-identical; arm inert on the corpus). "Green" =
`parse_grouped_type` accepts `(A | B)` in type-primary position, field unions parse+check, precedence `[]`
> `|` holds, `variant` still lives, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`parse_grouped_type` + the `LParen` arm accept a parenthesized union in type-primary position (slice
element, generic arg, wider-union member), struct fields accept an inline `A | B | C` union, precedence
`[]` > `|` holds, `type X = variant` still lives, the fixtures pass, and a `[dry]` build is byte-identical.
