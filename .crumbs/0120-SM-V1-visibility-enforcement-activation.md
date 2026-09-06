---
seq: 0120
crumb-id: SM-V1
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C17]
sources:
  - "docs/design/mudancas-superficie-0.3.1.md:1087-1115"          # §11.1 exp/pub/private triage rule
  - "docs/design/mudancas-superficie-0.3.1.md:1243-1245"          # §11 mechanism exists, enforcement not activated
  - "docs/design/estado-doc2-campanha-limpeza-0.3.1.md:20-24"     # visibility rule + clean-* lanes
  - "docs/design/mapa-superficie-exp-0.3.1.md"                    # the exp-surface map (drained)
---

# 0120 · SM-V1 — activate `exp`/`pub`/private visibility enforcement (Doc-2 §11)

> Turn on the visibility boundary the `.tkh` mechanism already carries: only `exp` reaches the `.tkh`;
> absent `exp`/`pub` = **namespace-local** (not file-local, §11.1); the enforcement is currently
> present-but-inert and must be ACTIVATED with the stdlib-ready item-by-item triage.

## Goal

Doc-2 §11 is the visibility law. The MECHANISM exists (`src/emit/header.tks`/`tkh.tks` `emit_tkh`/
`read_tkh`, `parser::Visibility` private/pub/exp, "only exp reaches the `.tkh`", Doc-2:1243) and the
`exp`-surface map is drained (`mapa-superficie-exp-0.3.1.md`); RM-C17 (`0107`) emits the aggregated
`exp` `.tkh`. What is MISSING from the plan is the **enforcement activation** + the **item-by-item
triage** gated on the stdlib being ready (Doc-2:1229 "ordem: PENÚLTIMA"; estado-doc2 style/clean-*
lanes). This crumb activates the checker enforcement (a non-`exp`/`pub` symbol is namespace-local; a
cross-namespace reference to it is rejected) and closes the triage. Much of the CONTENT triage is
already running on the separate `style/clean-*` lanes (estado-doc2:44-49) — this crumb is the
enforcement-activation keystone that ratifies their posture in the compiler.

## Where

- `src/checker/` — activate the visibility gate: a reference to a symbol from another namespace
  resolves ONLY if the symbol is `exp` or `pub` (namespace-local default, §11.1). Today the gate is
  inert (mechanism present, not enforced).
- `src/emit/header.tks`/`tkh.tks` — confirm only `exp` materializes into the `.tkh` (RM-C17 aggregates).
- The stdlib + compiler surface — apply the triage: default `exp` for consumable stdlib surface,
  `pub` for internal helpers, private for the rest (estado-doc2 visibility rule; the owner's ambiguous
  cases — `math/checked`, `list::grow`, `coverage`, `time::CivilDate`, `fmt::format_source` — are left
  `pub` pending the owner, estado-doc2:64-66; do NOT force them).

## How

1. **Activate the enforcement** behind the model corrected in §11.1 (no `exp`/`pub` = namespace-local,
   NOT file-local). A cross-namespace access to a private symbol becomes the SAME generic unknown-symbol
   error a never-existent name gets (no tombstone — consistent with the M3 expurgo discipline).
2. **Ride the clean-* lanes.** The `style/clean-*` lanes already re-posture visibility per the map;
   this crumb consumes their result and flips the gate ON once the stdlib is `exp`-triaged.
3. **Preserve the owner-ambiguous set** as `pub` (estado-doc2:64) — a HALT-relay item, not decided here.

```teko
/**
 * visibility_permits — may a reference from `from_ns` reach `sym` declared in `sym_ns`? True when they
 * share a namespace, or `sym` is `exp`/`pub`. Private symbols are namespace-local (§11.1), so a
 * cross-namespace reference to a private symbol is rejected with the generic unknown-symbol error.
 *
 * @param sym      the resolved symbol with its `Visibility`
 * @param sym_ns   the namespace declaring `sym`
 * @param from_ns  the referring namespace
 * @return         true if the reference is permitted
 * @since 0.3.1
 */
exp fn visibility_permits(sym: Symbol, sym_ns: str, from_ns: str): bool
```

## Rulings & laws

- **Teko-only:** `src/checker/*.tks` + `src/emit/*.tks`.
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp` decls; no `//` or `/* */`.
- **Visibility (owner, estado-doc2:20-24):** stdlib default `exp`; only internal helper `pub`; compiler
  machinery `pub`/private EXCEPT macro/comptime-reachable types stay `exp` (curated ABI). Doc only on
  `exp`.
- **No tombstone:** a cross-namespace private reference gets the GENERIC unknown-symbol error, not a
  "was private" diagnostic (M3 expurgo discipline).
- **Fork protocol (owner 2026-08-19):** the owner-ambiguous visibility set (estado-doc2:64) is a
  PENDING owner decision — leave `pub`, do NOT force, RELAY if it blocks. No self-decision.
- **W15 full Javadoc** on every `exp` decl; flatten; no `//`.
- **Safety:** NEVER `teko test .`; build under `ulimit -v 6815744`; `[fixpoint]` `gen2==gen3`; sweep
  `.tkt` after the gate flips.
- Rests on: Doc-2 §11.1 (1087-1115) + §11 mechanism (1243) + `mapa-superficie-exp-0.3.1.md`.

## Fixtures

| fixture | asserts | expected |
|---|---|---|
| `vis_private_cross_ns_reject` | a reference to a private symbol from another namespace is rejected (generic unknown-symbol) | `EXPECT_COMPILE_FAIL` |
| `vis_exp_reaches_tkh` | an `exp` symbol appears in the emitted `.tkh`; a `pub` one does not | `0` |

## Gate

`[fixpoint]` — `gen2==gen3` byte-identity (the enforcement flip must not move emitted bytes once the
tree is already triaged). "Green" = cross-namespace private references are rejected, only `exp` reaches
the `.tkh`, and the rebuild is byte-identical. Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C17` (`0107`, the `.tkh` aggregation). Rides the `style/clean-*` visibility lanes (estado-doc2).

## Done when

The visibility gate is ACTIVE (namespace-local default; cross-namespace private = generic reject), only
`exp` reaches the `.tkh`, the owner-ambiguous set is preserved `pub` pending ruling, and the rebuild is
byte-identical.
