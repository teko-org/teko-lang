# Generics

A generic class or struct takes type parameters and **constant** parameters. Its body is
recorded once and replayed per set of arguments, so every instance is an ordinary type with
a layout of its own and a constant the compiler folded.

---

## Declaring

```
class Name<T[, U...][, const N: i64]> { members }
struct Name<...> { members }
```

A type parameter stands wherever a type word stands inside the body — a field, a
parameter, a return type, a local, the element of an inline array. A `const` parameter is
an `i64` and stands wherever a constant stands, which is what makes `T items[N]` a real
inline array whose length the instantiation fixes.

## Instantiating

An instance is born at its **first use** — a declaration, a field or parameter type, a
return type, or `new` — and it is memoized by (name, arguments), so `Box<Circle, 2>` names
one type however many times it is written. Nesting closes on a `>>` without a space.

| written | the type |
|---|---|
| `Box<Circle, 4> b = new Box<Circle, 4>;` | `Box__Circle__4` |
| `Holder<Box<Circle, 2>> h = ...;` | `Holder__Box__Circle__2` |

The instance's own layout constants carry the mangled name: `BOX__CIRCLE__4_SIZE`,
`BOX__CIRCLE__2_COUNT`. A `const` argument is an integer literal or a `const` already
declared — nothing else.

```teko
// expect-exit: 42
#include "rt.tk"

const i64 TWO = 2;

class Circle {
    public i64 r;
}

class Box<T, const N: i64> {
    T items[N];
    i64 count;

    public i64 cap() {
        return N;                                // an integer literal in this instance
    }

    public i64 add(T it) {
        if (count >= N) {
            return 0;
        }
        this.items[this.count] = it;             // an inline array: through the receiver
        count = count + 1;
        return 1;
    }

    public i64 total() {
        i64 s = 0;
        i64 i = 0;
        loop {
            if (i >= count) break;
            T c = this.items[i];
            s = s + c.r;
            i = i + 1;
        }
        return s;
    }
}

class Holder<T> {
    public T inner;

    public T get() {
        return inner;
    }
}

struct Slot<T, const N: i64> {
    public T v[N];

    public i64 room() {
        return N;
    }
}

i64 main() {
    Box<Circle, 4> b4 = new Box<Circle, 4>;
    Box<Circle, TWO> b2 = new Box<Circle, TWO>;  // a declared const as the argument
    Holder<Box<Circle, TWO>> h = new Holder<Box<Circle, TWO>>;
    Slot<i64, 3> s = new Slot<i64, 3>;

    if (BOX__CIRCLE__2_SIZE != 40) return 1;     // 16 header + 2*8 items + 8 count
    if (BOX__CIRCLE__4_SIZE != 56) return 2;     // one body, four slots
    if (BOX__CIRCLE__2_COUNT != 32) return 3;    // the field after the array moves
    if (SLOT__I64__3_SIZE != 24) return 4;       // a struct: no header at all

    Circle p = new Circle;
    p.r = 20;
    Circle q = new Circle;
    q.r = 21;
    if (b2.add(p) != 1) return 5;
    if (b2.add(q) != 1) return 6;
    Circle x = new Circle;
    x.r = 7;
    if (b2.add(x) != 0) return 7;                // the constant closed this box

    h.inner = b2;
    if (h.get().total() != 41) return 8;

    s.v[0] = 1;
    if (s.room() != 3) return 9;

    return b4.cap() - b2.cap() + h.get().total() - s.v[0] + s.room() - 3;
}
```

---

## What the replay buys

`N` is a **constant inside the instance**, so an index written as a literal is checked
against it at compile time and costs no run-time guard: `items[4]` in a `Box<Circle, 2>` is
`teko: index 4 is out of range for items[2]` where it is written. A computed index keeps its
guard.

Two instances of one declaration are two types: they share no field, no method symbol and
no layout, and `Box<Circle, 2>` never converts to `Box<Circle, 4>`.

---

## `partial` generics

A generic is partial by the same rule an ordinary class is: one recorded region per part,
replayed as the parts were written.

```teko
// expect-exit: 42
#include "rt.tk"

partial class Cell<T, const N: i64> {
    public T items[N];
}

partial class Cell<T, const N: i64> {
    public i64 cap() {
        return N;
    }
}

i64 main() {
    Cell<i64, 3> c = new Cell<i64, 3>;
    c.items[0] = 39;
    return c.items[0] + c.cap();
}
```

---

## Limits

| limit | value |
|---|---|
| a `const` generic parameter | is an `i64` |
| a `const` generic argument | an integer literal or a declared `const` |
| a **qualified** generic (`geo.Box<i64>`) | not taught; a generic declared in a namespace keeps its short name |
| a generic `delegate` | not taught |
| a generic as a dependency-injection key (`IRepo<T>`) | not taught |
| generic declarations in one source | 16 |
| parameters of one generic | 4 |
| instances generated in one source | 32 |
| recorded parts, summed across all generics | 32 |

An argument that names no known type is `teko: unknown type as a generic argument`; a
`<` on a type that declares no parameters is `teko: not a generic type`. The full list is
[diagnostics.md](diagnostics.md).
