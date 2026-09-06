---
seq: 0007
crumb-id: SM-G1
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:174-196"  # §4 -> to :
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1170"     # §10 Phase G — G1
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1104"     # §9.1 byte-preserving post-sweep
---

# 0007 · SM-G1 — additive `:` return operator (accept `Arrow` OR `Colon`)

> Additive `:` return operator (accept `Arrow` OR `Colon`) — unify the type-annotation operator to `:`.

## Goal

Unify the type-annotation operator to `:` EVERYWHERE (var, param, field, return): `fn f(params): T { }`.
This crumb is the ADDITIVE half — the parser learns to accept `:` in return position while STILL
accepting `->` (`Arrow`), so the current seed (which only knows `->`) keeps parsing and the source sweep
(SM-S1, `0031`) can later rewrite `src/` to `:` against a seed that already accepts it. The eventual drop
of `->`/`Arrow` from the lexer is a SEPARATE M3 crumb (SM-S4, `0091`). `=>` (`FatArrow`) is NEVER
touched. It is byte-preserving: `:` and `->` lex to distinct tokens but parse to the IDENTICAL
return-type AST node, so the same program emits the same bytes. Written in OLD spelling (the seed
parses it); its seed folds into SM-R1.

## Where

- `src/parser/parse_decl.tks:359` — `parse_function` return parse. Today:
  `if is_kind_at(tokens, p, lexer::TokenKind::Arrow)`. Change to accept `Arrow` OR `Colon` after the `)`.
- `src/lexer/token.tks:85` — `Arrow // ->` — UNCHANGED here (kept emitting `->`); `:86` `FatArrow` (`=>`)
  untouched.
- `src/lexer/lexer.tks` — UNCHANGED (`:` already lexes; no new token).

## How

1. **Broaden the return-operator accept.** In `parse_function` (`parse_decl.tks:359`), replace the
   `Arrow`-only guard with an accept of EITHER `Arrow` OR `Colon` after the closing `)` of the param
   list. `:` after `)` is unambiguous: a block `{` cannot start there, and there is no other `:` position
   after a param list. Both spellings produce the IDENTICAL return-type AST node (parse the type that
   follows exactly as today).
2. **Do NOT drop `->`.** The lexer keeps emitting `Arrow`; the parser accepts both. Dropping `Arrow`
   from the lexer + `token.tks` is SM-S4 (`0091`, M3), gated on the sweep having removed every `->` from
   `src/`.
3. **Leave `=>` alone.** `FatArrow` (match arms, closures) is a distinct token in a distinct position —
   never touched by this change.
4. **Confirm byte-neutrality.** `src/` still spells returns with `->`, so it parses to the same AST and
   emits the same bytes — a `[dry]` build is byte-identical. The `:` acceptance is inert until adopted.

No new `fn`/`type` — this is a one-guard broadening in `parse_function`.

## Rulings & laws

- **Teko-only:** `src/parser/parse_decl.tks`; no C twin.
- **Additive-transition law (§4.1 / §9.2):** the seed must ACCEPT both before the sweep; write the
  acceptance in OLD spelling so the current seed parses this crumb (bootstrap-additive).
- **W15:** `parse_function`'s doc-comment stays; no `//`.
- **NO tombstone / removal here** — this is purely additive; the `->` drop is SM-S4, a separate M3 crumb.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step;
  no reseed (`[dry]`, folds into SM-R1).

## Fixtures

The self-build does NOT yet use `:` returns (it still says `->`), so the fixpoint does not exercise the
new accept — an isolated accept fixture is required:

| fixture | asserts | expected |
|---|---|---|
| `colon_return_operator` | `fn f(): T { }` parses; `->` also parses (additive window) | 0 |

(The reject `arrow_token_removed` — `->` no longer parsing — is a SM-S4/M3 fixture, NOT here; `->` still
parses in this crumb.)

## Gate

`[dry]` — compile + the `colon_return_operator` fixture + trivial fixpoint (bytes unchanged, `src/`
still uses `->`). "Green" = both `:` and `->` parse to the same return-type AST, `[dry]` build
byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

`parse_function` accepts `:` OR `->` after the param list (both to the identical AST node), the
`colon_return_operator` fixture is exit `0`, and a `[dry]` build is byte-identical (`->` still parses).
