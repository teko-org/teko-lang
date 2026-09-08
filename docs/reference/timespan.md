# `TimeSpan`

A length of time, C#'s own: a **signed 64-bit count of ticks**, and a tick is 100
nanoseconds. It is a value, not an object — eight bytes, no allocation, no reference count
— and it is a **type of its own**: no integer becomes a `TimeSpan` without being told to,
and a `TimeSpan` becomes no integer without `.Ticks`.

Everything on this page needs one include:

```
#include "time.tk"          // from a program next to lib/, or "../lib/time.tk"
```

A program that names `TimeSpan` without it is refused where the type word is used —
`teko: TimeSpan needs #include "time.tk" before it is used` — rather than at the link.

`DateTime` is [designed and not built](../specs/datetime.md); so are `TimeSpan.Parse`,
`.ToString()` and the two operators listed at the bottom of this page
([not-yet.md](not-yet.md)).

---

## Building one

| written | is |
|---|---|
| `new TimeSpan(ticks)` | that many ticks; the argument is an integer |
| `new TimeSpan()` | zero, C#'s parameterless value constructor |
| `TimeSpan.FromTicks(i64)` | the same, spelled as C#'s static |
| `TimeSpan.FromDays(f64)` | days, scaled to ticks |
| `TimeSpan.FromHours(f64)` `FromMinutes(f64)` `FromSeconds(f64)` `FromMilliseconds(f64)` | the same, per unit |
| `TimeSpan.Zero` `MinValue` `MaxValue` | the three constants |

The `From*` family takes an `f64`, and an integer written at one of them converts on the
spot ([types.md](types.md#f32-and-f64)): `TimeSpan.FromHours(2)` is `FromHours(2.0)`. The
value is scaled and **truncated toward zero**, at full double precision — .NET 7's own
behaviour, so `FromSeconds(0.0000001)` is one tick and not zero. A value that scales past
the range, and a NaN, panic: `teko: a time span is out of range`.

## Reading one

| member | type | is |
|---|---|---|
| `.Ticks` | `i64` | the whole span, in ticks |
| `.Days` `.Hours` `.Minutes` `.Seconds` `.Milliseconds` | `i64` | the **component**: `.Hours` is `0..23` |
| `.TotalDays` `.TotalHours` `.TotalMinutes` `.TotalSeconds` `.TotalMilliseconds` | `f64` | the **whole span** in that unit |
| `.Duration()` | `TimeSpan` | the absolute value |
| `.Negate()` | `TimeSpan` | the same as `-t` |
| `.CompareTo(TimeSpan)` | `i64` | `-1`, `0` or `1` |
| `.Equals(TimeSpan)` | `i64` | `1` when the ticks are equal |
| `TimeSpan.TicksPerDay` `TicksPerHour` `TicksPerMinute` `TicksPerSecond` `TicksPerMillisecond` | `i64` | the five scale constants |

`.Hours` against `.TotalHours` is the difference C# users get wrong and both fixtures
assert it: a span of 26 hours has `.Hours == 2` and `.TotalHours == 26.0`.

A negative span answers negative components (`-1 day - 2 hours` has `.Days == -1` and
`.Hours == -2`), which is C#'s rule.

A member is read-only: `t.Ticks = 5;` is `teko: a member of TimeSpan is read-only: Ticks`.

## The operators

| written | is | overflow |
|---|---|---|
| `a + b`, `a - b` | a `TimeSpan` | panic |
| `-a` | a `TimeSpan` | panic on `MinValue` |
| `a * n`, `n * a` (`n` an integer) | a `TimeSpan` | panic |
| `a / n` (`n` an integer) | a `TimeSpan` | panic; `/ 0` panics |
| `a == b`, `!=`, `<`, `<=`, `>`, `>=` | `i64`, 0 or 1 | — |

`t += a` and `t -= a` are the compound forms of the first row, rewritten to `t = t + a`
before the operator pass reads them, and they work.

Every one of them is a call into `lib/time.tk` ([runtime.md](runtime.md#the-time-library)), never
the core's raw arithmetic on the tick counts, which is what makes the overflow a panic
(`teko: a time span overflowed`, exit 70) instead of a wrap. Precedence is the core's own
and is not touched.

Anything else — `a % b`, `a & b`, `a * b`, `a / b`, `a + 1`, `~a`, `!a`, `+a` — is
``teko: no operator `X` takes these operands``.

## Conversions

There are none, in either direction. A `TimeSpan` in an `i64` or `f64` slot is
`teko: a value of type TimeSpan does not convert to i64`; an integer, a float, `null` or a
reference in a `TimeSpan` slot is `teko: a value of type i64 does not convert to TimeSpan`
and its siblings. The two spellings that DO cross are `.Ticks` (out) and
`new TimeSpan(ticks)` (in), and both are free: the same eight bytes under the other type,
with no instruction between them.

The nine slots this holds in are the nine [types.md](types.md#f32-and-f64) lists: an
initializer, an assignment, a `return`, an argument of a free, method, virtual or
interface call, an element of a `params` list, and a field store.

## A whole program

```teko
// expect-exit: 42
#include "time.tk"

class Meeting {
    public TimeSpan length;
    public Meeting(TimeSpan len) { length = len; }
    public i64 minutes() { return length.Minutes; }
}

// a `TimeSpan` parameter, a `TimeSpan` return: eight bytes, like any scalar
TimeSpan longer(TimeSpan a, TimeSpan b) {
    if (a > b) return a;
    return b;
}

i64 main() {
    TimeSpan day = TimeSpan.FromDays(1.0);
    if (day.Ticks != 864000000000) return 1;
    if (day.Ticks != TimeSpan.TicksPerDay) return 2;

    TimeSpan hour = TimeSpan.FromHours(1.0);
    TimeSpan half = TimeSpan.FromMinutes(30.0);
    TimeSpan both = hour + half;
    if (both.Ticks != 54000000000) return 3;
    if (both.Minutes != 30) return 4;              // the component
    if (both.TotalMinutes != 90.0) return 5;       // ...and the whole span

    TimeSpan long_day = day + TimeSpan.FromHours(2.0);
    if (long_day.Hours != 2) return 6;
    if (long_day.TotalHours != 26.0) return 7;

    if (longer(hour, half).Ticks != hour.Ticks) return 8;
    if ((hour * 3).Ticks != 108000000000) return 9;
    if ((3 * hour).Ticks != 108000000000) return 10;
    if ((day / 24).Ticks != hour.Ticks) return 11;
    if (-hour != TimeSpan.FromHours(0.0) - hour) return 12;
    if (hour.CompareTo(half) != 1) return 13;

    Meeting m = new Meeting(half);
    if (m.minutes() != 30) return 14;
    if (m.length.TotalSeconds != 1800.0) return 15;

    return 42;
}
```

## What a `TimeSpan` refuses

```teko
// no-run
#include "time.tk"

i64 main() {
    TimeSpan t = TimeSpan.FromHours(1.0);

    i64 n = t;                 // teko: a value of type TimeSpan does not convert to i64
    f64 x = t;                 // teko: a value of type TimeSpan does not convert to f64
    TimeSpan a = 5;            // teko: a value of type i64 does not convert to TimeSpan
    TimeSpan b = null;         // teko: a value of type uptr does not convert to TimeSpan
    TimeSpan c = t + 1;        // teko: no operator `+` takes these operands
    TimeSpan d = t * t;        // teko: no operator `*` takes these operands
    i64 y = t.Tickss;          // teko: unknown member of TimeSpan: Tickss
    i64 z = TimeSpan.Nope;     // teko: unknown static member of TimeSpan: Nope
    i64 w = TimeSpan.Ticks;    // teko: TimeSpan.Ticks is an instance member; reach it through an object
    i64 v = t.Zero.Ticks;      // teko: TimeSpan.Zero is static; reach it through its type
    t.Ticks = 5;               // teko: a member of TimeSpan is read-only: Ticks
    i64 u = t.Ticks();         // teko: the member is a property; it is not called: Ticks
    TimeSpan e = t.Duration;   // teko: the member is a method; call it with (): Duration
    TimeSpan f = TimeSpan.FromDays();   // teko: wrong number of arguments for FromDays
    TimeSpan g = new TimeSpan(1.5);     // teko: a value of type f64 does not convert to i64
    return 0;
}
```

## Where the value lives

A `TimeSpan` is eight bytes with an eight-byte alignment, so it goes everywhere a scalar
goes and costs the same: a local, a global, a parameter, a return, a field of a class or a
struct, an element of a fixed array or of a `T[]`, a `ref`/`out` pointee, a generic
argument. It is never reference counted, because there is no object to count
([memory.md](memory.md)).

Two places the type is not known are the two every scalar shares, and
[not-yet.md](not-yet.md) carries both: a **global** as the receiver of a `.`
(`teko: unknown member: Days`) and an **array element** as an operand (`xs[i] + t`, which
the core's own `+` then runs on the raw ticks, right in value and without the overflow
check). Bind either one to a local first.

## Under the hood

`TimeSpan` is a **primitive with members**: one `type_new("TimeSpan", 8, 8, TK_SINT)` and a
table of rows saying what each member lowers to — an ordinary call to an ordinary function
of `lib/time.tk`, over the raw `i64` ticks. There is no vtable, no run-time tag and no new
compiler pass; [the internals note](../internals/primitives.md) is the whole mechanism, and
[the spec](../specs/datetime.md) is where it was designed.
