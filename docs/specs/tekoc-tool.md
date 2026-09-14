# `tekoc` — the taught compiler as an installable `mc` tool

**Designed, not yet built.** This page plans the last row of
[roadmap-1.0.md](roadmap-1.0.md) § What teko owes: *a user who wants teko should not have
to know what a hook module is*. It is a **packaging** crumb sequence — it teaches no teko
construct, adds no `teko:` refusal and touches none of the 35 modules at the root.

Everything below was measured on this machine against the pinned `mc` **0.17.0** (D72),
with a throwaway registry, a throwaway `HOME` and a throwaway bin directory; the real
`~/.mc` was never written to. Where a sentence says *measured*, the command that produced
it is named.

The `mc` surface is frozen at 0.17.0
([hooks.md](https://github.com/minicompiler/mc/blob/main/docs/reference/hooks.md) § 8), so
nothing here asks `mc` for anything. What the measurements found that `mc` gets wrong is in
§ 10, written as defects
with reproducers, not as requests.

---

## 1. Two packages, and the second one is its own repository

**One manifest cannot be both.** A package's kind is written in exactly one place
([packages.md](https://github.com/minicompiler/mc/blob/main/docs/reference/packages.md)
§ The kind: library or tool):

| the package's `mc.toml` | kind |
|---|---|
| `[package]` and no `[project]` | library |
| `[package]` + `[project] kind = "obj"` | library that also builds an object |
| `[package]` + `[project] kind = "exe"` | **tool** |

`teko`'s root `mc.toml` is the first row on purpose (D15). Writing
`[project] kind = "exe"` into it would move the name to the third row, and
`teko_std`'s `[deps] teko` would then be refused — *a tool named under `[deps]`* is
exit 1. The registry publishes the kind, and a name's kind is fixed at its first
publication. `teko` is already published as `kind = "lib"` (measured: the cached index row
`~/.mc/libs/index/teko.toml` carries `kind = "lib"`). **So: a second package, `tekoc`.**
That is what D15 and [packages.md](packages.md) § What is still missing already ruled; this
page only fixes where it lives.

**It has to be its own repository.** The registry registers a repository, not a directory:
`POST /repos` takes one field, `git_url`, reads `mc.toml` **at the default branch** for the
one `[package].name` that repository owns, and the archive it publishes is the tag's own
source archive
([27-publishing.md](https://github.com/minicompiler/mc/blob/main/docs/guide/27-publishing.md)
§§ 3–4). The index row's `strip` is `tar --strip-components` and nothing more (measured:
`mc/src/pkg.mc:412`, `strip` defaulting to 1), so it cannot select a subdirectory of a
monorepo tag: `strip = 2` would flatten `docs/` and `tests/` into the package root as well.
A subdirectory package in this repository is therefore not publishable, and a release asset
is not what the validator reads.

> **The shape.** `teko-org/tekoc` — a repository of **two files**, `mc.toml` and
> `tekoc.tk` — registered once on `/me`, tagged in lockstep with `teko`. The canonical
> copy of those two files lives **here**, at `tools/tekoc/`, so this repository's own CI can
> install and run the tool it describes; the release job mirrors them to `teko-org/tekoc`
> and tags it. One source of truth, two repositories, one version.

This is `teko_std`'s shape (its own repository, lockstep versions) applied to the third
name, and it does not disturb the `[compiler] modules` road, which stays valid
([packages.md](packages.md) § Consuming teko, D16).

---

## 2. The manifest, whole

`tools/tekoc/mc.toml` — the file the registry reads and the file `mc tool install` builds.
This is not a sketch: it is the text that was installed and run for this page.

```toml
[package]
name    = "tekoc"
bin     = "tekoc"
licence = "MIT OR Apache-2.0"
mc      = "0.17.0"
files   = ["mc.toml", "tekoc.tk"]
check   = ["tekoc.tk"]

[project]
name  = "tekoc"
entry = "tekoc.tk"
out   = "build/tekoc"
kind  = "exe"

[compiler]
core    = "<mc/core_min>"
modules = ["<teko/core_teko.mc>", "<teko/teko.tk>", "<teko/user.mc>"]
out     = "build/tekoc-stage1"

[limits]
tolerance = 1.0

[deps]
teko = "^0.12.0"
```

and `tools/tekoc/tekoc.tk`, five lines — `mc_teko.tk` with the includes spelled as package
names:

```c
#include <mc/host>
#include <mc/core_min>
#include <teko/core_teko.mc>
#include <teko/teko.tk>
#include <teko/user.mc>
```

Four things about it are load-bearing.

**No `[target]` and no `[linker]`.** `mc tool install` builds on the user's own machine, so
the host's pair is the right one and there is nothing to pin. `kind = "exe"` with no
`[linker]` uses the built-in backend — `macho-exe` on macOS, `elf-exe` on Linux since M42 —
so the *install* needs no `cc` on the path. Measured: the same tree built to a working
1.7 MB `tekoc` both with `[linker] cmd = "cc"` and without it.

**`[project]` and `[compiler]` are both present, and that is the whole trick.** A tool's
launcher runs `[project].out` and only that (`tool.out = project.out`,
[toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md) § `[tool]`;
measured in `mc/src/tool.mc:781`). A `[compiler]`-only manifest would therefore install the
wrong binary. So `[compiler]` builds a **stage 1** taught compiler from the `teko` package,
and the entry it is then spawned to compile is the taught compiler's own single unit — the
first rung of [`scripts/bootstrap.sh`](../internals/bootstrap.md)'s ladder, run at install
time. `[project].out` is the stage-2 compiler, which is the binary the launcher runs.

**`^0.12.0`, not `^0.12`.** Measured: `mc build` refuses a two-part constraint —
`a version constraint must be X.Y.Z, =X.Y.Z, ~X.Y.Z, ^X.Y.Z, >=X.Y.Z, or *`, exit 2 — and
it refuses it *after* the fetch and *after* stage 1 is built (§ 10).

**`[limits] tolerance = 1.0`** is teko.toml's own number, for the same reason: the static
estimate does not fit a taught compiler this size.

---

## 3. What the user runs: `tekoc` **is** the taught compiler

Not a wrapper. `[project].entry` is teko's own single unit, so `build/tekoc` is
byte-for-byte the kind of binary `mc build . --config teko.toml` writes as `build/teko` —
the same `main()` from `core_teko.mc`, the same subcommand table, the same flags. A wrapper
would be a second surface to keep in step with the first, for nothing.

Measured through `~/.mc/bin/tekoc`, the installed launcher:

```
usage: teko build [DIR] [--config FILE] [--entry-only] [--compiler-only] [--limits|--fix-limits] [--sysroot-dir DIR] [--libs-dir DIR]
       teko limits [DIR|FILE.tk]
```

| what | measured |
|---|---|
| `tekoc build .` | compiles and links a project from its `mc.toml` |
| `tekoc build . --config FILE --entry-only` | the fixture road `scripts/fixtures.sh` drives |
| `tekoc limits .` | the `mc limits` tables, per translation unit |
| `tekoc --dump-ast FILE.tk` | the AST dump, unchanged |
| `tekoc --version` | `mc 0.17.0` — the compiler reports the `mc` it was taught from |
| a refusal | `bad.tk:9: teko: an abstract class is not instantiated: Shape`, identical text and identical line |

There is **no `pkg` subcommand**: `<mc/core_min>` carries no `mc pkg`
([core_min.mc](https://github.com/minicompiler/mc/blob/main/src/core_min.mc), *what is NOT
here*). Measured: `tekoc pkg` answers `mc: cannot open: pkg`. A project with `[deps]` is
still built correctly by `tekoc` — it reads `mc.lock` and resolves every root (measured) —
but the **fetching** stays `mc pkg sync`'s job. That is not a gap to close: a user who has
`mc tool install` has `mc`.

One cosmetic mismatch: the banner says `teko`, from the literal at `core_teko.mc:57`, while
the binary is `tekoc`. Crumb **T6** below, optional.

---

## 4. The library tree: root 2, staged, self-sufficient

`lib/rt.tk` includes `<sys>`, so a teko compiler that cannot find `mc`'s library tree can
compile nothing at all ([packages.md](packages.md) § The mc 0.16.0 migration). A tool
installed under `~/.mc/tools/tekoc/v<ver>/` is nowhere near the tree beside `mc`, so the
question is which of the three roots
([packages.md](https://github.com/minicompiler/mc/blob/main/docs/reference/packages.md)
§ 2) answers.

**Root 2 does, and `mc build` puts it there.** Beside a `[compiler]` product, `mc build`
stages `<dir of [compiler].out>/lib/mc/v<version>/` — `bundle.list` plus the library files
(M52 step D). `[compiler].out` is `build/tekoc-stage1` and `[project].out` is
`build/tekoc`: the same directory, so the tree lands beside **both**. Measured, the staged
tool tree after `mc tool install tekoc@0.12.9`:

```
~/.mc/tools/tekoc/v0.12.9/
    mc.toml  mc.lock  tekoc.tk
    deps/teko/                        the library package, vendored by the stage step
    build/tekoc                       <- tool.out, what the launcher runs
    build/tekoc-stage1
    build/lib/mc/v0.17.0/             <- root 2, 101 rows of bundle.list
```

That is the whole answer, and it costs nothing: `mc tool install` stages `mc.toml` and the
declared `files` and then spawns `mc build`, which does the rest. The package does **not**
carry `lib/mc/` in `files` — it could not, since that tree is `mc`'s and is not in any git
repository of teko's.

**Proved with no `HOME` at all.** Measured: `env -u HOME ./tekoc build . --config …
--entry-only` over `tests/hello.tk` (exit 42) and over `tests/surface_arrays.tk`, which
includes `lib/rt.tk` and therefore `<sys>` (exit 42). Root 1 (`$HOME/.mc/libs/mc/v<ver>/`,
written by `mc install`) is never consulted and is not a precondition.

**`lib/rt.tk` itself is the user's dependency, not the tool's.** A tool registers no include
root — *`mc build` registers no include root for it, opens nothing under it*
([toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md) § `[tools]`)
— so `#include <tekoc/lib/rt.tk>` is not a thing and never will be. A program that uses the
runtime declares the **library**:

```toml
[deps]
teko = "0.12.9"

[tools]
tekoc = "0.12.9"
```

```c
#include <teko/lib/rt.tk>
```

Measured end to end, with a scratch `HOME`, from a clean tree: `mc tool install tekoc@0.12.9
--yes` (7.7 s), `mc pkg sync --yes`, then `~/.mc/bin/tekoc build .` over a copy of
`tests/surface_arrays.tk` with that one include rewritten — `link build/app.o -> build/app`,
and the program exits **42**, its own `// expect-exit`.

---

## 5. Permissions: the minimal set, and what the box does with it

The rows are `[[permission]]` in the package's `mc.toml`, at most 32, each one sentence a
user reads before a byte is fetched
([toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md)
§ `[[permission]]`). `path` is `workspace`, `tmp`, `workspace/<rel>` or `home/<rel>` and
never absolute.

```toml
[[permission]]
kind   = "fs.read"
path   = "workspace"
reason = "reads the sources of the project it compiles"

[[permission]]
kind   = "fs.write"
path   = "workspace/build"
reason = "writes the objects and executables it produces"

[[permission]]
kind   = "fs.read"
path   = "home/.mc/libs"
reason = "reads the library packages the project depends on"

[[permission]]
kind   = "env"
name   = "HOME"
reason = "finds the package cache"

[[permission]]
kind   = "exec"
name   = "cc"
reason = "links the executable when the project names a linker"
```

Five rows. **`net` never**: a compiler fetches nothing; `mc pkg sync` does.

Measured — `mc tool box-args tools/tekoc/mc.toml --workspace <project>`, the one definition
`mc tool run` and a registry validator share:

```
--env HOME
--bin cc
--ro /Users/me/.mc/libs --at-path
--ro /Users/me/project --at-path
--rw /Users/me/project/build --at-path
```

Three judgements behind that list:

* **`fs.write workspace/build`, not `fs.write workspace`.** The narrower row is the honest
  one for a compiler whose output convention is `build/` — `mc`'s own, and what
  `[project].out` reads in every config in this repository. The cost is stated: on Linux, a
  project whose `[project].out` is outside `build/` is refused by the box. The wider row is
  the escape hatch and this page does not take it.
* **`fs.read home/.mc/libs` + `env HOME` are what make `[deps]` work inside the box.**
  Without them the derived flags bind nothing of `$HOME`, and `#include <teko/lib/rt.tk>`
  cannot be answered on Linux. Measured: `box-args` without those two rows emits no `--ro`
  for the cache.
* **`exec cc` is declared because it is true, and it is not the Linux road.** The tool does
  spawn `cc` when the project's `[linker]` names it. Inside the box `--bin cc` binds one
  program at `/bin/cc` with `PATH=/bin`, and a real `cc` immediately wants `cc1`,
  `collect2` and `ld`; linking through an external toolchain inside the box is not going to
  work and this page does not pretend otherwise. **The supported Linux road is no
  `[linker]` at all** — `mc`'s built-in `elf-exe`, with `[target] libc = "gnu"` where the
  host's loader is glibc. macOS and Windows have no box
  ([sandbox.md](https://github.com/minicompiler/mc/blob/main/docs/reference/sandbox.md)
  § Hosts), so the row is recorded and not enforced there.

Out of scope, stated: a **cross** or **static** target writes a sysroot cache under
`~/.mc/sysroots`, which would need `fs.write home/.mc/sysroots`. `tekoc` is declared for
native builds; a cross build stays the `[compiler] modules` road.

---

## 6. Release and registry

**Lockstep, same version, same day.** `tekoc` has no source of its own beyond five
`#include` lines: its only content is the `teko` version it names. So its version *is*
teko's, and D15's lockstep rule — already in force for `teko_std` — covers it.

The cut, per release:

1. This repository tags `vX.Y.Z` and `release.yml` runs as it does today.
2. A new job mirrors `tools/tekoc/` to `teko-org/tekoc` with `[deps] teko` bumped to
   `^X.Y.0`, tags it `vX.Y.Z` and creates its Release. A Release, not just a tag: the
   registry takes up a version only when `GET /repos/<owner>/<repo>/releases/tags/vX.Y.Z`
   answers with something that is not a draft (§ 4 of the publishing guide).
3. `minicompiler/register-action@v1` announces it, exactly as
   `publish-to-registry` announces `teko` — behind the same `TEKO_REGISTRY_PUBLISH`
   switch, and after `teko-org/tekoc` has been registered once by a person on `/me`.

**Does R2 validate an exe tool?** Partly known and partly open. The validator hashes the
checkout, compiles each `[package].check` unit in its own box, and — quoting the guide —
*if the tagged `mc.toml` carries a `[project]`, the package's own tests run in a second box
afterward*. What "the package's own tests" means for a package with no test road is not
written down, and neither is the fallback when a package with a `[project]` has no
`[package].lib` for `check` to default to. This page therefore sets `check = ["tekoc.tk"]`
explicitly, which makes the validator compile the taught compiler (R2 builds it first from
`[deps] teko`, which is what classifies the package `teko` with no `language` key of its
own). The 120-second clock over a build measured at **7.7 s** locally leaves room, but the
box is slower than this machine. Open question **O1** below.

**The user's road**, in full:

```sh
mc tool install tekoc          # the plan and the permission table; nothing without --yes
mc tool install tekoc --yes    # fetch, stage, build, launch
tekoc build .                  # ~/.mc/bin on PATH
mc tool upgrade tekoc          # the newest of the same major
mc tool remove tekoc
```

---

## 7. What this changes in `roadmap-1.0.md`

One row of § What teko owes is rewritten from a wish to a pointer:

> **`tekoc` installable.** The compiler as a tool, under its own package name and its own
> kind — a user who wants teko should not have to know what a hook module is
> ([tekoc-tool.md](tekoc-tool.md)).

and the `mc tool install` row of § What depends on `mc` is closed the way the others were:
**delivered** (mc 0.15.21, M48 C3; in the pin since 0.17.0), with `tekoc` now teko's own
work and not a dependency.

**What stays.** The other six criteria are untouched. In particular **self-hosting on the
five pairs** is not weakened and not duplicated: the tool's install is *one* rung of the
ladder on the user's own machine, and the four-stage fixed point
([self-hosting.md](self-hosting.md)) stays the gate. The `[compiler] modules` road stays
valid and stays documented — `tekoc` is a convenience, never the only way in (D16).

---

## 8. The crumbs

Six, ordered, each landing on its own. Sizes are modules-and-fixtures moved; every one of
them moves **zero** of the 35 modules at the root, which is why none is larger than M.

| # | crumb | size | depends on | gate | docs it owes |
|---|---|---|---|---|---|
| **T1** | `tools/tekoc/` — the two files of § 2, and nothing else | S | — | `mc build tools/tekoc` builds `build/tekoc`, and that binary compiles `tests/hello.tk` to exit 42. `--dump-ast` over every `tests/*.tk` is byte-identical to `build/teko`'s | none: the page you are reading is the design, and it stays a spec until T4 |
| **T2** | `scripts/tool-fixture.sh` — the local-registry install harness | M | T1 | run by hand: `HOME=$tmp sh scripts/tool-fixture.sh "$MC"` prints `TOOL OK` and exits 0 | `internals/ci.md` § the tool leg |
| **T3** | the `tool` leg in `ngen.yml` | S | T2 | the leg is green on `ubuntu-latest`; the aggregator gains it | `internals/ci.md` |
| **T4** | `docs/guide/` — installing and using `tekoc` | S | T3 | `sh scripts/check-docs.sh` green; every fenced `teko` block carries `// expect-exit: N` or `// no-run` | `guide/95-packages.md` gains the tool road; `specs/packages.md` § What is still missing loses its `tekoc` bullet |
| **T5** | the release mirror job + registration | M | T4 | a `v0.0.0-test` pre-release tag mirrors, tags and is taken up by the registry; `mc tool install tekoc@0.0.0-test --yes` from the real registry installs and runs | `internals/ci.md` § release |
| **T6** | the banner follows `argv[0]` (optional) | S | T1 | `--dump-ast` unchanged over every fixture; `tekoc` with no argument prints `usage: tekoc …`, `build/teko` prints `usage: teko …` | `reference/build.md` |

**`mc limits` moves in none of them.** T1 adds a translation unit that is `mc_teko.tk`'s
twin, with the same `tolerance = 1.0`; the tables it reports are the fixed point's own.
T2–T6 add no source to the compiler at all. A leg that measured a move would be a bug in
T1's include list, not a budget to raise.

**T1 is the only one that can be wrong quietly**, and its `--dump-ast` gate is why it
cannot: two compilers built from the same five includes by different roads must agree on
every accepted program, which is the repository's own no-op proof (CLAUDE.md § Code laws).

---

## 9. The fixtures

Every one carries its exit code, `expect-exit` where the harness reads it and an asserted
shell exit where the harness is the shell (D52).

| name | what it asserts | exit |
|---|---|---|
| `tests/hello.tk` (reused) | `tekoc build . --config … --entry-only` over it, then the run | **42** |
| `tests/surface_arrays.tk` (reused, through the tool) | the same program with `#include "../lib/rt.tk"` rewritten to `#include <teko/lib/rt.tk>`, built by `~/.mc/bin/tekoc` against `[deps] teko` — the proof that `<sys>` and `lib/rt.tk` are both reachable from the install tree | **42** |
| `tests/refuse/abstract_new.tk` (reused, through the tool) | the refusal survives the tool: stderr contains `:9: teko: an abstract class is not instantiated: Shape` | build **non-zero** |
| `scripts/tool-fixture.sh` | the harness itself: build the two archives, write the two index rows, `mc tool install tekoc@<ver> --yes --registry <dir>`, run the three above through the launcher, `mc tool remove tekoc`, and assert `~/.mc/tools/installed` is empty afterwards | **0** |

`scripts/tool-fixture.sh`, step by step — this is what was run by hand for this page, and
every step is measured:

1. `tar -czf teko-<ver>.tar.gz` of the checkout; `sha256 = $(mc pkg hash .)`. **The index
   row's `sha256` is the TREE hash, not the archive's** (measured: `mc` reported
   `got 4d3752…` for an archive whose `shasum -a 256` is `371dae…`; the tree hash from
   `mc pkg hash DIR` matches).
2. the same for `tools/tekoc/`.
3. write `$reg/index/teko.toml` and `$reg/index/tekoc.toml`, the second with
   `kind = "tool"`, `deps = ["teko <ver>"]` and the five canonical permission lines, sorted
   bytewise.
4. `HOME=$tmp/home "$MC" tool install tekoc@<ver> --yes --registry "$reg"`.
5. `HOME=$tmp/home "$MC" pkg sync "$proj" --yes --registry "$reg"`.
6. `HOME=$tmp/home "$tmp/home/.mc/bin/tekoc" build "$proj"`, run, compare.

**`HOME`, not `--libs-dir`.** Measured: the launcher passes `--libs-dir` to `mc tool run`,
which uses it to *find the install* — it is never handed to the tool, and the tool resolves
`<libs>` from `$HOME/.mc/libs` alone. A leg that fetches into `--libs-dir X` and then runs
the tool gets `teko <ver> is not fetched`. Point `HOME` at a scratch directory and take
every default, and the leg is hermetic and touches nothing of the runner's own `~/.mc`.

**`[replace]` is not available here.** Measured: `mc tool install` resolves a tool through
the registry row and fetches the archive whatever `[replace]` says; the replace path then
leaks into the staged tree's own lock and the install dies with
`cannot open: …/tools/tekoc/toy/mc.toml` (§ 10, defect 2). The local-registry road above is
not a workaround for that — it is how a tool is installed, and `[replace]` never applied.

**Which runner.** One leg, `ubuntu-latest`: the boxed host is the strict one, and the
fixture project therefore carries `[target] libc = "gnu"` and **no `[linker]`**, so nothing
has to `exec` (§ 5). If the runner's kernel cannot give `mc` a box,
`host_sandbox_supported()` answers false and the tool runs direct — the leg is green either
way, and the permission table is printed either way.

---

## 10. For the `mc` channel: three measured defects, no feature ask

Each is a defect with a reproducer, which is the only thing this repository sends
(CLAUDE.md § Code laws). None of them blocks the design; all three cost a reader time.

1. **`mc tool install DIR` ignores `[tools]` when the project has no `[deps]`.**
   `mc/src/deps.mc:1630`, `deps_apply`: `nd` counts only `deps.` keys and the function returns
   `if (nd == 0)` before `dep_read_lock`, so `dp_npkg()` is 0 and `tool_install_project`
   prints `no tools required by this project` and exits **0**. Reproducer: a project whose
   only package table is `[tools] x = "0.1.0"`, with a valid `mc.lock` naming it. Adding one
   `[deps]` row makes the same tree install. A project that names a tool and no library is
   the ordinary case.
2. **A `[replace]`d tool is half-applied.** The tool is fetched from the registry (correct,
   arguably), but `tool_stage`'s `pkg_write_lock` then resolves the project's `[replace]`
   path against the *staged* tree's `mc.toml` and dies with
   `mc: cannot open: <tools>/<name>/<name-of-replace-dir>/mc.toml` — measured with
   `[replace] toytool = "../toy"` and a staged tree at `…/tools/toytool/v0.1.0/`. Ignore the
   row or honour it; half of it is a confusing failure after a successful download.
3. **Two smaller ones.** A `[deps]` constraint `mc build` refuses (`^0.12`, two parts) is
   accepted by `mc tool install`'s resolver and only refused *after* the fetch and after
   stage 1 is built — a minute of work before a manifest error at line 23. And the
   permission table loses a space when the permission text is at its column width:
   `fs.write workspace/buildmay create, change and delete files under build…` (measured,
   verbatim, in every install above).

---

## 11. Risks, and the law tensions

| risk | resolution |
|---|---|
| **An install runs a compiler build on the user's machine** — measured 7.7 s here, and it is two compilations. On a slow machine or a cold registry it is a minute. | Accept and say so. `mc tool install` is a source install by design (*build it by spawning this compiler*, tools.md step 5); there is no binary road and asking for one is a feature ask. `mc tool upgrade` pays it again only on a version change. |
| **The install is a bootstrap rung, so a fixed-point defect breaks every install**, not just CI. | This is the honest cost of `tool.out = project.out`. It is also a benefit: an install that succeeds has proved stage 0 → stage 1 on that user's own host and architecture. The four-stage ladder stays the gate here (T3 keeps the existing `fixpoint` legs untouched). |
| **`fs.write workspace/build` refuses a project whose output is elsewhere, on Linux.** | Stated in § 5, and the narrower row is kept. If a real user hits it, the crumb that widens it is one line and a DECISION_LOG entry; widening it now would be a permission asked for a case nobody has. |
| **Linking through `cc` inside the box does not work.** | Named in § 5. The Linux road is `elf-exe` with no `[linker]`. This is a property of `mc sandbox`'s `--bin`, not a teko problem, and it is not worth a feature ask: the built-in backend is the better road on Linux anyway. |
| **Two repositories to keep in step**, and a mirror job needs a cross-repository token. | The canonical copy stays here (`tools/tekoc/`), so the *source* is never two things; only the publication is. If the token is refused, the fallback is a person bumping two files once per release — the same cadence `teko_std` already has. T5 is the crumb that finds out. |
| **Law tension: "zero new intrinsics", "`mc limits` is the budget".** | Not touched. `tekoc.tk` is `mc_teko.tk`'s twin; no surface code is added, no intrinsic is registered, no table grows. § 8 says why no leg should measure a move. |
| **Law tension: "C# decides the form."** | C# has no form for this — `dotnet` is a toolchain installer, not a package-manager tool. The market decides, and the market is `cargo install` / `go install` / `dotnet tool install`: a **verb on the package manager**, a **binary on PATH**, a **name distinct from the library's**. `mc tool install tekoc` is all three. The name `tekoc` is D15's and is not re-opened. |
| **Law tension: `--dump-ast` identical on a no-op.** | T1's gate, literally: the binary built by the tool road must dump identically to `build/teko` over every fixture. |

---

## 12. Open questions

Two, neither blocking. Both are for the registry, not for `mc`'s surface.

* **O1 — what does the validator do with an exe tool?** Whether `[package].check` is the
  right key for a package with a `[project]`, what *the package's own tests* means for one
  that has none, and whether the 120-second box clock covers a taught-compiler build. The
  measurable answer is T5's `v0.0.0-test` pre-release: the report at `<registry>/jobs/<id>`
  says exactly what ran. Until then `check = ["tekoc.tk"]` is the conservative choice.
* **O2 — does the registry accept two repositories under one owner publishing lockstep
  names?** Nothing suggests it does not (`teko` and `teko_std` already are two), but
  `tekoc` is the first *tool* teko publishes, and the permission table is shown for the
  first time with teko's name on it. T5 finds out.

---

## D73 — draft, not yet in the log

> The implementer moves this entry into `DECISION_LOG.md` when the crumb lands, dated the
> day it lands, with the measurements the leg actually produced.

### D73 · `tekoc`: the taught compiler as an `mc` tool, in its own repository (draft)

`mc tool install` (mc 0.15.21, M48 C3; in the pin since 0.17.0, D72) makes the compiler
installable, and D15's second name is taken up. **`tekoc` is a second package and a second
repository**, `teko-org/tekoc`, of two files: a `[project] kind = "exe"` manifest with
`[package].bin = "tekoc"` and five `#include` lines. It cannot share `teko`'s manifest — a
package's kind is `[project]`'s alone, and `[project] kind = "exe"` in the root `mc.toml`
would reclassify `teko` and refuse `teko_std`'s `[deps] teko`. It cannot be a subdirectory
of this repository either: the registry registers a repository and publishes its tag's own
source archive, and `strip` is `tar --strip-components` and cannot select a subdirectory.
The canonical copy of the two files stays here at `tools/tekoc/`, so CI installs and runs
what it describes; the release job mirrors and tags. Versions are lockstep with `teko`,
like `teko_std`'s.

`tekoc` **is** the taught compiler, not a wrapper: `[compiler]` builds stage 1 from
`<teko/teko.tk>` and `[project].entry` is teko's own single unit, so `[project].out` — the
only binary a launcher runs (`tool.out = project.out`) — is the stage-2 compiler, and
`mc build` has staged `build/lib/mc/v<ver>/` beside it, so `<sys>` and therefore `lib/rt.tk`
resolve through root 2 with no `HOME`, no `mc install` and no `--libs-dir` (measured:
`env -u HOME` over `tests/hello.tk` and `tests/surface_arrays.tk`, both exit 42). A program
that uses the runtime declares `[deps] teko` and writes `#include <teko/lib/rt.tk>`: a tool
registers no include root, and never will. Five permissions, `net` never:
`fs.read workspace`, `fs.write workspace/build`, `fs.read home/.mc/libs`, `env HOME`,
`exec cc` — the last declared because it is true and not because it works inside the box,
where the Linux road is `elf-exe` with no `[linker]`. The `[compiler] modules` road (D16)
stays valid and stays documented.

Three `mc` defects were measured and reported with reproducers, none blocking:
`mc tool install DIR` ignoring `[tools]` when a project has no `[deps]`
(`deps_apply`'s `if (nd == 0) return`); a `[replace]`d tool half-applied, dying in
`tool_stage`'s lock writer after a successful fetch; and a `[deps]` constraint refused only
after the fetch and after stage 1 is built. Plus one cosmetic: a missing space in the
permission table at column width.
