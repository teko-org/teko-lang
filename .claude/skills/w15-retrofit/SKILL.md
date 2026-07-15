---
name: w15-retrofit
description: The W15 canonicalization retrofit — the "arruma a casa" pass over a lane's delta before it bumps and promotes. Enforces the W15 convention (doc-comments-only, flatten/extract, SOLID/KISS/YAGNI/12-Factor, split large files, 100% delta coverage) as a strictly behavior-preserving refactor, proven by the unchanged fixpoint + green tests. Invoke when closing a lane (all sub-PRs drained) before the version bump, or when the owner asks for a "W15 sweep / retrofit / canonicalization / arruma a casa".
---

# W15 retrofit — canonicalize a lane's delta before promotion

W15 is the project's **canonicalization** standard: keep one convention (code + doc-comments) and apply engineering best practice (SOLID, KISS, YAGNI, 12-Factor) so the codebase stays readable and maintainable. Applied **continuously** ("W15-from-now") so this pass is mostly VERIFICATION — but at lane close it is a real refactor pass over the lane's delta, done **without changing behavior**.

Canon: `CONTRIBUTING.md` (W15 doc-comment + flatten rulings, 100%-coverage-on-delta), `docs/memory/teko-w15-style-from-now.md`, `docs/memory/teko-100-percent-coverage-on-new-code.md`.

## When to run

At **lane close**: every feature/fix sub-PR has drained into the lane (all green), and the lane is about to bump `teko.tkp` and promote to the umbrella. The retrofit runs AFTER the subs, BEFORE the bump. (Also on demand when the owner asks for a W15 sweep.)

## The inviolable rule: BEHAVIOR-PRESERVING

Every change is a refactor. The guardrail that proves it:
- **Fixpoint gen2 == gen3 byte-identical** (the compiler rebuilds itself to the same bytes).
- **`teko test .` green** on the OS matrix; the own==C differential unchanged.
- **100% coverage of the delta.**

Anything that would move emitted bytes, a test outcome, or the fixpoint is out of scope. Stop and report it.

## Procedure

1. **Scope the delta.** `git diff --stat <lane-base>...<lane-head>` → the touched files. That set (plus any file you must split to canonicalize them) is your surface. Nothing outside it.
2. **Comments → doc-comments only.** Convert every fn/type/member comment to `/** … */` on the declaration; delete inline `//` (mid-body/trailing). A line that "needs" a comment → extract a named function instead.
3. **Flatten.** Replace `if{if{if}}` / nested-`match` pyramids with early returns / guards / `continue`. Where flattening alone won't cut it, **extract** a function/method (cyclomatic down, functions short + single-purpose).
4. **Apply the principles.** SOLID (single responsibility, small units), KISS (remove cleverness), YAGNI (drop speculative generality), 12-Factor at config/env seams. Rename for clarity.
5. **Split large files.** Break an unbounded file into cohesive modules in the same namespace along responsibility lines — desirable. Keep the public surface (exported symbols) identical so callers don't change.
6. **Coverage → 100% of the delta.** Add `.tkt` tests for any new/changed line/branch the retrofit leaves uncovered.
7. **Prove it.** Push to a `chore/…`/`w15/…` branch, draft sub-PR **into the lane branch**. Let the lane CI re-prove fixpoint + tests + coverage. Do NOT run the heavy self-host gate locally if it may exceed ~5 min.

## Boundaries

- **Teko-only.** No frozen C-twin edits (checker/codegen/vm/build `.c`); only `teko_rt.{c,h}`/assert seed for a genuine runtime reason (rare in a retrofit).
- **No behavior changes, no scope creep, no new features.** A real bug found under the refactor is handed back to the integrator, not silently fixed.
- **Never bump/merge/close.** The integrator promotes the lane (bump → close issues → clean workspace → merge to umbrella). The retrofit only canonicalizes.

## Definition of done

The lane's delta is in W15 convention, the lane CI is green (fixpoint byte-identical, tests pass, 100% delta coverage), and a short report lists what was split/extracted and any out-of-scope findings handed back.
