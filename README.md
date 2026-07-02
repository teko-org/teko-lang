<div align="center">

<img src="docs/brand/logo.svg" alt="Teko" width="420"/>

**A self-hosting, all-native programming language — safe by construction, tested by default.**

[![Native Build & Test](https://github.com/schivei/teko-lang/actions/workflows/native.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/native.yml)
[![Sanitizers & Stress](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/sanitizers.yml)
[![SAST Gate](https://github.com/schivei/teko-lang/actions/workflows/sast.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/sast.yml)
[![CodeQL](https://github.com/schivei/teko-lang/actions/workflows/codeql.yml/badge.svg)](https://github.com/schivei/teko-lang/actions/workflows/codeql.yml)

<img src="docs/brand/mascot.svg" alt="The Teko mascot — a baby guará (scarlet ibis)" width="280"/>

*Meet our mascot: a baby **guará** (scarlet ibis), a bird endemic to Brazil.*

</div>

---

## What is Teko?

Teko is a compiled, statically-typed programming language with a **fully self-hosting compiler**: the compiler is written in Teko itself and compiles its own source tree to a working native binary — and that binary rebuilds itself to a byte-identical fixpoint (generation 2 == generation 3).

- **All-native output.** `teko build` lowers your program to C and hands it to the host C compiler — no runtime VM, no GC, no interpreter in production binaries.
- **A VM for development.** `teko run` executes the same checked program on a tree-walking VM for fast iteration and debugging; VM and native results are kept behaviorally identical by the project's differential-equivalence gate.
- **Tests are part of the build.** `teko build` runs your `#test` functions **before** codegen; failing tests or a coverage floor below the manifest's threshold **bar the release**. Coverage can be exported as Cobertura XML (`--coverage`).
- **Errors are values.** Functions return `T | error`; the `?` family (`T?`, `?.`, `??`) handles absence. There is no `null` outside `T?`, no exceptions, no `never`.
- **Automatic memory without a GC.** Lexical arena regions with escape analysis — allocation and deallocation are compiler-managed; no `malloc`/`free`, no borrow checker ceremony.
- **A deliberately small surface.** One loop construct (`loop` + `break`), `match` for control flow over data, generics via monomorphization, value structs, reference classes, pure-contract interfaces, bitflag `flags`, `extern` FFI to C libraries.

> **Status: pre-release** (`0.0.1.0-bootstrap`). The language and compiler are under active, fast-moving development on the `chore/reboot` line. Syntax and semantics can still change between commits. See [TEKO_MASTER_PLAN.md](TEKO_MASTER_PLAN.md) for the live execution roadmap.

## A taste of Teko

```teko
// Errors are values: a function that can fail returns `T | error`.
pub type Box = struct { v: i64 }

fn ok()   -> Box | error { Box { v = 7 } }
fn fail() -> Box | error { error { message = "boom" } }

pub fn classify() -> i64 {
    match ok() {
        Box as b  => b.v      // bind the success member
        error     => 0        // handle the failure member
    }
}
```

```teko
// Optionals: `T?`, safe navigation `?.` and coalescing `??`.
pub fn safe() -> i64 {
    let b: Box? = null
    b?.v ?? 8                 // → 8 (absent → fallback)
}
```

```teko
// Classes are reference types with factories instead of constructors.
type Dog = class {
    pub name: str
    pub age: i64

    pub fn make(n: str, a: i64) -> Dog {
        Dog { name = n; age = a }
    }

    pub fn is_puppy(self) -> bool {
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

Programs have a *virtual main*: top-level statements in `main.tks` are the entry point — no boilerplate `fn main` required.

## Quick start

### Prerequisites

- **CMake ≥ 3.20**
- A **C23-capable compiler** (clang on macOS/Linux; clang with lld on Windows)
- A host `cc` on `PATH` (used by `teko build` to link generated programs)

### Build the compiler

```sh
git clone https://github.com/schivei/teko-lang.git
cd teko-lang

cmake -B build
cmake --build build            # → build/teko  (the C bootstrap compiler)

cmake --build build --target selfhost   # → bin/teko (Teko compiled by itself — THE validation gate)
```

Full technical instructions (targets, gates, troubleshooting): **[docs/BUILDING.md](docs/BUILDING.md)**.

### Use it

```sh
./bin/teko build .            # compile the project in ./ to a native binary
./bin/teko run .              # run it on the development VM
./bin/teko test .             # run the project's #test functions
./bin/teko build . -o out     # choose the output directory
./bin/teko test . --coverage  # emit a Cobertura coverage report
```

A project is any directory with a `*.tkp` manifest (TOML) and a source tree — see [teko.tkp](teko.tkp), the compiler's own manifest, for a commented reference.

## Project layout

| Path | What lives there |
|---|---|
| `src/` | The compiler, in Teko (`.tks`, canonical) **and** its C23 bootstrap mirror (`.c`/`.h`) — kept in lockstep by the project's SUPREME RULE |
| `src/lexer/ · parser/ · checker/ · codegen/ · vm/ · emit/` | The pipeline stages: lex → parse → type-check → (native C emission \| VM execution) → `.tkb`/`.tkh`/`.tkl` artifacts |
| `src/runtime/teko_rt.{h,c}` | The minimal execution runtime linked into generated programs |
| `examples/` | Smoke and regression programs, each verified VM == native |
| `docs/brand/` | Mascot, logo and icon assets ([brand guide](docs/brand/README.md)) |
| `TEKO_*.md` | The language's design record: constitution, legislation, master plan, roadmaps |

## Documentation

- **[TEKO_MASTER_PLAN.md](TEKO_MASTER_PLAN.md)** — the single ordered execution sequence for all open work (start here for status)
- **[TEKO_CONSTITUTION.md](TEKO_CONSTITUTION.md)** — the laws (M.0–M.5) that govern every design ruling
- **[TEKO_LEGISLATION.md](TEKO_LEGISLATION.md)** — ratified language design decisions
- **[TEKO_SOURCE.md](TEKO_SOURCE.md) / [TEKO_CHECKER.md](TEKO_CHECKER.md)** — the original spec-as-source documents (frozen; `src/` is canonical)
- **[docs/BUILDING.md](docs/BUILDING.md)** — building, testing and verification gates
- **[TEKO_ROADMAP_TOOLING.md](TEKO_ROADMAP_TOOLING.md)** — editor/IDE tooling roadmap (VS Code, JetBrains, Vim, …)

## Contributing

Contributions are welcome — but read **[CONTRIBUTING.md](CONTRIBUTING.md)** first: this project has unusual invariants (the C↔Teko twin-mirroring SUPREME RULE and a dual-engine verification gate) that every change must respect.

## The mascot

<p>
<img src="docs/brand/poses/hello.svg" width="170" alt="saying hi"/>
<img src="docs/brand/poses/dev.svg" width="140" alt="coding"/>
<img src="docs/brand/poses/sticker.svg" width="140" alt="sticker"/>
<img src="docs/brand/mascot-pastel.svg" width="140" alt="pastel edition"/>
</p>

The Teko mascot is a baby **guará** (*Eudocimus ruber*, the scarlet ibis) — one of Brazil's most striking endemic birds. All assets, palettes and usage rules are in the [brand guide](docs/brand/README.md).

## License

Dual-licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT), at your option. Unless you explicitly state otherwise, any contribution you intentionally submit for inclusion is dual-licensed the same way, without additional terms.
