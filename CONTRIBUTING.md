# Contributing to teko

Teko is a language taught to the [`mc`](https://github.com/minicompiler/mc) compiler via
hook modules. This guide covers the workflow for working on the language port.

## Prerequisites

- **`mc` toolchain**: pinned version in `MC_VERSION` (one line, e.g., `0.15.18`).
  Download from [releases](https://github.com/minicompiler/mc/releases); verify the SHA256.
  ```sh
  mc --version          # must match MC_VERSION
  ```
- **Platform**: Linux/macOS/Windows x86_64/aarch64 (the five legs the CI tests).
- **No external libc:** the runtime (`lib/rt.tk`) uses only syscalls and the ABI — no FFI
  dependencies beyond the host C library.

## Building locally

```sh
# Derive your platform's config from teko.toml (set [target] os/arch)
sed -e 's/^os   = .*/os   = "linux"/' -e 's/^arch = .*/arch = "x86_64"/' \
    teko.toml >mc.host.toml

# Build the taught compiler
mc build . --config mc.host.toml

# Run the fixtures (45 programs, each with // expect-exit: N oracle)
for src in tests/*.tk; do
  n=$(basename "$src" .tk); w=$(grep -m1 '// expect-exit:' "$src" | sed 's/.*expect-exit: *//')
  sed -e "s#^entry = .*#entry = \"tests/$n.tk\"#" -e "s#^out   = .*#out   = \"build/$n\"#" \
      mc.host.toml >"mc.$n.toml"
  ./build/teko build . --config "mc.$n.toml" --entry-only && "./build/$n"
  echo "$n exit=$?  want=$w"; rm -f "mc.$n.toml"
done
```

## Fixpoint (self-hosting proof)

The fixed point proves the taught compiler reproduces itself:

```sh
sh scripts/bootstrap.sh --os linux --arch x86_64
# Output: teko0 (mc stock) → teko1 → teko2 → teko3 over mc_teko.tk
# Green means: teko2.o ≡ teko3.o (byte-identical), --dump-asm identical,
#              teko1 compiles and runs all 45 fixtures with correct exit codes.
```

Runs on five native legs in CI (`ngen.yml`, `fixpoint` job).

## Documentation

`docs/README.md` is the map. A change to public behavior updates the matching
`docs/guide/`, `docs/reference/` or `docs/specs/` page in the same PR — the gate below
checks that every fenced ` ```teko ` example actually compiles and, with
`// expect-exit: N`, runs with that exit code.

```sh
sh scripts/check-docs.sh
```

### Seeing the site

`docs/` is also the source of [teko-lang.org](https://teko-lang.org). The generator is
`mcsite`, `minicompiler/mc`'s own, built from the release `MC_VERSION` pins and never
vendored here — so rendering the site locally starts with a shallow checkout of that tag:

```sh
git clone --depth 1 --branch "v$(cat MC_VERSION)" https://github.com/minicompiler/mc _mc
(cd _mc && mc build site --config site/mc.toml)         # macOS
# (cd _mc && mc build site --config site/mc.linux.toml) # Linux instead
_mc/build/mcsite site --check                           # writes site/public, then validates
python3 -m http.server 8000 --directory site/public
```

`--check` resolves every internal link and runs the two Python checkers in `site/tools/`;
it is the same gate `site.yml` runs on a pull request that touches `docs/**` or `site/**`.
`site/public/` and `_mc/` are generated and ignored. [`site/README.md`](site/README.md) has
the rest.

## What a PR must contain

- **Green `mc build ngen && run`**: all 45 fixtures compile and execute with correct exit codes
  on your platform.
- **Fixpoint closure** (if touching modules in `mc_teko.tk`): `teko1 == teko2 == teko3` byte-identical
  objects, matching `--dump-asm`.
- **Fixtures with `// expect-exit: N`**: every new test carries its oracle. No test runs without one.
- **Docs gate green** (if touching `docs/**`, `README.md` or `CONTRIBUTING.md`): `sh scripts/check-docs.sh`.
- **No changes to mc's core** (`minicompiler/mc src/`). Teko only teaches new modules; the base
  grammar, lexer, and type system of mc are off-limits.
- **No new intrinsics or hardcoded backend logic.** Every function has surface code (`exp fn` in `.tk`).
  If a feature "wants" special handling in the backend, that's a fork: record it in `DECISION_LOG.md` and ask the owner.

## Code style for `.tk` hook modules

- **Short, clear.** No doc-comments longer than the code they document.
- **No inline `//` comments.** Inline commentary is banned — the code speaks for itself, or the design
  lives in `docs/specs/`.
- **Error messages: compiler style.** `file:line:column: "short cause"` (e.g., `teko: unsupported (os,arch)`).
  No lengthy explanations or references to docs.
- **Refused features carry the `teko:` prefix:** v0.4.0 does not support `Func<>`, `params T[]`,
  `T[][]`, nested `namespace`, float in `params`, `when` on the last `_` arm. Any of these triggers
  `teko: <short cause>`; the full list is [`docs/reference/not-yet.md`](docs/reference/not-yet.md).

## Decisions and forks

**Default: follow C# semantics.** Teko's surface mirrors mc's `[compiler]` modules, which themselves
mirror C# grammar and behavior (where applicable).

**Open forks** (design decisions not yet decided, or tensions between laws):
1. Check `DECISION_LOG.md`; the newest entry on a point supersedes the older ones.
2. Check `docs/reference/` for what is built and `docs/specs/` for what is already designed.
3. If genuinely open, record the fork in `DECISION_LOG.md` and ask the owner, with the fork
   statement and context. Do not implement competing designs.

## Reporting mc-side defects

If `mc` itself has a bug or limitation affecting teko's port:
1. Verify the issue on `minicompiler/mc` head.
2. Open an issue on the mc repo with reproducible steps.
3. Link from teko's PR or DECISION_LOG until resolved.
4. Do not work around mc bugs in teko code; fix the root cause in mc.

## CI and workflows

- **`ngen.yml`**: matrix of 5 native legs, each runs `mc build ngen` and all 45 fixtures.
- **`fixpoint` job**: teko0→teko1→teko2→teko3, object comparison and ASM diff, all 45 fixtures via teko1.
- **`docs` job**: `sh scripts/check-docs.sh` against `docs/**`.
- **`site.yml`**: builds `mcsite` from the pinned mc tag and renders `docs/` into the
  website; `--check` on every pull request touching `docs/**` or `site/**`, deploy to
  GitHub Pages only on a push to `main`.
- **Squash merge only.** The ruleset `main` requires fast-forward or squash; merge commits are blocked.
- **No legacy workflows.** The retired standalone compiler's own CI configuration
  (`pr.yml`, release cycles) is history, not run.

## Language

**This repository is English-only.** Every file that lands here — documentation, code
comments, commit messages, PR bodies, workflow comments — is written in English, and
`sh scripts/check-docs.sh` fails on Portuguese words in tracked sources. Portuguese belongs
in chat with the maintainer, or in the private history repository `teko-org/teko-history`,
which keeps the retired compiler's record verbatim.

---

How the port itself is built is [`docs/internals/`](docs/internals/README.md); what is
designed and not yet built is [`docs/specs/`](docs/specs/README.md); the decisions in
force are `DECISION_LOG.md`.
