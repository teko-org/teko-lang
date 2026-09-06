---
seq: 0016
crumb-id: SM-G10
milestone: M1
gate: "[dry]"
reseed-class: "(folds R1)"
deps: []
sources:
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:926-1013" # §7c.1 method overloading
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1082-1085"# §7c.3 G10 crumb
  - "docs/design/lang-evolution-0.3.1-memory-and-surface.md:1110"     # §9.1 byte-preserving until used
---

# 0016 · SM-G10 — method overloading (relax same-name reject to param-signature distinctness)

> Method overloading (relax same-name reject to param-signature distinctness).

## Goal

RELAX the checker's same-name-definition reject: same name is allowed IFF the PARAMETER signatures differ.
A single-def name resolves EXACTLY as today (fully additive for non-overloaded code). Resolution is on
PARAMETER signatures ONLY, never the return type (a call site provides argument types, not an expected
return — M.3 honesty). Distinct overloads mangle to distinct symbols FOR FREE via the existing AL4a
signature-mangling; a non-overloaded name's symbol is UNCHANGED (byte-preservation depends on this).
INERT until `src/` defines an overload set. Its seed folds into SM-R1; `src/` adoption (if any) is SM-S7
(`0035`, M2).

## Where

- `src/checker/revalidate.tks:6` — the "no duplicate definition" reject — RELAX to param-signature
  distinctness (identical param signatures remain a redeclaration error; the return type is NOT part of
  signature identity for the distinctness check).
- `src/checker/resolve.tks:147-218` — `TtCands`/`tt_cands` candidate-cursor machinery — REUSE for the
  overload candidate set (a linear candidate scan keeping the best match, ambiguity on a tie).
- `src/checker/resolve.tks:697/893` — the existing `ambiguous` diagnostic idiom — reuse for the tie error.
- The call path (the achatamento CK3 "overload resolution" step) — add `select_overload`.
- `src/codegen/codegen.tks:517-532` — `mangle_type_name` + the function-symbol mangler — confirm the
  signature component is emitted for OVERLOADED names ONLY (single-def names keep their current symbol).

## How

1. **Relax the definition rule** (`revalidate.tks:6`): an environment may bind N functions to one name if
   their PARAMETER signatures are pairwise distinct (by the AL4a signature identity used for mangling).
   Two defs with identical parameter signatures remain a redeclaration error. Two overloads differing
   ONLY in return type are a COMPILE ERROR (indistinguishable at every call site).
2. **Add `select_overload` at the call path** (extending CK3):

```teko
/**
 * select_overload — choose the one function an overloaded call resolves to, by PARAMETER signatures only
 * (never the return type). Filters the candidate set to those accepting `args`, prefers an EXACT
 * parameter-type match over one reached by the checker's existing implicit widenings (numeric-literal
 * context typing, `null`→union), and REJECTS a tie (>=2 equally-good candidates) as an ambiguous-call
 * compile error rather than picking silently. A single-def name trivially returns its sole member. A
 * generic candidate matches by unifying its type params and ranks at widening rank (a concrete exact
 * overload beats a generic one), then monomorphizes as today.
 *
 * @param cands  the functions bound to the called name (>=1)
 * @param args   the argument types at the call site
 * @return       the selected function, or an ambiguity/no-match error
 * @throws       when no candidate accepts `args`, or >=2 accept it equally well
 * @since 0.3.1
 */
fn select_overload(cands: []checker::TFunction, args: []Type): checker::TFunction | error
```

3. **Resolution rule:** collect the overload set; filter to candidates whose arity + param types ACCEPT
   `args`; then (1) exact param-type match wins outright; (2) else the existing coercion/widening ranks
   below exact; (3) a tie (≥2 equally good) is the ambiguous-call error (reuse `resolve.tks:697/893`).
4. **Mangling (free):** the function symbol already includes the parameter-signature component for
   generics; overloads reuse it. Confirm a NON-overloaded `f`'s symbol is UNCHANGED — do not add a
   signature suffix to single-def names (or every existing symbol moves). Only overloaded names get the
   disambiguating suffix.
5. **Methods vs free functions:** overloading applies to BOTH. For methods, the synthetic `self` (SM-G3)
   is `params[0]`; two methods overload on the NON-receiver params (the receiver type is fixed by the
   owning type). Method resolution dispatches by receiver type then name; overloading adds the
   param-signature selection after the name match.
6. **DI non-interaction (§7c.1):** the §7.2 DI conflict is about INTERFACE PROVIDERS (the DI table);
   overloading is about SAME-NAME FUNCTIONS (the fn environment). Different namespaces, no shared
   mechanism — state explicitly: they do not interact.
7. **Confirm byte-neutrality.** `src/` has zero same-name collisions (the reject guaranteed it), so
   enabling overloading cannot change how existing `src/` resolves or mangles — `[dry]` build
   byte-identical. Belt-and-braces: grep `src/` for accidental same-name defs (none can exist in
   compiling source).

## Rulings & laws

- **Teko-only:** checker/codegen `.tks`; no C twin.
- **W15 full Javadoc** on `select_overload` and helpers; no `//`.
- **Resolution by PARAMETERS only (M.3 honesty):** return-type-only difference is a compile error; a tie
  is never silently picked ("no magic values").
- **Byte-preservation depends on the single-def symbol staying unchanged** — only overloaded names get a
  suffix.
- **Byte-preserving until used (§9.1):** inert until an overload set exists.
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 6815744`; commit the green step.

## Fixtures

`src/` defines no overload set (adoption is SM-S7), so the resolution + both rejects are NOT
self-exercised — isolated oracles required:

| fixture | asserts | expected |
|---|---|---|
| `overload_resolves_by_params` | `f(i64)` and `f(str)` both defined; each call picks the right one | 0 |
| `overload_ambiguous_rejected` | two equally-good candidates for a call → compile error | EXPECT_COMPILE_FAIL |
| `overload_return_type_only_rejected` | two `f()` differing only in return type → compile error | EXPECT_COMPILE_FAIL |
| `single_def_symbol_unchanged` | a non-overloaded `f`'s emitted symbol is unchanged (byte-identity) | 0 |

## Gate

`[dry]` — compile + the four fixtures + fixpoint (byte-identical; `src/` has no overload set). "Green" =
overloads resolve by parameters, ties + return-type-only pairs are rejected, single-def symbols unchanged,
`[dry]` build byte-identical. Reseed-class: `(folds R1)`.

## Deps

`—`

## Done when

The same-name reject is relaxed to param-signature distinctness, `select_overload` picks the exact-match
candidate (rejecting ties and return-type-only pairs), overloaded names get a mangled suffix while
single-def symbols stay unchanged, the four fixtures pass, and a `[dry]` build is byte-identical.
