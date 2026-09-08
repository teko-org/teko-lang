# Memory

One fixed arena, a free list per size class, and a reference count per **scope** that the
compiler injects. There is no garbage collector and no `malloc`: an object is born at `new`
and handed back the moment the last name holding it stops holding it.

---

## The arena

A block of **4 MiB** in `__bss`, handed out 16-byte aligned. A free list per size class
answers first — 16, 32, … up to 256 bytes — and only when the list for the size is empty
does the bump pointer move. A block bigger than 256 bytes has no list and is not recycled;
when the bump pointer would leave the block, the program panics with `arena exhausted`.

The consequence worth writing down: a loop that churns objects reuses one block forever.

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

i64 main() {
    i64 i = 0;
    loop {
        Cell c = new Cell(i);                    // born and released every iteration
        i = i + 1;
        if (i >= 1000000) break;
    }
    if (dtors != 1000000) return 1;
    if (rt_live() != 0) return 2;
    if (rt_peak() > 4096) return 3;              // one block, reused a million times
    return 42;
}
```

## The object

| word | holds |
|---|---|
| +0 | the vtable pointer |
| +8 | the reference count |
| +16 | the fields, the base class's first |

and the vtable itself starts with the class's own release function at +0 and its interface
table (or 0) at +8, which is how the runtime frees an object whose class it knows nothing
about. A `T[]` and a delegate value are objects of the same shape.

**A nullable over a VALUE is one too** — the box `i64?`, `f64?`, an `enum?`, `TimeSpan?`
and every other value nullable point at ([nullable.md](nullable.md)):

| word | holds |
|---|---|
| +0 | the vtable pointer, whose +0 is the one release every box shares |
| +8 | the reference count |
| +16 | the payload width, in bytes |
| +24 | the payload itself |

A box holds no reference of its own — a counted `T` is already a pointer, and its nullable
IS that pointer — so the release is `rt_free(p, 24 + width)` and nothing else, and it is
the same function for every payload type there is.

`rt_alloc` hands out **zeroed** bytes, so a field nobody assigned reads as `0` and a
reference field reads as null.

---

## What owns what

| the holder | owns |
|---|---|
| a **local** of counted type | releases at the end of its block, and on the way out of a `break`, `continue` or `return` that leaves it |
| a **field** of counted type | released when the object holding it dies, and when the field is overwritten |
| an **element** of a `T[]` | the same |
| a **capture by value** in a closure | released with the closure |
| a **global**, a **static field**, a **Singleton** | never released: it is a root |
| a **parameter** of class type | **borrows** — it carries no count of its own, and reassigning it is refused |

"Counted" means a class, an interface, a delegate or a `T[]` -- and a `T?` over any of
them, which is counted exactly as what it encloses is: `Cell?` IS the `Cell` pointer, and
the null handle `0` is a release the runtime already treats as a no-op
([nullable.md](nullable.md)). A `T?` over a VALUE is counted too, as the box above: it is
released at the `}` that closes its block, with the object that holds it as a field,
element by element inside an `i64?[]`, with the closure that captured it, and at the store
that overwrites it — every one of those the rule that was already there. A value produced and handed
straight to a call — `f(new Cell(1))` — has no owner, so it is **parked** and released when
the statement that built it ends, which is C#'s and C++'s rule for a temporary. At most 64
such temporaries may be alive in one statement.

A function that returns a counted value hands the caller a reference it **already owns**:
the caller's slot takes it with no extra increment, and releases it in the ordinary way.

### The destructor runs first

At zero, the class's release function runs the destructor, then releases the class-typed
fields, then hands the block back. A chain runs the derived class's destructor before the
base's.

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

class Node {
    public Cell head;

    public Node(i64 x) {
        head = new Cell(x);
    }
}

i64 early(i64 want) {
    Cell outer = new Cell(1);
    {
        Cell inner = new Cell(2);
        if (inner.v == want) return outer.v;     // both released on the way out
    }
    return 0;
}

i64 main() {
    {
        Node n = new Node(5);
        if (rt_live() != 2) return 1;            // the node and the cell it built
        n.head = new Cell(6);                    // the old cell has no owner left
        if (dtors != 1) return 2;
        if (rt_live() != 2) return 3;
    }
    if (rt_live() != 0) return 4;
    if (dtors != 2) return 5;

    dtors = 0;
    if (early(2) != 1) return 6;
    if (dtors != 2) return 7;
    if (rt_live() != 0) return 8;

    return 42;
}
```

---

## What is not reclaimed

Declared debt, not silence — `rt_live()` counts all of it, so a program that mixes these
with classes sees a floor above zero rather than a wrong answer.

| | why |
|---|---|
| a `struct` allocation | a struct has no vtable, so there is no release function to reach and no count to keep |
| a `struct?` | the same: a nullable answers for the row it encloses, and a struct is not counted |
| a `static` field of class type | it holds its reference correctly, and lives for the whole run |
| a global, and a global `T[]` | a root by construction |
| a Singleton service | a root by design ([di.md](di.md)) |

---

## Reading the numbers

| call | answers |
|---|---|
| `rt_live()` | blocks handed out and not yet given back |
| `rt_used()` | how far the bump pointer has moved |
| `rt_peak()` | the high-water mark of the bump pointer |

They are ordinary functions of the runtime ([runtime.md](runtime.md)), and the fixtures use
them as the oracle of every reclaim claim on this page.

`i64? n = 5;` moves `rt_live()` by **one**: the box is an allocation, and this page counts
it rather than hiding it. A 32-byte box comes straight back to the 32-byte free list, so a
loop that churns `i64?` reuses one block and `rt_peak()` does not move at all —
`tests/surface_nullable_value.tk` asserts both.

---

## Panics

A panic writes `teko: <cause>` to standard error and exits **70**. There is no recovery and
no handler. `panic("...")` is a surface function a program may call itself.

| message | when |
|---|---|
| `call through a null delegate` | a delegate value that is `null` is called |
| `index into a null array` | an index into a `T[]` that is `null` |
| `index below zero into an array` | a negative index into a `T[]` |
| `index past the end of an array` | an index at or past `Length` |
| `a negative array length` | `new T[n]` with `n < 0` |
| `a nullable with no value` | `.Value` on a `T?` whose handle is 0 ([nullable.md](nullable.md)) |
| `interface dispatch on a class with no interface table` | an interface call on an object whose class declares none |
| `interface not implemented by this class` | the table has no row for that interface |
| `arena exhausted` | the 4 MiB block cannot serve the allocation |
| `allocation of a negative size` | `rt_alloc` was asked for a negative size |
| `reference count below zero` | a release too many: a bug, not a program condition |
| `too many temporaries of class type in one statement` | more than 64 unowned values in one statement |

```teko
// expect-exit: 70
#include "rt.tk"

class Cell {
    public i64 v;
}

i64 main() {
    Cell c = new Cell;
    if (c.v == 0) panic("nothing to do");
    return 42;
}
```

---

## Limits

| limit | value |
|---|---|
| the arena | 4 MiB, fixed |
| a recycled block | at most 256 bytes; anything larger is dropped when freed |
| temporaries of counted type in one statement | 64 |
| stores into a slot of counted type, per unit | 128 |
