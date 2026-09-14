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
| `Op? h = null;` | the null delegate: `null` needs a slot declared `T?` |
| `Op p = flag != 0 ? add : mul;` | a ternary of two function names, coerced arm by arm |
| `f = g;` | another value of the same delegate type |

Anything else is `teko: Op takes a function, another Op, or null`, and a function whose
signature does not match is refused by name. The thunk is generated once per (delegate,
function) pair.

`Op f = add;` and `Op g = new Op(add);` are the **same** thunk: the contextual form and the
explicit `new` form share one memoized wrapper per (delegate, function) pair, so the two
roads accept and refuse exactly the same targets. A parameter the delegate declares
`ref`/`out` travels through that wrapper as **the caller's own address**, not a copy, and the
signature has to match on the kind as well as on the type — which is checked once, on the
target, before any wrapper exists: `void byval(f64 x)` is `teko: byval does not match the
delegate Mut(ref f64)` (D63). The pointee is any type at all — a scalar, a class, a struct or
a primitive such as `DateOnly` — and the target may write through the reference (`b.v = 3`) or
rebind the caller's own slot (`b = new Box(7)`), exactly as a direct call does; a target whose
pointee is a DIFFERENT type is refused by the same signature check, `teko: fillc does not
match the delegate Fill(ref Box)`.

```teko
// expect-exit: 42
#include "rt.tk"

delegate void Mut(ref f64 x);
delegate void Setter(out i64 x);

void bumpf(ref f64 x) { x = x + 1.0; }
void seti(out i64 x) { x = 7; }

i64 run() {
    Mut m = new Mut(bumpf);
    f64 v = 1.0;
    m(ref v);
    if (v != 2.0) return 1;
    Setter s = new Setter(seti);
    i64 r = 0;
    s(out r);
    if (r != 7) return 2;
    return 0;
}

i64 main() {
    if (run() != 0) return 1;
    return 42;
}
```

### Which function a bare name names

A bare name on a delegate slot is resolved exactly as a **call** to that name would be
([namespaces.md](namespaces.md)): a local or a parameter in scope wins (and is never
wrapped — it is already a value), then the site's own namespace and its prefixes outward,
then a plain top-level declaration of the exact name, then the `using`s of the file. So
inside `namespace geo`, `Op f = col;` wraps `geo.col` when `geo` declares one, and the flat
`col` only when it does not — the same function `col(2, 3)` written one line over would
call.

```teko
// expect-exit: 42
#include "rt.tk"

delegate i64 Op(i64 a, i64 b);

i64 col(i64 a, i64 b) {                          // the flat one
    return a + b + 1000;
}

namespace geo {
    i64 col(i64 a, i64 b) {                      // geo.col, same signature
        return a * b;
    }

    i64 pick() {
        Op f = col;                              // geo.col, not the flat one
        return f(2, 3);
    }
}

i64 main() {
    if (geo.pick() != 6) return 1;
    return 42;
}
```

`new Op(name)` is the one form this order does **not** reach: its thunk is built while the
file is still being read, before namespaces are mangled, so the name it takes has to be one
that is already flat ([not-yet.md](not-yet.md)).

`Op g;` at top level is a **global** slot, and every form above works on it exactly as it
does on a local: `g = add;`, `g = new Op(mul);`, and `g(3, 4)` from any function of the
unit, the declaring one included (D51). It reads the same on the RIGHT of every slot that
takes a delegate — `h = g;`, `f(g)`, `return g;`, `Op m = g;` — where a *function's* name is
wrapped in a thunk and a delegate value is passed as it is. A global of any other type on
such a slot is the ordinary mismatch:

```teko
// no-run
delegate i64 Op(i64 a);
i64 n = 1;
i64 main() {
    Op h;
    h = n;                          // teko: Op takes a function, another Op, or null
    return h(1);
}
```

## Calling

`f(3, 4)` is an indirect call through the value's own code pointer. A **null** delegate
called is a panic with exit 70, never a segfault ([memory.md](memory.md)).

A delegate **field** is called wherever it can be read: through a receiver the parser
already types (`h.cb(x)` on a local), through one only the pass can type (a parameter, a
global, a field of either), by its bare name inside a method of the declaring type
(`cb(x)`, the unqualified spelling of `this.cb(x)`) and, for a `static` one, through the
type (`H.cb(x)`). A method of the same name still answers first on the bare-name and the
`Type.` roads, as it does in C#, and the class's own member — method, then field — answers
before any FREE function of that name, **in or out of a namespace**: inside `go`, `cb(x)`
is the field both when a top-level `i64 cb(i64)` is declared and when the class's own
namespace declares one. Outside the class that free function is untouched — including a
call written in the namespace but not in the class — and a local or a parameter named `cb`
still answers before either.

The call's **result carries the delegate's own return type wherever it is read** — an
initializer, an operand, an argument, and a `.` on it: `DOp f = mk; f().DayNumber` reads
`DateOnly`'s member, on a local, a parameter, a global slot, a field and a lambda bound to
a slot alike.

Its arguments are judged and converted exactly as a direct call's are — the count, the
`ref`/`out` kind, the pointee of a `ref`/`out` one, the type of one passed by value, and
C# §10.2.3's widening of an integer onto a float parameter — on all three roads a delegate
is called through: a LOCAL, PARAMETER or GLOBAL slot named directly, a class FIELD —
`h.cb(x)` — and an `Op[]` ELEMENT called at its index. See
[diagnostics.md](diagnostics.md#parameters-overloads-ref-out-and-params).

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
    h.cb = mul;                                  // a bare function name into a field (D62)
    if (h.cb(2, 3) != 6) return 8;

    Op? maybe = null;
    if (maybe != null) return 7;

    return f(20, 20) + g(1, 2);                  // 40 + 2
}
```

```teko
// expect-exit: 70
#include "rt.tk"

delegate i64 Op(i64 a, i64 b);

class NH {
    public Op cb;                                // never assigned: rt_alloc zeroes it
}

i64 main() {
    NH h = new NH;
    return h.cb(1, 2);                           // teko: call through a null delegate
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

A **contextual** lambda — one with no `new Op(...)` around it — is written wherever the
reader of the value is teko's own, which is every slot of delegate type the language has a
parse position for (D66):

| road | written |
|---|---|
| a declaration | `Op f = (i64 x) => x * 2;` |
| an instance field | `h.cb = (i64 x) => x * 2;` |
| a field, inside a constructor | `this.cb = (i64 x) => x * 2;` |
| a **static** field of a type declared **above** | `St.cb = (i64 x) => x * 2;` |
| an element of a **local** or field `T[]` | `fs[0] = (i64 x) => x * 2;` |
| an argument of a **method**, overloaded or not | `h.relay((i64 x) => x - 1, 43)` |

An `Op?` slot takes one exactly as an `Op` slot does — a declaration, a field, an element
and a **parameter** alike. At a method call the parameter's type is read per position, so
an OVERLOADED name takes one too wherever every candidate signature that could reach that
position spells the same delegate there; where they disagree (`mix(Op)` beside `mix(i64)`),
which signature applies is decided by the argument count, which the parser does not have
yet, and the argument needs the explicit `new Op(...)`.

Three positions still need the explicit `new Op(...)`, because mc's own parser owns them
and reads the `(` as a cast with no fallback: a `return`, an argument of a **free**
function, and an assignment to a bare name (a global, or a field written without `this.`).

Two more need it for a reason of teko's own: the value is parsed before the slot's TYPE is
known, so there is no delegate row to read the lambda against. A static field of a type
declared **below** the write (`LateH.scb = ...`) is read by the deferred reader the forward
pre-scan leaves behind — that scan reserves the type's WORD and never a row, so the type
has no fields yet — and an element of a **global** `T[]` (`Op[] g; g[0] = ...`) is read by
the deferred array write, since a global array may be declared below its own write and the
element type is only collected in a later pass. `docs/reference/not-yet.md` carries all
five with the reason.

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

A global, a `const` and a free function are read **live** and need no `use` at all — and so
is a **member** of the enclosing class, judged BEFORE any global of the same name ever gets
a look: a STATIC field, a member `const` and a STATIC property resolve with no `use` and no
receiver, exactly as they do for the method itself. An INSTANCE one — a field, a delegate
field called bare, a property, or a method (called or named bare) — needs `this`, which is
not implicitly captured (D11), so it is refused by the same sentence a plain global-shadowed
name already gets, `X is not captured; add it to use (...)`, except a bare method CALL,
which has a sentence of its own, `a method is not reachable from a lambda` — `use (...)`
captures a value, never a method (D70).

The WRITE side — a bare name on the LEFT of `=` (and `+=`/`++`, which lower to `=` before
this judge ever runs) — reads the identical table, before falling through as an ordinary
local or global assignment. A STATIC field or a STATIC property's `set` needs no receiver
and stores through it, the same coercion a plain static store already takes; a member
`const` has no slot to store into and is refused the const's own sentence,
`a constant is not assigned or called`. Every INSTANCE road — a field, a property, a method
name — needs `this`, not implicitly captured (D11), so it reads the identical
`X is not captured; add it to use (...)`: `(i64 x) => { n = x; }` inside a method, `n` a
field the class declares and a global of the same name standing beside it, used to write
the GLOBAL silently (D70's own adjacent finding, closed on the write door too).

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
    i64 got = 0;
    {
        BoxOp f;
        {
            Box b = new Box(42);
            f = new BoxOp(() use (b) => b.v);
            got = f();
        }                                        // the Box's own slot closes here
        if (released != 0) return 1;             // the closure keeps it alive
        if (rt_live() != 2) return 2;            // the Box and the closure
    }                                            // ...and the closure's closes here
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
