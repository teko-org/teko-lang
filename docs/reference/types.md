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

Two more are sixteen bytes, behind `#include "wide.tk"` — the widest integers teko has,
and primitives of teko's own rather than anything `mc` brings
([below](#i128-and-u128), N6a):

| word | width | signed | notes |
|---|---|---|---|
| `i128` | 16 | **yes** | C#'s `Int128` under `mc`'s own word; `−2^127 .. 2^127−1`, literal suffix `i` |
| `u128` | 16 | no | C#'s `UInt128`; `0 .. 2^128−1`, literal suffix `u` |

A type word is **reserved program-wide**: `i64 str = 1;` is refused, exactly as C#
refuses a variable named `int`.

A **member** name is the one seat that reservation does not reach: `p.str`, `Box.ref`, a
field `public i64 ref;`, a method `i64 params() { ... }`, a property, an enum member, all
read the word after a `.` (or declare it) rather than as a bare name — exactly as C# lets a
member share a name with a contextual keyword (`value`, `where`...). A type name, a local
and a parameter stay refused: only the member seat has no shadow to guard against.

```teko
// expect-exit: 42
#include "rt.tk"

isize sum_isize(isize a, i64 b) {
    return a + b;
}

class Box {
    public i64 ref;
    public i64 params() { return this.ref; }
}

i64 main() {
    byte  b = 20;
    char  c = 12;
    usize u = (usize) b + (usize) c;
    isize n = sum_isize(10, 0);
    Box box = new Box();
    box.ref = 10;
    return (i64) u + (i64) n + box.params() - 10;
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

### Integer division

D82: `/` and `%` panic rather than answer whatever the machine's own `sdiv`/`idiv` gave
(the two ISAs used to disagree — see [diagnostics.md](diagnostics.md#integer-division)).
A **literal** nonzero divisor (`x / 2`) carries no guard: the core owns it exactly as it
did before this crumb, and `--dump-ast` is unmoved.

| expression | is |
|---|---|
| `a / b`, `a % b`, `b` a non-literal | the eight core widths; `b == 0` is `teko: division by zero`, exit 70 (a literal `0` divisor is refused at compile time: `teko: division by zero` at the line when the dividend is not constant, mc's own `division by zero` when both sides are; `i128`/`u128` `/` and `%` are `lib/wide.tk`'s own guard, D81/D83: the same two messages at run time, and no compile-time answer for a literal `0`) |
| `a / b`, `a % b`, `b` a non-literal, signed only | `b == -1 && a == <the type's own MinValue>` is `teko: an integer division overflowed`, exit 70 — for `%` too, as .NET throws `OverflowException` for `int.MinValue % -1` on both its ISAs (the C# specification ties the remainder's overflow to the quotient's) |

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
(the section above; `f32` widens to `f64` the same way, C#'s own direction
[below](#f32-and-f64)); nothing converts back without a cast, and a float, `null`, or a
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
of a `params f64[]`, a field store, an array ELEMENT store (a `T[]` of heap and a fixed
array alike), a GLOBAL slot's own initializer and assignment, and a binary mixing the two — where the integer
operand is converted whichever side it stands on, so `1 + 2.5` and `2.5 + 1` are both
three point five. The conversion is a cast the compiler writes for you, and it rounds the
way C#'s `long` to `double` does: an `f64` carries 53 bits of precision, so every integer up
to 2^53 in magnitude arrives exact and a larger one lands on the nearest representable
double.

**`f32` converts to `f64` the same way, and `f64` does not convert back either** (D78, C#
§10.2.3 and CS0664) — the SAME nine slots, and a mixed `f32`/`f64` binary promotes the
`f32` side whichever it stands on, exactly as an integer does above. `f32 b = 2.5f; f64 e
= b;` widens the four bytes of `2.5f` into the eight bytes of `2.5`, an ordinary `fcvt`,
never a raw copy of the narrower value's bits into the wider slot's low half. The other
direction is the same refusal an integer's narrowing gets: `f32 c = 2.5;` is `teko: a
value of type f64 does not convert to f32` — write `2.5f` (the literal's own width) or
`(f32) 2.5` (an explicit, truncating cast) to mean it.

```teko
// expect-exit: 42
i64 main() {
    f32 b = 2.5f;
    f64 e = b;                                   // widens: an ordinary cast, not raw bits
    if (e != 2.5) return 1;
    if (b + 1.0 != 3.5) return 2;                // the narrower side promotes
    return 42;
}
```

```teko
// expect-exit: 42
#include "rt.tk"

f64 twice(f64 x) { return x + x; }

i64 main() {
    f64 y = 1;                                   // an initializer
    y = 5;                                       // an assignment
    f64 fa[2];
    fa[0] = 1;                                   // an array element
    if (twice(3) != 6.0) return 1;               // an argument
    if (1 + 2.5 != 3.5) return 2;                // the integer on the left
    if (2.5 + 1 != 3.5) return 3;                // ...and on the right
    if (fa[0] != 1.0) return 4;                  // ...and it landed as a float
    return (i64) (y + 37);
}
```

**A global slot converts exactly as a local does.** The slot at the top of a file is judged
by the same rules and refused with the same words as the one inside a body — the widening
above, the narrowing below, `null` outside a `T?`, a reference in a numeric slot, an `enum`
from a bare integer and a class that fits no row. So `f64 g = 1;` at file scope holds one
point zero, and `g = 5;` written from inside any body holds five point zero.

```teko
// expect-exit: 42
const i64 K = 2;
f64 g  = 1;                                      // an initializer, at file scope
f64 g2 = K;                                      // a `const` name folds to the same literal
f32 g3 = 1;                                      // and `f32` rounds to its own width

i64 main() {
    g = 5;                                       // an assignment to a global
    if (g2 != 2.0) return 1;
    if (g3 != 1.0f) return 2;
    return (i64) (g + 37);
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
carries. A **raw `uptr`/`ptr`** slot is the one exception, local, field or global: `0` is an ordinary
value of a raw pointer, the type `null` already carries, so `uptr p = null;` is accepted and
no `?` is written on it.

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

**A field store is one rule, at every site that writes one.** `p.f = e`, `this.f = e`, the
implicit `f = e` inside a method or a constructor, a `static` field `T.f = e` (through its
type, whether or not the type is already known where the store is written) and an ELEMENT
of a `T[]`, `xs[i] = e`, all pass through the same check: the widening above, the narrowing
refusal, `null` only in a `T?` field ([nullable.md](nullable.md)) with the raw `uptr`/`ptr`
slot above excepted, an `enum` field converting from nothing but itself, and the
reference/number mismatch (D34), in both directions.

```teko
// no-run
class Foo { public i64 v; }
enum Color { Red, Green, Blue }

class H {
    public i64 n;
    public Color c;
    public static Foo f;

    public H(f64 x) {
        this.n = x;                              // teko: a value of type f64 does not convert to i64
    }

    public void set(i64 k) {
        c = k;                                    // teko: a value of type i64 does not convert to Color
    }
}

i64 main() {
    H.f = 5;                                      // teko: a value of type i64 does not convert to Foo
    return 0;
}
```

The rule does not weaken where the site cannot type the value — a parameter, an implicit
`f = e` the pass rewrites, a `static` field on a type declared below, a call to an
overloaded name, a user operator. That store waits for the pass and is judged there, under
the same rule and at its own line; a value that has no type even then is refused rather
than written raw
([diagnostics.md](diagnostics.md#a-field-store-whose-value-only-the-pass-can-type)).

The two float widths convert one way: `f32` widens to `f64` implicitly, `f64` narrows to
`f32` only through a cast (D78, [above](#f32-and-f64)) — the same field store this
paragraph is about takes an `f32` value into an `f64` field, and refuses the width the
other way round, exactly as an `i64` field does an out-of-range integer.

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
held. Reading or writing a field through such a local is **refused where it is read**
(`teko: p is used before it is assigned`, D46): `p.x = 4` reads `p` to reach the field, and
a local declared without an initializer and with no assignment anywhere earlier in the body
does not have one to read. Build it (`Name p = new Name;`), or assign it before you touch
it — including inside a branch, which the rule counts on purpose
([nullable.md](nullable.md) § Definite assignment). The refusal is what D42 deferred to the
nullable design; it did not exist before, and the line reached the run instead.

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

A struct **global** is the same eight-byte slot a class global is: it converts by the same
assignment corridor a local's own store takes (D53), and assigning one struct global (or a
struct-typed local) to another copies the POINTER, not the fields — `gp = p;` makes `gp` and
`p` the same allocation, so a field written through either name is read through the other
(D5, D56). It cannot be built at its own declaration: `Point gp = new Point();` at file scope
is refused by mc's own core, `global initializer must be constant` — `new` is a call, not a
constant. Declare it bare (`Point gp;`) and build it in a body instead.

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

**The allocator's own local.** `new Name(...)` lowers to a generated `Name_new` function
that allocates the object, installs its vtable and reference count, and calls the
constructor with the allocated address as its first argument. That address is held in a
local of the allocator's own — gensym'd (`gensym_new()`, the `$`-prefixed convention the
compiler's temporaries use wherever a user-chosen name can share the scope), never a fixed
spelling such as `p`, so a constructor parameter written with that same ordinary identifier
reads its own argument and never the allocator's local. (Four other generators still declare
a fixed `p` — a delegate's, a heap array's, a struct's allocator, a DI getter — in functions
whose every parameter is the compiler's own, where no collision is possible; D87.)

**`this` is a value of the enclosing class** (D102). `return this;` is what fluent chaining
is made of, and `C d = this;`, `f(this)`, `b ? this : o`, a property accessor's `return
this;`, a return typed as a BASE class, and a return typed as an INTERFACE the class
implements all read as the class. An interface's own default body sees `this` as the
interface. The receiver is a **borrowed** reference — a parameter — so returning it raises
the count for the caller and nothing leaks; a discarded link of a chain is released where it
is discarded ([memory.md](memory.md)).

```teko
// expect-exit: 42
#include "../../lib/rt.tk"
interface Counter { i64 Value(); Counter Me() { return this; } }
class Acc : Counter {
    public i64 v;
    public Acc() { v = 0; }
    public i64 Value() { return v; }
    public Acc Bump(i64 x) { v = v + x; return this; }
    public Counter AsI() { return this; }
}
i64 main() {
    Acc a = new Acc();
    a.Bump(1).Bump(2).Bump(3);                   // chained, the result discarded
    Counter c = a.AsI();
    return c.Value() + 36;                       // 6 + 36
}
```

Three limits. `this = e;` is refused — ``teko: `this` is read-only`` — because the receiver
is a parameter, not a slot; assign to a field instead. The compound spellings of the same
write (`this += e`, `this++`) earn the same sentence. `this == o` is a comparison of two
class references and gets the rule every class reference gets, ``teko: C declares no
operator `==` ``. And `this` inside a `struct` body is not a value
([not-yet.md](not-yet.md)), because a `struct` has no copy at assignment yet.

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

### `ToString`, `Parse`, `TryParse`, `IsDefined`

Four members, dispatched by name and built lazily — the two globals a text-using enum needs
(`Color__names`, `Color__vals`) are written the first time one of these four is spelled on
it, never at the `enum` itself, so an enum a program never asks text of costs nothing. They
lower to [`lib/rt.tk`](runtime.md), behind the include a program that asks an enum for
text has to carry (the one most fixtures already do):

```
#include "rt.tk"
```

```teko
// expect-exit: 42
#include "rt.tk"

enum Color { Red, Green, Blue }
enum Perm  { None = 0, Read = 1, Write = 2, All = 3, Alias = 3 }   // Alias aliases All

i64 main() {
    if (!tk_str_eq(Color.Red.ToString(), "Red")) return 1;
    if (!tk_str_eq(Perm.Alias.ToString(), "All")) return 2;        // the FIRST name of an
                                                                     // aliased value, C#'s rule
    Color c = (Color) 9;                                            // outside the declared set
    if (!tk_str_eq(c.ToString(), "9")) return 3;                    // the decimal digits

    if (Color.Parse("Green") != Color.Green) return 4;
    if (Color.Parse("Blue") != Color.Blue) return 5;

    Color out1;
    if (Color.TryParse("Red", out out1) != 1) return 6;
    if (out1 != Color.Red) return 7;
    Color out2 = Color.Green;
    if (Color.TryParse("Nope", out out2) != 0) return 8;
    if (out2 != Color.Green) return 9;                              // untouched on failure

    if (!Color.IsDefined(1)) return 10;
    if (Color.IsDefined(9)) return 11;

    return 42;
}
```

`Color.Parse(s)` panics (exit 70) on a name no member spells — C#'s own `FormatException`
road, teko's own answer since this language has no exceptions:

```teko
// expect-exit: 70
#include "rt.tk"

enum Color { Red, Green, Blue }

i64 main() {
    Color c = Color.Parse("Nope");
    return (i64) c;
}
```

`CompareTo`/`Equals`, `GetNames`/`GetValues` and `[Flags]`-style formatting are not taught
yet ([not-yet.md](not-yet.md)). [diagnostics.md](diagnostics.md#enums) has every message in
full.

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

`DateTimeKind` is an [`enum`](#enum) with three values, declared in `lib/time.tk`, so
`d.Kind` answers it and no bare integer lands in it. A date that does not exist panics
where it is built (`new DateTime(2023, 2, 29)` is `teko: a date does not exist`, exit 70).

`DateTime.UtcNow` reads the host wall clock (C6, D90): `clock_gettime` on Linux and macOS,
`GetSystemTimePreciseAsFileTime` on Windows, teko's own `extern` per target -- never an `mc`
hook. `DateTime.Now` reads the SAME instant and carries `DateTimeKind.Local`, C#'s own
spelling, but teko has no time-zone database, so its value does not actually convert to a
local time; that divergence from C# is documented, not hidden. `DateTime.Today` is `Now`
truncated to midnight, the same `Kind`. [datetime.md](datetime.md) is the whole type.

---

## `DateOnly`

A date with no time of day, behind the same include: the count of days since `0001-01-01`,
**four bytes** — C#'s own `int` representation, and the first primitive here narrower than
the machine word. The raw four bytes are `.DayNumber`.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    DateOnly leap = new DateOnly(2024, 2, 29);
    if (leap.DayNumber != 738944) return 1;
    if (leap.DayOfWeek != 4) return 2;              // a Thursday, 0 is Sunday
    if (leap.AddMonths(1).Day != 29) return 3;      // 2024-03-29
    if (new DateOnly(2024, 1, 31).AddMonths(1).Day != 29) return 4;   // C# clamps
    if (DateOnly.FromDayNumber(leap.DayNumber) != leap) return 5;
    return 42;
}
```

C# declares **no arithmetic operator** on it and neither does teko: `d + t` and `d - d` are
refused, and `d1.DayNumber - d2.DayNumber` is the form for a difference. The six
comparisons are all it carries.
[datetime.md § `DateOnly`](datetime.md#dateonly) is the whole type.

---

## `TimeOnly`

A time of day with no date, behind the same include: the count of ticks since midnight,
`0 .. 863999999999`, **eight bytes** — so, unlike `DateOnly`, every slot it travels needs
none of the compiler's own sign-extend/narrow pair.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    TimeOnly t = new TimeOnly(13, 45, 30);
    if (t.Hour != 13) return 1;
    if (t.AddHours(11).Hour != 0) return 2;         // wraps at midnight, never panics
    TimeSpan since = t - new TimeOnly(12, 45, 30);
    if (since.TotalHours != 1.0) return 3;
    return 42;
}
```

C# declares **one** arithmetic operator, `t - t`, and teko's answer is never negative: it
wraps forward across midnight when the left side is earlier. `t + t` is refused, and
`.Add(ts)`/`.AddHours(f)`/`.AddMinutes(f)` are C#'s own wrapping form for advancing one.
[datetime.md § `TimeOnly`](datetime.md#timeonly) is the whole type, including
`DateOnly.ToDateTime(TimeOnly)`, the member N4a left out because this type did not exist
yet.

---

## `decimal`

Sixteen bytes, sixteen-byte aligned, behind `#include "decimal.tk"`. It is C#'s type, and
it exists because `0.1 + 0.2 == 0.3` is false in an `f64` and true in a `decimal`. It
moves, it computes, it converts, it rounds and it is written and read back as text (C3,
C4 and C5, [the specification](../specs/decimal.md) § 12).

```teko
// expect-exit: 42
#include "rt.tk"
#include "decimal.tk"

decimal rate;

decimal echo(decimal d) { return d; }

i64 main() {
    decimal price = 19.99m;                       // C#'s suffix, `M` too
    rate = 0.07m;
    decimal copy = echo(price);                   // a parameter, and a return
    decimal cells[2];
    cells[1] = copy;                              // an element, sixteen bytes of it
    decimal back = cells[1];
    ptr p = &back;                                // `&` on a local of decimal type
    if (ld64(p) != 1999) return 1;                // the low 64 bits of the mantissa
    if (((ld64(p + 8) >> 32) & 0xff) != 2) return 2;   // the scale
    if (ld64(&rate) != 7) return 3;               // `&` reaches a global's sixteen bytes too
    if (0.1m + 0.2m != 0.3m) return 4;            // exact in base ten; an `f64` fails this
    if (price + price * rate != 21.3893m) return 5;
    if (1m / 3m * 3m == 1m) return 6;             // 0.9999999999999999999999999999m
    decimal n = 5;                                // an integer converts, C# §10.2.3
    if (n / 2m != 2.5m) return 7;
    if ((i64) 2.7m != 2) return 8;                // explicit, truncating toward zero
    return 42;
}
```

**The operators** are C#'s, and every one of them is a call into `lib/decimal.tk`:

| written | answers | rounding, and how it fails |
|---|---|---|
| `a + b`, `a - b` | `decimal` at `max(sa, sb)` | exact; `teko: decimal overflow`, exit 70 |
| `a * b` | `decimal` at `sa + sb` | reduced half away from zero past 28 places; the same overflow |
| `a / b` | `decimal`, up to 28 places | half away from zero; `teko: decimal division by zero`, exit 70 |
| `a % b` | `decimal` at `max(sa, sb)`, the sign of the DIVIDEND | exact; the same division panic |
| `-a` | `decimal` | one bit, flipped |
| `+a` | `decimal` | the operand itself, with no call at all |
| `==` `!=` `<` `<=` `>` `>=` | `i64` 0/1 | the scales align first, so `1.0m == 1.00m` |
| `a + 1`, `1 + a` | `decimal` | the integer converts first, then the row above |

`+ - *` are exact whenever the result fits 96 bits at the scale the operands imply; only a
result past 28 decimal places rounds, and it rounds **half away from zero**, which is what
`.NET`'s own `DecCalc` does for the operators. `1m / 3m * 3m` is therefore
`0.9999999999999999999999999999m` and not `1m` — the same answer C# gives.
`<<`, `>>`, `&`, `|`, `^` and `~` take no `decimal`, in C# or here.

**The conversions** (C# §10.2.3 and §10.3), none of them an instruction:

| from | to | how |
|---|---|---|
| any integer (`u8`..`i64`, `i32`) | `decimal` | **implicit**, in every one of D33's nine slots |
| `decimal` | any integer | **explicit** `(i64) d`, truncating toward zero |
| `f32`, `f64` | `decimal` | **explicit** `(decimal) x`, the double's own value rounded to 15 significant digits |
| `decimal` | `f64`, `f32` | **explicit** `(f64) d`, may lose precision |
| `decimal` | `str` | `.ToString()`, never implicit; `decimal.Parse(s)` is the way back |
| `null`, a class, a struct, a `T[]` | `decimal` | refused |

**The API** is C#'s, and every row is an ordinary call into `lib/decimal.tk`:

| static | answers | |
|---|---|---|
| `decimal.Zero`, `One`, `MinusOne` | `decimal` | 0, 1, -1 at scale 0 |
| `decimal.MaxValue`, `MinValue` | `decimal` | `±79228162514264337593543950335m`, the full 96-bit mantissa |
| `decimal.Round(d)`, `Round(d, places)` | `decimal` | **half to even**, C#'s `MidpointRounding.ToEven`; the scale never grows, so `Round(1.5m, 3)` is `1.5m` at scale 1 |
| `decimal.Truncate(d)`, `Floor(d)`, `Ceiling(d)` | `decimal` at scale 0 | toward zero, toward `-∞`, toward `+∞`: `Floor(-1.5m)` is `-2m` and `Ceiling(-1.5m)` is `-1m` |
| `decimal.Abs(d)` | `decimal` | the sign bit cleared; the scale is untouched |
| `decimal.Parse(s)` | `decimal` | `teko: the string is not a decimal` or `teko: decimal overflow`, exit 70 |
| `decimal.TryParse(s, out d)` | `i64` 0/1 | never panics; `decimal.Zero` on failure |

| instance | answers | |
|---|---|---|
| `d.ToString()` | `str` | the shortest exact form, trailing zeros KEPT: `1.50m` writes `"1.50"` |
| `d.ToString(places)` | `str` | C#'s `"F<n>"`: rounded to `places`, then padded with zeros |
| `d.CompareTo(e)` | `i64` `-1/0/1` | the VALUE, whatever the scales |
| `d.Equals(e)` | `i64` 0/1 | the same question `==` asks |
| `d.Scale` | `i64` | `0..28`, the raw byte of the layout |
| `d.Sign` | `i64` `-1/0/1` | the VALUE's, so `-0m` answers 0 |

A `places` outside `0..28` is `teko: the decimal places are out of range`, exit 70 —
`ToString(places)` shares it, because it rounds first. `Math.Round`, `Truncate`, `Floor`,
`Ceiling` and `Abs` are C#'s other spelling of five of them, an ordinary class of static
methods behind `#include "math.tk"`.

**The text** is exact in both directions. `ToString` places the point by the scale and
keeps trailing zeros, because `1.50m` and `1.5m` are two values; a zero never carries a
sign, so `-0m` writes `"0"` while `0.00m` writes `"0.00"`. `Parse` accepts exactly what
`ToString` writes plus an optional exponent — `[+|-] digits [ . digits ] [ (e|E) [+|-]
digits ]`, with `.5` and `5.` accepted as C# accepts them — and **nothing else**: no
surrounding space, no thousands separator, no culture and no currency
([decimal.md § 9](../specs/decimal.md)). A value it can read but not hold — a mantissa past
96 bits, or a scale past 28 places once the exponent moved it — is `teko: decimal
overflow`, because this type is exact or loud and never rounds a number the writer wrote
out in full.

```teko
// expect-exit: 42
#include "rt.tk"
#include "decimal.tk"

i64 main() {
    if (decimal.Round(2.5m) != 2m) return 1;           // half to EVEN, not away from zero
    if (decimal.Round(3.5m) != 4m) return 2;
    if (decimal.Round(2.675m, 2) != 2.68m) return 3;   // 2.675 is exact here, so it ties
    if (decimal.Floor(-1.5m) != -2m) return 4;
    if (decimal.Ceiling(-1.5m) != -1m) return 5;
    if (!tk_str_eq(1.50m.ToString(), "1.50")) return 6;
    if (!tk_str_eq(1.5m.ToString(3), "1.500")) return 7;
    if (decimal.Parse("1.5E-2") != 0.015m) return 8;
    decimal v = 0m;
    if (decimal.TryParse("x", out v)) return 9;
    if (v != decimal.Zero) return 10;
    if (1.0m.CompareTo(1.00m) != 0) return 11;
    if (0.05m.Scale != 2) return 12;
    return 42;
}
```

**The literal** is `<digits>[.<digits>][e[+|-]<digits>]m`, case-insensitive in the suffix:
`1m`, `0.1m`, `19.99M`, `1.5e3m`, `15e-2m`. It is refused where it is written when the
mantissa needs more than 96 bits (`teko: decimal literal out of range`) or the scale passes
28 (`teko: a decimal carries at most 28 decimal places`). The **unary minus** is an
operator and not part of the literal, so `-3.25m` is `tk_dec_neg(3.25m)` — which is why a
`decimal` GLOBAL is a slot and an assignment and never an initializer: a call is not a
constant, and neither is the literal itself (it is the name of its own blob global).

**The layout**, which `&d` reads back with two `ld64`s:

| offset | bits | holds |
|---|---|---|
| `+0` | 0..63 | the low 64 bits of the 96-bit mantissa |
| `+8` | 0..31 | the high 32 bits of the mantissa |
| `+8` | 32..39 | the scale, `0..28` |
| `+8` | 40..62 | zero |
| `+8` | 63 | the sign, `1` negative |

The value is `(-1)^sign * mantissa / 10^scale`, so `0m`, `0.00m` and `-0m` are three
distinct bit patterns that are all equal — C#'s rule, and the reason equality will be a call
and never a `cmp`.

**It moves by address.** A local, a global, a parameter, a field and an array element are
all sixteen bytes copied two words at a time; an argument travels as the ADDRESS of the
value's slot, placed by the underlying machine's own ABI exactly as it places any `uptr`;
a return goes through one buffer the program declares (`tk_dec_retbuf`, in `decimal.tk`) and
is copied out at the call site before anything else runs, which is what makes a recursive
`decimal` function safe. Three derived machines do it — `arm64`, `x86_64`, `x86_64-win` —
and none of them adds an instruction the base machine did not already encode
([`teko_wide.tk`](../../teko_wide.tk), D74).

**It is a primitive and not a `struct`**, because a teko `struct` value is a pointer to an
allocation (§ `struct` above): a `decimal` field would be a reference to sixteen bytes on the
arena, copied by aliasing rather than by value, and every `decimal` in a loop would be a
block to reclaim. C# gives it value semantics and no heap.

**What is still refused**, every one of them by name and at the line it was written:

| written | message |
|---|---|
| `d << 1`, `d & d`, `~d` | ``teko: no operator `<<` takes these operands`` |
| `i64 n = d;`, `f64 x = d;` | `teko: a value of type decimal does not convert to i64` — the cast is the written form |
| `decimal d = 1.5;` | `teko: a value of type f64 does not convert to decimal` — `(decimal) 1.5` is the written form |
| `(str) d` | `` teko: a decimal does not cast; `.ToString()` writes it and `decimal.Parse(s)` reads it `` — text is `ToString`/`Parse` (C5, D79) |
| `decimal g = 5;` at file scope | `teko: a global decimal takes no initializer` |
| `d.Anything`, `decimal.Anything` | `teko: unknown member of decimal` and its static twin |
| `const decimal R = 1m;` | `teko: const requires a constant expression` |
| `case 1m:` | `teko: a case label must be a constant expression` |
| `extern i64 f(decimal d);` | ``teko: an `extern` takes no decimal`` |

`ref decimal` and `out decimal` are NOT in that list any more: N3 opened the wide pointee
road for every `TK_WIDE` type at once (D75, [`Guid`](#guid) below), so a `decimal` reached
through a `ref`/`out` parameter moves all sixteen bytes.

---

## `Guid`

Sixteen bytes, sixteen-byte aligned, behind `#include "guid.tk"`. It is C#'s `System.Guid`:
a 128-bit identifier written `f81d4fae-7dec-11d0-a765-00a0c91e6bf6`, compared and ordered by
value, with an all-zero `Guid.Empty`. It is the SECOND `TK_WIDE` type and the first that is
not `decimal`, so it moves on exactly the machine that one brought
([`teko_wide.tk`](../../teko_wide.tk), D74/D75) — read "It moves by address" above, word for
word.

```teko
// expect-exit: 42
#include "rt.tk"
#include "guid.tk"

i64 main() {
    Guid a = Guid.Parse("f81d4fae-7dec-11d0-a765-00a0c91e6bf6");
    Guid b = Guid.Parse("F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6");   // case-insensitive
    if (a != b) return 1;
    if (a == Guid.Empty) return 2;

    str s = a.ToString();                            // the "D" form, lowercase
    if (Guid.Parse(s) != a) return 3;
    if (!tk_str_eq(a.ToString("N"), "f81d4fae7dec11d0a76500a0c91e6bf6")) return 4;

    Guid z = Guid.Empty;
    if (!z.IsEmpty) return 5;
    if (z >= a) return 6;                            // the bytes, left to right

    Guid v = a;
    if (Guid.TryParse("nope", out v) != 0) return 7;
    if (!v.IsEmpty) return 8;                        // Empty written on failure
    return 42;
}
```

**The layout** is RFC 4122's own, **in text order**, big-endian, byte 0 first — which makes
`ToString` a straight walk from byte 0 to byte 15 and `Parse` its inverse.

| offset | holds | prints as |
|---|---|---|
| `+0 .. +3` | `time_low` | the first group, 8 hex digits |
| `+4 .. +5` | `time_mid` | the second group, 4 |
| `+6 .. +7` | `time_hi_and_version` | the third group, 4 |
| `+8 .. +9` | `clock_seq` | the fourth group, 4 |
| `+10 .. +15` | `node` | the fifth group, 12 |

**The ordering diverges from C#, deliberately.** `<`, `<=`, `>`, `>=` and `CompareTo` walk
the sixteen bytes **unsigned, left to right**, which is the order the text sorts in. C#
compares its first field as a signed `int` and the next two as signed `short`s — an order
almost nobody intends and SQL Server famously disagrees with. Matching it would mean
matching C#'s in-memory byte swap too, and then `ToString` would stop being a straight walk
([the specification](../specs/guid.md) § 1, § 6).

**The API.**

| static | instance |
|---|---|
| `Guid.Empty` | `.ToString()` — the `"D"` form, lowercase, 36 characters |
| `Guid.Parse(str)` — `"D"` or `"N"`, either case | `.ToString(str fmt)` — `"D"` or `"N"` |
| `Guid.TryParse(str, out Guid)` — `1`/`0`, `Empty` on failure | `.CompareTo(Guid)`, `.Equals(Guid)` |
| `Guid.NewGuid()` | `.IsEmpty` — an `i64` `0`/`1` |

`.IsEmpty` is not a C# member — C# writes `g == Guid.Empty`, which works here too. It is the
one addition this type makes, and it is additive.

`Guid.Parse` panics (exit 70, `teko: the string is not a Guid`) on anything that is neither
the 36-character hyphenated form nor the 32-character bare one. `ToString` takes `"D"` and
`"N"` and panics on any other format (`teko: the Guid format is not taught`); `"B"`, `"P"`
and `"X"` are three more spellings of the same sixteen bytes and are not taught.

`Guid.NewGuid()` reads the host's own cryptographic randomness (N9, D91): `getrandom` on
Linux, `getentropy` on macOS, `BCryptGenRandom` on Windows — teko's own `extern` per target,
never an `mc` hook, the same mechanism C6 (D90) gave the wall clock. The version-4 layout
(RFC 4122): the version nibble, the top nibble of `time_hi_and_version` (`+6`), is always
`4`; the variant bits, the top two bits of `clock_seq` (`+8`), are always `10` — so
`Guid.NewGuid()` never answers `Guid.Empty`, and two consecutive draws differ with
overwhelming probability. A `Guid` built from a counter, a clock or an address would compile
and two processes would collide, so there is no fallback.

**What `Guid` refuses**, every one of them by name and at the line it was written:

| written | message |
|---|---|
| `a + b`, `~a`, every operator but the six | ``teko: no operator `+` takes these operands`` |
| `i64 n = g;` | `teko: a value of type Guid does not convert to i64` |
| `Guid g = 0;` | `teko: a value of type i64 does not convert to Guid` |
| `(i64) g`, `(Guid) n` | ``teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it`` |
| `g.Anything`, `Guid.Anything` | `teko: unknown member of Guid` and its static twin |
| `const Guid ID = ...;`, `case Guid.Empty:` | `teko: const requires a constant expression` / `teko: a case label must be a constant expression` |
| `extern i64 f(Guid g);` | ``teko: an `extern` takes no Guid`` |

---

## `DateTimeOffset`

Sixteen bytes, sixteen-byte aligned, behind `#include "time.tk"`. It is C#'s
`System.DateTimeOffset`: a UTC instant, `+0`, and its offset in minutes, `+8`. It is the
THIRD `TK_WIDE` type after `decimal` and `Guid`, so it moves on exactly the machine those two
brought ([`teko_wide.tk`](../../teko_wide.tk), D74/D75/D76) — read "It moves by address"
above, word for word.

```teko
// expect-exit: 42
#include "rt.tk"
#include "time.tk"

i64 main() {
    DateTime dt = new DateTime(2024, 2, 29, 13, 45, 30);
    DateTimeOffset o = new DateTimeOffset(dt, TimeSpan.FromHours(0 - 3));
    if (o.UtcDateTime.Hour != 16) return 1;          // 13:45 at -03:00 is 16:45 UTC

    DateTimeOffset p = new DateTimeOffset(o.UtcDateTime, TimeSpan.Zero);
    if (o != p) return 2;                            // the same instant: equal

    if (!tk_str_eq(o.ToString(), "2024-02-29T13:45:30.0000000-03:00")) return 3;
    if (DateTimeOffset.Parse(o.ToString()) != o) return 4;
    return 42;
}
```

**The two halves.** `+0` is a `DateTime` whose `Kind` is always `Utc`; `+8` is the offset in
SIGNED MINUTES, `-840 .. 840` (`-14:00 .. +14:00`), C#'s own bound. `.DateTime` and
`.LocalDateTime` re-apply the offset and hand back an Unspecified `DateTime`; `.UtcDateTime`
hands back the `+0` half as it is. `.Year` … `.Millisecond` read that same re-applied, LOCAL
reading. Two values that name the same instant under different offsets are **equal** and
compare equal — the six comparisons and `CompareTo` read the instant and ignore the offset,
C#'s own rule.

**The API.**

| static | instance |
|---|---|
| `DateTimeOffset.MinValue`, `MaxValue`, `UnixEpoch` | `.Offset` → `TimeSpan`, `.TotalOffsetMinutes` → `i64` |
| `new DateTimeOffset(DateTime, TimeSpan)` | `.UtcDateTime`, `.LocalDateTime`, `.DateTime` → `DateTime` |
| `DateTimeOffset.FromUnixTimeSeconds(i64)`, `FromUnixTimeMilliseconds(i64)` | `.Year` … `.Millisecond`, of the local reading |
| `DateTimeOffset.Parse(str)`, `TryParse(str, out DateTimeOffset)` | `.ToUnixTimeSeconds()`, `.ToUnixTimeMilliseconds()` |
| `DateTimeOffset.Now`, `UtcNow` (C6, D90) | `.ToOffset(TimeSpan)`, `.CompareTo`, `.Equals`, `.ToString()`, `.ToString(str)` |

`DateTimeOffset.UtcNow` reads the same host wall clock `DateTime.UtcNow` does, offset `+0`.
`DateTimeOffset.Now` is the same instant under the same offset -- a recorded divergence from
C#, not a gap: teko has no time-zone database, so there is no local offset to carry.

C#'s `new DateTimeOffset(i64 ticks, TimeSpan)` overload is **not taught**: the one `new` row
this table carries takes a `DateTime`, and `new DateTimeOffset(new DateTime(t), ts)` is the
written form ([not-yet.md](not-yet.md)).

**The text.** `ToString()` and `ToString("o")` write the round-trip form,
`yyyy-MM-ddTHH:mm:ss.fffffff+HH:MM`, 33 characters, the local reading plus its own offset
suffix; `ToString("s")` writes `yyyy-MM-ddTHH:mm:ss`, 19 characters, no fraction and no
offset. `Parse` accepts exactly those two shapes and panics on anything else
(`teko: the string is not a DateTimeOffset`, exit 70); a 19-character string is read as UTC,
offset zero, since teko has no time-zone database to read a bare wall-clock string against.
`TryParse` answers `0`/`1` and writes `MinValue` on failure instead of panicking.

**What N5 refuses**, every one of them by name and at the line it was written:

| written | message |
|---|---|
| `a + b`, `a * b`, the arithmetic C# has none of | ``teko: no operator `+` takes these operands`` |
| `i64 n = o;` | `teko: a value of type DateTimeOffset does not convert to i64` |
| `DateTimeOffset o = 5;` | `teko: a value of type i64 does not convert to DateTimeOffset` |
| `(i64) o`, `(DateTimeOffset) n` | ``teko: a DateTimeOffset does not cast; `.UtcDateTime` reads it and `new DateTimeOffset(...)` builds it`` |
| `o.Anything`, `DateTimeOffset.Anything` | `teko: unknown member of DateTimeOffset` and its static twin |
| `new DateTimeOffset()` | `teko: new DateTimeOffset() is not taught; write DateTimeOffset.MinValue` (D76: a wide type's zero-argument constructor names its own zero instead of the identity cast every eight-byte primitive gets) |
| `new DateTimeOffset(ticks, ts)` on an `i64 ticks` | `teko: a value of type i64 does not convert to DateTime` — the one `new` row takes a `DateTime` first, and the C# ticks overload is not taught |
| `extern i64 f(DateTimeOffset o);` | ``teko: an `extern` takes no DateTimeOffset`` |
| an offset outside `-14:00 .. +14:00`, or not a whole minute | `teko: that UTC offset does not exist`, exit 70 |
| a value whose LOCAL clock (instant + offset) leaves the `DateTime` range (`MaxValue.ToOffset(+01:00)`) | `teko: the local time of that DateTimeOffset is out of range`, exit 70 — refused where the value is built (`tk_dto_make`), as C# does at construction |

---

## `i128` and `u128`

Sixteen bytes each, sixteen-byte aligned, behind `#include "wide.tk"` — one include for the
pair, because the two types are one representation. They are C#'s `Int128` and `UInt128`
under `mc`'s own words (`i128`/`u128`: a type `mc` already has a word for keeps it, D3), and
they exist for the arithmetic that runs out of room in an `i64`
([the specification](../specs/small-ints.md) § 6, N6a).

The layout is little-endian: the low half at `+0`, the high half at `+8`. `i128` and `u128`
are **the same bits** and part company in exactly four places — `/`, the four ordering
comparisons (D81), `%` and `>>` (D83).

```teko
// expect-exit: 42
#include "rt.tk"
#include "wide.tk"

i128 total;

i128 echo(i128 v) { return v; }

i64 main() {
    i128 big = 18446744073709551616i;             // 2^64, which no i64 literal says
    total = big;                                  // a global: a slot and an assignment
    i128 copy = echo(total);                      // a parameter, and a return
    i128 cells[2];
    cells[1] = copy;                              // an element, sixteen bytes of it
    i128 back = cells[1];
    ptr p = &back;                                // `&` on a local of wide type
    if (ld64(p) != 0) return 1;                   // the low half
    if (ld64(p + 8) != 1) return 2;               // ...and the high half
    if (18446744073709551615i * 2i != 36893488147419103230i) return 3;
    if (-7i / 2i != -3i) return 4;                // truncating toward zero
    i128 mx = 170141183460469231731687303715884105727i;
    if (mx + 1i != -mx - 1i) return 5;            // the wrap: MaxValue + 1 is MinValue
    if ((u128)(0i - 1i) != 340282366920938463463374607431768211455u) return 6;
    i128 n = 5;                                   // an integer converts
    if (n + 1 != 6i) return 7;                    // ...and so does one beside a wide value
    return 42;
}
```

**The literal** is `<digits>i` for an `i128` and `<digits>u` for a `u128`, both suffixes
case-insensitive and **decimal digits only** — `0x` is not a wide literal. It is `mc`'s own
`<i128>` spelling; C# has no `Int128` literal at all and writes `Int128.Parse("…")`. The
literal carries the MAGNITUDE only, so the ceiling is 2^127−1 for `i` and 2^128−1 for `u`,
and a value above it is `teko: an i128 literal is out of range` at compile time.
`i128.MinValue` the LITERAL is still written `-170141183460469231731687303715884105727i - 1i`
(the literal grammar carries a magnitude only); `i128.MinValue` the STATIC (below) reads the
same value a shorter way, a call rather than a literal (D85).

**The operators** are C#'s, and every one of them is a call into `lib/wide.tk`:

| written | answers | how it fails |
|---|---|---|
| `a + b`, `a - b`, `a * b` | `i128` / `u128` | **wraps** modulo 2^128 — C#'s unchecked default, and teko has no `checked` word |
| `a / b` | `i128` / `u128` | truncates toward zero, signed for `i128` and unsigned for `u128`; `b == 0` is `teko: division by zero`, exit 70; `i128.MinValue / -1i` is `teko: an integer division overflowed`, exit 70 (D82, `u128` has no such row) |
| `a % b` | `i128` / `u128` | truncating, and the sign follows the DIVIDEND alone — unlike `/`'s `sign(a) != sign(b)` on the quotient; `b == 0` and `i128.MinValue % -1i` fail the same two ways `/` does (D83, D82's ruling 7) |
| `a << n`, `a >> n` | `i128` / `u128` | `>>` is ARITHMETIC (sign-filled) for `i128` and LOGICAL (zero-filled) for `u128`; the count converts to the SAME wide type before the row is looked up (never `i64`) and the wrapper masks its own low 7 bits — `count & 127`, .NET's `Int128`/`UInt128` shift mask, so a negative count falls out under it too (D83) |
| `a & b`, `a \| b`, `a ^ b`, `~a` | `i128` / `u128` | the same bits either way, limb-wise |
| `-a` | `i128` / `u128` | wraps: `-MinValue` is `MinValue` |
| `+a` | the operand | no call at all |
| `==` `!=` `<` `<=` `>` `>=` | `i64` 0/1 | signed for `i128`, unsigned for `u128`: the same sixteen bytes read `-1i` and `340282366920938463463374607431768211455u` |
| `a + 1`, `1 + a` | `i128` / `u128` | the integer converts first, then the row above |

`i128 + u128` takes **no** row and is refused without a cast, which is C#'s rule for the
same pair.

**The conversions**, none of them an instruction — every one is a call:

| from | to | how |
|---|---|---|
| any integer (`u8`..`i64`, `i8`, `i16`, `i32`) | `i128`, `u128` | **implicit**, and `(i128) n` writes it down. A `u64` at or above 2^63 goes through a row of its own, so it never converts through a signed door |
| `i128`, `u128` | any integer | **explicit** `(i64) v`, `(i32) v`, `(u64) v` — the low 64 bits, then the ordinary narrowing |
| `i128` | `u128`, and back | **explicit**, the same bits: `(u128)(-1i)` is `2^128−1`, C#'s own answer |
| `i128`, `u128` | `f64` | **explicit** `(f64) v`, ROUNDS once, to nearest even, over the whole 128-bit magnitude (D84) |
| `f64` | `i128`, `u128` | **explicit** `(i128) x`, truncates toward zero and SATURATES: NaN → 0, a magnitude at or past the type's own bound → that bound — .NET's `Int128`/`UInt128` rule, and teko's own since there is no `checked` word (D84) |
| `i128`, `u128` | `decimal` | **explicit** `(decimal) v`; a magnitude at or past `2^96` (`decimal.MaxValue` is `2^96 - 1`) panics `teko: decimal overflow`, exit 70 (D84) |
| `decimal` | `i128`, `u128` | **explicit** `(i128) d`, truncates toward zero and never overflows the width it lands in; `(u128) d` panics `teko: decimal overflow`, exit 70, on a negative `d` whose truncated MAGNITUDE is still nonzero (`-0.5m` → `0u` is fine, `-1m` is not) — C#'s own rule for a negative decimal read into an unsigned type (D84) |
| `i128`, `u128` | `str` | **never a cast** — text is `.ToString()`/`i128.Parse(s)`, two members (below), not a conversion row; `(str) x` is refused, `decimal`'s own rule (D85) |
| `null`, a class, a struct, a `T[]` | either | refused |

None of the eight new rows opens an IMPLICIT door either way: `f64 x = v;` and `decimal d = v;`
on an `i128`/`u128` `v` are both refused, `teko: a value of type i128 does not convert to
f64`/`decimal` — the conversion table's row is read only from an explicit `N_CAST`
(`tk_prim_cast_lower`), and `tk_num_wide_widens` still answers 0 for a wide source (D38),
so a plain assignment builds no cast at all (D84).

**The members and statics** (N6b-3, D85), each an ordinary call into `lib/wide.tk`, `decimal`'s
own shape:

| written | answers | notes |
|---|---|---|
| `x.ToString()` | `str` | the shortest decimal digits, a leading `-` for a negative `i128`; `MinValue`'s magnitude (`2^127`) is exact |
| `i128.Parse(s)`, `u128.Parse(s)` | `i128`, `u128` | `[+|-] digits`, no surrounding white space, no separator, no exponent, no suffix (narrower than `decimal.Parse`'s own grammar); panics `teko: the string is not an i128` (`…not a u128` for `u128.Parse`), exit 70, on anything else or a number out of range |
| `i128.TryParse(s, out v)`, `u128.TryParse(s, out v)` | `i64` (`bool`) | `1` and the value, or `0` and `v` zeroed — never panics; an INSTANCE reaching for it is refused, `TryParse` is static |
| `x.CompareTo(y)` | `i64` | `-1`/`0`/`1`, signed for `i128`, unsigned for `u128` — the same comparison the six operators already lower to |
| `x.Equals(y)` | `i64` (`bool`) | takes an `i128`/`u128`; there is no `Equals(object)` — teko has no boxed root type |
| `i128.MinValue`, `MaxValue`, `Zero`, `One`; the same four on `u128` | `i128`, `u128` | each a CALL, never folded: `const i128 K = i128.One;` stays refused, the same rule every other wide `const` already follows |

`u128.Parse("-1")` panics too: a leading `-` is a format failure exactly when the magnitude is
nonzero — `u128.Parse("-0")` succeeds and answers `0`, C#'s own documented `UInt128.Parse`
rule (D85).

**What is refused**, and with which words:

| written | message |
|---|---|
| `a + b` on an `i128` and a `u128` | ``teko: no operator `+` takes these operands`` |
| `u128 u = x;` on an `i128 x` | `teko: a value of type i128 does not convert to u128` |
| `f64 x = v;`, `decimal d = v;` on an `i128`/`u128` `v` | `teko: a value of type i128 does not convert to f64`/`decimal` — the eight rows above are explicit only (D84) |
| `(str) x` | ``teko: an i128 does not cast; `.ToString()` writes it and `i128.Parse(s)` reads it`` (`a u128` for the other) — D85 |
| `x.TryParse(...)` on an instance | ``teko: i128.TryParse is static; reach it through its type`` — D85 |
| `170141183460469231731687303715884105728i` | `teko: an i128 literal is out of range` |
| `const i128 K = 1i;`, `case 1i:` | `teko: const requires a constant expression` / `teko: a case label must be a constant expression` — the folder has no 128-bit arithmetic |
| `i128 g = 5;` at file scope | `teko: a global i128 takes no initializer` — a wide global is a slot and an assignment |
| `extern i128 f();` | ``teko: an `extern` takes no i128`` — the sixteen-byte convention is teko's own, not a C ABI |
| `new i128()` | `teko: new i128() is not taught; write 0i` |
| `x / 0i` | `teko: division by zero`, exit 70 |
| `i128.MinValue / -1i` | `teko: an integer division overflowed`, exit 70 (D82) |

---

## `string`

An ORDINARY counted class, declared in `lib/string.tk` behind `#include "string.tk"` — not
a `type_new` primitive, because a primitive carries no row `tk_is_counted` answers for, and
the reclaim would never fire over one ([the specification](../specs/string.md) § 1, N7a).
`str` — a `uptr` with a NUL at the end, [above](#ptr-uptr-and-str) — stays exactly what it
was: the C boundary every `extern` and every existing fixture already reaches. `string` is
the language's own string, sitting beside it:

```
class string {
    private i64  nbytes;      // the UTF-8 length, no NUL
    private i64  nchars;      // the code-point count
    private uptr data;        // nbytes bytes, then a NUL
    private i64  owned;       // 1 when the destructor frees `data`
}
```

**This page's own N7a share.** `new string(raw)` is the constructor that takes text (the other, `new string()`, is the empty string, D86): it measures `raw`
(a `str`) with `tk_str_len` and COPIES it, so mutating or releasing the source afterwards
has no effect on the object. `.Length` is the code-point COUNT and `.Utf8Length` is the
byte count — teko's `char` is a scalar code point, not a UTF-16 unit, so `.Length` keeps
C#'s MEANING and states its own units (both `O(1)`, both stored at construction). `==`/`!=`
compare BY VALUE — a byte compare of the two `data` fields — never by identity, and
`operator+`/`operator==` are ordinary `public static` members resolved the way
[classes.md](classes.md) § Operators already resolves any (D8): no row of `teko_ops.tk`'s
own, no new pass, no new intrinsic.

```teko
// expect-exit: 42
#include "rt.tk"
#include "string.tk"

i64 main() {
    string a = new string("hi");
    string b = new string("hi");
    if ((a == b) != 1) return 1;                  // by VALUE, not by reference
    if (a.Length != 2) return 2;

    string c = a + b;                              // operator+, a fresh string
    if (c.Length != 4) return 3;
    if (c.CompareTo(new string("hihi")) != 0) return 4;

    string mb = new string("hπllo");                // one two-byte code point
    if (mb.Utf8Length != 6) return 5;
    if (mb.Length != 5) return 6;

    if (string.IsNullOrEmpty(string.Empty) != 1) return 7;
    return 42;
}
```

| member | is |
|---|---|
| `new string(raw)` | copies a `str` |
| `new string()` | the empty string — never a zeroed object (D86) |
| `.Length`, `.Utf8Length` | code points, bytes — both `O(1)` |
| `.ToString()`, `.Equals(string)`, `.CompareTo(string)` (ordinal), `.GetHashCode()` | |
| `operator+` | concatenation, a fresh `string` |
| `operator==`, `operator!=` | by value |
| `string.Empty`, `string.Concat(a, b)`, `string.IsNullOrEmpty(s)` | statics |

`a == null` and `null == a` never reach `operator==` at all: a comparison against the
`null` literal is the CORE's own raw-pointer check against zero, on any reference-shaped
operand, nullable slot or not (D43 rule 2) — `string` needs no row for it and declares
none. `null` itself still lands only in a slot declared `string?` (D43), so
`string.IsNullOrEmpty` takes a `string?`, the one parameter this page's members declare
that way.

**N7b's own share (D88).** A `"..."` LITERAL written where a `string` is expected is no
longer refused: it interns, at compile time, to a module-private global built once per
distinct literal and reused by every other occurrence of the same bytes in the unit —
`string s = "hi";` reads like C# and allocates nothing (`teko_string.tk`, no `rt_alloc`, no
new `syntax`/`type_new`/`pass()` of its own). The conversion table, over every one of
D33's nine slots — an initializer, an assignment, a `return`, an argument of a
free/method/virtual/interface call, an element of a `params T[]`, a field store, an array
element, a `??` arm and a `+`/`==`/`!=` operand:

| from | to | how |
|---|---|---|
| a `"..."` LITERAL | `string` | implicit, interned (§ 4 above) |
| `string` | `str`, `ptr`, `uptr` | implicit at a slot the PROGRAM declares, one `ld64` of the `data` field — never at a parameter of `lib/rt.tk`'s own (which takes a `uptr` generically), never at a method's receiver |
| `str`, `ptr`, `uptr` (not a literal) | `string` | explicit only: `new string(p)`, which copies |
| `null` | `string` | implicit, only into a slot declared `string?` (D43) |

```teko
// expect-exit: 42
#include "rt.tk"
#include "string.tk"

i64 main() {
    string a = "hi";                              // interned, no allocation
    string b = "hi";
    if ((uptr) a != (uptr) b) return 1;            // the SAME object
    if (a.Length != 2) return 2;

    str p = a;                                    // `string` -> `str`, a field load
    if (tk_str_len(p) != 2) return 3;
    if ((a == "hi") != 1) return 4;                // a literal on either side of `==`
    if ((a + " there").Length != 8) return 5;      // ...and of `+`

    string? n = null;
    string d = n ?? "fallback";                    // ...and of `??`
    if (d.Length != 8) return 6;

    return 42;
}
```

A `str` VARIABLE — even one a literal initialized two lines earlier — is still refused the
generic way (`teko: a value of type uptr does not convert to string`,
`tests/refuse/string_str_implicit.tk`): interning reads the SOURCE NODE, not the value, so
only a literal written directly in the slot converts, and `new string(p)` stays the road
for everything else.

**N8's own share (D89): `s[i]` and the method surface.** `s[i]` answers the `i`-th CODE
POINT, as a `char` — `O(1)` on an ASCII string, `O(i)` otherwise — through `tk_string_at`
(`lib/string.tk`), the one targeted row `tk_bracket` (`teko_params.tk`) gains for a
`string` receiver; out of range panics `teko: string index out of range`, exit 70. `s[i] =
c` refuses, `teko: a string is immutable`. Every method returns a NEW string or an
existing one, ordinal everywhere (no culture):

| member | is |
|---|---|
| `.Substring(i)`, `.Substring(i, n)` | code-point indices, matching `.Length` |
| `.IndexOf(string)`, `.IndexOfChar(char)`, `.LastIndexOf(string)` | `-1` when not found, a code-point index otherwise |
| `.Contains(string)`, `.StartsWith(string)`, `.EndsWith(string)` | |
| `.Trim()`, `.TrimStart()`, `.TrimEnd()` | ASCII whitespace only — a divergence from C#'s Unicode-aware `char.IsWhiteSpace` |
| `.ToUpper()`, `.ToLower()` | ASCII only (§ 10) |
| `.Replace(old, new)`, `.Split(char)` → `string[]` | an empty `old` replaces nothing (no exception: teko has none to throw) |
| `.PadLeft(n)`, `.PadRight(n)` | padded with ASCII spaces to `n` code points |
| `string.Join(sep, string[])` | static |

`.IndexOf(char)` is spelled `IndexOfChar` rather than a second `IndexOf` overload: a class
method is resolved by name-and-ARITY alone, unlike a free function's overload machinery,
so a second one-argument `IndexOf` is genuinely ambiguous today
([not-yet.md](not-yet.md) § `string`).

```teko
// expect-exit: 42
#include "rt.tk"
#include "string.tk"

i64 main() {
    string s = new string("hello, world");
    if (s[0] != 'h') return 1;
    if (s.Substring(7) != new string("world")) return 2;
    if (s.IndexOf(new string("world")) != 7) return 3;
    if (s.IndexOfChar('w') != 7) return 4;
    if (s.Contains(new string("wor")) != 1) return 5;
    if (s.StartsWith(new string("hello")) != 1) return 6;
    if (s.EndsWith(new string("world")) != 1) return 7;
    if ((new string("  hi  ")).Trim() != new string("hi")) return 8;
    if ((new string("MiXeD")).ToUpper() != new string("MIXED")) return 9;
    if (s.Replace(new string("world"), new string("there")) != new string("hello, there")) return 10;
    string[] parts = (new string("a,b,c")).Split(',');
    if (parts.Length != 3) return 11;
    if (string.Join(new string("-"), parts) != new string("a-b-c")) return 12;
    return 42;
}
```

**The include refusal § 5 names is not built, and not buildable the way the page asks.**
D48 already measured the identical question for `DateTimeKind`, a type an `#include`d
library file declares rather than a compiler-registered primitive: the only door to a
program-wide hint is `syntax_expr`/`type_alias`/`type_new`, and all three claim the WORD,
which would make `lib/string.tk`'s own `class string { ... }` refuse its own name the
moment it is parsed (`teko: the name is already a type`, measured). `string` named with no
`#include "string.tk"` therefore stays the core's own `expected ; after expression`, and
`docs/specs/string.md` § 5/§ 9's own row is corrected to say so — a library type is told
apart by the library, D48's own sentence (D88).

**N10's own share (D92): `$"..."` interpolation.** `syntax_expr("$", …)` claims the token
(`teko_interp.tk`, new module); `$"a{x}b"` lowers into a `+` chain of the literal pieces and
one formatter call per hole, chosen by the hole's STATIC type:

| the hole's type | the formatter |
|---|---|
| `string` | itself |
| `str`, `ptr`, `uptr` | the class's own `str` constructor |
| `char`, and `u32` (the same core id) | a UTF-8 encode |
| `u64`, `usize` | an UNSIGNED decimal peel of its own — `tk_i64_to_dec` is signed, and read every `u64` at or above 2^63 as its negative twin |
| every other integer width | the decimal text `tk_i64_to_dec` already writes |

`u32` and `char` are the SAME core type id (`type_alias("char", TY_U32)`, no distinct
`type_new`), so a `u32` hole gets the UTF-8 encode too — `65` reads `"A"`, one character,
not the digits `"65"` — the one observable consequence of the alias. `f64`/`f32`/
`decimal`/`DateTime`/`TimeSpan`/`Guid`/an `enum` are refused by name
(`teko: no interpolation of a value of type Foo`, [not-yet.md](not-yet.md)); so is C#'s
alignment/format specifier, `{x,10}`/`{x:N2}` (`teko: an interpolation hole holds one
expression, no alignment or format specifier`), a ternary/`??`/`?.` hole
(`teko: an interpolation hole holds no ternary, ?? or ?.`) and a line comment inside a hole
(`teko: a line comment does not fit in an interpolation hole` — a block comment is fine).
`$` with no `#include "string.tk"` reads `teko: string interpolation needs #include
"string.tk" before it is used`; that check runs after the string-literal one, so a `$` that
is not an interpolation at all (`${1}`, `$ 1`, `$(1)` → `teko: $ needs a string literal for
interpolation`) is never blamed on a missing include.

An interpolated string is an ordinary expression: any infix operator may follow it
(`$"a{n}" + s`, `$"c" == s`). A `.` directly on it (`$"a{n}".Length`) is refused
`teko: uptr has no members: Length`, the same answer `"abc".Length` gives with no
interpolation involved; bind to a `string` local first.

```teko
// expect-exit: 42
#include "rt.tk"
#include "string.tk"

class Point { public i64 X; public i64 Y; public Point(i64 x, i64 y) { X = x; Y = y; } }

i64 main() {
    i64 n = 7;
    string a = $"n={n}";
    if (a.Length != 3) return 1;

    Point p = new Point(3, 4);
    string b = $"({p.X}, {p.Y})";
    if (b.Length != 6) return 2;

    string c = $"{{literal braces}} and {n}";
    if (c.Length != 22) return 3;

    return 42;
}
```

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
