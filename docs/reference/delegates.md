# Delegates and lambdas

A `delegate` is a named function type. A value of that type is an object — a vtable, a
reference count and a code pointer — so it is counted and released exactly as a class
reference is, and a lambda is the same object with capture slots after the code pointer.

---

## Declaring

```
[public|internal] delegate T Name(T a, T b);
```

A delegate is declared at top level or inside a `namespace`, never inside a function body.
Its parameter list is at most **ten** parameters: an indirect call spends two of the
machine's twelve on the code pointer and the object itself.

## Taking a value

| written | means |
|---|---|
| `Op f = add;` | contextual: the function's name is wrapped in a thunk |
| `Op g = new Op(mul);` | the explicit form of the same |
| `Op h = null;` | the null delegate |
| `Op p = flag != 0 ? add : mul;` | a ternary of two function names, coerced arm by arm |
| `f = g;` | another value of the same delegate type |

Anything else is `teko: Op takes a function, another Op, or null`, and a function whose
signature does not match is refused by name. The thunk is generated once per (delegate,
function) pair.

## Calling

`f(3, 4)` is an indirect call through the value's own code pointer. A **null** delegate
called is a panic with exit 70, never a segfault ([memory.md](memory.md)).

```teko
// expect-exit: 42
#include "rt.tk"

delegate i64 Op(i64 a, i64 b);

i64 add(i64 a, i64 b) {
    return a + b;
}

i64 mul(i64 a, i64 b) {
    return a * b;
}

i64 apply(Op f) {                                // as a parameter
    return f(3, 4);
}

i64 apply(i64 k) {                               // overloaded against the delegate type
    return k;
}

Op choose(i64 which, Op a, Op b) {               // as a return type
    if (which != 0) return a;
    return b;
}

class H {
    public Op cb;                                // as a field
}

i64 main() {
    Op f = add;
    if (f(3, 4) != 7) return 1;

    Op g = new Op(mul);
    if (g(3, 4) != 12) return 2;

    if (apply(f) != 7) return 3;
    if (apply(9) != 9) return 4;

    Op picked = choose(0, f, g);
    if (picked(3, 4) != 12) return 5;

    H h = new H;
    h.cb = f;
    if (h.cb(2, 3) != 5) return 6;               // called where it is read

    Op maybe = null;
    if (maybe != null) return 7;

    return f(20, 20) + g(1, 2);                  // 40 + 2
}
```

```teko
// expect-exit: 70
#include "rt.tk"

delegate i64 Op(i64 a, i64 b);

i64 main() {
    Op f = null;
    return f(1, 2);                              // teko: call through a null delegate
}
```

---

## Lambdas

A lambda builds the same object at the site that writes it.

| form | written |
|---|---|
| explicit | `Op f = new Op((i64 x) => x * 2);` |
| contextual | `Op f = (i64 x) => x * 2;` |
| short, one parameter | `Op f = x => x + 1;` |
| block body | `Op f = new Op((i64 x) => { i64 y = x * 2; return y + 1; });` |
| with captures | `Op f = new Op((i64 x) use (k, &acc) => { acc = acc + x * k; return acc; });` |

A lambda is written wherever a value of its delegate type is expected: an initializer, an
assignment, a `return`, and an argument of a free function or of a method. `return
(i64 x) => e;` is not taught — write `return new Op((i64 x) => e);`.

### `use (...)` — captures are explicit

Nothing is captured implicitly. A name from the declaring scope enters the lambda only
through `use (...)`, and a name used in the body that is neither a parameter, a local of
the body, a capture, a global, a `const` nor a function is
`teko: X is not captured; add it to use (...)`.

| written | means |
|---|---|
| `use (k)` | **by value**: a copy is frozen into the object at construction |
| `use (&acc)` | **by reference**: the declaring local's own slot, read and written through its address |

A capture of a **counted** type (a class, an interface, another delegate, a `T[]`) is by
value and the closure holds a reference of its own — the object outlives the declaring
local and is released when the closure is. A capture **by reference** cannot leave the
scope that declared it: returning such a lambda, or storing it into a field, is refused.

A global, a `const` and a free function are read **live** and need no `use` at all.

```teko
// expect-exit: 42
#include "rt.tk"

delegate i64 Op(i64 a);

i64 apply(Op f, i64 x) {
    return f(x);
}

i64 glob = 0;

const i64 STEP = 5;

i64 bump_free(i64 x) {
    return x + 1;
}

i64 main() {
    Op nocap = new Op((i64 x) => x * 2);
    if (nocap(21) != 42) return 1;

    i64 k = 3;
    Op byval = new Op((i64 x) use (k) => x * k);
    k = 999;                                     // frozen at construction
    if (byval(5) != 15) return 2;

    i64 acc = 0;
    Op byref = new Op((i64 x) use (&acc) => { acc = acc + x; return acc; });
    if (byref(3) != 3) return 3;
    if (byref(4) != 7) return 4;                 // the declarer's own slot
    if (acc != 7) return 5;

    Op contextual = (i64 x) use (k) => x + k;    // no `new`
    if (contextual(1) != 1000) return 6;

    Op short_form = x => x + 1;
    if (short_form(41) != 42) return 7;

    if (apply(new Op((i64 x) => x - 1), 43) != 42) return 8;

    glob = 10;
    Op reads = new Op((i64 x) => bump_free(x) + STEP + glob);
    if (reads(0) != 16) return 9;                // 1 + 5 + 10, no `use` needed
    glob = 20;
    if (reads(0) != 26) return 10;               // the live global, not a frozen copy

    if (rt_live() != 6) return 11;               // six closures, still in scope
    return 42;
}
```

### Captures of a counted type

```teko
// expect-exit: 42
#include "rt.tk"

delegate i64 BoxOp();

i64 released = 0;

class Box {
    public i64 v;

    public Box(i64 x) {
        v = x;
    }

    ~Box() {
        released = released + 1;
    }
}

i64 main() {
    Box b = new Box(42);
    BoxOp f = new BoxOp(() use (b) => b.v);
    i64 got = f();

    b = null;
    if (released != 0) return 1;                 // the closure keeps it alive
    if (rt_live() != 2) return 2;                // the Box and the closure

    f = null;
    if (released != 1) return 3;
    if (rt_live() != 0) return 4;
    return got;
}
```

---

## Delegates and reference counting

A delegate value is counted like any other object: a local releases it at the end of its
scope, a field releases it when the object dies, and a delegate whose **return type** is a
class hands the caller a reference it already owns — no extra increment, and the caller's
slot releases it. Nothing about the reclaim is special-cased for delegates
([memory.md](memory.md)).

---

## Limits

| limit | value |
|---|---|
| parameters of a delegate | 10 (an indirect call spends two of the twelve) |
| captures, summed across every lambda still being read | 32 |
| a capture **by reference** of a counted type | not taught |
| capturing a **parameter** of the declaring function | not taught; capture a local initialized from it |
| a by-reference capture leaving its scope | refused (`return`, a field, a static field) |
| `return (i64 x) => e;` | not taught; `return new Op(...)` |
| `Func<>` / `Action<>` | not taught: there is no generic delegate |
| `+=` / `-=` on a delegate (multicast) | not taught: there is no invocation list |
| `op.Invoke(x)` | not taught: a delegate is called as `op(x)` |
| covariance / contravariance | not taught |
| (delegate, function) pairs wrapped in one unit | 64 |
| lambdas capturing at least one name by reference | 64 |
