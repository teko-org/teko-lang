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
| `return this;`, `C d = this;`, `f(this)` — a bare `this` used as a VALUE | `teko: a value of type uptr does not convert to C` — `this` is the receiver of `.member` and nothing else today. An attempt to type it as the enclosing class (2026-09-15) made the compiler's own field addressing `this + OFF` reach a user `operator+` and was withdrawn; the redesign (the class for surface consumers, `uptr` for the compiler's own addressing, across every scope walk) is owed — D87 |

Interfaces have no covariance and no contravariance, and a `struct` has no reference count
of its own ([memory.md](memory.md)).

## Numeric conversions

An integer converts to a float in every slot that has one, `f32` converts to `f64` the
same way, and nothing narrows back — an `f64` into an `f32` slot is refused rather than run
raw (D78, [types.md](types.md#f32-and-f64)). What is still missing around it:

| written | what happens |
|---|---|
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
| a member named `namespace` (`public i64 namespace;`) | `mc: empty lexeme`, from the core: the word's token carries no lexeme for `p_name()` to read, unlike every other taught word (measured; the other `syntax` words — `when`, `scope`, `match`, `type`, `switch`, `using`, `import`… — name a member). `loop` is mc's own keyword and stays `name expected` everywhere, as any core keyword does |
| a field named `ref` or `out` read by its BARE name inside a method (`return ref;`, `i64 x = ref;`, `ref = 5;`) | `` teko: `ref`/`out` requires a variable: ; `` — at expression position those two words wear their prefix meaning (`ref x`, `out x`, registered by `type_new`) and the parser reaches it before any member lookup (measured, all three spellings). `this.ref` and `h.ref` work (D68: a member name after the dot may be a taught word); qualify the read or pick another name |
| `m ?? 0m` / `m ?? Guid.Empty` (the `??` of a `T?` over a sixteen-byte value), and a lambda capturing a `decimal` or a `Guid` (`F f = () use (d) => d;` — the capture list follows the parameter list, [lambdas](delegates.md#use-captures-are-explicit); `use (d) () => d` is a different refusal, `call by name only`, and says nothing about the wide value) | `mc: teko: a cast is not defined on a sixteen-byte value yet`, with no `file:line`: both roads reach the machine's own guard (`teko_wide.tk`) rather than a surface refusal — they refuse, never miscompile, but the harness cannot pin them. `m.Value` on a `decimal?`/`Guid?` runs since D75 (the wide primitive lowering). A surface refusal with a line is a small crumb of its own |
| `(u64) d` on a `decimal` at or above 2^63 | `teko: decimal overflow`, exit 70: the cast routes through `tk_dec_to_i64` (the only integer target row today), so a value a `u64` could hold is refused as an overflow rather than answered. A `tk_dec_to_u64` and a `(TY_U64, decimal)` row are the same shape D77's ruling 8 landed for the source side (found by the verifier of C4) |
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

`TimeSpan` ([timespan.md](timespan.md)), `DateTime` ([datetime.md](datetime.md)) and its wall
clock (`Now`/`UtcNow`/`Today`, C6, D90) are built, and with them the primitive-member
mechanism; the rest of the page's own crumbs are not:

**`DateTime.Now` carries `DateTimeKind.Local` but reads the SAME instant `UtcNow` does** —
a recorded divergence from C#, not a gap: teko has no time-zone database (§ 8), so there is
no host offset to apply, and `Now` would otherwise need to fake one. `DateTime.Today` is
`Now` truncated to midnight, the same `Kind`.

| written | what happens |
|---|---|
| `d.ToString()`, `DateTime.Parse(s)`, `TryParse` | `teko: unknown member of DateTime` / `teko: unknown static member of DateTime` — a crumb of its own; the `enum` page's own N2b landed a PARALLEL mechanism keyed on a struct-table row, not `teko_prim.tk`'s lowering table `TimeSpan`/`DateTime` use, so it does not carry over automatically |
| `DateTime.SpecifyKind(d, k)`, `d.Subtract(x)` | `teko: unknown static member of DateTime: SpecifyKind` / `teko: unknown member of DateTime: Subtract` — neither is registered. A row's parameters may differ in type since N2c (`new DateTime(ticks, kind)` is the first one that does), so `SpecifyKind` is now only a row nobody has written; `Subtract` still needs two overloads of ONE arity, which the mechanism picks by argument COUNT alone. `SpecifyKind(d, k)` is `new DateTime(d.Ticks, k)` and `Subtract` is `-` |
| `ToLocalTime`, `ToUniversalTime` | not taught: a time-zone database is not a language feature. `DateTimeOffset` (N5, D76, [the type reference](types.md#datetimeoffset)) needs none of it — its `.LocalDateTime` reads the offset the value carries and nothing more — and landed whole; `DateOnly` landed with N4a ([datetime.md](datetime.md#dateonly)) and `TimeOnly` with N4b ([datetime.md](datetime.md#timeonly)) |
| `d.ToString()`, `DateOnly.Parse(s)`, `TryParse` on a `DateOnly` | `teko: unknown member of DateOnly` / `teko: unknown static member of DateOnly` — the same crumb the `DateTime` row above waits on, for the same reason: no `teko_prim.tk` primitive has a `str` member yet |
| `t.ToString()`, `TimeOnly.Parse(s)`, `TryParse` on a `TimeOnly` | `teko: unknown member of TimeOnly` / `teko: unknown static member of TimeOnly` — the same crumb, the same reason; `d.ToDateTime(t)` itself IS taught now (N4b, [datetime.md § `TimeOnly`](datetime.md#timeonly)) |
| `switch` on a `DateTime` | ``teko: no operator `==` takes these operands`` — a `switch` compares its subject against integer case labels, and a date takes no integer operand |
| `t.ToString()`, `TimeSpan.Parse(s)`, `TryParse` | `teko: unknown member of TimeSpan` / `teko: unknown static member of TimeSpan` — a crumb of its own; no `teko_prim.tk` primitive has a `str` member yet, and the `enum` page's own N2b (built) does not carry over, since it is a parallel mechanism keyed on a struct-table row rather than a reuse of this table |
| `t * 1.5`, `t / 1.5` (C#'s `operator *(TimeSpan, double)`) | ``teko: no operator `*` takes these operands`` — the spec's § 4 leaves the float multiply out |
| `t / t` (C# 7's `operator /(TimeSpan, TimeSpan)` → `double`) | ``teko: no operator `/` takes these operands`` |
| `new TimeSpan(h, m, s)` and the two longer constructors | `teko: wrong number of arguments for new` — one row, one argument: the tick constructor. Build it from `FromHours(h) + FromMinutes(m) + FromSeconds(s)` |
| `switch` on a `TimeSpan` | ``teko: no operator `==` takes these operands`` — a `switch` compares its subject against integer case labels, and a `TimeSpan` takes no integer operand |

## `decimal`, and the rest of `docs/specs/decimal.md`

C3 landed the sixteen-byte value, its literal and its movement (D74), C4 the arithmetic,
the comparisons and the four conversions (D77) and C5 the rounding, the text and the
statics (D79, [the type reference](types.md#decimal)). What is left of
[decimal.md](../specs/decimal.md) § 12 is C7 alone, a SPEED crumb nothing depends on: the
limb arithmetic replaced by each machine's own 128-bit instructions, with every fixture
unchanged at the same exit code.

| written | what happens |
|---|---|
| `(str) d` | ``teko: a decimal does not cast; `.ToString()` writes it and `decimal.Parse(s)` reads it`` — the four casts § 6 opens are calls since C4, and text is those two members since C5 |
| `decimal g = 3.25m;` at file scope | `global initializer must be constant`, from the core — the literal is the `N_IDENT` of its own blob global and a `decimal` has no folded form at all. `decimal g = 5;` gets past that rule (`5` IS an `N_INT`) and is refused by name instead: `teko: a global decimal takes no initializer`. A global is a slot and an assignment |
| `decimal.ToDouble(d)`, `decimal.FromDouble(x)` | `teko: unknown static member of decimal` — they are `(f64) d` and `(decimal) x`, C4's own conversion rows under C#'s other spelling, so C5 registers no second door to one road (D79, ruling 1) |
| `Math.Round(x)` on an `f64`, `Math.Max`, `Math.Sqrt` | `Math.Round(x)` on an `f64`: `teko: a value of type f64 does not convert to decimal`; `Math.Max`/`Math.Sqrt`: `teko: unknown member: Max`/`Sqrt` — `lib/math.tk` is the `decimal` half C5 landed; a float half is a library crumb of its own |
| a culture, a thousands separator or a currency sign in `Parse`/`ToString` | `teko: the string is not a decimal` on the way in, and never written on the way out — formatting is a library and not a primitive ([decimal.md § 9](../specs/decimal.md)) |
| `decimal.Parse` on a value with more than 28 decimal places | `teko: decimal overflow`, exit 70 — exact or refused: this type never rounds a number the writer wrote out in full (D79, ruling 5) |
| `const decimal RATE = 0.07m;` | `teko: const requires a constant expression` — a `const` is folded at compile time and the folder has no 128-bit arithmetic, so a `decimal` has no folded form. An array size is the same rule |
| `case 1m:` | `teko: a case label must be a constant expression`, for the same reason |
| `extern i64 f(decimal d);` | ``teko: an `extern` takes no decimal`` — the sixteen-byte convention is teko's own and is not a C ABI |
| `&a[i]` on any array, `decimal` included | ``teko: `[` needs an array`` — teko takes no address of an element, for any type; it is pre-existing and not this type's own. `decimal a[i]` and `a[i] = d` themselves DO move all sixteen bytes |
| `#include "decimal.tk"` forgotten, on a function that returns a `decimal` | `teko: include "decimal.tk" before returning a sixteen-byte value` — the return buffer `tk_dec_retbuf` is a global the PROGRAM declares. It is the rule `rt.tk` already has for an `enum`'s own lowering symbols, and it has no line: the machine is past the parse when it asks |
| a generic `T` bound to `decimal` | not refused on principle; not measured by C3 |
| `checked` / `unchecked` | teko has neither word; when the arithmetic lands, the overflow is always loud |

`ref decimal` and `out decimal` left this table with N3 (D75): the wide pointee road is open
for every `TK_WIDE` type at once, and `tests/primitives_decimal_out.tk` is its oracle.

## `i128` and `u128`

N6a (D81), N6b-1 (D83), N6b-2 (D84) and N6b-3 (D85) together landed the whole of N6b
([the specification](../specs/small-ints.md) § 6, § 7, § 8, [the type
reference](types.md#i128-and-u128)): the two types, their literal, `+ - * / % << >> & | ^ ~`,
unary `-`, the six comparisons, the explicit casts to and from a 64-bit integer, between the
two types, and to and from `f64`/`decimal`, and the members and statics — `ToString`,
`Parse`, `TryParse`, `CompareTo`, `Equals`, `MinValue`, `MaxValue`, `Zero`, `One`. N6b owes
nothing more. Every row below is a RULE, not a gap a future crumb closes.

| written | what happens |
|---|---|
| `(str) x` | ``teko: an i128 does not cast; `.ToString()` writes it and `i128.Parse(s)` reads it`` — text is `.ToString()`/`i128.Parse(s)`, a MEMBER call each, so a cast never opens even though the type registers a reader and a builder clause (D85), `decimal`'s own shape (D79) |
| `x.TryParse(...)` on an instance | ``teko: i128.TryParse is static; reach it through its type`` — `TryParse` is a static, `decimal`'s own shape (D79) |
| `new i128()` | `teko: new i128() is not taught; write 0i` — D76's shared guard for every wide type's zero-argument constructor |
| `const i128 K = 1i;`, `case 1i:`, an array size | `teko: const requires a constant expression` / `teko: a case label must be a constant expression` — the folder has no 128-bit arithmetic, exactly as for `decimal` |
| `i128 g = 1i;` at file scope | `global initializer must be constant`, from the core; `i128 g = 5;` gets past that rule and is refused by name, `teko: a global i128 takes no initializer`. A wide global is a slot and an assignment |
| `extern i64 f(i128 v);` | ``teko: an `extern` takes no i128`` — the sixteen-byte convention is teko's own and is not a C ABI |
| `a + b` on an `i128` and a `u128` | ``teko: no operator `+` takes these operands`` — C# refuses the same pair without a cast, and this one is a RULE and not a gap: `(i128) u` and `(u128) x` are the two spellings, and neither moves a bit |
| `f64 x = v;`, `decimal d = v;` on an `i128`/`u128` `v` | `teko: a value of type i128 does not convert to f64`/`decimal` — D84's own eight rows are explicit only, the same rule every other conversion of a wide type already follows |
| `a & b == c` on `i128`/`i128`/`i128` | **accepted, and read as `a & (b == c)`** where a reader used to C#'s precedence expects `(a & b) == c` — `mc`'s core grammar gives `&`/`\|`/`^` LOWER precedence than `==` (C's own table, D3), and `b == c` answers `i64` 0/1, which `tk_ops_promote` widens to the wide type before `&` is looked up, so the expression TYPE CHECKS rather than refuses. `%`, `<<`, `>>`, `&`, `\|`, `^` opened this door for `i128`/`u128` (D83); it was already open for `i8`/`u8`/every other integer. Write `(a & b) == c` |
| `Int128.PopCount`, `LeadingZeroCount`, `RotateLeft` | C# 11's generic-math surface; a library, once there is one ([small-ints.md § 9](../specs/small-ints.md)) |
| `checked` / `unchecked` | teko has neither word; `i128` wraps, which is C#'s unchecked default and the only behaviour there is |

`u128 x = -1;` is **accepted** and answers 2^128−1, where C# makes the signed source an
explicit cast. teko's own `u64 x = -1;` has always been accepted for the same reason —
there is no `checked` word to tell a wrapped conversion from a wrong one — and the
conversion table carries one row per source signedness and no implicit/explicit column
(D81).

## `string`, and the rest of `docs/specs/string.md`

N7a (D86) landed the class ([the type reference](types.md#string)): the four fields,
`new string(raw)` from a `str`, `.Length`/`.Utf8Length`/`.ToString()`/`.Equals`/
`.CompareTo`/`.GetHashCode`, `operator+`/`operator==`/`operator!=`, and the three statics
`Empty`/`Concat`/`IsNullOrEmpty`. N7b (D88) landed § 2-4: literal interning, the two
implicit conversions in every one of D33's nine slots, and the measurement of the include
refusal § 5 names. One crumb still owes the rest of the design.

| written | what happens today, and why |
|---|---|
| `string s = "hi";` | **accepted, interned** (N7b) — `string s = p;` on a `str` VARIABLE `p` (a literal or not) still refuses, `teko: a value of type uptr does not convert to string`: interning reads the source NODE, not the value, so only a literal written directly in the slot converts (`tests/refuse/string_str_implicit.tk`) |
| `puts(s)`, `str p = s;`, `extern ... (str ...)` given a `string` | **accepted, and correct** since N7b: the implicit `string` → `str` conversion (`tk_str_borrow`) reads the `data` field, one `ld64`, wherever a `str`/`ptr`/`uptr` slot THE PROGRAM DECLARES receives a `string` — the object-header bug this row used to name is gone |
| `rt_own(s)`, `rc_inc(s)`, any `lib/rt.tk` function OTHER than its text readers, given a `string` | **no conversion**: the runtime's own parameters are `uptr` GENERICALLY — the slot, the value, any counted reference at all — so the object stays the object there (`tk_cap_own` receiving `.data` was a SIGSEGV on a captured `string`, PR #749 finding 1). The runtime's TEXT readers — `panic`, `rt_panic`, `tk_str_len`, `tk_str_slice`, `tk_str_eq`, the enum readers — DO borrow the text (`tk_rc_rt_text`, D88): `panic(s)` prints the string, never its header: `str p = s; tk_str_len(p);`, or a `str` parameter of the program's own |
| `string g = "hi";` at FILE scope | `global initializer must be constant`, the core's own — the interned literal is a global itself and `&global` is no constant initializer (§ 4). A `string` global takes no initializer; assign it in `main` |
| a `class string` the PROGRAM declares | an ordinary user class: neither the interning nor either conversion fires on it, and `string s = "hi";` refuses with `teko: a value of type uptr does not convert to string`. The row is `lib/string.tk`'s only — provenance plus layout, `tk_str_class_si` (D88) |
| `string` named with no `#include "string.tk"` | a plain parse error, `expected ; after expression` — and it stays that way: D48 already measured the identical question for `DateTimeKind`, a type an `#include`d library file declares, and ruled the friendly hint IMPOSSIBLE without making `class string { ... }` refuse its own name (`tk_newname`, `teko: the name is already a type`) the moment `lib/string.tk` is parsed. A library type is told apart by the library, D48's own sentence and D88's re-measurement of it for a class rather than an `enum` |

**N8 landed (D89)**: `s[i]` (`tk_string_at`, `teko_params.tk`'s own row in `tk_bracket`),
`s[i] = c` (`teko: a string is immutable`), and every instance/static method § 7 names —
`Substring`, `IndexOf`, `LastIndexOf`, `Contains`, `StartsWith`, `EndsWith`,
`Trim`/`TrimStart`/`TrimEnd`, `ToUpper`/`ToLower`, `Replace`, `Split`, `PadLeft`/`PadRight`,
`string.Join`. One divergence: `.IndexOf(char)` is spelled `IndexOfChar` rather than a
second `IndexOf` overload — a class method is resolved by name-and-ARITY alone
(`tk_method_pick`, `teko_class.tk`), unlike a free function's `teko_over.tk`, so two
one-argument `IndexOf` signatures are genuinely ambiguous (`teko: ambiguous overload; two
signatures take this many arguments: IndexOf`, measured); extending method dispatch to
read argument types is real machinery outside this crumb's own boundary and stays a row
here:

| written | what happens today |
|---|---|
| `s.IndexOf(c)` where `c` is a `char` (a second `IndexOf` overload beside `.IndexOf(string)`) | `teko: ambiguous overload; two signatures take this many arguments: IndexOf` — write `s.IndexOfChar(c)` instead (N8, D89); a class method has no argument-type-based dispatch, only a free function does (`teko_over.tk`) |
| `this[i]` on a user-declared class | `` teko: `[` needs an array `` — N8's own `string`-receiver row in `tk_bracket` is targeted, not a general indexer (`docs/specs/string.md` § 6/§ 10); a user `operator[]`/indexer is a fork of its own, undesigned |
| `"hi"[0]` — a LITERAL indexed directly | `` teko: `[` needs an array `` — the index hook reads the receiver's own type through `tk_struct_of_expr`, which answers for a local, a field chain and a call's return, never for a literal the interning has not placed yet. `string s = "hi"; s[0]` is the spelling (D89) |

Two gaps N7b's own interning does not reach, both measured directly, neither one of D33's
nine slots:

| written | what happens today, and why |
|---|---|
| `pick("hi")` against an OVERLOADED `string pick(string)`/`pick(i64)` | `teko: no overload of pick matches these arguments` — overload SELECTION (`tk_ov_args_fit`, `teko_over.tk`) is a different question from the nine slots' own conversion, asked before any declaration is chosen; a name declared ONCE still interns (`greet("world")` in `docs/specs/string.md` § 5's own sample), and `string pick(string s) { return s; }` alone, called the same way, works |
| `c ? "yes" : s` (a literal ternary arm beside a `string`) | `` teko: the two arms of ?: have different types `` — `tk_tern_lower` (`teko_ternary.tk`) requires its two arms' types to already be EQUAL and converts neither one, for any type, string included (`cond ? 1 : 2.5` refuses the identical way); `??` is `string`'s own gap this crumb closed (`teko_null.tk`), `?:` is a different mechanism with no conversion of its own to extend |

`$"..."` **landed, N10, D92** — [the type reference](types.md#string) and
[the specification](../specs/string.md) § 9 have it. What § 9 itself still names as not
taught, over a hole:

| written | what happens today, and why |
|---|---|
| `$"{x,10}"` — C#'s alignment | `teko: an interpolation hole holds one expression, no alignment or format specifier` — a hole holds one expression, nothing else (§ 9's own decision) |
| `$"{x:N2}"` — C#'s format specifier | the same message |
| `$"{x}"` where `x` is `f64`/`f32`/`decimal`/`DateTime`/`TimeSpan`/`Guid`/an `enum` | `` teko: no interpolation of a value of type Foo `` — each one's own `.ToString()` (where it has one) hands back an `rt_alloc`-owned `str` this crumb cannot free with the right size from outside the module that sized it; write the value into a `string` first and concatenate with `+` |
| `$"{(a > 0 ? 5 : 6)}"`, `$"{x ?? 3}"`, `$"{o?.Name}"` | `teko: an interpolation hole holds no ternary, ?? or ?.` — each parks as a marker call (`tk_ternary`/`tk_coalesce`/`tk_qdot`, `teko_ternary.tk`) that `tk_ternary_pass` unpacks, and that pass runs AFTER the one this crumb types a hole in, so `tk_ty_of` has nothing to answer with yet. Moving the hole's own walk behind `tk_ternary_pass` is the upgrade; it needs a scope walk of its own and is not this crumb's. Compute into a local, interpolate the local |
| `cond ? $"a{n}" : $"b"` — an interpolation as a TERNARY ARM, and `$"a" + $"b"` with no hole in either | `teko: a value of type uptr does not convert to string` / `teko: the two arms of ?: have different types` — neither is this crumb's: a hole-less `$"…"` is a plain `str`, and `cond ? "a" + z : z` refuses identically with no interpolation anywhere. The generic row above owns it; named here because the message never says the word interpolation |
| `char 0` in a hole, and a `u32` above `0x10FFFF` | the empty string, and one garbage byte — pre-existing ceilings of the NUL-terminated `str` and of `tk_string_cp_encode`, not reachable before this crumb and not refused by it (C# gives a one-character string for `'\0'`) |
| `$"{x // note}"` | `teko: a line comment does not fit in an interpolation hole` — the `+` chain is pushed back through the lexer as ONE line. `$"{x /* note */}"` works |
| `$"a{n}".Length` — a `.` directly on the literal | `` teko: uptr has no members: Length `` — NOT this construct's: `"abc".Length` and `("a" + s).Length` are refused identically without any interpolation, a `str` literal carrying no members. Bind to a `string` local first |
| `$"{u}"` where `u` is `u32` | the UTF-8 encoding of the code point, not the decimal — `char` is `type_alias("char", TY_U32)`, the same core id, so the two cannot be told apart (D92). `u8`, `u16`, `i8`, `i16`, `i32`, `i64` and `u64` all give decimal |

`"n=" + 5` (needing a universal `ToString`/`object`, § 11) is neither N7a's, N7b's, N8's
nor N10's; it stays exactly where [the specification](../specs/string.md) § 11 leaves it.

## The rest of `docs/specs/guid.md`

N3 landed the type whole but one member (D75, [the type reference](types.md#guid)): the
sixteen bytes, `Parse`/`ToString`, `TryParse`, the ordering and `Empty`. N9 (D91) landed the
one function that was left, `Guid.NewGuid()`, over the SAME wrapped bundle resolver C6 (D90)
installed for the wall clock — `getrandom` on Linux, `getentropy` on macOS,
`BCryptGenRandom` on Windows, the last needing the `bcrypt.def` this crumb added to teko's
own Windows sysroot.

| written | what happens |
|---|---|
| `g.ToString("B")`, `"P"`, `"X"` | `teko: the Guid format is not taught`, a run-time panic (exit 70) — three more spellings of the same sixteen bytes, and nothing asks for them |
| `g.ToByteArray()`, `new Guid(byte[])` | `teko: unknown member of Guid` / `teko: this primitive has no constructor` — the byte-order question of § 1 becomes visible the moment either exists, and neither is asked for |
| version 1, 3, 5 and 7 `Guid`s | not taught: v1 needs a MAC address and a clock, v3/v5 need MD5/SHA-1, v7 needs a clock, and all of them are a library over `NewGuid`'s own primitive |
| `a < b` matching C#'s field-wise `CompareTo` | **not a gap, a recorded divergence**: teko orders by the bytes as they print, unsigned and left to right ([the specification](../specs/guid.md) § 1, § 6) |

## `DateTimeOffset`, and the rest of `docs/specs/datetime-extras.md`

N5 landed the type whole (D76, [the type reference](types.md#datetimeoffset)): the sixteen
bytes, the constructor, every reader, the Unix conversions, `ToOffset`, the six comparisons,
the two arithmetic operators and both text forms. C6 (D90) landed its wall clock,
`Now`/`UtcNow`, alongside `DateTime`'s own. What is left is one overload the design narrows
on purpose.

**`DateTimeOffset.Now` and `UtcNow` are the one instant under two names** — a recorded
divergence from C#, not a gap: both carry offset `+0`, `TimeSpan.Zero`, because teko has no
time-zone database (Ruling 7, § 8) and there is no local offset to apply.

| written | what happens |
|---|---|
| `new DateTimeOffset(i64 ticks, TimeSpan)` | `teko: a value of type i64 does not convert to DateTime` — C#'s raw-ticks overload is **not taught**; the one `new` row this table carries takes a `DateTime` first, and `tk_prim_pick` chooses a `"new"` row by ARGUMENT COUNT alone, so an `i64` written there is an ordinary mismatch against that row's own first position rather than a row of its own. `new DateTimeOffset(new DateTime(t), ts)` is the written form |
| `new DateTimeOffset()` | `teko: new DateTimeOffset() is not taught; write DateTimeOffset.MinValue` — D76's shared guard for every wide type's zero-argument constructor (`decimal`, `Guid`, `DateTimeOffset`): the identity cast to zero every OTHER primitive answers `new T()` with has no sixteen-byte form, so the type's own named zero is the written form instead |

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
