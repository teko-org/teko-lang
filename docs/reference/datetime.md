# `DateTime`

A point in time, C#'s own: a **tick count from `0001-01-01 00:00:00`**, a tick being 100
nanoseconds, with a `Kind` (`Unspecified`, `Utc`, `Local`) carried in the value. It is
eight bytes — no allocation, no reference count — and a **type of its own**: no integer
becomes a `DateTime` without being told to, and a `DateTime` becomes no integer without
`.Ticks`.

The range is `0001-01-01 00:00:00.0000000` to `9999-12-31 23:59:59.9999999`, the calendar
is the proleptic Gregorian one C# uses, and everything below is exact — the arithmetic is
in ticks, never in floating point.

Everything on this page needs one include:

```
#include "time.tk"          // from a program next to lib/, or "../lib/time.tk"
```

A program that names `DateTime` without it is refused where the type word is used —
`teko: DateTime needs #include "time.tk" before it is used` — rather than at the link.
[`TimeSpan`](timespan.md) is the other half of the same page and the same include.

`DateTime.Now`, `UtcNow` and `Today` are **not** taught: they need a wall clock, which is
one symbol per operating system and `mc`'s to give ([the spec](../specs/datetime.md) § 8).
Each is refused by name. So are the text members (`ToString`, `Parse`) and everything
about time zones ([not-yet.md](not-yet.md)).

---

## Building one

| written | is |
|---|---|
| `new DateTime(y, m, d)` | midnight of that civil date, `Kind` `Unspecified` |
| `new DateTime(y, m, d, h, mi, s)` | ...with a time of day |
| `new DateTime(y, m, d, h, mi, s, ms)` | ...and a millisecond |
| `new DateTime(ticks)` | that many ticks since `0001-01-01`, `Kind` `Unspecified` |
| `new DateTime(ticks, kind)` | the same, with a `DateTimeKind` |
| `new DateTime()` | `DateTime.MinValue`, C#'s parameterless value constructor |
| `DateTime.MinValue` `MaxValue` | the two ends of the range |
| `DateTime.UnixEpoch` | `1970-01-01 00:00:00` Utc, `621355968000000000` ticks |

Every field is checked where the date is built, and a date that does not exist **panics**
(exit 70, [memory.md](memory.md)): `new DateTime(2023, 2, 29)` is
`teko: a date does not exist`, because 2023 is not a leap year. A year outside `1..9999`,
a month outside `1..12`, an hour outside `0..23`, a tick count outside the range and a
`Kind` outside `0..2` each have their own panic, listed in
[diagnostics.md](diagnostics.md).

## Reading one

| member | type | is |
|---|---|---|
| `.Ticks` | `i64` | the whole value in ticks, `Kind` masked off |
| `.Kind` | `DateTimeKind` | `Unspecified`, `Utc` or `Local` |
| `.Year` `.Month` `.Day` | `i64` | the civil date |
| `.Hour` `.Minute` `.Second` `.Millisecond` | `i64` | the time of day |
| `.DayOfWeek` | `i64` | `0` is Sunday, as C# counts |
| `.DayOfYear` | `i64` | `1..366` |
| `.Date` | `DateTime` | the same day at midnight, `Kind` kept |
| `.TimeOfDay` | `TimeSpan` | the time since that midnight |
| `.CompareTo(DateTime)` | `i64` | `-1`, `0` or `1`, by ticks |
| `.Equals(DateTime)` | `i64` | `1` when the ticks are equal |

A member is read-only: `d.Year = 5;` is `teko: a member of DateTime is read-only: Year`.

**`.Ticks` is not the raw eight bytes.** The `Kind` sits in the two bits above the tick
count (C#'s own `dateData` packing), so a `Local` date read as a number is negative — which
is why a hand-written cast is refused (below) and `.Ticks` is the only way out.

## Moving one

| written | is | out of range |
|---|---|---|
| `.AddDays(f64)` `.AddHours(f64)` `.AddMinutes(f64)` `.AddSeconds(f64)` `.AddMilliseconds(f64)` | a `DateTime`, `Kind` kept | panic |
| `.AddTicks(i64)` | the same, tick for tick | panic |
| `.AddMonths(i64)` `.AddYears(i64)` | the same, on the calendar | panic |

The five that take an `f64` are C#'s own: the value is scaled to milliseconds and
**rounded to the nearest one**, so `d.AddSeconds(0.0000001)` is `d`. An integer written at
one of them converts on the spot, `AddDays(1)` is `AddDays(1.0)`.

`AddMonths` **clamps the day to the length of the target month**, which is C#'s rule:
`2024-01-31` plus one month is `2024-02-29`, not a date that does not exist.
`AddYears(n)` is `AddMonths(n * 12)`, so `2000-02-29` plus one year is `2001-02-28`.
`AddMonths` takes `-120000..120000` and `AddYears` `-10000..10000`, C#'s own bounds.

## The calendar, without a value

| written | is |
|---|---|
| `DateTime.IsLeapYear(i64 year)` | `0` or `1`; the year is `1..9999` |
| `DateTime.DaysInMonth(i64 year, i64 month)` | `28`, `29`, `30` or `31` |

Both are the proleptic Gregorian rule and nothing else: `IsLeapYear(1900)` is `0`,
`IsLeapYear(2000)` is `1`.

## `DateTimeKind`

Three values under one word:

| written | is |
|---|---|
| `DateTimeKind.Unspecified` | `0`, and the default of every constructor that takes no kind |
| `DateTimeKind.Utc` | `1` |
| `DateTimeKind.Local` | `2` |

`DateTimeKind` is an **`enum`** — the ordinary one, declared in `lib/time.tk` as

```teko
// no-run
public enum DateTimeKind : i32 { Unspecified = 0, Utc = 1, Local = 2 }
```

so it has everything [an enum has](types.md#enum): `.ToString()`, `Parse`, `TryParse`,
`IsDefined`, `case DateTimeKind.Utc:` as a `switch` label, and the two explicit casts.
`DateTimeKind k = d.Kind;` is a declaration like any other, and **nothing else converts
into one**: `i64 n = d.Kind;` and `DateTimeKind k = 7;` are refused where the older alias
of `i32` took any integer, and so is `new DateTime(t, 7)`. Write `(DateTimeKind) 7` when
you mean it — an explicit cast into an enum is C#'s own, and the constructor's own range
check still panics on it.

Because the type is declared by the library file and not by the compiler, it needs
`#include "time.tk"` like everything else `lib/time.tk` carries; without it `DateTimeKind.Utc`
is refused as `teko: unknown member: Utc`, and a declaration such as `DateTimeKind k;` reaches
the core's own `expected ; after expression` ([diagnostics](diagnostics.md)).

The arithmetic **keeps** the `Kind` of the value it started from; the comparisons
**ignore** it. Two dates of different `Kind` and equal ticks are equal, which is C#'s own
`Compare`, and no conversion between kinds exists — that is a time-zone database, not a
language feature.

## The operators

| written | is | out of range |
|---|---|---|
| `a - b`, both `DateTime` | a `TimeSpan` | panic |
| `d + t`, `t + d` (`t` a `TimeSpan`) | a `DateTime` | panic |
| `d - t` | a `DateTime` | panic |
| `a == b`, `!=`, `<`, `<=`, `>`, `>=` | `i64`, 0 or 1 | — |

`d += t` and `d -= t` are the compound forms of the middle rows and work.

Every one of them is a call into `lib/time.tk`
([runtime.md](runtime.md#the-time-library)), never the core's raw arithmetic on the eight
bytes — which is what makes both the overflow (`teko: a date is out of range`, exit 70)
and the `Kind` bits correct. Precedence is the core's own and is not touched.

Anything else — `a + b` with two dates, `d * 2`, `d % 1`, `-d`, `d + 1` — is
``teko: no operator `X` takes these operands``.

## Conversions

There are none, in either direction. A `DateTime` in an `i64` or `f64` slot is
`teko: a value of type DateTime does not convert to i64`; an integer, a float, `null`, a
reference or a `TimeSpan` in a `DateTime` slot is
`teko: a value of type i64 does not convert to DateTime` and its siblings. The two
spellings that DO cross are `.Ticks` (out) and `new DateTime(ticks)` (in).

**A cast does not cross either.** `(i64) d` and `(DateTime) n` are refused where they are
written —
``teko: a DateTime does not cast; `.Ticks` reads it and `new DateTime(...)` builds it`` —
and so are `(i64) t` and `(TimeSpan) n` over a [`TimeSpan`](timespan.md). The reason is
this page's own: the raw bits of a date are not its ticks, and an integer that skipped
`new DateTime(t)` skipped the range check with it.

The nine slots this holds in are the nine [types.md](types.md#f32-and-f64) lists: an
initializer, an assignment, a `return`, an argument of a free, method, virtual or
interface call, an element of a `params` list, and a field store.

## A whole program

```teko
// expect-exit: 42
#include "time.tk"

class Release {
    public DateTime day;
    public Release(DateTime d) { day = d; }
    public i64 weekday() { return day.DayOfWeek; }
}

// a `DateTime` parameter, a `DateTime` return: eight bytes, like any scalar
DateTime earlier(DateTime a, DateTime b) {
    if (a < b) return a;
    return b;
}

i64 main() {
    DateTime leap = new DateTime(2024, 2, 29);
    if (leap.Ticks != 638447616000000000) return 1;
    if (leap.Year != 2024 || leap.Month != 2 || leap.Day != 29) return 2;
    if (leap.DayOfWeek != 4) return 3;             // a Thursday
    if (leap.DayOfYear != 60) return 4;

    DateTime next = leap.AddDays(1);
    if (next.Month != 3 || next.Day != 1) return 5;

    TimeSpan gap = next - leap;
    if (gap.Ticks != 864000000000) return 6;
    if (gap.TotalDays != 1.0) return 7;
    if ((leap + gap).Day != 1) return 8;

    DateTime jan31 = new DateTime(2024, 1, 31);
    if (jan31.AddMonths(1).Day != 29) return 9;    // C# clamps to the last day

    if (DateTime.IsLeapYear(1900) != 0) return 10;
    if (DateTime.DaysInMonth(2024, 2) != 29) return 11;
    if (DateTime.UnixEpoch.Ticks != 621355968000000000) return 12;
    if (DateTime.UnixEpoch.Kind != DateTimeKind.Utc) return 13;
    if (DateTime.MaxValue.Year != 9999) return 14;

    DateTime stamp = new DateTime(2024, 2, 29, 13, 45, 30);
    if (stamp.TimeOfDay.Hours != 13) return 15;
    if (stamp.Date.Ticks != leap.Ticks) return 16;
    if (stamp.Kind != DateTimeKind.Unspecified) return 17;

    if (earlier(next, leap).Day != 29) return 18;
    Release r = new Release(leap);
    if (r.weekday() != 4) return 19;

    return 42;
}
```

## What a `DateTime` refuses

```teko
// no-run
#include "time.tk"

i64 main() {
    DateTime d = new DateTime(2024, 2, 29);
    TimeSpan t = TimeSpan.FromHours(1.0);

    DateTime s = d + d;        // teko: no operator `+` takes these operands
    i64 n = d;                 // teko: a value of type DateTime does not convert to i64
    DateTime a = 5;            // teko: a value of type i64 does not convert to DateTime
    DateTime b = null;         // teko: a value of type uptr does not convert to DateTime
    DateTime c = t;            // teko: a value of type TimeSpan does not convert to DateTime
    i64 raw = (i64) d;         // teko: a DateTime does not cast; `.Ticks` reads it and `new DateTime(...)` builds it
    DateTime e = (DateTime) 5; // the same refusal, the other way
    i64 y = d.Yearr;           // teko: unknown member of DateTime: Yearr
    i64 z = DateTime.Epoch;    // teko: unknown static member of DateTime: Epoch
    DateTime f = DateTime.Now; // teko: DateTime.Now is not taught yet
    i64 w = DateTime.Year;     // teko: DateTime.Year is an instance member; reach it through an object
    i64 v = d.MinValue;        // teko: DateTime.MinValue is static; reach it through its type
    d.Year = 5;                // teko: a member of DateTime is read-only: Year
    i64 u = d.Year();          // teko: the member is a property; it is not called: Year
    DateTime g = d.AddDays;    // teko: the member is a method; call it with (): AddDays
    DateTime h = new DateTime(2024, 2, 29, 13);   // teko: wrong number of arguments for new
    return 0;
}
```

...and at run time, where the value decides:

```teko
// expect-exit: 70
#include "time.tk"

i64 main() {
    DateTime impossible = new DateTime(2023, 2, 29);   // teko: a date does not exist
    return impossible.Day;
}
```

## Where the value lives

A `DateTime` is eight bytes with an eight-byte alignment, so it goes everywhere a scalar
goes and costs the same: a local, a global, a parameter, a return, a field of a class or a
struct, an element of a fixed array or of a `T[]`, a `ref`/`out` pointee, a generic
argument. It is never reference counted, because there is no object to count
([memory.md](memory.md)).

A **global** is a receiver like any other since the oracle answers one by its
declaration (D48), and so is an **array element** since D50: an element's load carries the
element's own type, so `ds[i] + t` is claimed and checked like any other operand pair, and
an element of a `DateTimeKind` array is an argument like any other — `tests/surface_datetime.tk`
reads `ds[i] + t`, `ds[i] - ds[j]` and `new DateTime(1, ks[i])` on both shapes of array. It
was the one gap left here — with a typed operand beside it the site was refused, and with
an element on both sides the core's own arithmetic ran on the raw bits, `Kind` included.

## `DateOnly`

A **date with no time of day**, C#'s own: the count of days since `0001-01-01`, four bytes
and nothing else — no hour, no `Kind`. It lives behind the same include, it is a primitive
with members like `DateTime`, and the raw four bytes ARE `.DayNumber`, so that member is
the identity read `TimeSpan.Ticks` is.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    DateOnly leap = new DateOnly(2024, 2, 29);
    if (leap.DayNumber != 738944) return 1;
    if (leap.Year != 2024 || leap.Month != 2 || leap.Day != 29) return 2;
    if (leap.DayOfWeek != 4) return 3;              // a Thursday, 0 is Sunday
    if (leap.DayOfYear != 60) return 4;             // 31 + 29
    if (leap.AddDays(1).Month != 3) return 5;
    if (new DateOnly(2024, 1, 31).AddMonths(1).Day != 29) return 6;   // C# clamps
    if (new DateOnly(2000, 2, 29).AddYears(1).Day != 28) return 7;    // ...and here
    if (DateOnly.FromDayNumber(738944) != leap) return 8;
    if (DateOnly.FromDateTime(new DateTime(2024, 2, 29, 13, 45, 30)) != leap) return 9;
    if (DateOnly.MaxValue.DayNumber != 3652058) return 10;            // 9999-12-31
    if (!(leap < leap.AddDays(1))) return 11;
    return 42;
}
```

| what | is |
|---|---|
| `new DateOnly(y, m, d)` | the calendar constructor; a date that does not exist panics where it is built |
| `DateOnly.MinValue`, `MaxValue` | `0001-01-01` and `9999-12-31`, day `0` and day `3652058` |
| `DateOnly.FromDayNumber(n)`, `FromDateTime(d)` | the two other builders; the second drops the time of day and the `Kind` |
| `.DayNumber` | the four bytes themselves, as an `i64` |
| `.Year` `.Month` `.Day` `.DayOfWeek` `.DayOfYear` | the civil components, the same calendar a `DateTime` reads |
| `.AddDays(n)` `.AddMonths(n)` `.AddYears(n)` | C#'s three, the month clamp included: `2024-01-31` plus one month is `2024-02-29` |
| `.CompareTo(e)` `.Equals(e)` | `-1`/`0`/`1`, and `0`/`1` |
| `==` `!=` `<` `<=` `>` `>=` | the six comparisons, on the day number |

**There is no arithmetic operator on a `DateOnly`**, because C# declares none: `d + t` and
`d + 1` are ``teko: no operator `+` takes these operands``, `d - d` is the same sentence
with `-` in it, and `d1.DayNumber - d2.DayNumber` is how a difference in days is written —
there as here.
It converts to nothing either, in either direction: `i64 n = d;`, `DateOnly e = 5;` and
`DateTime x = d;` are each refused, and so is `(i64) d` —
``teko: a DateOnly does not cast; `.DayNumber` reads it and `new DateOnly(...)` builds it``.

`.ToString()` and `DateOnly.Parse` are not taught yet ([not-yet.md](not-yet.md)): the text
crumb every primitive waits on. `.ToDateTime(TimeOnly)` **is** — [`TimeOnly`](#timeonly)
below — the one member N4a left out because the argument's type did not exist yet.

**Four bytes, and that is the only thing that makes it different.** A `DateOnly` travels
every slot a scalar travels — a local, a parameter, a return, a field, a global, an element
of a fixed array or of a `T[]`, a `ref`/`out` pointee, a closure's captured copy — and the
compiler's own two casts around each lowering are a sign extension in and a narrowing store
out instead of nothing at all. `tests/surface_dateonly.tk` walks that whole set with the
largest day number there is.

## `TimeOnly`

A **time of day with no date**, C#'s own: the count of ticks since midnight,
`0 .. 863999999999` (one tick short of a full day), eight bytes and no `Kind` — a
`TimeOnly` behaves under the compiler exactly like `TimeSpan` and `DateTime` do, so no slot
it travels through needs the sign-extend/narrow pair `DateOnly`'s four bytes do.

```teko
// expect-exit: 42
#include "time.tk"

i64 main() {
    TimeOnly t = new TimeOnly(13, 45, 30);
    if (t.Hour != 13) return 1;
    if (t.Ticks != 495300000000) return 2;
    TimeSpan since = t - new TimeOnly(12, 45, 30);      // a TimeSpan, never negative
    if (since.TotalHours != 1.0) return 3;
    if (t.AddHours(11).Hour != 0) return 4;             // it wraps at midnight

    DateOnly d = new DateOnly(2024, 2, 29);
    DateTime dt = d.ToDateTime(t);
    if (dt.Day != 29) return 5;
    if (DateOnly.FromDateTime(dt) != d) return 6;
    if (TimeOnly.FromDateTime(dt) != t) return 7;
    return 42;
}
```

| what | is |
|---|---|
| `new TimeOnly(h, mi)`, `(h, mi, s)`, `(h, mi, s, ms)` | the calendar-free constructors; an hour/minute/second/millisecond out of range panics where it is built |
| `new TimeOnly(ticks)` | that many ticks since midnight, checked against the range |
| `TimeOnly.MinValue`, `MaxValue` | `00:00:00` and `23:59:59.9999999`, ticks `0` and `863999999999` |
| `TimeOnly.FromDateTime(d)` | the time-of-day half of a `DateTime`, `Kind` dropped |
| `TimeOnly.FromTimeSpan(ts)` | a raw `TimeSpan`'s ticks, checked against the same range |
| `.Hour` `.Minute` `.Second` `.Millisecond` | the components, `DateTime`'s own functions read directly |
| `.Ticks` | the eight bytes themselves, as an `i64` |
| `.Add(ts)` `.AddHours(f)` `.AddMinutes(f)` | C#'s **wrapping** family: past either end of the day, never a panic |
| `.ToTimeSpan()` | the same ticks under `TimeSpan` |
| `.IsBetween(a, b)` | `a` inclusive, `b` exclusive, and it **wraps** when `a` is after `b` (`22:00` to `02:00` covers midnight) |
| `.CompareTo(u)` `.Equals(u)` | `-1`/`0`/`1`, and `0`/`1` |
| `==` `!=` `<` `<=` `>` `>=` | the six comparisons, on the tick count |

**One arithmetic operator, and it is never negative.** `t1 - t2` answers the elapsed
`TimeSpan` from `t2` to `t1`, wrapping **forward** across midnight when `t1` is earlier than
`t2` — C#'s own rule for two times of day with no date attached. `t + t` has no row and is
refused, the same sentence `DateOnly`'s own arithmetic reaches:
``teko: no operator `+` takes these operands``. It converts to nothing either, in either
direction, exactly as `DateOnly` does not: `i64 n = t;`, `TimeOnly u = 5;` and
`DateTime x = t;` are each refused, and so is `(i64) t` —
``teko: a TimeOnly does not cast; `.Ticks` reads it and `new TimeOnly(...)` builds it``.

`.ToString()`, `TimeOnly.Parse` and `TryParse` are not taught yet ([not-yet.md](not-yet.md)):
the same text crumb `DateOnly` waits on.

## Under the hood

`DateTime` is a **primitive with members**, the mechanism [`TimeSpan`](timespan.md)
brought: one `type_new("DateTime", 8, 8, TK_SINT)` and a table of rows saying what each
member lowers to — an ordinary call to an ordinary function of `lib/time.tk`, over the raw
eight bytes. There is no vtable, no run-time tag and no new compiler pass;
[the internals note](../internals/primitives.md) is the whole mechanism, and
[the spec](../specs/datetime.md) is where it was designed. `DateOnly` is the same
mechanism one width down — `type_new("DateOnly", 4, 4, TK_SINT)` and sixteen rows plus a
seventeenth, `.ToDateTime(TimeOnly)`, added with N4b once the argument's type existed — and
`TimeOnly` is the mechanism again at the ORIGINAL width — `type_new("TimeOnly", 8, 8,
TK_SINT)`, twenty rows of its own and the `tk_to_*` half of `lib/time.tk`, registered
BEFORE `DateOnly` so that seventeenth row's own column reads a live id. Both are designed in
[datetime-extras.md](../specs/datetime-extras.md).

`DateTimeOffset` — a UTC instant plus its offset, sixteen bytes and the THIRD `TK_WIDE`
type after `decimal` and `Guid` — is registered in this same `tk_time_init()`, LAST, so its
own rows can name `DateTime` and `TimeSpan` as live ids; it is documented in full on
[the type reference](types.md#datetimeoffset) rather than here, since every wide type's own
page is there (N5, D76).
