# Building: the CLI, `teko.toml` and the fixed point

teko is taught to [`mc`](https://github.com/minicompiler/mc): a build first assembles the
**taught compiler** out of the modules of this package, and then that binary compiles the
program. Both steps are one `build` command reading one config file.

The `mc` a build uses is **pinned** by [`../../MC_VERSION`](../../MC_VERSION): download the
release of exactly that version. Another one may teach a different compiler, or none.

---

## `teko build`

```
teko build [DIR] [--config FILE] [--entry-only] [--compiler-only]
           [--limits | --fix-limits] [--sysroot-dir DIR] [--libs-dir DIR]
```

`DIR` defaults to `.`. With no `--config`, `DIR/teko.toml` is used when it exists, and
`DIR/mc.toml` otherwise. Every path inside a config is resolved against the **config's own
directory**, and that directory is also the project root the `internal` check measures from
— so the path stays relative, without a leading `./`, and lives at the package root.

| flag | means |
|---|---|
| `--config FILE` | read FILE instead of the default |
| `--compiler-only` | build the taught compiler from `[compiler].modules`, print its path, stop |
| `--entry-only` | skip the `[compiler]` step and compile `[project].entry` with the running binary |
| `--limits` | after the build, print the table report and give a verdict |
| `--fix-limits` | the same report, and rewrite only `[limits]` with the smallest tolerance that fits |
| `--libs-dir DIR` | where an installed package lives, instead of `$HOME/.mc/libs` |
| `--sysroot-dir DIR` | this directory **is** the sysroot for `[target]` |

`--entry-only` and `--compiler-only` are exclusive. One line is printed per step, always
`what from -> to`.

```sh
mc build . --config teko.toml       # the stock mc: builds build/teko, then the entry
build/teko build . --config mc.host.toml --entry-only    # the taught compiler, one file
```

## `teko limits`

```
teko limits [DIR | FILE.tk]
```

With a `.tk` file it runs the whole pipeline over that one file and writes no object; with
a directory it is `teko build DIR --limits`. The report is one row per internal table —
the static estimate, what was reserved, the high-water usage, how many times the block
doubled — and a verdict of `ok`, `tight` or `grew`.

## The single-file modes

Anything that is not a subcommand goes to the compiler driver teko inherits, so
`teko [MODE] SOURCE [-o OUT]` compiles one file, and the dumps are the core's own:
`--dump-tokens`, `--dump-ast` (after every pass), `--dump-rules`, `--dump-asm`,
`--dump-syms`, `--dump-machine`. `teko --host` prints the pair this binary was built for
and `teko --version` the `mc` version underneath it.

## Exit codes

| code | means |
|---|---|
| `0` | success |
| `1` | a diagnostic: a compile error, a TOML error, a tool that failed |
| `2` | the environment is not ready (no sysroot, a dependency not fetched) |
| `3` | verdict `tight` or `grew`, under `--limits` |
| `70` | **a program that panicked at run time** ([memory.md](memory.md)) |

---

## `teko.toml` — how a project is built

```toml
[project]
name  = "teko"
entry = "tests/hello.tk"
out   = "build/teko-hello"
kind  = "exe"

[target]
os   = "linux"
arch = "x86_64"

[compiler]
core    = "<mc/core_min>"
modules = ["core_teko.mc", "teko.tk", "user.mc"]
out     = "build/teko"

[linker]
cmd  = "cc"
args = ["-o", "{out}", "{obj}"]

[limits]
tolerance = 1.0

[include]
paths = ["lib"]
```

| section | key | means |
|---|---|---|
| `[project]` | `name` | the project's name |
| | `entry` | the source the build compiles **with the taught compiler** |
| | `out` | where that program is written |
| | `kind` | `exe` or `obj` |
| `[target]` | `os`, `arch` | the pair to build for; `interp` and `libc` name a Linux loader and C library family |
| `[compiler]` | `core` | the core parts the taught compiler is assembled from |
| | `modules` | the modules taught on top of it, in order — the last one defines `user_init` |
| | `out` | where the taught compiler is written |
| `[linker]` | `cmd`, `args` | the external linker, with `{out}` and `{obj}` substituted. With no `[linker]`, the built-in executable writer is used where the host has one |
| `[limits]` | `tolerance` | the slack over the static estimate the tables are reserved with |
| `[include]` | `paths` | extra roots a `#include "…"` is resolved against, after the includer's own directory |

A host that does not link the pair in `[target]` **derives** its own config from this file
rather than editing it — replace `os`/`arch`, drop `[linker]` where the platform has a
built-in writer — which is what the CI legs and the fixed-point script do.

## `mc.toml` — the package manifest

The package and the build are two files on purpose: `mc pkg hash DIR` reads `DIR/mc.toml`
and takes no `--config`, so the two roles cannot share one file once the package **is** the
repository.

```toml
[package]
name  = "teko"
lib   = "lib/rt.tk"
files = [ "core_teko.mc", "lib/rt.tk", "teko.tk", "..." ]
check = ["mc_teko.tk"]
```

| key | means |
|---|---|
| `name` | the package name — `teko`, the library |
| `lib` | the file `#include <teko>` serves |
| `files` | every file the package ships; the tree hash digests exactly these bytes |
| `check` | units the registry compiles on their own, with the stock `mc` and no teko loaded, to validate a publication |

There is **no `[project]`** here, and that is what makes the name a library rather than a
tool: a manifest carrying `kind = "exe"` would be published as a tool for good.

---

## The fixed point

[`../../scripts/bootstrap.sh`](../../scripts/bootstrap.sh) proves the compiler reproduces
itself:

```sh
sh scripts/bootstrap.sh                          # the host pair, from `mc --host`
sh scripts/bootstrap.sh --os linux --arch x86_64
```

| stage | built by | from |
|---|---|---|
| `teko0` | the stock `mc` | `[compiler].modules` |
| `teko1` | `teko0` | `mc_teko.tk` — the whole compiler as one unit |
| `teko2` | `teko1` | the same |
| `teko3` | `teko2` | the same |

Three criteria, all of them checked:

1. `cmp build/teko2.o build/teko3.o` — **byte-identical objects**. `teko1.o` against
   `teko2.o` is deliberately not the criterion: those come from two different compilers.
2. the `--dump-asm` of `teko2` and of `teko3` differ nowhere.
3. `teko1` compiles and runs all 45 fixtures of
   [`tests/`](https://github.com/teko-org/teko-lang/tree/main/tests), each one's
   own `// expect-exit: N` being the oracle.

It prints `FIXPOINT OK` when all three hold. The ladder **runs** every stage it builds, so
the target pair is checked against `mc --host` and a cross build is refused rather than
produced unproven.

## The gates a change has to pass

| gate | what it proves |
|---|---|
| the five native legs | each `(os, arch)` pair builds the taught compiler on its own runner and runs the 45 fixtures there |
| the five `fixpoint` legs | the ladder above closes on the same five pairs |
| `docs` | [`../../scripts/check-docs.sh`](../../scripts/check-docs.sh): links resolve, every `teko` example on this site is compiled **and run**, every `teko: …` message the sources carry is documented in [diagnostics.md](diagnostics.md) |
