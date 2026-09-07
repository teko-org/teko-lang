# Parameters, overloads, `ref`/`out` and `params`

Four things a parameter list can do beyond naming a type: carry a default, distinguish one
overload from another, pass by reference, and take a list of arguments whose count the call
site decides.

A function takes at most **12** parameters; a method's receiver is one of them, and a
virtual call's vtable pointer another.

---

## Default arguments

```
i64 add(i64 a, i64 b = 10)
```

A default belongs to the **declaration**, and the call site fills it in. It works on a free
function, on a method, on a constructor and on an interface signature. Two rules:

- the default is a **constant expression**, folded at the declaration;
- no parameter without a default may follow one that has a default.

A `virtual`/`override` pair keeps one vtable slot and each end carries its own default: a
call through a base-typed name takes the **base's** default and still runs the override.

When one name is declared at the same arity both with and without a default, the candidate
that needs no default wins — C#'s own rule.

```teko
// expect-exit: 42
#include "rt.tk"

i64 add(i64 a, i64 b = 10) {
    return a + b;
}

i64 scale3(i64 a, i64 b = 2, i64 c = 3) {
    return a * b * c;
}

i64 pref(i64 n) {
    return n + 100;
}

i64 pref(i64 n, i64 k = 5) {
    return n + k;
}

class Base {
    public i64 n;

    public virtual i64 area(i64 k = 2) {
        return n * k;
    }
}

class Derived : Base {
    public override i64 area(i64 k = 5) {
        return n * k * 10;
    }
}

i64 main() {
    if (add(1) != 11) return 1;
    if (add(1, 2) != 3) return 2;
    if (scale3(2) != 12) return 3;
    if (scale3(2, 4) != 24) return 4;
    if (pref(1) != 101) return 5;                // the signature with no default wins
    if (pref(1, 2) != 3) return 6;

    Base b = new Base;
    b.n = 3;
    Derived d = new Derived;
    d.n = 1;
    if (b.area() != 6) return 7;                 // the base's own default
    if (d.area() != 50) return 8;                // the override's own
    Base r = d;
    if (r.area() != 20) return 9;                // the base's default, the override's body

    return add(1) + scale3(2) + b.area() + d.area() - 37;
}
```

---

## Overloading

One name, several signatures — on a method, on a constructor, on an operator and on a free
function. Which one a site calls is decided by the **argument types**, from the compiler's
own static-type oracle; an integer literal is an `i64`, and a float literal is an `f64`
(an `f32` under the `f` suffix).

| | |
|---|---|
| a method | overloads by signature; a `virtual` pair is one slot **per signature** |
| a free function | every overload is given a symbol of its own (`pick__i64`, `pick__Vec`) |
| a name declared once | keeps its symbol untouched |
| `extern` | owns its C symbol and cannot be overloaded |
| `main` | takes one signature |
| two signatures differing only by `ref`/`out` | refused |
| two candidates fitting one site | refused as ambiguous |
| a `params T[]` | one signature among the name's; the site decides (§ `params`) |

Resolution is by arity first, then by types; the return type never takes part. A call
nested in another is resolved first, so its return type types the outer site, and a
recursive call is an ordinary site.

### What a literal converts to

An **integer** literal carries no type of its own: it lands on any of the core's integers,
and on `i64` before the others, which is what makes `pick(1)` with both `pick(i64)` and
`pick(u8)` in reach `pick(i64)`.

A **float** literal does carry one. `1.5` is an `f64` exactly as C# reads it, so only an
`f64` parameter takes it — in the exact round and in every round after it, whatever the
declaration order. There is no implicit conversion from a float to an integer, so a value
of float type in an integer slot is refused, `teko: a value of type f64 does not convert
to i64`, and it is refused the same way when the name is declared once and no overload is
searched at all. It holds for an expression as much as for a literal: a local of float
type, a call whose return type is `f64`, and a binary whose left operand is a float all
pick the float signature — `near` in the sample below.

```teko
// expect-exit: 42
#include "rt.tk"

struct Vec {
    public i64 x;
    public i64 y;
}

i64 pick(i64 n) {
    return n + 1;
}

i64 pick(Vec v) {
    return v.x + v.y;
}

i64 pick(i64 a, i64 b) {
    return a * b;
}

i64 tally() {
    return 2;
}

i64 tally(i64 k) {
    return k;
}

i64 near(i64 n) {
    return 700 + n;
}

i64 near(f64 x) {
    return 800;
}

class Box {
    public i64 w;

    public i64 grow() {
        return w + 1;
    }

    public i64 grow(i64 k) {
        return w + k;
    }
}

i64 main() {
    Vec v = new Vec;
    v.x = 4;
    v.y = 6;

    if (pick(9) != 10) return 1;
    if (pick(v) != 10) return 2;
    if (pick(3, 4) != 12) return 3;
    if (pick(pick(3, 4)) != 13) return 4;        // the inner call types the outer

    if (tally() != 2) return 5;
    if (tally(5) != 5) return 6;

    Box b = new Box;
    b.w = 5;
    if (b.grow() != 6) return 7;
    if (b.grow(3) != 8) return 8;

    if (near(9) != 709) return 9;                // the integer literal is i64
    if (near(1.5) != 800) return 10;             // the float literal is f64

    return pick(9) + pick(v) + pick(3, 4) + tally() + b.grow() + 2;
}
```

---

## `ref` and `out`

```
void bump(ref i64 x)
void mkbox(out Box b)
```

`ref T` and `out T` are C#'s by-reference parameters, and the **call site says so too**:
`bump(ref a)`, `mkbox(out x)`. The argument is a name — a local, a parameter, a field or an
array element — never a call's result and never an expression.

| | `ref` | `out` |
|---|---|---|
| the caller's variable is read by the callee | yes | not required |
| the callee is expected to assign it | no | yes; never assigning it is refused |
| a counted pointee starts released | no | yes, so the first assignment frees nothing |

The pointee type is checked against the declaration, scalars included, so `ref i64` never
receives a `ref u8`. A pointee narrower than a word (`u8`, `i32`) and a float pointee
(`f64`) both work: the slot is a pointer and the value is read and written at its own width.
A `ref`/`out` parameter is passed on to another `ref`/`out` parameter as many levels down as
you like. When the pointee is a class, assigning through the reference releases what the
caller's variable held.

`ref`/`out` is valid **only** in parameter position, and a parameter with one takes no
default.

```teko
// expect-exit: 42
#include "rt.tk"

i64 dtors = 0;

class Cell {
    public i64 v;

    public Cell(i64 x) {
        v = x;
    }

    ~Cell() {
        dtors = dtors + 1;
    }
}

void bump(i64 x) {
    x = x + 1;                                   // by value: the caller sees nothing
}

void bump(ref i64 x) {
    x = x + 1;                                   // overloaded on `ref`
}

void split(i64 v, out i64 hi, out i64 lo) {
    hi = v / 10;
    lo = v % 10;
}

void inc(ref i64 x) {
    x = x + 1;
}

void relay(ref i64 x) {
    inc(ref x);                                  // repassed one level down
}

void bumpb(ref u8 b) {
    b = b + 1;                                   // a pointee narrower than a word
}

void raise(ref f64 v) {
    v = v + 1.5;
}

void replace(ref Cell c, i64 nv) {
    c = new Cell(nv);                            // releases what the caller held
}

class Point {
    public i64 x;
}

i64 main() {
    i64 a = 1;
    bump(a);
    if (a != 1) return 1;
    bump(ref a);
    if (a != 2) return 2;
    relay(ref a);
    if (a != 3) return 3;

    i64 hi = 0;
    i64 lo = 0;
    split(57, out hi, out lo);
    if (hi != 5) return 4;
    if (lo != 7) return 5;

    Point p = new Point;
    p.x = 10;
    bump(ref p.x);                               // a field
    if (p.x != 11) return 6;

    i64 arr[3];
    arr[1] = 2;
    bump(ref arr[1]);                            // an array element
    if (arr[1] != 3) return 7;

    u8 byte_v = 255;
    bumpb(ref byte_v);
    if (byte_v != 0) return 8;                   // one byte wide, so it wraps

    f64 f = 1.0;
    raise(ref f);
    if (f != 2.5) return 9;

    Cell c = new Cell(1);
    replace(ref c, 5);
    if (c.v != 5) return 10;
    if (dtors != 1) return 11;                   // the original Cell, released

    return hi * 8 + lo - c.v;                    // 40 + 7 - 5
}
```

---

## `params`

```
i64 total(params i64[] xs) { ... xs.Length ... xs[i] ... }
```

`params` is a **modifier** read before the type, the way `ref` and `out` are, and the type
after it is a genuine `T[]` ([arrays.md](arrays.md)). It goes on the **last** parameter of
a **free function**, one only, never with `ref`/`out`, never with a default, never on an
`extern`. Inside the body `xs` is an ordinary `T[]`: `xs.Length`, `xs[i]`, `xs[i] = e`,
`foreach (T v in xs)`, passing it on, returning it.

**At the call site**, N arguments of type `T` become the array, N ≥ 0 — zero arguments is
an array of length 0, never null. A single argument that is **already a `T[]`** passes
straight through, without a copy. The element type is anything a `T[]` holds: a scalar, a
**float**, a class, an interface, a delegate.

The array is a counted object, so it is released at the end of the statement that built it,
taking every counted element with it ([memory.md](memory.md)). Nothing is left on the
arena.

```teko
// expect-exit: 42
#include "rt.tk"

i64 total(params i64[] xs) {
    i64 s = 0;
    i64 i = 0;
    while (i < xs.Length) {
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

i64 offset_total(i64 base, params i64[] rest) {
    return base + total(rest);                   // the list passed on, no copy
}

f64 ftotal(params f64[] xs) {
    f64 s = 0.0;
    i64 i = 0;
    while (i < xs.Length) {
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

i64 main() {
    if (total() != 0) return 1;                  // an array of length 0
    if (total(7) != 7) return 2;
    if (total(1, 2, 3, 4, 5) != 15) return 3;
    if (offset_total(10, 1, 2, 3) != 16) return 4;
    if (total(total(1, 2), 3) != 6) return 5;    // one variadic call inside another
    if ((i64) ftotal(1.5, 2.5) != 4) return 6;   // a float element

    i64[] ready = new i64[2];
    ready[0] = 20;
    ready[1] = 20;
    if (total(ready) != 40) return 7;            // the normal form

    return total(20, 20, 2);
}
```

### `params` and overloading

A list is **one signature among the name's**, and C#'s own rule decides a site: a candidate
applicable in its **normal form** wins, and only a call that no candidate takes as written
asks a list to swallow the tail.

| written, with `f(i64)` and `f(params i64[])` both in reach | what it means |
|---|---|
| `f(1)` | `f(i64)`, the normal form |
| `f(1, 2)` | the list, expanded |
| `f()` | the list, expanded — an array of length 0 |
| `f(xs)`, `xs` an `i64[]` | the list in **its own** normal form, without a copy |

A default belongs to the normal form, so `f(i64 a, i64 b = 5)` takes `f(1)` ahead of any
list. Two lists of one name are told apart by their **element type**, and between two that
both take a site the one with **more declared parameters** wins — `f(i64, params i64[])`
over `f(params i64[])` at `f(1, 2)`, as in C#. A tail no element type takes is
`teko: no overload of f matches these arguments`; two lists nothing tells apart are
`teko: more than one overload of f matches these arguments`.

```teko
// expect-exit: 42
#include "rt.tk"

i64 span(i64 n) {
    return 1000 + n;
}

i64 span(params i64[] xs) {
    i64 s = 0;
    i64 i = 0;
    while (i < xs.Length) {
        s = s + xs[i];
        i = i + 1;
    }
    return 2000 + s;
}

i64 mix(params i64[] xs) {
    return 100 + xs.Length;
}

i64 mix(params f64[] ys) {
    return 200 + ys.Length;
}

i64 wide(i64 a, i64 b = 5) {
    return 300 + a + b;
}

i64 wide(params i64[] xs) {
    return 400 + xs.Length;
}

i64 main() {
    if (span(9) != 1009) return 1;               // the normal form wins
    if (span(1, 2) != 2003) return 2;            // ...and only then the list
    if (span() != 2000) return 3;                // an array of length 0

    i64[] ready = new i64[2];
    ready[0] = 20;
    ready[1] = 20;
    if (span(ready) != 2040) return 4;           // the list's own normal form

    if (mix(1) != 101) return 5;                 // the integer list
    if (mix(1.5) != 201) return 6;               // the float one

    if (wide(1) != 306) return 7;                // a default completes the normal form
    if (wide(1, 2, 3) != 403) return 8;          // only the list takes three

    return 42;
}
```

---

## Limits

| limit | value |
|---|---|
| parameters of a function | 12 (8 in registers) |
| parameters of a method | 12, the receiver and a virtual call's vtable pointer included |
| fixed parameters before a `params` list | 11 (the list itself is the twelfth) |
| arguments at one `params` call site | no ceiling: the tail goes to memory, not to the ABI |
| arguments at one call of an **overloaded** name | 64, every one of them typed at once |
| a `params` list with a default or with `ref`/`out` | refused |
| `params` on an `extern`, a method, a constructor or a `delegate` | refused |
| `params` lists in one unit | 64 |
| reading a `ref T[]` / `out T[]` inside the callee | not taught: `xs[i]` and `xs.Length` there are refused |
| `f(out i64 a)` declaring the variable at the call site | not taught |
| an `out` assigned only along some paths | only "never assigned" is checked |
| a `ref` **parameter** repassed to an **overloaded** name | not taught: the argument's type is not known at that site |
| `ref`/`out` parameters declared in one unit | 512 |
| default arguments, summed across all signatures | 64 |
| names declared at more than one signature | 64 |
