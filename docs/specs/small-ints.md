# The integers C# has and teko does not: `i8`, `i16`, `i128`, `u128`

**Designed, not built.** Nothing on this page compiles today; every sample carries
`// no-run` for that reason. What runs is [the type reference](../reference/types.md), and
this page is kept apart from it on purpose ([the specs index](README.md)).

Four integers are missing from teko's scalar row, and they split into two halves that share
nothing but the word "integer".

- **`i8` and `i16`** — C#'s `sbyte` and `short`. They are **one `type_new` line each**,
  because `mc`'s fifth kind `TK_SINT` makes the core do all of it: the sign-extending load,
  the sign-extending cast, the signed `/ % >>`, the signed comparison and the narrowed call
  result. This half is the cheapest crumb in the whole plan.
- **`i128` and `u128`** — C# 11's `Int128` and `UInt128`. They are sixteen-byte values, and
  they ride the machine module and the limb arithmetic the `decimal` design already
  builds (`docs/specs/decimal.md` § 1, § 2 and § 8). This half is expensive and lands late.

---

## 1. `i8` and `i16`: the registration

```
type_new("i8",  1, 1, TK_SINT)
type_new("i16", 2, 2, TK_SINT)
```

Two lines in `teko_type.tk`, beside the seven `type_alias` calls already there. That really
is the whole representation, and the reason is `mc`'s own: since M45 the core reads
`type_kind` in exactly three places, and all three answer for a registered `TK_SINT`
without a line of teko's ([`mc` hooks.md](https://github.com/minicompiler/mc/blob/main/docs/reference/hooks.md)
§ `type_new`, [language.md](https://github.com/minicompiler/mc/blob/main/docs/reference/language.md) § 2).

| what the program writes | who does it | what it costs teko |
|---|---|---|
| `i8 b; b = -5;` | the registry: `type_width` sizes the slot, `type_align` aligns it | nothing |
| reading `b` back | the machine's narrow load **by kind** — `ldrsb` on AArch64, `movsx` on x86-64 | nothing |
| `(i8) n` | `MTASK_CAST` **by kind** — `sxtb` / `movsx` | nothing |
| `a / b`, `a % b`, `a >> 1` | `type_signed(t)`, which is `t == TY_I64 \|\| type_kind(t) == TK_SINT` | nothing |
| `a < b` | the same, in the comparison | nothing |
| `i8 f(); ... f()` | `walk_narrow`: a call's result is extended from the **declared** type's width by its kind | nothing |
| `-3 + 1` folded at compile time | `fold_taught` runs for `TK_INT` and `TK_SINT` | nothing |
| `i8 tbl[8];`, an `i8` field, an `i8` array element | `type_width`/`type_align` again | nothing |
| `extern i8 f();`, `extern i16 g();` | C's `signed char` and `short`, narrowed on the way back | nothing |

The machine contract that promises it is version 4 —
[machine.md](https://github.com/minicompiler/mc/blob/main/docs/reference/machine.md) § 3:
"a machine owes extension **by kind** — zero above the width for a `TK_INT`, the sign for a
`TK_SINT` — in its loads and its `MTASK_CAST`". Both bundled machines honour it by width
and kind, **never by the id** (`mc`'s own `mc/src/machine_arm64.mc` asks
`type_kind(ty) == TK_SINT`, and so does its x86-64 twin), which is precisely what makes a
module's own id work.

So `i8` and `i16` need no derived machine, no `syntax_lit`, no `syntax_expr`, no
`intrinsic`, no pass and no runtime function. They need the two lines above and one
predicate, § 2.

### The words

The words are **`i8` and `i16`**, not `sbyte` and `short`. That is not a preference: the
base grammar is `mc`'s and is reused as it is (D3), `mc` already names the family `i32` and
`i64`, and teko never inherited C#'s `int`/`long` either — `docs/reference/types.md` § Scalars
is the eight core words plus seven aliases, and `int` is not among them. Registering
`short` while `int` does not exist would be half a family, which reads worse than neither
half.

The C# alias family — `sbyte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong` — is one
`type_alias` line each and **is a single decision, all seven or none**. It is not taken
here, and it is the one genuinely open fork on this page (§ 12).

## 2. `tk_is_int_ty`, and why `TK_SINT` is the wrong question

`tk_is_int_ty` (teko_typeof.tk) is the predicate D33 built the whole
integer-to-float conversion on, and D34 the reference refusal. Today it reads

```
i64 tk_is_int_ty(i64 t) { return (t >= TY_U8 && t <= TY_I64) || t == TY_MAX; }
```

`t == TY_MAX` is `i32`: the core registers it before any module's init runs, so it always
lands on the first registered id, `7`. Two things are wrong with that line the moment this
page lands.

1. **It writes an id down as a number.** `mc`'s own rule is that the id a registration
   returns is not surface (`hooks.md` § `type_new`: "a module keeps it in a global rather
   than writing the number down"), and the core exports `ty_i32` for exactly this.
2. **`i32` stops being the only registered integer**, so a range or a single equality
   cannot be extended by guessing.

The tempting repair is `type_kind(t) == TK_SINT`, and it is **wrong** — it would be a
silently wrong answer, which is the one outcome D20 rules out. `TK_SINT` is the kind
`docs/specs/datetime.md` registers `TimeSpan` and `DateTime` with, and it is the kind every
`enum` on `docs/specs/enum.md` is registered with. None of the three is a number:

- `tk_num_widen` would wrap a `DateTime` in a cast to `f64` and hand the ticks to `scvtf`;
- `tk_check_scalar_compat` would read a `DateTime` slot as a numeric one and accept a bare
  `i64` into it, which § 5 of the `DateTime` design refuses by name;
- `Color.Red` would convert to an `f64`, which C# refuses in both directions.

**The final form is a positive list, and it names its three ids:**

```
i64 tk_is_int_ty(i64 t) {
    return (t >= TY_U8 && t <= TY_I64)          // the core's five; TY_UPTR is not one
        || t == ty_i32                           // the core's own registration, by name
        || t == tk_ty_i8 || t == tk_ty_i16;      // this page's two
}
```

and the **rule** that goes with it, which is what stops the next registration getting it
wrong: **a `type_new` id joins `tk_is_int_ty` only when the module that registered it says
so by name.** `decimal`, `DateTime`, `TimeSpan`, `Guid`, `DateOnly`, `TimeOnly`,
`DateTimeOffset`, every `enum`, `ref`, `out`, `params`, and every class, struct, interface
and delegate teko registers with `type_new(name, 8, 8, TK_INT)` are outside it by
construction, and none of them has to be listed anywhere for that to hold.

`i128` and `u128` are **also outside it**, deliberately: they are integers, but
`tk_num_widen` writes a machine cast, and a machine cast on a `TK_WIDE` value has no
meaning (`docs/specs/decimal.md` § 6 makes the same call for `decimal`). Their conversions
are explicit calls, § 8.

## 3. The surface, `i8` and `i16`

**Legal.**

```teko
// no-run
#include "rt.tk"

i16 clamp16(i32 v) {
    if (v > 32767) return 32767;
    if (v < 0 - 32768) return 0 - 32768;
    return (i16) v;
}

i64 main() {
    i8  a = 0 - 5;
    i16 b = 0 - 300;
    if (a >> 1 != 0 - 3) return 1;               // an arithmetic shift: TK_SINT
    if (b / 2 != 0 - 150) return 2;              // a signed divide
    if (a >= 0) return 3;                        // a signed comparison
    i64 wide = a;                                // widens with its sign
    if (wide != 0 - 5) return 4;
    f64 f = a;                                   // C# §10.2.3, D33's own conversion
    if (f != 0.0 - 5.0) return 5;
    if (clamp16(70000) != 32767) return 6;
    i8 tbl[4];
    tbl[0] = 0 - 1;
    if (tbl[0] != 0 - 1) return 7;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
i64 main() {
    i8 a = 1;
    i8 b = 2.5;                   // teko: a value of type f64 does not convert to i8
    i8 c = null;                  // teko: a value of type uptr does not convert to i8
    i8 d = 200;                   // teko: the constant 200 does not fit i8
    return 0;
}
```

| written | message |
|---|---|
| an `f64`/`f32` in an `i8`/`i16` slot | `teko: a value of type f64 does not convert to i8` (D33's own wording, from the predicate above) |
| `null` in an `i8`/`i16` slot | `teko: a value of type uptr does not convert to i8` (D32/D33) |
| a class, a struct, a delegate, a `T[]` | `teko: a value of type Foo does not convert to i8` (D34) |
| a `DateTime`, a `decimal`, an `enum` value | `teko: a value of type DateTime does not convert to i8` — the § 2 rule, not a special case |
| a **constant** outside the type's range | `teko: the constant 200 does not fit i8` (§ 5, its own crumb) |
| `sbyte`, `short` | nothing: they are ordinary identifiers, because teko does not register them (§ 1) |

## 4. Arithmetic keeps the core's rule, not C#'s promotion

C# promotes `sbyte + sbyte` to `int`, so `(sbyte)100 + (sbyte)100` is `200`. teko does not,
and it is not a decision this page takes: `docs/reference/types.md` § "Casts, width and
sign" already states the rule for `u8`, `u16` and `u32` — "arithmetic is 64-bit on the
widened value and wraps at the next store" — and `res_binary` (mc's own) types a binary
from its left operand. `i8` and `i16` join a rule that is three types old.

The consequence is written down rather than hidden: `(i8) 100 + (i8) 100` is `200` while it
is in flight, and `-56` the moment it is stored back into an `i8`. The fixture asserts both
halves so the behaviour has an oracle rather than a footnote. Teaching C#'s promotion for
two new types while `u8` keeps the old rule would be the real inconsistency, and it would
be a change to the base grammar, which D3 does not allow.

## 5. A constant that does not fit its slot

C# refuses `sbyte b = 200;` — "constant value 200 cannot be converted to sbyte" — and teko
refuses nothing of the sort today: `u8 b = 300;` compiles and stores `44`.

This page proposes the refusal for **every** narrow integer at once, `u8`, `u16`, `u32`,
`i8`, `i16` and `i32`, because a rule that holds for two of the six is not a rule:

```
teko: the constant 300 does not fit u8
```

It is a check on a **folded** value only — an `N_INT` whose node type is the untyped
integer literal, or a `const` the folder has already reduced — at the initializer, the
assignment, the argument, the `return`, the field store and the array element, which is the
same list of slots D33 enumerated. A run-time value still masks, as it does in C#.

It is its own crumb (§ 11, N1b) and it is **the one crumb on this page that can change an
accepted program's answer**: a program relying on `u8 b = 300;` today stops compiling. That
is the point of it, and it is why it is separated from the two registrations rather than
smuggled in beside them.

## 6. `i128` and `u128`: the representation

```
type_new("i128", 16, 16, TK_WIDE)
type_new("u128", 16, 16, TK_WIDE)
```

Sixteen bytes, sixteen-byte aligned, `TK_WIDE`, little-endian: the low half at `+0` and the
high half at `+8`. That is the registration `mc`'s own `<i128>` makes and the one
`docs/specs/decimal.md` § 1 makes, and **every word of that page's "How sixteen bytes
travel" applies here unchanged** — a local, a global, a parameter and an array element are
sixteen bytes of frame or of `__data`; an argument travels as the address of its slot; a
return goes through one module-private buffer the call site copies out of immediately.
It is not repeated here.

The two types differ from each other in exactly three places, and in no other:

| | `i128` | `u128` |
|---|---|---|
| `/`, `%` | signed | unsigned |
| `>>` | arithmetic | logical |
| `<`, `<=`, `>`, `>=` | signed | unsigned |

`+`, `-`, `*`, `&`, `\|`, `^`, `~` and `<<` are the same bits either way, so one
implementation serves both and the sign is a parameter of four functions.

### The words, again

`i128` and `u128`, not `Int128` and `UInt128`. Same reason as § 1, with one more: `mc`
**already names the type `i128`**, in `lib/i128.mc`, and D3 says the base grammar comes from
`mc` and is reused as it is. A type `mc` has a word for keeps `mc`'s word; a type `mc` has
no word for takes C#'s.

### The literal

`170141183460469231731687303715884105727i` and `340282366920938463463374607431768211455u`
— the suffix `mc`'s own `<i128>` uses, `i` for the signed and `u` for the unsigned, both
case-insensitive. C# has no `Int128` literal at all (it writes `Int128.Parse("…")`), so
there is no C# form to follow and the market's nearest neighbour is the compiler this one
is taught to.

The mechanism is `docs/specs/decimal.md` § 3's: a value above 64 bits does not fit
`MTASK_CONST`'s single `i64`, so the literal becomes a module-private global built from
32-bit limbs and the node returned names it. The registration order rule is the same and is
load-bearing for the same reason: `tk_wide_init()` runs **before** `tk_float_init()`, and
the handler declines everything that does not end in `i` or `u`.

### The arithmetic

Surface teko in `lib/wide.tk`, over 32-bit limbs held in `u64` locals — `docs/specs/decimal.md`
§ 8's technique, and about 300 lines rather than 900, because there is no scale to align
and no decimal rounding to do. Overflow **wraps**, which is C#'s unchecked default and
`Int128`'s own documented behaviour outside a `checked` block; teko has no `checked` word
(`docs/specs/decimal.md` § 9), so wrapping is the only behaviour there is and the fixture
asserts it. Division by zero panics, exit 70.

## 7. The API

| static | instance |
|---|---|
| `i128.MinValue`, `MaxValue`, `Zero`, `One` | `.ToString()` |
| `u128.MinValue`, `MaxValue`, `Zero`, `One` | `.CompareTo(i128)`, `.Equals(i128)` |
| `i128.Parse(str)`, `TryParse(str, out i128)` | — |
| `u128.Parse(str)`, `TryParse(str, out u128)` | — |

`ToString` writes the decimal digits, with a leading `-` for a negative `i128`; `Parse`
reads what `ToString` writes and panics otherwise (`teko: the string is not an i128`).
`tk_i128_fmt(ptr buf, i128 v)` is the allocation-free half, the split `<float_rt>` makes
between `putf64` and `fmt_f64`.

## 8. Conversions

| from | to | how |
|---|---|---|
| any integer (`u8`..`i64`, `i8`, `i16`, `i32`) | `i8`, `i16` | **implicit** where it widens, **explicit** where it narrows — the core's own rule, unchanged |
| `i8`, `i16` | `f32`, `f64` | **implicit**, D33's own conversion, through the predicate of § 2 |
| `f32`, `f64` | `i8`, `i16` | refused: `teko: a value of type f64 does not convert to i8` |
| any integer | `i128`, `u128` | **explicit**: `(i128) n`, lowered to `tk_i128_from_i64` / `tk_u128_from_u64` |
| `i128`, `u128` | any integer | **explicit**: `(i64) v`, lowered to `tk_i128_to_i64`, truncating |
| `i128` | `u128`, and back | **explicit**, the same bits |
| `i128`, `u128` | `f64` | **explicit**: `(f64) v`, lowered to a call, rounding |
| `f64` | `i128`, `u128` | **explicit**, truncating toward zero |
| `i128`, `u128` | `decimal` | **explicit**, both directions, when both pages have landed |
| `null`, a class, a struct, a `T[]` | any of the four | refused (D32/D34) |

Everything about `i128` and `u128` that is not a comparison is a **call**, so `tk_cast`
(teko_array.tk) is where the explicit direction lives — a cast whose source or target is
sixteen bytes becomes a call instead of an `MTASK_CAST`, which is the rule
`docs/specs/decimal.md` § 6 states.

## 9. What stays out

| left out | why |
|---|---|
| `sbyte`, `short`, `ushort`, `int`, `uint`, `long`, `ulong` | § 1: all seven or none, and it is not this page's decision |
| C#'s integer promotion (`sbyte + sbyte` is an `int`) | § 4: it would change the base grammar for `u8` too |
| `checked` / `unchecked` | teko has neither word; `i128` wraps and `decimal` is loud, each as C# is without the words |
| `i128` as a `const`, an array size or a `switch` label | the core's folder has no 128-bit arithmetic, exactly as for `decimal` |
| `i128` on an `extern` | the sixteen-byte convention is teko's own and is not a C ABI |
| `Int128.PopCount`, `LeadingZeroCount`, `RotateLeft` | C# 11's generic-math surface; a library, once there is one |
| `nint`/`nuint` | `isize`/`usize` are already the alias pair teko carries for the same job |

## 10. What it costs in `mc limits`

The baseline is the one `docs/specs/decimal.md` § 10 measured on `6b868f0c` with
`mc limits . --config teko.toml` at tolerance 1.0: `types 7/14`, `alias 14/28`,
`syntax 14/28`, `passes 15/30`, `intrin 8/16`, verdict `ok`.

| row | today | after `i8`/`i16` | after `i128`/`u128` | why |
|---|---|---|---|---|
| `types` | 7 | **9** | **11** | four `type_new` calls, all four in the compiler itself |
| `alias` | 14 | 14 | 14 | no alias unless the seven C# words are taken |
| `syntax` | 14 | 14 | **16** | `i128.MaxValue` and `u128.MaxValue` need the type word to open an expression; `i8` and `i16` never open one |
| `passes` | 15 | 15 | 15 | no pass either half |
| `intrin` | 8 | 8 | 8 | teko still registers none |

`i8` and `i16` move exactly one row by exactly two, which is why they are first.

The `types` row is the one to watch **across the whole plan**, not on this page: teko
registers one id per declared class, struct, interface and delegate already
(`teko_struct.tk`'s and `teko_access.tk`'s own `type_new(name, 8, 8, TK_INT)`), so the row
is a per-program budget the compiler's own registrations raise the floor of. The floor goes
from 7 to about 19 once every page of this plan has landed. The registry grows
(`ty_reg_add` uses `grow`), so the row is a **budget report and not a wall**, and
`[limits] tolerance` is the knob — the same resolution `docs/specs/datetime.md` § 13
reaches.

## 11. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/primitives_small_int.tk` | a negative `i8` and `i16` round-trip through a local, a parameter, a return, a field, a global, an array element and an `extern`-shaped declaration; `>>` is arithmetic; `/` and `%` are signed; the six comparisons are signed; widening to `i64` carries the sign | `42` |
| `tests/primitives_small_int_convert.tk` | an `i8` and an `i16` convert to `f64` in all nine slots D33 enumerated; a positive value only on the `aarch64` path the `not-yet.md` row names; nothing narrows back | `42` |
| `tests/primitives_small_int_wrap.tk` | `(i8) 100 + (i8) 100` is `200` in flight and `-56` stored back; `(i16) 32767 + (i16) 1` is `-32768` stored back | `42` |
| `tests/primitives_i128.tk` | a literal above `2^64` round-trips through a local, a parameter, a return, a field, a global and an `i128[]` element; a recursive `i128` function proves the return buffer; `MaxValue + 1` wraps to `MinValue` | `42` |
| `tests/primitives_i128_math.tk` | `+ - * / % & \| ^ ~ << >>` against values chosen so a 64-bit implementation gives a different answer; `u128` division and `>>` are unsigned where `i128`'s are signed | `42` |
| `tests/primitives_i128_text.tk` | `ToString`/`Parse` round-trip at `MinValue`, `MaxValue` and zero; `TryParse` on a good and a bad string | `42` |
| `tests/primitives_i128_divzero.tk` | `(i128) 1 / (i128) 0` | `70` |
| `tests/primitives_i128_parse_bad.tk` | `i128.Parse("x")` | `70` |

The constant-range crumb adds no fixture of its own: a refusal has no harness
(D33's own closing note), so `teko: the constant 300 does not fit u8` is documented with a
`// no-run` fence in [diagnostics.md](../reference/diagnostics.md) and
[types.md](../reference/types.md), and the crumb's oracle is that all existing fixtures
still pass.

## 12. The crumbs

Sizes are S/M/L in modules and fixtures moved. The single order across every page of this
plan is in [the specs index](README.md).

### N0 — `i8` and `i16`, and the predicate (S)

Two `type_new` lines in `teko_type.tk`; `tk_is_int_ty` rewritten to § 2's form, with
`TY_MAX` replaced by `ty_i32` by name. Depends on nothing. **It is the first crumb of the
whole plan**, and it should land before `docs/specs/datetime.md`'s own P0, because that
page's `TimeSpan` is a `TK_SINT` and the predicate has to be right before a second
`TK_SINT` exists.

**Gate:** `tests/primitives_small_int.tk`, `_convert.tk` and `_wrap.tk` at `42`; the other
45 fixtures at their `expect-exit` with `--dump-ast` **byte-identical**; no fixture and no
module uses `i8` or `i16` as an identifier (the registration takes both words from every
program, and that is checked before the merge, not after); `FIXPOINT OK`; `mc limits`
verdict `ok` with `types` 7 to 9 and every other row unmoved; `sh scripts/check-docs.sh`
green. **Owes:** an `i8`/`i16` row in [types.md](../reference/types.md) § Scalars, the two
refusals in [diagnostics.md](../reference/diagnostics.md), and the promotion note of § 4 in
types.md § "Casts, width and sign".

### N1b — the constant that does not fit (S, optional, behaviour-changing)

§ 5's refusal, for all six narrow integers. Depends on N0. **It is the only crumb here that
can stop an existing program compiling**, so it is separated and can be declined without
touching anything else.

**Gate:** all 48 fixtures at their exit codes and `--dump-ast` byte-identical — no fixture
stores an out-of-range constant, and that is a claim the crumb proves rather than assumes;
`FIXPOINT OK`; the compiler's own sources build (`mc_teko.tk` is compiled by this
compiler). **Owes:** [not-yet.md](../reference/not-yet.md)'s row moved from "what happens"
to "refused", diagnostics.md and types.md.

### N6 — `i128` and `u128` (L)

Two `type_new` calls in a new `teko_wide.tk`-adjacent registration, the two literals,
`lib/wide.tk`'s limb arithmetic, the operator rows, the explicit casts, `ToString`/`Parse`.
**Depends on `docs/specs/decimal.md`'s C3** for the sixteen-byte machine module and on its
C4 for the limb technique the arithmetic reuses; it duplicates neither.

**Gate:** the five `i128` fixtures at their codes **on all five legs** — a sixteen-byte
value's ABI is the one thing a single leg cannot prove — everything N0 gated on, and
`mc limits` with `intrin` and `passes` unmoved. **Owes:** the two rows in types.md, the
conversion table, diagnostics.md, runtime.md for `lib/wide.tk`, and
[modules.md](../internals/modules.md).

## 13. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **`type_kind(t) == TK_SINT` is the obvious predicate and it is wrong.** It would make `DateTime`, `TimeSpan` and every `enum` an integer, silently. | § 2's positive list, plus the written rule that a `type_new` id joins it only by name. The fixture that proves it cannot be written until a second `TK_SINT` exists, so the **crumb that adds the second one** (`docs/specs/datetime.md`'s C1, or `enum`, whichever lands first) carries a fixture asserting `i64 n = someDateTime;` is refused. Named here so neither page assumes the other did it. |
| **`i8` and `i16` take two words from every program.** A source using `i8` as a variable name stops compiling. | It is what every `type_new` does (`mc`'s guide says so about `f32`), it is why `f32` is not in `mc`'s default module, and it is checked over the whole tree in N0's gate. The two names are short and unusual enough that the check is expected to pass; if it does not, the finding is the answer, not a workaround. |
| **C#'s integer promotion is not taught**, so `(i8) 100 + (i8) 100` differs from C#. | § 4: teko already diverges for `u8`/`u16`/`u32`, the rule is `mc`'s `res_binary`, and D3 forbids changing the base grammar. Written down in types.md with a fixture, not left to be discovered. |
| **The seven C# alias words are a fork this page declines to take.** | It is the one genuinely open item, and it is cheap either way: seven `type_alias` lines, `alias` 14 to 21, no other row. Recorded rather than guessed, because "C# decides the form" and "the base grammar is `mc`'s" both apply and point opposite ways. |
| **`i128` on top of a design that is not merged.** Everything in § 6 leans on `docs/specs/decimal.md` § 1 and § 2. | N6 is last in the order for that reason, and it introduces **no mechanism of its own**: if the `decimal` machine module changes shape, `i128` follows it with no page of its own to rewrite. If `decimal` is dropped, `i128` needs the whole of that page's § 1-§ 2 and stops being an L. |
| **`mc`'s `<i128>` and teko's `i128` would be two owners of the sixteen-byte depth.** `mc` is expected to grow `<i128>` on x86-64 and a `u128` on arm64. | teko carries **its own**, over the one wide machine `decimal` already derives. Two derived machines both claiming sixteen-byte depths is a five-leg ordering problem with no good answer, and the surface would be the same either way. `mc`'s module stays what it is for teko: the source of the **instruction sequences** an accelerator crumb would copy, exactly as `docs/specs/decimal.md` § 11 treats it. Nothing on this page is blocked on that release. |
| **`<i128>` moves to the `stdlib` package at `mc` 0.16.0** (`M52`), together with `<float>` and the two float machines this repository already includes. | Unrelated to this page, and it is a migration `teko_float.tk` owes anyway: `[deps] stdlib` and `<stdlib/float.mc>`. Named here so the two are not confused: teko's `i128` is teko's, and `#include <i128>` never appears in this repository. |

## 14. What the `mc` channel is asked

**Nothing.** `type_new`, `TK_SINT`, `type_signed`, `walk_narrow`, `fold_taught` and the
machine contract's extension-by-kind are all released on the pinned `0.15.22`, and the
sixteen-byte half asks exactly what `docs/specs/decimal.md` § 15 already asks and no more.
