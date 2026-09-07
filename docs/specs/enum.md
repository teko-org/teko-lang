# `enum`

**Designed, not built.** Nothing on this page compiles today; every sample carries
`// no-run` for that reason. What runs is [the type reference](../reference/types.md), and
this page is kept apart from it on purpose ([the specs index](README.md)).

`enum` is C#'s: a named set of integer constants with an **underlying type**, a distinct
type of its own that no integer converts into without being told to, comparison and the
bitwise operators, and a `switch` that reads like a `switch` over the names.

It is the cheapest real feature in this plan, and the reason is that teko already does
every part of it for something else. A class is `type_new(name, 8, 8, TK_INT)` plus
`syntax_expr(name, …)`, both **at parse time**, in `teko_struct.tk` and `teko_access.tk`
today. A `const` member is a folded value in a table keyed by its qualified name, in
`teko_const.tk` today. An `enum` is those two mechanisms, pointed at each other.

---

## 1. Representation

```
enum Color { Red, Green, Blue }        ->  type_new("Color", 4, 4, TK_SINT)
enum Mask : u8 { None, A, B }          ->  type_new("Mask",  1, 1, TK_INT)
```

One `type_new` per declaration, with the **width, alignment and kind of the underlying
type** — `i32` by default, C#'s own default. The eight underlying types C# allows, in
teko's spelling:

| written | `type_new` | note |
|---|---|---|
| (nothing) | `(4, 4, TK_SINT)` | C#'s default, `int` |
| `: u8` `: u16` `: u32` `: u64` | `(1,1,TK_INT)` … `(8,8,TK_INT)` | `byte`, `ushort`, `uint`, `ulong` |
| `: i8` `: i16` | `(1,1,TK_SINT)` `(2,2,TK_SINT)` | `sbyte`, `short` — **needs `docs/specs/small-ints.md`'s N0** |
| `: i32` `: i64` | `(4,4,TK_SINT)` `(8,8,TK_SINT)` | `int`, `long` |

The kind is what buys the signed comparison and the signed `/` for a signed underlying
type, from the core and from a machine that honours the kind, with no line of teko's — the
same one-line argument `docs/specs/small-ints.md` § 1 makes.

A value of an enum type **is** its underlying integer at run time. There is no box, no tag
and no indirection: `Color` is four bytes in a frame slot, a field, an array element, a
parameter and a return, and it crosses an `extern` as the C `enum` it is.

**An `enum` id is `TK_SINT` and is not a number.** That is the trap
`docs/specs/small-ints.md` § 2 names: `tk_is_int_ty` must not be `type_kind(t) == TK_SINT`,
or `f64 x = Color.Red;` would compile.

## 2. Declaring

```
[public|internal] enum Name [: underlying] { Member [= const] , ... [,] }
```

- Members take the **previous value plus one**, starting at `0`, which is C#'s rule.
- An explicit value is any expression the core's folder reduces — an integer literal, a
  declared `const`, or arithmetic over them. It is the same folding `teko_const.tk` does
  and the same `docs/reference/generics.md` accepts for a `const` argument.
- **Duplicate values are legal** (C# calls them aliases); duplicate **names** are refused.
- A trailing comma is accepted, as C# accepts it.
- `public`/`internal` are the same two words and the same default a class has (D6:
  `internal`), read by the same `tk_public`/`tk_internal` handlers.
- An `enum` inside a namespace is mangled `geo__Color`, exactly as a class is
  (`teko_ns.tk`), and is reached as `geo.Color`.
- An `enum` inside a type is refused: `teko: a type is declared at top level; there is no
  type inside a type` (D6, and the message already exists).
- An `enum` with no member is refused: `teko: an enum declares at least one member` —
  `mc`'s own demo handler refuses it too, for the same reason.

### Where each member goes

Two tables, both of which exist:

1. **The qualified constant table** (`teko_const.tk`'s own, the one that answers `Nome.MAX`
   and `geo.N`), under the key `Color__Red`. This is what makes `case Color.Red:` work with
   **zero lines in `teko_switch.tk`** — `docs/reference/not-yet.md` records that a qualified
   `const` already resolves as a case label, and an enum member is one.
2. **The enum's own row** — `(type id, member name, value)` — which is what types the node.
   `Color.Red` is an `N_INT` of `0` with `set_nd_type(n, ty_Color)`, and that single line is
   the whole difference between an `enum` and `mc`'s demo `enum`, which is a `type_alias`
   over `i64` and therefore assignable to any integer.

The **bare** name is deliberately not defined program-wide. `mc`'s demo writes
`def_add(p_ident(), v, …)`, which would make `Red` mean `0` in every file; C# requires
`Color.Red` and teko follows it. The one place C# allows the short name — a `case` arm of a
`switch` on that enum — is not taught either, and is a `not-yet.md` row.

## 3. The surface

**Legal.**

```teko
// no-run
public enum Color { Red, Green, Blue }

enum Level : u8 { Low = 10, Mid, High }          // 10, 11, 12

enum Perm { None = 0, Read = 1, Write = 2, All = 3 }

i64 describe(Color c) {
    switch (c) {
        case Color.Red:   return 1;
        case Color.Green: return 2;
        default:          return 3;
    }
}

i64 main() {
    Color c = Color.Green;
    if (c != Color.Green) return 1;
    if (c == Color.Red) return 2;
    if (describe(c) != 2) return 3;

    if (Level.Mid != (Level) 11) return 4;        // an explicit cast, both ways
    if ((i64) Level.High != 12) return 5;

    Perm p = Perm.Read | Perm.Write;              // the bitwise trio, C#'s own rule
    if (p != Perm.All) return 6;
    if ((p & Perm.Read) == Perm.None) return 7;

    Color tbl[3];
    tbl[0] = Color.Blue;
    if (tbl[0] != Color.Blue) return 8;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
enum Color { Red, Green }
enum Size  { Small, Big }

i64 main() {
    Color c = Color.Red;
    i64 n = c;                    // teko: a value of type Color does not convert to i64
    Color d = 0;                  // teko: a value of type i64 does not convert to Color
    if (c == 0) return 1;         // teko: no operator `==` takes these operands
    if (c == Size.Small) return 2;// teko: no operator `==` takes these operands
    Color e = c + Color.Green;    // teko: no operator `+` takes these operands
    Color f = Color.Blue;         // teko: Color has no member Blue
    return 0;
}
```

| written | message |
|---|---|
| an enum value in an `i64`/`f64` slot | `teko: a value of type Color does not convert to i64` |
| an integer in an enum slot, **including the literal `0`** | `teko: a value of type i64 does not convert to Color` |
| an enum compared with an integer | ``teko: no operator `==` takes these operands`` |
| two **different** enums compared or combined | ``teko: no operator `==` takes these operands`` |
| `+ - * / % << >>` on an enum | ``teko: no operator `+` takes these operands`` |
| an unknown member | `teko: Color has no member Blue` |
| the bare member name (`Red`) | it is an ordinary identifier and resolves to nothing: `mc: unknown name` |
| an `enum` inside a class or struct | `teko: a type is declared at top level; there is no type inside a type` |
| `enum Empty { }` | `teko: an enum declares at least one member` |
| a duplicate member name | `teko: duplicate enum member: Red` |
| `[Flags]` | `mc`'s own parse error: teko has no attribute grammar (§ 7) |

**The literal `0` is refused, and that is a deliberate divergence from C#.** C# lets `0`
convert implicitly to any enum, so `Color c = 0;` and `if (p & Perm.Read) != 0` compile
there. It is a wart C# itself steers people away from, it is the single hole through which
an untyped integer would reach an enum slot, and the replacement reads better:
`Perm.None`, which the program declares. Recorded as a divergence rather than a gap.

## 4. Operators

Two of the three groups need **no lowering at all**, and that is the point of registering
the enum with the underlying type's own width and kind.

| written | what happens | result |
|---|---|---|
| `a == b`, `!=`, `<`, `<=`, `>`, `>=`, both the same enum | the core's own comparison, signed or unsigned by the kind | `i64` 0/1 |
| `a & b`, `a \| b`, `a ^ b`, `~a`, both the same enum | the core's own bitwise instruction; `res_binary` types the result from the left operand, which is the enum | the enum |
| everything else | refused by `teko_ops.tk` before it reaches the machine | — |

`teko_ops.tk` gains one rule, and it is a **refusal** rather than a lowering: a binary or
unary node with an enum operand is claimed, and every combination outside the two rows
above is refused by name. That is the same guard `docs/specs/datetime.md` § 13 puts on
`TK_SINT`, for the same reason — a `TK_SINT` id makes the core *willing* to do arithmetic
on the raw bits, and a construct that reaches the machine unclaimed is a wrong answer
rather than a diagnostic.

`| & ^ ~` are legal on **every** enum, not only on a "flags" one. That is C#'s rule
exactly: `[Flags]` changes `ToString` and nothing else.

## 5. Conversions

| from | to | how | why |
|---|---|---|---|
| an enum | its underlying type, or any integer | **explicit**: `(i64) c` | C# §10.3.3 |
| any integer | an enum | **explicit**: `(Color) n` | C# §10.3.3; **not** range-checked, as C# does not check it either |
| an enum | another enum | **explicit**, through the integer: `(Size) (i64) c` | C# needs the same two casts |
| an enum | `f32`, `f64` | refused | C# has none |
| an enum | `str`, `string` | `.ToString()` | never implicit |
| `null`, a class, a struct, a `T[]` | an enum | refused (D32/D34) | |

Both explicit directions are **a machine cast and nothing more**: two integer slots of
known width and kind, so `MTASK_CAST` does the whole job and `tk_cast` (teko_array.tk)
passes the node through untouched. There is no call, no runtime function and no allocation
anywhere in this section.

The refusal side is `tk_check_scalar_compat`'s (teko_typeof.tk), through **one clause**:
a value whose type is a teko-registered primitive outside `tk_is_int_ty` converts to
nothing but itself, in either direction. That is the same clause
`docs/specs/datetime.md` § 5 adds for `DateTime`, written once and read by both; whichever
of the two crumbs lands first writes it, and the other finds it there. It covers all nine
slots D33 enumerated at once, because they all end in that one function.

## 6. `ToString`, `Parse` and the rest

These are the only part of `enum` that is not free, and they are a **second crumb** (§ 11,
N2b) because they need the primitive-member lowering table
(`docs/specs/datetime.md` § 2), which `TimeSpan` brings.

| static | instance |
|---|---|
| `Color.Parse(str)`, `TryParse(str, out Color)` | `.ToString()` |
| `Color.IsDefined(i64)` | `.CompareTo(Color)`, `.Equals(Color)` |
| `Color.GetNames()` → `str[]`, `GetValues()` → `Color[]` | — |

The names live in **two ordinary globals per enum**, emitted by the declaration handler:

```
str Color__names[3] = { "Red", "Green", "Blue" };
i64 Color__vals[3]  = { 0, 1, 2 };
```

A global array whose elements are string literals is an ordinary `mc` initializer — proved
on the tree, not assumed — so there is no `N_BLOB`, no relocation the compiler has to write
by hand, and no generated function body per enum. One shared runtime pair in `lib/rt.tk`
reads them:

```
str tk_enum_name(uptr names, uptr vals, i64 n, i64 v);   // the digits when no name matches
i64 tk_enum_value(uptr names, uptr vals, i64 n, str s);  // -1 when no name matches
```

`ToString()` returns the member's name, or the decimal digits when the value matches none —
C#'s own behaviour for a value cast in from outside the set. `Parse` panics on a name that
is not a member (`teko: the string is not a Color`); `TryParse(s, out c)` answers `0`/`1`.
A **flags-style** `ToString` that decomposes `Perm.All` into `"Read, Write"` is C#'s
`[Flags]` behaviour and is not taught: teko has no attribute grammar, and inventing one for
a formatting rule would be a language feature bought for a string.

## 7. What stays out

| left out | why |
|---|---|
| `[Flags]` | teko has no attributes; it changes only `ToString`, and the operators are already C#'s for every enum |
| the bare member name inside a `switch` on that enum | C# allows it; teko requires `Color.Red` everywhere, one rule instead of two |
| `Color c = 0;` and `p != 0` | § 3: the one implicit hole, deliberately closed |
| an enum as a dependency-injection key, a generic argument or a `delegate` return | none is refused on principle; none is measured by this design |
| `enum` members with an underlying `f64` | C# has none either |
| `Enum.GetName<T>`, `Enum.Parse<T>` | the generic-static form needs generics over a primitive; the per-enum statics of § 6 are the same surface without it |

## 8. What `DateTimeKind` becomes

`docs/specs/datetime.md` § 1 registers `DateTimeKind` as `type_alias` over `i32` with three
constants beside it, and its § 8 says plainly that `enum` is a crumb of its own. When this
page lands **first** — and § 11 puts it first, because it is cheaper — that page writes

```teko
// no-run
public enum DateTimeKind : i32 { Unspecified = 0, Utc = 1, Local = 2 }
```

in `lib/time.tk` instead, and **not one line of its surface changes**:
`leap.Kind != DateTimeKind.Unspecified` is spelled identically either way, and its fixture
`tests/primitives_datetime.tk` is unchanged. What changes is under it, and all of it is an
improvement:

- `dt.Kind` is typed `DateTimeKind` rather than `i32`, so `i64 k = dt.Kind;` is refused
  where it used to convert;
- `DateTime.SpecifyKind(d, 7)` is refused, where an `i32` alias would take any integer;
- the `alias` row of `mc limits` does not move (14 stays 14) and the `types` row takes one
  more, which is the trade this page makes everywhere.

If `datetime.md`'s C2 lands **before** this page, nothing breaks and the change is a
three-line follow-up inside `lib/time.tk`, carried by N2a's own gate.

## 9. The hooks, by module

| module | what it grows |
|---|---|
| **`teko_enum.tk`** (new) | the declaration handler (`syntax("enum", &tk_enum)`), the per-enum `type_new` and `syntax_expr` registration, the member table, the two name/value globals, and the `Type.Member` resolution |
| `teko_const.tk` | nothing new: the members are written into the qualified table it already owns, under `Color__Red`, so `case Color.Red:` and a `const`-position reference both work through code that exists |
| `teko_access.tk` | `Color.Red` reaches `tk_type_expr`'s road — the same `syntax_expr(name, …)` a class name gets at line 588 today — and the member lookup falls through to the enum table when the name is not a class row |
| `teko_typeof.tk` | `tk_ty_of` answers an enum member's own type; `tk_check_scalar_compat` gains the § 5 clause (shared with `docs/specs/datetime.md`) |
| `teko_ops.tk` | one claim-and-refuse rule for a binary or unary with an enum operand (§ 4) |
| `teko_ns.tk` | an `enum` name is mangled like a class name inside a `namespace` |
| `teko_switch.tk` | **nothing**: a qualified folded constant is already a legal case label |
| `teko.tk` | `#include` and `syntax("enum", &tk_enum)` |
| `lib/rt.tk` | the two functions of § 6, and only when N2b lands |
| `core_teko.mc`, `user.mc` | nothing: `teko_init()` is still the one registration site |

The module count in [`CLAUDE.md`](../../CLAUDE.md) and [docs/README.md](../README.md) moves
from 31 to 32 files; that is part of what the crumb owes.

## 10. What it costs in `mc limits`

Baseline as `docs/specs/decimal.md` § 10 measured it: `types 7/14`, `alias 14/28`,
`syntax 14/28`, `passes 15/30`, `intrin 8/16`, verdict `ok`.

| row | today | after | why |
|---|---|---|---|
| `types` | 7 | **+1 per `enum` a program declares** | the compiler's own floor does not move: no enum is registered by teko itself |
| `syntax` | 14 | **15** | one, for the `enum` word |
| `alias` | 14 | 14 | none |
| `passes` | 15 | 15 | the operator rule rides `tk_ops_pass`, the member lookup rides the parse |
| `intrin` | 8 | 8 | teko still registers none |

An `enum` costs a program exactly what a `class` costs it — one `type_new` and one
`syntax_expr`, both at parse time — which is the cost model
`docs/reference/types.md` § Limits already publishes ("types declared in one source: 32").

## 11. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/surface_enum.tk` | implicit and explicit member values; the default underlying type; `: u8` and `: i64`; both explicit casts; the six comparisons; `\| & ^ ~`; an enum field, global, array element, parameter and return; a duplicate value used as an alias | `42` |
| `tests/surface_enum_switch.tk` | `switch` over an enum with `case Color.Red:`, a fallthrough pair, `default`, and a `switch` **expression** arm over the same labels | `42` |
| `tests/surface_enum_ns.tk` | an `enum` inside `namespace geo`, reached as `geo.Color.Red` and, under a `using`, as `Color.Red` | `42` |
| `tests/surface_enum_text.tk` | `ToString` on a member and on a value cast in from outside the set; `Parse` round-trip; `TryParse` on a good and a bad name; `IsDefined`; `GetNames().Length` | `42` |
| `tests/surface_enum_parse_bad.tk` | `Color.Parse("Mauve")` | `70` |

The refusals of § 3 have no harness (D33's own note) and are documented with a `// no-run`
fence in [diagnostics.md](../reference/diagnostics.md) and this page.

## 12. The crumbs

### N2a — the type, the members, the operators, the `switch` (M)

`teko_enum.tk`; the parse-time `type_new` + `syntax_expr` pair; the member table and the
write into `teko_const.tk`'s qualified table; the two explicit casts; `teko_ops.tk`'s
claim-and-refuse; the `tk_check_scalar_compat` clause if no earlier crumb wrote it.
**Depends on `docs/specs/small-ints.md`'s N0** for `: i8` and `: i16`, and on nothing else —
in particular **not** on the primitive-member mechanism, which is why it lands ahead of it.

**Gate:** `tests/surface_enum.tk`, `_switch.tk` and `_ns.tk` at `42`; the other fixtures at
their `expect-exit` with `--dump-ast` byte-identical; a probe program proving
`i64 n = Color.Red;` and `Color c = 0;` are both refused; `FIXPOINT OK`; `mc limits`
verdict `ok` with `passes` and `intrin` **not moved**; `sh scripts/check-docs.sh` green.
**Owes:** an `enum` section in [types.md](../reference/types.md), the six refusals in
[diagnostics.md](../reference/diagnostics.md), the two `not-yet.md` rows (`[Flags]`, the
bare member name in a `case`), the new module in
[modules.md](../internals/modules.md), and the module count in
[`CLAUDE.md`](../../CLAUDE.md) and [docs/README.md](../README.md).

### N2b — `ToString`, `Parse` and the statics (S)

The two globals per enum, the two `lib/rt.tk` functions, and the member rows.
**Depends on N2a and on `docs/specs/datetime.md`'s C1** for the primitive-member lowering
table.

**Gate:** `tests/surface_enum_text.tk` at `42` and `_parse_bad.tk` at `70`; everything N2a
gated on. **Owes:** [runtime.md](../reference/runtime.md) and the `enum` section of
types.md.

### N2c — `DateTimeKind` becomes an `enum` (S)

§ 8, three lines inside `lib/time.tk`. **Depends on N2a and on
`docs/specs/datetime.md`'s C2**, and is unnecessary if C2 lands after N2a — in which case
C2 simply writes the `enum` in the first place.

**Gate:** `tests/primitives_datetime.tk` unchanged and still at `42`; `mc limits` `alias`
back to 14. **Owes:** one row of `docs/specs/datetime.md` § 1, when that page is merged.

## 13. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **A `TK_SINT` id makes the core willing to add two enums.** Nothing stops `res_binary` typing `Color.Red + Color.Green` and emitting an `add` if the operator pass ever fails to claim it. | § 4's rule claims **every** binary and unary with an enum operand and refuses the ones with no row. The fixture covers all the combinations of two enums, one enum and an integer, and one enum and an `f64`. It is the same resolution `docs/specs/datetime.md` § 13 reaches for the same hazard, and the two rules are one rule in one place. |
| **`type_new` and `syntax_expr` at parse time.** An `enum` registers a type in the middle of a compile. | It is not new and it is not a probe: `teko_struct.tk` line 630 and `teko_access.tk` lines 587-588 do exactly this for every declared class, struct and interface, and `mc`'s own hooks documentation shows an `enum` handler calling `type_alias` at that position as its worked example. What does **not** work is a forward reference — an enum used above its declaration — and that is `teko_fwd.tk`'s existing job, which the crumb wires the enum name into rather than reinventing. |
| **The literal `0` refusal diverges from C#.** | § 3, deliberately. It is the one implicit door into an enum slot, C# steers people away from it, and `Perm.None` is the replacement. Recorded as a divergence in this page and in `not-yet.md`, so a reader coming from C# finds it where they look. |
| **No `[Flags]`, because there are no attributes.** | The operators are C#'s for every enum, so the only thing missing is a formatting rule. It is a `not-yet.md` row, not a design hole, and an attribute grammar bought for one `ToString` would be the wrong trade. |
| **A member name that collides with a `const`.** `Color__Red` goes into `teko_const.tk`'s table, which already refuses a redefinition (`def_add` rejects an already-defined name). | Keep the refusal and let it fire: `teko: duplicate constant: Color__Red`. A program can only hit it by declaring a `const` with a doubled underscore inside a type of the same name, which is the naming convention teko already publishes for a static member. |
| **`types` grows per enum.** A program with twenty enums spends twenty ids before it declares a class. | It is what a class costs, the table grows rather than caps (`ty_reg_add` uses `grow`), and `docs/reference/types.md` § Limits already publishes "types declared in one source: 32" as the working number. If a real program crosses it, the answer is `[limits] tolerance`, the same one `docs/specs/datetime.md` § 13 gives. |

## 14. What the `mc` channel is asked

**Nothing.** `type_new`, `type_alias`, `syntax`, `syntax_expr`, `def_add` and the parse-time
registration road are all released on the pinned `0.15.22`, and `mc`'s own
`lib/user_syntax_demo.mc` ships an `enum` handler on that road.
