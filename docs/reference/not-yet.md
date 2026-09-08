# What v0.4.0 does not accept

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
| `sbyte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong` | not taught: ordinary identifiers, because teko registers none of the seven C# alias words — `i8`/`i16` are `type_new` primitives of their own, spelled `mc`'s way, not C#'s ([types.md](types.md#i8-and-i16)) |

Interfaces have no covariance and no contravariance, and a `struct` has no reference count
of its own ([memory.md](memory.md)).

## Numeric conversions

An integer converts to a float in every slot that has one, and nothing narrows back
([types.md](types.md#f32-and-f64)). What is still missing around it:

| written | what happens |
|---|---|
| `f64 d = s;` with an `f32` `s` | neither converted nor refused: the four bytes are read as eight, so the value is wrong. The two float widths convert to each other in neither direction |
| `7 % 2.5` | the remainder is not promoted — the integer operand stays one, and the float's bit pattern is read as an integer. `2.5 % 7` is `mc: no float remainder`, the backend having no float remainder instruction at all |
| `2.5 << 1`, `2.5 & 1` | a shift and the bitwise operators take no float in C# and are not promoted here either |
| an integer argument at a **virtual** or an **interface** call, written as a bare **parameter** name (`a.by(n)` inside `g(i64 n)`) | not converted: those two are shaped at parse time and a parameter carries no type the parser can read, so the argument passes its own bits |
| `(i64) p.w` on a field | `teko: i64 has no members: w` — the cast binds tighter than the `.`, so it reads as `((i64) p).w`. Write the load into a local first, `f64 v = p.w;` |
| `h.f = 5;` on a field of class/struct/interface/delegate type, the value written by a PARSE-TIME field store (`p.f = e`) | neither converted nor refused: `tk_check_field_store` (teko_struct.tk) reads the value through `tk_struct_of_expr`, which answers about an OBJECT expression only, so a scalar value silently answers "not known" and the store proceeds, writing the integer's bit pattern where a pointer is expected. D34 fixed the opposite direction (a reference reaching a NUMERIC field); this is the same defect class, the other way round, at this one site — every other numeric-into-reference slot (an initializer, an assignment, an argument, a `return`) already refuses through `tk_check_compat`'s row check |

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
| a **fixed** array of a class or struct type (an `enum` element is taught, N2a: it owns no object slot for `teko_rc.tk` to walk) | `teko: an array of objects is not taught yet; use a field array or wait for T[]` |
| reading a `ref T[]` / `out T[]` inside the callee | `expression with no codegen`, from the core — the parameter carries the caller's slot, and the array is not reachable through it |
| `.Length` on a **global fixed** array | `teko: unknown member: Length` — a local fixed array and any `T[]` answer |
| an index whose base is not an array the parse can name | ``teko: `[` needs an array`` |
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
| `params` on a method, a constructor, an interface signature or a `delegate` | ``teko: `params` is taught on a free function only`` |
| `params` not last, or two of them | ``teko: `params` must be the last parameter, and there is only one`` |
| `params` over anything but a `T[]` (`params xs`, `params i64 xs`) | ``teko: `params` names an array type: write `params T[] xs``` |
| `params` with `ref`/`out`, with a default, or on an `extern` | ``teko: a `params` list is not `ref` or `out```, ``teko: a `params` list has no default``, ``teko: an `extern` symbol takes no `params` list`` |
| `params` outside parameter position | ``teko: `params` declares a parameter list, nothing else`` |
| an argument that does not convert to the element type | `teko: a value of type X does not convert to T` — identity or derives/implements, an integer literal into any integer of the core, and an integer into a float ([types.md](types.md#f32-and-f64)) |
| two `params` lists of one name that a site cannot tell apart (`params u8[]` and `params u64[]` at `f(1)`) | `teko: more than one overload of X matches these arguments` |
| an integer argument on an **overloaded** name where no signature has an integer at that position (`f(f64)` beside `f(uptr)`, at `f(3)`) | `teko: no overload of X matches these arguments` — the rounds do not search for the conversion; a name declared **once** converts |
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

## Enums

`docs/specs/enum.md`'s N2a is the type, the members, the operators and `switch`; two later
crumbs of the same spec are not built yet:

| written | what happens |
|---|---|
| `.ToString()`, `.Parse()`, `.TryParse()`, `.IsDefined()`, `.GetNames()`, `.GetValues()` (N2b) | not taught: an enum value converts to `str` no way at all yet |
| `DateTimeKind` as an `enum` (N2c) | it is a `type_alias` over `i32` today ([datetime.md](datetime.md)); the crumb that makes it an `enum` is below, with the rest of `docs/specs/datetime.md` |
| `[Flags]` | not taught: teko has no attribute grammar; the bitwise operators already work on every enum, `[Flags]` only changes `ToString` |
| the bare member name inside a `switch` on that enum (`case Red:` instead of `case Color.Red:`) | an ordinary identifier, `mc: unknown name` — C# allows it, teko requires the qualified form everywhere |
| `switch` on an enum **parameter**, directly (`switch (c)` with `Color c` the enclosing function's own parameter) | the switch's own hidden local is declared from the parser's best guess at the subject's type (`tk_pty_of`, teko_struct.tk), which sees a DECLARED LOCAL but not a parameter (a parameter carries no type the parser can read at that point, the same limitation this page's own "Numeric conversions" section already names for a virtual/interface call argument), so the hidden local stays `i64`; `$t == Color.Red` then has one enum-typed operand, which `teko_ops.tk`'s own enum guard (D39 § "the operators") refuses before the scalar-compat check is ever reached — `teko: no operator \`==\` takes these operands`, not a conversion refusal. Assign the parameter to a local first, `Color d = c; switch (d) { ... }` |
| a unary `!`/`-`/`~` directly in front of a parenthesized expression that itself starts with a qualified constant (`!(Color.Red < Color.Green)`, `!(Shape.MAX < 10)`) | `mc: expected ) in cast` — the core's own cast detection, right after a unary prefix, does not backtrack past a type word followed by `.`; not this crumb's to fix (D2), and not new to `enum` (a plain class const in parens reproduces it too). Bind the qualified constant to a local first, or drop the outer parentheses when the operator allows it |

## `TimeSpan`, `DateTime`, and the rest of `docs/specs/datetime.md`

`TimeSpan` ([timespan.md](timespan.md)) and `DateTime` ([datetime.md](datetime.md)) are
built, and with them the primitive-member mechanism; the rest of the page's own crumbs are
not:

| written | what happens |
|---|---|
| `DateTime.Now`, `UtcNow`, `Today` (C6) | `teko: DateTime.Now is not taught yet` and its two siblings — a wall clock is one symbol per operating system (`clock_gettime`, `GetSystemTimeAsFileTime`) and `mc`'s `<sys>` is where it has to land; the ask is filed (`docs/specs/datetime.md` § 8/§ 14) |
| `d.ToString()`, `DateTime.Parse(s)`, `TryParse` | `teko: unknown member of DateTime` / `teko: unknown static member of DateTime` — the same text crumb `TimeSpan` waits for |
| `DateTime.SpecifyKind(d, k)`, `d.Subtract(x)` | `teko: unknown static member of DateTime: SpecifyKind` / `teko: unknown member of DateTime: Subtract` — both need a member row whose parameters differ from each other (a `DateTime` beside a `DateTimeKind`, and two overloads of one arity), and a row takes one parameter TYPE and a count. `SpecifyKind` is `new DateTime(d.Ticks, k)` and `Subtract` is `-` |
| `ToLocalTime`, `ToUniversalTime`, `DateTimeOffset`, `DateOnly`, `TimeOnly` | not taught: a time-zone database is not a language feature, and the two date-only types wait on [datetime-extras.md](../specs/datetime-extras.md) |
| `DateTimeKind` as an `enum` (N2c) | it is a `type_alias` over `i32`, so any integer lands in a `DateTimeKind` slot and `d.Kind` lands in an `i64` one. Turning it into a real `enum` only tightens that, and is a crumb of its own |
| `switch` on a `DateTime` | ``teko: no operator `==` takes these operands`` — a `switch` compares its subject against integer case labels, and a date takes no integer operand |
| `t.ToString()`, `TimeSpan.Parse(s)`, `TryParse` | `teko: unknown member of TimeSpan` / `teko: unknown static member of TimeSpan` — text is a crumb of its own, shared with the `enum` page's N2b, and no primitive has a `str` member yet |
| `t * 1.5`, `t / 1.5` (C#'s `operator *(TimeSpan, double)`) | ``teko: no operator `*` takes these operands`` — the spec's § 4 leaves the float multiply out |
| `t / t` (C# 7's `operator /(TimeSpan, TimeSpan)` → `double`) | ``teko: no operator `/` takes these operands`` |
| `+t` (C#'s unary plus) | ``teko: no operator `+` takes these operands`` — the unary minus is taught, its C# twin is not |
| `new TimeSpan(h, m, s)` and the two longer constructors | `teko: wrong number of arguments for new` — one row, one argument: the tick constructor. Build it from `FromHours(h) + FromMinutes(m) + FromSeconds(s)` |
| a primitive **global** as a `.` receiver (`g.Days` with `g` a global) | `teko: unknown member: Days` — the oracle types a local, a parameter, a `ref`/`out` pointee, a field and a call, and a scalar global is the one it does not see (the same gap `.Length` on a global fixed array already has, above), so the member falls through to the by-name search every type's members are asked about. Assign the global to a local first |
| an **array element** as an operand | the oracle answers "not known" for an array element ([types.md](types.md)). With a typed operand on the other side (`xs[i] + t`) the operator table claims the node and refuses it, ``teko: the type of the left side of `+` is not known here`` (or `right`); with an array element on BOTH sides (`xs[i] + xs[j]`) nothing claims it and the core's own `+` runs on the two raw values — for a `TimeSpan` the value is right and only the OVERFLOW CHECK is skipped, and for a `DateTime` it is wrong outright, `Kind` bits included. Bind the elements to locals first |
| `switch` on a `TimeSpan` | ``teko: no operator `==` takes these operands`` — a `switch` compares its subject against integer case labels, and a `TimeSpan` takes no integer operand |

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
