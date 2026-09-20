# CI

Six workflows and three composite actions. Everything a change has to pass runs from
`.github/workflows/ngen.yml`; the other five guard the edges.

## The jobs of `ngen.yml`

| job | count | proves |
|---|---|---|
| `ngen (<os>/<arch>)` | 5 | the taught compiler builds on that pair, `tests/*.tk` compiles **and runs** there with the right exit code, and `tests/refuse/*.tk` is refused with the right message and line (`scripts/fixtures.sh`, D52) |
| `fixpoint (<os>/<arch>)` | 5 | the ladder closes on the same five pairs ([bootstrap.md](bootstrap.md)) |
| `docs` | 1 | `sh scripts/check-docs.sh` and `sh scripts/check-limits.sh`, on one pair — both prove a tree, not a platform |
| `mc build ngen && run` | 1 | the aggregator: green only when every one of the five legs is |

The five pairs are linux/x86_64, linux/aarch64, macos/aarch64, windows/x86_64 and
windows/aarch64, each on a runner of that operating system **and** architecture, with
`mc --host` asserted against the leg before anything is built. That is what makes "native" a
checked fact rather than a runner label: nothing is cross-compiled and left unproven.

All twelve run on every push and pull request, with the branch-policy gate and CodeQL beside
them; the `main` ruleset requires exactly one of them, the aggregator `mc build ngen && run`,
which fails when any of the five legs fails. The others are reported, not required.

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

## The two fixture kinds, one corridor

`scripts/fixtures.sh <compiler> <config-base> [exe-suffix]` (D52) is what a leg, the
`fixpoint` ladder ([bootstrap.md](bootstrap.md)) and a local checkout all call — the per-leg
config swap above, written once instead of copied at four sites.

`tests/*.tk` is the first kind: a program that compiles and runs, judged by its own
`// expect-exit: N`. `tests/refuse/*.tk` is the second: a program that must FAIL to
compile, judged by a two-line header —

```
// expect-refuse: teko: <the exact message>
// expect-refuse-line: <the exact line>
```

— checked against the compiler's own stderr, CR-stripped, with `grep -F` (`:<line>: teko:
<message>` has to appear literally; a refusal with the right words on the wrong line is as
wrong as one with the wrong words). A `.tk` under either directory missing its header(s)
fails the run instead of being silently skipped. `docs/reference/diagnostics.md` lists every
message either kind can hit; D33 first named the gap a refusal fixture had no harness for.

## The decision numbering, and the limits budget (D94)

`docs` carries two checks that read `sh scripts/check-docs.sh`'s own output but measure
something neither the fixtures nor the samples do.

`scripts/check-docs.sh`'s **seventh check** reads `DECISION_LOG.md`'s own numbering:
every `### D<n>` header unique, the set dense from D1 to the highest with no gap except a
number a docs/ page explicitly reserves (`docs/specs/tekoc-tool.md`'s own draft, D73 today
— read from that page, never hardcoded), and every `D<n>` cited under `docs/`, in a root
`*.tk` module or in `scripts/`/`.github/` resolving to a header.

Those three read only the file under test, and anything that reads only the file under test
is satisfied by deleting more of it: `433b18d3` carried thirty-one entries (D57-D86) out of
the file by TRUNCATING THE TOP, which lowers the ceiling density anchors on and exempts from
the citation rule the very citations that would have tripped it — the check above, run
against that commit, prints `ok decisions: 56 entries, D1..D56, D73 reserved` and exits 0.
So the check has a **floor**: the `### D<n>` header set of the same file at the base commit
(the merge-base with `origin/main`, or `HEAD~1` when that is HEAD itself), read with `git
show`. A header that existed there and is gone here fails, whatever shape the removal took —
truncation, hole, or hole plus a reservation marker. Run against `433b18d3` the check now
refuses, naming `D57`-`D86` less the reserved `D73`. A base that cannot be read is itself a
failure, which is why the `docs` job checks out with `fetch-depth: 0`. What is still not
covered: a header added on the branch under test is not yet floored, the floor is one commit
deep rather than the whole history, and a citation ABOVE the ceiling is unchecked, so a `D205`
typed where `D95` was meant passes (D93, D94).

`sh scripts/check-limits.sh "$MC" teko.toml` runs `mc limits . --config teko.toml` (`rm -rf
build` first, so the measurement is clean rather than incremental) and fails if the
**compiler** table (`[compiler]`, `build/teko.mc` -> `build/teko`) answers `grow` on any
row. `teko.toml` already targets linux/x86_64 — the same pair `docs` runs on — so no derived
config is needed. It does NOT gate the second table `mc limits` prints, the **entry**
(`[project]`, `tests/hello.tk` built by the taught compiler): measured at head that table
already answers `grow` (`passes`, `syntax`, `alias`, `types`, at `[limits] tolerance = 1.0`),
and gating on it would either fail on the first run or need its cap raised to pass — exactly
the silent weakening this check exists to refuse. It is printed and left as a debt
([debts.md](debts.md)). `heap` is never read as pass/fail on either table: it is `mc
limits`'s own estimate, not a hand-set budget.

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

## The other five workflows

| workflow | runs on | does |
|---|---|---|
| `mc-canary.yml` | every 15 minutes, or a dispatch | runs the recipe against a CANDIDATE mc and publishes a verdict mc's own release reads ([below](#the-mc-canary)) |
| `site.yml` | a push to `main`, a pull request touching `docs/**` or `site/**`, a dispatch | renders `docs/` into the website and publishes it to the `site` branch, which the server behind teko-lang.org pulls |
| `branch-policy.yml` | every pull request | refuses an ungated source namespace against a protected base. The head branch name is read through `env:`, never interpolated into a `run:` — it is text the opener of the pull request controls |
| `codeql.yml` | pull requests only | the `actions` analyzer. There is no analyzer for `.tk` or `.mc`, and what is left to scan is CI that downloads a toolchain, creates a release with `contents: write` and interpolates branch names. No `paths:` filter: a required check that stops **reporting** goes pending forever rather than red |
| `release.yml` | a `v*` tag, or a dispatch on one | promotes what the gate proved |

## The site is rendered by mc's generator, at the pinned tag

`site.yml` has two jobs. `build` obtains the pinned `mc` through `setup-mc`, checks
`minicompiler/mc` out at **the tag that action resolved** into `_mc/`, builds `mcsite` there
(`mc build site --config site/mc.linux.toml`, mc's own ELF writer, no linker), and runs
`_mc/build/mcsite site --check` from the repository root, with `contents: read` — a pull
request's check never holds a write token. On a push to `main` it hands the rendered
`public/` to `publish`, the one job granted `contents: write`, which commits that tree as an
orphan commit on the `site` branch and force-pushes it with the workflow's own token; the
VPS that serves teko-lang.org pulls that branch every five minutes and hands it to nginx
behind Traefik, the same server and the same rite as mc's own domain (D36). No deploy
credential exists: the repository is public and the server only reads it.

Nothing of the generator is vendored here, so it cannot drift from the compiler the fixtures
were proved on: raising `MC_VERSION` moves both at once. Everything `mcsite` reads at run
time — `site/templates/`, `site/static/`, `site/tools/*.py` — is resolved against the
directory holding `site.toml`, so the mc checkout is dead weight the moment the binary
exists.

`--check` is the gate, and it runs on a **pull request** too, without deploying: it resolves
every internal link of every page it wrote, then spawns `site/tools/checkhtml.py` (structure,
accessibility, the three Content-Security-Policy rules) and `site/tools/contrast.py` (WCAG
ratios read out of the stylesheet). A page that breaks the site is caught before the merge.
The custom domain is DNS on the Cloudflare side, not a file in the tree: nothing a publish
writes can drop it.

`docs` (in `ngen.yml`) and `site` prove different things and neither replaces the other: the
first compiles and runs the samples, the second checks the pages they end up on.

## The mc canary

`mc-canary.yml` is teko's half of mc's freeze-and-canary protocol (mc `docs/specs/M53.md`
§ 6, [D71](../../DECISION_LOG.md)). mc publishes **every** tag as a GitHub *pre-release* with
its assets complete; a pre-release becomes a release only after this repository says the
compiler still teaches teko.

Neither side holds a credential for the other. mc cannot dispatch a workflow here and this
repository does not receive mc's `release` webhook, so both halves **pull**, and the only
thing that crosses is a URL each side reads anonymously.

| step | who | what |
|---|---|---|
| poll | teko, every 15 min | `repos/minicompiler/mc/releases` — the newest pre-release with no verdict on the `canary` branch yet |
| judge | teko | `ngen.yml` called with `mc_version` = that candidate: the five legs, the five fixpoint legs, `docs` and the aggregator; plus `teko_std` at its newest tag |
| publish | teko | `<version>.json` at the root of the `canary` branch, committed with this repository's own `GITHUB_TOKEN` |
| promote | mc | reads that file — contents API first, raw only as a fallback (raw served a stale `fail` for minutes after a push, 2026-09-15) — in two places since 2026-09-20: a short poll inside mc's own release run (~15 min) and a scheduled `promote-pending.yml` of mc's own, every 30 min, which promotes any pre-release whose verdict is green whenever it lands. `ok` flips the pre-release to a release, `fail` leaves it a pre-release, and a file that never appears is neither — no longer a deadline, since the scheduled job picks up a late verdict |

The verdict is one line at a fixed URL:

```
https://raw.githubusercontent.com/teko-org/teko-lang/canary/0.17.0.json
{"version":"0.17.0","status":"ok","run":"<this workflow run>","utc":"2026-09-15T12:34:56Z"}
```

Four fields, and `version` is bare — no leading `v`. `status` is `ok` only when **every**
needed job succeeded; cancelled and skipped are `fail`, because neither is evidence that the
candidate compiles teko and mc must not read silence as consent. The verdict job is
`if: always()`: a canary that goes quiet on a red leg times mc out instead of answering it.

**The recipe is not copied.** `ngen.yml` is called with one input, `mc_version`, which every
`setup-mc` in it takes instead of `MC_VERSION`; empty — a push, a pull request, a release —
is the pin, unchanged. So what a candidate mc has to pass is exactly what a merge into `main`
has to pass, and the two cannot drift. The one thing `ngen.yml` does not cover is the
standard library, which mc counts as part of the recipe, so a `std` job runs
`teko-org/teko-std`'s own linux/x86_64 leg — the vendored `deps/teko` its `mc.lock` pins,
`scripts/fixtures.sh` over its fixtures — with the same candidate. Nothing there writes to
the registry.

Three properties the branch has on purpose: it **accumulates**, one file per mc version, and
is never force-pushed (mc reads one file and it must not vanish under a reader); the workflow
holds a single `concurrency` group so two runs cannot race the same push; and a dispatch
naming a `version` explicitly rewrites a verdict already there, which is how a re-run
corrects itself.

The 15 minutes were mc's number, not a preference: its `promote` budgeted a quarter of an
hour for this schedule to notice inside a 90-minute poll — the contract until 2026-09-20,
when `promote-pending.yml` took the deadline away. The cron stays because it is still the
road a verdict travels by default. Cron is GitHub's least punctual trigger and the schedule
is disabled after 60 days with no push to `main` — measured on 2026-09-19/20, `*/15`
delivered about one run every two hours, and mc v1.1.0 (tagged 03:42Z) fell in a
three-and-a-half-hour gap — so the manual road, `gh workflow run mc-canary.yml -f
version=1.1.0`, is the one that removes the wait.

`canary` must be **pushable by `GITHUB_TOKEN`**: the `All Green` ruleset covers `~ALL` and
requires a pull request, so `refs/heads/canary` needs to sit in its exclude list beside
`refs/heads/site`, which is there for the same reason.

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

1. download the new release and run the whole local recipe against it — every fixture and every refusal;
2. `sh scripts/bootstrap.sh` against the new release has to print `FIXPOINT OK`;
3. **only then** write the new version into `MC_VERSION`.

Never the file first. A patch release of `mc` can change what a hook returns, and a pin
raised ahead of the proof puts that into the gate of every open pull request at once.
