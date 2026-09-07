# What v0.1.0 does not accept

The rule of the cut is **no silently wrong result**: a construct that is not taught is
refused where it is written, with a `teko: <cause>` naming it. This page is the list, with
the message each one answers.

Nothing here is a promise about a later version; what is designed and not built lives in
[`../specs/`](../specs/README.md).

---

## Types and declarations

| written | message |
|---|---|
| a type inside a type | `teko: a type is declared at top level; there is no type inside a type` |
| `partial` on a method | `teko: a partial method is not taught; only a partial class` |
| `abstract` in a `trait` | ``teko: `abstract` in a trait not taught yet`` |
| a `const` in a `trait` | `teko: a trait brings no const; only a class or struct declares one` |
| `insteadof` / `as` in a `use` block | ``teko: `insteadof`/`as` in a `use` block not taught yet`` |
| an operator on an `interface` | `teko: an operator is declared by a class or a struct, not by an interface` |
| a `const` local to a function | `teko: a local const is not taught; declare it at the top or as a member` |
| `var` | `teko: var not taught yet` |
| `match` | `teko: match not taught yet` |
| a standalone `when` | `teko: when not taught yet` |
| `type X = ...` | not taught: there is no type alias in the surface |
| a variant / discriminated union | not taught |

Interfaces have no covariance and no contravariance, and a `struct` has no reference count
of its own ([memory.md](memory.md)).

## Generics and delegates

| written | message |
|---|---|
| a qualified generic (`geo.Box<i64>`) | not taught: a generic declared in a namespace keeps its short name |
| a generic `delegate` | not taught |
| `Func<>` / `Action<>` | `teko: not a generic type` — there is no generic delegate to instantiate |
| `f += g` / `f -= g` on a delegate (multicast) | `teko: <Type> declares no operator +` / ``teko: no operator `+` takes these operands`` |
| `op.Invoke(x)` | not taught: a delegate value is called as `op(x)` |
| delegate covariance / contravariance | not taught |
| `return (i64 x) => e;` | not taught: write `return new Op((i64 x) => e);` |
| a capture **by reference** of a counted type | `teko: a capture by reference of a counted type is not taught yet` |
| capturing a **parameter** of the declaring function | ``teko: `use` captures a local; this name is not one`` |
| a by-reference capture returned or stored in a field | `teko: a lambda that captures by reference cannot leave its scope` |
| a lambda written against a `delegate` declared **below** it | ``teko: a delegate declared below `new` is not taught yet`` |

## Arrays

| written | message |
|---|---|
| `T[][]`, or any multidimensional array | `teko: an array of arrays is not taught yet` |
| a **fixed** array of a class or struct type | `teko: an array of objects is not taught yet; use a field array or wait for T[]` |
| reading a `ref T[]` / `out T[]` inside the callee | `expression with no codegen`, from the core — the parameter carries the caller's slot, and the array is not reachable through it |
| `params T[]` | not taught: a `params` list holds words |
| `.Length` on a **global fixed** array | `teko: unknown member: Length` — a local fixed array and any `T[]` answer |
| an inline array field through a **parameter** (`p.items[i]`) | ``teko: `[` indexes a `params` list only`` |
| an inline array field by its bare name | ``teko: an array field is reached through `this.``` |
| an inline array field of a type declared **below** | `teko: an array field on a type declared below is not taught yet` |
| a heap array as the element of another heap array | not taught |
| a `foreach` over a **global** array, or over a forward-declared source | `teko: not a known array` |

A run-time index into a **fixed** array is not guarded; every index into a `T[]` is
([arrays.md](arrays.md)).

## Namespaces and order of declaration

| written | message |
|---|---|
| a nested `namespace` | `teko: a nested namespace is not taught` |
| `using X = A.B;` / `using static` | not taught |
| a **bare** namespaced `const` as a `case` label (under a `using`) | `teko: a case label must be a constant expression` — the qualified form (`geo.N`) does resolve |
| a `global`, an `extern` or `main` inside a namespace block | `teko: a global is declared outside every namespace`, and its two siblings |
| a **qualified** base or interface declared **below** its use | `teko: unknown base class or interface` |
| a base class in **another** namespace, declared below | the same |
| a type used above an `#include "x.tk"` that declares it | `expected ; after expression`, from the core — the forward scan does not read across a raw `#include`, so the name is not a type there; use `import` |

## Parameters and calls

| written | message |
|---|---|
| a float argument to a `params` list | ``teko: a `params` list holds words; a float argument is not taught yet`` |
| a `params` element read as a float | ``teko: a `params` list holds words; its element does not read as a float`` |
| `params` outside parameter position | ``teko: `params` declares a parameter list, nothing else`` |
| an overload, a default or `&f` on a `params` function | ``teko: a `params` list cannot be overloaded``, ``teko: a `params` list has no default``, ``teko: a `params` function exists once per call site and has no address`` |
| two overloads differing only by `ref`/`out` | ``teko: two overloads differ only by `ref`/`out``` |
| a `ref` **parameter** repassed to an **overloaded** name | `teko: the type of argument N of X is not known here` |
| `f(out i64 a)` declaring the variable at the call site | not taught: declare it first |
| a `.` on a receiver the parse cannot type | ``teko: the type of the left side of `.` is not known here`` |
| `b.x += 1` on such a receiver | the same |

## `switch`

| written | message |
|---|---|
| a type pattern in a `case` | `teko: a case label must be a constant expression` |
| a `when` on the textually last `_` arm | ``teko: the last `_` arm of a switch expression cannot carry a `when``` |
| a switch expression with no `_` arm | ``teko: a switch expression needs a `_` arm`` |
| control falling out of a non-empty `case` | `teko: control cannot fall out of a case; end it with break` |

## Dependency injection

| written | message |
|---|---|
| a generic key (`IRepo<T>`) | not taught |
| a keyed or named registration, a factory, a decoration | not taught |
| `Services.Get<T>()` | there is no such surface: the form is a marker plus `inject` |
| an `abstract class` as a service | `teko: an abstract class is not a service` |
| a service that is not a class | `teko: a service marker names a class` |
| `inject` inside a lambda | `teko: inject inside a lambda takes the service from the enclosing scope; bind it outside and capture it with use (...)` |

---

The whole catalogue of messages, including the ones a well-formed program never sees, is
[diagnostics.md](diagnostics.md).
