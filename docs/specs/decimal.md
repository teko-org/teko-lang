# `decimal`

**Designed, not built.** Nothing on this page compiles today; every sample carries
`// no-run` for that reason. What runs is [the type reference](../reference/types.md), and
this page is kept apart from it on purpose ([the specs index](README.md)).

`decimal` is C#'s, exactly ([the surface policy](surface.md), rule 1): 128 bits, a 96-bit
mantissa, a scale of 0 to 28, a sign, **exact base-ten arithmetic**, and an overflow that
is loud. It is the type money is written in, and the reason it exists at all is that
`0.1 + 0.2 == 0.3` is false in an `f64` and true in a `decimal`.

It shares [the primitive-member mechanism](datetime.md#2-a-primitive-with-members) with
`DateTime` and `TimeSpan`, which land first because they need no sixteen-byte value. Read
that section before this one; it is not repeated here.

---

## 1. Representation

```
type_new("decimal", 16, 16, TK_WIDE)
```

Sixteen bytes, sixteen-byte aligned, `TK_WIDE` — the kind that says "wider than a register,
lives in a frame slot", the same registration `mc`'s own `<i128>` makes
([a new primitive](https://github.com/minicompiler/mc/blob/main/docs/guide/96-a-new-primitive.md)).
The two words, little-endian in memory:

| offset | bits | holds |
|---|---|---|
| `+0` | 0..63 | the low 64 bits of the 96-bit mantissa |
| `+8` | 0..31 | the high 32 bits of the mantissa |
| `+8` | 32..39 | the scale, `0..28` |
| `+8` | 40..62 | zero |
| `+8` | 63 | the sign, `1` negative |

The value is `(-1)^sign * mantissa / 10^scale`. The layout is C#'s information in teko's
own order: C# packs `flags`, `hi`, `lo`, `mid` as four `int`s, and two `i64` halves are
what surface code reads with one `ld64` each. `0m`, `0.00m` and `-0m` are three distinct
bit patterns and all three are equal, which is C#'s rule and the reason equality is a call
and never a `cmp`.

**It is a primitive and not a struct, because a teko `struct` value is a pointer to an
allocation** ([types.md](../reference/types.md) § `struct`): a `decimal` field would be a
reference to sixteen bytes on the arena, copied by aliasing rather than by value, and every
`decimal` in a loop would be a block to reclaim. C# gives it value semantics and no heap,
`type_new` is the mechanism that gives exactly that, and the decision § 14 proposes is the ruling that
lets teko use it.

### How sixteen bytes travel

**Everything moves by address, and nothing moves in a register pair.** That single decision
is what makes the machine module small enough to be reviewed and identical on all five
legs:

| position | what happens |
|---|---|
| a local, a global, a parameter | sixteen bytes of frame or of `__data`, sized by the registry from `type_width`; the machine copies them two words at a time |
| an argument | the caller materialises the address of the value's slot into the argument's depth and lets the **underlying machine's own ABI** place that pointer, exactly as it places any `uptr` |
| a return | the callee copies its sixteen bytes into one module-private global and returns **its address**; the call site copies them out into the call depth's slot before anything else runs |
| a field, an array element | two `ld64`/`st64` pairs, written in ordinary teko: `tk_dec_ld(ptr)` and `tk_dec_st(ptr, decimal)` in `lib/decimal.tk` |

The consequences are worth naming, because they are why this shape was chosen over the
register pair `<i128>` uses:

- **teko writes no ABI.** SysV's `rax:rdx`, Win64's hidden reference and AAPCS64's
  even-numbered register pair are three different rules for a sixteen-byte value; a pointer
  is one rule, and it is the rule each machine already implements. The `MTASK_CALL` handler
  rewrites the wide argument depths and then **delegates the whole call** to the pristine
  table it copied.
- **A `decimal` and an `f64` mix in one call.** The float machine's own `MTASK_CALL` sees a
  pointer where the wide value was, puts it in an integer register, and keeps its float
  register accounting untouched. `decimal.ToDouble(d)` and `(decimal) x` both need that.
- **The return buffer is safe under recursion.** One global, sixteen bytes: the call site
  copies out immediately after the branch and before any other call is emitted, so a nested
  or recursive call can never find the buffer stale. It is a **rule of the handler**, not a
  hope, and the fixture that proves it is a recursive `decimal` function.
- **No `MTASK_DEPTH_SPAN`.** The value lives in ONE depth backed by a sixteen-byte slot,
  which is the constraint `mc`'s own `<i128>` records and the reason it is memory-resident.

## 2. The machine module

`teko_wide.tk` — a derived machine over the three tables `mc` registers, `arm64`,
`x86_64` and `x86_64-win`. Windows on aarch64 uses the `arm64` table like every other
aarch64 leg, so three tables cover all five legs.

**No new opcode, on either instruction set.** Every byte it emits is one the base machine
already encodes, so it registers no `MTASK_ENCODE`, no `MTASK_INS_SIZE`, no `MTASK_DUMP`
and no `MTASK_RELOC_KIND`, and `--dump-asm` needs no new mnemonic.

| slot | what the handler does when the type is `decimal` | otherwise |
|---|---|---|
| `MTASK_PROLOGUE` | clear the per-function map of depth → sixteen-byte slot | delegate |
| `MTASK_LOCAL_LOAD` / `LOCAL_STORE` | copy sixteen bytes between the local's slot and the depth's | delegate |
| `MTASK_GLOBAL_LOAD` / `GLOBAL_STORE` | delegate `MTASK_SYM_ADDR` for the address, then copy | delegate |
| `MTASK_PARAM` | delegate the incoming pointer as a `uptr` into a scratch slot, then copy sixteen bytes from it into the parameter's slot | delegate |
| `MTASK_CALL` | for each wide argument depth, materialise its slot address into that depth's register; delegate; if the result is wide, copy sixteen bytes from the returned pointer into the depth's slot | delegate |
| `MTASK_RET` | copy the depth's sixteen bytes into `$tk_wide_ret`, put its address in the depth, delegate | delegate |
| `MTASK_BIN`, `CMP`, `UN`, `CAST`, `CONST` | unreachable: teko lowers every one of them to a call before codegen. A guard that `die`s names the teko refusal it should have been | delegate |

The per-instruction-set part is two functions — "copy sixteen bytes from `base+off` to
`base2+off2`" — `I_LDR`/`I_STR` on AArch64 and `X_LD64`/`X_ST64` on x86-64, both already
defined by the bundled machines this compiler links (`<mc/core_machines>`), the way
`lib/machine_x86_64_float.mc` reaches `X_LD64` and `XR_RBP` today. **The x86-64 encoding
list this design needs is empty.** The list the optional accelerator would need is in
§ 11.

**Derivation order.** `<float>`'s two machines shadow `arm64`, `x86_64` and `x86_64-win`
already; `teko_wide.tk` copies the table that is current **after** `tk_float_init()` has
run and delegates through a pristine copy of it, so a float operation reaches `<float>`'s
handler and an integer one reaches the base — the chain the `mc` guide's own trap warns
about.

## 3. The literal

`1.5m`, `0.1m`, `1m`, `-3.25m`, `1.5e3m` — C#'s suffix, case-insensitive (`M` too).

`syntax_lit` is the hook, and the **registration order is load-bearing**: `<float>`'s
`fl_lit` reads `1.5` and stops before the `m`, leaving an identifier the parser would
choke on and, worse, leaving `1.5m` typed `f64` at any site where `m` happened to parse.
Handlers run in registration order and the first non-zero node wins, so `tk_dec_init()`
runs **before** `tk_float_init()` in `teko.tk` and the decimal handler declines (`return
0`) everything that does not end in `m`.

A 128-bit value does not fit `MTASK_CONST`'s single `i64`, so the literal becomes a
module-private global with an `N_BLOB` initializer and the node returned is an `N_IDENT`
naming it — the shape `<i128>` uses, with two differences of teko's own: the name is
gensym'd with a `$` the lexer never forms into an identifier, and the declaration is
emitted through `tk_top_emit` ([nodes-and-xt.md](../internals/nodes-and-xt.md)) rather than
`top_add`, because a literal fires in the middle of the declaration being parsed.

The digits are accumulated into 32-bit limbs, most significant first, exactly as
`iw_muladd` does; the scale is the count of fraction digits, and the exponent shifts it.
Out of range is refused where it is written:

| written | message |
|---|---|
| more than 96 bits of mantissa | `teko: decimal literal out of range` |
| a scale above 28 | `teko: a decimal carries at most 28 decimal places` |

## 4. The surface

**Legal.**

```teko
// no-run
#include "decimal.tk"

decimal tax(decimal amount, decimal rate) {
    return amount * rate;
}

i64 main() {
    decimal a = 0.1m;
    decimal b = 0.2m;
    if (a + b != 0.3m) return 1;                  // an f64 would fail this
    decimal price = 19.99m;
    decimal total = price + tax(price, 0.07m);
    if (decimal.Round(total, 2) != 21.39m) return 2;
    decimal n = 5;                                // an integer converts
    if (n / 2m != 2.5m) return 3;
    if (a < b == false) return 4;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
#include "decimal.tk"

i64 main() {
    decimal d = 1.5m;
    f64 x = d;                    // teko: a value of type decimal does not convert to f64
    decimal e = 1.5;              // teko: a value of type f64 does not convert to decimal
    i64 n = d;                    // teko: a value of type decimal does not convert to i64
    decimal m = d % 0m;           // teko: decimal division by zero   (at run time, exit 70)
    decimal s = d << 1;           // teko: no operator `<<` takes these operands
    return 0;
}
```

| written | message |
|---|---|
| a `decimal` in an `i64` slot | `teko: a value of type decimal does not convert to i64` |
| a `decimal` in an `f64` slot | `teko: a value of type decimal does not convert to f64` |
| an `f64` in a `decimal` slot | `teko: a value of type f64 does not convert to decimal` |
| `null`, a class, a struct, a `T[]` in a `decimal` slot | `teko: a value of type Foo does not convert to decimal` (D32/D34's own wording) |
| `<<`, `>>`, `&`, `|`, `^`, `~` on a `decimal` | ``teko: no operator `<<` takes these operands`` (teko_ops.tk's own) |
| an unknown member | `teko: unknown member of decimal`, `teko: unknown static member of decimal` |
| a `decimal` on an `extern` | ``teko: an `extern` takes no decimal`` — the sixteen-byte convention is teko's own and crosses no C boundary |
| a `decimal` as a `const` | `teko: a const is an integer` (the existing folding rule; a `decimal` has no folded form) |

## 5. Operators, and their precedence

The core's own table decides precedence and associativity; the operator pass rewrites a
**resolved node** and never reads a token, so `a + b * c` groups as it always did.

| written | lowered to | result | on failure |
|---|---|---|---|
| `a + b`, `a - b`, `a * b` | `tk_dec_add`, `tk_dec_sub`, `tk_dec_mul` | `decimal` | overflow: `teko: decimal overflow`, exit 70 |
| `a / b`, `a % b` | `tk_dec_div`, `tk_dec_rem` | `decimal` | `b == 0`: `teko: decimal division by zero`, exit 70 |
| `-a`, `+a` | `tk_dec_neg`, identity | `decimal` | — |
| the six comparisons | `tk_dec_eq` … `tk_dec_ge` | `i64` 0/1 | — |
| `a + 1`, `1 + a` | the integer operand converts first (§ 6), then the row above | `decimal` | — |

**Exactness is the contract.** `+ - *` are exact whenever the result fits 96 bits at the
scale the operands imply: addition and subtraction align to the larger scale, multiplication
sums the scales. Only when a result needs more than 28 decimal places does anything round,
and then it rounds **half away from zero**, which is what `.NET`'s own `DecCalc` does for
the operators. `Round` is the other rule, and it is the one the owner named:
`decimal.Round(d)` and `decimal.Round(d, n)` round **half to even**, C#'s
`MidpointRounding.ToEven` default.

`1m / 3m * 3m` is therefore `0.9999999999999999999999999999` and not `1m` — 28 digits, the
same answer C# gives, and a fixture asserts exactly that. A design that "fixed" it would be
a different type.

## 6. Conversions

| from | to | how | why |
|---|---|---|---|
| any integer (`u8`..`i64`, `i32`) | `decimal` | **implicit**, in every one of the nine slots D33 enumerated | C# §10.2.3 |
| `decimal` | any integer | **explicit**: `(i64) d` | C# §10.3, truncates toward zero |
| `f32`, `f64` | `decimal` | **explicit**: `(decimal) x` | C# §10.3; the double's own value, rounded to 15 significant digits, as C# does |
| `decimal` | `f64`, `f32` | **explicit**: `(f64) d` | C# §10.3, may lose precision |
| `decimal` | `str` | `.ToString()` | never implicit |
| `null`, a class, a struct, a `T[]` | `decimal` | refused | D32/D34 |

The implicit direction is `tk_num_widen`'s sibling: a `tk_dec_widen` in teko_typeof.tk that
wraps an integer node in a call to `tk_dec_from_i64`, handed **back** to the caller to
splice, which is the shape every one of the nine slots already knows how to use. The
explicit direction is `tk_cast`'s: a cast whose target or source is `decimal` becomes a call
(`tk_dec_to_i64`, `tk_dec_from_f64`, `tk_dec_to_f64`) instead of a machine cast, because
`MTASK_CAST` on a wide type has no meaning here.

An `i32` widens through the same door and carries its sign, so the defect D33 recorded on
`aarch64` (`fa_cast` choosing `ucvtf`) cannot reach a `decimal`: the conversion is a call
over an `i64` argument, not a float instruction.

## 7. The API

`decimal` is a type word, so `decimal.Round(...)` is a static member of a primitive and
resolves through the member table.

| static | instance |
|---|---|
| `decimal.Zero`, `One`, `MinusOne`, `MaxValue`, `MinValue` | `.ToString()`, `.ToString(i64 places)` |
| `decimal.Round(d)`, `Round(d, i64 places)` | `.CompareTo(decimal)`, `.Equals(decimal)` |
| `decimal.Truncate(d)`, `Floor(d)`, `Ceiling(d)`, `Abs(d)` | `.Scale` `i64`, `.Sign` `i64` |
| `decimal.Parse(str)`, `TryParse(str, out decimal)` | — |
| `decimal.ToDouble(d)`, `FromDouble(f64)` | — |

`Math.Round(d)` and `Math.Round(d, n)` are C#'s other spelling of the same two functions,
and they need no mechanism at all: `Math` is an ordinary teko class in `lib/math.tk` whose
`public static decimal Round(decimal d)` forwards. A static method on a declared class is
a construct that already runs today.

**Text.** `ToString()` writes the shortest exact form — the mantissa with the point placed
by the scale, a leading `-` when negative, trailing zeros kept because `1.10m` and `1.1m`
are different values with the same number. `Parse` accepts what `ToString` writes plus an
optional exponent, panics on anything else (`teko: the string is not a decimal`), and
`TryParse(s, out v)` answers `0`/`1`. `tk_dec_fmt(ptr buf, decimal d)` is the
allocation-free half, the split `<float_rt>` makes between `putf64` and `fmt_f64`.

## 8. The arithmetic, and where it lives

**All of it is surface teko**, in `lib/decimal.tk`, over 32-bit limbs held in `u64`
locals — the same technique `mc`'s own `<i128>` uses to convert a literal
(`iw_muladd`), and the reason this design needs **no 128-bit instruction anywhere**:

- a `decimal` is read into four `u32` limbs with `ld64` through `&d`, and written back with
  `st64`. `&local` on a wide local is the address of its own frame slot, so no new accessor
  and no new intrinsic is needed to reach the halves — the pair `tk_f64_bits` /
  `tk_f64_from_bits` already does the same job for an `f64` through a scratch global;
- add and subtract align scales by multiplying the smaller-scaled operand by a power of ten
  (a limb multiply-add), then add or subtract 96-bit magnitudes with an explicit carry;
- multiply is the school product of three limbs by three limbs into six, scales summed,
  reduced by dividing by ten with rounding while the result is too wide;
- divide is long division over the limbs, producing up to 29 digits and rounding the last
  one away from zero;
- every operation that leaves the 96-bit range calls `panic("decimal overflow")`, which is
  `rt_panic` and exit 70 — the same failure an array guard already gives
  ([memory.md](../reference/memory.md)).

About 900 lines of ordinary teko, all of it listed in
[the runtime reference](../reference/runtime.md) when it lands, none of it magic.

## 9. What stays out

| left out | why |
|---|---|
| `decimal` as a `const` or as an array size | a `const` is folded at compile time and the folder has no 128-bit arithmetic (`fold_taught` stands aside for `TK_WIDE` by design) |
| a `decimal` `switch` label | the same reason |
| `checked`/`unchecked` | teko has neither word; the overflow is always loud |
| culture, currency and grouping in the text | a formatting library, not a primitive |
| `decimal` on an `extern` | the sixteen-byte convention is teko's own and is not a C ABI |
| a generic `T` bound to `decimal` | not refused on principle; not measured by this design |

## 10. What it costs in `mc limits`

Measured on `6b868f0c` with `mc limits . --config teko.toml` (tolerance 1.0):
`types 7/14`, `intrin 8/16`, `passes 15/30`, `syntax 14/28`, verdict `ok`.

| row | today | after | why |
|---|---|---|---|
| `types` | 7 | **10** | with the two of [datetime.md](datetime.md); `decimal` is one of them |
| `intrin` | 8 | **8** | teko still registers none: the halves are reached with `&`, `ld64` and `st64` |
| `passes` | 15 | **15** | the operator and member lowerings ride passes that exist |
| `syntax` | 14 | **17** | one `syntax_expr` per type word, `decimal` included |
| `nodes`, `funcs`, `globals`, `heap` | — | up | about 900 lines of library and 400 of module; every `decimal` literal in a program is one more global |

The machine module adds **no** row: a derived table shadows the name it registers and does
not consume a `machines` slot.

## 11. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/primitives_decimal_value.tk` | a literal round-trips through a local, a parameter, a return, a field, a global and a `decimal[]` element; the raw halves read back through `&` match the documented layout; a recursive `decimal` function proves the return buffer | `42` |
| `tests/primitives_decimal_math.tk` | `0.1m + 0.2m == 0.3m`; `1m / 3m * 3m != 1m` and equals `0.9999999999999999999999999999m`; `2.5m * 4m == 10m`; `-0m == 0m`; `1.10m == 1.1m` with different scales; the six comparisons | `42` |
| `tests/primitives_decimal_convert.tk` | an integer converts implicitly in all nine slots; `(i64) 2.9m == 2`; `(f64) 0.5m == 0.5`; `(decimal) 0.5` round-trips | `42` |
| `tests/primitives_decimal_round.tk` | `decimal.Round(2.5m) == 2m` and `decimal.Round(3.5m) == 4m` (to even); `Round(21.385m, 2)`; `Truncate`, `Floor`, `Ceiling`, `Abs` | `42` |
| `tests/primitives_decimal_text.tk` | `ToString`/`Parse` round-trip, `TryParse` on a good and a bad string, trailing zeros preserved | `42` |
| `tests/primitives_decimal_overflow.tk` | `79228162514264337593543950335m + 1m` | `70` |
| `tests/primitives_decimal_divzero.tk` | `1m / 0m` | `70` |
| `tests/primitives_decimal_parse_bad.tk` | `decimal.Parse("x")` | `70` |

## 12. The crumbs

They follow [datetime.md](datetime.md)'s C1 and C2, because C1 carries the primitive-member
mechanism all of these read.

### C3 — the sixteen-byte value (M)

`teko_wide.tk` (the three derived tables), `type_new("decimal", 16, 16, TK_WIDE)`, the
literal, and the two copy helpers. **No arithmetic**: the fixture moves values and reads
their bytes.

**Gate:** `tests/primitives_decimal_value.tk` at `42` **on all five legs** — this is the
one crumb whose CI matrix is the proof, because three machine tables are being derived —
the other fixtures unchanged with `--dump-ast` byte-identical, `FIXPOINT OK`, `mc limits`
verdict `ok` with `intrin` and `passes` unmoved. **Owes:** a `decimal` section in
[types.md](../reference/types.md), the literal in the lexical part of the guide, the new
module in [modules.md](../internals/modules.md), and the amendment § 14 proposes to
[surface.md](surface.md) § "What is 'magic'".

### C4 — the arithmetic (L)

`lib/decimal.tk`'s limb arithmetic, the operator rows, the implicit integer conversion, the
explicit casts, the overflow and division-by-zero panics.

**Gate:** `primitives_decimal_math.tk`, `_convert.tk`, `_overflow.tk` and `_divzero.tk` at
their codes on all five legs; everything C3 gated on. **Owes:** diagnostics.md,
runtime.md, and the conversion table in types.md.

### C5 — round and text (M)

`Round`/`Truncate`/`Floor`/`Ceiling`/`Abs`, `ToString`/`Parse`/`TryParse`, `lib/math.tk`.

**Gate:** `primitives_decimal_round.tk`, `_text.tk`, `_parse_bad.tk`. **Owes:** runtime.md
and a guide section.

### C7 — the native wide instructions (S, optional, not required by anything)

The limb arithmetic replaced by the instruction each machine has: `adds`/`adc`,
`subs`/`sbc`, `mul`/`umulh` on AArch64 — which is `<i128>`'s own set — and, on x86-64,
`ADC r/m64, r64` (`REX.W 11 /r`), `SBB` (`REX.W 19 /r`), `MUL r/m64` (`REX.W F7 /4`,
`rdx:rax`), `IMUL r64, r/m64` (`REX.W 0F AF /r`) and `SHLD`/`SHRD` (`0F A4`/`0F AC`) for the
128-bit shifts, encoded through the same `x86_rex`/`x86_op`/`x86_modrm_rr` helpers
`lib/machine_x86_64_float.mc` uses.

**Gate:** every fixture of C3, C4 and C5 unchanged at the same exit codes, and
`--dump-asm` showing the new mnemonics. **It is a speed crumb and nothing else** — see the
first row of § 13.

## 13. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **The owner's ruling anticipated an `i128` on x86-64 as a dependency.** `mc`'s `<i128>` is AArch64 only, so teko would carry its own. | **The dependency dissolves.** With the arithmetic written in surface teko over 32-bit limbs (§ 8) and the value moved by address (§ 1), nothing in this design needs a 128-bit instruction on any leg — correctness lands on all five legs at once, and the native instructions become C7, a measurable speed crumb. The ruling stands where it matters: a primitive **may** use the ISA, and § 14 records it. Whether the accelerator is teko's own module or a contribution of an x86-64 half to `mc`'s `<i128>` is a question for the `mc` channel and blocks nothing. |
| **"Zero new intrinsics" against a machine primitive.** [surface.md](surface.md) states that teko registers no intrinsic and that the closed list is exactly what `mc` gives. | **§ 14**, below, and an amendment to that page in C3. The list stays closed as written: this design registers **no intrinsic at all** — the halves are read with `&`, `ld64` and `st64`, which are already on the list. What § 14 adds is narrower and needs saying anyway: a type registered with `type_new` carries a **machine module for its own value movement**, and a machine table is not an intrinsic and not backend magic — it is where the sixteen bytes of a load are decided, and there is no surface code that could decide them instead. |
| **Three derived tables are three chances to be wrong on one leg only.** A wrong `MTASK_PARAM` on Win64 is a wrong answer, not a diagnostic. | C3 is gated on the **five-leg matrix**, not on the local recipe, and its fixture reads the raw bytes back through `&` rather than trusting an arithmetic result. `lib/machine_probe.mc`'s trick — a derived machine that asserts the depth-type contract and changes no instruction — is worth one probe in P0. |
| **The return buffer is one global.** Recursion and nesting depend on the call site copying out immediately. | It is a rule of one handler, in one place, with the recursive fixture as its oracle. The alternative — a return in a register pair — is three ABIs and was rejected for that reason. |
| **The literal ordering.** If `tk_dec_init()` is ever registered after `tk_float_init()`, `1.5m` silently becomes an `f64` followed by an identifier. | The registration order is asserted by a fixture that puts `0.5m` and `0.5` in the same program and compares neither to the other, and by a comment at the call site in `teko.tk`. |
| **Rounding rules are two, not one.** The operators round half away from zero; `Round` rounds half to even. | That is C#, and both are fixtures with values that only pass under the right rule (`Round(2.5m) == 2m` for the one, `1m/3m*3m` for the other). If an implementation diverges, the fixture is the oracle and C# is the reference. |
| **`decimal` has no folded form**, so `const decimal RATE = 0.07m;` cannot work. | Refuse it by name and record the row in [not-yet.md](../reference/not-yet.md). A folded `TK_WIDE` constant would need the core's folder, which is `mc`'s and frozen. |

## 14. The decision this design proposes (numbered when it enters the log)

> **A primitive may be a machine type; the closed list stays closed.**
> A type teko registers with `type_new` carries whatever its representation needs to
> **move**: a derived machine table per instruction set, deriving from the table in effect
> and delegating everything else through a pristine copy. That is not an intrinsic and not
> a fork of `mc`'s core — `git diff src/` for it is empty, which is the criterion
> [`mc`'s own guide](https://github.com/minicompiler/mc/blob/main/docs/guide/96-a-new-primitive.md)
> sets — and the "zero new intrinsics" law does not veto it. What the law still forbids is
> unchanged and is the whole of it: **no operation of the language surface may be an
> intrinsic.** Addition, rounding, formatting, parsing and every conversion have surface
> code in `lib/`, and `mc limits`' `intrin` row does not move. A primitive that cannot be
> moved without a **new instruction encoding** is still allowed — the encoding is the
> module's, beside the table — and a primitive that cannot be expressed without changing
> `mc`'s core is a fork and halts, as D2 says.

## 15. What the `mc` channel is asked

1. **Does `mc` want an x86-64 half for `<i128>`?** Only C7 cares, and only for speed. If
   `mc` would take it, teko contributes it there rather than carrying a module of its own.
2. **Is a sixteen-byte value's ABI anything the core decides?** The reading of `<i128>` and
   of the machine contract says no — the module owns `MTASK_PARAM`, `MTASK_CALL` and
   `MTASK_RET` — and this design is built on that answer. A one-line confirmation closes
   it, and a "no" would move the whole of § 1 into a fork.
3. **A wall clock in `<sys>`**, which [datetime.md](datetime.md) § 8 asks for and this page
   does not need.
