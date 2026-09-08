# `T?` — the nullable, over any type

**Designed, not built.** Nothing on this page runs today; every fenced sample is marked
`// no-run` for that reason. The reference describes what runs
([types.md](../reference/types.md), [memory.md](../reference/memory.md)); this page is the
plan for the delta.

`T?` is teko's one nullable mechanism. It is sugar for `Nullable<T>` over **any** type —
a class, an interface, a delegate, a `struct`, a `T[]`, `string` when it lands, and every
value type: `i64`, `f64`, `i8`/`i16`, `bool`, `char`, an `enum`, `TimeSpan`, `DateTime`,
and `decimal`/`Guid` the day they land. No type is nullable unless the slot is written
`T?`.

This is a deliberate divergence from C#, and the only one the page makes on purpose:
C# has two unrelated mechanisms — `Nullable<T>` for value types (a real type) and `string?`
for reference types (an annotation the runtime never sees). teko has one mechanism, one
spelling and one rule, for both. Everything else — `HasValue`, `Value`,
`GetValueOrDefault()`, `??`, `?.`, the implicit `T` → `T?` and the refused `T?` → `T` —
follows C# where C# has a form.

---

## 1. The rulings this page is built on

| # | ruling | where it comes from |
|---|---|---|
| 1 | **No type is nullable by default.** `null` lands only in a slot declared `T?` | the owner, 2026-09-08 |
| 2 | **`T?` is `Nullable<T>` for any `T`**, reference or value: one mechanism, one spelling | the owner, same day |
| 3 | **A `struct` declared without `new` stays the developer's error** — no guard, no refusal of the declaration itself | D42 |
| 4 | **Definite assignment belongs to this design**: with `T?` in the language, the compiler can say that a name written without `?` has to be built before it is read | D42, in as many words |
| 5 | **`Nullable<T>` does not use the generic mechanism.** It is a construct of the compiler over the type it encloses, the way `T[]` and `params T[]` already are | the owner, same day |

Ruling 5 is not a convenience. A generic `struct Nullable<T>`
([generics.md](../reference/generics.md)) would be a teko `struct`, and a teko struct value
**is a pointer to an allocation** ([types.md](../reference/types.md) § `struct`) produced by
`new` — so `Nullable<T>` as a generic struct would be a second, weaker nullable sitting on
top of the first, with `new` required to make one and no reclaim behind it. The construct
has to be the compiler's.

Ruling 2 supersedes [string.md](string.md) § 11, which planned `Nullable<T>` as a generic
struct after `object`, and leaves the reference half of the question open. It is answered
here: § 2.

---

## 2. Representation — one rule, two storages

**A `T?` value is a pointer-width handle. `null` is the handle `0`. `HasValue` is
`handle != 0`.** That single invariant holds for every `T`, which is what makes the
surface rule uniform rather than two rules wearing one spelling.

What the handle points at depends on whether `T`'s own value is already a pointer:

| `T` | a value of `T` is | `T?` is | counted? |
|---|---|---|---|
| `class`, `interface`, `delegate`, `T[]`, `string` (N7) | a pointer to a counted object | **the same pointer**; no box, no extra word, no extra count | yes, exactly as `T` is |
| `struct` | a pointer to an arena allocation ([types.md](../reference/types.md) § `struct`) | **the same pointer** | no, exactly as `T` is |
| `i64`, `u64`, `i32`, `i8`, `i16`, `u8`, `u16`, `u32`, `bool`, `char`, `f64`, `f32`, an `enum`, `TimeSpan`, `DateTime`, and `decimal`/`Guid` when they land | a value in a register or a frame slot | a pointer to an **immutable counted box** holding those bytes | yes |

So `Cell?` and `Cell` have **the same representation**, and everything the reclaim already
does for a `Cell` it does for a `Cell?` with no new line: `rc_dec(0)` and `rt_own(0)` are
already no-ops over the null handle (`lib/rt.tk`, the reason `tk_null` was written as an
`N_INT` of 0 in the first place, [teko_type.tk](../../teko_type.tk)).

### The box, for a value `T`

The same object shape a `T[]` of heap already has ([memory.md](../reference/memory.md) §
"The object", [teko_heaparr.tk](../../teko_heaparr.tk)):

```
+0   vtable pointer  ->  { +0 &tknl_release, +8 0 }
+8   reference count
+16  the payload width, in bytes
+24  the payload: type_width(T) bytes
```

Four properties come out of that choice, and they are why it beats the alternatives:

- **One release function for every value nullable, not one per type.** A box holds no
  reference — a counted `T` never reaches the box arm, because a counted `T` is already a
  pointer and takes the row above — so the release is `rt_free(p, 24 + ld64(p + 16))` and
  nothing else. `lib/rt.tk` grows three small functions and one vtable, and the compiler
  generates **no** per-type code, which is less than `T[]` needs.
- **Every position works with no new rule.** A `T?` field is an ordinary counted field, a
  `T?` element of a `T?[]` is an ordinary counted element, a `T?` argument is borrowed, a
  `T?` return hands the caller a reference it already owns, a `T?` temporary is parked, a
  `T?` capture by value uses the counted capture writer. All of it exists
  ([teko_rc.tk](../../teko_rc.tk)), and none of it has to learn what a nullable is.
- **Value semantics come from immutability.** A box is never written after it is built:
  every store into a `T?` slot builds a fresh box, so `b = a; b = 7;` leaves `a` at its own
  value with no copy rule anywhere. There is no way to obtain the address of a payload —
  `.Value` is not a slot (§ 6) — so the invariant cannot be broken from the surface.
- **The allocation is recycled.** A 32-byte box comes from the 32-byte free list, so a loop
  that churns `i64?` reuses one block forever, the property
  [memory.md](../reference/memory.md) already states and already proves with `rt_peak()`.

The honest cost, stated rather than hidden: **`i64? x = 5;` allocates**, and `rt_live()`
counts it. C#'s `int?` costs nothing. Every reclaim fixture on this page asserts the number
so the cost is measured and not argued.

### Why not the alternatives

| alternative | why not |
|---|---|
| **a sixteen-byte value + flag, by address** (`decimal`'s own shape, [decimal.md](decimal.md) § 1) | it needs `TK_WIDE` and `teko_wide.tk`, a derived machine over three tables — the whole C3 crumb — before a single `i64?` compiles. It would put `Nullable` behind `decimal` when the owner's order puts it ahead of `string` |
| **eight bytes of value + one flag byte in the same word** | it fits `i32?` and nothing else. `i64?`, `f64?`, `DateTime?` have no spare bit, and a rule that holds for some widths is not a rule |
| **two slots under one name** (`x` and `x$has`), the shape `params T[]` is lowered to | it breaks arity everywhere a type travels: an argument becomes two, so every overload, every `delegate` signature, every interface row, every `ref`/`out` and every `params` list would have to learn about it, and a `T?` return has nowhere to put the second slot |
| **a compiler-owned frame cell, addressed by the handle** | it keeps the handle rule, and then needs a new lifetime rule at seven positions (a field, an array element, an argument, a return through a global buffer with a copy-out discipline, a temporary pool, a capture, a recursive frame). It buys one arena allocation and pays for it with the exact class of rule that produces a silent miscompile |
| **`object` and boxing** | a run-time type tag, which D4 rules out. Nothing here reads a tag: `i64?` is a distinct **static** type, the payload's type is known at every site, and the vtable word is the reclaim's, the same word a `T[]` carries |

### The type row

One row of [teko_struct.tk](../../teko_struct.tk)'s type table per distinct `T`, made lazily
the first time `T?` is spelled — `tk_nl_row(ty)`, the exact shape `tk_ha_row(ety)` already
has for `T[]`:

```
type_new("Cell?", 8, 8, TK_INT)     // a lexeme the lexer can never form
tk_type_add("Cell?", ty, -1, TK_KNULL, TK_TPUBLIC, 1)
```

`TK_KNULL` is a sixth row kind beside `TK_KSTRUCT`/`TK_KCLASS`/`TK_KIFACE`/`TK_KDELEG`/
`TK_KARRAY`/`TK_KENUM`, and the row records the enclosed type. `tk_is_counted` answers for
a nullable row exactly what it answers for what it encloses, and `1` for the box arm.
A generated **symbol** may not carry `?`, so `tk_ty_mangle_name` maps the row to `opt_T`,
the rule it already applies to `T[]` (`arr_T`).

---

## 3. The surface

### Declaring

`T?` is a **type suffix**, read at the type position by a `syntax_type` handler — the
position `mc`'s own hooks documentation names `?` as an example of, and the same one `T[]`
is read at.

```teko
// no-run
#include "rt.tk"

class Cell {
    public i64 v;
    public Cell(i64 x) { v = x; }
}

class Holder {
    public Cell? head;                            // a field
    public i64?  count;                           // a value field
}

Cell? found(i64 k) {                              // a return type
    if (k == 0) return null;
    return new Cell(k);
}

i64 use(Cell? c, i64? n) {                        // parameters
    return 0;
}

i64 main() {
    Cell? a = null;                               // a local, declared empty
    Cell? b = new Cell(1);                        // a local, declared full
    i64?  n = 5;                                  // a value nullable
    i64?  m = null;
    Cell?[] cs = new Cell?[3];                    // an array of nullables
    Cell[]? xs = null;                            // a nullable array
    return 0;
}
```

A `T?` local written with no initializer is `null`, and it is definitely assigned (§ 8):
`i64? x;` is `i64? x = null;`. C# refuses that one and teko does not, because the slot is
zeroed either way and rule 1 says the `?` is the licence to hold nothing.

### `null`, and where it may not go

```teko
// no-run
#include "rt.tk"

class Cell {
    public i64 v;
}

i64 main() {
    Cell? ok = null;                              // the only slot null lands in
    Cell  no = null;                              // teko: null needs a slot declared Cell?
    i64   k  = null;                              // teko: a value of type uptr does not convert to i64
    Color c  = null;                              // teko: a value of type uptr does not convert to Color
    return 0;
}
```

The second refusal is this design's own and is new; the third and fourth already exist
today (D32/D33/D39/D40's own clause in `tk_check_scalar_compat`) and do not move.

**A comparison against `null` stays legal on a slot that is not nullable.** `if (c == null)`
where `c` is a `Cell` parameter compiles exactly as it does today, and must, because rule 1
is a rule about **declarations**, not a proof about values: a `null` still reaches a
non-nullable slot through a zeroed field, an element of `new T[n]`, and a `struct` declared
without `new` (§ 9). Refusing the test would delete the defensive code that catches them and
would make the run-time guards unreachable from the surface.

### Reading it

```teko
// no-run
#include "rt.tk"

class Cell {
    public i64 v;
    public Cell(i64 x) { v = x; }
    public i64 get() { return v; }
}

i64 main() {
    Cell? c = new Cell(7);

    if (!c.HasValue) return 1;                    // a truth value, no allocation
    Cell  hard = c.Value;                         // the checked read
    i64   a = c.Value.get();                      // ...and a member through it
    i64   b = c.v;                                // teko: a Cell? is read through .Value or ?.
    i64   d = c.zz;                               // teko: unknown member of Cell?: zz
    Cell  e = c;                                  // teko: a value of type Cell? does not convert to Cell

    i64? n = 5;
    i64  k = n.Value;                             // 5
    i64  j = n;                                   // teko: a value of type i64? does not convert to i64
    i64  z = n.GetValueOrDefault();               // 5, and 0 when empty
    return 0;
}
```

`c.v` is refused rather than deferred because `Nullable<T>` has the members
`Nullable<T>` has, which is C#'s own rule for `int?`. The message is a cause of its own only
when the name **is** a member of the enclosed type; anything else gets the ordinary unknown
member wording.

---

## 4. Conversions

| written | what happens |
|---|---|
| `T` → `T?` | **implicit**, at every one of the nine slots D33 enumerated. For a reference `T` it is identity, and the tree does not move; for a value `T` the compiler writes the box |
| `D` → `B?`, `C` → `I?` | implicit, whenever `D` → `B` and `C` → `I` already are: the nullable row asks `tk_row_fits` about what it encloses |
| `null` → `T?` | implicit; the handle `0` |
| `T?` → `T` | **refused**: `teko: a value of type Cell? does not convert to Cell`. Write `.Value`, or `??` |
| `T?` → `U?` where `T` → `U` | **refused** in this design: no covariance between nullable rows. A not-yet row; `x.Value` converts and re-wraps |
| `null` → `T` | **refused**: `teko: null needs a slot declared Cell?` |
| `null` → `uptr`, `ptr`, `str` | **accepted, unchanged**: `null` is an `N_INT` of 0 typed `TY_UPTR`, and 0 is an ordinary value of a raw pointer. `uptr?` is not a type (§ 11) |
| `T?` → a numeric slot | **refused, free**: D34's own clause already refuses a value whose type is a row in a numeric slot |
| a number → `T?` | **refused, free**: the row check the target already runs |

The nine slots, and the module that owns each, are D33's own list and do not change: an
initializer, an assignment and a `return` ([teko_rc.tk](../../teko_rc.tk)); an argument of a
free or method call (same file), of a virtual call ([teko_expr.tk](../../teko_expr.tk)) and
of an interface call ([teko_iface.tk](../../teko_iface.tk)); an element of a `params T[]`
([teko_params.tk](../../teko_params.tk)); a field store
([teko_typeof.tk](../../teko_typeof.tk)); and a binary
([teko_ops.tk](../../teko_ops.tk)). The wrap is written by one helper handed back to the
caller to splice — `tk_nl_wrap(tty, ety, e)`, the exact signature and the exact discipline
`tk_num_widen` already has.

---

## 5. Operators

| form | verdict |
|---|---|
| `x == null`, `x != null`, `null == x` | **taught**: a comparison of the handle against 0, for any `T?` and for any reference-representation type that is not nullable (unchanged from today) |
| `a ?? b` | **taught** (Q2): C#'s null-coalescing operator, right-associative, `a` evaluated once |
| `a?.m`, `a?.m(x)` | **taught** (Q2): the null-conditional member access, result `M?` |
| `a == b` on two nullables | **refused** until Q4a — `teko: no operator == takes these operands`. For a boxed value nullable, comparing handles is two different boxes holding 5 answering "different", which is the one thing this repository will not ship. Q4a teaches the lifted rule instead |
| `a + b`, `a < b`, `a & b` … on nullables | **refused**: `teko: no operator + takes these operands`. C#'s lifted arithmetic and lifted ordering stay out (§ 11) |
| `x switch { … }`, `switch (x)` on a nullable | **refused, free**: a switch compares its subject against constant labels, and a nullable takes no integer operand — the same wording a `DateTime` subject already gets |
| `x is null` | there is no `is` operator in teko at all. `x == null` is the form |

### `??`, and where it sits

C# puts `??` between `||` and the conditional operator. teko has no room there: `mc`'s
Pratt table starts at 1 with `||`, `syntax_infix` refuses a precedence outside 1..100, and
the ternary is already registered at 1 ([teko_ternary.tk](../../teko_ternary.tk)). `??` is
registered at **1** as well, tied with both, and the divergence that produces is exactly
one shape: `a || b ?? c` reads as `(a || b) ?? c` where C# reads `a || (b ?? c)`. It is
recorded in [not-yet.md](../reference/not-yet.md) rather than fixed, because fixing it means
renumbering `mc`'s own table, which is the base grammar (D3). Mixing the two without
parentheses is also a type error in almost every program that writes it: `||`'s operands are
truth values and `??`'s left operand is a nullable.

### `??` and `?.` are lowered into the ternary

Neither needs a hoisting rule of its own. Both are parsed into a placeholder call and
rewritten by a pass that runs **immediately ahead of `tk_ternary_pass`** into the
placeholder that pass already unpacks:

```
a ?? b       ->  tk_ternary($t != 0, $t.Value, b)      // $t binds `a` once
a?.m         ->  tk_ternary($t != 0, (M?) $t.Value.m, (M?) null)
```

so laziness, the hoist into the enclosing statement list, the fencing of a lone branch and
the right-associative nesting all come from the operator that already has them. `b` is
evaluated only when `a` is empty, which is C#'s rule and the reason the rewrite has to land
before the ternary pass rather than build `if`s of its own.

`?.` on a method that returns `void` is refused — `teko: ?. needs a value` — because the
lowering is an expression and a `void` arm has no type. `a?.b?.c` chains, each link
producing a nullable the next one tests.

### The `?` token, and the two new lexemes

`?` is already a taught lexeme (the ternary's). `??` and `?.` are registered through
`syntax_infix`, which adds them to the same lexeme table, and `mc`'s lexer takes the
**longest** punctuation prefix — so `??` and `?.` win over `?` wherever the characters are
adjacent, and `c ? a : b` is untouched. One ambiguity comes with the suffix and is C#'s own:
a statement that opens with a type word followed by `?` reads as a **declaration**, so
`Cell ? a : b;` written as an expression statement is not one. C# resolves it the same way,
in favour of the declaration.

---

## 6. `HasValue`, `Value`, `GetValueOrDefault()`

| member | for a reference/struct `T` | for a value `T` |
|---|---|---|
| `x.HasValue` | `x != 0`, typed `i64` | the same |
| `x.Value` | `(T) tk_nl_ck(x)` — the guard, then the same pointer | `tk_ld(T, tk_nl_ck(x) + 24)` — the guard, then the typed payload load |
| `x.GetValueOrDefault()` | **refused**: `teko: a reference nullable has no default` | `x == 0 ? <zero of T> : x.Value` |
| `x.Value = e` | **refused**: `teko: .Value is not a slot` | the same |

`tk_nl_ck(uptr p)` is three lines of `lib/rt.tk`: `if (p == 0) panic("a nullable with no
value"); return p;` — the same `panic`, the same `teko:` line on standard error, the same
**exit 70** every other guard in this port uses ([memory.md](../reference/memory.md) §
Panics, which gains the row).

`GetValueOrDefault()` is refused on a reference nullable on purpose, and it is the one place
where a law beats C#: C#'s answer is `default(T)`, which for a reference is `null`, and
handing a `null` to a slot typed `T` is precisely what ruling 1 forbids. `??` is the form,
and it says which default it means.

The typed load is `tk_ld` ([teko_struct.tk](../../teko_struct.tk)), which already sign-extends
a narrow signed load, already picks `ldf64`/`ldf32` for a float and already picks the width;
the typed store is `tk_stn`, the pair every field access in this compiler is built from. A
primitive with members — `TimeSpan`, `DateTime`, and `decimal`/`Guid` later — travels as the
raw eight (or sixteen) bytes with the compiler's own cast on the way in (`tk_prim_raw`) and
on the way out, which is D40/D41's rule and D42's precedent, so `tk_prim_cast_check` never
mistakes either for a cast a source wrote.

`.ToString()` on a nullable is **deferred**: no type in teko converts to text yet — that is
N2b's and N7's shared crumb — and a `T?` will get it when its `T` has it.

---

## 7. The reclaim

Nothing new. A `T?` local is released at the `}` that closes its block and on every jump
that leaves it; a `T?` field is released with the object that holds it and when it is
overwritten; a `T?` element of a `T?[]` the same; a `T?` returned hands the caller a
reference it already owns; a `T?` temporary is parked and swept at the end of the statement.
Every one of those is `tk_is_counted` answering `1`, and `tk_is_counted` answers for a
nullable row what it answers for the row it encloses (`1` for the box arm).

| the value | `rt_live()` moves by | released when |
|---|---|---|
| `Cell? c = new Cell(1);` | 1 — the cell, exactly as `Cell c` would | the block closes |
| `Cell? c = null;` | 0 | nothing to release; `rc_dec(0)` is a no-op |
| `i64? n = 5;` | 1 — the box | the block closes |
| `i64? n = null;` | 0 | — |
| `n = 7;` (a second store) | 0 net — a new box, the old one released | at the store |
| `Cell?[] cs = new Cell?[3];` | 1 + one per element that is not null | the array's own release walks the elements |

A `struct?` is not counted, exactly as a `struct` is not
([memory.md](../reference/memory.md) § "What is not reclaimed"), and the debt is the one
already declared there.

---

## 8. Definite assignment

The rule ruling 4 asks for, at its **minimum useful strength**: a local declared **without**
`?` and **without** an initializer, read before any assignment to it appears, is refused.

```teko
// no-run
struct Vec { public i64 x; }

i64 main() {
    i64 a;
    i64 b = a;                                    // teko: a is used before it is assigned

    Vec v;
    v.x = 4;                                      // teko: v is used before it is assigned

    Vec w;
    if (b > 0) { w = new Vec(); }
    w.x = 4;                                      // accepted: the analysis is not sure
    return 0;
}
```

**It refuses only what it is sure about.** A name counts as assigned from the moment an
assignment to it appears earlier in source order — including one inside an `if`, a `loop` or
a nested block, which C# would not accept. That over-approximation is deliberate: a false
refusal would break legitimate code (declare first, build in a branch — D42's own example),
and this analysis has no dominator tree. What it catches is the whole class D42 named: a
`struct` or a class local declared and then read with nothing ever written to it, which
today reaches the run as whatever the stack held.

| counts as an assignment | why |
|---|---|
| an initializer, and any later `x = e` | the obvious one |
| `f(out x)`, `f(ref x)` | the callee's own store; C# requires definite assignment for `ref` and teko does not, conservatively |
| a `foreach` variable, a `for` initializer | the construct writes it |
| `use (x)` / `use (&x)` in a lambda | the capture reads it, and refusing a capture is not this crumb's business |

**Out of scope, and stated so:** a field (`rt_alloc` hands out zeroed bytes, so a field
nobody assigned reads as 0 by design), a global (BSS), an element of `new T[n]`, and a
parameter. The analysis is about **locals**, which is where the compiler has the whole
lifetime in front of it.

---

## 9. What rule 1 does **not** prove

A slot written without `?` is a promise the **source** makes; it is not a proof the
compiler can enforce end to end, and pretending otherwise would be the dishonest half of
this design. `null` still reaches a non-nullable slot by three roads:

| road | why | what happens today, and after |
|---|---|---|
| a field of counted type nobody assigned | `rt_alloc` hands out zeroed bytes ([memory.md](../reference/memory.md)) | unchanged: a null delegate field called is `teko: call through a null delegate`, exit 70 |
| an element of `new T[n]` | the same zeroing | unchanged: `index into a null array`, and a null element is a null reference |
| a `struct` local declared without `new`, assigned in a branch that did not run | D42's ruling: the developer's error | unchanged, except that the certain case is now refused at compile time (§ 8) |

Which is why every run-time guard stays exactly where it is, and why `x == null` stays legal
on a non-nullable slot (§ 3).

---

## 10. The migration, fixture by fixture

Rule 1 is a **breaking change**: `Cell c = null;` compiles today (D32: a reference fits any
row) and refuses after Q1a. Twelve lines across nine fixtures are affected, and each one is a
`null` **assignment**, never a comparison. Two shapes of migration, and neither one uses
`.Value` — so the whole migration lands with Q1a and depends on nothing later:

- **shape A — declare the slot `T?`**, where the name is only ever compared against `null`;
- **shape B — close a block around the value's life**, where the name is read afterwards.
  The reclaim already releases at the `}` and on the way out of a `return`
  ([memory.md](../reference/memory.md) § "The destructor runs first" proves it), so
  `x = null;` followed by an `rt_live()` assertion becomes a `}` followed by the same
  assertion.

| fixture | line | shape |
|---|---|---|
| `tests/order_types.tk` | `Box b = null;`, `Op f = null;` | A — `Box? b`, `Op? f`; the `== null` test is unchanged |
| `tests/surface_delegate.tk` | `Op maybe = null;` | A |
| `tests/surface_delegate.tk` | `c = null;` in `rcheck` | B — two blocks, one per cell, the `cell_dtors` assertions between them |
| `tests/surface_lambda.tk` | `b = null;`, `f = null;` in `counted_check` | B — nested blocks, the closure's outlives the box's |
| `tests/surface_params.tk` | `a = null; b = null;` | B |
| `tests/surface_refout.tk` | `c = null;` (twice), `x = null; y = null;` | B |
| `tests/surface_array_heap.tk` | `cs = null;` | B |
| `tests/surface_foreach.tk` | `cs = null;` | B |
| `tests/surface_overload_free.tk` | `held(null)` against `held(Cell)` | A — the overload becomes `held(Cell? c)`, and its body's non-null arm returns a constant instead of calling `c.get()`, which keeps the `1000` oracle and needs no `.Value` |
| `tests/surface_panic_null.tk` | `Op f = null; return f(1, 2);` | **the null arrives through a zeroed field** — `class H { public Op cb; } H h = new H(); return h.cb(1, 2);` — which keeps the exit-70 oracle **and** documents § 9's first road |

`--dump-ast` therefore moves **only** on those nine fixtures, and only by the block insertion
or the type word. The other forty-two must be byte-identical, and that is the crumb's proof.

---

## 11. What stays out

Each row is a `not-yet.md` entry the crumb that lands it owes.

| written | what happens |
|---|---|
| `Nullable<i64>` spelled out | `teko: not a generic type` — the existing message. The spelling is `T?` and there is no `Nullable` type word |
| `T??` | `teko: a nullable of a nullable is not taught` |
| `uptr?`, `ptr?`, `str?` | `teko: a raw pointer has no nullable` — `0` is an ordinary value of a raw pointer, and `null` already lands in one |
| `void?` | `teko: void? is not a type` |
| `a + b`, `a - b`, `a < b`, `a & b` on nullables (C#'s lifted operators) | `teko: no operator + takes these operands`. Lifting arithmetic means a result that is itself nullable at every operator of every primitive and every enum — a surface that grows by a table, for a gain `.Value` and `??` already cover |
| `a == b` on two nullables, before Q4a | `teko: no operator == takes these operands` |
| `ref T?`, `out T?` | `teko: a ref or out of a nullable is not taught yet` |
| `T?` → `U?` where `T` → `U` | `teko: a value of type Circle? does not convert to Shape?` — no covariance between nullable rows; write `x.Value` |
| `x?.m()` where `m` returns `void` | `teko: ?. needs a value` |
| `x.GetValueOrDefault(fallback)` (C#'s one-argument overload) | `teko: unknown member of i64?: GetValueOrDefault` at that arity — `x ?? fallback` is the form |
| `x.Value = e`, `ref x.Value` | `teko: .Value is not a slot` |
| `x is null`, `case null:` | there is no `is`, and a switch takes no nullable subject |
| `x.ToString()` on a nullable | deferred with all text (N2b, N7/N8) |
| flow narrowing (`if (c != null) { c.v }` reading `c` as a `Cell`) | not taught: C# 8's flow analysis is a dominator-based pass this design does not buy. `c.Value.v` is the form |

---

## 12. The hooks, by module

| module | what it grows |
|---|---|
| **`teko_null.tk`** (new, the 32nd module) | `tk_nl_type` (the `syntax_type` handler reading the `?` suffix), `tk_nl_row`/`tk_nl_of` (the row and its enclosed type), `tk_nl_wrap` (the implicit `T` → `T?`), the member lowering for `HasValue`/`Value`/`GetValueOrDefault`, the `??`/`?.` handlers and the pass that rewrites them into `tk_ternary`, and every refusal on this page |
| `teko_struct.tk` | `TK_KNULL` beside the five row kinds; `tk_is_nl`; one clause in `tk_is_counted` (a nullable row answers for what it encloses, `1` for a box); one clause in `tk_row_fits` (`T` and its bases/interfaces fit `T?`); one row in `tk_ty_mangle_name` (`opt_T`) |
| `teko_typeof.tk` | one clause in `tk_check_scalar_compat` (`null` refused unless the target is a nullable row, a raw `uptr` or a `T?`-shaped slot); the `tk_nl_wrap` call at the field store it owns; `tk_ty_of` answering the nullable row for a `.Value`-free nullable node |
| `teko_rc.tk` | the `tk_nl_wrap` call at the six slots it owns — an initializer, an assignment, a `return`, and the three call-argument kinds — each one line beside the `tk_num_widen` call already there |
| `teko_expr.tk`, `teko_iface.tk`, `teko_params.tk` | one `tk_nl_wrap` call each, at the slot each already owns |
| `teko_expr.tk` | one branch in `tk_dot`/`tk_member_of`: a receiver whose type is a nullable row takes the three members and refuses everything else |
| `teko_typeof.tk` | the same branch at `tk_pend_do`, the deferred access a parameter receiver takes |
| `teko_access.tk` | `tk_quest_follows()` beside `tk_bracket_follows()`, so `Cell? c = …;` reaches `parse_var` (which calls `take_type`, which runs the hook) rather than the delegate reader |
| `teko_deleg.tk` | `tk_deleg_var_stmt` reads a `?` before its own initializer (`Op? f = null;`) |
| `teko_heaparr.tk` | `tk_ha_type` looks for a `?` after `[]` (`T[]?`), and `tk_nl_type` looks for a `[]` after `?` (`T?[]`): `take_type` dispatches the chain once per type position, so the two suffixes cooperate in one place each |
| `teko_ref.tk`, `teko_default.tk` | the two edits `T[]` already made at `tk_ref_param`/`tk_default_param`, for the same reason |
| `teko_ops.tk` | one guard: a nullable operand takes `==`/`!=` against `null` and nothing else (Q4a relaxes it to the lifted pair) |
| `teko_generic.tk` | nothing: `tk_gen_ty` already falls back to `p_type()`, which runs the chain |
| `lib/rt.tk` | `tk_nl_new(i64 w)` (allocate, install the vtable, count 1, record the width), `tknl_release(uptr p)` (`rt_free(p, 24 + ld64(p + 16))`), `tk_nl_ck(uptr p)` (the guard and the panic), and one vtable global. About thirty lines |
| `teko.tk` | one `#include`, one `_init()` call, two `syntax_infix` registrations (`??` at 1, `?.` at 12), one `pass` registration ahead of the ternary's |
| `core_teko.mc`, `user.mc` | **nothing** |

The definite-assignment analysis (Q3) rides
[teko_typeof.tk](../../teko_typeof.tk)'s existing walk — the one that already visits every
node in source order under the scope that holds at that node — and adds a bit per local. It
registers a pass of its own only if the probe in Q0 shows the walk cannot see an assignment
before the read.

---

## 13. What it costs in `mc limits`

Measured against D42's own baseline (`types` 11, `alias` 19, `syntax` 15, `passes` 15/30,
`intrin` 8/16).

| row | moves | why |
|---|---|---|
| `types`, `alias` | **not at all** | the compiler registers no new primitive. A nullable row is `type_new` called **while compiling a program that spells `T?`**, and the compiler's own sources spell none |
| `syntax` | **+2** (Q2) | the two operator words, `??` and `?.` |
| `passes` | **+1** (Q2) | the rewrite that runs ahead of `tk_ternary_pass`. 16/30 |
| `intrin` | **not at all** | every function has surface code: three in `lib/rt.tk`, and the loads and stores are `tk_ld`/`tk_stn`, which are the core's own fixed intrinsics already in use |
| `nodes`, `funcs`, `globals` | up | one module of roughly 600 lines, and one vtable global |
| a **compiled program**'s `types` | **+1 per distinct `T?` spelled** | exactly what `T[]` costs, and the registry doubles rather than dies |
| `TK_MAXSTRUCT` (teko's own, 32) | one row per distinct `T?` | shared with every class, struct, interface, delegate and `T[]`; the ceiling is the existing `teko: too many type declarations`. **Measure before raising it** — a raise costs BSS in `globals` and no fixture on this page needs more than four |

---

## 14. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/surface_nullable_ref.tk` | `Cell?`/`IShape?`/`Op?`/`Vec?`/`Cell[]?` in all nine slots; `null` in and out; `HasValue` and `Value`; a derived class into a base nullable; `rt_live()` identical to the same program written with `Cell` | `42` |
| `tests/surface_nullable_value.tk` | `i64?`, `u8?`, `i8?` (a negative value through the box, sign extended), `bool?`, `char?`, `f64?`, `Color?` (an enum), `TimeSpan?`, `DateTime?`; value semantics (`b = a; b = 7;` leaves `a`); `GetValueOrDefault()`; `rt_live()` back to its floor | `42` |
| `tests/surface_nullable_panic.tk` | `.Value` on an empty nullable | `70` |
| `tests/surface_nullable_ops.tk` | `??` on a reference and on a value nullable, evaluating the left side once (a counter proves it); `?.` on a field, on a property and on a method; `a?.b?.c` chained; `??` inside a `return`, an argument and a loop condition | `42` |
| `tests/surface_nullable_rc.tk` | a `Cell?` field released with its object and when overwritten; a `Cell?[]` released element by element; an `i64?[]`; a `T?` parked as an argument temporary; `rt_live()` at zero at the end | `42` |
| `tests/surface_definite_assign.tk` | every shape the analysis **accepts**: a local assigned in one branch and read after, a `struct` built in a branch, an `out` argument, a `foreach` variable, a captured local | `42` |
| the nine migrated fixtures | their own oracles, unchanged | unchanged |

Refusals are proved the way this repository already proves them: probes outside `tests/`
(`build/probe_nl_*.tk`, not committed), enumerated in the crumb's own log entry, plus the
`check-docs.sh` diagnostics check, which fails until every new `teko:` literal appears in
[diagnostics.md](../reference/diagnostics.md).

---

## 15. The crumbs

Each one lands on its own and is gated on its own fixtures.

### Q0 — the probes (S)

Ten questions this design answers from documentation and should answer from measurement
before a line of it is written:

1. does `take_type` reach the `?` at **every** declaration teko owns — a class local through
   `tk_type_stmt` → `parse_var`, a delegate local through `tk_deleg_var_stmt`, a field, a
   method parameter, a method return, a free-function parameter, a cast, an `extern`, a
   generic argument through `tk_gen_ty`?
2. does a second `syntax_type` handler coexist with `tk_ha_type` (registration order, the
   "consumed tokens and returned 0" guard)?
3. does `syntax_infix("??", …)` lex `??` as one token beside the ternary's `?`, and does
   `?.` beat `?` followed by `.`?
4. does a precedence of 1 on `??` really produce the associativity § 5 claims?
5. does `err_at` at the `?` position report the declaration's own line?
6. what does `--dump-ast` print for a `type_new` name carrying `?`?
7. does `tk_ld`/`tk_stn` at `+24` of a box round-trip an `i8`, an `f64`, an `enum : u8` and a
   `TimeSpan` without a cast the later refusal would mistake for a hand-written one?
8. does a 32-byte box come back to the 32-byte free list (`rt_peak()` flat across a million
   iterations)?
9. can teko_typeof.tk's walk see an assignment before the read it has to judge (Q3)?
10. what does `mc limits` say after one extra row is registered while compiling a program?

**Gate:** no fixture — a probe is not a crumb's oracle. **Owes:**
`docs/internals/nullable-probes.md`, the way P0 owes `docs/internals/primitives.md`, with
the measured answer to each of the ten and the citation for it. A probe that answers "no" is
a finding that rewrites the crumb it belongs to, **before** that crumb is dispatched.

### Q1a — `T?` over a reference, and `null` only in a `T?` slot (M)

The type row and the `syntax_type` handler; `T?` at every declaration position;
`tk_is_counted`/`tk_row_fits`/`tk_ty_mangle_name`; the implicit `T` → `T?` (identity here);
the refusal of `null` in a non-nullable slot; `HasValue`, `Value` and the panic;
`tk_nl_ck` in `lib/rt.tk`; the operator guard; **and the whole of § 10's migration**.
No box: the reference arm needs none. Depends on Q0.

**Gate:** `surface_nullable_ref.tk` at `42`, `surface_nullable_panic.tk` at `70`; the nine
migrated fixtures at their own unchanged `expect-exit`; the other forty-two at theirs with
`--dump-ast` **byte-identical** to the base commit — that is the proof the new clauses fire
only where a `?` is written; `FIXPOINT OK`; `mc limits` verdict `ok` with `types`, `alias`,
`syntax`, `passes` and `intrin` **unmoved**; `sh scripts/check-docs.sh` green.
**Owes:** a `T?` section in [types.md](../reference/types.md), the nullable rows in
[memory.md](../reference/memory.md) (the panic, and the counting rule), every new message in
[diagnostics.md](../reference/diagnostics.md), the § 11 rows in
[not-yet.md](../reference/not-yet.md), the new module in
[modules.md](../internals/modules.md), the module count in
[`CLAUDE.md`](../../CLAUDE.md) and [docs/README.md](../README.md), and a section in
[guide/10-values-and-types.md](../guide/10-values-and-types.md).

### Q1b — `T?` over a value (M)

The box: `tk_nl_new`/`tknl_release`/the vtable in `lib/rt.tk`; `tk_nl_wrap` writing the box
at the nine slots; the typed payload load at `.Value`; `GetValueOrDefault()`;
`i64?`/`f64?`/`bool?`/`char?`/`i8?`/`enum?`/`TimeSpan?`/`DateTime?`; `T?[]` and `T[]?`.
Depends on Q1a.

**Gate:** `surface_nullable_value.tk` and `surface_nullable_rc.tk` at `42`; everything Q1a
gated on, `--dump-ast` still byte-identical on the forty-two; `rt_live()` back to its floor
in every reclaim fixture; `mc limits` unmoved. **Owes:** the box in
[memory.md](../reference/memory.md) § "The object" and § "Reading the numbers", and the value
half of the [types.md](../reference/types.md) section.

### Q2 — `??` and `?.` (M)

Two `syntax_infix` registrations, one pass ahead of `tk_ternary_pass`, the rewrite into
`tk_ternary`, the single evaluation of the left side, the `void` refusal.
Depends on Q1b (a `?.` on a value member produces a boxed nullable).

**Gate:** `surface_nullable_ops.tk` at `42`; everything above; `mc limits` with `syntax`
+2 and `passes` +1 and **nothing else moved**. **Owes:** the operator rows in
[types.md](../reference/types.md) and the precedence divergence in
[not-yet.md](../reference/not-yet.md).

### Q3 — definite assignment (M)

The bit per local on teko_typeof.tk's walk, the four "counts as an assignment" cases, and
the one refusal. Depends on nothing in Q1/Q2 — it can land before them — but it is written
**after**, because D42 hands it to this design and this design is what makes the refusal
answerable ("declare it `T?`").

**Gate:** `surface_definite_assign.tk` at `42`; all fifty-one at their `expect-exit` with
`--dump-ast` byte-identical (the crumb refuses and rewrites nothing); the probe set proving
each refusal and, more importantly, the **absence** of a false refusal on the shapes § 8
lists as accepted. **Owes:** the section in [types.md](../reference/types.md), the message in
[diagnostics.md](../reference/diagnostics.md), and the D42 cross-reference in
[not-yet.md](../reference/not-yet.md).

### Q4a — lifted `==` and `!=` (S, optional)

`a == b` on two nullables of the same row: both empty is equal, one empty is not, both full
compares the values. Nothing else lifts. Depends on Q1b.

**Gate:** an extension of `surface_nullable_value.tk`; everything above.
**Owes:** the row moves out of [not-yet.md](../reference/not-yet.md).

### Q4b — lifted arithmetic and ordering

**Not planned.** § 11's row says so, and it stays a row until a program asks for it.

---

## 16. Where it lands in the order

[README.md](README.md)'s sequence puts `Nullable<T>` outside itself and defers to
[string.md](string.md) § 11. That is superseded: **Q0 → Q1a → Q1b → Q2 → Q3 land next, ahead
of every remaining type crumb.**

| crumb | before | why |
|---|---|---|
| **N7/N8 `string`** | yes, mandatory | `string` is a counted class, so `string? s = null;` is the spelling from its first day. Landing `string` first means writing `string s = null` into the fixtures, the guide and the reference and migrating all of it a second time |
| **C3 `decimal`, N3 `Guid`** | yes | both are sixteen-byte values; `decimal?`/`Guid?` cost each of those crumbs **one row** in this design's typed load/store pair, and nothing else. Landing `Nullable` first is what makes that one row a line instead of a crumb |
| **N2b `enum` text, N2c `DateTimeKind`, N4/N5 the date types** | yes, by convenience | none of them depends on `T?`; all of them would write a `TryParse`-shaped `out` where a `T?` return is the C# form |
| **`object`** | after | `object` is the item [string.md](string.md) § 11 says runs into D4, and it is not this page's business |

The dependency in the other direction is empty: `Nullable` depends on **nothing** that is
not already landed. `enum`, `TimeSpan` and `DateTime` are in the tree (D39/D40/D41), and a
type that arrives later gets its nullable for the cost of a row.

---

## 17. What the `mc` channel is asked

**Nothing.** Every mechanism this page uses exists in the pinned `mc`: `syntax_type` (the
hook whose own documentation names `?` as an example suffix), `syntax_infix` with a taught
punctuation lexeme, `pass`, `type_new`, and the parser API. Zero changes to `mc`'s core,
zero new intrinsics, no new machine module, no new opcode.

**One conditional ask, and only if Q0 measures it.** If probe 1 finds a declaration position
teko owns where `take_type` is never reached **and** teko cannot reach the hook from its own
reader either, that position is a gap on `mc`'s side, and it is filed the way this port has
always filed one: a minimal, pure-`mc` reproducer to `minicompiler/mc`, through the notices
file, never a workaround here. Everything else on this page is designed and buildable
without it; the crumb that needs the position would carry that position alone and would
unblock on whichever `mc` release answers.

---

## 18. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **`i64? x = 5;` allocates, and C#'s `int?` does not.** | Named in § 2 rather than hidden, measured by `surface_nullable_value.tk`'s `rt_live()`/`rt_peak()` assertions, and bounded by the free list that already recycles a churned block. The alternatives that avoid it (a wide value, a frame cell) each buy the allocation with a new lifetime rule at seven positions — the exact class of change that produces a silent miscompile, which this repository forbids ahead of performance |
| **The box carries a vtable word, and D4 forbids a run-time tag.** | Nothing reads it to decide a meaning: it is the reclaim's release pointer, the same word a `T[]` carries. `i64?` is a distinct static type, the payload's type is known at every site, and there is no `is`, no unbox and no dynamic dispatch anywhere in the design |
| **Rule 1 breaks nine fixtures on day one.** | § 10 gives the migration line by line, in two mechanical shapes, neither of which needs anything later than Q1a. `--dump-ast` moving on exactly those nine and on no other is the crumb's own gate |
| **Rule 1 does not prove non-nullness** (§ 9). | Written down as a limit rather than sold as a guarantee: the run-time guards stay, `x == null` stays legal on a non-nullable slot, and § 9 is a section of the reference page the crumb owes, not a footnote |
| **`c.v` on a `Cell?` is refused, and it is the first thing a C# reader writes.** | It is `Nullable<T>`'s own rule, which C# applies to `int?` and would apply to `Cell?` if `Cell?` were a `Nullable<Cell>`. The message names the two forms that work, and `?.` (Q2) is the one a C# reader reaches for anyway. Flow narrowing is a row in not-yet, honestly priced |
| **`??` cannot sit where C# puts it**, because `mc`'s table starts at 1. | § 5: tie it at 1, document the one shape that differs, do not renumber the base grammar (D3) |
| **Two suffixes now compete at one type position** (`[]` and `?`). | `take_type` dispatches the chain once per position, so the cooperation is two lines in each of two handlers, and `T[][]`'s existing refusal is the precedent for how the second suffix is judged |
| **`TK_MAXSTRUCT` is 32 and a nullable row consumes one.** | The ceiling already exists with a message (`teko: too many type declarations`), and a raise is BSS. Measure in Q1a; raise only against a fixture that proves it |
| **The definite-assignment analysis is weaker than C#'s.** | Deliberately: it never refuses what it is not sure about, so it can never break a correct program. The gap is a not-yet row, and the strong version is a dominator pass nobody has asked for |
| **`GetValueOrDefault()` diverges from C# on a reference nullable.** | Ruling 1 beats C# fidelity here: `default(T)` for a reference is `null`, and handing a `null` to a `T` slot is what the whole page exists to stop. The refusal names `??`, which is the form that says which default it means |

---

## 19. The decisions this design proposes

Numbered when they enter [`DECISION_LOG.md`](../../DECISION_LOG.md), after D42.

1. **`T?` is `Nullable<T>` over any type, and the representation is a pointer-width handle
   whose `0` is the absent value** — the enclosed reference itself for a reference or a
   struct, an immutable counted box for a value. One rule on the surface, one invariant
   underneath, two storages.
2. **`null` lands only in a slot declared `T?`; a comparison against `null` stays legal
   anywhere a reference-shaped value does.** The first half supersedes D32's "a reference
   fits any row" for the **store** direction; the second half is what keeps the run-time
   guards and the defensive code that § 9 requires.
3. **`Nullable<T>` is a construct of the compiler, not an instance of the generic
   mechanism** — the same standing `T[]` and `params T[]` already have, and the reason a
   generic `struct Nullable<T>` would be a second, weaker nullable.
4. **A local declared without `?` and read before any assignment is refused, and the
   analysis over-approximates so that it never refuses a correct program.** This is D42's
   deferred half, landed here.
