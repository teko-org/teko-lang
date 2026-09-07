# Parameters, overloads, `ref`/`out` and `params`

Four things a parameter list does beyond naming types: carry a default, tell one overload
from another, pass by reference, and take a list whose length the call site decides. A
function takes at most **12** parameters, a method's receiver being one of them.

## Defaults

A default belongs to the **declaration** and the call site fills it in — on a free
function, a method, a constructor or an interface signature. It is a constant expression,
folded where it is declared, and no parameter without a default may follow one that has
one. A `virtual`/`override` pair keeps one slot and each end carries its own default, so
a call through a base-typed name takes the base's default and still runs the override.

## Overloading

One name, several signatures, resolved by **arity first and then by argument types**; the
return type never takes part, and an integer literal is an `i64`. A call nested in another
is resolved first, so its return type types the outer site. Two candidates fitting one
site is refused as ambiguous, and so are two signatures differing only by `ref`/`out`.

## `ref` and `out`

C#'s by-reference parameters, and **the call site says so too**: `bump(ref a)`,
`split(57, out hi, out lo)`. The argument is a name — a local, a parameter, a field or an
array element — never an expression and never a call's result.

| | `ref` | `out` |
|---|---|---|
| the callee reads the caller's variable | yes | not required |
| the callee must assign it | no | yes; never assigning it is refused |
| a counted pointee starts released | no | yes, so the first assignment frees nothing |

The pointee may be narrower than a word (`u8`, `i32`) or a float, and it may be passed on
to another `ref`/`out` parameter as many levels down as you like. A parameter carrying
`ref` or `out` takes no default.

## `params`

`params i64[] xs` is C#'s own: a **modifier** before the type, a genuine `T[]` after it, on
the last parameter of a free function. Inside the body `xs` is an ordinary array —
`xs.Length`, `xs[i]`, `foreach`, passing it on. At the call site the compiler builds the
array out of the arguments, none of them being a limit; a single argument that is already a
`T[]` passes straight through. The element may be a float, or a class, and the array is
released at the end of the statement that built it. A fixed parameter may come first, and a
variadic call may sit inside another one.

## One program

```teko
// expect-exit: 42
#include "rt.tk"

i64 add(i64 a, i64 b = 10) {
    return a + b;
}

i64 pick(i64 n) {
    return n + 1;
}

i64 pick(i64 a, i64 b) {                     // overloaded by arity
    return a * b;
}

void bump(i64 x) {
    x = x + 1;                               // by value: the caller sees nothing
}

void bump(ref i64 x) {
    x = x + 1;                               // overloaded on `ref`
}

void split(i64 v, out i64 hi, out i64 lo) {
    hi = v / 10;
    lo = v % 10;
}

i64 total(params i64[] xs) {
    i64 s = 0;
    i64 i = 0;
    loop {
        if (i >= xs.Length) break;
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

f64 ftotal(params f64[] xs) {                // a float element travels as a float
    f64 s = 0.0;
    i64 i = 0;
    loop {
        if (i >= xs.Length) break;
        s = s + xs[i];
        i = i + 1;
    }
    return s;
}

i64 main() {
    if (add(1) != 11) return 1;              // the declaration's own default
    if (add(1, 2) != 3) return 2;
    if (pick(9) != 10) return 3;
    if (pick(3, 4) != 12) return 4;

    i64 a = 1;
    bump(a);
    if (a != 1) return 5;
    bump(ref a);                             // the call site says `ref` as well
    if (a != 2) return 6;

    i64 hi = 0;
    i64 lo = 0;
    split(57, out hi, out lo);
    if (hi != 5) return 7;
    if (lo != 7) return 8;

    if (total() != 0) return 9;              // an array of length 0
    if (total(1, 2, 3) != 6) return 10;
    if ((i64) ftotal(1.5, 2.5) != 4) return 11;

    i64[] ready = new i64[2];
    ready[0] = 4;
    ready[1] = 3;
    if (total(ready) != 7) return 12;        // the normal form: no copy

    return add(1) + pick(3, 4) + total(10, 7) + a;
}
```

A list is one signature among the name's: `f(i64)` beside `f(params i64[])` is C#'s own
pair, and `f(1)` is the first — a candidate that takes the arguments as written wins, and
only then is a list asked to swallow the tail. `params` on a method or a constructor, and
`f(out i64 a)` declaring the variable at the call site, are not taught; the full account is
[parameters.md](../reference/parameters.md).
