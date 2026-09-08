# `T?` — the nullable

`T?` is teko's one nullable mechanism, and the only slot `null` lands in. It is a **type
suffix**, written wherever a type is written, and today it is taught over a **reference**:
a `class`, an `interface`, a `delegate`, a `T[]` of heap and a `struct`. A value type
(`i64?`, `f64?`, `bool?`, an `enum?`, `TimeSpan?`) is refused for now
([not-yet.md](not-yet.md)).

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
`handle != 0`.** For a reference `T` the handle **is** `T`'s own pointer: no box, no extra
word, no extra reference count. `Cell?` and `Cell` have the same representation, so
everything the reclaim does for a `Cell` it does for a `Cell?` with no line of its own —
`rc_dec(0)` and `rt_own(0)` are already no-ops over the null handle
([memory.md](memory.md)).

A `T?` costs the type registry **one row per distinct `T` spelled**, exactly what `T[]`
costs, and nothing at run time.

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
| `x.GetValueOrDefault()` | `teko: a reference nullable has no default` |

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

---

## Conversions

| written | what happens |
|---|---|
| `T` → `T?` | **implicit**, at every slot. For a reference it is identity: the tree does not move |
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
| `held(null)` against `held(Cell)` alone | refused: `teko: no overload of held matches these arguments` |

---

## Operators

| form | verdict |
|---|---|
| `x == null`, `x != null`, `null == x` | taught: a comparison of the handle against 0 |
| `a == b` on two nullables | refused: ``teko: Cell? declares no operator `==` ``. Comparing two handles is not comparing two values |
| `a + b`, `a < b`, `a & b` … | refused, the same way two bare references are |
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

---

## What it does not do

`T?` over a value type, `??`, `?.`, `GetValueOrDefault()` on a value nullable, a lifted
`==`, flow narrowing (`if (c != null) { c.v }`), `T?` as a generic argument, calling a
nullable delegate and `T?` on a short type name inside a `namespace` are each a row of
[not-yet.md](not-yet.md), with the message each one answers. `ref T?` and `out T?` are
taught. The design they come from is [`../specs/nullable.md`](../specs/nullable.md).
