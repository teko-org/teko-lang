# Delegates and lambdas

A `delegate` is a named function type, and a value of that type is an **object**: a
vtable, a reference count and a code pointer. It is counted and released exactly as a
class reference is; a lambda is the same object with the capture slots after the pointer.

`delegate i64 Op(i64 a);` is declared at top level or inside a namespace, never inside a
function body, and takes at most ten parameters.

## Taking a value and calling it

| written | means |
|---|---|
| `Op f = twice;` | contextual: the function's name is wrapped in a thunk |
| `Op g = new Op(twice);` | the explicit form of the same |
| `Op h = null;` | the null delegate |
| `Op p = flag != 0 ? add : mul;` | a ternary of two function names, coerced arm by arm |

`f(3)` is an indirect call through the value's own code pointer. A delegate is a type in
every position — a local, a field, a parameter, a return type, the element of a `T[]` —
and calling one that is `null` panics with exit **70**, never faults.

## Lambdas

| form | written |
|---|---|
| explicit | `Op f = new Op((i64 x) => x * 2);` |
| contextual | `Op f = (i64 x) => x * 2;` |
| short, one parameter | `Op f = x => x + 1;` |
| block body | `Op f = new Op((i64 x) => { i64 y = x * 2; return y + 1; });` |

A lambda goes wherever a value of its delegate type is expected: an initializer, an
assignment, an argument. `return (i64 x) => e;` is not taught — write `return new Op(...)`.

## Captures are explicit

Nothing is captured implicitly, PHP-style: a name from the declaring scope enters the
lambda through `use (...)` and nowhere else, while a global, a `const` and a free function
are read **live** and need no `use` at all.

| written | means |
|---|---|
| `use (k)` | **by value**: a copy is frozen into the object at construction |
| `use (&acc)` | **by reference**: the declaring local's own slot, through its address |

A capture of a **counted** type — a class, an interface, another delegate, a `T[]` — is by
value, and the closure takes a reference of its own: the object outlives the declaring
local and dies with the closure. A capture by reference cannot leave the scope that
declared it, so returning such a lambda or storing it in a field is refused.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

delegate i64 Op(i64 a);

i64 twice(i64 x) {
    return x * 2;
}

i64 apply(Op f, i64 x) {                         // a delegate as a parameter
    return f(x);
}

class Box {
    public i64 v;

    public Box(i64 x) {
        v = x;
    }
}

i64 main() {
    Op named = twice;                            // contextual: the name is wrapped
    Op made = new Op(twice);                     // the explicit form of the same
    Op lambda = (i64 x) => x + 1;
    Op shorter = x => x + 1;
    if (named(3) + lambda(1) + shorter(1) != 10) return 1;
    if (made(4) != 8) return 2;

    i64 k = 3;
    Op byval = new Op((i64 x) use (k) => x * k);
    k = 999;                                     // frozen at construction
    if (byval(5) != 15) return 3;

    i64 acc = 0;
    Op byref = new Op((i64 x) use (&acc) => { acc = acc + x; return acc; });
    if (byref(4) != 4) return 4;
    if (byref(6) != 10) return 5;                // the declarer's own slot
    if (acc != 10) return 6;

    Box b = new Box(20);
    Op reads = new Op((i64 x) use (b) => b.v + x);
    b = null;
    if (reads(2) != 22) return 7;                // the closure kept the Box alive

    if (apply(named, 10) != 20) return 8;
    return byref(0) + reads(2) + 10;
}
```

Calling a null delegate stops the program instead of faulting:

```teko
// expect-exit: 70
#include "rt.tk"

delegate i64 Op(i64 a);

i64 main() {
    Op f = null;
    return f(1);                                 // teko: call through a null delegate
}
```

There is no `Func<>`/`Action<>`, no multicast and no `op.Invoke(x)`; every form and every
limit is [delegates.md](../reference/delegates.md).
