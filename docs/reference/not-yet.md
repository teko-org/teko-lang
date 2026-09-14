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
| `(i64) p.w` on a field | `teko: i64 has no members: w` — the cast binds tighter than the `.`, so it reads as `((i64) p).w`. Write the load into a local first, `f64 v = p.w;` |

## Generics and delegates

| written | message |
|---|---|
| a qualified generic (`geo.Box<i64>`) | not taught: a generic declared in a namespace keeps its short name |
| a generic `delegate` | not taught |
| `Func<>` / `Action<>` | `teko: not a generic type` — there is no generic delegate to instantiate |
| `f += g` / `f -= g` on a delegate (multicast) | `teko: <Type> declares no operator +` / ``teko: no operator `+` takes these operands`` |
| `op.Invoke(x)` | not taught: a delegate value is called as `op(x)` |
| delegate covariance / contravariance | not taught |
| a **contextual** lambda (no `new Op(...)`) in one of the three positions mc's own parser owns: `return (i64 x) => e;`, an argument of a **FREE** function (`apply((i64 x) => e, 1)`), and an assignment to a bare name (`g = (i64 x) => e;` on a global, `cb = (i64 x) => e;` on a field written without `this.`) | `expected ) in cast`, from the core: `(` opens a cast or a group there and the disambiguation has no fallback. The lookahead that decides a lambda needs a reader of teko's own, and those three positions have none — `parse_stmt_core` refuses to hand a core keyword like `return` to `syntax_stmt`, `parse_call` reads a free call's arguments itself, and a bare-name assignment falls to `parse_expr` at the bottom of the statement parser. Write `new Op((i64 x) => e)`, which is typed where it stands (D66). Every other slot of delegate type takes the contextual form: a declaration, an instance field, `this.f = ...` in a constructor, a static field, a `T[]` element and a METHOD's own argument |
| a **contextual** lambda as the argument of an INTERFACE method call (`i.take((i64 x) => e)` on `interface Taker { i64 take(Op f); }`) | `expected ) in cast`, from the core: the interface call reads its arguments with the untyped `tk_args` ([teko_expr.tk](../../teko_expr.tk)), not the per-position reader a class method call has (D66). Write `new Op((i64 x) => e)`, or call through a class that implements the interface (measured, D66's verifier) |
| a **contextual** lambda into a STATIC field of a type declared BELOW the write (`LateH.scb = (i64 x) => e;`) | `expected ) in cast`, from the core: the store is read by the deferred reader the forward pre-scan leaves behind (`tk_fwd_defer_static`, [teko_access.tk](../../teko_access.tk)), and the scan reserves the type's WORD and never a row of the type table — creating rows at scan time would reorder every interface's vtable slot — so the field has no declared type yet when its value is parsed, and there is no delegate row to read the lambda against. The resolver that learns the type runs after parsing, too late to re-read anything. Write `new Op((i64 x) => e)`, or declare the type above the write (measured, D66) |
| a **contextual** lambda into an element of a GLOBAL `T[]` (`Op[] g; g[0] = (i64 x) => e;`) | `expected ) in cast`, from the core: a bare-name index write is deferred whole (`tk_arr_defer_write`, [teko_array.tk](../../teko_array.tk)) because a global array may be declared BELOW its own write — measured, and accepted today — so the element type is only collected in a later pass (`tk_hg_collect`) and the value has already been parsed by then. A LOCAL or field `T[]` is read by `tk_ha_index`, which has the element type in hand, and takes the contextual form. Write `new Op((i64 x) => e)` (measured, D66) |
| a **contextual** lambda at an argument of an OVERLOADED method whose candidates DISAGREE at that position (`mix(Op)` beside `mix(i64)`) | `expected ) in cast`, from the core: which signature applies is decided by the argument COUNT, which the parser does not have while it is still reading that position, so a position the candidates do not agree on has no delegate row to read against. Where every candidate that reaches the position spells the SAME delegate, the contextual form is taken (D66). Write `new Op((i64 x) => e)` (measured, D66) |
| a capture **by reference** of a counted type | `teko: a capture by reference of a counted type is not taught yet` |
| capturing a **parameter** of the declaring function | ``teko: `use` captures a local; this name is not one`` |
| a by-reference capture returned or stored in a field | `teko: a lambda that captures by reference cannot leave its scope` |
| a by-reference capture handed to a PARAMETER that the callee stores in a slot outliving the caller — `void sink(Op p) { g_op = p; }`, called `sink(held)` | not caught: the taint does not cross a call. The escape is an intra-function rule (D42), and the call itself is the ordinary way to use a delegate — `forEach(xs, new Op((i64 x) use (&sum) => ...))` is the same shape, and a rule that refused the argument would refuse it too. Carrying the verdict into the callee needs a qualifier on the parameter's own type, which is a design, not a patch (D51, sixth pass) |
| a lambda written against a `delegate` declared **below** it | ``teko: a delegate declared below `new` is not taught yet`` |
| a lambda inside a METHOD, reading an INSTANCE field/property, calling an INSTANCE method or an INSTANCE delegate field, or naming an INSTANCE method bare with no call around it, of the enclosing class (`(i64 x) => x + n` where `n` is a field, not a global) | `teko: n is not captured; add it to use (...)` for a field, a delegate field call, a property or a bare method name, `teko: a method is not reachable from a lambda` for a method call — `this` is not implicitly captured (D11 stands), and giving a lambda one is a design of its own: a closure holding a counted `this` opens a reference cycle the reclaim (`teko_rc.tk`) does not break on its own, an open fork rather than a patch here. A STATIC member of the class needs no receiver and resolves the same way it does for the method itself (D70, `tests/refuse/lambda_field_name.tk`, `tests/refuse/lambda_deleg_field_call.tk`, `tests/refuse/lambda_prop_name.tk`, `tests/refuse/lambda_method_call.tk`, `tests/refuse/lambda_method_name.tk`) |
| unwrapping a slot declared `T?` of delegate type with `??` (`Op h = g_maybe ?? addOne;`) | `teko: Op takes a function, another Op, or null` — the validator on the `Op` slot reads the coalesce as a value it cannot name. Calling one is the row under *Nullable* below; what a `T?` delegate does carry today is the store, the comparison against `null` and the read back through `!= null` (measured, D51 twelfth pass) |
| a bare FUNCTION name as an ARGUMENT at a call through a delegate slot whose parameter is itself of delegate type (`r(twice)` on a `delegate i64 Runner(Op f)`) | `teko: the type of this argument is not known here` — the wrap that turns a function name into a delegate object is written at a call that NAMES its callee, and an indirect call names none. Write `r(new Op(twice))`, which is typed where it stands. A DIRECT `apply(twice)` on the same `Op` parameter is wrapped and runs |
| a field of **non-delegate** type called by its bare name inside a method (`n(1)` on an `i64 n`) | `call to unknown function`, from the core — a bare name is rewritten into a call only where it can become one, and for a field that means a delegate row. A field of delegate type IS called that way ([delegates.md](delegates.md#calling)); every other field is left to the resolver, which finds no function of that name. Write the arithmetic the site meant, or give the field a delegate type |
| a bare FUNCTION name stored into a slot declared `T?` of delegate type (`Op? h = addOne;`, `xs[0] = addOne` on an `Op?[]`, `h.cb = addOne` on an `Op? cb` field) | `unknown name` from the core at an initializer, `teko: the type of this value is not known here` at an element and at a field store: the wrap that turns a function name into a delegate object reads the delegate row, which a `T?` row answers -1 for. Write `new Op(addOne)`, which is typed where it stands (measured, D51 twelfth pass; the field road takes the same -1 at the same door, D62) |
| calling a delegate through an element of an ARRAY FIELD — the index of `public Op cbs[2]` followed straight by a call's own argument list | `expected ) after condition`, from the core's own parser: the `(` right after `]` is not read as a call on that road. The STORE through it is taught (`h.cbs[0] = add;`, D62); read the element into a local first, `Op f = h.cbs[0];` and call `f`, which is typed where it stands (measured, identical on `441be45a`, D62) |

## Arrays

| written | message |
|---|---|
| `T[][]`, or any multidimensional array | `teko: an array of arrays is not taught yet` |
| a **fixed** array of a class or struct type (an `enum` element is taught, N2a: it owns no object slot for `teko_rc.tk` to walk) | `teko: an array of objects is not taught yet; use a field array or wait for T[]` |
| reading a `ref T[]` / `out T[]` inside the callee | `expression with no codegen`, from the core — the parameter carries the caller's slot, and the array is not reachable through it |
| `.Length` on a **global fixed** array | `teko: unknown member: Length` — a local fixed array and any `T[]` answer |
| an index whose base is not an array the parse can name, a **local**, a **field**, a member **const** and a **property** of the class being parsed shadowing a global array included | ``teko: `[` needs an array`` (the base's own name follows it) |
| `&n` or a `ref n` / `out n` argument on a **bare member name** shadowed by a same-named global, in a method or in a lambda alike (`n` a field, static or instance) | not refused: the address-of road has no member judge — the name under `&` takes the global's own address, so the read or the write lands on the global (measured on `bcee28f2` and after D70, identical in a plain method and inside a lambda; D70's judge covers the read, the call and the assignment of a bare name, not its address). Write `this.n` / `H.n`, or rename one of the two (found by the verifier of D70) |
| a **member declared BELOW the method that reads it** shadowing a global array (`src[0]` above `public i64 src;`, and the same over a member `const` or a property) | not refused: the parser has not read the member yet, so the index takes the global road and the name under it is the member's — the program compiles and runs wrong (measured: exit 139 when the member's own bytes are read as the array's handle, exit 70 — `teko: index into a null array` — when the member is still zero and the trap catches it). Declare the member above its readers, which is what every other implicit use of it already asks for, or rename one of the two |
| a **parameter** that shadows a **global array** (`f(i64 src)` beside a global `i64[] src`) | not refused: the index is rewritten into a read of the GLOBAL, while the name under it is still the parameter's — the program compiles and runs wrong (measured, exit 139). A parameter is in no parse-time scope, which is where the binding is known; shadow it with a LOCAL and the site is refused instead. Rename one of the two |
| an inline array field by its bare name | ``teko: an array field is reached through `this.``` |
| an inline array field of a type declared **below** | `teko: an array field on a type declared below is not taught yet` |
| a heap array as the element of another heap array | not taught |
| a `foreach` over a **global** array, or over a forward-declared source | `teko: not a known array` |

A run-time index into a **fixed** array is not guarded; every index into a `T[]` is
([arrays.md](arrays.md)).

## The nullable `T?`

`T?` is taught over ANY type — a class, an interface, a delegate, a `T[]`, a `struct`, and
every value type through the counted box ([nullable.md](nullable.md)). What is not taught
around it:

| written | message |
|---|---|
| `T??` | `teko: a nullable of a nullable is not taught` |
| `uptr?`, `ptr?`, `str?` | `teko: a raw pointer has no nullable` |
| `void?` | `teko: void? is not a type` |
| `Nullable<T>` spelled out | `teko: not a generic type` — the spelling is `T?` and there is no `Nullable` type word |
| `Box<Cell?>`, a nullable as a generic argument | `teko: a nullable is not a generic argument yet` — a type argument travels as a spelling, and `Cell?` is not one the lexer can form |
| `a \|\| b ?? c` | **accepted, and read as `(a \|\| b) ?? c`** where C# reads `a \|\| (b ?? c)`. `mc`'s Pratt table starts at 1 with `\|\|` and `syntax_infix` refuses a precedence outside 1..100, so `??` ties with `\|\|` and the ternary instead of sitting between them; renumbering the table is `mc`'s base grammar (D3). It is a type error in almost every program that writes it — `\|\|`'s operands are truth values and `??`'s left operand is a nullable, so what actually prints is `teko: ?? needs a nullable on the left`. Write `a \|\| (b ?? c)` |
| `a ??= b` | `teko: ??= is not taught` — `??=` is no lexeme of its own, and `a = a ?? b;` is the form |
| `a?[i]`, C#'s null-conditional index | `expression expected`, from the core: `?` and `[` are two tokens, so the `?` reads as the ternary's and the `[` is where its first arm should be. `a?.items[i]` is refused too, `teko: ?. reads a member, not an element: items` |
| `(a ?? b).m`, `a?.b.c`, `a?.b.Value` — a plain `.` on the result of either operator | `teko: bind the ?? or ?. result to a variable before reading a member`. `.` and `?.` share precedence 12, so `a?.b.c` would read as `(a?.b).c` where C# short-circuits the whole chain; the type of the result is also only known at the rewrite, so the `.` would resolve by member name alone. `a?.b?.c`, or a local of its own, is the form |
| `a ?? b` where `b` is a nullable of ANOTHER row (`Shape? ?? Circle?`) | `teko: a value of type Circle? does not convert to Shape` — the right side is either the same `T?` row or a value that fits `T`; there is no covariance between nullable rows anywhere |
| `a == b` on two nullables, `a == 5`/`a == c` (a nullable against a plain value of what it encloses) | ``teko: Cell? declares no operator `==` `` — the handle is not the value, so only `null` is the other side a nullable may compare against |
| `a + b`, `a < b`, `a & b` on nullables (C#'s lifted operators), reference or value, either operand a plain value of the enclosed type or another nullable | ``teko: Cell? declares no operator `+` `` — every operator but `==`/`!=` against `null`; `a.Value + b.Value` is the form |
| `T?` to `U?` where `T` converts to `U` | `teko: a value of type Circle? does not convert to Shape?` — no covariance between nullable rows; write `x.Value` |
| `x.GetValueOrDefault()` on a reference nullable | `teko: a reference nullable has no default` — `default(T)` for a reference is `null`, which is the one value a `T` slot may not take |
| `x.GetValueOrDefault(fallback)`, C#'s one-argument overload | `teko: unknown member of i64?: GetValueOrDefault` — an arity this type does not have. `??` (Q2) is the form that says which default it means |
| `f(5)` where the only candidate that could take it is `f(i64?)` and another overload exists | `teko: no overload of f matches these arguments` — the overload rounds match by exact type, an integer literal and `null`; the implicit `T` → `T?` is not one of them. A name declared ONCE takes the wrap and needs no round at all |
| `k > 0 ? 5 : null` in an `i64?` slot | `teko: the two arms of ?: have different types` — the ternary types its arms against each other, and `null` is a `uptr`. Write two arms of the same type, or two statements |
| `n = 5;` on a PARAMETER declared `i64?` | `teko: a parameter of class type is borrowed; it is not reassigned` — a box is counted, so the rule every counted parameter already lives by (K2) reaches it. Declare a local |
| a GLOBAL declared `T?` over a **value** (`i64? n;` at the top of a file) | `teko: a global does not hold a nullable box` — the box that type promises is built by the rc pass out of the SCOPE a local lives in, and a global has none, so the slot would hold the bare value. Refused at the declaration (D48). A `T?` over a **reference** is a global like any other: its handle IS the pointer, and `.HasValue`/`.Value`/`??`/`?.` on one read exactly as a local's do |
| `x.Value = e` | `teko: .Value is not a slot` |
| `c.v` on a `Cell?` (flow narrowing, C# 8's `if (c != null) { c.v }`) | `teko: a Cell? is read through .Value` — the analysis behind narrowing is a dominator pass this design does not buy |
| `f(3, 4)` on an `Op?` local or PARAMETER | `call to unknown function f`, from the core — a nullable delegate is a value to compare and to pass, not one to call, and `.Value(...)` is not taught either |
| `x is null`, `case null:` | there is no `is` in teko, and a `switch` takes no reference subject at all |
| `if (a)`, `a ? x : y` on a bare `T?` | `teko: i64? is not a condition` (named by the row; `bool?` prints as `u8?`) — the handle answers `HasValue`, and for a boxed value `false` is still a live box, so a bare condition would run the branch its own value refuses. `a.HasValue`, `a == null` or `a.Value` is the form |
| `while (a)`, `for (…; a; …)`, `do … while (a);` on a bare `T?` | ``teko: i64? declares no operator `!` `` — a loop's guard is `!(cond)`, and the unary refusal is reached first |
| `x.ToString()` on a nullable | deferred with all text |
| a `switch` whose subject is a `Cell` or a `Cell?` | **accepted and compared as a pointer**, which no `case` label can match. Pre-existing for every reference, not a nullable's own |

### What definite assignment lets through

The rule ([nullable.md](nullable.md) § Definite assignment) refuses a local read with **no**
assignment anywhere earlier in the body, and nothing more. It over-approximates on purpose,
so it can never refuse a correct program; the price is the list below, every row of which
**compiles** and reaches the run with whatever the slot held. C# refuses most of them, with
a dominator pass nobody has asked for here.

| written | what happens |
|---|---|
| `Vec v; if (c) { v = new Vec(); } v.x = 4;` | **accepted.** One arm assigns, and the rule does not ask which arm runs. This is the shape D42 called legitimate, and buying the refusal would cost the shape |
| `Vec v; loop { v = new Vec(); break; } v.x = 4;` | **accepted**, for the same reason: an assignment inside a loop body counts without the loop being unrolled |
| `i64 a; { a = 1; } i64 b = a;` | **accepted**, and correct — the block runs. A nested block is not distinguished from the body |
| `Op f = (i64 x) use (a) => x + a;` on an `i64 a;` never assigned | **accepted.** A capture counts as an assignment: what a closure does with the name is the lambda's business, not this rule's |
| a field, a global, a parameter, an element of `new T[n]` | **not judged at all.** Three of the four are zeroed by construction, and a parameter arrives with a value |
| `Point gp; ... gp.x` — a struct or class GLOBAL read through a field before anything ever built it | **not judged, and not zero-safe the way a field is**: a global slot IS the pointer, zero at load, so the read dereferences a null pointer and the process ends with **exit 139** (SIGSEGV) rather than reading `0`. D42's ruling reaches a global exactly as it reaches a local's own unguarded read: unguarded, it is the developer's error, and this design does not buy the analysis that would catch it either way. Build the global (`gp = new Point();`) before the first read |
| more than 256 locals in one function | **the rule steps aside for that function**, rather than judge a name against a table that could not hold its declaration |
| the position of a refusal on `x++` / `x--` / `x += k` as a STATEMENT | reported at `<teko-loop-prelude>`, the `#rule` that lowers those three, instead of at the source line. Pre-existing for **every** diagnostic raised on a node that rule builds (``teko: no operator `+` takes these operands`` on `t++` prints the same file), not this rule's own |

## Namespaces and order of declaration

| written | message |
|---|---|
| a nested `namespace` | `teko: a nested namespace is not taught` |
| `using X = A.B;` / `using static` | not taught |
| a **bare** namespaced `const` as a `case` label (under a `using`) | `teko: a case label must be a constant expression` — the qualified form (`geo.N`) does resolve |
| a `global`, an `extern` or `main` inside a namespace block | `teko: a global is declared outside every namespace`, and its two siblings |
| a **qualified** base or interface declared **below** its use | `teko: unknown base class or interface` |
| a base class in **another** namespace, declared below | the same |
| a `T[]` or a `T?` on a SHORT type name inside a `namespace`, at a parameter or a return | `name expected`, from the core — the short-name reader answers the type without going through `p_type()`, so no type suffix is ever read at that position. Write the declaration outside the namespace |
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
| two **method** overloads of the same arity (`i64 pick(i64)` beside `i64 pick(f64)` in one class) | `teko: ambiguous overload; two signatures take this many arguments: pick` — a method call picks by argument COUNT alone, and the argument types that would tell two signatures of one count apart are not asked (`tests/surface_overload_method.tk`). A FREE function overloads by type as well |
| a `.` on a receiver the parse cannot type | ``teko: the type of the left side of `.` is not known here`` |
| `b.x += 1` on such a receiver | the same |
| a **user operator** in a ternary arm, on the right of `??`, or behind `?.` | `teko: the two arms of ?: have different types`, `teko: a value of type Vec does not convert to i64`, ``teko: ?. needs a nullable on the left`` — one message per shape, all three for the same reason: the ternary, `??` and `?.` are rewritten two passes AHEAD of the operator pass, so `v + 1` is still an `N_BINARY` there and the oracle reads it by the core's own left-operand rule (the type of `v`) instead of by the return type of `operator+`, which is not written yet. Bind it to a local first, `i64 sum = v + 1;` |
| an **overloaded call** as the SUBJECT of a `switch` | ``teko: no operator `==` takes these operands`` — the subject's temporary is typed at PARSE time, where the unit is not complete and no overload table exists, so `switch (pick(1))` types it by the FIRST declaration of `pick` and every `case` label of the picked overload's own type then disagrees with it. Bind the call to a local first, `i64 p = pick(1);`. Everywhere a PASS types the call — an operator, a ternary arm, `??`, a primitive row's argument, an initializer, and since D61 a `.` on the call's result — the pick is what answers (D49) |

## `switch`

| written | message |
|---|---|
| a type pattern in a `case` | `teko: a case label must be a constant expression` |
| a `when` on the textually last `_` arm | ``teko: the last `_` arm of a switch expression cannot carry a `when``` |
| a switch expression with no `_` arm | ``teko: a switch expression needs a `_` arm`` |
| control falling out of a non-empty `case` | `teko: control cannot fall out of a case; end it with break` |

## Enums

`docs/specs/enum.md`'s N2a is the type, the members, the operators and `switch`; N2b is
`ToString`, `Parse`, `TryParse` and `IsDefined` — all four taught
([types.md](types.md#tostring-parse-tryparse-isdefined)). What is left of § 6 and one later
crumb of the same spec are not built yet:

| written | what happens |
|---|---|
| `.CompareTo(other)`, `.Equals(other)` | `teko: unknown member of Color` — C#'s ordering/equality members; the ordinal comparison itself is already free through `<`/`<=`/`>`/`>=`/`==`/`!=` (N2a), so these two would be a thin wrapper over the same operators and are left for the crumb that adds `IComparable`/`IEquatable` conformance generally, rather than one-off for `enum` |
| `Color.GetNames()` → `str[]`, `Color.GetValues()` → `Color[]` | `teko: Color has no member GetNames` — both need a HEAP array (`T[]`) built and filled at compile time from the two globals `tk_enum_ensure` already writes (`Color__names`, `Color__vals`); the array machinery itself is proven for `str[]`/`Color[]` (`docs/specs/enum.md` § 6), but filling one from a fixed-size global inside the generated body is not measured yet, so it stays out rather than land unproven |
| `[Flags]`-style `ToString` (`Perm.Read \| Perm.Write` printing `"Read, Write"`) | not taught: teko has no attribute grammar, so there is no `[Flags]` to key the decomposition on; `.ToString()` always answers a single name or the digits (C#'s own behaviour for a value with no `[Flags]`) |
| `[Flags]` | not taught: teko has no attribute grammar; the bitwise operators already work on every enum, `[Flags]` only changes `ToString` |
| the bare member name inside a `switch` on that enum (`case Red:` instead of `case Color.Red:`) | an ordinary identifier, `mc: unknown name` — C# allows it, teko requires the qualified form everywhere |
| `switch` on an enum **parameter**, directly (`switch (c)` with `Color c` the enclosing function's own parameter) | the switch's own hidden local is declared from the parser's best guess at the subject's type (`tk_pty_of`, teko_struct.tk), which sees a DECLARED LOCAL but not a parameter (a parameter carries no type the parser can read at that point — the same limitation a virtual or interface call's argument used to carry, which D57 closed by DEFERRING the judgement to the pass instead of guessing at it, and which a `switch` subject cannot take the same way: the hidden local is DECLARED at parse time, so there is no later point at which its type could still be chosen), so the hidden local stays `i64`; `$t == Color.Red` then has one enum-typed operand, which `teko_ops.tk`'s own enum guard (D39 § "the operators") refuses before the scalar-compat check is ever reached — `teko: no operator \`==\` takes these operands`, not a conversion refusal. Assign the parameter to a local first, `Color d = c; switch (d) { ... }` |
| a unary `!`/`-`/`~` directly in front of a parenthesized expression that itself starts with a qualified constant (`!(Color.Red < Color.Green)`, `!(Shape.MAX < 10)`) | `mc: expected ) in cast` — the core's own cast detection, right after a unary prefix, does not backtrack past a type word followed by `.`; not this crumb's to fix (D2), and not new to `enum` (a plain class const in parens reproduces it too). Bind the qualified constant to a local first, or drop the outer parentheses when the operator allows it |

## `TimeSpan`, `DateTime`, and the rest of `docs/specs/datetime.md`

`TimeSpan` ([timespan.md](timespan.md)) and `DateTime` ([datetime.md](datetime.md)) are
built, and with them the primitive-member mechanism; the rest of the page's own crumbs are
not:

| written | what happens |
|---|---|
| `DateTime.Now`, `UtcNow`, `Today` (C6) | `teko: DateTime.Now is not taught yet` and its two siblings — a wall clock is one symbol per operating system (`clock_gettime`, `GetSystemTimePreciseAsFileTime`), and teko declares it itself as an `extern` chosen by the target host (C6, the owner's ruling of 2026-09-08) |
| `d.ToString()`, `DateTime.Parse(s)`, `TryParse` | `teko: unknown member of DateTime` / `teko: unknown static member of DateTime` — a crumb of its own; the `enum` page's own N2b landed a PARALLEL mechanism keyed on a struct-table row, not `teko_prim.tk`'s lowering table `TimeSpan`/`DateTime` use, so it does not carry over automatically |
| `DateTime.SpecifyKind(d, k)`, `d.Subtract(x)` | `teko: unknown static member of DateTime: SpecifyKind` / `teko: unknown member of DateTime: Subtract` — neither is registered. A row's parameters may differ in type since N2c (`new DateTime(ticks, kind)` is the first one that does), so `SpecifyKind` is now only a row nobody has written; `Subtract` still needs two overloads of ONE arity, which the mechanism picks by argument COUNT alone. `SpecifyKind(d, k)` is `new DateTime(d.Ticks, k)` and `Subtract` is `-` |
| `ToLocalTime`, `ToUniversalTime`, `DateTimeOffset` | not taught: a time-zone database is not a language feature, which rules the first two out; `DateTimeOffset` needs none (its `.LocalDateTime` reads the offset the value carries) and is the one crumb still open on [datetime-extras.md](../specs/datetime-extras.md) (N5), queued behind C3, the sixteen-byte machine. `DateOnly` landed with N4a ([datetime.md](datetime.md#dateonly)) and `TimeOnly` with N4b ([datetime.md](datetime.md#timeonly)) |
| `d.ToString()`, `DateOnly.Parse(s)`, `TryParse` on a `DateOnly` | `teko: unknown member of DateOnly` / `teko: unknown static member of DateOnly` — the same crumb the `DateTime` row above waits on, for the same reason: no `teko_prim.tk` primitive has a `str` member yet |
| `t.ToString()`, `TimeOnly.Parse(s)`, `TryParse` on a `TimeOnly` | `teko: unknown member of TimeOnly` / `teko: unknown static member of TimeOnly` — the same crumb, the same reason; `d.ToDateTime(t)` itself IS taught now (N4b, [datetime.md § `TimeOnly`](datetime.md#timeonly)) |
| `switch` on a `DateTime` | ``teko: no operator `==` takes these operands`` — a `switch` compares its subject against integer case labels, and a date takes no integer operand |
| `t.ToString()`, `TimeSpan.Parse(s)`, `TryParse` | `teko: unknown member of TimeSpan` / `teko: unknown static member of TimeSpan` — a crumb of its own; no `teko_prim.tk` primitive has a `str` member yet, and the `enum` page's own N2b (built) does not carry over, since it is a parallel mechanism keyed on a struct-table row rather than a reuse of this table |
| `t * 1.5`, `t / 1.5` (C#'s `operator *(TimeSpan, double)`) | ``teko: no operator `*` takes these operands`` — the spec's § 4 leaves the float multiply out |
| `t / t` (C# 7's `operator /(TimeSpan, TimeSpan)` → `double`) | ``teko: no operator `/` takes these operands`` |
| `+t` (C#'s unary plus) | ``teko: no operator `+` takes these operands`` — the unary minus is taught, its C# twin is not |
| `new TimeSpan(h, m, s)` and the two longer constructors | `teko: wrong number of arguments for new` — one row, one argument: the tick constructor. Build it from `FromHours(h) + FromMinutes(m) + FromSeconds(s)` |
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
