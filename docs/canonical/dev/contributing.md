# Contributing to Teko

The full, authoritative rules live in `CONTRIBUTING.md` at the repository root — this page is
an orientation to the same discipline for someone reading the canonical architecture first.

## The non-negotiable invariants

1. **Native is the sole engine.** The compiler's canonical source is Teko
   (`src/**/*.tks`); the only C in the tree is `teko_rt`/`assert` (the FFI seam) — see
   `ffi-and-runtime.md`. There is no C bootstrap left as a production
   path; CI seeds every build from the latest released `teko` binary and climbs the rung
   ladder only when a genuine capability gap demands it (`self-host-fixpoint.md`).
2. **The native gate is mandatory.** Every change passes `teko test .` with coverage enforced,
   plus the regression examples under `examples/` that encode expected native exit codes.
3. **The self-host fixpoint is re-verified,** not assumed: rebuild the compiler with itself,
   run the full gate, and confirm the fixpoint (`gen2 == gen3`) still holds.
4. **Design tensions are resolved law-first.** `TEKO_CONSTITUTION.md` (M.0–M.5) and
   `TEKO_LEGISLATION.md`'s ratified rulings settle a design tension — not personal taste. A
   genuinely new tension is written up as an issue naming which law each option satisfies,
   not silently buried inside an unrelated PR.

## Branch and delivery model

Development proceeds in **waves**, one per `0.X` version. Each wave has one umbrella PR
(`remodel/<slug>`, based on `main`) that carries the version bump and aggregates the wave; every
feature/fix is a sub-PR based on the umbrella, never on `main` directly, and drains cleanly
(every check green) into it. Two passes run before a wave's umbrella merges: the **W15 quality
sweep** and the **doc-sync** coherence pass — every wave gets both, until the backlog itself is
empty and the project reaches LTS.

Within that model, delivery is a **stacked train**: a chain of PRs, each based on the one
before it, drained top-down (the topmost PR is the only one that needs to be green; a lower
PR that was red goes green automatically once the child above it merges into it). A small
correction lands in the train's last-engaged car; a large one becomes a new car on top. A
closed car is not reopened except to fix a merge error.

Outside a wave — a hotfix or a tooling-only change — a PR bases directly on `main`.

## Commit and PR conventions

- **Conventional Commits** (`feat(parser): …`, `fix(checker): …`, `docs: …`, `chore: …`).
- One focused feature/fix per PR, tests and regression examples included in the same PR.
- History on a shared branch is forward-only once pushed: a bad commit is corrected by a new
  commit that says so, not by rewriting pushed history — force-push permissions differ by
  branch namespace and are not something a contributor should assume are available.
- `main` only ever receives a `remodel/**` PR with its version bump — there is no direct push
  and no shortcut, for anyone, including the project owner.

## Style — the W15 bar

- **Doc-comments only.** Every comment on a function, type, or member is a `/** … */`
  doc-comment on the declaration. No inline `//` comments inside a body — if a line needs
  explaining, that is the signal to extract a well-named function, not to annotate the
  unclear one.
- **Flatten; no deep nesting.** No pyramided `if`-inside-`if`-inside-`match`. Use guard clauses
  and early returns; where flattening genuinely isn't possible, extract a function to keep
  cyclomatic complexity and file size bounded.
- **The language's own style laws, self-applied:** the only loop is `loop { }` (no
  `while`/`for`); never `match` on a `bool` (use `if`/`else`); casts go `bool → numeric`, never
  the reverse (use `x != 0`).
- **Whatever you touch, you clean** — a changed function's leftover inline comments or nesting
  are brought up to the current bar as part of the change, not left for a later sweep.

## No deferred failures

A defect found during a change — even one that "doesn't block" the change at hand, even an old
one noticed in passing — is fixed in the same piece of work, not deferred to "a future wave."
If the fix genuinely needs a piece of infrastructure planned for later, that piece is pulled
forward rather than the fix being postponed. Scoping out a *feature* nothing currently requires
is fine (that is ordinary roadmap sequencing); leaving a known-real defect unresolved because
addressing it is inconvenient right now is not.

## Versioning

`MAJOR.MINOR.PATCH.BUILD-<stage>`, held verbatim in `teko.tkp` as the single source of truth.
`<stage>` tracks the remodel: `alpha` (pre-remodel) → `beta` (the `0.X` wave sequence) →
`1.0.0.0` = LTS once the backlog is empty. The integrator bumps `BUILD` by one on every merged
code change (a `MAJOR`/`MINOR`/`PATCH` bump resets `BUILD` to 0); docs/config-only merges do not
bump the manifest. Every `teko.tkp` version change auto-tags and publishes a prerelease.

## Reporting a compiler bug

The most valuable artifact is a **minimal `.tks` reproducer** plus the observed behavior — and,
if it differs, the divergence between the native path and any other path the reproducer can be
run through, since a divergence between two paths that are supposed to agree is itself the bug.
