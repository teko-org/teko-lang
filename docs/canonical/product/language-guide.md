# The Teko Language Guide

## Entry points

A project has exactly one of two forms of entry point, never both:

- **Virtual main** — top-level statements (plus local `let`/`const`) directly in `main.tks`,
  no function wrapper. Natural end of the file → exit 0; `teko::exit(n)` → exit `n`; a panic →
  stderr + a nonzero exit.
- **Explicit `fn main`** — a single `fn main(): i32` inside `main.tks`, and only there.
  Teko has no `void` and no function overloading (both are explained below), so there is
  exactly one legal signature — a `fn main` without an arrow is rejected as an honest error
  rather than silently treated as exit 0. Command-line arguments come from `teko::env::args()`
  (also callable bare, as `args()`, alongside a short list of other bare-callable builtins).

Mixing loose top-level statements with an `fn main` declaration in the same file is rejected —
one file, one entry point, no ambiguity about which one runs.

## Types

The native numeric set is `u8…u64` / `i8…i64` (integers), `f32`/`f64` (floats), `dec` (a
512-bit decimal), `bigint` (arbitrary precision), plus `bool` and `byte` (a distinct octet
type, not interchangeable with `u8`). An unannotated float literal is `f64`; `f32` needs an
explicit annotation. Floating-point division by zero **panics** — Teko intercepts at the
origin rather than letting an IEEE ∞/NaN leak into ordinary values; a caller that wants
recoverable division uses `teko::math::div -> T | error`.

`type X = Y` is **nominal**, never a transparent alias: `X` is a distinct type whose identity
is its name, not "the same shape as `Y`." Crossing between a newtype and its base needs an
explicit cast.

A **`variant`** is a closed union of complete, separately-declared types
(`type Shape = Circle | Square`) — no inline payload constructors, no `void` member, no
nullable member. The only way to reach the value inside is `match`, which forces
exhaustiveness over every case the compiler knows about at compile time.

## Errors are values

A recoverable failure is the native, lowercase `error` case of a variant — `T | error` for a
function that can fail with a value on success, `error?` for a function with no success value
(null on the ok path, an `error` on failure — never `void | error`, which is illegal). There is
no exception, no `raise`/`throw`, no stack unwinding: a failure is handled with `match`, exactly
like any other variant.

```teko
fn parse_port(s: str): u16 | error {
    match teko::numeric::parse_u16(s) {
        u16 as n => n
        error as e => e
    }
}
```

The idiomatic way to *produce* an error is the factory, not a bare struct literal:

```teko
error::new("port out of range")
error::new_pos("unexpected token", line, col, file)
error::join(left, right)          // flattens two errors into one message
```

**Guard, don't nest.** When a `match` on a fallible call exists only to extract the value and
continue, write it as a flat guard line rather than nesting the rest of the function inside the
success arm:

```teko
let p = match parse_port(input) { error as e => return e; u16 as n => n }
// use `p` at the top level from here — no extra indentation
```

Chained fallible calls stay flat this way — N guard lines instead of an N-level nested
"staircase."

## Optionals

`T?` is the *only* vessel for absence — a plain `T` is guaranteed non-null. `?.` safely
navigates through a possibly-absent value; `??` supplies a fallback:

```teko
let name: str? = lookup_name(id)
teko::io::println(name?.upper() ?? "unknown")
```

## Pattern matching

`match` is the one construct for control flow over data, on two axes depending on the subject:
a **value** match (literals, ranges, `2 | 3`) or a **type** match over a variant's cases.
Variant-axis binding is intrinsic — `Circle as c` binds the whole case, `Rect { w; h }` binds
named fields by position-independent name (unlisted fields are simply ignored — there is no
`..` spread). Exhaustiveness is enforced at compile time; `_` is an optional valve used only
when coverage is otherwise incomplete. A `when`-guarded arm covers *conditionally* and never
counts toward exhaustiveness on its own.

`match` is never used on a `bool` — write `if`/`else` instead; this is a style law the compiler
itself follows and that `teko lint` flags.

## Classes, structs, interfaces

- **`struct`** — a value type. Cheap to copy, no identity beyond its contents.
- **`class`** — a reference type. Classes have factories, not constructors: a static method
  (called with `::`) builds and returns an instance; there is no hidden allocation the language
  performs on your behalf.
- **`interface`** — a pure contract: method signatures only, dispatched dynamically through a
  fat pointer at runtime for class implementers. No overload, no override-of-arbitrary-methods:
  a name is unique in its scope, and subtype polymorphism goes through interfaces explicitly.

```teko
type Shape = interface {
    fn area(self): f64
}

type Circle = class {
    pub r: f64
    pub fn make(r: f64): Circle { Circle { r = r } }
    pub fn area(self): f64 { 3.14159 * self.r * self.r }
}
```

An instance method's receiver (conventionally named `self`, though the name itself is not a
reserved word) is implicit at the call site (`shape.area()`); a static method is called with
`::` (`Circle::make(2.0)`). Methods on a `class` (a reference type) that need to receive
another class value as a parameter, or store one in a field, use the explicit **`ref`**
modifier — see "References and memory" below.

## Generics

Generics are resolved by **monomorphization**: a generic function or type is specialized once
per distinct set of concrete type arguments actually used in the program, so there is no
runtime dispatch cost and no boxing for the generic path itself.

```teko
fn first<T>(xs: []T): T? {
    if xs.len == 0 { return null }
    xs[0]
}
```

## References and memory — no garbage collector

Values (structs, primitives, inline variant payloads) copy cheaply. **Classes are reference
types.** A function receiving a class argument, or a struct field storing a borrowed reference,
declares it with **`ref`**, explicit at the parameter/field:

```teko
fn rename(ref d: Dog, new_name: str) {
    d.name = new_name
}
```

`ref` is the entire safe surface — there is no `&`/`*` sigil in safe code; those exist only as
raw, `unsafe`-only pointers. A `ref` is never null (absence is `ref T?`, following the same
`T?` rule as everything else), and a stored borrow of an optional field is only accepted when
its lifetime provably matches the field's — the compiler proves this for you; you do not write
lifetime annotations to make it happen.

Memory itself has no garbage collector and (for ordinary code) no manual `free`:

- **Arena regions (default, invisible).** Allocation and deallocation are compiler-managed;
  a scope's region is dropped as a whole when the scope ends.
- **`adopt { }` (opt-in).** For cyclic or long-lived graphs that don't fit a strict scope
  lifetime — the region is bulk-dropped at the block's closing brace regardless of internal
  cycles.
- **`unsafe` (opt-in, explicit, contagious).** A type or function modifier — not a block —
  that enables raw pointers (`ptr<T>`) and addresses every method on the marked type as unsafe.
  If a type doesn't need raw pointers or unsafe calls, it has no reason to be marked.

There is no `malloc`/`free` in safe code; raw allocation exists, but only behind `unsafe`,
always explicit and contained.

## Concurrency

The concurrency unit is **`isolate`**: a genuinely separate OS thread with its own arena root,
sharing nothing until an explicit channel moves data across — the opposite guarantee from an
async/await suspension model, and named to say so.

```teko
teko::isolate::fork_join([
    teko::isolate::spawn(fn() { compute_left() }),
    teko::isolate::spawn(fn() { compute_right() }),
])
```

`teko::isolate::hardware_parallelism()` reports how many isolates can usefully run in parallel
on the host. Explicit cross-isolate sharing (`chan<T>`, `send`/`recv`) is the mechanism for the
cases that genuinely need shared mutable state between isolates — most concurrent work in
practice is fork-join with disjoint writes and a read after a join barrier, which needs no
shared channel at all.

## Testing is part of the language, not an add-on

```teko
#test
fn adds_two_positive_numbers() {
    teko::test::assert_eq(add(2, 3), 5)
}
```

A `#test` function lives beside the code it tests, in a `.tkt` file, discovered and run by
`teko test .`. `teko build` runs the same gate before it will produce a release artifact — see
`cli-and-tooling.md` for the full command surface, and `packages.md` for how coverage floors
are declared in the manifest.

## Operators and syntax, briefly

- **Precedence follows ordinary school-math intuition** (bitwise/shift sit at the arithmetic
  levels they analogize to, and above comparison — fixing the classic C gotcha where
  `a & b == c` silently means `a & (b == c)`).
- **`+` never concatenates strings.** String building is `~` (concatenation, literal runs fold
  at compile time) or `$"…{expr}…"` interpolation.
- **`++`/`--` are statement-only** (`p++`, never inside an expression like `arr[i++]`) — a
  machine increment exists, but never as a value-and-mutation hybrid inside a larger expression.
- **The only loop is `loop { }` + `break`** — no `while`, no `for`. Iteration over a collection
  goes through `teko::iter` adapters and the `in` operator.
- **Arrays are `[]T`** (Go-style), never nullable, always initialized, index out of range panics
  at runtime, `.len` is `u64`.
- **`flags`** is a dedicated bitflag type — a closed set of named bit positions with bitwise
  combination built in, rather than modeling bitflags as raw integers with manually-maintained
  constants.

## Modules, visibility, and the manifest

Every symbol's canonical name is an absolute path from the project's root namespace
(`teko::lexer::Token`); the source root is invisible in that address. `use` is alias-only — it
never changes visibility and never declares a dependency, it only binds a shorter local name;
there is no wildcard import and no re-export. Visibility is `pub` (visible across the project,
not in the compiled artifact's public header) versus `exp` (exported in the artifact's public
header — the library's real external ABI); private is simply the absence of either keyword.
See `packages.md` for the manifest format itself.
