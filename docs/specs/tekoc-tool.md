# `tekoc` — the taught compiler as an installable `mc` tool

**Designed, not yet built.** This page plans the last row of
[roadmap-1.0.md](roadmap-1.0.md) § What teko owes: *a user who wants teko should not have
to know what a hook module is*. It is a **packaging** crumb sequence — it teaches no teko
construct, adds no `teko:` refusal and touches none of the 35 modules at the root.

Everything below was measured on this machine against `mc` **0.17.0** — the pin of the day
(D72; the pin has since moved to 0.17.5 under the same frozen surface, and `[package].mc`
names 0.17.0 as the minimum, D72's second amendment — the measurements stand as taken),
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

> **The shape.** `teko-org/tekoc` — a repository of **three files**, `mc.toml`,
> `tekoc.tk` and the committed `mc.lock` — registered once on `/me`, tagged in lockstep
> with `teko`. The canonical copy of those three files lives **here**, at `tools/tekoc/`, so
> this repository's own CI can install and run the tool it describes; the release job mirrors
> them to `teko-org/tekoc` and tags it. One source of truth, two repositories, one version.

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
files   = ["mc.toml", "mc.lock", "tekoc.tk"]
check   = ["tekoc.tk"]
modules = ["<teko/core_teko.mc>", "<teko/teko.tk>", "<teko/user.mc>"]

[project]
name  = "tekoc"
entry = "tekoc.tk"
out   = "build/tekoc"
kind  = "exe"

[compiler]
core    = "<mc/core_min>"
modules = ["<teko/core_teko.mc>", "<teko/teko.tk>", "<teko/user.mc>"]
out     = "build/tekoc-stage1"

[linker]
cmd  = "cc"
args = ["-o", "{out}", "{obj}"]

[limits]
tolerance = 1.0

[deps]
teko = "0.12.6"
```

The five `[[permission]]` rows of § 5 belong to this same file; they are quoted there and not
twice.

Beside it, `tools/tekoc/tekoc.tk`, five lines — `mc_teko.tk` with the includes spelled as
package names:

```c
#include <mc/host>
#include <mc/core_min>
#include <teko/core_teko.mc>
#include <teko/teko.tk>
#include <teko/user.mc>
```

Seven things about it are load-bearing.

**`[package].modules` next to `[compiler].modules`, and both are needed.** They are two keys
of two different readers. `[compiler]` is `mc`'s, and drives the local `mc build` and the
install. `[package].modules` is the **registry's**: R2 classifies this package `teko` from
`[deps] teko` alone — no `language` key of its own — and then *builds the taught compiler
first, from `[package].modules` and the package's `[deps]`*, and compiles the `check` units
with it ([packages.md](packages.md) § Validation, D26). Without the key R2 has nothing to
build and `check = ["tekoc.tk"]` is handed to a stock `mc`. `mc` itself ignores the key, so
carrying both costs nothing — measured: with the manifest exactly as above,
`mc pkg sync . --yes --registry <dir>`, `mc build .` and
`mc tool install tekoc@0.12.6 --yes --registry <dir>` all run unchanged (tree hash
`68b398ae…`). The two lists are the same three paths, and a crumb that edits one edits the
other.

**`mc.lock` is the third file, and it is committed.** `mc build` of this package with no lock
stops at `mc: mc.lock is stale` / `run:   mc pkg sync --yes`, exit 2 — measured, and, like the
manifest errors of § 10, only *after* stage 1 has been built. So the package carries the lock
`mc pkg sync` wrote, in `files` and in git, and `mc pkg sync` is what a reader is told to run.
`mc tool install` re-stages and rewrites its own copy (measured: the staged
`~/.mc/tools/tekoc/v0.12.6/mc.lock` gains `tekoc`'s own `kind = "tool"` row), so the committed
lock is what makes a plain `mc build tools/tekoc` — and the registry's own checkout — work.

**No `[target]`, and a `[linker]` that is the host's own `cc`.** Three measurements decide
this, and the first two rule out the shorter road:

* `mc` **never probes the host** for its libc (`mc` `docs/build.md` § The matrix: *the
  compiler never probes the host for its libc*; the one loader probe in `mc`'s source,
  `mc/src/sandbox.mc:1249`, is the box's). So with no `[linker]`, the built-in `elf-exe` writes
  **musl's** `PT_INTERP` on every Linux host, and on a glibc machine that binary exists and
  does not run — `build/teko: not found`, exit 127 — which is exactly what
  [bootstrap.md](../internals/bootstrap.md) § The glibc tail records. `mc build` *spawns*
  stage 1, so the install would die there.
* the `mc.toml` cure for that, `[target] libc = "gnu"`, cannot travel in a published manifest:
  the same file on a macOS host is refused,
  `mc.toml:11:8: libc applies to a linux target: target.libc`, exit 1 (measured). One
  manifest, two hosts — the key is not available to it, and `mc tool install` takes no
  `--config`.
* a `[linker]` covers the `[compiler]` product too, **since mc 0.16.1**
  ([toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md)
  § `[linker]`: *for the entry and, since 0.16.1, for the compiler a `[compiler]` section
  builds* — and *the linker it names is expected to produce a binary this host can run*). The
  host's `cc` is that linker on macOS and on Linux, glibc or musl, with no key naming either.
  Measured on macos/aarch64, through the whole install:
  `link build/tekoc-stage1.o -> build/tekoc-stage1`, `link build/tekoc.o -> build/tekoc`,
  7.9 s, and everything §§ 3–4 claim holds through the binary it produced.

The price is a `cc` on the install machine — which the `[compiler] modules` road already
charges on those two hosts ([teko.toml](../../teko.toml) § `[linker]`) — and Windows.

**Windows is out of scope, and the refusal is `mc`'s own.** windows/x86_64 has had a direct
PE backend since M42 step 2 (`pe-exe-x86_64`); windows/aarch64 has none, and `mc build` there
answers `windows/aarch64 requires [linker]: there is no direct executable`
([diagnostics.md](https://github.com/minicompiler/mc/blob/main/docs/reference/diagnostics.md)
§ 10, [objects.md](https://github.com/minicompiler/mc/blob/main/docs/reference/objects.md)
§ 8c). On neither is `cc` a linker: the Windows link is `lld-link` against a sysroot of three
files ([bootstrap.md](../internals/bootstrap.md), and `ngen.yml`'s two Windows legs). One
static manifest cannot be both `cmd = "cc"` and `cmd = "lld-link"` with its sysroot arguments,
so **`tekoc` is declared for macOS and Linux**, and Windows keeps the `[compiler] modules`
road its own `ngen` and `fixpoint` legs already run (D16). T3 gates the scope by testing exactly it: one
`ubuntu-latest` leg, no Windows leg, and the guide page T4 writes says the same in one line.
**Not measured here** — this machine is macos/aarch64; the Windows half of the paragraph is
read from `mc`'s reference pages and from this repository's Windows legs, not from a run.

**`[project]` and `[compiler]` are both present, and that is the whole trick.** A tool's
launcher runs `[project].out` and only that (`tool.out = project.out`,
[toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md) § `[tool]`;
measured in `mc/src/tool.mc:781`). A `[compiler]`-only manifest would therefore install the
wrong binary. So `[compiler]` builds a **stage 1** taught compiler from the `teko` package,
and the entry it is then spawned to compile is the taught compiler's own single unit — the
first rung of [`scripts/bootstrap.sh`](../internals/bootstrap.md)'s ladder, run at install
time. `[project].out` is the stage-2 compiler, which is the binary the launcher runs.

**`teko = "0.12.6"` — a bare minimum, not `^0.12.0` and not `^0.12`.** Two measurements:

* a two-part constraint is refused, `mc.toml:29:8: a version constraint must be X.Y.Z,
  =X.Y.Z, ~X.Y.Z, ^X.Y.Z, >=X.Y.Z, or *`, exit 2 — and refused *after* the fetch and *after*
  stage 1 is built (§ 10).
* selection is **minimal version selection**, so a caret is a ceiling and never a preference.
  Measured with `0.12.0`, `0.12.6` and `0.13.0` in the index: `teko = "^0.12.0"` locks
  **0.12.0** — the floor, the oldest of the line — while `teko = "0.12.6"` and
  `teko = "=0.12.6"` both lock 0.12.6. A lockstep tool therefore names the version it was
  tagged at as its floor; the bare form is taken and not `=`, because a project that needs a
  newer `teko` in the same graph still resolves with a minimum and is refused by a pin
  ([packages.md](https://github.com/minicompiler/mc/blob/main/docs/reference/packages.md)
  § 10). The mirror job of § 6 is what writes that number, once per release.

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
tool tree after `mc tool install tekoc@0.12.6`:

```
~/.mc/tools/tekoc/v0.12.6/
    mc.toml  mc.lock  tekoc.tk
    deps/teko/                        the library package, vendored by the stage step
    build/tekoc  build/tekoc.o        <- tool.out, what the launcher runs
    build/tekoc-stage1  build/tekoc-stage1.o
    build/lib/mc/v0.17.0/             <- root 2, 101 rows of bundle.list
```

The two `.o` files are the `[linker]` road of § 2: `mc` writes the object and `cc` links it,
for the compiler and for the entry alike.

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
teko = "0.12.6"

[tools]
tekoc = "0.12.6"
```

```c
#include <teko/lib/rt.tk>
```

Measured end to end, with a scratch `HOME`, from a clean tree: `mc tool install tekoc@0.12.6
--yes` (7.9 s), `mc pkg sync --yes`, then `~/.mc/bin/tekoc build .` over a copy of
`tests/surface_arrays.tk` with that one include rewritten — `compile app.tk -> build/app`,
the project naming no `[linker]` of its own, and the program exits **42**, its own
`// expect-exit`.

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

Four judgements behind that list:

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
  work and this page does not pretend otherwise. **The supported Linux road for the project
  the tool compiles is no `[linker]` at all** — `mc`'s built-in `elf-exe`, with
  `[target] libc = "gnu"` where the host's loader is glibc. (That is the *project's*
  manifest, inside the box. The *tool's* own manifest is the `[linker] cmd = "cc"` of § 2,
  and it is linked at install time, outside any box.) macOS and Windows have no box
  ([sandbox.md](https://github.com/minicompiler/mc/blob/main/docs/reference/sandbox.md)
  § Hosts), so the row is recorded and not enforced there.
* **`libc = "gnu"` is the whole tail; `interp` is an override, not a second requirement.**
  Measured: a `kind = "exe"` build for `linux/x86_64` with `libc = "gnu"` and no `interp`
  writes `PT_INTERP` `/lib64/ld-linux-x86-64.so.2` and `DT_NEEDED` `libc.so.6` — the family
  key sets both names at once, per architecture
  ([toml.md](https://github.com/minicompiler/mc/blob/main/docs/reference/toml.md)
  § `target.libc`). `scripts/bootstrap.sh` writes an `interp` line beside `libc` because its
  `glibc_loader_of` **probe** is how it decides the family in the first place — the loader
  file has to exist for the tail to be written at all, and a musl machine gets nothing — and
  because a host may keep its loader at neither standard path. So the T2 fixture derives its
  `[target]` the same way and for the same reason: if `/lib64/ld-linux-x86-64.so.2`
  (x86_64) or `/lib/ld-linux-aarch64.so.1` (aarch64) exists, the fixture project gets
  `libc = "gnu"`; if it does not, nothing is written and the musl default stands; `interp` is
  written only when the loader is found somewhere else. `ngen.yml`'s Linux legs carry the
  same two lines in their matrix, which is where the fixture's row comes from.

Out of scope, stated: a **cross** or **static** target writes a sysroot cache under
`~/.mc/sysroots`, which would need `fs.write home/.mc/sysroots`. `tekoc` is declared for
native builds; a cross build stays the `[compiler] modules` road.

---

## 6. Release and registry

**Lockstep, same version, same day.** `tekoc` has no source of its own beyond five
`#include` lines: its only content is the `teko` version it names. So its version *is*
teko's, and D15's lockstep rule — already in force for `teko_std` — covers it.

**Which version line.** **0.12.x.** `v0.12.6` is the newest tag and the registry carries
`teko` 0.12.5 and 0.12.6. D17's sentence *the next release of the line taught to `mc` is
v0.4.0* — and CLAUDE.md § Versioning's copy of it — is stale text from 2026-09-07, written
before the line was cut; this page's D73 corrects both, and the same commit rewrites
CLAUDE.md § Versioning and [roadmap-1.0.md](roadmap-1.0.md)'s *the release before it is
v0.4.0*. What is being corrected is only the number: **tags are cut from a green `main`, one
per landed decision**, the line today is 0.12.x, and D17's two standing rules are untouched —
**v1.0.0 ships only with `mc` 1.0.0**, and **publication happens only from a stable version**,
which is why T5 below publishes no test version.

The cut, per release:

1. This repository tags `vX.Y.Z` and `release.yml` runs as it does today.
2. A **mirror job** in that same `release.yml` pushes `tools/tekoc/`'s three files to
   `teko-org/tekoc` — `[deps] teko` set to `X.Y.Z`, the lock re-synced against the published
   row — and pushes the tag `vX.Y.Z` there. `GITHUB_TOKEN` cannot reach a second repository,
   so the job authenticates with a **fine-grained token carrying `contents: write` on
   `teko-org/tekoc` alone**, stored here as the repository secret `TEKOC_MIRROR_TOKEN`. That
   token is the **owner's** to create, like the `/me` registration itself; nothing in this
   plan mints it.
3. **The tag is the handoff.** `teko-org/tekoc` carries its **own** `release.yml`, on
   `push: tags: ["v*"]`, which creates the Release — a Release, not just a tag: the registry
   takes a version up only when `GET /repos/<owner>/<repo>/releases/tags/vX.Y.Z` answers with
   something that is not a draft (§ 4 of the publishing guide) — and then runs
   `minicompiler/register-action@v1` **there**. It has to run there: the action POSTs
   `git_url=https://github.com/$GITHUB_REPOSITORY` and takes no repository override
   (`.github/workflows/release.yml:336-341`, and the dry-run text at `:353` that spells the
   POST out), so the same step run from `teko-lang` would announce `teko-lang` a second time.
   That repository keeps its own `TEKO_REGISTRY_PUBLISH` variable and its own
   `MC_REGISTRY_TOKEN` (scope `poll`), and a person registers it once on `/me`, exactly as
   `teko` was.

**Does R2 validate an exe tool?** Partly known and partly open. The validator hashes the
checkout, compiles each `[package].check` unit in its own box, and — quoting the guide —
*if the tagged `mc.toml` carries a `[project]`, the package's own tests run in a second box
afterward*. What "the package's own tests" means for a package with no test road is not
written down, and neither is the fallback when a package with a `[project]` has no
`[package].lib` for `check` to default to. This page therefore sets `check = ["tekoc.tk"]`
explicitly, which makes the validator compile the taught compiler (R2 builds it first from
`[package].modules` and `[deps] teko`, the second of which is what classifies the package
`teko` with no `language` key of its own — § 2). The 120-second clock over a build measured
at **7.9 s** locally leaves room, but the box is slower than this machine, and `exec cc`
inside it is § 5's known hole. Open question **O1** below.

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
work and not a dependency. One more line of that page is corrected in the same commit, and
it is not about `tekoc`: *the release before it is v0.4.0*, and § Cadence's copy of it, are
stale — the line is 0.12.x (§ 6, D73).

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
| **T1** | `tools/tekoc/` — the three files of § 2, and nothing else | S | — | `mc pkg sync tools/tekoc --yes --registry <scratch>`, over one index row carrying the checkout's own `mc pkg hash .`, rewrites the lock; then `mc build tools/tekoc` builds `build/tekoc`, that binary compiles `tests/hello.tk` to exit 42, and `--dump-ast` over every `tests/*.tk` and `tests/refuse/*.tk` is byte-identical to `build/teko`'s — **160 files, measured 160/160 today** | none: the page you are reading is the design, and it stays a spec until T4 |
| **T2** | `scripts/tool-fixture.sh` — the local-registry install harness | M | T1 | run by hand: `HOME=$tmp sh scripts/tool-fixture.sh "$MC"` prints `TOOL OK` and exits 0 | `internals/ci.md` § the tool leg |
| **T3** | the `tool` leg in `ngen.yml` | S | T2 | the leg is green on `ubuntu-latest`; the aggregator gains it | `internals/ci.md` |
| **T4** | `docs/guide/` — installing and using `tekoc` | S | T3 | `sh scripts/check-docs.sh` green; every fenced `teko` block carries `// expect-exit: N` or `// no-run` | `guide/95-packages.md` gains the tool road; `specs/packages.md` § What is still missing loses its `tekoc` bullet |
| **T5** | the mirror job here, `teko-org/tekoc`'s own `release.yml`, and the one-time registration | M | T4 | the **next ordinary release cut** after T4 mirrors, tags, releases and is taken up by the registry at that version; `mc tool install tekoc@X.Y.Z --yes` from the real registry installs and compiles `tests/hello.tk` to exit 42. No test version and no pre-release: D17 publishes only from a stable one, and a row R2 refuses is fixed and re-tagged as the **next patch** — a published row is immutable, and a burned patch number is the cheapest thing in this plan | `internals/ci.md` § release |
| **T6** | the banner follows `argv[0]` (optional) | S | T1 | `--dump-ast` unchanged over every fixture; `tekoc` with no argument prints `usage: tekoc …`, `build/teko` prints `usage: teko …` | `reference/build.md` |

**`mc limits` moves in none of them.** T1 adds a translation unit that is `mc_teko.tk`'s
twin, with the same `tolerance = 1.0`; the tables it reports are the fixed point's own.
T2–T6 add no source to the compiler at all. A leg that measured a move would be a bug in
T1's include list, not a budget to raise.

**T1 is the only one that can be wrong quietly**, and its `--dump-ast` gate is why it
cannot: two compilers built from the same five includes by different roads must agree on
every accepted program, which is the repository's own no-op proof (CLAUDE.md § Code laws).

**Why T1's gate needs a scratch registry.** The committed `mc.lock` names the *published*
`teko`, whose tree is not this commit's; a build that took it would prove the release, not the
head. So the gate writes one index row for the checkout itself — `mc pkg hash .` is the row's
`sha256`, § 9 step 1 — syncs, and builds against that. It is three lines of the same harness
T2 turns into a script, which is why T1 stays S and T2 owns the rest.

---

## 9. The fixtures

Every one carries its exit code, `expect-exit` where the harness reads it and an asserted
shell exit where the harness is the shell (D52).

| name | what it asserts | exit |
|---|---|---|
| `tests/hello.tk` (reused) | `tekoc build . --config … --entry-only` over it, then the run | **42** |
| `tests/surface_arrays.tk` (reused, through the tool) | the same program with `#include "../lib/rt.tk"` rewritten to `#include <teko/lib/rt.tk>`, built by `~/.mc/bin/tekoc` against `[deps] teko` — the proof that `<sys>` and `lib/rt.tk` are both reachable from the install tree | **42** |
| `tests/refuse/abstract_new.tk` (reused, through the tool) | the refusal survives the tool: stderr contains `:9: teko: an abstract class is not instantiated: Shape` | build **non-zero** |
| `scripts/tool-fixture.sh` | the harness itself: build the two archives, write the two index rows, `mc pkg sync` the package and then the project, `mc tool install tekoc@<ver> --yes --registry <dir>`, run the three above through the launcher, `mc tool remove tekoc`, and assert `~/.mc/tools/installed` is empty afterwards | **0** |

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
4. `HOME=$tmp/home "$MC" pkg sync tools/tekoc --yes --registry "$reg"` — the lock the package
   carries names the published `teko`, and this rewrites it to the checkout's own row. The
   committed lock is never edited by hand and the rewrite is thrown away with `$tmp`.
5. `HOME=$tmp/home "$MC" tool install tekoc@<ver> --yes --registry "$reg"`.
6. `HOME=$tmp/home "$MC" pkg sync "$proj" --yes --registry "$reg"`, `$proj` being the fixture
   project of § 4 — `[deps] teko`, `[tools] tekoc`, and on Linux the `[target]` tail § 5
   derives.
7. `HOME=$tmp/home "$tmp/home/.mc/bin/tekoc" build "$proj"`, run, compare.

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
   arguably), but `tool_stage`'s `pkg_write_lock` then resolves the project's **relative**
   `[replace]` path against the *staged* tree instead of against the project, and dies after
   the download. Re-measured for this revision, verbatim, with
   `[replace] toytool = "../toy"` in a project whose `[tools]` names `toytool 0.1.0` and a
   staged tree at `~/.mc/tools/toytool/v0.1.0/` — exit **1**, stdout ending at
   `package toytool 0.1.0 -> …/libs/toytool/v0.1.0/`, stderr:

   ```
   mc: cannot open: <home>/.mc/tools/toytool/toy/mc.toml
   ```

   `../toy` resolved from `tools/toytool/v0.1.0/` is `tools/toytool/toy`, which is the bug in
   one line. Where the resolved path happens to exist without being a package tree, the same
   half-application lands on `no mc.toml in the package tree` and then
   `the tool did not build: <name> <ver>` — this page's independent verifier measured that
   pair; the reproducer above is the one measured here. With an **absolute** replace path
   pointing at a real tree the install simply succeeds and the row is ignored (measured).
   Ignore the row or honour it; half of it is a confusing failure after a successful
   download.
3. **Two smaller ones.** A manifest or lock error is reported only *after* stage 1 has been
   built: `^0.12`, a two-part constraint, comes back as
   `mc.toml:29:8: a version constraint must be X.Y.Z, =X.Y.Z, ~X.Y.Z, ^X.Y.Z, >=X.Y.Z, or *`
   (exit 2) and a missing lock as `mc: mc.lock is stale` (exit 2), both after
   `compiler build/tekoc-stage1.mc -> build/tekoc-stage1` has run — seven seconds of work
   before a one-line TOML error. And the permission table loses a space when the permission
   text is at its column width:
   `fs.write workspace/buildmay create, change and delete files under build…` (measured,
   verbatim, in every install above).

---

## 11. Risks, and the law tensions

| risk | resolution |
|---|---|
| **An install runs a compiler build on the user's machine** — measured 7.9 s here, and it is two compilations and two links. On a slow machine or a cold registry it is a minute. | Accept and say so. `mc tool install` is a source install by design (*build it by spawning this compiler*, tools.md step 5); there is no binary road and asking for one is a feature ask. `mc tool upgrade` pays it again only on a version change. |
| **The install is a bootstrap rung, so a fixed-point defect breaks every install**, not just CI. | This is the honest cost of `tool.out = project.out`. It is also a benefit: an install that succeeds has proved stage 0 → stage 1 on that user's own host and architecture. The four-stage ladder stays the gate here (T3 keeps the existing `fixpoint` legs untouched). |
| **`fs.write workspace/build` refuses a project whose output is elsewhere, on Linux.** | Stated in § 5, and the narrower row is kept. If a real user hits it, the crumb that widens it is one line and a DECISION_LOG entry; widening it now would be a permission asked for a case nobody has. |
| **Linking through `cc` inside the box does not work.** | Named in § 5. The Linux road is `elf-exe` with no `[linker]`. This is a property of `mc sandbox`'s `--bin`, not a teko problem, and it is not worth a feature ask: the built-in backend is the better road on Linux anyway. |
| **Two repositories to keep in step**, and a mirror job needs a cross-repository token. | The canonical copy stays here (`tools/tekoc/`), so the *source* is never two things; only the publication is. The token is a fine-grained `contents: write` on `teko-org/tekoc` alone (§ 6), and it is the owner's to create. If it is refused, the fallback is a person pushing three files and a tag once per release — the same cadence `teko_std` already has. T5 is the crumb that finds out. |
| **Windows gets no tool** (§ 2): one static manifest cannot carry `cc` and `lld-link`, and windows/aarch64 has no direct executable at all. | Scoped out loud, gated by testing only what is claimed (one `ubuntu-latest` leg; macOS is the local road). Windows keeps the `[compiler] modules` road its own two ngen legs run today, and the guide says so in one line. A Windows user who tries anyway gets `mc`'s own `cannot run: cc`, not a wrong binary. Widening it is a crumb of its own, and it is a `[linker]`-per-target feature ask on `mc`, which this repository does not make. |
| **The install needs a `cc` on the machine**, which the no-`[linker]` road did not. | Accepted, and it is what buys Linux: with no `[linker]` the built-in `elf-exe` writes musl's loader on every host and a glibc machine cannot run the result (§ 2, measured). `cc` is already the `[compiler] modules` road's requirement on both supported hosts. |
| **Law tension: "zero new intrinsics", "`mc limits` is the budget".** | Not touched. `tekoc.tk` is `mc_teko.tk`'s twin; no surface code is added, no intrinsic is registered, no table grows. § 8 says why no leg should measure a move. |
| **Law tension: "C# decides the form."** | C# has no form for this — `dotnet` is a toolchain installer, not a package-manager tool. The market decides, and the market is `cargo install` / `go install` / `dotnet tool install`: a **verb on the package manager**, a **binary on PATH**, a **name distinct from the library's**. `mc tool install tekoc` is all three. The name `tekoc` is D15's and is not re-opened. |
| **Law tension: `--dump-ast` identical on a no-op.** | T1's gate, literally: the binary built by the tool road must dump identically to `build/teko` over every fixture. |

---

## 12. Open questions

Two, neither blocking. Both are for the registry, not for `mc`'s surface.

* **O1 — what does the validator do with an exe tool?** Whether `[package].check` is the
  right key for a package with a `[project]`, what *the package's own tests* means for one
  that has none, whether the 120-second box clock covers a taught-compiler build, and whether
  a box built from this permission table can run the `cc` that `[linker]` names. The
  measurable answer is T5's first real publication: the report at `<registry>/jobs/<id>` says
  exactly what ran. Until then `check = ["tekoc.tk"]` plus `[package].modules` is the
  conservative pair, and a refusal costs one patch number.
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
repository**, `teko-org/tekoc`, of three files: a `[project] kind = "exe"` manifest with
`[package].bin = "tekoc"`, five `#include` lines, and the `mc.lock` beside them. It cannot share `teko`'s manifest — a
package's kind is `[project]`'s alone, and `[project] kind = "exe"` in the root `mc.toml`
would reclassify `teko` and refuse `teko_std`'s `[deps] teko`. It cannot be a subdirectory
of this repository either: the registry registers a repository and publishes its tag's own
source archive, and `strip` is `tar --strip-components` and cannot select a subdirectory.
The canonical copy of the three files stays here at `tools/tekoc/`, so CI installs and runs
what it describes; the release job mirrors and tags. Versions are lockstep with `teko`,
like `teko_std`'s.

**The version line is 0.12.x**, and this entry corrects the number D17 carries. D17's
*the next release is v0.4.0* was written on 2026-09-07, before the line was cut; releases are
tagged from a green `main`, one per landed decision, and `v0.12.6` is the newest — `teko`
0.12.5 and 0.12.6 are registered. CLAUDE.md § Versioning and
[roadmap-1.0.md](roadmap-1.0.md) are corrected in the same commit. What D17 still rules is
untouched: `vX.Y.Z`, **v1.0.0 only with `mc` 1.0.0**, and **publication only from a stable
version** — so `tekoc`'s first publication is an ordinary release of the line, never a
`0.0.0-test`, and a row the validator refuses is fixed and re-tagged as the next patch.

`tekoc` **is** the taught compiler, not a wrapper: `[compiler]` builds stage 1 from
`<teko/teko.tk>` and `[project].entry` is teko's own single unit, so `[project].out` — the
only binary a launcher runs (`tool.out = project.out`) — is the stage-2 compiler, and
`mc build` has staged `build/lib/mc/v<ver>/` beside it, so `<sys>` and therefore `lib/rt.tk`
resolve through root 2 with no `HOME`, no `mc install` and no `--libs-dir` (measured:
`env -u HOME` over `tests/hello.tk` and `tests/surface_arrays.tk`, both exit 42). A program
that uses the runtime declares `[deps] teko` and writes `#include <teko/lib/rt.tk>`: a tool
registers no include root, and never will. The manifest carries `[package].modules` beside
`[compiler].modules` — the first is R2's, which builds the taught compiler from it before
compiling `check` (D26), the second is `mc`'s — a committed `mc.lock` as its third file, a
`[deps] teko` that is the **bare lockstep version** (selection is minimal, so `^X.Y.0` would
lock the floor of the line, measured), and `[linker] cmd = "cc"`, which since mc 0.16.1 links
the `[compiler]` product too: without it the built-in `elf-exe` writes musl's loader on every
Linux host, and `[target] libc = "gnu"` cannot be in a manifest that also installs on macOS
(`libc applies to a linux target`, measured). That scopes `tekoc` to **macOS and Linux**;
Windows, where `cc` is not a linker and windows/aarch64 has no direct executable at all,
keeps the `[compiler] modules` road (D16). Publication is a mirror to `teko-org/tekoc` with a
fine-grained `contents: write` token, and the registration runs in **that** repository's own
`release.yml`, because `register-action` announces `$GITHUB_REPOSITORY`. Five permissions,
`net` never:
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
