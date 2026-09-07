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
own static-type oracle; an integer literal is an `i64`.

| | |
|---|---|
| a method | overloads by signature; a `virtual` pair is one slot **per signature** |
| a free function | every overload is given a symbol of its own (`pick__i64`, `pick__Vec`) |
| a name declared once | keeps its symbol untouched |
| `extern` | owns its C symbol and cannot be overloaded |
| `main` | takes one signature |
| two signatures differing only by `ref`/`out` | refused |
| two candidates fitting one site | refused as ambiguous |

Resolution is by arity first, then by types; the return type never takes part. A call
nested in another is resolved first, so its return type types the outer site, and a
recursive call is an ordinary site.

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
i64 total(params xs) { ... xs_len ... xs[i] ... }
```

The spelling is the core's — `params xs`, not `params i64[] xs` — and the list holds
**words**. Inside the body, `xs_len` is the count and `xs[i]` is the i-th element. The
declaration is a generic in that count: a site with `k` arguments instantiates a body of
its own with `xs_len` equal to `k`, so a **literal** index is checked against it at compile
time and costs no guard, while a computed index keeps one and panics out of range.

The list is allocated at the call site, so a variadic call may sit inside another one and
inside the body of a variadic function; each list is its own block. A fixed parameter may
come before the list.

```teko
// expect-exit: 42
#include "rt.tk"

i64 total(params xs) {
    i64 s = 0;
    i64 i = 0;
    loop {
        if (i >= xs_len) break;
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

i64 offset_total(i64 base, params rest) {
    i64 s = base;
    i64 i = 0;
    loop {
        if (i >= rest_len) break;
        s = s + rest[i];
        i = i + 1;
    }
    return s;
}

i64 first_and(params xs) {
    return xs[0] + xs[1];                        // literal indexes, checked per instance
}

i64 main() {
    if (total() != 0) return 1;                  // the empty list
    if (total(7) != 7) return 2;
    if (total(1, 2, 3, 4, 5) != 15) return 3;
    if (offset_total(10, 1, 2, 3) != 16) return 4;
    if (total(total(1, 2), 3) != 6) return 5;    // one variadic call inside another
    if (first_and(10, 20, 30) != 30) return 6;

    return total(20, 20, 2);
}
```

The list itself is **not** reclaimed: it is born and read inside one expression, with no
name to hold it, so a `params` call in a hot loop walks the arena forward
([memory.md](memory.md)).

---

## Limits

| limit | value |
|---|---|
| parameters of a function | 12 (8 in registers) |
| parameters of a method | 12, the receiver and a virtual call's vtable pointer included |
| fixed parameters before a `params` list | 10 (the list costs two of the twelve) |
| arguments at one `params` call site | 12, the fixed ones included |
| a `float` argument to a `params` list | not taught |
| `params T[]` | not taught |
| a `params` list with a default, an overload, or an address (`&f`) | refused |
| `params` on an `extern` | refused |
| `ref T[]` / `out T[]` | not taught |
| `f(out i64 a)` declaring the variable at the call site | not taught |
| an `out` assigned only along some paths | only "never assigned" is checked |
| a `ref` **parameter** repassed to an **overloaded** name | not taught: the argument's type is not known at that site |
| `ref`/`out` parameters declared in one unit | 512 |
| default arguments, summed across all signatures | 64 |
| names declared at more than one signature | 64 |
