# `Guid`

**N3 and N9 both landed** (D75, D91): everything on this page compiles today, `Guid.NewGuid`
included. The samples keep their `// no-run` marker because they are fragments rather than
whole programs; the runnable form is [the type reference](../reference/types.md#guid), and
`tests/primitives_guid.tk`, `tests/primitives_guid_bytes.tk`,
`tests/primitives_guid_tryparse.tk`, `tests/primitives_guid_parse_bad.tk` and
`tests/primitives_guid_newguid.tk` are the oracles. What each section owes the build is
marked below.

`Guid` is C#'s `System.Guid`: a 128-bit identifier, written as
`f81d4fae-7dec-11d0-a765-00a0c91e6bf6`, compared and ordered by value, with an all-zero
`Guid.Empty` and a random `Guid.NewGuid()`.

It is **the smallest page in this plan** because it invents nothing: the sixteen-byte value,
the machine module that moves it and the member-lowering table are all built by
`docs/specs/decimal.md` and `docs/specs/datetime.md`, and this page spends them. What it
adds is 16 bytes of layout, a hex parser, a hex formatter and a byte comparison — and one
function that reads the host's entropy (N9, § 5).

---

## 1. Representation

```
type_new("Guid", 16, 16, TK_WIDE)
```

Sixteen bytes, sixteen-byte aligned, `TK_WIDE` — `docs/specs/decimal.md` § 1's registration
exactly, moved by `teko_wide.tk`'s three derived machine tables, by address and never in a
register pair. Everything in that page's "How sixteen bytes travel" applies here word for
word and is not repeated.

**The sixteen bytes are in text order**, big-endian, byte 0 first:

| offset | holds | prints as |
|---|---|---|
| `+0 .. +3` | `time_low` | the first group, 8 hex digits |
| `+4 .. +5` | `time_mid` | the second group, 4 |
| `+6 .. +7` | `time_hi_and_version` | the third group, 4 |
| `+8 .. +9` | `clock_seq` | the fourth group, 4 |
| `+10 .. +15` | `node` | the fifth group, 12 |

That is RFC 4122's own order and the order the "D" format prints, which makes `ToString` a
straight walk from byte 0 to byte 15 and `Parse` its inverse. It is **not** C#'s in-memory
order: `System.Guid` stores `_a` as an `int` and `_b`/`_c` as `short`s, so on a
little-endian host its first eight bytes are byte-swapped relative to the text. The
difference is invisible everywhere the surface can see it — `ToString`, `Parse`, `==` and
`!=` give identical answers either way — and it is visible in exactly two places C# users
know are traps:

- **`CompareTo` and the ordering operators.** C# compares `_a` as a signed `int`, then
  `_b`, then `_c` as signed `short`s, then the remaining bytes — an order almost nobody
  intends and SQL Server famously disagrees with. teko compares the sixteen bytes
  **unsigned, left to right**, which is the order the text sorts in. Recorded as a
  divergence, § 6.
- **`ToByteArray()` and `new Guid(byte[])`**, which are not in this design at all.

## 2. The surface

**Legal.**

```teko
// no-run
#include "rt.tk"
#include "guid.tk"

i64 main() {
    Guid a = Guid.Parse("f81d4fae-7dec-11d0-a765-00a0c91e6bf6");
    Guid b = Guid.Parse("F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6");   // case-insensitive
    if (a != b) return 1;
    if (a == Guid.Empty) return 2;

    str s = a.ToString();                        // the "D" form, lowercase
    if (Guid.Parse(s) != a) return 3;
    if (!tk_str_eq(a.ToString("N"), "f81d4fae7dec11d0a76500a0c91e6bf6")) return 4;   // `str` is bytes: compare with tk_str_eq (lib/rt.tk)

    Guid z = Guid.Empty;
    if (!tk_str_eq(z.ToString(), "00000000-0000-0000-0000-000000000000")) return 5;
    if (z >= a) return 6;                        // ordered by the bytes, left to right

    Guid out_v = Guid.Empty;
    if (Guid.TryParse("nope", out out_v) != 0) return 7;
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
#include "guid.tk"

i64 main() {
    Guid a = Guid.Empty;
    i64 n = a;                    // teko: a value of type Guid does not convert to i64
    Guid b = 0;                   // teko: a value of type i64 does not convert to Guid
    Guid c = a + a;               // teko: no operator `+` takes these operands
    i64 d = (i64) a;              // teko: a Guid does not cast; `.ToString()` writes it
    const Guid E = Guid.NewGuid();   // teko: const requires a constant expression -- a CALL, still no constant
    return 0;
}
```

| written | message |
|---|---|
| a `Guid` in any numeric slot | `teko: a value of type Guid does not convert to i64` |
| anything in a `Guid` slot | `teko: a value of type i64 does not convert to Guid` |
| `+ - * / % & \| ^ ~ << >>` | ``teko: no operator `+` takes these operands`` |
| `(i64) g`, `(Guid) n` written by hand | ``teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it`` |
| an unknown member | `teko: unknown member of Guid`, `teko: unknown static member of Guid`, each completed by the name written |
| a `Guid` on an `extern` | ``teko: an `extern` takes no Guid`` — the sixteen-byte convention is teko's own and crosses no C boundary |
| a `Guid` as a `const` or a `switch` label, `Guid.NewGuid()` included (§ 5, N9) | `teko: const requires a constant expression` and `teko: a case label must be a constant expression` — **as built**: the folder has no 128-bit arithmetic and those are the wordings it already had, exactly as for `decimal`; a CALL is no constant expression either, § 5's own ruling 3 |

## 3. Operators

| written | lowered to | result |
|---|---|---|
| `a == b`, `a != b` | `tk_guid_eq`, `tk_guid_ne` | `i64` 0/1 |
| `a < b`, `<=`, `>`, `>=` | `tk_guid_lt` … `tk_guid_ge` | `i64` 0/1 |
| everything else | refused | — |

Every one is a call, because a sixteen-byte value has no `cmp` — the same rule
`docs/specs/decimal.md` § 5 states. The comparison walks the bytes from `+0`, unsigned, and
stops at the first difference.

## 4. The API

| static | instance |
|---|---|
| `Guid.Empty` | `.ToString()` — the "D" form |
| `Guid.Parse(str)` | `.ToString(str fmt)` — `"D"` or `"N"` |
| `Guid.TryParse(str, out Guid)` | `.CompareTo(Guid)`, `.Equals(Guid)` |
| `Guid.NewGuid()` — N9, § 5, **landed** | `.IsEmpty` — an `i64` 0/1 |

**Text.** `ToString()` writes 36 characters, lowercase, `8-4-4-4-12`, which is C#'s `"D"`
and C#'s own casing. `ToString("N")` writes the same 32 hex digits with no dashes. `"B"`
(braces), `"P"` (parentheses) and `"X"` (the C struct form) are not taught: they are three
more formats for the same sixteen bytes and nothing asks for them.

`Parse` accepts `"D"` and `"N"`, either case, and panics on anything else
(`teko: the string is not a Guid`). `TryParse(s, out g)` answers `0`/`1` and writes
`Guid.Empty` on failure — the C# pair, and the reason `out` is worth having.
`tk_guid_fmt(ptr buf, Guid g, i64 fmt)` is the allocation-free half, the split
`<float_rt>` makes between `putf64` and `fmt_f64`.

`.IsEmpty` is not a C# member — C# writes `g == Guid.Empty` — and it is here because it is
one comparison the caller would otherwise write against a static that costs a load. It is
the one addition on this page, and it is additive: `g == Guid.Empty` works too.

## 5. `Guid.NewGuid` is teko's own entropy `extern` — **landed, N9, D91**

A version-4 `Guid` is sixteen bytes of **cryptographically random** data with the version
nibble set to `4` and the variant bits to `10`. The nibbles are arithmetic; the randomness
comes from the host, under one name per operating system:

| host | the `extern` |
|---|---|
| Linux | `i64 getrandom(uptr buf, i64 n, u32 flags)` — `flags` 0, looped until `n` |
| macOS | `i32 getentropy(uptr buf, i64 n)` — at most 256 bytes a call, sliced |
| Windows | `u32 BCryptGenRandom(uptr h, uptr buf, u32 n, u32 flags)` — `h` 0, `flags` 2; `bcrypt.dll`, and **not** one of the kernel32 entry points teko's sysroot already binds |

**These are teko's own** (the owner's ruling of 2026-09-08: the tooling `mc` gives —
`extern`, the target host the taught compiler knows at compile time, and a sysroot this
repository writes itself — is enough, and nothing is asked of `mc`). The mechanism landed is
NOT a per-target declaration in `lib/guid.tk` directly (`[include].paths` cannot vary across
`ngen.yml`'s five CI legs, and `mc` does not dead-strip an unreachable function, so an
unused wrapper's `extern` would still have to resolve — C6's own measurement, D90): instead
`lib/guid.tk` includes `<teko/entropy.tk>`, a SECOND name the SAME wrapped bundle resolver
C6 installed for `<teko/clock.tk>` answers, by host, in `teko_time.tk`. `tk_guid_newguid()`
calls the one function that text expands to, `tk_entropy_fill(uptr buf, i64 n)`, fills the
sixteen bytes, and sets the version and variant nibbles by hand. teko's `windows-sysroot`
action writes a one-line `bcrypt.def` (`EXPORTS BCryptGenRandom`) beside its `kernel32.def`
and runs `llvm-dlltool` over it into its own `bcrypt.lib`, exactly as `kernel32.lib` is made
(D36 for the site's own precedent of a hand-written piece of infrastructure); `ngen.yml`'s
two Windows legs link both import libraries.

A `Guid` built from a counter, a clock or an address would compile and would be a wrong
answer — two processes would collide — so it is not the fallback. There is no fallback: on
a failing or non-progressing call, `tk_entropy_fill` panics `teko: the entropy source is not
available`, exit 70, the same road the wall clock's own failure takes.

## 6. What stays out

| left out | why |
|---|---|
| version 1, 3, 5 and 7 `Guid`s | v1 needs a MAC address and a clock, v3/v5 need MD5/SHA-1, v7 needs a clock; all of them are a library over `NewGuid`'s own primitive |
| `ToByteArray`, `new Guid(byte[])` | the byte order question of § 1 becomes visible the moment either exists, and neither is asked for |
| `"B"`, `"P"`, `"X"` formats | three more spellings of the same bytes |
| C#'s field-wise `CompareTo` order | § 1: teko orders by the bytes as they print. C#'s order is a documented trap; matching it would mean matching the in-memory byte swap too, and then `ToString` stops being a straight walk |
| a `Guid` on an `extern` | teko's sixteen-byte convention is not a C ABI |

## 7. The hooks, by module

| module | what it grows |
|---|---|
| `teko_wide.tk` | **one column**: the machine is general, as this page predicted, but the RETURN BUFFER was one name for the whole program (`tk_dec_retbuf`), so returning a `Guid` asked for `decimal.tk`. It is per type now — a fourth column of the wide set — and every handler that moves the bytes is untouched (D75) |
| `teko_ref.tk` | **one tag**, and C3's own hole closed with it: a `ref`/`out` parameter's name INSIDE the load or the store the deref pass writes is the ADDRESS, and nothing said so, so `ref decimal`/`out decimal` was refused `teko: a value of type decimal does not convert to uptr` on a line no source wrote. `Guid.TryParse(s, out g)` needs that road, and `tests/primitives_decimal_out.tk` proves it for `decimal` too |
| **`teko_guid.tk`** (new) | one `type_new`, one `tk_wide_add`, one `syntax_expr`/`syntax_stmt` pair for the type word, the rows of § 3 and § 4 in the primitive-member and primitive-operator tables, and `Guid.TryParse(s, out g)` parsed by hand — `out <name>` is no column those tables have, the same reason `Color.TryParse` is hand-parsed (`teko_enum.tk`). N9: `NewGuid`'s own row flips `TK_PMSOON` to `TK_PMSFUN` naming `tk_guid_newguid` |
| `teko_prim.tk` | **as built, four things** — this page's estimate of "nothing" was wrong (D75). C1's lowering crossed a receiver as `(i64) recv`, wrapped a returning call in a cast and cast an argument, all three of which are `tw_cast` on a sixteen-byte value; a WIDE receiver, argument and result now cross UNCAST. The cast refusal gained a BUILDER clause beside the reader, so a type that is written and read rather than constructed says so (§ 2). And `tk_prim_static_m` splits the member name out of `tk_prim_static`, for the one static this page parses by hand |
| `teko_typeof.tk` | nothing: the § 5 clause of that page already refuses every conversion |
| **`teko_time.tk`** | N9: no new registration, but its wrapped bundle resolver (C6, D90) answers a SECOND name, `<teko/entropy.tk>`, by host, rather than installing a second wrapper — three new small source-builder functions, one per host |
| `teko.tk` | `#include` and one `_init()` call |
| `lib/guid.tk` (new) | the parser, the formatter, the comparison and `Empty`: about 200 lines of ordinary teko over `ld8`/`st8` through `&g`. N9: `#include <teko/entropy.tk>` and `tk_guid_newguid()`, the version-4 layout over `tk_entropy_fill` |
| `lib/rt.tk`, `core_teko.mc`, `user.mc` | nothing |
| `.github/actions/windows-sysroot/action.yml` | N9: a second import library, `bcrypt.lib`, from a one-line `bcrypt.def` |
| `.github/workflows/ngen.yml` | N9: `{sysroot}/bcrypt.lib` added to both Windows legs' `[linker] args` |

## 8. What it costs in `mc limits`

| row | before | after | why |
|---|---|---|---|
| `types` | 14 | **15** | one `type_new` |
| `syntax` | 15 | **16** | `Guid.Empty` and `Guid.Parse` need the type word to open an expression. The table is keyed by NAME, so `syntax_expr("Guid")` and `syntax_stmt("Guid")` are one row between them |
| `alias` | 21 | **22** | **measured, and this page's "unmoved" was wrong**: `type_new` reserves the word in the very table `type_alias` uses (`alias_add`, mc's own hooks module), so `types` and `alias` move together and always have. It is mechanical, not a choice |
| `passes`, `intrin` | 15, 8 | unmoved | by design, as every page in this plan |

The machine module adds no row: `teko_wide.tk` is already derived, and a derived table
shadows the name it registers rather than consuming a `machines` slot.

**N9 (D91) moves none of these.** `Guid.NewGuid` flips an EXISTING row's kind, `TK_PMSOON`
to `TK_PMSFUN` — it registers no new `type_new`, no new `syntax_expr`/`syntax_stmt`, no new
alias. Measured: `passes`, `syntax`, `alias`, `types`, `intrin`, `on_stmt` and `syntax_type`
identical on base and head, all seven unmoved (D91's own proof).

## 9. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/primitives_guid.tk` | `Parse`/`ToString` round-trip in both cases and both formats; `Empty`; the six comparisons against a known ordering; a `Guid` through a local, a parameter, a return, a field, a global and a `Guid[]` element; a recursive `Guid` function, proving the sixteen-byte return buffer under nesting | `42` |
| `tests/primitives_guid_bytes.tk` | the sixteen bytes read back through `&g` with `ld8` match the documented order for a known text form — the layout has an oracle, not a comment | `42` |
| `tests/primitives_guid_tryparse.tk` | `TryParse` on a good "D", a good "N", a wrong length, a bad character and a misplaced dash; `Empty` written on every failure | `42` |
| `tests/primitives_guid_parse_bad.tk` | `Guid.Parse("nope")` | `70` |
| `tests/primitives_guid_newguid.tk` | 64 draws of `Guid.NewGuid()`: never `Guid.Empty`, the version nibble exactly `4` and the variant nibble one of `8`/`9`/`a`/`b` on every one, two consecutive draws never equal, `ToString`/`Parse` (both formats) round-trips the very value drawn | `42` |
| `tests/refuse/guid_newguid_const.tk` | `const Guid ID = Guid.NewGuid();` — a CALL is still no constant expression | refuse |

Every one is a whole program with `#include "../lib/guid.tk"`, returning `42` on success and
a small distinct number per failed assertion.

## 10. The crumb

### N3 — `Guid` (M) — **landed, D75**

The registration, the tables, `lib/guid.tk`, and the `NewGuid` refusal.
**Depends on `docs/specs/decimal.md`'s C3** for `teko_wide.tk` and the sixteen-byte value,
and on `docs/specs/datetime.md`'s C1 for the primitive-member and primitive-operator tables.
It depends on nothing else — in particular not on `decimal`'s arithmetic, so it can land
between C3 and C4.

**Gate, as run:** 84 fixtures passed and 102 refused as expected, 0 failed; `--dump-ast` of
all 170 pre-existing fixtures byte-identical; `FIXPOINT OK`; `mc limits` verdict `ok` with
`passes` and `intrin` unmoved. Eleven refuse fixtures landed beside the four that run, and
`tests/primitives_decimal_out.tk` is the fifth runnable one — the `ref`/`out` road this
crumb opened is proved for `decimal` as well as for `Guid`.

**Gate, as designed:** the four fixtures at their exit codes **on all five legs** — a sixteen-byte value
in a parameter and a return is what a single leg cannot prove — every other fixture
unchanged with `--dump-ast` byte-identical, `FIXPOINT OK`, `mc limits` verdict `ok` with
`passes` and `intrin` **not moved**, `sh scripts/check-docs.sh` green. **Owes:** a `Guid`
section in [types.md](../reference/types.md), the refusals in
[diagnostics.md](../reference/diagnostics.md), `lib/guid.tk` in
[runtime.md](../reference/runtime.md), the `NewGuid` row in
[not-yet.md](../reference/not-yet.md), and the new module in
[modules.md](../internals/modules.md).

### N9 — `Guid.NewGuid` (S) — **landed, D91**

One static row, one call into `lib/guid.tk`, one fixture asserting that two consecutive
`NewGuid()` values differ, that the version nibble is `4` and that the variant bits are
`10`. The entropy `extern` per host and the `bcrypt.def` in teko's Windows sysroot (§ 5) are
part of the crumb; it depended on N3 and on nothing outside this repository.

**Gate:** `sh scripts/fixtures.sh` clean, `FIXPOINT OK`, `sh scripts/check-docs.sh` green,
`mc limits` verdict `ok` with `passes` and `intrin` **not moved**, `--dump-ast` proof over
every base fixture. See D91 for the full measured numbers.

## 11. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **The ordering diverges from C#.** `a < b` answers by the printed bytes, C# by its fields. | § 1, deliberately, and written into `types.md` beside the type. Matching C# would mean matching its in-memory byte swap, which would make `ToString` a shuffle and `Parse` its inverse, for an ordering C# users are warned about anyway. The fixture pins teko's order with values that only pass under it. |
| **`Guid` is the second `TK_WIDE` type and the first that is not `decimal`.** If `teko_wide.tk` turned out to be `decimal`-shaped, this is where it shows. | That is a feature of the order, not a risk of it: N3 lands right after C3 and **before** `decimal`'s arithmetic, so the machine module is proved general while it is still small. The `_bytes.tk` fixture reads raw bytes through `&` rather than trusting an arithmetic result, which is `docs/specs/decimal.md` § 13's own technique. |
| **`NewGuid` on four legs only** — an `extern getentropy` alone would work on four of the five. | **Resolved, as landed.** All three host `extern`s and the `bcrypt.def` land together, in this one crumb, measured on all five `ngen.yml` legs. |
| **`Guid.NewGuid` and `DateTime.Now` need the same kind of thing.** | **Resolved, as landed.** N9 reuses C6's own wrapped bundle resolver (`teko_time.tk`) for a second name, `<teko/entropy.tk>`, rather than installing a second `lex_set_bundle` wrapper — the SAME mechanism, not merely the same shape. |
| **The include.** `#include "guid.tk"` is a build-time step C# does not have. | Same answer as `docs/specs/datetime.md` § 13: refuse the type word with the include named in the message, and leave the flip into `lib/rt.tk` open for the owner — it is a one-line change either way, and it is the same decision for `time.tk`, `decimal.tk`, `guid.tk` and `string.tk`, so it should be taken once for all four. |
