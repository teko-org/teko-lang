# `DateOnly`, `TimeOnly` and `DateTimeOffset`

**Designed, partly built: `DateOnly` (N4a) has landed, D54.** What it does today is
[datetime.md § `DateOnly`](../reference/datetime.md#dateonly), and the fixtures are
`tests/surface_dateonly.tk`, `tests/surface_dateonly_panic.tk` and the five under
`tests/refuse/`. `TimeOnly` (N4b) and `DateTimeOffset` (N5) are still design and nothing
more, so every sample that names one carries `// no-run`.

**Four amendments N4a made to this page as written**, each marked where it appears below:
`DateOnly` registers **no arithmetic operator at all** and its six comparisons lower to
`tk_ts_eq` … `tk_ts_ge` rather than to a `tk_do_*` family of wrappers (§ 3);
`ToString`/`Parse`/`TryParse` and `ToDateTime(TimeOnly)` are **out of N4a** — no primitive
here has a `str` member yet and the third needs `TimeOnly` (§ 4, § 5); the cast refusal
names the type's **own** reader, `` `.DayNumber` ``, and its own builder,
`new DateOnly(...)`, because the message is generated from a per-primitive column (§ 2);
and the fixtures landed under the names `tests/surface_dateonly*.tk`, beside the
`surface_*` family the rest of the tree uses (§ 7).

**This page is a proposed section of [datetime.md](datetime.md)**, not a design of its own.
That page designs `DateTime` and `TimeSpan` and lists
`DateOnly` and `TimeOnly` under "What stays out" with the reason *"C# has them; nothing asks
for them yet"*, and `DateTimeOffset` beside the time-zone database. Something asks for them
now — the owner's "types are missing" — and the answer is short, because all three are
**derived**: they add three registrations, one packing decision and a handful of rows to
tables that page already builds. It is kept in a file of its own only so that the two
branches do not conflict; when both are merged it should be folded in as `datetime.md` § 15
and this file deleted.

Everything not restated here is that page's: the primitive-member lowering table (§ 2), the
conversion clause (§ 5), the calendar in `lib/time.tk` (§ 6), the `"o"`/`"s"` formats (§ 7)
and the `mc` wall-clock ask (§ 8).

---

## 1. Representation

| type | width | align | kind | what it holds |
|---|---|---|---|---|
| `DateOnly` | 4 | 4 | `TK_SINT` | a day number, `0 .. 3652058`, from `0001-01-01` |
| `TimeOnly` | 8 | 8 | `TK_SINT` | ticks since midnight, `0 .. 863999999999` |
| `DateTimeOffset` | 16 | 16 | `TK_WIDE` | `+0` the UTC tick count as a `DateTime`, `+8` the offset in **minutes**, signed |

`DateOnly` and `TimeOnly` are C#'s own representations exactly — `DayNumber` is an `int`,
`Ticks` is a `long` — and neither needs a machine, a literal or an intrinsic, for the same
one-line reason `TimeSpan` does not.

`DateTimeOffset` is the one that does not fit. C# stores a `DateTime` plus a `short` of
offset minutes, and the two do not fit in eight bytes here: the ticks need 62 bits (the
`dateData` packing `datetime.md` § 1 documents) and an offset of `-14:00 .. +14:00` needs
another 12. Rather than shave the tick range, it becomes the **third `TK_WIDE` type**, moved
by the same `teko_wide.tk` `decimal` and `Guid` already use, by address and never in a
register pair. It therefore lands after `docs/specs/decimal.md`'s C3 and adds no machine
work of its own.

The `+0` half is a `DateTime` whose `Kind` bits are always `Utc`, which is C#'s own
invariant: a `DateTimeOffset` stores the instant in UTC and the offset separately, so two
values with different offsets that name the same instant are **equal** and compare equal.
`.DateTime` re-applies the offset and hands back an Unspecified `DateTime`; `.UtcDateTime`
hands back the `+0` half as it is.

## 2. The surface

**Legal.** The `DateOnly` half runs today (N4a); everything below it is still design.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    DateOnly d = new DateOnly(2024, 2, 29);
    if (d.Year != 2024) return 1;
    if (d.Month != 2) return 2;
    if (d.Day != 29) return 3;
    if (d.DayOfWeek != 4) return 4;                    // a Thursday
    if (d.AddDays(1).Month != 3) return 5;
    if (d.DayNumber != 738944) return 6;               // N4a: `ToString` is not taught yet
    if (DateOnly.FromDayNumber(738944) != d) return 7;
    if (DateOnly.FromDateTime(new DateTime(2024, 2, 29, 13, 45, 30)) != d) return 8;
    return 42;
}
```

```teko
// no-run
#include "time.tk"

i64 main() {
    DateOnly d = new DateOnly(2024, 2, 29);
    TimeOnly t = new TimeOnly(13, 45, 30);
    if (t.Hour != 13) return 7;
    if (t.Ticks != 495300000000) return 8;
    TimeSpan since = t - new TimeOnly(12, 45, 30);      // a TimeSpan
    if (since.TotalHours != 1.0) return 9;
    if (t.AddHours(11).Hour != 0) return 10;           // it wraps at midnight

    DateTime dt = d.ToDateTime(t);
    if (dt.Day != 29) return 11;
    if (DateOnly.FromDateTime(dt) != d) return 12;

    DateTimeOffset o = new DateTimeOffset(dt, TimeSpan.FromHours(0 - 3));
    if (o.Offset.TotalHours != 0.0 - 3.0) return 13;
    if (o.UtcDateTime.Hour != 16) return 14;           // 13:45 at -03:00 is 16:45 UTC
    if (o.ToString() != "2024-02-29T13:45:30.0000000-03:00") return 15;

    DateTimeOffset p = new DateTimeOffset(o.UtcDateTime, TimeSpan.Zero);
    if (o != p) return 16;                             // the same instant: equal
    return 42;
}
```

**Illegal, and what each one earns.**

```teko
// no-run
#include "time.tk"

i64 main() {
    DateOnly d = new DateOnly(2024, 2, 29);
    TimeOnly t = new TimeOnly(13, 45, 30);

    i64 n = d;                    // teko: a value of type DateOnly does not convert to i64
    DateOnly e = 5;               // teko: a value of type i64 does not convert to DateOnly
    DateTime x = d;               // teko: a value of type DateOnly does not convert to DateTime
    DateOnly f = d + t;           // teko: no operator `+` takes these operands
    i64 c = (i64) d;              // N4a: teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it
    DateOnly g = new DateOnly(2024, 2, 30);   // teko: that date does not exist  (exit 70)
    TimeOnly h = new TimeOnly(24, 0, 0);      // teko: that time of day does not exist  (exit 70)
    return 0;
}
```

| written | message |
|---|---|
| any of the three in a numeric slot | `teko: a value of type DateOnly does not convert to i64` |
| an integer in any of the three | `teko: a value of type i64 does not convert to DateOnly` |
| a `DateOnly` in a `DateTime` slot, or the reverse | `teko: a value of type DateOnly does not convert to DateTime` — `.ToDateTime(t)` and `DateOnly.FromDateTime(d)` are the two roads |
| `d + t` | ``teko: no operator `+` takes these operands`` — C# has no such operator either; `d.ToDateTime(t)` is the form |
| `(i64) d` written by hand | ``teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it`` — **as landed**: the message is built from the type's name, its own reader column and its constructor, so it names a member the type really has (D54) |
| a day that does not exist | `teko: that date does not exist`, exit 70 |
| an hour, minute or second out of range | `teko: that time of day does not exist`, exit 70 |
| an offset outside `-14:00 .. +14:00`, or not a whole minute | `teko: that UTC offset does not exist`, exit 70 |
| `DateTimeOffset.Now`, `UtcNow` | `teko: DateTimeOffset.Now is not taught yet` — the same wall clock `datetime.md` § 8 is blocked on |

## 3. Operators

Every row is a call into `lib/time.tk`; nothing is left to the core's own arithmetic, for
the reason `datetime.md` § 4 gives.

| written | lowered to | result |
|---|---|---|
| `d == d`, `!=`, `<`, `<=`, `>`, `>=` (`DateOnly`) | `tk_ts_eq` … `tk_ts_ge` (**as landed**: a day number is an ordinary non-negative `i64`, so the `TimeSpan` functions already answer this and six forwarding wrappers were not written) | `i64` 0/1 |
| `t == t` and the other five (`TimeOnly`) | `tk_to_eq` … `tk_to_ge` | `i64` 0/1 |
| `t - t` | `tk_to_sub` | `TimeSpan` — C#'s own, the elapsed time, never negative |
| `o == o` and the other five (`DateTimeOffset`) | `tk_dto_eq` … `tk_dto_ge` | `i64` 0/1, on the **instant**, offset ignored |
| `o - o` | `tk_dto_sub` | `TimeSpan` |
| `o + t`, `o - t` (`t` a `TimeSpan`) | `tk_dto_add`, `tk_dto_sub_ts` | `DateTimeOffset`, offset kept |
| `d + t`, `d - d`, `t + t` | refused | C# has none of the three |

`DateOnly` has no `-` in C# either: `d1.DayNumber - d2.DayNumber` is the form, and teko
keeps it rather than inventing an operator C# does not have.

## 4. The API

**`DateOnly`**

| static | instance |
|---|---|
| `DateOnly.MinValue`, `MaxValue` | `.Year` `.Month` `.Day` `.DayOfWeek` `.DayOfYear` `.DayNumber` — all `i64` |
| `new DateOnly(y, m, d)` | `.AddDays(i64)` `.AddMonths(i64)` `.AddYears(i64)` |
| `DateOnly.FromDayNumber(i64)` | `.ToDateTime(TimeOnly)` → `DateTime` — **N4b**, the argument's type is not registered yet |
| `DateOnly.FromDateTime(DateTime)` | `.CompareTo` `.Equals`; `.ToString()` is **not in N4a** |
| `DateOnly.Parse(str)`, `TryParse(str, out DateOnly)` — **not in N4a**, no primitive here has a `str` member yet ([not-yet.md](../reference/not-yet.md)) | |

**`TimeOnly`**

| static | instance |
|---|---|
| `TimeOnly.MinValue`, `MaxValue` | `.Hour` `.Minute` `.Second` `.Millisecond` `.Ticks` |
| `new TimeOnly(h, mi, s)`, `new TimeOnly(h, mi, s, ms)` | `.Add(TimeSpan)` `.AddHours(f64)` `.AddMinutes(f64)` — all **wrapping**, C#'s own |
| `new TimeOnly(i64 ticks)` | `.ToTimeSpan()` → `TimeSpan` |
| `TimeOnly.FromDateTime(DateTime)`, `FromTimeSpan(TimeSpan)` | `.IsBetween(TimeOnly, TimeOnly)` |
| `TimeOnly.Parse(str)`, `TryParse(str, out TimeOnly)` | `.CompareTo` `.Equals` `.ToString()` |

**`DateTimeOffset`**

| static | instance |
|---|---|
| `DateTimeOffset.MinValue`, `MaxValue`, `UnixEpoch` | `.Offset` → `TimeSpan`, `.TotalOffsetMinutes` → `i64` |
| `new DateTimeOffset(DateTime, TimeSpan)` | `.UtcDateTime` `.LocalDateTime` `.DateTime` → `DateTime` |
| `new DateTimeOffset(i64 ticks, TimeSpan)` | `.Year` … `.Millisecond`, of the **local** reading |
| `DateTimeOffset.FromUnixTimeSeconds(i64)`, `FromUnixTimeMilliseconds(i64)` | `.ToUnixTimeSeconds()`, `.ToUnixTimeMilliseconds()` |
| `DateTimeOffset.Parse(str)`, `TryParse(str, out DateTimeOffset)` | `.ToOffset(TimeSpan)`, `.CompareTo`, `.Equals`, `.ToString()` |
| `DateTimeOffset.Now`, `UtcNow` — **blocked** | |

`.LocalDateTime` is `.DateTime` under another name here: teko has no time-zone database
(`datetime.md` § 8), so "local" means "the offset this value carries" and nothing more. It
is kept because C# code reads it and because refusing it would be refusing a member that
does have an honest meaning; what it does **not** do — consult the host's zone — is stated
in its own documentation row.

## 5. Text

All three extend `datetime.md` § 7's two formats and add nothing new:

| type | `ToString()` | notes |
|---|---|---|
| `DateOnly` | `2024-02-29` | 10 characters; `"o"` and `"s"` are the same string. **Not in N4a**: the text crumb every primitive waits on |
| `TimeOnly` | `13:45:30.1234567` | 16 characters; `"s"` drops the fraction, giving `13:45:30` |
| `DateTimeOffset` | `2024-02-29T13:45:30.1234567-03:00` | `"o"`; `"s"` is the 19-character form with no offset, as C# does |

`Parse` accepts exactly what `ToString` writes, panics on anything else
(`teko: the string is not a date`, `teko: the string is not a time of day`), and `TryParse`
answers `0`/`1` and writes `MinValue` on failure — the same pair, the same reason.

## 6. What it costs in `mc limits`

| row | before | after | why |
|---|---|---|---|
| `types` | — | **+3** | three `type_new` calls in the compiler itself |
| `syntax` | — | **+3** | `DateOnly.MinValue` and its two siblings need the type word to open an expression |
| `alias`, `passes`, `intrin` | — | unmoved | rows in tables that exist, and functions in `lib/time.tk` |

## 7. Fixtures

| fixture | asserts | `expect-exit` |
|---|---|---|
| `tests/surface_dateonly.tk` (**landed**; the page said `primitives_dateonly.tk`) | `2024-02-29` built three ways and read back component by component; `DayOfWeek` and `DayOfYear`; `AddDays` over a month and a year boundary; `AddMonths` clamping `2024-01-31` to `2024-02-29`; `AddYears` `2000-02-29` to `2001-02-28`; `MinValue`/`MaxValue`; the six comparisons; `DayNumber` round-trip; `FromDateTime`; and the value through a local, a parameter, a return, a field, a global, both shapes of array element, a `ref`/`out` pointee and a closure capture — the set that proves the four-byte width. No `ToString`/`Parse` | `42` |
| `tests/primitives_timeonly.tk` | components; `Ticks`; `AddHours` wrapping past midnight in both directions; `t - t` as a `TimeSpan`; `IsBetween` across midnight; `ToTimeSpan`; the six comparisons; `ToString`/`Parse` in both formats | `42` |
| `tests/primitives_dto.tk` | a value through a local, a parameter, a return, a field, a global and an array element, and a recursive function proving the sixteen-byte return buffer; two values with different offsets naming one instant compare **equal**; `ToOffset` preserves the instant; the Unix conversions round-trip; `ToString`/`Parse` | `42` |
| `tests/surface_dateonly_panic.tk` (**landed**) | `new DateOnly(2024, 2, 30)`, after a page of checks that must not fire | `70` |
| `tests/refuse/dateonly_{from_int,to_i64,to_datetime,cast,plus_datetime}.tk` (**landed**) | the five compile-time refusals of § 2, each by its exact message and line | *refused* |
| `tests/primitives_timeonly_bad.tk` | `new TimeOnly(24, 0, 0)` | `70` |
| `tests/primitives_dto_bad_offset.tk` | `new DateTimeOffset(dt, TimeSpan.FromHours(15))` | `70` |

## 8. The crumbs

### N4a — `DateOnly` (M) — **landed**, D54

One `type_new` of FOUR bytes, one `syntax_expr`/`syntax_stmt` pair, sixteen member rows,
six operator rows and the `lib/time.tk` half that reads a day number as a civil date —
which `datetime.md`'s C2 had already written for `DateTime` and which this crumb **calls**
rather than copies. It cost the mechanism one thing the two eight-byte primitives did not:
the compiler's own casts became instructions (a sign extension and a narrowing store), and
the record that tells such a cast from a hand-written one had to be handed over at five
more in-place-replacement doors ([the internals note](../internals/primitives.md)).

### N4b — `TimeOnly` (M)

The second `type_new` of this crumb as it was first written: eight bytes of ticks since
midnight, the wrapping `Add*` family, `t - t` as a `TimeSpan`, `IsBetween`, and
`DateOnly.ToDateTime(TimeOnly)` — the one member N4a left out because the argument's type
did not exist yet. **Depends on `docs/specs/datetime.md`'s C2** and on nothing else.

**Gate (both):** the surface fixture at `42` and the panic one at `70`; every other
fixture unchanged with `--dump-ast` byte-identical; `FIXPOINT OK`; `mc limits` verdict `ok`
with `passes` and `intrin` **not moved**; `sh scripts/check-docs.sh` green.
**Owes:** the section in [types.md](../reference/types.md) and in
[datetime.md](../reference/datetime.md), the refusals in
[diagnostics.md](../reference/diagnostics.md), the functions in
[runtime.md](../reference/runtime.md), and the removal of the "left out" row in
`not-yet.md`.

### N5 — `DateTimeOffset` (M)

One `type_new` of sixteen bytes, the offset validation, the instant-based comparison, the
Unix conversions, `ToOffset` and the `"o"` format with its offset suffix. **Depends on
`docs/specs/datetime.md`'s C2 and on `docs/specs/decimal.md`'s C3** — the second only for
the sixteen-byte machine, which by then already carries `decimal` and `Guid`.

**Gate:** `primitives_dto.tk` at `42` **on all five legs** — a sixteen-byte value's ABI is
what a single leg cannot prove — `_bad_offset.tk` at `70`, and everything N4 gated on.
**Owes:** the section in types.md, diagnostics.md, runtime.md, the `Now`/`UtcNow` row in
[not-yet.md](../reference/not-yet.md), and the removal of the "left out" row in
`datetime.md` § 8.

## 9. Risks and law tensions

| tension | recommended resolution |
|---|---|
| **`DateTimeOffset` needs sixteen bytes and `datetime.md` promised its two types needed no machine.** | It does not break that promise, it inherits another page's: `teko_wide.tk` exists for `decimal` and already carries `Guid` by the time this lands, so N5 adds no machine work at all. It is also why N5 is a separate crumb from N4 — `DateOnly` and `TimeOnly` are free of that dependency and should not wait behind it. |
| **`.LocalDateTime` without a time-zone database.** | It means "the offset this value carries", it is documented as exactly that, and `DateTimeOffset.Now` — the one member that would need the host's zone — is refused by name and blocked on the same wall clock `datetime.md` § 8 asks for. Refusing the whole member would refuse something that has an honest meaning. |
| **`d + t` reads like it should work.** A `DateOnly` plus a `TimeOnly` is a `DateTime` to every reader. | C# has no such operator and `d.ToDateTime(t)` is its form, so the refusal is C#'s own, with the method named in the neighbouring row of the table. Adding the operator would be teko inventing surface, which D3 does not allow. |
| **Three more type words taken from every program.** `DateOnly`, `TimeOnly`, `DateTimeOffset`. | The same cost every `type_new` has, checked over the whole tree in each crumb's gate. They are C#'s spellings and long enough that a collision is unlikely; a collision found in the gate is the finding, not a reason to rename. |
| **This page and `datetime.md` will conflict** when both branches merge. | It is written as a standalone file for exactly that reason, it restates nothing that page owns, and folding it in as `datetime.md` § 15 and deleting this file is a mechanical step on whichever branch merges second. |

## 10. What the `mc` channel is asked

**Nothing new.** `DateTimeOffset.Now` and `UtcNow` are blocked on the wall clock
`datetime.md` § 14 already asks for, and nothing else on this page needs `mc` to change.
