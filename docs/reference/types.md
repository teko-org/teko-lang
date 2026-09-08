# Types

What a teko program can name: the scalars it inherits from the `mc` core, the seven words
teko adds on top of them, and the three declarations that build a type of your own —
`struct`, `class` and `enum`.

Every example on this page is a whole program, compiled and run by
[`../../scripts/check-docs.sh`](../../scripts/check-docs.sh); the exit code is the
assertion.

---

## Scalars

The core's own eight words are teko's ([the core language](https://github.com/minicompiler/mc/blob/main/docs/reference/language.md) § 2):

| type | width | signed | notes |
|---|---|---|---|
| `u8` | 1 | no | a byte |
| `u16` | 2 | no | |
| `u32` | 4 | no | |
| `u64` | 8 | no | |
| `i32` | 4 | **yes** | C's `int`; the type an `extern` returning `int` declares |
| `i64` | 8 | **yes** | the working integer |
| `uptr` | 8 | no | the only pointer: opaque, no pointee, byte arithmetic |
| `void` | — | — | a return type, and nothing else |

teko adds seven names of its own. Each one is an **alias**: a new word for a
representation the core already has, so mixing an alias with its base needs no cast and
costs no instruction.

| word | is | notes |
|---|---|---|
| `bool` | `u8` | the width of a declaration; `true`/`false` are the literals |
| `byte` | `u8` | |
| `char` | `u32` | a scalar code point; `'a'`, `'\n'` are its literals |
| `isize` | `i64` | index/size, architecture-dependent — 64-bit today |
| `usize` | `u64` | the unsigned half of the same |
| `ptr` | `uptr` | teko draws no signed/unsigned pointer distinction: `ptr` and `uptr` are one type |
| `str` | `uptr` | a NUL-terminated string, by pointer; there is no separate string object |

`f32` and `f64` come from the `<float>` library the compiler loads, with their own
literals (`2.0`, `0.5f`) and the intrinsics `ldf64`/`ldf32`/`stf64`/`stf32`/`sqrt_f64`/
`fabs`/`fmin`/`fmax`. `i8` and `i16` (below, § "`i8` and `i16`") are neither of the eight
core words nor an alias of one: they are two primitives of teko's own, C#'s `sbyte` and
`short`.

A type word is **reserved program-wide**: `i64 str = 1;` is refused, exactly as C#
refuses a variable named `int`.

```teko
// expect-exit: 42
isize sum_isize(isize a, i64 b) {
    return a + b;
}

i64 main() {
    byte  b = 20;
    char  c = 12;
    usize u = (usize) b + (usize) c;
    isize n = sum_isize(10, 0);
    return (i64) u + (i64) n;
}
```

### Casts, width and sign

A cast is C-shaped and unambiguous, `(u32) x`, because a type word always follows the
`(`. Narrowing masks; widening is by the type's own kind — zero for `u8`/`u16`/`u32`,
**sign** for `i32`, `i8` and `i16`. Arithmetic is 64-bit on the widened value and wraps at
the next store. A comparison yields `i64` `0` or `1`.

teko does not teach C#'s integer promotion (`sbyte + sbyte` is an `int` in C#): `(i8) 100 +
(i8) 100` is `200` while it is still in flight — an ordinary 64-bit value — and only
narrows to `-56` at the next store into an `i8` slot, the same rule `u8`/`u16`/`u32`
already had. Teaching promotion for the two newest types while the three older ones kept
the old rule would be the real inconsistency, and it would change the base grammar (the
`+` in `a + b` is `mc`'s own, D3).

### `i8` and `i16`

C#'s `sbyte` and `short`: signed, one and two bytes wide. Unlike the seven aliases above,
each is its own primitive — `type_new("i8", 1, 1, TK_SINT)` and `type_new("i16", 2, 2,
TK_SINT)` — because `mc`'s own `TK_SINT` kind already does the sign-extending load, the
sign-extending cast, a signed `/`, `%` and `>>`, a signed comparison and a narrowed call
result for a type registered with it; nothing here re-teaches any of that.

```teko
// expect-exit: 42
i16 clamp16(i32 v) {
    if (v > 32767) return 32767;
    if (v < 0 - 32768) return 0 - 32768;
    return (i16) v;
}

i64 main() {
    i8  a = 0 - 5;
    i16 b = 0 - 300;
    if (a >> 1 != 0 - 3) return 1;               // an arithmetic shift, signed
    if (b / 2 != 0 - 150) return 2;               // a signed divide
    if (a >= 0) return 3;                         // a signed comparison
    i64 wide = a;                                 // widens with its sign
    if (wide != 0 - 5) return 4;
    f64 f = a;                                    // an integer converts to a float (above)
    if (f != 0.0 - 5.0) return 5;
    if (clamp16(70000) != 32767) return 6;        // saturates, narrows explicitly
    return 42;
}
```

`i8`/`i16` convert to a wider integer or to `f64`/`f32` the same way `i64` already does
(the section above; what does NOT convert is an `f32` into an `f64` slot or an `f64` into
an `f32` one — the width gap [not-yet.md](not-yet.md#numeric-conversions) lists, where the
bytes are reinterpreted and the value is wrong — so keep an `f32` in `f32` slots); nothing converts back without a cast, and a float, `null`, or a
class/struct/interface/delegate/`T[]` value does not convert INTO an `i8`/`i16` slot
either, with the same wording every other mismatched value already gets.

```teko
// no-run
i64 main() {
    i8 a = 1;
    i8 b = 2.5;                   // teko: a value of type f64 does not convert to i8
    i8 c = null;                  // teko: a value of type uptr does not convert to i8
    return 0;
}
```

`sbyte` and `short` are not taught: teko never inherited C#'s `int`/`long` either, and
registering half of the C# alias family while `int` does not exist would be worse than
neither half ([not-yet.md](not-yet.md)).

### `bool`, `true` and `false`

`true` and `false` are integer literals `1` and `0`, and both words are reserved. `bool`
sizes a declaration (one byte); a truth value in flight is an `i64`, so `if (x == 42)`
and `bool ok = x == 42;` are both ordinary.

```teko
// expect-exit: 42
bool is_the_answer(i64 x) {
    return x == 42;
}

i64 main() {
    bool ok = is_the_answer(42);
    if (ok == false) return 1;
    if (true == false) return 2;
    return 42;
}
```

### `f32` and `f64`

Float arithmetic is the `<float>` library's; the bit pattern of an `f64` is reached
through the runtime pair `tk_f64_bits`/`tk_f64_from_bits`
([runtime.md](runtime.md)), because a cast between a float and an integer converts the
**value**, never the bits.

```teko
// expect-exit: 42
#include "rt.tk"

i64 main() {
    f64 a = 40.0;
    f64 b = 2.0;
    u64 bits = tk_f64_bits(a + b);
    f64 back = tk_f64_from_bits(bits);
    return (i64) back;
}
```

**An integer converts to a float; a float does not convert back.** That is C#'s own
direction, and it holds in every slot: a variable initializer, an assignment, a `return`,
an argument (of a free function, a method, a virtual call, an interface call), an element
of a `params f64[]`, a field store, and a binary mixing the two — where the integer
operand is converted whichever side it stands on, so `1 + 2.5` and `2.5 + 1` are both
three point five. The conversion is a cast the compiler writes for you, and it rounds the
way C#'s `long` to `double` does: an `f64` carries 53 bits of precision, so every integer up
to 2^53 in magnitude arrives exact and a larger one lands on the nearest representable
double.

```teko
// expect-exit: 42
#include "rt.tk"

f64 twice(f64 x) { return x + x; }

i64 main() {
    f64 y = 1;                                   // an initializer
    y = 5;                                       // an assignment
    if (twice(3) != 6.0) return 1;               // an argument
    if (1 + 2.5 != 3.5) return 2;                // the integer on the left
    if (2.5 + 1 != 3.5) return 3;                // ...and on the right
    return (i64) (y + 37);
}
```

The other direction is refused where it is written, because C# narrows nothing without a
cast spelled out. It reads `teko: a value of type f64 does not convert to i64`
([diagnostics.md](diagnostics.md)) — and `(i64) x` is the cast that says you meant it.

```teko
// no-run
i64 main() {
    i64 n = 2.5;                                 // teko: a value of type f64 does not convert to i64
    return n;
}
```

`null` lands **only in a slot declared `T?`** ([nullable.md](nullable.md)):
`Cell c = null;` is `teko: null needs a slot declared Cell?`, and `Cell? c = null;` is the
spelling. A comparison against `null` stays legal on any reference-shaped slot. It is not a
number either, so it does not land in a numeric slot at all —
`teko: a value of type uptr does not convert to i64`, `uptr` being the type `null`
carries.

**Every type on this page has a `T?`**, the scalars included: `i64?`, `f64?`, `bool?`,
`char?`, `i8?`, an `enum?`, `TimeSpan?` and `DateTime?` are the same one mechanism, over a
counted box holding the value's own bytes. `i64? n = 5;` therefore ALLOCATES, `n.Value` is
the checked read and `i64 j = n;` is refused —
`teko: a value of type i64? does not convert to i64`. The whole rule, the cost and what it
does not do are [nullable.md](nullable.md).

Two operators read a nullable, both C#'s own: **`a ?? b`** answers `a`'s own value when the
handle is not 0 and `b` when it is (`i64 n = count ?? 5;`, `Cell c = maybe ?? new Cell(3);`),
evaluating `a` exactly once and `b` only when it is needed; **`a?.m`** and **`a?.m(x)`** read
a member only when `a` has a value and answer `M?`, so `a?.b?.c` chains and every link that
is empty makes the whole read `null`. `??` is right-associative and ties with `||` at the
Pratt table's floor, which is the one divergence it carries — `a || b ?? c` reads as
`(a || b) ?? c` ([not-yet.md](not-yet.md)). Both are [nullable.md](nullable.md)'s
own section.

```teko
// no-run
i64 solo(i64 a) { return a; }

i64 main() {
    return solo(null);                           // teko: a value of type uptr does not convert to i64
}
```

Neither does a NON-null reference. A struct, a class, an interface, a delegate and a `T[]`
of heap all carry a row of the type table, and none of them is a number — a class value
reaching an `i64`/`f64` slot used to pass its pointer through as a bit pattern, silently,
in every one of the same slots listed above (D34). It is refused the same way, by the
value's own type name:

```teko
// no-run
class Foo { public i64 v; }

i64 main() {
    Foo f = new Foo();
    i64 n = f;                                   // teko: a value of type Foo does not convert to i64
    return n;
}
```

A raw `uptr`/`ptr` value is left alone in a numeric slot — the core converts it to an
integer of its own accord, and only a value the type table has a row for (or the `null`
literal above) is judged here.

Two floats of different widths do not convert to each other yet: an `f32` in an `f64`
slot is neither converted nor refused ([not-yet.md](not-yet.md)).

### `ptr`, `uptr` and `str`

Memory is read and written by explicit width — `ld8`/`ld16`/`ld32`/`ld64` and
`st8`/`st16`/`st32`/`st64` — and `&x` is the address of a local, a global or a function.
`p + 1` is one **byte** further: a pointer has no pointee to scale by. `str` is that same
pointer with a NUL at the end, so `tk_str_len` and `tk_str_slice` (a view, zero copy) are
ordinary functions over it.

```teko
// expect-exit: 42
#include "rt.tk"

u8 tbl[8];

ptr at(uptr base, i64 i) {
    return base + i;
}

i64 main() {
    st64(tbl, 40);
    ptr p = at(tbl, 0);
    str s = "hi";
    return ld64(p) + tk_str_len(s);
}
```

---

## `struct`

A struct is a block of fields with no header of its own: the first field sits at offset 0.
A value of struct type is a **pointer to the allocation**, eight bytes wide, produced by
`new`; `new Name` and `new Name()` both hand out zeroed bytes, so a field nobody assigned
reads as `0` and a reference field reads as null. **A struct has no default value of its
own**: `Name p;` with no `new` is not initialized — it is not null, it is whatever the slot
held — and reading or writing a field through it is the developer's error, not a refusal or
a panic the compiler makes for you. Build it (`Name p = new Name;`) before you touch it.

```
struct Name { [modifier] type field; ... methods ... }
```

Fields are laid out in declaration order at their natural alignment. Every offset, and the
total size, is published as a compile-time constant: `NAME_FIELD` (both parts uppercased)
and `NAME_SIZE`.

```teko
// expect-exit: 42
#include "rt.tk"

struct Point {
    public u8  tag;
    public i64 x;
    public u32 y;
}

struct Line {
    public Point a;
    public i64   len;
}

i64 main() {
    if (POINT_TAG != 0) return 1;
    if (POINT_X != 8) return 2;                  // natural alignment, not offset 1
    if (POINT_Y != 16) return 3;
    if (POINT_SIZE != 24) return 4;

    Point p = new Point;
    if (p.x != 0) return 5;                      // the allocation is zeroed
    p.tag = 2;
    p.x = 30;
    p.y = 10;

    Line l = new Line();
    l.a = p;
    l.len = 0;
    return l.a.x + l.a.y + l.a.tag;              // a struct field chains
}
```

A struct declares methods, with the same implicit receiver, default arguments and
overloads a class method has ([classes.md](classes.md)). What it does not have is a
vtable: `virtual`, `override` and `use` of a trait are refused on a struct by name, and a
struct is never a base class.

A struct is **not reference-counted** — it has no vtable, hence no release function to
reach. Its allocation lives for the run ([memory.md](memory.md)).

---

## `class`

A class is a struct with two words in front of it: word 0 is the vtable pointer and word 1
the reference count, so its own fields start at offset 16, after the base class's.

```
[public|internal] [abstract] [partial] class Name [: Base] [, Iface...] { members }
```

`NAME_FIELD`/`NAME_SIZE` are published the same way, and they measure the object — a
`static` field is a global of its own and takes none of it.

```teko
// expect-exit: 42
#include "rt.tk"

class Shape {
    public i64 side;

    public i64 twice() {
        return side * 2;                         // `side` is `this.side`
    }
}

class Square : Shape {
    public i64 tag;
}

i64 main() {
    if (SHAPE_SIDE != 16) return 1;              // the vtable and the count come first
    if (SHAPE_SIZE != 24) return 2;
    if (SQUARE_TAG != 24) return 3;              // the base's field keeps its place
    if (SQUARE_SIZE != 32) return 4;

    Shape p = new Shape;
    p.side = 21;
    return p.twice();
}
```

A class value is a reference. Assigning one to another name copies the reference and the
object is released when the last reference to it dies ([memory.md](memory.md)); everything
a class can carry — inheritance, `virtual`, interfaces, traits, properties, operators,
constructors and destructors — is [classes.md](classes.md).

---

## `enum`

A named set of integer constants with an underlying type, C#'s own: `Color.Red` is the
integer `0`, `Color` is a distinct type from `i64` that no integer converts into without
being told to, and the six comparisons plus `& | ^ ~` are the only operators it computes.
There is no box, no tag and no indirection — a value of an enum type IS its underlying
integer, in a frame slot, a field, an array element, a parameter or a return.

```
[public|internal] enum Name [: underlying] { Member [= const], ... [,] }
```

Members take the previous value plus one, starting at `0`; an explicit value is any
expression that folds to a constant, the same folding a top-level `const` does. The
underlying type is one of eight, `i32` by default (C#'s own `int`):

| written | width | signed |
|---|---|---|
| (nothing), `i32` | 4 | yes |
| `u8`, `u16`, `u32`, `u64` | 1, 2, 4, 8 | no |
| `i8`, `i16`, `i64` | 1, 2, 8 | yes |

```teko
// expect-exit: 42
enum Color { Red, Green, Blue }
enum Level : u8 { Low = 10, Mid, High }          // 10, 11, 12
enum Perm { None = 0, Read = 1, Write = 2, All = 3 }

i64 describe(Color c) {
    if (c == Color.Red) return 1;
    return 2;
}

i64 main() {
    Color c = Color.Green;
    if (c == Color.Red) return 1;                // the six comparisons
    if (describe(c) != 2) return 2;

    if (Level.Mid != (Level) 11) return 3;        // an explicit cast, both ways
    if ((i64) Level.High != 12) return 4;

    Perm p = Perm.Read | Perm.Write;              // the bitwise trio, over the same enum
    if (p != Perm.All) return 5;
    if ((p & Perm.Read) == Perm.None) return 6;

    Color tbl[3];                                 // an enum array, field and parameter/
    tbl[0] = Color.Blue;                          // return all carry the same one word
    if (tbl[0] != Color.Blue) return 7;

    Color d = c;
    i64 r = 0;
    switch (d) {                                  // `case Color.Green:` is a qualified
        case Color.Red:   r = 8;  break;           // constant, the same road `case N:` on
        case Color.Green: r = 42; break;           // a top-level `const` already takes
        default:          r = 9;  break;
    }
    return r;
}
```

Two explicit conversions, a plain machine cast, unchecked: `(i64) c` (an enum to its
underlying type or any other integer) and `(Color) n` (any integer to an enum). Nothing
else converts — not a bare integer, not the literal `0`, not a different enum, not `null` —
implicitly, in either direction:

```teko
// no-run
enum Color { Red, Green }

i64 main() {
    Color c = Color.Red;
    i64 n = c;                    // teko: a value of type Color does not convert to i64
    Color d = 0;                  // teko: a value of type i64 does not convert to Color
    return n;
}
```

```teko
// no-run
enum Color { Red, Green }
enum Size  { Small, Big }

i64 main() {
    Color c = Color.Red;
    if (c == Size.Small) return 1;   // teko: no operator `==` takes these operands
    Color e = c + Color.Green;       // teko: no operator `+` takes these operands
    return 0;
}
```

An `enum` is a type declared at the top level, exactly as a `struct`/`class` is — there is
no `enum` inside a class or struct body, and `enum Empty { }` is refused: it declares at
least one member. [diagnostics.md](diagnostics.md) has every one of these messages in full.

---

## `TimeSpan`

A length of time as a signed 64-bit count of 100-nanosecond ticks, C#'s own, behind one
include:

```
#include "time.tk"
```

It is a **primitive with members**: eight bytes like an `i64`, a distinct type no integer
converts into, and a table of members — `.Ticks`, `.Days`, `.TotalHours`,
`TimeSpan.FromHours(x)`, `new TimeSpan(ticks)` — that lower to ordinary functions of
[`lib/time.tk`](runtime.md#the-time-library). `+ - * /`, the unary minus and the six comparisons
are calls with an overflow check; every other operator is refused.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    TimeSpan day = TimeSpan.FromDays(1.0);
    TimeSpan two = TimeSpan.FromHours(2.0);
    TimeSpan both = day + two;
    if (both.Ticks != 936000000000) return 1;
    if (both.Hours != 2) return 2;               // the component, 0..23
    if (both.TotalHours != 26.0) return 3;       // ...and the whole span
    if (both <= day) return 4;
    return 42;
}
```

[timespan.md](timespan.md) is the whole type: every member, every operator, every refusal.

---

## `DateTime`

A point in time behind the same include: a tick count from `0001-01-01 00:00:00` with a
`Kind` (`Unspecified`, `Utc`, `Local`) in the two bits above it, C#'s own packing. It is a
primitive with members like `TimeSpan`, and the two share every mechanism — but **the raw
bits are not the ticks**, so `.Ticks` is a call, a hand-written `(i64) d` is refused, and
every comparison masks the `Kind` off.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    DateTime leap = new DateTime(2024, 2, 29);
    if (leap.Year != 2024 || leap.Day != 29) return 1;
    if (leap.DayOfWeek != 4) return 2;           // a Thursday, 0 is Sunday
    DateTime next = leap.AddDays(1);
    if (next.Month != 3 || next.Day != 1) return 3;
    if ((next - leap).TotalDays != 1.0) return 4;
    if (new DateTime(2024, 1, 31).AddMonths(1).Day != 29) return 5;   // C# clamps
    if (DateTime.IsLeapYear(1900) != 0) return 6;
    if (leap.Kind != DateTimeKind.Unspecified) return 7;
    return 42;
}
```

`DateTimeKind` is an alias of `i32` with three values. A date that does not exist panics
where it is built (`new DateTime(2023, 2, 29)` is `teko: a date does not exist`, exit 70),
and `DateTime.Now` is refused by name until `mc`'s `<sys>` carries a wall clock.
[datetime.md](datetime.md) is the whole type.

---

## Members

The same member grammar serves a struct and a class.

| member | written |
|---|---|
| field | `public i64 side;` |
| method | `public i64 area() { ... }` |
| constructor | `public Name(i64 x) { ... }` (a class only) |
| destructor | `~Name() { ... }` (a class only) |
| property | `public i64 Side { get; set; }` |
| constant | `public const i64 MAX = 4;` |
| operator | `public static Vec operator+(Vec a, Vec b)` |

### Modifiers

Modifiers come before the type, in any order, each at most once.

| modifier | on | means |
|---|---|---|
| `public` | member, top-level type | reachable from anywhere |
| `private` | member | the declaring type only — the **default** for a member |
| `protected` | member | the declaring type and the types derived from it |
| `internal` | top-level type | this project only — the **default** for a type |
| `static` | member | no receiver; reached through the type |
| `const` | member | a compile-time constant; no slot in the object |
| `virtual` `override` `abstract` | method, property | the vtable slot ([classes.md](classes.md)) |

`public` and `internal` are reserved words, because they open a top-level declaration.
`private`, `protected`, `static`, `virtual`, `override`, `abstract` and `operator` are
**contextual**: they mean something inside a type body and are ordinary names outside it.

`internal` names the **project**: a declaration belongs to it when the file it was read
from lives inside the directory of the build config. An absolute path, a path that climbs
out (`../elsewhere.tk`) and a bundled `#include <name>` are all outside.

### `static`

A static field is one global named `Type_field`, so it takes no byte of the object; a
static method takes no receiver. Both are reached through the type, never through an
instance.

```teko
// expect-exit: 42
#include "rt.tk"

struct Point {
    public i64 x;

    public static i64 made;

    public static i64 tally() {
        return made;                             // the bare name, no receiver
    }
}

i64 main() {
    if (POINT_SIZE != 8) return 1;               // the static field is not in the object
    if (Point.made != 0) return 2;
    Point.made = 41;
    Point.made = Point.made + 1;
    return Point.tally();
}
```

### `const`

A member `const` has no slot either: it is a folded constant, read as `Type.MAX` from
outside and by its bare name inside. A top-level `const` is the same thing without an
owner, and it is what an array size or a `const` generic argument may name.

```teko
// expect-exit: 42
#include "rt.tk"

const i64 BASE = 30;
const i64 SIZE = 4;

i64 arr[SIZE];

class Counter {
    public const i64 MAX = 4;
    private const i64 STEP = 2;

    public i64 sum() {
        return MAX + STEP;                       // bare, from inside
    }
}

i64 main() {
    if (BASE != 30) return 1;
    arr[3] = 4;                                  // `SIZE` sized the global array
    if (arr[3] != 4) return 2;
    if (Counter.MAX != 4) return 3;              // through the type, from outside
    Counter c = new Counter;
    return BASE + c.sum() + arr[3] + 2;          // 30 + 6 + 4 + 2
}
```

There is no **local** `const`: declare it at the top or as a member.

---

## Limits

| limit | value |
|---|---|
| there is no type inside a type | nested classes and structs are refused |
| a struct is never a base class | `: Base` names a class |
| a struct has no reference count | its allocation lives for the run |
| `str` has no length field | `tk_str_len` walks to the NUL |
| `ptr` and `uptr` are the same type | an overload cannot tell them apart |
| types declared in one source | 32 — a `T[]` and a `T?` take one row each, made the first time each is spelled |
| distinct value types boxed by `T?` in one source | 32 |
| fields, summed across all types | 256 |

What a v0.4.0 program cannot write at all, and the message it gets, is
[not-yet.md](not-yet.md).
