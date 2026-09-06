# Getting Started

## Prerequisites

- A **released Teko binary**, used only to bootstrap building the compiler from source (see
  `scripts/fetch_teko.sh`) — most users instead install a packaged release directly.
- If you're building from source: a **C23-capable compiler** (clang) is used by the bootstrap
  seed on its way to producing a self-hosted `teko` binary. Once you have a `teko` binary,
  ordinary `teko build`/`teko run`/`teko test` on your own projects need no C toolchain at all —
  the own native backend emits object files directly and links via the system linker.

## Install

```sh
# Option 1 — the released installer (fetches the right binary for your platform)
curl -fsSL https://raw.githubusercontent.com/teko-org/teko-lang/main/install.sh | sh

# Option 2 — build the compiler from source (self-hosted)
git clone https://github.com/teko-org/teko-lang.git
cd teko-lang
./scripts/fetch_teko.sh          # fetch a released seed binary
./.teko/teko . -o bin            # the seed compiles the tree → bin/teko (self-hosted)
```

## Your first project

```sh
mkdir hello && cd hello
teko init                        # scaffolds teko.tkp + main.tks
```

`teko init` produces a minimal manifest and entry file:

```toml
# teko.tkp
name = "hello"
source = "src"
version = "0.1.0.0"
suffix = "beta"

[artifact]
kind = "binary"
```

```teko
// main.tks — the virtual main: top-level statements are the program
teko::io::println("hello, teko")
```

## The core commands

```sh
teko build .              # compile, link, produce a native binary
teko run .                # debug build + run in one step (like `cargo run`)
teko test .               # run the project's #test functions natively
teko test . --coverage    # also emit a Cobertura coverage report
teko build . -o out       # choose the output directory
teko fmt .                # canonically reformat the project
teko fmt --check .        # CI-friendly: nonzero exit if anything is unformatted
```

A project is any directory with a `*.tkp` manifest (TOML) and a source tree — see `teko.tkp` at
the root of this repository, the compiler's own manifest, for a fully commented reference of
every section.

## What happens when you `teko build`

1. The project's dependencies (if any) are resolved and loaded first.
2. The whole program — your code plus its dependencies — is type-checked together.
3. Your `#test` functions run; coverage is measured against the manifest's floors.
4. If (and only if) the gate passes, the checked program is lowered to native machine code and
   linked into the final artifact.

A failing test or a coverage shortfall stops at step 3 — nothing gets to codegen. This is
deliberate: **a release build cannot exist without its own tests having just passed.**

## Next steps

- [`language-guide.md`](language-guide.md) — learn the language
- [`cli-and-tooling.md`](cli-and-tooling.md) — editors, the formatter, linting, the debugger
- [`packages.md`](packages.md) — adding dependencies, publishing a package
- [`examples.md`](examples.md) — worked examples
