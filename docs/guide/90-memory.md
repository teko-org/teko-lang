# Memory

There is no garbage collector and no `malloc`. One fixed arena of **4 MiB** hands out
16-byte aligned blocks, a free list per size class takes them back, and the compiler
injects a reference count per **scope**: an object is born at `new` and handed back the
moment the last name holding it stops. A loop that churns objects reuses one block
forever.

## Who owns what

"Counted" means a class, an interface, a delegate or a `T[]`; a `struct` has none.

| the holder | owns |
|---|---|
| a **local** of counted type | releases at the end of its block, and on the way out of a `break`, `continue` or `return` that leaves it |
| a **field**, or an element of a `T[]` | released when the holder dies, and when the slot is overwritten |
| a **capture by value** in a closure | released with the closure |
| a **global**, a **static field**, a **Singleton** | never: it is a root |
| a **parameter** of class type | **borrows** — it carries no count, and reassigning it is refused |

A value produced and handed straight to a call, `f(new Cell(1))`, has no owner: it is
parked and released when the statement ends. A function returning a counted value hands
the caller a reference it **already owns**. At zero the destructor runs **first**, then
the counted fields are released, then the block goes back to its free list; a chain runs
the derived class's destructor before the base's.

## What is not reclaimed

Declared debt, not silence — `rt_live()` counts all of it: a `struct` allocation (no vtable
means no release function to reach) and every root: a global, a `static` field of class
type, a Singleton.

`rt_live()` is the blocks handed out and not yet given back, `rt_used()` how far the bump
pointer moved, `rt_peak()` its high-water mark; the fixtures use them as the oracle of
every reclaim claim, and so does the program below.

## Panics

A guard writes `teko: <cause>` to standard error and exits **70**: a null delegate called,
an index into a null `T[]` or past its `Length`, a negative `new T[n]`, an interface call
on a class with no table, the arena exhausted. There is no recovery and no handler, and
`panic("...")` is a function a program may call itself.

## One program

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
        if (rt_live() != 2) return 1;            // the node, and the cell it built
        n.head = new Cell(6);                    // the old cell has no owner left
        if (dtors != 1) return 2;
    }
    if (rt_live() != 0) return 3;
    if (dtors != 2) return 4;                    // the node released its field too

    dtors = 0;
    if (early(2) != 1) return 5;
    if (dtors != 2) return 6;

    i64 i = 0;
    loop {
        Cell c = new Cell(i);                    // born and released every iteration
        i = i + 1;
        if (i >= 100000) break;
    }
    if (rt_live() != 0) return 7;
    if (rt_peak() > 4096) return 8;              // one block, reused a hundred thousand times
    return 42;
}
```

An index a `T[]` cannot answer stops the program where it stands:

```teko
// expect-exit: 70
#include "rt.tk"

i64 main() {
    i64[] xs = new i64[3];
    return xs[5];                                // teko: index past the end of an array
}
```

The arena, the object layout and every message: [memory.md](../reference/memory.md),
[runtime.md](../reference/runtime.md).
