<div align="center">

<img src="docs/brand/logo.svg" alt="Teko" width="420"/>

**A self-hosting, all-native programming language — safe by construction, tested by default.**

[![Pull Request CI](https://github.com/teko-org/teko-lang/actions/workflows/pr.yml/badge.svg)](https://github.com/teko-org/teko-lang/actions/workflows/pr.yml)
[![Nightly Build](https://github.com/teko-org/teko-lang/actions/workflows/nightly.yml/badge.svg)](https://github.com/teko-org/teko-lang/actions/workflows/nightly.yml)
[![CodeQL](https://github.com/teko-org/teko-lang/actions/workflows/codeql.yml/badge.svg)](https://github.com/teko-org/teko-lang/actions/workflows/codeql.yml)

<img src="docs/brand/mascot.svg" alt="The Teko mascot — a baby guará (scarlet ibis)" width="280"/>

*Meet **Guri**, our mascot: a baby **guará** (scarlet ibis), a bird endemic to Brazil.*

</div>

---

> **Draft note (not yet live):** this file is a proposed as-if-done rewrite of the repository's
> `README.md`, staged at `docs/canonical/README.draft.md` for review. It is not the real README
> — see `docs/canonical/README.md` for what this canonical set is and is not.

## What is Teko?

Teko is a compiled, statically-typed programming language with a **fully self-hosting
compiler**: the compiler is written in Teko itself and compiles its own source tree to a
working native binary — and that binary rebuilds itself to a byte-identical fixpoint
(generation N == generation N+1).

- **All-native output.** `teko build` lowers your program directly to native object code (own
  backend) and links it — no GC; production binaries are native machine code. A
  transpile-to-C path is kept alongside as a differential-correctness comparative and a
  bootstrap bridge, never as the shipping default.
- **Native debug iteration.** `teko run` compiles the same checked program natively under
  `-O0` and executes it immediately in-process — fast, native debugging with full optimization
  choice via `-O` flags.
- **Tests are part of the build.** `teko build` runs your `#test` functions **before** codegen;
  failing tests or a coverage floor below the manifest's threshold **bar the release**.
  Coverage can be exported as Cobertura XML (`--coverage`).
- **Errors are values.** Functions return `T | error`; the `?` family (`T?`, `?.`, `??`)
  handles absence. There is no `null` outside `T?`, no exceptions, no `void`.
- **Automatic memory without a GC — with opt-in layers.** Arena regions are the invisible
  default: allocation and deallocation are compiler-managed, no GC, no borrow-checker
  ceremony — an *inferred* points-to/uniqueness fact (the "spine") is what makes stored borrows
  and manual `mem::free` sound; it is not a borrow checker you write to. On top ship two opt-in
  layers: `adopt { }` for cyclic or long-lived data (bulk-dropped at the block's brace), and
  `unsafe` — a **type/function modifier** (full risk ownership by type, not a block scope) —
  for explicit raw allocation. No `malloc`/`free` in safe code; raw allocation is explicit and
  contained behind `unsafe`.
- **A deliberately small surface.** One loop construct (`loop` + `break`), `match` for control
  flow over data, generics via monomorphization, value structs, reference classes, pure-contract
  interfaces, bitflag `flags`, `extern` FFI to C libraries, and a built-in `tdb` source-level
  debugger.
- **A real editor experience.** `teko lsp` is a native language server built into the compiler
  itself, powering diagnostics, hover, go-to-definition, completion, and formatting in VS Code,
  Vim/Neovim, and Emacs — the same lexer/parser/checker a build runs, in-process, so what your
  editor shows and what `teko build` enforces never diverge.

> **Status: pre-release, beta.** The language and compiler are under active, fast-moving
> development; syntax and semantics can still change between commits. Versioning tracks the
> remodel: `alpha` (`0.0.1.x`, pre-remodel) → `beta` (the `0.X` remodel/backlog waves, each
> finalizing one coherent subset) → `1.0.0.0` = LTS once the backlog is empty. The compiler is
> fully self-hosting (byte-identical fixpoint across native generations). See
> [TEKO_MASTER_PLAN.md](TEKO_MASTER_PLAN.md) for the live execution roadmap, and
> [`docs/canonical/`](docs/canonical/README.md) for the target-state reference this file is
> drawn from.

## A taste of Teko

```teko
// Errors are values: a function that can fail returns `T | error`.
pub type Box = struct { v: i64 }

fn ok(): Box | error { Box { v = 7 } }
fn fail(): Box | error { error::new("boom") }

pub fn classify(): i64 {
    match ok() {
        Box as b  => b.v      // bind the success member
        error     => 0        // handle the failure member
    }
}
```

```teko
// Optionals: `T?`, safe navigation `?.` and coalescing `??`.
pub fn safe(): i64 {
    let b: Box? = null
    b?.v ?? 8                 // → 8 (absent → fallback)
}
```

```teko
// Classes are reference types with factories instead of constructors.
type Dog = class {
    pub name: str
    pub age: i64

    pub fn make(n: str, a: i64): Dog {
        Dog { name = n; age = a }
    }

    pub fn is_puppy(self): bool {
        self.age < 1
    }
}

let rex = Dog::make("Rex", 3)
```

```teko
// String interpolation and the `~` concat operator (literal runs fold at compile time).
let name = "world"
let greeting = $"hi {name}" ~ "!"
```

Programs have a **virtual main**: top-level statements in `main.tks` are the entry point (no
boilerplate `fn main` required), or, when you want an explicit exit-code contract, a single
`fn main(): i32` in that same file — never both.

## Quick start

### Prerequisites

- **A released Teko binary** (for bootstrapping; see `scripts/fetch_teko.sh`) — most users
  instead install a packaged release directly.
- A **C23-capable compiler** (clang) — used only by the bootstrap seed on its way to a
  self-hosted `teko` binary; ordinary project builds afterward need no host C toolchain at all.

### Build the compiler

```sh
git clone https://github.com/teko-org/teko-lang.git
cd teko-lang

./scripts/fetch_teko.sh          # Download a released Teko binary (the bootstrap seed)
./.teko/teko . -o bin            # Compile the project with the seed → bin/teko (self-hosted binary)
```

Full technical instructions (CI models, targets, gates, troubleshooting):
**[docs/BUILDING.md](docs/BUILDING.md)**.

### Use it

```sh
./bin/teko build .            # compile and link the project to a native binary
./bin/teko run .              # debug build and run the project natively (like cargo run)
./bin/teko test .             # run the project's #test functions natively
./bin/teko build . -o out     # choose the output directory
./bin/teko test . --coverage  # emit a Cobertura coverage report
./bin/teko fmt --check .      # canonical-format check (CI gate)
./bin/teko doc .              # generate a documentation site
./bin/teko lsp                # start the language server (for editor integrations)
```

A project is any directory with a `*.tkp` manifest (TOML) and a source tree — see
[teko.tkp](teko.tkp), the compiler's own manifest, for a commented reference.

## Project layout

| Path | What lives there |
|---|---|
| `src/` | The compiler, in Teko (`.tks`, canonical) — self-hosted, 100% Teko source |
| `src/lexer/ · parser/ · checker/ · lir/ · backend/` | The pipeline stages: lex → parse → type-check (+ monomorphization + const-eval) → low-level IR → native object emission (own backend), with a C-transpile path kept as bootstrap bridge + comparative |
| `src/runtime/teko_rt.{h,c}` | The minimal execution runtime linked into generated programs — the one deliberate C exception |
| `src/lsp/` | The native `teko lsp` language server |
| `examples/` | Smoke and regression programs, each verified on native |
| `docs/canonical/` | The target-state reference: dev architecture + product language/tooling guide |
| `docs/brand/` | Mascot, logo and icon assets ([brand guide](docs/brand/README.md)) |
| `TEKO_*.md` | The language's design record: constitution, legislation, master plan, roadmaps |

## Documentation

- **[docs/canonical/README.md](docs/canonical/README.md)** — the target-state reference: what
  Teko *is* meant to be, once its designed backlog is complete (start here for the language and
  architecture; see `TEKO_MASTER_PLAN.md` for how much of it is built today)
- **[TEKO_MASTER_PLAN.md](TEKO_MASTER_PLAN.md)** — the single ordered execution sequence for
  all open work (the live, current-state status)
- **[TEKO_CONSTITUTION.md](TEKO_CONSTITUTION.md)** — the laws (M.0–M.5) that govern every
  design ruling
- **[TEKO_LEGISLATION.md](TEKO_LEGISLATION.md)** — ratified language design decisions
- **[docs/BUILDING.md](docs/BUILDING.md)** — building, testing and verification gates
- **[TEKO_ROADMAP_TOOLING.md](TEKO_ROADMAP_TOOLING.md)** — editor/IDE tooling roadmap (VS Code,
  JetBrains, Vim, …)

## Contributing

Contributions are welcome — but read **[CONTRIBUTING.md](CONTRIBUTING.md)** first: this project
has strong quality standards (W15 doc-comment style, 100% coverage on the delta, native-only
verification) that every change must respect.

## The mascot

<p>
<img src="docs/brand/poses/hello.svg" width="170" alt="saying hi"/>
<img src="docs/brand/poses/dev.svg" width="140" alt="coding"/>
<img src="docs/brand/poses/sticker.svg" width="140" alt="sticker"/>
<img src="docs/brand/mascot-pastel.svg" width="140" alt="pastel edition"/>
</p>

The Teko mascot is **Guri** — a baby **guará** (*Eudocimus ruber*, the scarlet ibis), one of
Brazil's most striking endemic birds. *Guri* is southern-Brazilian Portuguese for "little kid",
a warm nod to the fledgling and an echo of *gua*rá. All assets, palettes and usage rules are in
the [brand guide](docs/brand/README.md).

## License

Dual-licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT), at your option. Unless
you explicitly state otherwise, any contribution you intentionally submit for inclusion is
dual-licensed the same way, without additional terms.
