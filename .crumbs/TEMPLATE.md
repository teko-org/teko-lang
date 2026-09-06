---
seq: NNNN
crumb-id: XX-YY
milestone: MN
gate: "[dry] | [fixpoint] | [RITUAL] | [RITUAL]*"
reseed-class: "none | (folds R1) | teaching | expurgo | fixpoint-rebuild"
deps: []            # crumb-ids that must land first (verbatim from 000-INDEX)
sources:            # doc:line refs the recipe rests on
  - "docs/design/<doc>.md:<a>-<b>"
---

# NNNN · XX-YY — <slug title>

> One-line restatement of the manifest goal cell for this seq.

## Goal

One paragraph: WHAT this crumb delivers and WHY it exists in the wave. State the
byte-preservation posture (byte-preserving / feature-gated-inert / byte-mover) and
whether it drives the reseed. An implementer must understand the intent without
opening the source docs.

## Where

Exact files + functions to touch, with `file:line` where the source docs give it.
Format as a table or a bullet list:

- `src/<path>.tks:<line>` — `<fn/type>` — what changes here.

List EXISTING fns touched and any NEW module skeletons/decls introduced.

## How

The concrete, ordered implementation recipe. Every NEW surface is written in full
W15 Javadoc doc-comment form the implementer copies VERBATIM. Steps are numbered,
each independently reviewable. Where a decision was open, STATE the decision this
crumb makes and cite the ruling — never "TODO".

```teko
/**
 * <fn> — <summary the signature does not already say>.
 *
 * @param <name>  <meaning>
 * @return        <meaning> (the `-> T | error` case documents @throws)
 * @throws        <when it returns error>
 * @since 0.3.1
 */
fn <name>(<params>): <ret>
```

## Rulings & laws

The owner rulings + project laws that bind THIS crumb, each cited to a Doc-2 / model
line or a CLAUDE.md law. Always include the applicable safety rules:

- **Teko-only:** new work is `.tks` only; C twins frozen (runtime exception noted where relevant).
- **Comment convention (W15, owner 2026-08-19):** `/** */` only on `exp`; no `//` or `/* */`; a doc-comment never larger than the code it documents — enforced by the W15 canonicalizer/reviewer, NOT the compiler.
- **Fork protocol (owner 2026-08-19):** at a fork, FIRST check if already deliberated (`DECISION_LOG.md`, `docs/design/**`, `.crumbs/**`, Doc-2/Doc-1/umbrella); if multiple, the MOST RECENT wins; HALT + notify ONLY for a genuinely undecided fork.
- **W15 full Javadoc** on every declaration (pub + private); flatten/extract; no inline `//`.
- **Removals = clean expurgo** of lexer/parser/checker, **NO tombstone diagnostic** (only where this crumb removes surface).
- **Safety:** NEVER `teko test .`; build in a subshell with `ulimit -v 4194304` (4 GiB) cap — a blown guard is a root-cause fix, never a raised ceiling; commit each green step; reseed ONLY at a [RITUAL]; fixpoint `gen2==gen3` byte-identical; sweep `.tkt`/`.tkr` after any AST/signature change. **Conditional (owner 2026-08-19):** once a DRY build peaks ≤ 1.5 GB (1572864 KB), full `teko build .` (loads tests) is UNLOCKED and the `teko test .` ban lifts — the trigger is the measured dry-build milestone, not a date; measure and report peak when crossing it.
- The specific Doc-2 / owner ruling this crumb rests on: `<cite>`.

## Fixtures

The `.tkr` fixtures to add — ISOLATED standalone projects under
`examples/regressions/<name>/` (native exit code, or `EXPECT_COMPILE_FAIL` marker
file for rejects). **ONLY for paths the self-build fixpoint does NOT exercise.** If
the fixpoint already exercises the path, write exactly:
`none — the fixpoint self-build exercises this`.

| fixture | asserts | expected |
|---|---|---|
| `<name>` | `<what>` | `0` / `EXPECT_COMPILE_FAIL` |

## Gate

The exact gate + what "green" means + reseed-class (verbatim from the manifest).
`[dry]` = compile + scoped `.tkr` + trivial fixpoint (no emitted-byte change);
`[fixpoint]` = build gen2 + scoped regression + `gen2==gen3` byte-identity;
`[RITUAL]` = full native ladder + a genuine reseed; `[RITUAL]*` = carries a dev
native-ladder ritual but its SEED is harvested once at SM-R1 (`0030`).

## Deps

The crumb-ids that must land first (verbatim from 000-INDEX). `—` if none.

## Done when

The crisp, single acceptance criterion — the observable fact that makes this crumb DONE.
