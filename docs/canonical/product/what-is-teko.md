# What is Teko?

Teko is a compiled, statically-typed programming language with a **fully self-hosting
compiler**: the compiler is written in Teko itself, compiles its own source tree to a working
native binary, and that binary rebuilds itself to a byte-identical fixpoint. It targets systems
programming — the space where C, Rust, Zig, and Go compete — with a specific bet: that the
common case can stay simple and safe *without* a garbage collector and *without* asking every
programmer to learn a borrow checker.

## The pillars

- **All-native output.** `teko build` compiles straight to a native object and links it — no
  GC; the shipped binary is native machine code.
- **Native debug iteration.** `teko run` compiles the same checked program natively at `-O0`
  and runs it immediately — fast, native debugging, full optimization control via `-O` flags
  when you want it.
- **Tests are part of the build.** `teko build` runs your `#test` functions *before* codegen;
  a failing test or coverage below the manifest's floor bars the release outright. Coverage
  exports as Cobertura XML.
- **Errors are values.** A function that can fail returns `T | error`; there is no exception,
  no `null` outside the explicit `T?` optional, no hidden control-flow path a failure can take.
- **Automatic memory, no GC, opt-in layers when you need more.** Arena regions are the
  invisible default — allocation and deallocation are compiler-managed, with no annotation
  burden in ordinary code. Two opt-in layers sit on top for the cases that genuinely need them:
  `adopt { }` for cyclic/long-lived graphs, and `unsafe` (a type/function modifier, not a block)
  for raw allocation. There is no `malloc`/`free` in safe code — raw allocation is explicit and
  contained.
- **A deliberately small surface.** One loop construct (`loop` + `break`), `match` for control
  flow over data, generics via monomorphization, value structs, reference classes, pure-contract
  interfaces, bitflag `flags`, and `extern` FFI to C libraries. Restriction is a design tool
  here, not an oversight: a smaller surface forces every addition to justify its weight.

## Who it's for

Teko targets the same territory as Rust, Zig, and Go — programs that need predictable
performance and memory behavior without a runtime — but bets that most of that territory does
not actually need a borrow checker's ceremony to get memory safety. The arena-by-default model
plus a compiler-inferred points-to/uniqueness analysis (the "spine") is the wager: safety comes
from the compiler proving what it can prove automatically, with `unsafe` as an honest, visible
opt-out for the code that genuinely needs raw control.

## A one-page taste

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
// Optionals: `T?`, safe navigation `?.`, and coalescing `??`.
pub fn safe(): i64 {
    let b: Box? = null
    b?.v ?? 8                 // absence falls back to 8
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

Programs have a **virtual main**: top-level statements in `main.tks` are the entry point — no
boilerplate `fn main` required for a simple program. A program that wants an explicit exit code
contract can instead write `fn main(): i32` (see `language-guide.md`); the two forms are
mutually exclusive within one project.

## Project status

Teko is pre-release, in active beta. Versioning tracks the remodel:
**alpha** (`0.0.1.x`, pre-remodel) → **beta** (the `0.X` waves, each finalizing one coherent
subset) → **`1.0.0.0`** = LTS, once the backlog is empty. This reference describes the target,
stable end-state of the design already committed to — see `docs/canonical/README.md` for how
that relates to the project's current, in-progress state.
