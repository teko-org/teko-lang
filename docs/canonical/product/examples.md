# Worked Examples

The repository's `examples/` tree is the compiler's own regression corpus (used to prove the
toolchain against real programs, one behavior per fixture) rather than a curated tutorial
gallery — the examples below are written for this guide instead, each demonstrating one pillar
of the language guide end to end.

## 1. Errors as values — a small CLI argument parser

```teko
// main.tks
let args = teko::env::args()

if args.len < 2 {
    teko::io::eprintln("usage: greet <name>")
    teko::exit(2)
}

match greet(args[1]) {
    str as msg => teko::io::println(msg)
    error as e => {
        teko::io::eprintln(e.message)
        teko::exit(1)
    }
}

fn greet(name: str): str | error {
    if name.len == 0 {
        return error::new("name must not be empty")
    }
    $"hello, {name}!"
}
```

Every failure path is visible in the return type and handled with `match` — there is no hidden
exception that could unwind past `greet`'s caller unannounced.

## 2. Optionals and safe navigation — looking something up

```teko
type User = struct { name: str; email: str? }

fn find_user(id: u64): User? {
    if id == 1 { return User { name = "Ana", email = "ana@example.com" } }
    null
}

fn contact_line(id: u64): str {
    let u = find_user(id)
    $"{u?.name ?? "unknown"} <{u?.email ?? "no email"}>"
}
```

`?.` short-circuits through an absent `User`; `??` supplies the fallback at each step. There is
no null-pointer dereference possible here — `find_user`'s `User?` return type says, at the
type level, that absence is a real, handled case.

## 3. Classes, interfaces, and generics — a small shape library

```teko
type Shape = interface {
    fn area(self): f64
}

type Circle = class {
    pub r: f64
    pub fn make(r: f64): Circle { Circle { r = r } }
    pub fn area(self): f64 { 3.14159 * self.r * self.r }
}

type Square = class {
    pub side: f64
    pub fn make(side: f64): Square { Square { side = side } }
    pub fn area(self): f64 { self.side * self.side }
}

fn total_area<T>(ref shapes: []T): f64 {
    let sum = 0.0
    loop shapes as s {
        sum += s.area()
    }
    sum
}
```

`total_area` is monomorphized once per concrete shape type it's actually called with — no
runtime dispatch cost for the generic itself; dynamic dispatch across *different* shape types
in the same slice goes through the `Shape` interface's fat pointer instead.

## 4. Memory — arena by default, `adopt` for a cyclic graph

```teko
type Node = class {
    pub value: i64
    pub next: Node?
}

fn build_chain(n: i64): Node? {
    let head: Node? = null
    let i = n
    loop {
        if i == 0 { break }
        head = Node { value = i; next = head }
        i -= 1
    }
    head              // the whole chain drops with this function's arena scope
}

fn build_ring(n: i64) {
    adopt {
        // a cyclic structure would leak under strict arena scoping without `adopt`;
        // this whole region bulk-drops at the closing brace regardless of the cycle
        let a = Node { value = 1; next = null }
        let b = Node { value = 2; next = a }
        a.next = b     // now cyclic
    }
}
```

## 5. Testing embedded in the build

```teko
// math.tks
pub fn add(a: i64, b: i64): i64 { a + b }
```

```teko
// math_test.tkt
use math

#test
fn add_combines_two_positives() {
    teko::test::assert_eq(math::add(2, 3), 5)
}

#test
fn add_handles_negatives() {
    teko::test::assert_eq(math::add(-1, 1), 0)
}
```

```sh
teko test .          # runs both #test functions natively
teko build .          # runs them again, as a gate, before producing a release binary
```

## 6. Isolate concurrency — fork-join

```teko
fn parallel_sum(xs: []i64): i64 {
    let mid = xs.len / 2
    let (left, right) = (xs[0..mid], xs[mid..xs.len])
    let results = teko::isolate::fork_join([
        teko::isolate::spawn(fn() { sum(left) }),
        teko::isolate::spawn(fn() { sum(right) }),
    ])
    results[0] + results[1]
}

fn sum(xs: []i64): i64 {
    let total = 0
    loop xs as x { total += x }
    total
}
```

Each spawned unit runs on its own isolated thread with its own arena root; the two halves never
share mutable state, and `fork_join` only reads the results after both have completed.
