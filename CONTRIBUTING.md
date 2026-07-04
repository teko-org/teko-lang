# Contributing to Teko

Thanks for your interest! Teko is a young, fast-moving project with a few **non-negotiable invariants**. Read this before opening a PR — a change that violates them will be rejected regardless of how good it otherwise is.

## The invariants

### 1. The SUPREME RULE — zero `.c`/`.h` ↔ `.tks` misalignment

**Teko-only (ruling 2026-07-04):** the compiler's canonical source is Teko (`src/**/*.tks`) and ALL new work is implemented in Teko only. The C23 files (`src/**/*.{c,h}` except the runtime) are the FROZEN historical bootstrap — do NOT extend them for new features; the bootstrap seed is the previous released `teko` binary (or the released `teko-bootstrap-src` any-cc snapshot). The one exception is `src/runtime/teko_rt.{c,h}` (and `src/assert/assert.{c,h}`): the execution runtime linked into generated programs stays maintained C — it is the FFI seam, not mirrored compiler logic. Until CI seeds from the released binary, compiler sources must not USE a new language feature in their own code (implement feature X written in the pre-X feature set; the corpus may adopt X once a seed containing X exists).

### 2. Differential equivalence — VM == native

Every validated change must behave identically on the VM (`teko run`/`teko test`) and in native output (`teko build`). Regression examples in `examples/` encode expected exit codes for both engines.

### 3. Verify BOTH engines + the self-host fixpoint

Before marking work done:

```sh
./build/teko build . -o bin          # C bootstrap engine, runs the .tkt test gate
./bin/teko  build . -o /tmp/gen2     # self-hosted engine, same gate through vm.tks
```

The two generated C outputs must be byte-identical after gensym normalization, and gen-2 == gen-3 exactly. See [docs/BUILDING.md](docs/BUILDING.md) §5.

### 4. Law-first design

Language-design tensions are resolved by the laws in [TEKO_CONSTITUTION.md](TEKO_CONSTITUTION.md) (M.0–M.5) and the rulings in [TEKO_LEGISLATION.md](TEKO_LEGISLATION.md) — not by taste. If your change needs a new design decision, open an issue describing the tension and which law each option satisfies; don't bury the decision inside a PR.

## Branch and PR mechanics

- **Base your PRs on `main`** (`gh pr create --base main`). The pre-reboot line lives on in git history only; `main` is the active development line (promoted from `chore/reboot` via PR #25).
- Use **Conventional Commits** (`feat(parser): …`, `fix(vm): …`, `docs: …`, `chore: …`).
- Keep PRs focused: one feature/fix per PR, with its tests and regression examples included.

## Tests

- Compiler tests are Teko functions annotated `#test`, in files ending `.tkt`, run by `teko test .`.
- `teko build` runs the test gate before codegen and enforces the coverage floors in `teko.tkp` — a PR that drops coverage below the floor will not build.
- New language features need a regression example under `examples/regressions/<name>/` (a `.tkp` + sources whose exit code proves the behavior), verified on **both** engines.

## Style

- Write self-explanatory code; avoid cryptic mnemonic abbreviations.
- Teko style laws (enforced by the grammar itself in several cases): the only loop is `loop { … }` (no `while`/`for`); never `match` on a bool — use `if`/`else`; casts go `bool → numeric`, never `T → bool` (use `x != 0`).
- Comments state constraints the code can't show — not narration of what the next line does.

## Reporting issues

Use the issue templates. For suspected compiler bugs, the most valuable artifact is a **minimal `.tks` reproducer** plus the observed VM and native behaviors (they may differ — that difference is itself a bug).

## Security

Please report suspected vulnerabilities privately — see [SECURITY.md](SECURITY.md).

## License of contributions

Teko is dual-licensed under Apache-2.0 OR MIT. Unless you explicitly state otherwise, any contribution you intentionally submit for inclusion is dual-licensed the same way, without additional terms or conditions.
