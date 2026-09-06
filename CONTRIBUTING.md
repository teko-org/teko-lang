# Contributing to Teko

Thanks for your interest! Teko is a young, fast-moving project with a few **non-negotiable invariants**. Read this before opening a PR — a change that violates them will be rejected regardless of how good it otherwise is.

## The invariants

### 1. Native is the sole engine (ruling 2026-07-13, #524)

**Teko-only source:** the compiler's canonical source is Teko (`src/**/*.tks`), and the C23 bootstrap files (`src/**/*.{c,h}` except the runtime) are archived at tag `0.0.1.3-bootstrap`. All new work is written in Teko only; do NOT extend the frozen C bootstrap. The one exception is `src/runtime/teko_rt.{c,h}` (and `src/assert/assert.{c,h}`): the execution runtime linked into generated programs stays maintained C — it is the FFI seam for native binaries. CI seeds from the latest released `teko` binary.

**Seed-fallback (owner ruling 2026-07-24):** the invariant is that the released seed builds the PR's base lineage, not necessarily any given PR's tip. A wave is never blocked by a seed capability gap: `scripts/build_with_seed_fallback.sh` first tries the seed directly on the tip (the common case, zero extra cost); only if that fails does it engage the **staged bootstrap ladder**, an iterative walk — *while the compiler in hand cannot build the tip, find the NEWEST first-parent ancestor it CAN build, build that, and climb onto the resulting compiler* — bounded by a stage cap and a no-progress guard. Every CI lane that builds gen1 from the released seed goes through this script, so a genuine language/codegen capability jump introduced by a PR never needs an intermediate version cut.

The rung is **discovered by probing, never assumed**: a fixed guess (the merge-base with the base branch) lands on the wrong side of the jump whenever a wagon ADDS a capability and then DELETES the corpus that did without it — its own head then requires the newer compiler, while the previous generation dies on it. The buildable rung is the wagon that already has the capability but not yet the corpus depending on it, and only a probe finds that commit. Two further invariants, each learned from a real failure: `TK_RT_DIR` is **pinned per stage** (the compiler otherwise resolves `teko_rt.{h,c}` relative to its own binary and mixes runtime eras), and each probe's output lives **inside the probed worktree** (same reason). A push directly to `main` is exempt by construction: `main`'s own merge-base with itself is itself, so a seed that fails there is a real regression, not a capability gap the fallback can bridge.

### 2. Native verification gate

Every validated change must pass the native gate (`teko test .` with coverage). Regression examples in `examples/` encode expected exit codes for native execution.

### 3. Verify native build + the self-host fixpoint

Before marking work done:

```sh
./bin/teko build . -o /tmp/gen1      # self-hosted engine gen-1
./bin/teko test .                     # run the full test gate (native) with coverage enforced
```

The generated C output (gen-2 from gen-1 rebuilt) must match gen-1 exactly (fixpoint). See [docs/BUILDING.md](docs/BUILDING.md) §5.

### 4. Law-first design

Language-design tensions are resolved by the laws in [TEKO_CONSTITUTION.md](TEKO_CONSTITUTION.md) (M.0–M.5) and the rulings in [TEKO_LEGISLATION.md](TEKO_LEGISLATION.md) — not by taste. If your change needs a new design decision, open an issue describing the tension and which law each option satisfies; don't bury the decision inside a PR.

## Branch and PR mechanics

- **Wave dev model (remodel).** Development proceeds in **waves**, one per `0.X` version. Each wave has **one umbrella PR** (branch `remodel/<slug>`, base `main`) that carries the version bump and aggregates the wave. Every feature/fix is a **sub-PR based on the umbrella**, never on `main`, and you **never commit directly to `main`**. Each sub-PR is drained CLEAN (all checks green) into the umbrella. Before the umbrella merges, two passes run **pre-launch**: the **W15 quality sweep** (#234 verifier + #231 lint) and the **doc-sync** (this coherence pass). The umbrella → `main` merge (strict All-Green gate) is what ships the `0.X.0.0-beta` version. Every wave gets its own W15 sweep and doc-sync, until LTS.
- Outside a wave (a hotfix or tooling change on `main`), base the PR on `main` directly.
- Use **Conventional Commits** (`feat(parser): …`, `fix(checker): …`, `docs: …`, `chore: …`).
- Keep PRs focused: one feature/fix per PR, with its tests and regression examples included.

## Tests

- Compiler tests are Teko functions annotated `#test`, in files ending `.tkt`, run by `teko test .`.
- `teko build` runs the test gate before codegen and enforces the coverage floors in `teko.tkp` — a PR that drops coverage below the floor will not build.
- New language features need a regression example under `examples/regressions/<name>/` (a `.tkp` + sources whose exit code proves the behavior), verified natively.

## Style

- Write self-explanatory code; avoid cryptic mnemonic abbreviations.
- Teko style laws (enforced by the grammar itself in several cases): the only loop is `loop { … }` (no `while`/`for`); never `match` on a bool — use `if`/`else`; casts go `bool → numeric`, never `T → bool` (use `x != 0`).
- **Comments = doc-comments only (ruling 2026-07-04, W15-from-now).** Every comment on a function, type, or member is a `/** … */` doc-comment attached to the declaration. Do NOT write inline comments (`// …` mid-body or trailing). If a line genuinely needs explaining, that is the signal to extract a well-named function instead of annotating it. This applies to **new code AND any code you touch** — a changed function's old inline comments are cleaned as part of the change, not left behind.
- **Flatten; no "Hadouken" code (same ruling).** No deep-nested pyramids (`if { if { if … } }`, nested `match` arms). Flatten with early returns / guard clauses / continues. Where flattening is impossible, **extract a function/method** to cut cyclomatic complexity and keep functions short and single-purpose (and files from growing unbounded). New and touched code both land in this shape — we apply the W15 quality standard as-you-go so the final sweep is only verification.

## Versioning

The version is `MAJOR.MINOR.PATCH.BUILD-<stage>`, held verbatim in `teko.tkp` (the single source of truth — the embedded `teko --version` and the release tag both read it). The `<stage>` tracks the remodel: **`alpha`** (`0.0.1.x`, pre-remodel) → **`beta`** (the `0.X` remodel/backlog waves, one coherent subset each) → stable at **`1.0.0.0` = LTS**, once the backlog is empty. When the integrator merges a **code** change, they bump the 4th field (`BUILD`) by 1 in the PR; a `MAJOR`/`MINOR`/`PATCH` bump resets `BUILD` to 0. No Action detects the increment — the manifest carries it. Docs/config-only merges do not bump `teko.tkp` (no new release). A `teko.tkp` version change auto-tags and publishes a prerelease; during a wave, each sub-PR merge mints an intermediate `0.X.0.N-beta` seed that the umbrella dogfoods (progressive self-hosting).

## Reporting issues

Use the issue templates. For suspected compiler bugs, the most valuable artifact is a **minimal `.tks` reproducer** plus the observed native behavior vs. what you expected (exit code, panic/error output).

## Security

Please report suspected vulnerabilities privately — see [SECURITY.md](SECURITY.md).

## License of contributions

Teko is dual-licensed under Apache-2.0 OR MIT. Unless you explicitly state otherwise, any contribution you intentionally submit for inclusion is dual-licensed the same way, without additional terms or conditions.
