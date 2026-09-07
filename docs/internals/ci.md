# CI

Four workflows and three composite actions. Everything a change has to pass runs from
`.github/workflows/ngen.yml`; the other three guard the edges.

## The jobs of `ngen.yml`

| job | count | proves |
|---|---|---|
| `ngen (<os>/<arch>)` | 5 | the taught compiler builds on that pair, and all 45 fixtures compile **and run** there with the right exit code |
| `fixpoint (<os>/<arch>)` | 5 | the ladder closes on the same five pairs ([bootstrap.md](bootstrap.md)) |
| `docs` | 1 | `sh scripts/check-docs.sh`, on one pair — it proves a documentation tree, not a platform |
| `mc build ngen && run` | 1 | the aggregator: green only when every one of the five legs is |

The five pairs are linux/x86_64, linux/aarch64, macos/aarch64, windows/x86_64 and
windows/aarch64, each on a runner of that operating system **and** architecture, with
`mc --host` asserted against the leg before anything is built. That is what makes "native" a
checked fact rather than a runner label: nothing is cross-compiled and left unproven.

`main` requires the twelve above, plus the branch-policy gate and CodeQL.

**The aggregator keeps its literal name.** A required check is matched by name, so renaming
the job without changing the ruleset in the same step would leave `main` waiting forever for
a check nobody reports. The directory the name refers to is long gone; the name is not a
description, it is a key.

`docs` is deliberately **not** a dependency of the aggregator: a red documentation gate must
not change what the aggregator says about the five legs. It is required in its own right.

## How a leg is configured

`teko.toml` is the single source of truth of a build and is never edited. Each leg **derives**
its config from it, changing only the machine and the way it links: `[target] os`/`arch`, the
`.exe` suffix Windows needs, the glibc `interp`/`libc` tail on Linux, and the
`[linker]`/`[sysroot]` block that leg carries in the matrix. So `[project]`, `[compiler]`,
`[include]` and `[limits]` cannot drift between the five.

Each fixture then gets its own config, derived from the **leg's** config by swapping
`[project].entry` and `[project].out`, and is fed to the already-built compiler with
`--entry-only`, which reuses it instead of teaching it again once per fixture. That road —
`mc build DIR --config FILE` — is the only one that honours `[target]` and `[linker]`
([pitfalls.md](pitfalls.md)).

macOS appends nothing: with no `[linker]` the built-in Mach-O executable backend writes and
signs the binary itself, which is the only shape that works there. Linux fills the same slot
with mc's own ELF writer. Windows has neither, so both Windows jobs link with `lld-link`
against a sysroot the workflow builds first.

## The composite actions

| action | does |
|---|---|
| `setup-mc` | resolves the release pinned by `MC_VERSION`, downloads the asset for one pair, verifies its `.sha256`, asserts `mc --host`, and exports the binary, its directory and the version |
| `windows-sysroot` | assembles `winstart.obj`, `mcrt.obj` and `kernel32.lib`, puts LLVM on `$PATH` in Windows form, and points `TMPDIR` at the runner's own temp |
| `package-teko` | turns a leg's proven `build/teko` into `teko-<version>-<target>.tar.gz` and its `.sha256` |

Every job that needs `mc` obtains it through `setup-mc`, so no two jobs can silently
disagree about which compiler they tested. `latest` resolves only when asked for explicitly:
an unannounced release must never change what CI tests. Both Windows jobs use the same
sysroot action, so they cannot assemble different sysroots.

## The other three workflows

| workflow | runs on | does |
|---|---|---|
| `branch-policy.yml` | every pull request | refuses an ungated source namespace against a protected base. The head branch name is read through `env:`, never interpolated into a `run:` — it is text the opener of the pull request controls |
| `codeql.yml` | pull requests only | the `actions` analyzer. There is no analyzer for `.tk` or `.mc`, and what is left to scan is CI that downloads a toolchain, creates a release with `contents: write` and interpolates branch names. No `paths:` filter: a required check that stops **reporting** goes pending forever rather than red |
| `release.yml` | a `v*` tag, or a dispatch on one | promotes what the gate proved |

## The release promotes, it does not rebuild

`release.yml` calls `ngen.yml` through `workflow_call` rather than repeating the matrix, with
`package: true`. That input turns on two steps an ordinary push does not run: the per-leg
tarball, built **inside** the leg from the binary the fixtures have just run against, and the
fixpoint provenance file.

Nothing recompiles at tag time. A job that built the compiler again would publish bytes no
gate ever saw.

Four jobs: derive the tag and version from the run's own ref; run the gate; publish; then the
registry job. The publisher collects ten files — a `.tar.gz` and a `.sha256` for each of the
five legs — refuses to continue if any is missing, and composes notes that quote the mc
version and the fixpoint hashes out of the provenance artifact, so the published numbers and
the measured ones have one source.

A version carrying a `-` suffix is published as a **pre-release**.

## The registry job

Two halves. The **pre-flight** always runs: `mc pkg hash .` of the tagged tree, and every
`[package].check` unit compiled on its own by a stock `mc` on linux/x86_64 — which is what
the registry's validator will do in its own sandbox. Failing here, on a tag that has not
been announced yet, is cheaper than failing in the validator.

The **announcement** is gated by the repository variable `TEKO_REGISTRY_PUBLISH`. The
package has to be registered once by hand before it can be announced; until the variable is
`1` the job prints what it would have sent and is not a failure.

## Raising the pin

`MC_VERSION` is one line, without a leading `v`, and it is the answer to "which mc does CI
use". Raising it is its own change, in this order:

1. download the new release and run the whole local recipe against it — 45/45 fixtures;
2. `sh scripts/bootstrap.sh` against the new release has to print `FIXPOINT OK`;
3. **only then** write the new version into `MC_VERSION`.

Never the file first. A patch release of `mc` can change what a hook returns, and a pin
raised ahead of the proof puts that into the gate of every open pull request at once.
