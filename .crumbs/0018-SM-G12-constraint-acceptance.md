---
seq: 0018
crumb-id: SM-G12
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/mudancas-superficie-0.3.1.md:402-449"                # Doc-2 §9.2b constraint union
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1146-1148"# umbrella §10 (Phase G family)
  - "000-INDEX.md:72"                                                  # owner 2026-08-19 (CI 965)
---

# 0018 · SM-G12 — generic-constraint acceptance (a constraint accepts ANY type)

> Generic-constraint acceptance: a constraint accepts ANY type (named, `[]A`, `error`, disjunctions like
> `[]A | A | error`) + markers (`class`/`struct`/`service`/`notnull`) + interfaces + service lifetimes;
> rejects ONLY traits. Closes the §9.2b gap; broadens the constraint-term grammar to a full type.

## Goal

The delivered §9.2b shape/constraint solver (`cf0c70b5`) left a GAP: the parser's "constraint term"
grammar accepts a NAME, not a full type, so `<T: []A | A | error>` currently FAILS (observed CI 965,
error verbatim: *"expected a constraint term (a form word `class`/`service`/`struct`, an interface/type
name, or `notnull`) in a type-parameter constraint"*). This crumb BROADENS the constraint-term grammar
to a FULL TYPE: a constraint is a disjunction of conjunctions `<T: Alt1 | Alt2 | …>` where each `Alt` is
`Term & Term & …`, and a Term may be ANY type — a named type, a slice `[]A`, `error`, a concrete type
(`str`), disjunctions written by extension — PLUS the special markers (`class`/`struct`/`service`,
`service` taking a lifetime `singleton`/`scoped`/`transient`), interfaces, and `notnull` (the one term
that enters ONLY via `&`, never `|`). It REJECTS ONLY traits. Byte-preserving (broadens acceptance —
nothing previously compiling changes); its seed folds into SM-R1.

## Where

- `src/parser/parse_type.tks` — the "constraint term" grammar (the site the CI-965 message fires from) —
  broaden a Term from a NAME to a FULL type (`parse_type`), so `[]A`, `error`, concrete types, and
  parenthesized inner unions are accepted in a constraint term.
- `src/checker/resolve.tks` / the §9.2b constraint solver (`cf0c70b5`) — accept the broadened term set;
  a constraint is a disjunction of conjunctions; each `Alt` = `Term & Term & …`.
- The constraint diagnostic — broaden its message (the verbatim CI-965 string is superseded by this
  crumb): the term set is now "a form word, a service lifetime, an interface/type/slice/`error`, a
  disjunction, or `notnull`", and the ONLY rejected term is a trait.

## How

1. **Broaden the term grammar** (`parse_type.tks`): a constraint Term parses as a FULL type via
   `parse_type` (reusing SM-G-family / 9D-T1 grouped-type support for parenthesized inner unions), not a
   bare NAME. So `<T: []A | A | error>` and `<T: (A | B) & Ifce>` parse.

```teko
fn parse_constraint_term(tokens: []lexer::Token, pos: u64): ParsedConstraintTerm | error
```

2. **Wire the solver acceptance** (§9.2b): a constraint is `<T: Alt1 | Alt2 | …>`, each `Alt = Term &
   Term & …`. The terms: FORM words (`class`/`service`/`struct` force the type's FORM; `service` accepts a
   lifetime, forcing HOW the service lives — a non-`singleton` transport fails at the `make`, an HONEST
   failure not a silent coercion); interfaces + concrete types force conformance/identity; `notnull` is
   the ONLY term that enters solely via `&` (a modifier, not a form-alternative — forbids the generic
   being null, even in the definition). Any named type / `[]A` / `error` / disjunction is now accepted.
3. **Precedence `[]` > `|`** (Doc-2 ruling): `[]A | B` parses as `([]A) | B`; an array whose element is
   the union must be parenthesized `[](A | B)` (this reuses 9D-T1's grouped-type; the constraint grammar
   inherits it).
4. **Reject ONLY traits.** The single disallowed constraint term is a trait; everything else (named,
   slice, `error`, concrete, disjunction, form word, interface, lifetime, `notnull`) is accepted.
5. **Broaden the diagnostic** (supersedes the CI-965 verbatim string): the new message names the full
   term set and states that a trait is the only rejected term — file:line:col + short cause per CLAUDE.md
   message style.
6. **Confirm byte-neutrality.** This BROADENS acceptance — every constraint that compiled before still
   compiles identically; only previously-rejected forms newly parse. `[dry]` build byte-identical.

## Rulings & laws

- **Teko-only:** parser/checker `.tks`; no C twin.
- **Comment convention (W15, owner 2026-08-19):** `parse_constraint_term` and its helpers are internal
  (non-`exp`) parser fns → they carry NO `/** */` doc-comment; no `//` or `/* */` either. A `/** */` is
  allowed ONLY on an `exp` decl and never larger than the code it documents — canonicalizer/reviewer, not
  the compiler.
- **Owner 2026-08-19 (CI 965) + Doc-2 §9.2b:** a constraint accepts ANY type + markers + interfaces +
  service lifetimes; rejects ONLY traits. Forcing the service lifetime in a constraint is HONEST (a
  non-singleton transport fails at the `make`, not silently redirected).
- **Message style (CLAUDE.md):** the broadened diagnostic is `file:line:col: short cause`, no novel/ref —
  the CI-965 verbatim string is superseded.
- **Byte-preserving:** broadening acceptance changes nothing previously compiling.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` may already use simple constraints (exercised by the fixpoint), but the newly-accepted broad forms
and the trait-only reject are NOT self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `constraint_union_full_type` | `<T: []A \| A \| error>` parses and checks (the CI-965 form) | 0 |
| `constraint_form_and_iface` | `<T: class & Ifce \| struct & OtherIfce \| str>` checks | 0 |
| `constraint_service_lifetime` | `<K: service singleton & IChannelKind<T>>` checks; a non-singleton transport fails at `make` | 0 (and the reject arm compile-fails) |
| `constraint_notnull_only_via_and` | `<T: notnull>` ok; `notnull` alone via `\|` rejected | 0 / EXPECT_COMPILE_FAIL |
| `constraint_trait_rejected` | a trait as a constraint term is rejected (the sole disallowed term) | EXPECT_COMPILE_FAIL |

## Gate

`[dry]` — compile + the fixtures + fixpoint (byte-identical; acceptance broadened only). "Green" =
`<T: []A | A | error>` and the form/interface/lifetime/`notnull` constraints parse and check, a trait is
the only rejected term, `[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—` (inherits 9D-T1's grouped-type for parenthesized inner unions but is not a hard build dependency —
the constraint grammar reuses `parse_type`).

## Done when

A constraint term parses as a full type (named / `[]A` / `error` / concrete / disjunction / form word /
service lifetime / interface / `notnull`-via-`&`), a trait is the only rejected term, the broadened
diagnostic replaces the CI-965 message, the fixtures pass, and a `[dry]` build is byte-identical.
