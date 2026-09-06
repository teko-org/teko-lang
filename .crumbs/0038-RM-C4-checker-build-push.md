---
seq: 0038
crumb-id: RM-C4
milestone: M2
gate: "[fixpoint]"
reseed-class: "fixpoint-rebuild"
deps: [RM-C3]
sources:
  - "docs/design/reducao-memoria-arrays-0.3.1.md:246-250"   # C4 — convert checker + build (4 naturezas)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:141-143"    # the 4 conversion natures (MAP/PARSE/FILTER/BUFFER)
  - "docs/design/reducao-memoria-arrays-0.3.1.md:110-126"    # C4/C5 reach the <=1.5 GB goal
---

# 0038 · RM-C4 — convert checker + build push (MAP/PARSE/FILTER/BUFFER; redesign `Env` ownership)

> Convert the checker + build `push` sites by the four conversion natures and redesign the aliasing-aggregate
> (`Env`) ownership — the conversion that reaches the ≤1.5 GB goal.

## Goal

Convert every growth (`teko::list::push`) site in `src/checker/*` (the doc's 1615 sites; ~768 `list::push`
in the current tree) and `src/build/*` (652 / ~404) to the no-push idiom, classifying each by the four
permanent conversion NATURES (CLAUDE.md): (1) **MAP** → `of_len(source.len)` + index; (2) **PARSE/SCAN** →
count `n`, then `of_len(n)` (two passes); (3) **FILTER** → allocate the largest, write `count`, slice
`s[0..count]`; (4) **BUFFER** → spread-literal + interpolation (the RM-C3 idiom). AND redesign the ownership
of the aliasing aggregates — chiefly `Env` (`scope.tks:14`) — so the binding/name tables are not grown by
copy but sized/materialized by index. This is the conversion that REACHES the ≤1.5 GB goal (C4/C5 land it).
It builds on RM-C3 (the emit idiom + `copy` already converted and byte-identity-proven) and core-consumes
the SM-R1 seed → `fixpoint-rebuild`, fixpoint per module.

## Where

- `src/checker/*.tks` (~768 `teko::list::push` sites) — each classified MAP/PARSE/FILTER/BUFFER and
  converted; the dense targets are the type/name/binding accumulators.
- `src/build/*.tks` (~404 `teko::list::push` sites) — the build-driver accumulators, same four natures.
- `src/checker/scope.tks:14` — `Env = struct { base_slots; bindings; cur_ns; owner_type; file; base_index;
  ret_expected; explicit }` — REDESIGN the ownership of the `base_slots`/`bindings` aliasing aggregates so
  they are sized/materialized (MAP over the source count) rather than grown by copy; the aliasing is the
  reason a naive `push`-swap would double-count.
- `src/runtime/arena.tks` — `copy`/`of_len` (RM-C2/COL-F0a) — the primitives the conversions materialize
  over; UNCHANGED.

NEW: no new surface; a discriminated conversion of the checker/build growth sites + an `Env` ownership
redesign, gated by per-module byte-identity.

## How

1. **Classify every `push` by nature.** For each `teko::list::push` site: is it a MAP (one output per input
   → `of_len(source.len)` + index), a PARSE/SCAN (unknown count → count pass, then `of_len(n)` + fill), a
   FILTER (subset → allocate the largest, write `count`, slice `s[0..count]`), or a BUFFER (text → the RM-C3
   spread-literal idiom)? Convert accordingly. The generic collections instantiate mostly MAP.
2. **Redesign `Env` ownership.** The `Env` binding/name aggregates (`base_slots`/`bindings`) ALIAS across
   scopes; a naive `push`→`of_len` swap would mis-size or double-hold them. Redesign so each is materialized
   by index from its source count with a single owner, and inner scopes reference (not copy-grow) the outer
   tables. This is the load-bearing ownership fix that makes the checker conversion sound.
3. **Convert module-by-module, fixpoint each.** After each module (`src/checker/typer.tks`,
   `src/checker/resolve.tks`, `src/build/project.tks`, …) is converted, build gen2 on the SM-R1 seed and
   prove `gen2==gen3` byte-identical — the checker's decisions and the emitted C must be UNCHANGED. Commit
   each green module; sweep `.tkt`/`.tkr` on any signature move.
4. **Reach the goal.** Against the RM-C1 baseline, C4 (with C3 already landed) drives the peak toward the
   ≤1.5 GB target (C4/C5 reach it); confirm the drop holds WHILE `gen2==gen3` survives — a conversion that
   changes a byte is a bug, not a win.
5. **Remove nothing structurally.** The growth-primitive REMOVAL (`empty`/`push`/`with_cap`/`grow_inplace`)
   is the later expurgo (COL-F2, `0093`, M3 / the RM-C-later purge, `reducao-memoria:201-206`); RM-C4 swaps
   the idiom at each site so that `src/checker`+`src/build` no longer CALL the growth — the removal follows
   once C3–C8 leave zero callers.

## Rulings & laws

- **Teko-only:** checker/build `.tks` over the pure-Teko `copy`/`of_len`; NO `teko_rt.c`.
- **W15 full Javadoc** on every rewritten accumulator/helper; flatten/extract to cut the push chains; no
  inline `//`.
- **The four natures (CLAUDE.md, permanent law):** MAP/PARSE/FILTER/BUFFER — each `push` is classified and
  converted by its nature; no growth survives the converted site.
- **NO PUSHES / ZERO dynamic growth (CLAUDE.md):** every converted site sizes-then-fills by index; `Env`
  aggregates are materialized, not copy-grown.
- **Byte-preserving:** the checker's decisions + emitted C are IDENTICAL — the per-module `gen2==gen3` is the
  proof.
- **Build-before-remove:** RM-C4 converts the call-sites; the growth-primitive removal is the later expurgo
  once no caller remains.
- **Safety:** NEVER `teko test .`; build gen2 in a subshell with `ulimit -v 6815744` — the drop is the point,
  a blown guard is a root-cause fix; commit each green module; NO reseed (fixpoint-rebuild); fixpoint
  `gen2==gen3`; sweep `.tkt`/`.tkr` on any signature move.

## Fixtures

`none — the fixpoint self-build exercises this`. RM-C4 converts the checker's + build's OWN growth sites;
building gen2 on the SM-R1 seed and proving `gen2==gen3` byte-identical per module IS the exercise (the
checker decisions + emitted C must not change). The peak-drop is measured against the RM-C1 baseline, not a
new `.tkr`.

## Gate

`[fixpoint]` — build gen2 on the SM-R1 seed + scoped regression + `gen2==gen3` byte-identity, per converted
module. "Green" = every `push` in `src/checker`+`src/build` is converted by its nature
(MAP/PARSE/FILTER/BUFFER), `Env` ownership is redesigned to materialize-not-copy-grow, the checker decisions
+ emitted C are byte-identical (`gen2==gen3`), and the peak drops toward ≤1.5 GB against the RM-C1 baseline.
Reseed-class: `fixpoint-rebuild`.

## Deps

`RM-C3` (the emit idiom + `copy` converted and byte-identity-proven; RM-C4 stands on that idiom and the same
SM-R1 seed).

## Done when

Every growth site in `src/checker`+`src/build` is converted by its conversion nature, `Env` ownership is
redesigned so its aliasing aggregates are materialized by index (not copy-grown), each converted module is
byte-identical (`gen2==gen3`), and the peak has dropped toward the ≤1.5 GB goal against the RM-C1 baseline.
