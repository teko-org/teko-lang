# Arrays

Two array shapes, and they are different types. A **fixed** array has a length the source
writes down and lives where it is declared; a **heap** array `T[]` is an object with a
length it carries, allocated by `new T[n]` and reference-counted.

---

## Fixed arrays

```
T a[N];              // a local, a global, or a field
T t[] = { … };       // a global with an initializer
```

`N` is an integer literal or a `const`, and it is positive. The element type is a scalar —
an array of objects is refused (`teko: an array of objects is not taught yet`); use `T[]`.

| written | on a local | on a global | on a field |
|---|---|---|---|
| `a[i]` | yes | yes | through the receiver: `this.items[i]` |
| `a[i] = e`, `+=`, `-=`, `++`, `--` | yes | yes | through the receiver |
| `a.Length` | yes | — | — |

A **literal** index outside `[0, N)` is a compile-time error,
`teko: index K is out of range for a[N]`; an index computed at run time on a fixed array is
not guarded. Width and sign follow the element type: `u8`/`u16`/`u32` read back
zero-extended, `i32` sign-extended.

```teko
// expect-exit: 42
#include "rt.tk"

u8  g[8];
i64 t[] = { 1, 2, 3, 4, 5 };

i64 sum_local() {
    i64 a[4];
    for (i64 i = 0; i < a.Length; i++) {
        a[i] = i * 2;
    }
    a[0] += 100;
    a[0] -= 100;
    i64 total = 0;
    for (i64 i = 0; i < a.Length; i++) {
        total = total + a[i];
    }
    return total;                                // 0 + 2 + 4 + 6
}

i64 sum_global() {
    i64 total = 0;
    for (i64 i = 0; i < 8; i++) {
        g[i] = i;
        total = total + g[i];
    }
    for (i64 i = 0; i < 5; i++) {
        total = total + t[i];
    }
    return total;                                // 28 + 15
}

i64 signs() {
    i32 s[2];
    s[0] = 0 - 5;
    if (s[0] >= 0) return 1;                     // read back sign-extended
    return 0;
}

class Grid {
    public i64 product3() {
        i64 xs[3];
        xs[0] = 2;
        xs[1] = 3;
        xs[2] = 7;
        i64 p = 1;
        for (i64 i = 0; i < xs.Length; i++) {
            p = p * xs[i];
        }
        return p;
    }
}

i64 main() {
    if (sum_local() != 12) return 1;
    if (sum_global() != 43) return 2;
    if (signs() != 0) return 3;
    Grid gr = new Grid();
    return gr.product3();
}
```

### An inline array field

A field written `T items[N]` is part of the object's own layout — the size grows with `N`,
and the field after it moves. It is reached through the receiver written out (`this.items[i]`
inside the type, `b.items[i]` on a local), never by the bare name.

---

## Heap arrays: `T[]`

```
T[] xs = new T[n];       // n is any expression
```

`T[]` is an object: a vtable, a reference count, its `Length`, then the elements. It is a
type in every position — a local, a global, a field, a parameter, a return type — and it is
counted like a class reference.

| written | means |
|---|---|
| `new T[n]` | `n` elements, zeroed; a negative `n` panics |
| `xs[i]` / `xs[i] = e` / `+=` / `-=` / `++` / `--` | element access, **guarded at run time** |
| `xs.Length` | the length it carries |
| `xs = null` | releases the array, and every element still in it |

Every index is checked: out of range is a panic with exit 70
(`teko: index past the end of an array`), never a read past the allocation.

```teko
// expect-exit: 42
#include "rt.tk"

class Box {
    i64[] items;                                 // as a field

    public Box(i64 n) {
        this.items = new i64[n];
    }

    public void fill(i64[] src) {                // as a parameter
        i64 i = 0;
        while (i < src.Length) {
            this.items[i] = src[i];
            i = i + 1;
        }
    }

    public i64 total() {
        i64 s = 0;
        i64 i = 0;
        while (i < this.items.Length) {
            s = s + this.items[i];
            i = i + 1;
        }
        return s;
    }
}

delegate i64 Op(i64 a, i64 b);

i64 add(i64 a, i64 b) { return a + b; }
i64 mul(i64 a, i64 b) { return a * b; }

i64 main() {
    i64 n = 5;
    i64[] xs = new i64[n];
    for (i64 i = 0; i < xs.Length; i++) {
        xs[i] = i;
    }
    xs[0] += 100;
    xs[2]++;
    if (xs[0] != 100) return 1;
    if (xs[2] != 3) return 2;

    u8[] bs = new u8[2];
    bs[0] = 250;
    bs[1] = 10;
    if (bs[0] + bs[1] != 260) return 3;          // widened on the way out

    i64[] src = new i64[3];
    src[0] = 1;
    src[1] = 2;
    src[2] = 3;
    Box b = new Box(3);
    b.fill(src);
    if (b.total() != 6) return 4;

    Op[] ops = new Op[2];                        // an array of delegate values
    ops[0] = add;
    ops[1] = mul;
    Op picked = ops[1];
    if (picked(3, 4) != 12) return 5;

    return xs[0] - b.total() - 52;                // 100 - 6 - 52
}
```

An element of delegate type is called like any other delegate value: read it into a name,
or write an argument list straight after the index — the element `ops[i]` followed by
`(3, 4)` is the call.

### Elements of a counted type

An element of class, interface, delegate or array type holds a reference: overwriting a
slot releases what was there, and releasing the array releases every element still in it.

```teko
// expect-exit: 42
#include "rt.tk"

i64 dtors = 0;

class Circle {
    i64 r;

    public Circle(i64 r) {
        this.r = r;
    }

    public i64 area() {
        return r * r;
    }

    ~Circle() {
        dtors = dtors + 1;
    }
}

i64 sum_areas(Circle[] cs) {
    i64 total = 0;
    i64 i = 0;
    while (i < cs.Length) {
        total = total + cs[i].area();
        i = i + 1;
    }
    return total;
}

i64 main() {
    Circle[] cs = new Circle[3];
    cs[0] = new Circle(1);
    cs[1] = new Circle(2);
    cs[2] = new Circle(3);
    if (rt_live() != 4) return 1;                // the array object and the three circles
    if (sum_areas(cs) != 14) return 2;

    cs[0] = new Circle(10);                      // the slot released Circle(1)
    if (dtors != 1) return 3;

    cs = null;                                   // the array, and the three live elements
    if (dtors != 4) return 4;
    if (rt_live() != 0) return 5;
    return 42;
}
```

```teko
// expect-exit: 70
#include "rt.tk"

i64 main() {
    i64[] xs = new i64[3];
    return xs[5];                                // teko: index past the end of an array
}
```

### A global `T[]`

`i64[] g;` at top level declares the reference with no length; `g = new i64[n];` fills it
later, and `g[i]`, the compound forms and `g.Length` all answer. A global is a **root**: it
is never released, so `rt_live()` never returns to its floor once one has been filled
([memory.md](memory.md)).

---

## Iterating

`foreach (T x in xs)` walks a heap array — a local or a parameter — a local fixed array or
an inline array field; the element type may widen, never narrow. A **global** `T[]` is not a
`foreach` source. It is [control-flow.md](control-flow.md) § foreach.

---

## Limits

| limit | value |
|---|---|
| `T[][]`, or any multidimensional array | not taught (`an array of arrays is not taught yet`) |
| a fixed array of a class or struct type | not taught; use `T[]` |
| reading a `ref T[]` / `out T[]` **inside the callee** | not taught: the parameter carries the caller's slot, so `xs[i]` and `xs.Length` there are refused (`expression with no codegen`) |
| `.Length` on a **global fixed** array | not taught; a local fixed array and any `T[]` answer |
| an inline array field through a **parameter** (`p.items[i]`) | not taught; a local or `this` resolves |
| a run-time index into a **fixed** array | not guarded (a `T[]` index always is) |
| local array declarations in scope | 1024 |
| global arrays in one source | 512 |
| global `T[]` in one source | 32 |
