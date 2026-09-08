# `T?` — the nullable

`T?` is teko's one nullable mechanism, and the only slot `null` lands in. It is a **type
suffix**, written wherever a type is written, and it is taught over **any** type: a
`class`, an `interface`, a `delegate`, a `T[]` of heap and a `struct`, and every value
type — `i64`, `u64`, `i32`, `i8`, `i16`, `u8`, `u16`, `u32`, `bool`, `char`, `f64`, `f32`,
an `enum`, `TimeSpan` and `DateTime`.

This is a deliberate divergence from C#, and the only one made on purpose: C# has two
unrelated mechanisms — `Nullable<T>` for value types and `string?` for reference types, an
annotation the runtime never sees. teko has one mechanism, one spelling and one rule.
Everything else follows C#: `HasValue`, `Value`, the implicit `T` → `T?` and the refused
`T?` → `T`.

---

## The rule

**`null` lands only in a slot declared `T?`.** Written anywhere else it is refused where it
stands:

```teko
// no-run
class Cell {
    public i64 v;
}

i64 main() {
    Cell? ok = null;                   // the only slot null lands in
    Cell  no = null;                   // teko: null needs a slot declared Cell?
    i64   k  = null;                   // teko: a value of type uptr does not convert to i64
    return 0;
}
```

The rule reaches every store the language has: an initializer, an assignment, a `return`,
an argument of a free, method, virtual or interface call, an element of a `params T[]`, an
element of a `T[]`, a field store, a constructor argument, a delegate initializer and a
parameter default.

**A comparison against `null` stays legal on a slot that is not nullable**, and must: the
rule is about declarations, not a proof about values. A `null` still reaches a
non-nullable slot by three roads, all of them still open on purpose:

| road | why |
|---|---|
| a field of counted type nobody assigned | `rt_alloc` hands out zeroed bytes ([memory.md](memory.md)) |
| an element of `new T[n]` | the same zeroing |
| a `struct` local declared without `new` | the developer's error, by ruling (DECISION_LOG D42) |

So `if (c == null)` on a `Cell` parameter compiles exactly as it always did, and the
run-time guards — `teko: call through a null delegate`, `teko: index into a null array` —
stay exactly where they are.

---

## Representation

**A `T?` value is a pointer-width handle. `null` is the handle `0`. `HasValue` is
`handle != 0`.** That one invariant holds for every `T`; what the handle points at is what
differs.

| `T` | `T?` is | counted? |
|---|---|---|
| `class`, `interface`, `delegate`, `T[]` | **the same pointer**: no box, no extra word, no extra count | yes, exactly as `T` is |
| `struct` | the same pointer | no, exactly as `T` is |
| `i64`, `f64`, `i8`…`u64`, `bool`, `char`, an `enum`, `TimeSpan`, `DateTime` | a pointer to an **immutable counted box** holding those bytes | yes |

`Cell?` and `Cell` therefore have the same representation, and everything the reclaim does
for a `Cell` it does for a `Cell?` with no line of its own — `rc_dec(0)` and `rt_own(0)`
are already no-ops over the null handle ([memory.md](memory.md)).

The **box** is the object shape a `T[]` of heap already has (vtable, count, width, then
the payload at `+24` — [memory.md](memory.md) § "The object"), so a `T?` field, element,
argument, return, temporary and capture are the counted ones the reclaim already knew, and
no rule of lifetime is added anywhere. One release serves every value nullable there is: a
box holds no reference, because a counted `T` is already a pointer and takes the row above.

**The box is never written after it is built.** Every store into a `T?` slot builds a
fresh one, which is where value semantics come from:

```teko
// no-run
i64? a = 5;
i64? b = a;                                      // the same box
b = 7;                                           // ...and this is a NEW one
// a.Value is still 5
```

There is no way to reach a payload's address from the surface — `.Value` is not a slot —
so the invariant cannot be broken from a program.

**The honest cost, stated rather than hidden: `i64? x = 5;` allocates**, and `rt_live()`
counts it. C#'s `int?` costs nothing. A 32-byte box comes straight back to the 32-byte
free list, so a loop that churns `i64?` reuses one block forever and `rt_peak()` stays
flat; `tests/surface_nullable_value.tk` asserts both numbers rather than arguing them.

A `T?` costs the type registry **one row per distinct `T` spelled**, exactly what `T[]`
costs, plus — for a value `T` — one three-line writer per distinct payload type in the
programs that box one.

---

## Writing it

| written | means |
|---|---|
| `Cell? c;` | a local declared empty — `Cell? c = null;` is the same slot |
| `Cell? c = null;` | the explicit spelling |
| `Cell? c = obj;` | implicit `T` → `T?`, and the very same pointer |
| `Shape? s = new Circle(4);` | implicit `D` → `B?`, wherever `D` → `B` already is |
| `IArea? a = new Square(5);` | implicit `C` → `I?`, the same |
| `public Cell? next;` | a field |
| `i64 use(Cell? c)` / `Cell? make(i64 k)` | a parameter and a return type |
| `Cell?[] cs = new Cell?[3];` | an array whose ELEMENT is a nullable |
| `Cell[]? xs = null;` | a NULLABLE array |
| `params Cell?[] cs` | a variadic list of nullables |
| `Op? f = null;` | a nullable delegate |
| `Vec? v = null;` | a nullable `struct` |
| `void fill(ref Cell? c)` / `out Cell? c` | the caller's own slot, written by the callee |
| `i64? n = 5;` | a VALUE nullable: the implicit `T` → `T?` builds the box |
| `f64? x = 5;` | ...with C#'s own widening first: one point zero, boxed |
| `Color? c = Color.Blue;` | an `enum?`, at whatever width it is declared with |
| `TimeSpan? t = TimeSpan.FromHours(2.0);` | a primitive with members |
| `i64?[] ns = new i64?[3];` | an array whose elements are boxes, zeroed to null |
| `params i64?[] xs` | a variadic list of them |
| `public i64? count;` | a field, zeroed to null with the object |

---

## Reading it

| member | means |
|---|---|
| `x.HasValue` | `x != 0`, typed `i64`. No guard, no allocation |
| `x.Value` | the checked read: the handle, or a panic if it is `0` |
| `x.Value.m()` | ...and a member through it |
| `x.v` where `v` is a member of `T` | `teko: a Cell? is read through .Value` |
| `x.zz` where `zz` is nothing | `teko: unknown member of Cell?: zz` |
| `x.Value = e` | `teko: .Value is not a slot` |
| `x.GetValueOrDefault()` on a VALUE nullable | the payload, or the zero of its type when the handle is 0 |
| `x.GetValueOrDefault()` on a REFERENCE nullable | `teko: a reference nullable has no default` |
| `x.GetValueOrDefault(fallback)` | `teko: unknown member of i64?: GetValueOrDefault` — an arity this type does not have |

For a value `T`, `x.Value` is the guard and then a typed load of the payload: an `i8?` of
`-5` reads back `-5` (a signed narrow load is sign extended), an `f64?` reads back a
float, an `enum?` its own member and a `TimeSpan?` a `TimeSpan`. `GetValueOrDefault()`
reads the same width through an address that is the box's own or eight zero bytes, so
`default(T)` costs no branch of its own: `0`, `0.0`, `false`, `'\0'`, the zero member of
an `enum`, a `TimeSpan` of no ticks.

`c.v` is refused rather than forwarded because `Nullable<T>` has the members `Nullable<T>`
has, which is C#'s own rule for `int?`.

`GetValueOrDefault()` is refused on a reference nullable on purpose, and it is the one
place a rule beats C# fidelity: C#'s answer is `default(T)`, which for a reference is
`null`, and handing a `null` to a slot typed `T` is what the whole page exists to stop.

**`.Value` on an empty nullable panics** — `teko: a nullable with no value`, exit 70, the
same guard and the same code every other run-time check in this port uses
([memory.md](memory.md)).

```teko
// expect-exit: 42
#include "rt.tk"

class Cell {
    public i64 v;
    public Cell(i64 x) { v = x; }
    public i64 get() { return v; }
}

class Node {
    public i64 v;
    public Node? next;                           // a field that may hold nothing
    public Node(i64 x) { v = x; }
}

Cell? make(i64 k) {                              // a nullable return
    if (k == 0) return null;
    return new Cell(k);
}

i64 depth(Cell? c) {                             // a nullable parameter
    if (c == null) return 0;
    return c.Value.get();
}

i64 main() {
    Cell? c;                                     // no initializer: the slot is null
    if (c != null) return 1;
    if (c.HasValue) return 2;

    c = new Cell(7);
    if (!c.HasValue) return 3;
    if (c.Value.get() != 7) return 4;
    Cell hard = c.Value;                         // the checked read into a hard slot
    if (hard.v != 7) return 5;

    if (make(0) != null) return 6;
    if (depth(make(4)) != 4) return 7;

    Node a = new Node(1);
    a.next = new Node(2);                        // implicit Node -> Node?
    if (a.next.Value.v != 2) return 8;
    if (a.next.Value.next != null) return 9;     // never assigned: the zeroed field

    Cell?[] cs = new Cell?[2];                   // an array of nullables
    if (cs[0] != null) return 10;
    cs[0] = new Cell(3);
    if (cs[0].Value.get() != 3) return 11;

    Cell[]? xs = null;                           // a nullable array
    if (xs != null) return 12;
    return 42;
}
```

...and the same three members over a value, where the handle points at a box:

```teko
// expect-exit: 42
#include "rt.tk"

enum Color { Red, Green, Blue }

class Holder {
    public i64? count;                           // a field, zeroed to null
    public Holder() { }
}

i64? twice(i64? n) {                             // a parameter and a return
    if (n == null) return null;
    return n.Value * 2;
}

i64 main() {
    i64 floor = rt_live();

    i64? n;                                      // no initializer: the slot is null
    if (n != null) return 1;
    if (n.GetValueOrDefault() != 0) return 2;

    n = 5;                                       // ...and this ALLOCATES the box
    if (rt_live() != floor + 1) return 3;
    if (n.Value != 5) return 4;
    n = 7;                                       // a fresh box, the old one released
    if (rt_live() != floor + 1) return 5;

    i64? a = 5;
    i64? b = a;
    b = 7;                                       // the box is immutable: `a` stands
    if (a.Value != 5) return 6;

    i8? small = 0 - 5;                           // every width, and both register files
    if (small.Value != 0 - 5) return 7;
    f64? x = 5;                                  // an integer, widened then boxed
    if (x.Value != 5.0) return 8;
    Color? c = Color.Blue;
    if (c.Value != Color.Blue) return 9;

    Holder h = new Holder();
    if (h.count != null) return 10;
    h.count = 1;
    if (h.count.Value != 1) return 11;

    i64?[] ns = new i64?[2];                     // an array of boxes
    ns[0] = 3;
    if (ns[0].Value != 3) return 12;
    if (ns[1] != null) return 13;

    if (twice(4).Value != 8) return 14;
    if (twice(null) != null) return 15;
    return 42;
}
```

---

## Conversions

| written | what happens |
|---|---|
| `T` → `T?` | **implicit**, at every slot. For a reference it is identity and the tree does not move; for a value the compiler writes the box |
| an integer → `f64?` | implicit, C# §10.2.3's own widening first and the box second |
| a number → `Color?`, `TimeSpan?` | **refused**: `teko: a value of type i64 does not convert to Color?`. An `enum` and a primitive with members take their own type and nothing else, inside a box as outside one |
| `f64` → `i64?` | **refused**: `teko: a value of type f64 does not convert to i64?`. C# narrows nothing without a cast written down |
| `D` → `B?`, `C` → `I?` | implicit, whenever `D` → `B` and `C` → `I` already are |
| `null` → `T?` | implicit; the handle `0` |
| `T?` → `T` | **refused**: `teko: a value of type Cell? does not convert to Cell`. Write `.Value` |
| `T?` → `U?` where `T` → `U` | **refused**: no covariance between nullable rows. Write `x.Value` |
| `null` → `T` | **refused**: `teko: null needs a slot declared Cell?` |
| `null` → `uptr`, `ptr`, `str` | **accepted, unchanged**: `0` is an ordinary value of a raw pointer |
| `T?` → a numeric slot | **refused**: `teko: a value of type Cell? does not convert to i64` |

---

## Overloads

`null` matches a parameter declared `T?` and no other, so a name overloaded on both is
unambiguous:

| call | picks |
|---|---|
| `held(null)` against `held(Cell)` and `held(Cell?)` | `held(Cell?)` |
| `held(obj)` against the same pair | `held(Cell)` — an object prefers the exact signature |
| `held(null)` against `held(Cell)` alone | refused where it stands, `teko: null needs a slot declared Cell?` — a name declared once is judged by the slot, not by an overload search |
| `held(null)` against `held(Cell)` and `held(i64)` | refused: `teko: no overload of held matches these arguments` — neither candidate takes `null` |
| `pick(5)` against `pick(i64)` and `pick(i64?)` | `pick(i64)` — an integer literal lands on `i64`, which is C#'s own preference |
| `pick(q)` where `q` is an `i64?`, against the same pair | `pick(i64?)` — the exact type |
| `pick(5)` where the only candidate that could take it is `pick(i64?)` | refused: the rounds match by exact type, by integer literal and by `null`, and the implicit `T` → `T?` is not one of them ([not-yet.md](not-yet.md)). A name declared ONCE takes the wrap and needs no round |

---

## Operators

| form | verdict |
|---|---|
| `x == null`, `x != null`, `null == x` | taught: a comparison of the handle against 0 |
| `a == b` on two nullables | refused: ``teko: Cell? declares no operator `==` ``, and ``teko: i64? declares no operator `==` `` for a value. Comparing two handles is not comparing two values: two boxes holding 5 would answer "different" |
| `a + b`, `a < b`, `a & b` … | refused, the same way two bare references are. C#'s lifted arithmetic stays out; `a.Value + b.Value` is the form |
| `a ?? b`, `a?.m` | not taught yet ([not-yet.md](not-yet.md)) |
| `x is null`, `case null:` | there is no `is` in teko, and a `switch` takes no reference subject |

---

## The reclaim

Nothing new. A `T?` is counted exactly as what it encloses is, so it is released at the `}`
that closes its block, on every jump that leaves it, with the object that holds it as a
field, and element by element inside a `T?[]`.

| the value | `rt_live()` moves by | released when |
|---|---|---|
| `Cell? c = new Cell(1);` | 1 — the cell, exactly as `Cell c` would | the block closes |
| `Cell? c = null;` | 0 | nothing to release |
| `Cell?[] cs = new Cell?[3];` | 1 + one per element that is not null | the array's own release walks the elements |
| `Vec? v = new Vec();` | 0 — a `struct?` is not counted, exactly as a `struct` is not | — |
| `i64? n = 5;` | 1 — the box | the block closes |
| `i64? n = null;` | 0 | nothing to release |
| `n = 7;` (a second store) | 0 net — a new box, the old one released | at the store |
| `i64?[] ns = new i64?[3];` | 1 + one per element that is not null | the array's own release walks the elements |

A box captured by a closure earns the closure's own reference, and an `i64?` handed to a
call is parked and swept when the statement ends — both of them the counted rules that
were already there.

---

## What it does not do

`??`, `?.`, a lifted `==`, flow narrowing (`if (c != null) { c.v }`), `T?` as a generic
argument, calling a nullable delegate, `T?` on a short type name inside a `namespace`,
`x.GetValueOrDefault(fallback)`, a ternary whose arms are a value and `null`, an overload
picked through the implicit `T` → `T?`, a member on a GLOBAL nullable and an assignment to
a nullable PARAMETER of value type are each a row of [not-yet.md](not-yet.md), with the
message each one answers. `ref T?` and `out T?` are taught. The design they come from is
[`../specs/nullable.md`](../specs/nullable.md).
