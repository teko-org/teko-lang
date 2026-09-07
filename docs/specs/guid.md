# `Guid`

**Designed, not built.** Nothing on this page compiles today; every sample carries
`// no-run` for that reason. What runs is [the type reference](../reference/types.md), and
this page is kept apart from it on purpose ([the specs index](README.md)).

`Guid` is C#'s `System.Guid`: a 128-bit identifier, written as
`f81d4fae-7dec-11d0-a765-00a0c91e6bf6`, compared and ordered by value, with an all-zero
`Guid.Empty` and a random `Guid.NewGuid()`.

It is **the smallest page in this plan** because it invents nothing: the sixteen-byte value,
the machine module that moves it and the member-lowering table are all built by
`docs/specs/decimal.md` and `docs/specs/datetime.md`, and this page spends them. What it
adds is 16 bytes of layout, a hex parser, a hex formatter and a byte comparison — and one
blocked function.

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
#include "guid.tk"

i64 main() {
    Guid a = Guid.Parse("f81d4fae-7dec-11d0-a765-00a0c91e6bf6");
    Guid b = Guid.Parse("F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6");   // case-insensitive
    if (a != b) return 1;
    if (a == Guid.Empty) return 2;

    str s = a.ToString();                        // the "D" form, lowercase
    if (Guid.Parse(s) != a) return 3;
    if (a.ToString("N") != "f81d4fae7dec11d0a76500a0c91e6bf6") return 4;

    Guid z = Guid.Empty;
    if (z.ToString() != "00000000-0000-0000-0000-000000000000") return 5;
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
    Guid e = Guid.NewGuid();      // teko: Guid.NewGuid is not taught yet
    return 0;
}
```

| written | message |
|---|---|
| a `Guid` in any numeric slot | `teko: a value of type Guid does not convert to i64` |
| anything in a `Guid` slot | `teko: a value of type i64 does not convert to Guid` |
| `+ - * / % & \| ^ ~ << >>` | ``teko: no operator `+` takes these operands`` |
| `(i64) g`, `(Guid) n` written by hand | ``teko: a Guid does not cast; `.ToString()` writes it and `Guid.Parse(s)` reads it`` |
| an unknown member | `teko: unknown member of Guid`, `teko: unknown static member of Guid` |
| `Guid.NewGuid()` | `teko: Guid.NewGuid is not taught yet` (§ 5) |
| a `Guid` on an `extern` | ``teko: an `extern` takes no Guid`` — the sixteen-byte convention is teko's own and crosses no C boundary |
| a `Guid` as a `const` or a `switch` label | `teko: a const is an integer` — the folder has no 128-bit arithmetic, exactly as for `decimal` |

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
| `Guid.NewGuid()` — **blocked**, § 5 | `.IsEmpty` — an `i64` 0/1 |

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

## 5. `Guid.NewGuid` is blocked on `mc`

A version-4 `Guid` is sixteen bytes of **cryptographically random** data with the version
nibble set to `4` and the variant bits to `10`. The nibbles are arithmetic; the randomness
is not, and teko cannot get it:

| host | the name |
|---|---|
| Linux | `getrandom(2)`, or `/dev/urandom` |
| macOS | `getentropy(3)`, or `/dev/urandom` |
| Windows | `BCryptGenRandom` — `bcrypt.dll`, and **not** one of the kernel32 entry points `<sys>` binds |

`mc` has no conditional compilation, the five legs compile one source, and the Windows leg
resolves `<sys>`'s libc-shaped names through `mc`'s own runtime object, which is `mc`'s file
and not this repository's. Writing a second include per host, or a `[libs]`/`[externs]`
mapping in the leg config, would push an operating-system choice into every consumer's
`teko.toml` for one function — which is exactly the workaround
`docs/specs/datetime.md` § 8 refuses for the wall clock, for the same reasons.

**The ask is one function, and it goes to `mc`'s notices file beside the wall clock:**

```
i64 sys_random(uptr buf, i64 n);      // n bytes of entropy; 0 on success
```

one name across the three hosts, `getrandom`/`getentropy` under it on the two POSIX legs and
`BCryptGenRandom` on Windows. It unblocks on whichever `mc` release carries it. Until then
`Guid.NewGuid` is refused **by name**, and every other member of this page lands without it.

A `Guid` built from a counter, a clock or an address would compile and would be a wrong
answer — two processes would collide — so it is not the fallback. There is no fallback;
there is a refusal and a filed ask.

## 6. What stays out

| left out | why |
|---|---|
| `Guid.NewGuid` | § 5, blocked on `<sys>` |
| version 1, 3, 5 and 7 `Guid`s | v1 needs a MAC address and a clock, v3/v5 need MD5/SHA-1, v7 needs a clock; all of them are a library over `NewGuid`'s own primitive |
| `ToByteArray`, `new Guid(byte[])` | the byte order question of § 1 becomes visible the moment either exists, and neither is asked for |
| `"B"`, `"P"`, `"X"` formats | three more spellings of the same bytes |
| C#'s field-wise `CompareTo` order | § 1: teko orders by the bytes as they print. C#'s order is a documented trap; matching it would mean matching the in-memory byte swap too, and then `ToString` stops being a straight walk |
| a `Guid` on an `extern` | teko's sixteen-byte convention is not a C ABI |

## 7. The hooks, by module

| module | what it grows |
|---|---|
| `teko_wide.tk` | **nothing** — it already moves any `TK_WIDE` id; `Guid` is the second one it carries and proves the module is general rather than `decimal`-shaped |
| **`teko_guid.tk`** (new) | one `type_new`, one `syntax_expr` for the type word, and the rows of § 3 and § 4 in the primitive-member and primitive-operator tables |
| `teko_prim.tk` | nothing: the tables are `docs/specs/datetime.md` § 9's, and this page adds rows to them |
| `teko_typeof.tk` | nothing: the § 5 clause of that page already refuses every conversion |
| `teko.tk` | `#include` and one `_init()` call |
| `lib/guid.tk` (new) | the parser, the formatter, the comparison and `Empty`: about 200 lines of ordinary teko over `ld8`/`st8` through `&g` |
| `lib/rt.tk`, `core_teko.mc`, `user.mc` | nothing |

## 8. What it costs in `mc limits`

| row | before | after | why |
|---|---|---|---|
| `types` | — | **+1** | one `type_new` |
| `syntax` | — | **+1** | `Guid.Empty` and `Guid.Parse` need the type word to open an expression |
| `passes`, `intrin`, `alias` | — | unmoved | by design, as every page in this plan |

The machine module adds no row: `teko_wide.tk` is already derived, and a derived table
shadows the name it registers rather than consuming a `machines` slot.

## 9. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/primitives_guid.tk` | `Parse`/`ToString` round-trip in both cases and both formats; `Empty`; the six comparisons against a known ordering; a `Guid` through a local, a parameter, a return, a field, a global and a `Guid[]` element; a recursive `Guid` function, proving the sixteen-byte return buffer under nesting | `42` |
| `tests/primitives_guid_bytes.tk` | the sixteen bytes read back through `&g` with `ld8` match the documented order for a known text form — the layout has an oracle, not a comment | `42` |
| `tests/primitives_guid_tryparse.tk` | `TryParse` on a good "D", a good "N", a wrong length, a bad character and a misplaced dash; `Empty` written on every failure | `42` |
| `tests/primitives_guid_parse_bad.tk` | `Guid.Parse("nope")` | `70` |

Every one is a whole program with `#include "../lib/guid.tk"`, returning `42` on success and
a small distinct number per failed assertion.

## 10. The crumb

### N3 — `Guid` (M)

The registration, the tables, `lib/guid.tk`, and the `NewGuid` refusal.
**Depends on `docs/specs/decimal.md`'s C3** for `teko_wide.tk` and the sixteen-byte value,
and on `docs/specs/datetime.md`'s C1 for the primitive-member and primitive-operator tables.
It depends on nothing else — in particular not on `decimal`'s arithmetic, so it can land
between C3 and C4.

**Gate:** the four fixtures at their exit codes **on all five legs** — a sixteen-byte value
in a parameter and a return is what a single leg cannot prove — every other fixture
unchanged with `--dump-ast` byte-identical, `FIXPOINT OK`, `mc limits` verdict `ok` with
`passes` and `intrin` **not moved**, `sh scripts/check-docs.sh` green. **Owes:** a `Guid`
section in [types.md](../reference/types.md), the refusals in
[diagnostics.md](../reference/diagnostics.md), `lib/guid.tk` in
[runtime.md](../reference/runtime.md), the `NewGuid` row in
[not-yet.md](../reference/not-yet.md), and the new module in
[modules.md](../internals/modules.md).

### N9 — `Guid.NewGuid` (S, blocked)

One static row, one call into `lib/guid.tk`, one fixture asserting that two consecutive
`NewGuid()` values differ, that the version nibble is `4` and that the variant bits are
`10`. **Blocked** until `mc`'s `<sys>` carries § 5's entropy function on the three hosts.

## 11. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **The ordering diverges from C#.** `a < b` answers by the printed bytes, C# by its fields. | § 1, deliberately, and written into `types.md` beside the type. Matching C# would mean matching its in-memory byte swap, which would make `ToString` a shuffle and `Parse` its inverse, for an ordering C# users are warned about anyway. The fixture pins teko's order with values that only pass under it. |
| **`Guid` is the second `TK_WIDE` type and the first that is not `decimal`.** If `teko_wide.tk` turned out to be `decimal`-shaped, this is where it shows. | That is a feature of the order, not a risk of it: N3 lands right after C3 and **before** `decimal`'s arithmetic, so the machine module is proved general while it is still small. The `_bytes.tk` fixture reads raw bytes through `&` rather than trusting an arithmetic result, which is `docs/specs/decimal.md` § 13's own technique. |
| **`NewGuid` invites a workaround** — an `extern getentropy` would work on four of the five legs. | Refuse it. Four legs out of five is a silently wrong build on the fifth, and D2 says a construct `mc` cannot express is reported, never worked around. The refusal is by name and the ask is filed with the wall clock. |
| **`Guid.NewGuid` and `DateTime.Now` are blocked on the same kind of thing.** | They are one ask with two functions, and it should be sent as one: `<sys>` grows a wall clock and an entropy source. A release carrying either unblocks its own crumb independently. |
| **The include.** `#include "guid.tk"` is a build-time step C# does not have. | Same answer as `docs/specs/datetime.md` § 13: refuse the type word with the include named in the message, and leave the flip into `lib/rt.tk` open for the owner — it is a one-line change either way, and it is the same decision for `time.tk`, `decimal.tk`, `guid.tk` and `string.tk`, so it should be taken once for all four. |
