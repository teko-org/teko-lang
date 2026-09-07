# Generics

A generic class or struct takes **type** parameters and **constant** parameters. Its body
is recorded once and replayed per set of arguments, so every instance is an ordinary type
with a layout of its own and a constant the compiler already folded.

```
class Name<T[, U...][, const N: i64]> { members }
struct Name<...> { members }
```

A type parameter stands wherever a type word stands: a field, a parameter, a return type,
a local, the element type of an inline array. A `const` parameter is an `i64` and stands
wherever a constant stands — which is what makes `T items[N]` a real inline array whose
length the instantiation fixes.

## Instantiating

An instance is born at its **first use** — a declaration, a field or parameter type, a
return type, or `new` — and it is memoized by (name, arguments), so `Box<Circle, 2>` names
one type however many times it is written. Nesting closes on a `>>` with no space.

| written | the type it names | its layout constants |
|---|---|---|
| `Box<Circle, 4>` | `Box__Circle__4` | `BOX__CIRCLE__4_SIZE`, `BOX__CIRCLE__4_COUNT` |
| `Holder<Box<Circle, 2>>` | `Holder__Box__Circle__2` | the same shape, mangled |

A `const` argument is an integer literal or a `const` already declared, and nothing else.

## What the replay buys

`N` is a **constant inside the instance**, so an index written as a literal is checked
against it where it stands and costs no run-time guard; a computed index keeps its guard.
Two instances of one declaration are two unrelated types: no shared field, no shared
method symbol, and `Box<Circle, 2>` never converts to `Box<Circle, 4>`.

A generic may be `partial` like any other class — one recorded region per part, replayed
in the order the parts were written.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

const i64 TWO = 2;

class Circle {
    public i64 r;
}

class Box<T, const N: i64> {
    T items[N];                                  // the instance fixes the length
    i64 count;

    public i64 cap() {
        return N;
    }

    public i64 add(T it) {
        if (count >= N) return 0;
        this.items[this.count] = it;             // an array field, through the receiver
        count = count + 1;
        return 1;
    }

    public i64 total() {
        i64 s = 0;
        for (i64 i = 0; i < count; i++) {
            T c = this.items[i];
            s = s + c.r;
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

i64 main() {
    Box<Circle, TWO> b = new Box<Circle, TWO>;   // a declared const as the argument
    Holder<Box<Circle, TWO>> h = new Holder<Box<Circle, TWO>>;

    if (BOX__CIRCLE__2_SIZE != 40) return 1;     // 16 header + 2*8 items + 8 count
    if (BOX__CIRCLE__2_COUNT != 32) return 2;    // the field after the array moved

    Circle p = new Circle;
    p.r = 20;
    Circle q = new Circle;
    q.r = 21;
    if (b.add(p) != 1) return 3;
    if (b.add(q) != 1) return 4;

    Circle x = new Circle;
    x.r = 7;
    if (b.add(x) != 0) return 5;                 // the constant closed this box

    h.inner = b;
    if (h.get().total() != 41) return 6;

    return h.get().total() + b.cap() - 1;
}
```

There is no generic `delegate`, no qualified generic (`geo.Box<i64>`) and no generic as a
dependency-injection key; the full account, with the tables and their limits, is
[generics.md](../reference/generics.md).
